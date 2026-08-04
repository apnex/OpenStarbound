#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"
#include "StarMathCommon.hpp"
#include "StarLogging.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

// Moved here from WorldPainter by #137. Byte-identical to the loop it replaces: same scan over the same
// buffer, same clamp, same order of operations.
//
// THE CLAMP IS A REACH TRUNCATION, AND IT WAS UNOBSERVABLE (#224). The spread shader is max-plus BFS,
// not a linear relaxation: iteration i can only inform cells i steps away, so the requested count is
// not a convergence guess -- it is exactly the propagation distance the scene needs. Whenever the
// request exceeds the cap, light is denied reach the pass itself asked for, and with the shipped
// spreadMaxAir and cap both 32 that begins the moment any emission CHANNEL exceeds 1.0. The gather
// sums four independent addends with no clamp and brightnessLimit is a downstream compose, so nothing
// upstream bounds it.
//
// lighting.gpu.spread.passes records the GRANTED count, so it saturates at the cap and is structurally
// blind to the shortfall. These two gauges record what it hides: the peak emission that drove the
// request, and the request itself. Truncation is exactly `passes_requested > passes`. Recording both
// rather than a boolean keeps the MAGNITUDE, which is what decides whether the cap should be raised,
// derived from brightnessLimit, or documented as a deliberate cost ceiling.
unsigned GpuLightmapPass::spreadIterationsFor(ImageView const& emission, LightmapParams const& lp) {
  static auto maxEmissionGauge = Telemetry::gauge("lighting.gpu.spread.max_emission_x1000",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  static auto passesRequestedGauge = Telemetry::gauge("lighting.gpu.spread.passes_requested",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});

  float maxEmission = 0.0f;
  float const* ed = (float const*)emission.data;
  size_t n = (size_t)emission.size[0] * emission.size[1] * 3;
  for (size_t i = 0; i < n; ++i)
    maxEmission = std::max(maxEmission, ed[i]);

  unsigned const requested = std::max(8u, (unsigned)std::ceil(maxEmission * lp.point.spreadMaxAir));
  // x1000 to keep the gauge integral, matching lighting.lights.max_intensity_x1000.
  maxEmissionGauge.set((int64_t)std::lround(maxEmission * 1000.0f));
  passesRequestedGauge.set((int64_t)requested);

  return std::min(lp.spreadIterationCap, requested);
}

