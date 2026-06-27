#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"
#include "StarMathCommon.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

bool GpuLightmapPass::processFull(ImageView const& emission, List<uint16_t> const& emissionHalf,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
    PointParameters const& params, float brightnessScale, bool tonemap, bool shadowCompare, float worldUpscale, Image* gpuResult) {
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
  m_renderer->beginGpuTimer("lighting.gpu.spread.gpu_us");
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
  m_renderer->endGpuTimer("lighting.gpu.spread.gpu_us");

  // --- Point: one blended per-light bbox quad on top of the spread result (in lastTarget). ---
  if (!lights.empty()) {
    m_renderer->switchEffectConfig("lightingPoint");   // flushes the final spread quad into lastTarget
    m_renderer->beginGpuTimer("lighting.gpu.point.gpu_us");
    uploadObstacle();   // lightingPoint has its own "obstacle" sampler -> upload again (R8)
    m_renderer->setEffectParameter("pointObstacleBoost", params.pointObstacleBoost);
    m_renderer->setRenderTarget(String(lastTarget), size);   // accumulate onto the spread result
    m_renderer->setBlendMode(params.pointAdditive ? BlendMode::Additive : BlendMode::Max);

    unsigned drawn = 0;
    for (auto const& light : lights) {
      // Match production: skip lights whose center is outside the grid.
      if (light.position[0] < 0 || light.position[0] > w - 1 || light.position[1] < 0 || light.position[1] > h - 1)
        continue;
      float maxIntensity = max(light.value[0], max(light.value[1], light.value[2]));
      float airReach = light.asSpread ? params.spreadMaxAir : params.pointMaxAir;
      float obstacleReach = light.asSpread ? params.spreadMaxObstacle : params.pointMaxObstacle;
      // 1B: the shader's air cull hard-caps reach at airReach regardless of intensity, so for
      // maxIntensity>1 the old bbox (maxIntensity*airReach) drew a wide ring of always-output-0
      // cells. Cap the rect to the cull radius -> byte-identical, fill drops ~maxIntensity^2.
      float maxRange = std::min(maxIntensity, 1.0f) * airReach;
      float lxmin = floor(std::max(0.0f, light.position[0] - maxRange));
      float lymin = floor(std::max(0.0f, light.position[1] - maxRange));
      float lxmax = ceil(std::min(w, light.position[0] + maxRange));
      float lymax = ceil(std::min(h, light.position[1] + maxRange));
      if (lxmax <= lxmin || lymax <= lymin)
        continue;
      m_renderer->setEffectParameter("lightPosition", Vec2F(light.position));
      m_renderer->setEffectParameter("lightValue", Vec3F(light.value));
      m_renderer->setEffectParameter("lightBeam", light.beam);
      m_renderer->setEffectParameter("lightAsSpread", light.asSpread);
      // 1A hoist: per-light constants the shader used to recompute per-fragment (cos/sin etc.).
      m_renderer->setEffectParameter("lightMaxIntensity", maxIntensity);
      m_renderer->setEffectParameter("beamDirection", Vec2F(std::cos(light.beamAngle), std::sin(light.beamAngle)));
      m_renderer->setEffectParameter("oneMinusBeamAmbience", 1.0f - light.beamAmbience);
      m_renderer->setEffectParameter("perBlockObstacleAtten", 1.0f / obstacleReach);
      m_renderer->setEffectParameter("perBlockAirAtten", 1.0f / airReach);
      m_renderer->render(renderFlatRect(RectF(lxmin, lymin, lxmax, lymax), Vec4B::filled(255), 0.0f));
      ++drawn;
    }
    m_renderer->endGpuTimer("lighting.gpu.point.gpu_us");
    m_renderer->setBlendMode(BlendMode::Alpha);   // restore before the compose + world draw
    pointLightsDrawn.inc(drawn);
  }

  // --- Compose: cap (brightnessLimit) the spread+point accumulation into the other buffer. ---
  char const* composeTarget = targets[spreadIterations % 2];   // != lastTarget
  m_renderer->switchEffectConfig("lightingPassthrough");        // flushes the final point quad
  m_renderer->beginGpuTimer("lighting.gpu.compose.gpu_us");
  m_renderer->setEffectParameter("applyCap", true);
  m_renderer->setEffectParameter("brightnessLimit", params.brightnessLimit);
  m_renderer->setEffectParameter("brightnessScale", brightnessScale);
  m_renderer->setEffectParameter("tonemap", tonemap);
  m_renderer->setEffectTextureFromTarget("inputTexture", lastTarget);
  m_renderer->setRenderTarget(String(composeTarget), size);
  m_renderer->render(fullQuad);
  m_renderer->endGpuTimer("lighting.gpu.compose.gpu_us");
  m_renderer->flush();

  if (shadowCompare && gpuResult)
    *gpuResult = m_renderer->readFrameBuffer(composeTarget);

  if (worldUpscale >= 1.5f) {
    // R-A Form 2: bicubic-upscale the composed lightmap once (<=30Hz) into a higher-res linear FBO;
    // the world pass then does a single bilinear tap of it (smooth, cheap).
    unsigned n = (unsigned)(worldUpscale + 0.5f);
    Vec2U upSize = size * n;
    m_renderer->switchEffectConfig("lightingUpscale");
    m_renderer->beginGpuTimer("lighting.gpu.upscale.gpu_us");
    m_renderer->setEffectTextureFromTarget("inputTexture", composeTarget);
    m_renderer->setRenderTarget(String("lightingGpuUpscaled"), upSize);
    m_renderer->render(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(upSize)), Vec4B::filled(255), 0.0f));
    m_renderer->endGpuTimer("lighting.gpu.upscale.gpu_us");
    m_renderer->flush();
    m_renderer->setRenderTarget({});
    m_renderer->switchEffectConfig("world");
    m_renderer->setEffectTextureFromTarget("lightMap", "lightingGpuUpscaled");
  } else {
    m_renderer->setRenderTarget({});
    m_renderer->switchEffectConfig("world");
    m_renderer->setEffectTextureFromTarget("lightMap", composeTarget);
  }
  return true;
}

}
