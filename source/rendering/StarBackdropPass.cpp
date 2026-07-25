#include "StarBackdropPass.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarTelemetry.hpp"
#include "StarLogging.hpp"
#include "StarWorldCamera.hpp"
#include "StarMathCommon.hpp"

namespace Star {

BackdropPass::BackdropPass(Renderer* renderer) : m_renderer(renderer) {}

// CM-1: the merged env+parallax compose. One full-screen quad, sampling the env cache (opaque backdrop) and
// the parallax cache (premultiplied coverage), writing "main" once via the backdropCompose effect:
//   out = P.rgb + E.rgb * (1 - P.a)
// which is algebraically the sequential (env opaque replace, then parallax premultiplied-over) result. Blend
// is irrelevant -- the shader writes the full opaque result and env covers every pixel, so "main" (still on
// the startFrame clear, since the env compose was deferred) is fully overwritten. Mirrors GpuLightmapPass's
// full-quad pattern.
void BackdropPass::mergedCompose(Vec2U const& size) {
  // GUARD (adversarial review, Lens 2): switchEffectConfig returns false and mutates nothing if backdropCompose
  // is unregistered (asset missing / mod override / corrupt install). An UNCHECKED draw would then go through
  // the still-bound "world" shader into "main" -- a garbage full-screen backdrop. Fall back to the two
  // sequential lightingPassthrough composites (always present), byte-identical to the non-merge path. Every
  // sibling compose site guards switchEffectConfig the same way (composite(), GpuLightmapPass).
  if (!m_renderer->switchEffectConfig("backdropCompose")) {
    m_renderer->composite("lightingPassthrough", "main", size, "inputTexture", m_envCache.name(),
      {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", false}});
    m_renderer->setBlendMode(BlendMode::PremultipliedOver);
    m_renderer->composite("lightingPassthrough", "main", size, "inputTexture", m_parallaxCache.name(),
      {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", true}});
    m_renderer->setBlendMode(BlendMode::Alpha);
    m_renderer->switchEffectConfig("world");
    return;
  }
  if (!m_backdropMergeLogged) {
    Logger::info("[backdropmerge] engaged: env+parallax composited in one pass ({}x{})", size[0], size[1]);
    m_backdropMergeLogged = true;
  }
  if (!m_fullQuadBuffer)
    m_fullQuadBuffer = m_renderer->createRenderBuffer();
  if (m_fullQuadSize != size) {
    List<RenderPrimitive> prims;
    prims.append(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f));
    m_fullQuadBuffer->set(prims);
    m_fullQuadSize = size;
  }
  m_renderer->setEffectTextureFromTarget("envTexture", m_envCache.name());
  m_renderer->setEffectTextureFromTarget("parallaxTexture", m_parallaxCache.name());
  m_renderer->setRenderTarget(String("main"), size);
  // Explicit full-replace (adversarial review, Lens 3): the shader writes an opaque result and today "main" holds
  // only the startFrame clear, so any blend degenerates to a replace -- but that is an ACCIDENTAL invariant. Set
  // None so the merged pass stays correct if a future consumer (world-band cache, #143/#145) writes "main" before
  // the backdrop. Restore Alpha for the world layers.
  m_renderer->setBlendMode(BlendMode::None);
  m_renderer->renderBuffer(m_fullQuadBuffer);
  m_renderer->flush();
  m_renderer->setBlendMode(BlendMode::Alpha);
  m_renderer->switchEffectConfig("world");   // restore world effect + "main" target for the world layers
}

void BackdropPass::renderEnvironment(WorldCamera const& camera, WorldRenderData& renderData,
    EnvironmentPainter& envPainter, bool ablateEnv) {
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

  // Did the env cache do its heavy redraw this frame? Read by the parallax refresh arbiter (renderParallax),
  // which defers a purely time-gated parallax refresh off any frame env is already redrawing on.
  m_envRefreshedThisFrame = false;
  // CM-1: reset each frame; set true below iff the env cache is active AND the merge is enabled, in which case
  // the env->main compose is DEFERRED to renderParallax. The direct env path (cache off) never defers.
  m_envComposeDeferred = false;

  // Use a fixed pixel ratio for certain things.
  float pixelRatioBasis = camera.screenSize()[1] / 1080.0f;
  float starAndDebrisRatio = lerp(0.0625f, pixelRatioBasis * 2.0f, camera.pixelRatio());
  float orbiterAndPlanetRatio = lerp(0.125f, pixelRatioBasis * 3.0f, camera.pixelRatio());

  // Environment-cache probe (kubebound GPU-floor campaign). The env pass changes only slowly (star
  // twinkle + orbital drift) yet re-renders every frame at 1.6-7.6ms. Render it into its OWN persistent
  // screen-sized HDR FBO "envCache" (a clear:false mirror of "main") only every N frames, and composite
  // that cache into "main" every frame. envRefreshInterval N==1 (default) refreshes every frame and is
  // bit-identical to the direct-into-main path it replaces. Live: /rendercache envrefresh <N>.
  Vec2U envScreenSize = m_renderer->screenSize();
  float envPixelRatio = camera.pixelRatio();
  unsigned envRefreshInterval = Root::singleton().configuration()->get("envRefreshInterval", 1).optUInt().value(1);
  if (envRefreshInterval < 1)
    envRefreshInterval = 1;
  bool envOracle = Root::singleton().configuration()->get("envOracle", false).optBool().value(false);
  // NB: the oracles' reference surfaces (envRef, parallaxRef) are marked devOnly and are only ALLOCATED while
  // an oracle is armed -- ClientApplication::render does that before the frame starts, because it reloads the
  // framebuffer set and must not run mid-frame. Both oracle paths below are already guarded by hasFrameBuffer,
  // so they correctly no-op on the frame where the surfaces have not appeared yet.

  // The env draw sequence, shared by every path (AA-direct, cache-refresh, oracle reference) so the cache
  // and its bit-identity reference can never silently diverge -- a hand-duplicated copy that drifted would
  // make the oracle lie. Draws into whatever render target / effect is currently bound.
  auto drawEnv = [&]() {
    if (ablateEnv) return;
    envPainter.renderStars(starAndDebrisRatio, Vec2F(camera.screenSize()), renderData.skyRenderData);
    envPainter.renderDebrisFields(starAndDebrisRatio, Vec2F(camera.screenSize()), renderData.skyRenderData);
    if (renderData.skyRenderData.type != SkyType::Atmosphereless)
      envPainter.renderBackOrbiters(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), renderData.skyRenderData);
    envPainter.renderPlanetHorizon(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), renderData.skyRenderData);
    envPainter.renderSky(Vec2F(camera.screenSize()), renderData.skyRenderData);
    envPainter.renderFrontOrbiters(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), renderData.skyRenderData);
    if (renderData.skyRenderData.type == SkyType::Atmosphereless)
      envPainter.renderBackOrbiters(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), renderData.skyRenderData);
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
  // Hoisted above the branch below so one declaration dominates both begin() sites (see StarWorldPass.cpp
  // for why declaration lives at the pass and not where the value is recorded).
  [[maybe_unused]] static bool const envGpuDesc = [] {
    Telemetry::declare("render.pass.environment.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
    return true;
  }();
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
    static auto envRefreshed = Telemetry::counter("render.cache.env.refreshed",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
    static auto envSkipped = Telemetry::counter("render.cache.env.skipped",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
    bool envInvalidated = m_envCache.invalidated(envScreenSize, envPixelRatio);
    // cadenceHit is called UNCONDITIONALLY (not short-circuited behind envInvalidated) so the frame counter
    // advances every active frame -- exactly the old separate `++m_envRefreshCounter;` statement, which ran
    // even when the modulo test was skipped. Folding it into `envInvalidated || cadenceHit(...)` would stop
    // advancing the counter on invalidation frames and silently shift the N-cadence phase afterwards.
    bool envCadence = m_envCache.cadenceHit(envRefreshInterval);
    bool refreshEnv = envInvalidated || envCadence;
    (refreshEnv ? envRefreshed : envSkipped).inc(1);
    m_envRefreshedThisFrame = refreshEnv;

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

    // CM-1: when backdropComposeMerge is enabled, DEFER the env->main compose to renderParallax so env+parallax
    // become one full-screen pass. The env cache is already FILLED (above); only the compose moves. renderParallax
    // then guarantees env reaches "main" exactly once -- merged with parallax if the parallax cache is also active
    // this frame, standalone otherwise. Nothing draws into "main" between here and the parallax compose (the
    // lightmap phase only binds a texture), so deferring is order-safe.
    //
    // INVARIANT (adversarial review, Lens 1) -- deferring the ENTIRE backdrop across the lightmap phase makes
    // this a contract, not just a happen-to-be-safe ordering: (1) no pass inserted between renderEnvironment and
    // renderParallax may write "main" -- the deferred compose would overwrite it (a world-band retained cache,
    // #143/#145, is the concrete future risk); (2) both entry points must run the same frame -- an abort between
    // them leaves "main" on the startFrame clear, so the WHOLE backdrop (sky included) goes black, where
    // pre-CM-1 the sky was already in "main" and only parallax was lost. Verified safe today (the lightmap phase
    // writes only lightingGpu* and restores world/main); anything added to that window must preserve this.
    bool composeMerge = Root::singleton().configuration()->get("backdropComposeMerge", true).optBool().value(true);
    if (composeMerge) {
      m_envComposeDeferred = true;
    } else {
      // Composite the cached env into "main" every frame: a full-screen passthrough quad reusing
      // lightingPassthrough (nearest sampling; applyCap=false forces alpha=1.0 => a clean rgb replace of the
      // freshly-cleared main). composite() sets all four params explicitly, so the lighting compose's
      // mutations of the shared effect can't bleed in -- no forked config needed.
      //
      // Declared here (one of two begin() sites for this key -- the CM-1 reconcile site in renderParallax
      // below is the other). This branch only runs when backdropComposeMerge=false; that config defaults to
      // true, so in a default session the OTHER site is the one that actually declares it. Both declare
      // identically so whichever config is live, the key is never left undeclared.
      [[maybe_unused]] static bool const envComposeGpuDesc = [] {
        Telemetry::declare("render.pass.environment.compose.gpu_us",
          MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
        return true;
      }();
      m_renderer->gpuTimer().begin("render.pass.environment.compose.gpu_us");
      m_renderer->composite("lightingPassthrough", "main", envScreenSize, "inputTexture", m_envCache.name(),
        {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", false}});
      m_renderer->gpuTimer().end("render.pass.environment.compose.gpu_us");
      m_renderer->switchEffectConfig("world");   // restore world effect + "main" target for the world layers / non-GPU-lighting path
    }

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
      // In merge mode the env compose is deferred (main holds no env yet), so compare the reference against the
      // env CACHE. This validates the env cache FILL (cache draw == direct draw) -- NOT that env reaches "main".
      // In merge mode env reaches "main" via the merged backdropCompose (E.rgb*(1-P.a)), whose correctness is
      // covered by the PARALLAX oracle (its reference is env-cache + parallax-direct vs merged main). So the two
      // oracles split the coverage: env oracle = cache fill, parallax oracle = merged compose. CAVEAT (adversarial
      // review, Lens 4): with envOracle armed but parallaxOracle OFF, a bug in backdropCompose's env term would
      // NOT be caught -- arm both (as the render gate does) to cover the merged compose. Non-merge: main holds the
      // composited env, so compare vs main also validates the passthrough compose into main.
      auto d = m_renderer->oracle().compare("envRef", m_envComposeDeferred ? m_envCache.name() : String("main"));
      bool atmosphereless = renderData.skyRenderData.type == SkyType::Atmosphereless;
      if (d.first == NPos)
        Logger::info("[envoracle] SKIPPED (absent fbo or size mismatch) N={} atmosphereless={}", envRefreshInterval, atmosphereless);
      else if (d.first == 0)
        Logger::info("[envoracle] MATCH (0 diff) N={} atmosphereless={}", envRefreshInterval, atmosphereless);
      else
        Logger::info("[envoracle] DIFF={} first=({},{}) N={} atmosphereless={}", d.first, d.second[0], d.second[1], envRefreshInterval, atmosphereless);
    }
  }
}

void BackdropPass::renderParallax(WorldCamera const& camera, WorldRenderData& renderData,
    EnvironmentPainter& envPainter, bool ablateParallax) {
  auto parallaxDelta = camera.worldGeometry().diff(camera.centerWorldPosition(), m_previousCameraCenter);
  if (parallaxDelta.magnitude() > 10)
    m_parallaxWorldPosition = camera.centerWorldPosition();
  else
    m_parallaxWorldPosition += parallaxDelta;
  m_previousCameraCenter = camera.centerWorldPosition();
  m_parallaxWorldPosition[1] = camera.centerWorldPosition()[1];

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
  float parallaxPixelRatio = camera.pixelRatio();
  bool parallaxOracle = Root::singleton().configuration()->get("parallaxOracle", false).optBool().value(false);

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
      envPainter.renderParallaxLayers(m_parallaxWorldPosition, camera, renderData.parallaxLayers, renderData.skyRenderData);
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

  // Hoisted above the branch below (like render.pass.environment.gpu_us above) so one declaration
  // dominates BOTH begin() sites. It must not sit inside `if (!parallaxCacheActive)`: with
  // parallaxOracle on, parallaxParked is bypassed and parallaxCacheActive can be true from frame 1,
  // so the direct-path branch -- and a declare living only inside it -- would never run.
  [[maybe_unused]] static bool const parallaxGpuDesc = [] {
    Telemetry::declare("render.pass.parallax.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
    return true;
  }();

  static auto parallaxRefreshedCtr = Telemetry::counter("render.cache.parallax.refreshed",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
  static auto parallaxSkippedCtr = Telemetry::counter("render.cache.parallax.skipped",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
  static auto parallaxBypassedCtr = Telemetry::counter("render.cache.parallax.bypassed_moving",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});

  if (!parallaxCacheActive) {
    // Direct path (byte-identical stock parallax->main): camera moving, AA on, N<=1 oracle-off, or no layers.
    // Invalidate the cache so re-entry force-refreshes.
    m_parallaxCache.invalidate();
    if (parallaxHasLayers && !parallaxParked)
      parallaxBypassedCtr.inc(1);
    // CM-1 reconcile: renderEnvironment deferred the env compose for a merge that will NOT happen (parallax is
    // not caching this frame -- moving camera, AA on, no layers, or N<=1). Composite the env cache into "main"
    // now, standalone, so env still reaches "main" exactly once before the direct parallax draws over it. Same
    // passthrough the deferred env compose would have used; env stays byte-identical.
    if (m_envComposeDeferred) {
      // Declared here TOO, identically to renderEnvironment's else-branch site above: the two begin() sites
      // live in DIFFERENT functions gated by the same backdropComposeMerge flag, so no single declare
      // dominates both -- and since the flag defaults to true, THIS is the site a default session actually
      // takes. (Contrast render.pass.parallax.gpu_us below, whose two sites share a function and so could
      // be -- and now are -- hoisted to one dominating declare instead.)
      [[maybe_unused]] static bool const envComposeGpuDescReconcile = [] {
        Telemetry::declare("render.pass.environment.compose.gpu_us",
          MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
        return true;
      }();
      m_renderer->gpuTimer().begin("render.pass.environment.compose.gpu_us");
      m_renderer->composite("lightingPassthrough", "main", parallaxScreenSize, "inputTexture", m_envCache.name(),
        {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", false}});
      m_renderer->gpuTimer().end("render.pass.environment.compose.gpu_us");
      m_renderer->switchEffectConfig("world");
      m_envComposeDeferred = false;
    }
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
    if (refreshParallax && !parallaxInvalidated && m_envRefreshedThisFrame && !alreadyDeferred) {
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
      // Reference background: in merge mode env was deferred (main is empty), so seed parallaxRef from the env
      // CACHE (opaque); non-merge, main already holds the composited env, so copy main. Then draw parallax DIRECT
      // over it -> parallaxRef = env + parallax-direct, the sequential result the cache/merge must match. Both
      // compose to parallax.rgb*a + env*(1-a): the merged premultiplied pass and this direct-over reference agree
      // to <=1 LSB (the same premult double-rounding the parallax cache already carries).
      m_renderer->composite("lightingPassthrough", "parallaxRef", parallaxScreenSize, "inputTexture",
        m_envComposeDeferred ? m_envCache.name() : String("main"),
        {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", false}});
      m_renderer->switchEffectConfig("world");                        // world effect (binds main)
      m_renderer->setRenderTarget(String("parallaxRef"), parallaxScreenSize);   // -> parallaxRef, effect stays "world"
      m_renderer->setBlendMode(BlendMode::Alpha);
      drawParallax();
      m_renderer->flush();
    }

    // Composite into "main" every frame. CM-1: if the env compose was deferred, do the MERGED pass (env opaque
    // base + parallax premultiplied-over, one full-screen quad via backdropCompose); otherwise the standard
    // premultiplied-over parallax composite over the env already sitting in main.
    [[maybe_unused]] static bool const parallaxComposeGpuDesc = [] {
      Telemetry::declare("render.pass.parallax.compose.gpu_us",
        MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
      return true;
    }();
    m_renderer->gpuTimer().begin("render.pass.parallax.compose.gpu_us");
    if (m_envComposeDeferred) {
      mergedCompose(parallaxScreenSize);
      m_envComposeDeferred = false;
    } else {
      m_renderer->setBlendMode(BlendMode::PremultipliedOver);
      m_renderer->composite("lightingPassthrough", "main", parallaxScreenSize, "inputTexture", m_parallaxCache.name(),
        {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f}, {"tonemap", false}, {"preserveAlpha", true}});
      m_renderer->setBlendMode(BlendMode::Alpha);
      m_renderer->switchEffectConfig("world");   // restore world effect + "main" target for the world layers
    }
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
}

}
