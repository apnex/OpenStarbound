#include "StarWorldPainter.hpp"
#include "StarAnimation.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarAssets.hpp"
#include "StarJsonExtra.hpp"
#include "StarTelemetry.hpp"
#include "StarLogging.hpp"  // LogMap (/debug HUD per-pass GPU timings, Rung 0)
#include "StarCellularLightArray.hpp"  // spreadJacobiReference (CPU spread oracle for parity)

namespace Star {

// GPU-lighting FULL parity shadow-compare (diagnostics only, Slice 3). The GPU result (spread +
// point + cap) is calc-region sized; the CPU lightMap is the full (spread+point+cap) query-region
// result. With point lighting now on BOTH sides this is apples-to-apples in ANY scene. Crop the
// GPU result to the query sub-rect (offset = border on each axis) and compare per channel.
// Tolerance allows RGBA16F + additive float-add order + the DDA-vs-Xiaolin-Wu residual: mean abs
// <= 3/255, max <= 10/255.
static void shadowCompareFull(Image const& gpuCalc, Lightmap const& cpuQuery, int border,
    ImageView const& emission, ImageView const& obstacle,
    List<ColoredCellularLightArray::PointLight> const& lights, PointParameters const& params, unsigned iterations) {
  static auto mismatchCounter = Telemetry::counter("lighting.gpu.point.mismatch");
  unsigned qw = cpuQuery.width(), qh = cpuQuery.height();
  Vec2U gpuSize = gpuCalc.size();
  if (qw == 0 || qh == 0 || border < 0 || gpuSize[0] < qw + 2 * border || gpuSize[1] < qh + 2 * border)
    return;

  float const meanTol = 3.0f / 255.0f, maxTol = 10.0f / 255.0f;
  double sumAbs = 0.0;
  float worst = 0.0f;
  unsigned worstX = 0, worstY = 0;
  Vec3F worstCpu, worstGpu;
  float const* gpuData = (float const*)gpuCalc.data();
  for (unsigned y = 0; y < qh; ++y) {
    for (unsigned x = 0; x < qw; ++x) {
      Vec3F cpu = cpuQuery.get(x, y);
      size_t g = ((size_t)(y + border) * gpuSize[0] + (x + border)) * 3;
      Vec3F gpu(gpuData[g], gpuData[g + 1], gpuData[g + 2]);
      for (size_t c = 0; c < 3; ++c) {
        float e = std::fabs(cpu[c] - gpu[c]);
        sumAbs += e;
        if (e > worst) { worst = e; worstX = x; worstY = y; worstCpu = cpu; worstGpu = gpu; }
      }
    }
  }
  float mean = (float)(sumAbs / (qw * qh * 3));
  if (mean > meanTol || worst > maxTol) {
    mismatchCounter.inc(1);
    static int warnBudget = 8;   // rate-limited; the counter carries the running total
    if (warnBudget > 0) {
      --warnBudget;
      // 3-way localizer: compute the CPU reference (proven spreadJacobiReference + pointLightingReference
      // on the SAME exported inputs) at the worst cell. production==reference but GPU differs => GLSL
      // port bug at this geometry; reference==GPU but production differs => export/reference issue.
      Vec3F ref(-1, -1, -1);
      size_t w = emission.size[0], h = emission.size[1];
      if (w == gpuSize[0] && h == gpuSize[1] && emission.size == obstacle.size) {
        List<Vec3F> em; em.resize(w * h);
        List<uint8_t> ob; ob.resize(w * h);
        float const* eData = (float const*)emission.data;
        uint8_t const* oData = (uint8_t const*)obstacle.data;
        for (size_t x = 0; x < w; ++x)
          for (size_t y = 0; y < h; ++y) {
            size_t px = (y * w + x) * 3;
            em[x * h + y] = Vec3F(eData[px], eData[px + 1], eData[px + 2]);
            ob[x * h + y] = oData[px] > 127 ? 1 : 0;
          }
        auto spread = spreadJacobiReference(em, ob, w, h, SpreadParameters{params.spreadMaxAir, params.spreadMaxObstacle, params.brightnessLimit}, iterations);
        auto full = pointLightingReference(spread, ob, lights, w, h, params);
        ref = full[((size_t)worstX + border) * h + ((size_t)worstY + border)];
      }
      Logger::warn("GPU lighting full parity: mean={:.4f}/255 max={:.4f}/255 at ({},{}) cpu=({:.3f},{:.3f},{:.3f}) gpu=({:.3f},{:.3f},{:.3f}) ref=({:.3f},{:.3f},{:.3f})",
          mean * 255.0f, worst * 255.0f, worstX, worstY,
          worstCpu[0], worstCpu[1], worstCpu[2], worstGpu[0], worstGpu[1], worstGpu[2], ref[0], ref[1], ref[2]);
    }
  }
}

WorldPainter::WorldPainter() {
  m_assets = Root::singleton().assets();

  m_camera.setScreenSize({800, 600});
  m_camera.setCenterWorldPosition(Vec2F());
  m_camera.setPixelRatio(Root::singleton().configuration()->get("zoomLevel").toFloat());

  m_highlightConfig = m_assets->json("/highlights.config");
  for (auto p : m_highlightConfig.get("highlightDirectives").iterateObject())
    m_highlightDirectives.set(EntityHighlightEffectTypeNames.getLeft(p.first), {p.second.getString("underlay", ""), p.second.getString("overlay", "")});

  m_entityBarOffset = jsonToVec2F(m_assets->json("/rendering.config:entityBarOffset"));
  m_entityBarSpacing = jsonToVec2F(m_assets->json("/rendering.config:entityBarSpacing"));
  m_entityBarSize = jsonToVec2F(m_assets->json("/rendering.config:entityBarSize"));
  m_entityBarIconOffset = jsonToVec2F(m_assets->json("/rendering.config:entityBarIconOffset"));
  m_preloadTextureChance = m_assets->json("/rendering.config:preloadTextureChance").toFloat();
}

void WorldPainter::renderInit(RendererPtr renderer) {
  m_assets = Root::singleton().assets();

  m_renderer = std::move(renderer);
  auto textureGroup = m_renderer->createTextureGroup(TextureGroupSize::Large);
  m_textPainter = make_shared<TextPainter>(m_renderer, textureGroup);
  m_tilePainter = make_shared<TilePainter>(m_renderer);
  m_drawablePainter = make_shared<DrawablePainter>(m_renderer, make_shared<AssetTextureGroup>(textureGroup));
  m_environmentPainter = make_shared<EnvironmentPainter>(m_renderer);
}

void WorldPainter::setCameraPosition(WorldGeometry const& geometry, Vec2F const& position) {
  m_camera.setWorldGeometry(geometry);
  m_camera.setCenterWorldPosition(position);
}

WorldCamera& WorldPainter::camera() {
  return m_camera;
}

void WorldPainter::update(float dt) {
  m_environmentPainter->update(dt);
}

LightmapResult WorldPainter::runGpuLightmapPass(WorldRenderData& renderData) {
  auto config = Root::singleton().configuration();
  if (!(config->get("lightingGpu").optBool().value(false) && renderData.lightingInputsValid))
    return {};   // GPU lighting off / inputs invalid -> caller uses the CPU lightMap
  // Slice 3: compute the COMPLETE lightmap on the GPU (spread + per-light point + cap) from the exported
  // emission/obstacle/point-light grids; processFull restores the world effect + binds the result as lightMap,
  // and returns {active=false} (caller falls back to CPU) if assets are missing.
  if (!m_gpuLightmapPass)
    m_gpuLightmapPass = make_shared<GpuLightmapPass>(m_renderer.get());
  auto lc = m_assets->json("/lighting.config:lighting");
  PointParameters params{
      lc.getFloat("pointMaxAir"), lc.getFloat("pointMaxObstacle"),
      lc.getFloat("pointObstacleBoost"),
      config->get("newLighting").optBool().value(true),   // pointAdditive (matches lightingCalc)
      lc.getFloat("spreadMaxAir"), lc.getFloat("spreadMaxObstacle"),
      lc.getFloat("brightnessLimit")};
  // Auto-scale spread Jacobi iterations to the emission's peak intensity. Production's Gauss-Seidel sweep
  // propagates fully in 2 sweeps; a parallel Jacobi needs ~ceil(maxIntensity * spreadMaxAir) steps to reach the
  // same distance (the de-risk's K bound). A fixed K under-propagated bright (>1.0) FU spread lights ->
  // dimmer-far-from-source cells. Scan the (small) emission for its max channel; clamp [8, cfg spread cap].
  float maxEmission = 0.0f;
  {
    auto const& em = renderData.lightingEmission;
    float const* ed = (float const*)em.data();
    size_t n = (size_t)em.size()[0] * em.size()[1] * 3;
    for (size_t i = 0; i < n; ++i)
      maxEmission = std::max(maxEmission, ed[i]);
  }
  unsigned cap = config->get("lightingGpuSpreadIterations").optUInt().value(64);
  unsigned iterations = std::min(cap, std::max(8u, (unsigned)std::ceil(maxEmission * params.spreadMaxAir)));
  bool shadowCompare = config->get("lightingGpuShadowCompare").optBool().value(false);
  Image gpuResult;
  float brightnessScale = config->get("lightingGpuBrightness").optFloat().value(1.0f);
  bool tonemap = config->get("lightingTonemap").optBool().value(false);
  LightmapResult lm = m_gpuLightmapPass->processFull(renderData.lightingEmission, renderData.lightingEmissionHalf,
      renderData.lightingObstacle, renderData.lightingObstacleR8, renderData.lightingPointLights,
      iterations, params, brightnessScale, tonemap, shadowCompare,
      config->get("lightingWorldUpscale").optFloat().value(1.0f), &gpuResult, renderData.lightMapBorder);
  // shadowCompare (diagnostics only): parity-check the GPU result against the CPU reference. It uses the border
  // the active result carries (lm.border == renderData.lightMapBorder), which now travels IN the result.
  if (lm.active && shadowCompare && gpuResult.size()[0] > 0)
    shadowCompareFull(gpuResult, renderData.lightMap, lm.border,
        renderData.lightingEmission, renderData.lightingObstacle, renderData.lightingPointLights, params, iterations);
  return lm;
}

void WorldPainter::render(WorldRenderData& renderData, function<bool()> lightWaiter) {
  m_camera.setScreenSize(m_renderer->screenSize());
  m_camera.setTargetPixelRatio(Root::singleton().configuration()->get("zoomLevel").toFloat());

  m_assets = Root::singleton().assets();

  m_tilePainter->setup(m_camera, renderData);

  // Stars, Debris Fields, Sky, and Orbiters

  // RENDER-PASS ABLATION MASK (task #141). The per-pass GL_TIME_ELAPSED timers are NOT ADDITIVE -- with 12 of
  // them their sum overshot the real frame by 5ms, because each bracket serialises the pipeline and measures
  // its own stall. They RANK passes; they cannot BUDGET them. The only honest way to cost a pass is to REMOVE
  // it and re-measure the whole frame. This mask does that, in a LIVE world (a frozen world is required for the
  // byte-identity gate but measures a floor, not real play -- and worse, a frozen+unthrottled world saturates
  // the GPU and manufactures pipeline stalls that do not exist in real play).
  // A SET bit RENDERS its pass; a clear bit ABLATES it. Only the passes listed here exist -- do not document a
  // bit that nothing reads, or setting it measures an un-ablated frame and reports the pass as free, which is
  // precisely the measurement lie this tool was built to prevent.
  //   bit0 env/sky   bit1 parallax
  // Base 0 (NOT 10): the mask is written in hex, and strtol(...,10) parses "0xF" as 0 -- silently ablating every
  // pass, the exact inverse of what was asked for.
  static int const PassAll = 0x3;
  static int const passMask = []() {
    char const* e = getenv("STAR_RENDERTEST_PASS_MASK");
    return e && *e ? (int)strtol(e, nullptr, 0) : PassAll;
  }();
  bool const ablateEnv      = !(passMask & 1);
  bool const ablateParallax = !(passMask & 2);
  // An ablated run must never be mistaken for a normal one -- say so, once.
  if (passMask != PassAll) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      Logger::warn("[rendertest] PASS ABLATION ACTIVE mask={:#x}: env={} parallax={}",
          passMask, ablateEnv ? "ABLATED" : "on", ablateParallax ? "ABLATED" : "on");
    }
  }

  // A renderer config reload (setMainHDR / setMultiSampling -- ClientApplication polls the hdr and
  // antiAliasing client options EVERY frame) destroys and re-creates every framebuffer with UNDEFINED
  // content. Our retained clear:false caches cannot see that: their keys (size, camera, counter) are all
  // unchanged across it, so they would happily composite undefined GPU memory for up to N frames. Drop both
  // caches on a generation bump. Costs one integer compare per frame.
  uint64_t fbGeneration = m_renderer->frameBufferGeneration();
  if (fbGeneration != m_cacheFrameBufferGeneration) {
    m_cacheFrameBufferGeneration = fbGeneration;
    m_envCache.invalidate();
    m_parallaxCache.invalidate();
  }

  // Did the env cache do its heavy redraw this frame? Read by the parallax refresh arbiter below, which
  // defers a purely time-gated parallax refresh off any frame env is already redrawing on.
  bool envRefreshedThisFrame = false;

  // Use a fixed pixel ratio for certain things.
  float pixelRatioBasis = m_camera.screenSize()[1] / 1080.0f;
  float starAndDebrisRatio = lerp(0.0625f, pixelRatioBasis * 2.0f, m_camera.pixelRatio());
  float orbiterAndPlanetRatio = lerp(0.125f, pixelRatioBasis * 3.0f, m_camera.pixelRatio());

  // Environment-cache probe (kubebound GPU-floor campaign). The env pass changes only slowly (star
  // twinkle + orbital drift) yet re-renders every frame at 1.6-7.6ms. Render it into its OWN persistent
  // screen-sized HDR FBO "envCache" (a clear:false mirror of "main") only every N frames, and composite
  // that cache into "main" every frame. envRefreshInterval N==1 (default) refreshes every frame and is
  // bit-identical to the direct-into-main path it replaces. Live: /rendercache envrefresh <N>.
  Vec2U envScreenSize = m_renderer->screenSize();
  float envPixelRatio = m_camera.pixelRatio();
  unsigned envRefreshInterval = Root::singleton().configuration()->get("envRefreshInterval", 1).optUInt().value(1);
  if (envRefreshInterval < 1)
    envRefreshInterval = 1;
  bool envOracle = Root::singleton().configuration()->get("envOracle", false).optBool().value(false);
  bool parallaxOracle = Root::singleton().configuration()->get("parallaxOracle", false).optBool().value(false);
  // NB: the oracles' reference surfaces (envRef, parallaxRef) are marked devOnly and are only ALLOCATED while
  // an oracle is armed -- ClientApplication::render does that before the frame starts, because it reloads the
  // framebuffer set and must not run mid-frame. Both oracle paths below are already guarded by hasFrameBuffer,
  // so they correctly no-op on the frame where the surfaces have not appeared yet.

  // The env draw sequence, shared by every path (AA-direct, cache-refresh, oracle reference) so the cache
  // and its bit-identity reference can never silently diverge -- a hand-duplicated copy that drifted would
  // make the oracle lie. Draws into whatever render target / effect is currently bound.
  auto drawEnv = [&]() {
    if (ablateEnv) return;
    m_environmentPainter->renderStars(starAndDebrisRatio, Vec2F(m_camera.screenSize()), renderData.skyRenderData);
    m_environmentPainter->renderDebrisFields(starAndDebrisRatio, Vec2F(m_camera.screenSize()), renderData.skyRenderData);
    if (renderData.skyRenderData.type != SkyType::Atmosphereless)
      m_environmentPainter->renderBackOrbiters(orbiterAndPlanetRatio, Vec2F(m_camera.screenSize()), renderData.skyRenderData);
    m_environmentPainter->renderPlanetHorizon(orbiterAndPlanetRatio, Vec2F(m_camera.screenSize()), renderData.skyRenderData);
    m_environmentPainter->renderSky(Vec2F(m_camera.screenSize()), renderData.skyRenderData);
    m_environmentPainter->renderFrontOrbiters(orbiterAndPlanetRatio, Vec2F(m_camera.screenSize()), renderData.skyRenderData);
    if (renderData.skyRenderData.type == SkyType::Atmosphereless)
      m_environmentPainter->renderBackOrbiters(orbiterAndPlanetRatio, Vec2F(m_camera.screenSize()), renderData.skyRenderData);
  };

  // Direct path (byte-identical stock env->main) when the cache is effectively off: envRefreshInterval<=1
  // with the oracle disarmed -- N=1 is a true zero-overhead "off", no per-frame compose. N=1 with the oracle
  // ARMED still takes the cache path so the bit-identity gate can run. Invalidate the cache so re-entering it
  // force-refreshes instead of compositing stale content.
  //
  // THE ANTI-ALIASING GATE IS GONE, AND IT SHOULD HAVE GONE WHEN WE FIXED THE CAUSE. It read
  // `!envAntiAliasing && ...`, on the rationale that '"main"/"envCache" are multisample -> render-to-cache +
  // a sampler composite is invalid under MSAA'. That was true when multisampling was forced onto EVERY
  // framebuffer -- a sampled GL_TEXTURE_2D_MULTISAMPLE binds as GL_INVALID_OPERATION and reads whatever was
  // on the texture unit before. We fixed that at the cause: multisampling is now a per-framebuffer opt-in,
  // and ONLY "main" opts in -- because only "main" is MSAA-RESOLVED to the screen (glBlitFramebuffer) and is
  // never sampled. envCache is single-sample, so rendering into it and sampling it back is valid under AA.
  // The gate was a symptom patch that outlived its symptom, and it was silently costing every AA player the
  // whole env-cache lever.
  bool envCacheActive = (envRefreshInterval > 1 || envOracle);
  if (!envCacheActive) {
    m_envCache.invalidate();
    m_renderer->gpuTimer().begin("render.pass.environment.gpu_us");
    drawEnv();
    m_renderer->gpuTimer().end("render.pass.environment.gpu_us");
  } else {
    // Cache path (AA off). Force a refresh on the first frame + after any resize (envCache is realloc'd
    // to undefined content) so a skip frame never composites garbage. The pixelRatio term is NOT redundant
    // with the size term: drawEnv scales stars/debris/orbiters by camera pixelRatio (starAndDebrisRatio /
    // orbiterAndPlanetRatio above), so a ZOOM change alters the cached image at an unchanged screen size --
    // without it, zooming left the sky stale until the counter next came round.
    static auto envRefreshed = Telemetry::counter("render.cache.env.refreshed");
    static auto envSkipped = Telemetry::counter("render.cache.env.skipped");
    bool envInvalidated = m_envCache.invalidated(envScreenSize, envPixelRatio);
    // cadenceHit is called UNCONDITIONALLY (not short-circuited behind envInvalidated) so the frame counter
    // advances every active frame -- exactly the old separate `++m_envRefreshCounter;` statement, which ran
    // even when the modulo test was skipped. Folding it into `envInvalidated || cadenceHit(...)` would stop
    // advancing the counter on invalidation frames and silently shift the N-cadence phase afterwards.
    bool envCadence = m_envCache.cadenceHit(envRefreshInterval);
    bool refreshEnv = envInvalidated || envCadence;
    (refreshEnv ? envRefreshed : envSkipped).inc(1);
    envRefreshedThisFrame = refreshEnv;

    m_renderer->gpuTimer().begin("render.pass.environment.gpu_us");
    if (refreshEnv) {
      // Redirect the env draws from "main" into the cache. setScreenSize now records screen-sized FBO
      // textureSize, so envCache is not reallocated mid-frame (which would discard content); the passed
      // size still drives the first-frame / post-resize (re)alloc + the viewport.
      m_renderer->setRenderTarget(m_envCache.name(), envScreenSize);
      // clear:false FBO -> clear manually to main's clear color (0,0,0,1), matching the direct path's
      // once-per-frame startFrame clear (also stops alpha<1 sky/star draws ghost-accumulating).
      m_renderer->clearRenderTarget();
      // The env painters do NOT switchEffectConfig -- they inherit the bound "world" effect; only the
      // render target moved, so the draws are otherwise identical to the direct path.
      drawEnv();
      m_envCache.recordFilled(envScreenSize, envPixelRatio);
    }
    m_renderer->gpuTimer().end("render.pass.environment.gpu_us");

    // Composite the cached env into "main" every frame: a full-screen passthrough quad reusing
    // lightingPassthrough (nearest sampling; applyCap=false forces alpha=1.0 => a clean rgb replace of the
    // freshly-cleared main). composite() sets all four params explicitly, so the lighting compose's
    // mutations of the shared effect can't bleed in -- no forked config needed.
    m_renderer->gpuTimer().begin("render.pass.environment.compose.gpu_us");
    m_renderer->composite("lightingPassthrough", "main", envScreenSize, "inputTexture", m_envCache.name(),
      {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", false}});
    m_renderer->gpuTimer().end("render.pass.environment.compose.gpu_us");
    m_renderer->switchEffectConfig("world");   // restore world effect + "main" target for the world layers / non-GPU-lighting path

    // Bit-identity oracle (/rendercache envoracle on; default off, zero-cost when off). The cache path MUST
    // be pixel-identical to a direct env render. Render the SAME env sequence into "envRef" -- a clear:true
    // FBO that startFrame blacks every frame with main's exact clear (NOT the manual clearRenderTarget under
    // test) -- then pixel-compare against the just-composited "main". Any diff localizes the divergence to
    // surface lifecycle / global GL state (the class per-pass review is blind to), NOT the provably-identity
    // compose. envRef takes no explicit size, so it is never reallocated here: it relies purely on
    // setScreenSize's allocation + the startFrame clear.
    //
    // ONLY ON REFRESH FRAMES. On a skip frame "main" holds an env up to N-1 frames old while envRef is drawn
    // fresh, so the comparison measures TEMPORAL STALENESS -- the cache doing precisely what the lever exists
    // to do -- and reports it as a DIFF. That is a correct number answering a question nobody asked, and it
    // is worse than no number: it read as a 40%-failure rate and I filed a bug against my own gate. Gated
    // here, the oracle compares two freshly-drawn envs at EVERY N, so it always answers the one question it
    // exists to answer -- is the cache MECHANISM identity? -- and cannot be run outside its contract.
    if (envOracle && refreshEnv && m_renderer->hasFrameBuffer("envRef")) {
      m_renderer->setRenderTarget(String("envRef"));
      drawEnv();
      // Restore the draw target to "main"; the effect is already "world", so switchEffectConfig("world")
      // would early-out without rebinding and strand the target on envRef.
      m_renderer->setRenderTarget(String("main"));
      auto d = m_renderer->oracle().compare("envRef", "main");
      bool atmosphereless = renderData.skyRenderData.type == SkyType::Atmosphereless;
      if (d.first == NPos)
        Logger::info("[envoracle] SKIPPED (absent fbo or size mismatch) N={} atmosphereless={}", envRefreshInterval, atmosphereless);
      else if (d.first == 0)
        Logger::info("[envoracle] MATCH (0 diff) N={} atmosphereless={}", envRefreshInterval, atmosphereless);
      else
        Logger::info("[envoracle] DIFF={} first=({},{}) N={} atmosphereless={}", d.first, d.second[0], d.second[1], envRefreshInterval, atmosphereless);
    }
  }

  m_renderer->flush();

  bool lightMapUpdated = lightWaiter ? lightWaiter() : false;

  m_renderer->setEffectParameter("lightMapEnabled", !renderData.isFullbright);
  if (renderData.isFullbright) {
    m_renderer->setEffectTexture("lightMap", Image::filled(Vec2U(1, 1), { 255, 255, 255, 255 }, PixelFormat::RGB24));
    m_renderer->setEffectParameter("lightMapMultiplier", 1.0f);
    // Invariant: m_lightMapBorder always matches the currently-bound lightMap. The 1x1 white
    // texture is offset-invariant so this is defensive, but keeps the border consistent if the
    // binding persists into a later non-fullbright frame before a fresh lighting frame arrives.
    m_lightMapBorder = 0;
  } else {
    if (lightMapUpdated) {
      adjustLighting(renderData);
      // GPU-lightmap dispatch is pulled into runGpuLightmapPass(): it prepares the pass inputs and returns an
      // explicit LightmapResult (active + its matching border) instead of a bool plus a separately-read border.
      LightmapResult lm = runGpuLightmapPass(renderData);
      if (lm.active) {
        // The bound lightMap is the calc-region (border-padded) result; the border travels WITH the active
        // flag in the result, so the world shader's lightMapOffset can never pair an active GPU lightMap with a
        // stale/garbage border (it must NOT be reverse-derived from renderData.lightMap width, which is empty
        // => a garbage offset, dark world, when the CPU calc is skipped).
        m_lightMapBorder = lm.border;
      } else if (!renderData.lightMap.empty()) {
        // CPU lightMap upload (deep-gated; also the fallback when the GPU path is off/unavailable).
        static auto uploadTimer = Telemetry::timer("lighting.upload.us");
        TelemetryScope uploadScope(uploadTimer);
        m_renderer->setEffectTexture("lightMap", renderData.lightMap);
        m_lightMapBorder = 0;
      }
      // else (Slice 4): GPU pass failed AND no CPU lightMap this frame -- the skip-calculate latch raced a
      // transient GPU failure. Keep the previous lightMap binding for this one frame; reporting
      // m_gpuLightingActive=false (below) re-arms the CPU path on the lighting thread, so a real lightMap
      // returns within ~1 frame. (An empty upload would flash black.)
      // Slice 4: report this frame's GPU outcome so the lighting thread can drop the redundant CPU calculate().
      m_gpuLightingActive = lm.active;
    }
    m_renderer->setEffectParameter("lightMapMultiplier", m_assets->json("/rendering.config:lightMapMultiplier").toFloat());
    m_renderer->setEffectParameter("lightMapScale", Vec2F::filled(TilePixels * m_camera.pixelRatio()));
    // m_lightMapBorder persists across non-update frames to match the persistent lightMap binding.
    m_renderer->setEffectParameter("lightMapOffset",
        m_camera.worldToScreen(Vec2F(renderData.lightMinPosition) - Vec2F((float)m_lightMapBorder, (float)m_lightMapBorder)));
    m_renderer->setEffectParameter("lightmapBilinear",
        Root::singleton().configuration()->get("lightingWorldSampleBilinear").optBool().value(false));
    m_renderer->setEffectParameter("lightmapUpscale",
        Root::singleton().configuration()->get("lightingWorldUpscale").optFloat().value(1.0f));
  }

  // Parallax layers

  auto parallaxDelta = m_camera.worldGeometry().diff(m_camera.centerWorldPosition(), m_previousCameraCenter);
  if (parallaxDelta.magnitude() > 10)
    m_parallaxWorldPosition = m_camera.centerWorldPosition();
  else
    m_parallaxWorldPosition += parallaxDelta;
  m_previousCameraCenter = m_camera.centerWorldPosition();
  m_parallaxWorldPosition[1] = m_camera.centerWorldPosition()[1];

  // Parallax retained-cache (kubebound GPU-floor SP-2). Like the env cache but for the scrolling parallax
  // pass: render into a persistent RGBA FBO only every N frames / on camera move / zoom / resize (Option B,
  // static-camera) and composite the cache into "main" each frame. Parallax ALPHA-BLENDS over the env
  // background (not an opaque replace), so the cache is PREMULTIPLIED: cleared transparent, drawn with
  // PremultiplyInto, composited PremultipliedOver. NOTE: unlike the opaque env cache this is NOT bit-exact --
  // quantizing the premultiplied intermediate before the composite double-rounds the a*c term vs a single-pass
  // direct blend, so partial-alpha (soft-edge) texels differ by <=1 ULP (fp16) / <=1/255 (RGB8): sub-perceptual,
  // visually identical. N<=1 with the oracle off takes the direct (bit-exact) path. Live: /rendercache parallaxrefresh <N>.
  bool parallaxHasLayers = !renderData.parallaxLayers.empty();
  Vec2U parallaxScreenSize = m_renderer->screenSize();
  bool parallaxAntiAliasing = Root::singleton().configuration()->get("antiAliasing").optBool().value(false);
  float parallaxPixelRatio = m_camera.pixelRatio();

  // CONTENT-ADAPTIVE refresh interval. What the eye catches in a cached parallax is the per-refresh
  // positional STEP of its FASTEST-drifting layer: renderParallaxLayers scrolls each layer by
  // speed * (epochTime / dayLength), scaled by pixelRatio. Hold that step under a perceptual threshold and
  // the cache is imperceptible on ANY world -- static biomes (most of them) take a large N, fast-drifting
  // ones a small N -- instead of forcing the worst world's limit on every world. Snap DOWN to a validated
  // rung {1,2,4,8,16} (conservative). Config parallaxRefreshInterval: 0 = ADAPTIVE (default), 1 = off/direct,
  // >1 = manual fixed N. Threshold tunable via parallaxMaxDriftStepPx.
  unsigned parallaxRefreshCfg = Root::singleton().configuration()->get("parallaxRefreshInterval", 0).optUInt().value(0);
  float parallaxMaxStepPx = Root::singleton().configuration()->get("parallaxMaxDriftStepPx", 1.5f).optFloat().value(1.5f);

  // The drift rate is a property of the CONTENT, not of frame-to-frame tick jitter. A layer's screen offset
  // is speed * (epochTime / dayLength) * pixelRatio and epochTime advances ~1s per real second, so
  //     px/frame (nominal 60fps) = speed / (dayLength * 60) * pixelRatio.
  // (Do NOT derive this from a per-frame epochTime DELTA: skyRenderData only refreshes on WORLD TICKS, so
  //  that delta is exactly 0 on many render frames -- which made N flap between the static default and the
  //  real value every frame.)
  double parallaxDayLength = (double)renderData.skyRenderData.dayLength;
  unsigned parallaxAutoN = 16;       // static content: only the very slow day/night tint needs refreshing
  float parallaxMaxDriftPx = 0.0f;   // fastest layer's screen-pixel drift per frame
  bool parallaxAnimated = false;
  if (parallaxDayLength > 0.0) {
    for (auto const& layer : renderData.parallaxLayers) {
      float sx = layer.speed[0] < 0.0f ? -layer.speed[0] : layer.speed[0];
      float sy = layer.speed[1] < 0.0f ? -layer.speed[1] : layer.speed[1];
      float s = sx > sy ? sx : sy;
      float px = (float)((double)s / (parallaxDayLength * 60.0)) * parallaxPixelRatio;
      if (px > parallaxMaxDriftPx)
        parallaxMaxDriftPx = px;
      if (layer.frameNumber > 1)
        parallaxAnimated = true;
    }
    if (parallaxMaxDriftPx > 0.0f) {
      int n = (int)(parallaxMaxStepPx / parallaxMaxDriftPx);
      if (n < 1) n = 1;
      if (n > 16) n = 16;
      parallaxAutoN = (unsigned)n;
    }
  }
  // (No rung-snapping: any integer N is fine -- the step threshold above is what bounds perceptibility, and
  // snapping to powers of two just throws away saving, e.g. an ideal 3.9 collapsing to 2.)
  if (parallaxAnimated && parallaxAutoN > 4)
    parallaxAutoN = 4;   // don't delay an animation frame-flip by more than ~4 frames

  unsigned parallaxRefreshInterval = (parallaxRefreshCfg == 0) ? parallaxAutoN : parallaxRefreshCfg;
  if (parallaxRefreshInterval < 1)
    parallaxRefreshInterval = 1;
  if (parallaxRefreshInterval != m_lastLoggedParallaxN) {
    Logger::info("[parallaxauto] N={} (cfg={}) driftPx/frame={:.5f} stepThresh={:.2f} layers={} animated={}",
      parallaxRefreshInterval, parallaxRefreshCfg, parallaxMaxDriftPx, parallaxMaxStepPx,
      renderData.parallaxLayers.size(), parallaxAnimated);
    m_lastLoggedParallaxN = parallaxRefreshInterval;
  }

  // Shared draw sequence (single lambda -> cache and its oracle reference can't diverge).
  auto drawParallax = [&]() {
    if (ablateParallax) return;
    if (parallaxHasLayers)
      m_environmentPainter->renderParallaxLayers(m_parallaxWorldPosition, m_camera, renderData.parallaxLayers, renderData.skyRenderData);
  };

  // CONTENT KEY -- everything OTHER than camera/zoom/size that changes the drawn image, and none of which was
  // in the old refresh key. renderParallaxLayers tints every non-unlit/non-lightMapped layer with
  // sky.environmentLight and fades it by floor(255*layer.alpha) (StarEnvironmentPainter.cpp:251-255) -- and
  // layer.alpha is exactly what the biome CROSSFADE and timeOfDayCorrelation animate. So a cached parallax
  // froze its day/night tint and stalled biome crossfades for up to N frames. Hash the same QUANTIZED values
  // the draw itself consumes, so the key moves iff the rendered image would.
  // (epochTime drift is deliberately absent: amortizing it over N frames is the whole point of the cache, and
  //  N is derived from it.)
  uint64_t parallaxContentKey = 1469598103934665603ull;
  {
    auto mix = [&parallaxContentKey](uint64_t v) {
      parallaxContentKey = (parallaxContentKey ^ v) * 1099511628211ull;
    };
    Vec3B envLight = renderData.skyRenderData.environmentLight.toRgb();
    mix(envLight[0]); mix(envLight[1]); mix(envLight[2]);
    mix(renderData.parallaxLayers.size());
    for (auto const& layer : renderData.parallaxLayers)
      mix((uint64_t)(unsigned)floor(255.0f * layer.alpha));
  }

  // IS THE CAMERA MOVING RIGHT NOW? (vs. "does the cache hold a different position", which is a staleness
  // question and stays true on the first parked frame.)
  bool parallaxCameraMoving = (m_parallaxWorldPosition != m_parallaxPrevPosition)
      || (parallaxPixelRatio != m_parallaxPrevPixelRatio);
  m_parallaxPrevPosition = m_parallaxWorldPosition;
  m_parallaxPrevPixelRatio = parallaxPixelRatio;
  m_parallaxStillFrames = parallaxCameraMoving ? 0 : (m_parallaxStillFrames + 1);

  // MOVING-CAMERA BYPASS. Each layer scrolls by cameraDelta / parallaxValue_i (StarEnvironmentPainter.cpp:282),
  // so every layer shifts by a DIFFERENT amount: a flattened composite is not a rigid translation and cannot be
  // scroll-shifted. The cache therefore can never win on a moving frame -- it force-refreshes, redrawing the
  // full stack exactly as the direct path would, and then pays a full-screen composite ON TOP. That is a strict
  // REGRESSION vs vanilla on every moving frame (exploring, combat -- most of actual play), and it shipped.
  // So while the camera moves, bypass the cache entirely and draw direct, exactly like vanilla. The cache
  // engages only once parked, which is the only regime in which it ever won. ParkFrames of hysteresis keeps a
  // jittering camera from thrashing refresh/bypass (each thrash would cost a refresh + a composite).
  static constexpr unsigned ParallaxParkFrames = 2;
  bool parallaxParked = m_parallaxStillFrames >= ParallaxParkFrames;
  // THE ANTI-ALIASING GATE STAYS HERE -- and unlike the env cache's, it is NOT a dead symptom patch. I removed
  // it, measured, and put it back.
  //
  // It is safe to SAMPLE parallaxCache under AA (it is multisampled:false, so no GL_INVALID_OPERATION), which
  // is why the env cache's gate could go. But sampling is not the issue. antiAliasing turns on
  // glMinSampleShading(1.f), so the DIRECT path shades every parallax fragment ONCE PER SAMPLE into
  // multisampled "main". The CACHE path rasterizes parallax into a SINGLE-SAMPLE surface and then composites
  // that as one flat quad -- so the cached parallax never gets per-sample shading at all. Measured, frozen
  // world, in-process A/B of refresh 1 vs 8:
  //     AA off : byte-identical
  //     AA on  : 781760 px differ (22.3%), maxAbs 0.000977
  // Sub-perceptual (under a quarter of an 8-bit LSB) -- but it is precisely the quality the player asked for
  // when they ticked the box, and silently withholding it to buy back frame time is not our call to make.
  //
  // A retained surface CANNOT preserve the multisample shading of the content drawn into it. That is a
  // property of retained surfaces under MSAA, not a bug, and it is the reason this gate is real.
  bool parallaxCacheActive = parallaxHasLayers && !parallaxAntiAliasing
      && (parallaxRefreshInterval > 1 || parallaxOracle)
      && (parallaxParked || parallaxOracle);   // oracle must stay on the cache path to gate it

  static auto parallaxRefreshedCtr = Telemetry::counter("render.cache.parallax.refreshed");
  static auto parallaxSkippedCtr = Telemetry::counter("render.cache.parallax.skipped");
  static auto parallaxBypassedCtr = Telemetry::counter("render.cache.parallax.bypassed_moving");

  if (!parallaxCacheActive) {
    // Direct path (byte-identical stock parallax->main): camera moving, AA on, N<=1 oracle-off, or no layers.
    // Invalidate the cache so re-entry force-refreshes.
    m_parallaxCache.invalidate();
    if (parallaxHasLayers && !parallaxParked)
      parallaxBypassedCtr.inc(1);
    m_renderer->gpuTimer().begin("render.pass.parallax.gpu_us");
    drawParallax();
    m_renderer->gpuTimer().end("render.pass.parallax.gpu_us");
  } else {
    // Cache path (camera parked). INVALIDATION terms force a redraw regardless; the TIME gate is the ordinary
    // N-frame cadence and is the only one the arbiter may defer.
    bool parallaxInvalidated = m_parallaxCache.invalidated(parallaxScreenSize, parallaxPixelRatio)
        || (m_parallaxWorldPosition != m_parallaxCachePosition)
        || (parallaxContentKey != m_parallaxCacheContentKey);
    // cadenceHit advances the counter every active frame -- exactly the old separate `++m_parallaxRefreshCounter;`
    // statement. It is the FIRST operand of the || so it is always evaluated (never short-circuited); the
    // deferred flag is a side-effect-free bool read, so OR order is immaterial. (Same reasoning as the env cache.)
    bool parallaxTimeGate = m_parallaxCache.cadenceHit(parallaxRefreshInterval) || m_parallaxRefreshDeferred;

    // CROSS-SURFACE ARBITER. Stacking the env redraw and the parallax redraw into one frame spikes that frame's
    // GPU time. The old mechanism -- offsetting parallax's gate by +N/2 -- could not prevent it: env fires on
    // counter % 4 == 0, so for any N whose half is a multiple of 4 (notably the static-content default N=16, and
    // N=8) EVERY parallax refresh landed on an env refresh frame. Defer the time-gated refresh by one frame
    // instead; invalidation refreshes are never deferred (they would show a stale image).
    //
    // The deferral is bounded to ONE frame, and the bound is load-bearing. Without `!alreadyDeferred` the
    // arbiter re-defers on every frame that env refreshes, so whenever env refreshes on CONSECUTIVE frames
    // (env N==1, which is exactly what arming the env oracle forces) parallax is deferred forever: the cache
    // freezes at its first frame and the sky silently stops updating. Measured: the parallax oracle's diff
    // climbed monotonically from 187k to full-screen saturation over one run. Not reachable from the shipped
    // config (env N=4 refreshes 1 frame in 4, so the deferred refresh always lands on a non-env frame), but
    // it made the two oracles mutually exclusive and left the parallax gate reading garbage.
    bool alreadyDeferred = m_parallaxRefreshDeferred;
    bool refreshParallax = parallaxInvalidated || parallaxTimeGate;
    if (refreshParallax && !parallaxInvalidated && envRefreshedThisFrame && !alreadyDeferred) {
      refreshParallax = false;
      m_parallaxRefreshDeferred = true;   // fire next frame regardless of the counter
    } else if (refreshParallax) {
      m_parallaxRefreshDeferred = false;
    }
    (refreshParallax ? parallaxRefreshedCtr : parallaxSkippedCtr).inc(1);

    m_renderer->gpuTimer().begin("render.pass.parallax.gpu_us");
    if (refreshParallax) {
      m_renderer->setRenderTarget(m_parallaxCache.name(), parallaxScreenSize);
      m_renderer->clearRenderTarget(Vec4F(0.0f, 0.0f, 0.0f, 0.0f));   // transparent -> premultiplied accumulation
      m_renderer->setBlendMode(BlendMode::PremultiplyInto);
      drawParallax();
      m_renderer->flush();                          // render the parallax quads into the cache under PremultiplyInto
      m_renderer->setBlendMode(BlendMode::Alpha);   // restore the default blend
      m_parallaxCache.recordFilled(parallaxScreenSize, parallaxPixelRatio);
      m_parallaxCachePosition = m_parallaxWorldPosition;
      m_parallaxCacheContentKey = parallaxContentKey;
    }
    m_renderer->gpuTimer().end("render.pass.parallax.gpu_us");

    // Oracle reference (before the cache composite modifies main): parallaxRef = env_bg (a copy of main) +
    // parallax DIRECT. Built here because the composite below overwrites main with the cache result.
    //
    // ONLY ON A REFRESH FRAME. The cache's claim is "what I draw on a refresh equals what the direct path
    // draws" -- it claims nothing about the N-1 frames in between, where it is deliberately serving an older
    // image. Comparing on those frames measures staleness, not correctness, and reports a diff that grows
    // with N. The env oracle had this exact defect (#149) and was gated on refreshEnv; this one never was, so
    // it has been reporting a false failure at every N>1 for the whole campaign.
    if (parallaxOracle && refreshParallax && m_renderer->hasFrameBuffer("parallaxRef")) {
      m_renderer->composite("lightingPassthrough", "parallaxRef", parallaxScreenSize, "inputTexture", "main",
        {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", false}});
      m_renderer->switchEffectConfig("world");                        // world effect (binds main)
      m_renderer->setRenderTarget(String("parallaxRef"), parallaxScreenSize);   // -> parallaxRef, effect stays "world"
      m_renderer->setBlendMode(BlendMode::Alpha);
      drawParallax();
      m_renderer->flush();
    }

    // Composite the premultiplied cache over main (env bg) every frame.
    m_renderer->gpuTimer().begin("render.pass.parallax.compose.gpu_us");
    m_renderer->setBlendMode(BlendMode::PremultipliedOver);
    m_renderer->composite("lightingPassthrough", "main", parallaxScreenSize, "inputTexture", m_parallaxCache.name(),
      {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", true}});
    m_renderer->setBlendMode(BlendMode::Alpha);
    m_renderer->switchEffectConfig("world");   // restore world effect + "main" target for the world layers
    m_renderer->gpuTimer().end("render.pass.parallax.compose.gpu_us");

    if (parallaxOracle && refreshParallax && m_renderer->hasFrameBuffer("parallaxRef")) {
      // Bounded-diff gate (NOT a 0-diff gate): the premultiplied cache double-rounds partial-alpha texels, so a
      // small count on semi-transparent fringes with maxAbs ~<=1 LSB is EXPECTED + sub-perceptual. A large maxAbs
      // would flag a real blend/compose bug rather than the rounding.
      float pmax = 0.0f;
      auto d = m_renderer->oracle().compare("parallaxRef", "main", &pmax);
      if (d.first == NPos)
        Logger::info("[paralloracle] SKIPPED (absent fbo or size mismatch) N={}", parallaxRefreshInterval);
      else if (d.first == 0)
        Logger::info("[paralloracle] EXACT (0 diff) N={}", parallaxRefreshInterval);
      else
        Logger::info("[paralloracle] diff={} maxAbs={:.5f} first=({},{}) N={} (<=~1 LSB expected: premult double-rounding)",
          d.first, pmax, d.second[0], d.second[1], parallaxRefreshInterval);
    }
  }

  // Main world layers

  m_renderer->gpuTimer().begin("render.pass.world.gpu_us");
  Map<EntityRenderLayer, List<pair<EntityHighlightEffect, List<Drawable>>>> entityDrawables;
  for (auto& ed : renderData.entityDrawables) {
    for (auto& p : ed.layers)
      entityDrawables[p.first].append({ed.highlightEffect, std::move(p.second)});
  }

  auto entityDrawableIterator = entityDrawables.begin();
  auto renderEntitiesUntil = [this, &entityDrawables, &entityDrawableIterator](Maybe<EntityRenderLayer> until) {
    while (true) {
      if (entityDrawableIterator == entityDrawables.end())
        break;
      if (until && entityDrawableIterator->first >= *until)
        break;
      for (auto& edl : entityDrawableIterator->second)
        drawEntityLayer(std::move(edl.second), edl.first);
      ++entityDrawableIterator;
    }

    m_renderer->flush();
  };

  renderEntitiesUntil(RenderLayerBackgroundOverlay);
  drawDrawableSet(renderData.backgroundOverlays);
  renderEntitiesUntil(RenderLayerBackgroundTile);
  m_tilePainter->renderBackground(m_camera);
  renderEntitiesUntil(RenderLayerPlatform);
  m_tilePainter->renderMidground(m_camera);
  renderEntitiesUntil(RenderLayerBackParticle);
  renderParticles(renderData, Particle::Layer::Back);
  renderEntitiesUntil(RenderLayerLiquid);
  m_tilePainter->renderLiquid(m_camera);
  renderEntitiesUntil(RenderLayerMiddleParticle);
  renderParticles(renderData, Particle::Layer::Middle);
  renderEntitiesUntil(RenderLayerForegroundTile);
  m_tilePainter->renderForeground(m_camera);
  renderEntitiesUntil(RenderLayerForegroundOverlay);
  drawDrawableSet(renderData.foregroundOverlays);
  renderEntitiesUntil(RenderLayerFrontParticle);
  renderParticles(renderData, Particle::Layer::Front);
  renderEntitiesUntil(RenderLayerOverlay);
  drawDrawableSet(renderData.nametags);
  renderBars(renderData);
  renderEntitiesUntil({});
  m_renderer->gpuTimer().end("render.pass.world.gpu_us");

  m_renderer->gpuTimer().begin("render.pass.compose.gpu_us");
  auto dimLevel = round(renderData.dimLevel * 255);
  if (dimLevel != 0)
    m_renderer->render(renderFlatRect(RectF::withSize({}, Vec2F(m_camera.screenSize())), Vec4B(renderData.dimColor, dimLevel), 0.0f));
  m_renderer->gpuTimer().end("render.pass.compose.gpu_us");

  // Rung 0: surface the per-pass GPU timings (populated only under deep telemetry) on the /debug HUD.
  for (auto const& key : {"render.pass.environment.gpu_us", "render.pass.parallax.gpu_us",
                          "render.pass.world.gpu_us", "render.pass.compose.gpu_us"}) {
    if (auto us = m_renderer->gpuTimer().lastMicros(key))
      LogMap::set(String(key), strf("{:05d}us", *us));
  }

  int64_t textureTimeout = m_assets->json("/rendering.config:textureTimeout").toInt();
  m_textPainter->cleanup(textureTimeout);
  m_drawablePainter->cleanup(textureTimeout);
  m_environmentPainter->cleanup(textureTimeout);
  m_tilePainter->cleanup();
}

void WorldPainter::adjustLighting(WorldRenderData& renderData) {
  m_tilePainter->adjustLighting(renderData);
}

void WorldPainter::renderParticles(WorldRenderData& renderData, Particle::Layer layer) {
  const int textParticleFontSize = m_assets->json("/rendering.config:textParticleFontSize").toInt();
  const RectF particleRenderWindow = RectF::withSize(Vec2F(), Vec2F(m_camera.screenSize())).padded(m_assets->json("/rendering.config:particleRenderWindowPadding").toInt());

  if (!renderData.particles)
    return;

  for (Particle const& particle : *renderData.particles) {
    if (layer != particle.layer)
      continue;

    Vec2F position = m_camera.worldToScreen(particle.position);

    if (!particleRenderWindow.contains(position))
      continue;

    Vec2F size = Vec2F::filled(particle.size * m_camera.pixelRatio());

    if (particle.type == Particle::Type::Ember) {
      m_renderer->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(),
        RectF(position - size / 2, position + size / 2),
        particle.color.toRgba(),
        particle.fullbright ? 0.0f : 1.0f);

    } else if (particle.type == Particle::Type::Streak) {
      // Draw a rotated quad streaking in the direction the particle is coming from.
      // Sadly this looks awful.
      Vec2F dir = particle.velocity.normalized();
      Vec2F sideHalf = dir.rot90() * m_camera.pixelRatio() * particle.size / 2;
      float length = particle.length * m_camera.pixelRatio();
      Vec4B color = particle.color.toRgba();
      float lightMapMultiplier = particle.fullbright ? 0.0f : 1.0f;
      m_renderer->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(),
        position - sideHalf,
        position + sideHalf,
        position - dir * length + sideHalf,
        position - dir * length - sideHalf,
        color, lightMapMultiplier);

    } else if (particle.type == Particle::Type::Textured || particle.type == Particle::Type::Animated) {
      Drawable drawable;
      if (particle.type == Particle::Type::Textured)
        drawable = Drawable::makeImage(particle.image, 1.0f / TilePixels, true, Vec2F(0, 0));
      else
        drawable = particle.animation->drawable(1.0f / TilePixels);

      if (particle.flip && particle.flippable)
        drawable.scale(Vec2F(-1, 1));
      if (drawable.isImage() && particle.type != Particle::Type::Animated)
        drawable.imagePart().addDirectivesGroup(particle.directives, true);
      drawable.fullbright = particle.fullbright;
      drawable.color = particle.color;
      drawable.rotate(particle.rotation);
      drawable.scale(particle.size);
      drawable.translate(particle.position);
      drawDrawable(std::move(drawable));

    } else if (particle.type == Particle::Type::Text) {
      Vec2F position = m_camera.worldToScreen(particle.position);
      int size = min(128.0f, round((float)textParticleFontSize * m_camera.pixelRatio() * particle.size));
      if (size > 0) {
        m_textPainter->setFontSize(size);
        m_textPainter->setFontColor(particle.color.toRgba());
        m_textPainter->setProcessingDirectives("");
        m_textPainter->setFont("");
        m_textPainter->renderText(particle.string, {position, HorizontalAnchor::HMidAnchor, VerticalAnchor::VMidAnchor});
      }
    }
  }

  m_renderer->flush();
}

void WorldPainter::renderBars(WorldRenderData& renderData) {
  auto offset = m_entityBarOffset;
  for (auto const& bar : renderData.overheadBars) {
    auto position = bar.entityPosition + offset;
    offset += m_entityBarSpacing;
    if (bar.icon) {
      auto iconDrawPosition = position - (m_entityBarSize / 2).round() + m_entityBarIconOffset;
      drawDrawable(Drawable::makeImage(*bar.icon, 1.0f / TilePixels, true, iconDrawPosition));
    }

    if (!bar.detailOnly) {
      auto fullBar = RectF({}, {m_entityBarSize.x() * bar.percentage, m_entityBarSize.y()});
      auto emptyBar = RectF({m_entityBarSize.x() * bar.percentage, 0.0f}, m_entityBarSize);
      auto fullColor = bar.color;
      auto emptyColor = Color::Black;

      drawDrawable(Drawable::makePoly(PolyF(emptyBar), emptyColor, position));
      drawDrawable(Drawable::makePoly(PolyF(fullBar), fullColor, position));
    }
  }

  m_renderer->flush();
}

void WorldPainter::drawEntityLayer(List<Drawable> drawables, EntityHighlightEffect highlightEffect) {
  highlightEffect.level *= m_highlightConfig.getFloat("maxHighlightLevel", 1.0);
  if (m_highlightDirectives.contains(highlightEffect.type) && highlightEffect.level > 0) {
    // first pass, draw underlay
    auto underlayDirectives = m_highlightDirectives[highlightEffect.type].first;
    if (!underlayDirectives.empty()) {
      for (auto& d : drawables) {
        if (d.isImage()) {
          auto underlayDrawable = Drawable(d);
          underlayDrawable.fullbright = true;
          underlayDrawable.color = Color::rgbaf(1, 1, 1, highlightEffect.level * d.color.alphaF());
          underlayDrawable.imagePart().addDirectives(underlayDirectives, true);
          drawDrawable(std::move(underlayDrawable));
        }
      }
    }

    // second pass, draw main drawables and overlays
    auto overlayDirectives = m_highlightDirectives[highlightEffect.type].second;
    for (auto& d : drawables) {
      drawDrawable(d);
      if (!overlayDirectives.empty() && d.isImage()) {
        auto overlayDrawable = Drawable(d);
        overlayDrawable.fullbright = true;
        overlayDrawable.color = Color::rgbaf(1, 1, 1, highlightEffect.level * d.color.alphaF());
        overlayDrawable.imagePart().addDirectives(overlayDirectives, true);
        drawDrawable(std::move(overlayDrawable));
      }
    }
  } else {
    for (auto& d : drawables)
      drawDrawable(std::move(d));
  }
}

void WorldPainter::drawDrawable(Drawable drawable) {
  drawable.position = m_camera.worldToScreen(drawable.position);
  drawable.scale(m_camera.pixelRatio() * TilePixels, drawable.position);

  if (drawable.isLine())
    drawable.linePart().width *= m_camera.pixelRatio();

  // draw the drawable if it's on screen
  // if it's not on screen, there's a random chance to pre-load
  // pre-load is not done on every tick because it's expensive to look up images with long paths
  if (RectF::withSize(Vec2F(), Vec2F(m_camera.screenSize())).intersects(drawable.boundBox(false)))
    m_drawablePainter->drawDrawable(drawable);
  else if (drawable.isImage() && Random::randf() < m_preloadTextureChance)
    m_assets->tryImage(drawable.imagePart().image);
}

void WorldPainter::drawDrawableSet(List<Drawable>& drawables) {
  for (Drawable& drawable : drawables)
    drawDrawable(std::move(drawable));

  m_renderer->flush();
}

}