LightmapResult GpuLightmapPass::processFull(ImageView const& emission, List<uint16_t> const& emissionHalf,
    ImageView const& obstacle, List<uint8_t> const& obstacleR8,
    List<ColoredCellularLightArray::PointLight> const& lights,
    LightmapParams const& lp, Image* gpuResult, int lightMapBorder) {
  // Unpacked into the names the ~200-line body already uses. Deliberate: grouping the arguments is the
  // point of the change, and rewriting every use site would have buried a boundary change inside a
  // rename diff. All six are read-only below (verified before landing), so aliasing them is safe.
  PointParameters const& params = lp.point;
  float const brightnessScale = lp.brightnessScale;
  bool const tonemap = lp.tonemap;
  bool const shadowCompare = lp.shadowCompare;
  float const worldUpscale = lp.worldUpscale;
  // The pass now derives its own K from the emission it was handed, rather than being told by a caller
  // that had to scan the pass's input to work it out.
  unsigned const spreadIterations = spreadIterationsFor(emission, lp);
  // Cadence::Call, not Frame: processFull is reached only inside `if (lightMapUpdated)`
  // (StarWorldPainter.cpp), so it fires on lightmap-publish frames, not every frame. Declared Frame it
  // measured 1099 of 1500 frames -- 73% coverage -- and the consumer scaled the total UP by 1.36x,
  // inventing cost for frames the pass genuinely did not run on. Reported 458 us/frame; actual 336.
  static auto cpuCostTimer = Telemetry::timer("lighting.gpu.cpu_cost.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  static auto spreadPasses = Telemetry::counter("lighting.gpu.spread.passes",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  static auto pointLightsDrawn = Telemetry::counter("lighting.gpu.point.lights",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  TelemetryScope cpuCostScope(cpuCostTimer);

  Vec2U size = emission.size;
  float w = (float)size[0], h = (float)size[1];
  if (size[0] == 0 || size[1] == 0 || spreadIterations == 0)
    return {};
  if (!m_renderer->switchEffectConfig("lightingSpread"))
    return {};   // assets missing -> caller falls back to CPU

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
  //
  // Emission goes up as RGBA16F, not RGB16F: the alpha carries this cell's OBSTACLE FLAG (1 = obstacle,
  // 0 = air). The spread shader writes that flag straight back out in its own alpha, so from iteration 1
  // onward a neighbour's light AND its obstacle-ness arrive in a SINGLE lightState tap. That is the whole
  // J-2 lever: the obstacle field is invariant across the 33 iterations, yet the old shader re-fetched all
  // eight neighbours' obstacle bits on every one of them -- 8 of 17 taps per texel spent re-reading a
  // constant, 33 times over.
  //
  // Iteration 0 is why the flag must live in EMISSION and not just in the lightmap: it aliases lightState to
  // the emission texture (no second upload), so if emission had no alpha, every cell would read as an
  // obstacle on the first pass.
  static uint16_t const HalfZero = 0x0000, HalfOne = 0x3C00;   // exact in fp16; 0/1 survive the round-trip
  size_t const texels = (size_t)size[0] * size[1];
  bool const packedEmission = emissionHalf.size() == texels * 3 && obstacleR8.size() == texels;

  // Owner=Gl (GPU work inside the GPU frame, closes against render.frame.gpu_span_us) but
  // cadence=Recompute: this fires per lightmap recompute, not per frame, so its COUNT is checked
  // against recomputes, not frames. Same for point/compose/upscale below.
  m_renderer->gpuTimer().begin("lighting.gpu.spread.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Recompute, MetricRole::Budget});
  if (packedEmission) {
    m_emissionRGBA.resize(texels * 4);
    for (size_t i = 0; i < texels; ++i) {
      m_emissionRGBA[i * 4 + 0] = emissionHalf[i * 3 + 0];
      m_emissionRGBA[i * 4 + 1] = emissionHalf[i * 3 + 1];
      m_emissionRGBA[i * 4 + 2] = emissionHalf[i * 3 + 2];
      // Reproduce the shader's own test exactly: it did `texture(obstacle, uv).r > 0.5` on an R8 texture,
      // i.e. byte/255 > 0.5.
      m_emissionRGBA[i * 4 + 3] = (obstacleR8[i] / 255.0f > 0.5f) ? HalfOne : HalfZero;
    }
    m_renderer->setEffectTextureHalf("emission", size, m_emissionRGBA.ptr(), 4);
  } else {
    m_renderer->setEffectTexture("emission", emission);
  }
  uploadObstacle();   // still needed by the point pass, and by the oracle's reference leg
  m_renderer->setEffectParameter("dropoffAir", 1.0f / params.spreadMaxAir);
  m_renderer->setEffectParameter("dropoffObstacle", 1.0f / params.spreadMaxObstacle);
  m_renderer->setEffectParameter("applyCap", false);

  // Each Jacobi iteration REPLACES its target -- it is a relaxation step, not an accumulation. It used to get
  // that by accident (ambient BlendMode::Alpha + a hardcoded alpha of 1.0 computes dst = src*1 + dst*0). Now
  // stated. This is also what makes the alpha channel usable at all: under alpha blending, a fragment with
  // alpha=0 blends away to NOTHING instead of being written, so a 0/1 flag in alpha would silently delete
  // every air cell. It further removes a latent dst*0.0 = NaN hazard on the clear:false target.
  m_renderer->setBlendMode(BlendMode::None);

  // One spread solve. `fromAlpha` selects where a neighbour's obstacle-ness comes from: its lightState alpha
  // (the J-2 path, 9 taps/texel) or the separate obstacle sampler (the original, 17 taps). Returns the target
  // holding the result. Only `fromAlpha` differs between the two -- everything else is shared, so the oracle
  // below cannot accidentally compare two different algorithms.
  auto runSpread = [&](bool fromAlpha) -> char const* {
    m_renderer->setEffectParameter("obstacleInAlpha", fromAlpha);
    char const* last = nullptr;
    for (unsigned i = 0; i < spreadIterations; ++i) {
      char const* target = targets[i % 2];
      m_renderer->setRenderTarget(String(target), size);
      if (i == 0)
        // iteration-0 light state == emission: alias the already-uploaded emission texture into the
        // lightState sampler instead of uploading the same grid a second time (per-frame upload cut).
        m_renderer->setEffectTextureAlias("lightState", "emission");
      else
        m_renderer->setEffectTextureFromTarget("lightState", last);
      m_renderer->renderBuffer(m_fullQuadBuffer);
      last = target;
    }
    spreadPasses.inc(spreadIterations);
    return last;
  };

  // THE LIGHTING BIT-IDENTITY ORACLE (/lighting spreadoracle on; default off, devOnly surface).
  //
  // Run the spread BOTH ways in the SAME frame, on the SAME inputs, and pixel-compare. In-frame is the whole
  // point: the harness's cross-run frame hash is worthless here because the unpaused load phase lets the sim
  // diverge, and lightingGpuShadowCompare (GPU vs CPU reference) is permanently red for a known unrelated
  // reason -- it can detect a change but cannot certify identity. This can.
  //
  // It exists because J-2's identity is NOT provable by argument: setEffectTextureFromTarget binds a
  // framebuffer's texture object directly and never applies the effect's declared filtering, so `lightState`
  // samples with lightingGpu's LINEAR filter even though lightingSpread.config says "nearest". A linear tap
  // that lands fractionally off a texel centre would interpolate the alpha flag (0.5 between obstacle and
  // air) where the genuinely-nearest obstacle sampler reads a hard 0 or 1. Whether the coordinates are exact
  // is an empirical question, and this is what answers it.
  bool const oracle = packedEmission && m_renderer->hasFrameBuffer("lightingRef");
  char const* lastTarget = nullptr;
  if (oracle) {
    char const* refResult = runSpread(false);   // reference: obstacle from its own sampler
    // Park the reference result where the second solve cannot overwrite it (the solve ping-pongs across both
    // lighting targets). A passthrough with no cap/scale/tonemap and preserveAlpha is an exact 1:1 copy.
    m_renderer->composite("lightingPassthrough", "lightingRef", size, "inputTexture", refResult,
      {{"applyCap", false}, {"brightnessLimit", 1.0f}, {"brightnessScale", 1.0f},
       {"tonemap", false}, {"preserveAlpha", true}});
    m_renderer->switchEffectConfig("lightingSpread");

    lastTarget = runSpread(true);               // candidate: obstacle from lightState alpha

    float maxAbsDiff = 0.0f;
    auto d = m_renderer->oracle().compare("lightingRef", lastTarget, &maxAbsDiff);
    if (d.first == 0)
      Logger::info("[spreadoracle] MATCH (0 diff) iterations={} size={}x{}", spreadIterations, size[0], size[1]);
    else
      Logger::info("[spreadoracle] DIFF={} maxAbs={:.6f} first=({},{}) iterations={} size={}x{}",
          d.first, maxAbsDiff, d.second[0], d.second[1], spreadIterations, size[0], size[1]);
  } else {
    lastTarget = runSpread(packedEmission);
  }
  m_renderer->gpuTimer().end("lighting.gpu.spread.gpu_us");

  // --- Point: one blended per-light bbox quad on top of the spread result (in lastTarget). ---
  if (!lights.empty()) {
    m_renderer->switchEffectConfig("lightingPoint");   // flushes the final spread quad into lastTarget
    m_renderer->gpuTimer().begin("lighting.gpu.point.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Recompute, MetricRole::Budget});
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
    m_renderer->gpuTimer().end("lighting.gpu.point.gpu_us");
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
  m_renderer->gpuTimer().begin("lighting.gpu.compose.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Recompute, MetricRole::Budget});
  m_renderer->composite("lightingPassthrough", composeTarget, size, "inputTexture", lastTarget,
    {{"applyCap", true}, {"brightnessLimit", params.brightnessLimit},
     {"brightnessScale", brightnessScale}, {"tonemap", tonemap}, {"preserveAlpha", false}});
  m_renderer->gpuTimer().end("lighting.gpu.compose.gpu_us");
  m_renderer->flush();

  if (shadowCompare && gpuResult)
    *gpuResult = m_renderer->oracle().read(composeTarget);

  if (worldUpscale >= 1.5f && m_renderer->switchEffectConfig("lightingUpscale")) {
    // R-A Form 2: bicubic-upscale the composed lightmap once (<=30Hz) into a higher-res linear FBO;
    // the world pass then does a single bilinear tap of it (smooth, cheap). switchEffectConfig is
    // guarded in the condition: if the lightingUpscale effect is missing it returns false and we fall
    // through to the plain composeTarget bind below, instead of silently running the previously-bound
    // effect as a (bilinear) upscale -- the bug that made Form 2's first build show stepped shadow edges.
    unsigned n = (unsigned)(worldUpscale + 0.5f);
    Vec2U upSize = size * n;
    m_renderer->gpuTimer().begin("lighting.gpu.upscale.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Recompute, MetricRole::Budget});
    m_renderer->setEffectTextureFromTarget("inputTexture", composeTarget);
    m_renderer->setRenderTarget(String("lightingGpuUpscaled"), upSize);
    m_renderer->render(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(upSize)), Vec4B::filled(255), 0.0f));
    m_renderer->gpuTimer().end("lighting.gpu.upscale.gpu_us");
    m_renderer->flush();
    m_renderer->setRenderTarget({});
    m_renderer->switchEffectConfig("world");
    m_renderer->setEffectTextureFromTarget("lightMap", "lightingGpuUpscaled");
  } else {
    m_renderer->setRenderTarget({});
    m_renderer->switchEffectConfig("world");
    m_renderer->setEffectTextureFromTarget("lightMap", composeTarget);
  }
  // Report the K actually run alongside the border, for the same reason the border travels with `active`:
  // the caller's parity diagnostic must reference the exact iteration count this pass used, and deriving
  // it twice is how the two come to disagree.
  return {true, lightMapBorder, spreadIterations};
}

}
