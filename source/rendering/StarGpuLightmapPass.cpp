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

  // L3: (re)build the persistent full-quad buffer only when the lightmap size changes.
  if (!m_fullQuadBuffer)
    m_fullQuadBuffer = m_renderer->createRenderBuffer();
  if (m_fullQuadSize != size) {
    List<RenderPrimitive> fullQuadPrims;
    fullQuadPrims.append(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f));
    m_fullQuadBuffer->set(fullQuadPrims);
    m_fullQuadSize = size;
  }
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

  // Each Jacobi iteration REPLACES its target -- it is a relaxation step, not an accumulation. It has been
  // getting that by accident: the ambient BlendMode::Alpha with the shader's hardcoded alpha=1.0 computes
  // dst = src*1 + dst*0, which is a replace by arithmetic coincidence. Say what we mean instead.
  //
  // Bit-identical (src_alpha is 1.0 on every spread fragment, so the blend was already a pure replace), and
  // it buys two things: the 33 iterations stop paying for a per-fragment blend op they never wanted, and the
  // alpha channel stops being load-bearing -- an alpha of 0 would previously have blended the fragment away
  // to nothing rather than writing it. That is what makes the obstacle flag able to live there (J-2).
  // It also removes a latent dst*0.0 = NaN hazard: lightingGpu is clear:false, so its first-frame contents
  // are undefined, and NaN*0 is NaN.
  m_renderer->setBlendMode(BlendMode::None);

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
    m_renderer->renderBuffer(m_fullQuadBuffer);
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

    // L5: resolve the 9 per-light uniform handles ONCE (current effect = lightingPoint), set by handle
    // in the loop below instead of re-hashing each name 162x/recompute.
    auto hLightPosition         = m_renderer->getEffectParameterHandle("lightPosition");
    auto hLightValue            = m_renderer->getEffectParameterHandle("lightValue");
    auto hLightBeam             = m_renderer->getEffectParameterHandle("lightBeam");
    auto hLightAsSpread         = m_renderer->getEffectParameterHandle("lightAsSpread");
    auto hLightMaxIntensity     = m_renderer->getEffectParameterHandle("lightMaxIntensity");
    auto hBeamDirection         = m_renderer->getEffectParameterHandle("beamDirection");
    auto hOneMinusBeamAmbience  = m_renderer->getEffectParameterHandle("oneMinusBeamAmbience");
    auto hPerBlockObstacleAtten = m_renderer->getEffectParameterHandle("perBlockObstacleAtten");
    auto hPerBlockAirAtten      = m_renderer->getEffectParameterHandle("perBlockAirAtten");

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
      m_renderer->setEffectParameter(hLightPosition, Vec2F(light.position));
      m_renderer->setEffectParameter(hLightValue, Vec3F(light.value));
      m_renderer->setEffectParameter(hLightBeam, light.beam);
      m_renderer->setEffectParameter(hLightAsSpread, light.asSpread);
      // 1A hoist: per-light constants the shader used to recompute per-fragment (cos/sin etc.).
      m_renderer->setEffectParameter(hLightMaxIntensity, maxIntensity);
      m_renderer->setEffectParameter(hBeamDirection, Vec2F(std::cos(light.beamAngle), std::sin(light.beamAngle)));
      m_renderer->setEffectParameter(hOneMinusBeamAmbience, 1.0f - light.beamAmbience);
      m_renderer->setEffectParameter(hPerBlockObstacleAtten, 1.0f / obstacleReach);
      m_renderer->setEffectParameter(hPerBlockAirAtten, 1.0f / airReach);
      m_renderer->render(renderFlatRect(RectF(lxmin, lymin, lxmax, lymax), Vec4B::filled(255), 0.0f));
      ++drawn;
    }
    m_renderer->endGpuTimer("lighting.gpu.point.gpu_us");
    pointLightsDrawn.inc(drawn);
  }

  // Restore the engine's ambient blend mode for the compose and the world draw that follow.
  //
  // UNCONDITIONAL, and it must be: this used to sit inside the `if (!lights.empty())` above, which was only
  // safe while the point pass was the ONLY thing that touched the blend mode -- set and restored inside the
  // same block. The spread now sets BlendMode::None outside that block, so a lights-empty frame would leave
  // blending DISABLED for the compose and every subsequent world draw. Restore where the state was changed
  // from, not where one of its changers happens to end.
  m_renderer->setBlendMode(BlendMode::Alpha);

  // --- Compose: cap (brightnessLimit) the spread+point accumulation into the other buffer. ---
  char const* composeTarget = targets[spreadIterations % 2];   // != lastTarget
  m_renderer->beginGpuTimer("lighting.gpu.compose.gpu_us");
  m_renderer->composite("lightingPassthrough", composeTarget, size, "inputTexture", lastTarget,
    {{"applyCap", true}, {"brightnessLimit", params.brightnessLimit},
     {"brightnessScale", brightnessScale}, {"tonemap", tonemap}, {"preserveAlpha", false}});
  m_renderer->endGpuTimer("lighting.gpu.compose.gpu_us");
  m_renderer->flush();

  if (shadowCompare && gpuResult)
    *gpuResult = m_renderer->readFrameBuffer(composeTarget);

  if (worldUpscale >= 1.5f && m_renderer->switchEffectConfig("lightingUpscale")) {
    // R-A Form 2: bicubic-upscale the composed lightmap once (<=30Hz) into a higher-res linear FBO;
    // the world pass then does a single bilinear tap of it (smooth, cheap). switchEffectConfig is
    // guarded in the condition: if the lightingUpscale effect is missing it returns false and we fall
    // through to the plain composeTarget bind below, instead of silently running the previously-bound
    // effect as a (bilinear) upscale -- the bug that made Form 2's first build show stepped shadow edges.
    unsigned n = (unsigned)(worldUpscale + 0.5f);
    Vec2U upSize = size * n;
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
