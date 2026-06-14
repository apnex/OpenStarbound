#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"
#include "StarMathCommon.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

bool GpuLightmapPass::processFull(ImageView const& emission, List<uint16_t> const& emissionHalf,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
    PointParameters const& params, float brightnessScale, bool shadowCompare, Image* gpuResult) {
  static auto cpuCostTimer = Telemetry::timer("lighting.gpu.cpu_cost.us");
  static auto spreadPasses = Telemetry::counter("lighting.gpu.spread.passes");
  static auto pointLightsDrawn = Telemetry::counter("lighting.gpu.point.lights");
  TelemetryScope cpuCostScope(cpuCostTimer);

  Vec2U size = emission.size;
  float w = (float)size[0], h = (float)size[1];
  if (size[0] == 0 || size[1] == 0 || spreadIterations == 0)
    return false;
  if (!m_renderer->switchEffectConfig("lightingSpread"))
    return false;   // assets missing -> caller falls back to CPU

  auto fullQuad = renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f);
  char const* targets[2] = {"lightingGpu", "lightingGpuB"};

  // Upload the obstacle mask to the current effect's "obstacle" sampler as R8 (a third the bytes of
  // RGB24) from the lighting-thread-extracted mask; fall back to RGB24 if absent. Used by BOTH the
  // spread and point effects (each has its own sampler), so this runs once per effect.
  auto uploadObstacle = [&]() {
    if (obstacleR8.size() == (size_t)size[0] * size[1])
      m_renderer->setEffectTextureR8("obstacle", size, obstacleR8.ptr());
    else
      m_renderer->setEffectTexture("obstacle", obstacle);
  };

  // --- Spread: K Jacobi iterations, NO cap (point lighting is blended on top before the cap). ---
  // Upload emission as RGB16F from the lighting-thread-converted half buffer (half the per-frame
  // transfer); fall back to the RGB_F upload if the half buffer is absent/mismatched.
  if (emissionHalf.size() == (size_t)size[0] * size[1] * 3)
    m_renderer->setEffectTextureHalfRGB("emission", size, emissionHalf.ptr());
  else
    m_renderer->setEffectTexture("emission", emission);
  uploadObstacle();
  m_renderer->setEffectParameter("dropoffAir", 1.0f / params.spreadMaxAir);
  m_renderer->setEffectParameter("dropoffObstacle", 1.0f / params.spreadMaxObstacle);
  m_renderer->setEffectParameter("applyCap", false);

  char const* lastTarget = nullptr;
  for (unsigned i = 0; i < spreadIterations; ++i) {
    char const* target = targets[i % 2];
    m_renderer->setRenderTarget(String(target), size);
    if (i == 0)
      // iteration-0 light state == emission: alias the already-uploaded emission texture into the
      // lightState sampler instead of uploading the same grid a second time (per-frame upload cut).
      m_renderer->setEffectTextureAlias("lightState", "emission");
    else
      m_renderer->setEffectTextureFromTarget("lightState", lastTarget);
    m_renderer->render(fullQuad);
    lastTarget = target;
  }
  spreadPasses.inc(spreadIterations);

  // --- Point: one blended per-light bbox quad on top of the spread result (in lastTarget). ---
  if (!lights.empty()) {
    m_renderer->switchEffectConfig("lightingPoint");   // flushes the final spread quad into lastTarget
    uploadObstacle();   // lightingPoint has its own "obstacle" sampler -> upload again (R8)
    m_renderer->setEffectParameter("pointMaxAir", params.pointMaxAir);
    m_renderer->setEffectParameter("pointMaxObstacle", params.pointMaxObstacle);
    m_renderer->setEffectParameter("spreadMaxAir", params.spreadMaxAir);
    m_renderer->setEffectParameter("spreadMaxObstacle", params.spreadMaxObstacle);
    m_renderer->setEffectParameter("pointObstacleBoost", params.pointObstacleBoost);
    m_renderer->setRenderTarget(String(lastTarget), size);   // accumulate onto the spread result
    m_renderer->setBlendMode(params.pointAdditive ? BlendMode::Additive : BlendMode::Max);

    unsigned drawn = 0;
    for (auto const& light : lights) {
      // Match production: skip lights whose center is outside the grid.
      if (light.position[0] < 0 || light.position[0] > w - 1 || light.position[1] < 0 || light.position[1] > h - 1)
        continue;
      float maxIntensity = max(light.value[0], max(light.value[1], light.value[2]));
      float maxRange = maxIntensity * (light.asSpread ? params.spreadMaxAir : params.pointMaxAir);
      float lxmin = floor(std::max(0.0f, light.position[0] - maxRange));
      float lymin = floor(std::max(0.0f, light.position[1] - maxRange));
      float lxmax = ceil(std::min(w, light.position[0] + maxRange));
      float lymax = ceil(std::min(h, light.position[1] + maxRange));
      if (lxmax <= lxmin || lymax <= lymin)
        continue;
      m_renderer->setEffectParameter("lightPosition", Vec2F(light.position));
      m_renderer->setEffectParameter("lightValue", Vec3F(light.value));
      m_renderer->setEffectParameter("lightBeam", light.beam);
      m_renderer->setEffectParameter("lightBeamAngle", light.beamAngle);
      m_renderer->setEffectParameter("lightBeamAmbience", light.beamAmbience);
      m_renderer->setEffectParameter("lightAsSpread", light.asSpread);
      m_renderer->render(renderFlatRect(RectF(lxmin, lymin, lxmax, lymax), Vec4B::filled(255), 0.0f));
      ++drawn;
    }
    m_renderer->setBlendMode(BlendMode::Alpha);   // restore before the compose + world draw
    pointLightsDrawn.inc(drawn);
  }

  // --- Compose: cap (brightnessLimit) the spread+point accumulation into the other buffer. ---
  char const* composeTarget = targets[spreadIterations % 2];   // != lastTarget
  m_renderer->switchEffectConfig("lightingPassthrough");        // flushes the final point quad
  m_renderer->setEffectParameter("applyCap", true);
  m_renderer->setEffectParameter("brightnessLimit", params.brightnessLimit);
  m_renderer->setEffectParameter("brightnessScale", brightnessScale);
  m_renderer->setEffectTextureFromTarget("inputTexture", lastTarget);
  m_renderer->setRenderTarget(String(composeTarget), size);
  m_renderer->render(fullQuad);
  m_renderer->flush();

  if (shadowCompare && gpuResult)
    *gpuResult = m_renderer->readFrameBuffer(composeTarget);

  m_renderer->setRenderTarget({});
  m_renderer->switchEffectConfig("world");
  m_renderer->setEffectTextureFromTarget("lightMap", composeTarget);
  return true;
}

}
