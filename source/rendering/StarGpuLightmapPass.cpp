#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"
#include "StarMathCommon.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

bool GpuLightmapPass::processFull(ImageView const& emission, ImageView const& obstacle,
    List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
    PointParameters const& params, bool shadowCompare, Image* gpuResult) {
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

  // --- Spread: K Jacobi iterations, NO cap (point lighting is blended on top before the cap). ---
  m_renderer->setEffectTexture("emission", emission);
  m_renderer->setEffectTexture("obstacle", obstacle);
  m_renderer->setEffectTexture("lightState", emission);   // iteration-0 light state == emission
  m_renderer->setEffectParameter("dropoffAir", 1.0f / params.spreadMaxAir);
  m_renderer->setEffectParameter("dropoffObstacle", 1.0f / params.spreadMaxObstacle);
  m_renderer->setEffectParameter("applyCap", false);

  char const* lastTarget = nullptr;
  for (unsigned i = 0; i < spreadIterations; ++i) {
    char const* target = targets[i % 2];
    m_renderer->setRenderTarget(String(target), size);
    if (i > 0)
      m_renderer->setEffectTextureFromTarget("lightState", lastTarget);
    m_renderer->render(fullQuad);
    lastTarget = target;
  }
  spreadPasses.inc(spreadIterations);

  // --- Point: one blended per-light bbox quad on top of the spread result (in lastTarget). ---
  if (!lights.empty()) {
    m_renderer->switchEffectConfig("lightingPoint");   // flushes the final spread quad into lastTarget
    m_renderer->setEffectTexture("obstacle", obstacle);
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
