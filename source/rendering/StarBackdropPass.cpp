#include "StarBackdropPass.hpp"
// StarRoot.hpp / StarConfiguration.hpp are DELIBERATELY ABSENT. Every Root::singleton() read left this file
// when BackdropParams took over (#137); the includes outlived them by a week. A needle-based lint greps for
// the CALL, so a pass can be architecturally re-coupled through an include while still measuring 0 -- the
// compile is the only thing that can prove this edge is gone, so let it.
#include "StarTelemetry.hpp"
#include "StarLogging.hpp"
#include "StarWorldCamera.hpp"
#include "StarMathCommon.hpp"
#include <cstdlib>

namespace Star {

BackdropPass::BackdropPass(Renderer* renderer)
  : m_renderer(renderer),
    // #181: registered HERE, so every key exists in snapshot() from frame zero whatever the frame does.
    // As conditional function-local statics these were absent until their branch first ran, making
    // ABSENT indistinguishable from ZERO for a consumer differencing two snapshots.
    m_composeRecovered(Telemetry::counter("render.backdrop.compose_recovered", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})),
    m_envRefreshedCtr(Telemetry::counter("render.cache.env.refreshed", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})),
    m_envSkippedCtr(Telemetry::counter("render.cache.env.skipped", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})),
    m_parallaxRefreshedCtr(Telemetry::counter("render.cache.parallax.refreshed", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})),
    m_parallaxSkippedCtr(Telemetry::counter("render.cache.parallax.skipped", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})),
    m_parallaxBypassedCtr(Telemetry::counter("render.cache.parallax.bypassed_moving", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})),
    // WITNESS for the backdropComposeMerge lever, counting the arm the lever turns OFF. Merge on => the env
    // compose is deferred into the parallax pass and this stays at zero; merge off => one per frame. Needed
    // because render.pass.parallax.compose.gpu_us fires on BOTH arms, so its count cannot say which ran.
    // It is not exactly zero when the merge is on: a clause-2 recovery frame suppresses the merge and takes
    // the standalone branch. Those are separately counted by render.backdrop.compose_recovered and are rare,
    // so the two legs still differ by ~frames rather than by a rounding error.
    m_envComposeStandaloneCtr(Telemetry::counter("render.cache.env.compose_standalone", MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail})) {}

// CM-1: the merged env+parallax compose. One full-screen quad, sampling the env cache (opaque backdrop) and
// the parallax cache (premultiplied coverage), writing "main" once via the backdropCompose effect:
//   out = P.rgb + E.rgb * (1 - P.a)
// which is algebraically the sequential (env opaque replace, then parallax premultiplied-over) result. Blend
// is irrelevant -- the shader writes the full opaque result and env covers every pixel, so "main" (still on
// the startFrame clear, since the env compose was deferred) is fully overwritten. Mirrors GpuLightmapPass's
// full-quad pattern.
namespace {
  // THE SAME FIVE UNIFORMS, WRITTEN SIX TIMES. This parameter block appeared verbatim at every
  // lightingPassthrough composite in this file -- the merged-compose fallback pair, the standalone env
  // compose, the deferred env compose, the oracle reference, and the parallax compose -- differing only
  // in preserveAlpha. Six copies of a shader's uniform contract is five chances for one to drift, and a
  // drifted brightnessLimit is a silently wrong sky rather than a crash.
  //
  // preserveAlpha is the ONLY real axis: the env cache is opaque and replaces, the parallax cache is
  // PREMULTIPLIED and blends over it, so it must keep its alpha.
  List<pair<String, RenderEffectParameter>> passthroughParams(bool preserveAlpha) {
    return {{"applyCap", false}, {"brightnessLimit", 1.4f}, {"brightnessScale", 1.0f},
            {"tonemap", false}, {"preserveAlpha", preserveAlpha}};
  }
}

void BackdropPass::mergedCompose(Vec2U const& size) {
  // GUARD (adversarial review, Lens 2): switchEffectConfig returns false and mutates nothing if backdropCompose
  // is unregistered (asset missing / mod override / corrupt install). An UNCHECKED draw would then go through
  // the still-bound "world" shader into "main" -- a garbage full-screen backdrop. Fall back to the two
  // sequential lightingPassthrough composites (always present), byte-identical to the non-merge path. Every
  // sibling compose site guards switchEffectConfig the same way (composite(), GpuLightmapPass).
  if (!m_renderer->switchEffectConfig("backdropCompose")) {
    m_renderer->composite("lightingPassthrough", "main", size, "inputTexture", m_envCache.name(),
      passthroughParams(false));
    m_renderer->setBlendMode(BlendMode::PremultipliedOver);
    m_renderer->composite("lightingPassthrough", "main", size, "inputTexture", m_parallaxCache.name(),
      passthroughParams(true));
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

// Composite the env cache into "main" ALONE -- every path where the merged compose does not happen. Called
// from BOTH entry points: renderEnvironment when the merge is off, and renderParallax when the merge was
// armed but parallax then bypassed. These were two verbatim copies, ten-line comment included.
//
// Cadence::Call, NOT Frame. This compose and render.pass.parallax.compose.gpu_us are the two MUTUALLY
// EXCLUSIVE arms of the compose decision: with the parallax cache bypassed (moving camera) the env
// composites standalone; when parked, the merged parallax compose does it instead. Declared Frame, each
// covers only its share of frames and coverage_scale reads the shortfall as sampling loss -- inflating
// BOTH. Measured in real play 2026-07-25: env 1448/2208 frames (x1.52) + parallax 760/2208 (x2.91) drove
// owner `gl` to 118.4%, parts exceeding the whole by 1279 us/tick. Absence here is genuine gating.
//
// That was invisible to the render gate for a STRUCTURAL reason, not an oversight: the harness camera never
// moves, so the bypass never engages and the split never happens. Only a live capture with movement can
// produce it -- which is the whole argument for #174.
//
// switchEffectConfig("world") restores the world effect + "main" target for the world layers and the
// non-GPU-lighting path. It is part of the compose, not the caller's business -- which is exactly why this
// belongs in one function rather than two.
void BackdropPass::composeEnvStandalone(Vec2U const& size) {
  m_renderer->gpuTimer().begin("render.pass.environment.compose.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
  m_renderer->composite("lightingPassthrough", "main", size, "inputTexture", m_envCache.name(),
    passthroughParams(false));
  m_renderer->gpuTimer().end("render.pass.environment.compose.gpu_us");
  m_renderer->switchEffectConfig("world");
}

void BackdropPass::renderEnvironment(WorldCamera const& camera, Input const& in,
    EnvironmentPainter& envPainter, BackdropParams const& params, bool ablateEnv) {
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

  // CLAUSE (2) OF THE TWO-ENTRY-POINT CONTRACT, NOW CHECKED INSTEAD OF ASSERTED IN PROSE.
  //
  // If the deferred flag is STILL set on entry, the previous frame set it and renderParallax never ran to
  // consume it -- exactly the abort the invariant below warns about, whose symptom is the WHOLE backdrop
  // going black rather than merely losing the parallax. Until now that contract lived only as a comment,
  // so the failure would have arrived as a bug report about a black sky with nothing in the log.
  //
  // DETECT AND RECOVER, rather than assert. A hard failure would turn a mod-induced or refactor-induced
  // ordering mistake into a crash on the Director's machine.
  //
  // AND UNTIL #180 THE RECOVERY DID NOT HAPPEN. This block used to reason that "falling through to the
  // reset below makes this frame composite env into main directly" -- but the reset at the bottom of this
  // comment only clears the flag, and the merge decision further down SETS IT AGAIN on the same frame,
  // because backdropComposeMerge ships TRUE. So the deferral repeated, the black frame repeated, and the
  // log line "Recovering by compositing env directly this frame" described an action nobody took. Then it
  // went silent after four frames, because the warn budget is a process-lifetime static.
  //
  // A logged recovery that does not recover is worse than no recovery: it converts an unmissable failure
  // into a reassuring one. The flag below now actually forces this frame onto the standalone path.
  // FAULT INJECTION, so the detector and the recovery branch can be EXECUTED rather than only read. The
  // clause-2 fault is unreachable in the shipped config -- one caller, ordering verified -- which is
  // exactly why a recovery that did not recover survived review: nothing could run it. Env-gated like the
  // rest of the harness knobs (STAR_RENDERTEST_*), read once, zero cost when unset. Arms on one frame past
  // warmup, so the cache is populated and the recovered frame is a realistic one.
  //
  // WHAT IT DOES NOT REPRODUCE, stated so nobody mistakes a green run for more than it is: this injects a
  // STALE DEFERRAL FLAG, not the abort that would produce one. renderParallax still runs on the injected
  // frame, so the frame renders correctly either way and the gate cannot distinguish fixed from broken.
  // What it proves is that the branch executes and is harmless. Reproducing the black frame needs
  // renderParallax to be SKIPPED for a frame, which is a caller-side knob this pass cannot provide.
  static bool const forceDefer = []() {
    char const* e = getenv("STAR_BACKDROP_FORCE_DEFER");
    return e && *e == '1';
  }();
  if (forceDefer) {
    static int entries = 0;
    if (++entries == 10) {
      Logger::info("[backdrop] STAR_BACKDROP_FORCE_DEFER: arming the clause-2 fault on this frame");
      m_envComposeDeferred = true;
    }
  }

  bool recoverThisFrame = m_envComposeDeferred;
  if (recoverThisFrame) {
    if (m_clause2WarnBudget > 0) {
      --m_clause2WarnBudget;
      Logger::error("[backdrop] the env compose was deferred and renderParallax never ran to issue it -- "
                    "the backdrop would have gone black. Recovering by compositing env directly this "
                    "frame. Both entry points must run on the same frame (BackdropPass clause 2).");
    }
    m_composeRecovered.inc(1);
  }

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
  unsigned envRefreshInterval = params.envRefreshInterval;
  if (envRefreshInterval < 1)
    envRefreshInterval = 1;
  bool envOracle = params.envOracle;
  // NB: the oracles' reference surfaces (envRef, parallaxRef) are marked devOnly and are only ALLOCATED while
  // an oracle is armed -- ClientApplication::render does that before the frame starts, because it reloads the
  // framebuffer set and must not run mid-frame. Both oracle paths below are already guarded by hasFrameBuffer,
  // so they correctly no-op on the frame where the surfaces have not appeared yet.

  // The env draw sequence, shared by every path (AA-direct, cache-refresh, oracle reference) so the cache
  // and its bit-identity reference can never silently diverge -- a hand-duplicated copy that drifted would
  // make the oracle lie. Draws into whatever render target / effect is currently bound.
  auto drawEnv = [&]() {
    if (ablateEnv) return;
    envPainter.renderStars(starAndDebrisRatio, Vec2F(camera.screenSize()), in.sky);
    envPainter.renderDebrisFields(starAndDebrisRatio, Vec2F(camera.screenSize()), in.sky);
    if (in.sky.type != SkyType::Atmosphereless)
      envPainter.renderBackOrbiters(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), in.sky);
    envPainter.renderPlanetHorizon(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), in.sky);
    envPainter.renderSky(Vec2F(camera.screenSize()), in.sky);
    envPainter.renderFrontOrbiters(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), in.sky);
    if (in.sky.type == SkyType::Atmosphereless)
      envPainter.renderBackOrbiters(orbiterAndPlanetRatio, Vec2F(camera.screenSize()), in.sky);
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
    // Cadence::Call, matching the cache arm below -- one name, one declared cadence. See the parallax pair.
    m_renderer->gpuTimer().begin("render.pass.environment.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
    drawEnv();
    m_renderer->gpuTimer().end("render.pass.environment.gpu_us");
  } else {
    // Cache path (AA off). Force a refresh on the first frame + after any resize (envCache is realloc'd
    // to undefined content) so a skip frame never composites garbage. The pixelRatio term is NOT redundant
    // with the size term: drawEnv scales stars/debris/orbiters by camera pixelRatio (starAndDebrisRatio /
    // orbiterAndPlanetRatio above), so a ZOOM change alters the cached image at an unchanged screen size --
    // without it, zooming left the sky stale until the counter next came round.
    bool envInvalidated = m_envCache.invalidated(envScreenSize, envPixelRatio);

    // THE MOTION TERM THE ENV CACHE NEVER HAD. `invalidated()` covers only size and pixelRatio, so
    // before this the predicate could not tell a static planet sky from a warp: during ship flight the
    // entire moving backdrop was resampled at 60/N Hz and held, teleporting hundreds of pixels per
    // visible update. Planet-side StarSky pins starOffset/worldOffset to EXACTLY {} and both translation
    // terms are identically zero, which is why the defect hid for so long -- and why adding this costs
    // those frames nothing.
    //
    // Bound the on-screen STEP, exactly as the parallax cache bounds its own with parallaxMaxDriftStepPx.
    // Rotations convert to pixels via the view half-diagonal, which is the displacement of the
    // worst-placed star on screen, so the bound is conservative rather than average.
    auto const& envSky = in.sky;
    float envHalfDiagPx = 0.5f * Vec2F(camera.screenSize()).magnitude();
    float envDriftPx = max(
        (envSky.starOffset - m_envCacheStarOffset).magnitude() * starAndDebrisRatio
          + fabsf(constrainAngle(envSky.starRotation - m_envCacheStarRotation)) * envHalfDiagPx,
        (envSky.worldOffset - m_envCacheWorldOffset).magnitude() * orbiterAndPlanetRatio
          + fabsf(constrainAngle(envSky.worldRotation - m_envCacheWorldRotation)) * envHalfDiagPx);
    float envMaxStepPx = params.envMaxDriftStepPx;
    bool envMotion = envDriftPx > envMaxStepPx;

    // CONTENT KEY -- only what changes the image WITHOUT moving it. Deliberately NOT a hash of the raw
    // sky fields: starOffset/worldOffset change every frame in flight, so hashing them would refresh
    // every frame and delete the cache's entire win. Motion is the drift term's job; this covers the
    // rest -- the hyperspace flash (which was itself fading in N-frame steps), the sky colours, the sky
    // type, and the star twinkle frame, which advances on whole epochTime seconds.
    ContentKey envKey;
    envKey.mix(envSky.flashColor.toRgba());
    envKey.mix(envSky.mainSkyColor.toRgba());
    envKey.mix(envSky.topRectColor.toRgba());
    envKey.mix(envSky.bottomRectColor.toRgba());
    envKey.mix(envSky.environmentLight.toRgba());
    envKey.mix((uint64_t)envSky.type);
    envKey.mixQuantized(envSky.skyAlpha);
    envKey.mixQuantized(envSky.dayLevel);
    envKey.mix((uint64_t)(int64_t)envSky.epochTime);   // star twinkle advances on whole seconds
    uint64_t envContentKey = envKey.value();
    bool envContentChanged = envContentKey != m_envCacheContentKey;

    // The three terms above are DISCRETIONARY -- each fires only when something actually changed.
    // shouldRefresh ORs the N-frame cadence onto them, and that operand is what makes the trade bounded:
    // it is the ceiling on staleness for anything drifting below the per-frame threshold but accumulating.
    // It lives in RetainedSurface so this call site cannot omit it; see the note at shouldRefresh.
    bool refreshEnv = m_envCache.shouldRefresh(envRefreshInterval, envInvalidated || envMotion || envContentChanged);
    (refreshEnv ? m_envRefreshedCtr : m_envSkippedCtr).inc(1);
    m_envRefreshedThisFrame = refreshEnv;

    if (refreshEnv) {
      // Bracket INSIDE the gate, Cadence::Call -- the act timed is the env redraw, not the frame. Straddling
      // recorded every skip frame as a ~0us sample: 80% of this timer's records were zeros, and the mean it
      // reported (~346us) was ~5x below the true per-redraw cost of ~1700us. See the parallax pair.
      m_renderer->gpuTimer().begin("render.pass.environment.gpu_us",
        MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
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
      // Record WHAT WAS DRAWN, beside the record of THAT it was drawn. Drift is measured from here, so
      // these must be written by the act that fills the cache and nowhere else -- the same discipline
      // that the descriptor-beside-the-spec rule exists for.
      m_envCacheStarOffset = envSky.starOffset;
      m_envCacheStarRotation = envSky.starRotation;
      m_envCacheWorldOffset = envSky.worldOffset;
      m_envCacheWorldRotation = envSky.worldRotation;
      m_envCacheContentKey = envContentKey;
      m_renderer->gpuTimer().end("render.pass.environment.gpu_us");
    }

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
    // pre-CM-1 the sky was already in "main" and only parallax was lost.
    //
    // CLAUSE (2) IS NOW MACHINE-CHECKED, at the top of this function: a deferred flag still set on entry means
    // the previous frame never issued the compose, and we log + recover to the direct path
    // (render.backdrop.compose_recovered). CLAUSE (1) IS STILL BY CARE and cannot be checked here -- the
    // Renderer exposes no notion of "who last wrote this target", so proving nothing writes "main" in the
    // window would need renderer-side support. Verified safe today (the lightmap phase writes only
    // lightingGpu* and restores world/main); anything added to that window must preserve it BY REVIEW.
    // `&& !recoverThisFrame` IS THE RECOVERY (#180). Without it this line re-armed the very deferral the
    // detector had just reported recovering from, on the same frame, every frame -- because the merge
    // ships enabled. Suppressing the merge for the recovering frame only sends it down the standalone
    // branch below, which is the pre-CM-1 behaviour and always safe, and costs one merged compose in a
    // frame that was already broken. The next frame re-arms normally.
    bool composeMerge = params.composeMerge && !recoverThisFrame;
    if (composeMerge) {
      m_envComposeDeferred = true;
    } else {
      // Composite the cached env into "main" every frame: a full-screen passthrough quad reusing
      // lightingPassthrough (nearest sampling; applyCap=false forces alpha=1.0 => a clean rgb replace of the
      // freshly-cleared main). composite() sets all four params explicitly, so the lighting compose's
      // mutations of the shared effect can't bleed in -- no forked config needed.
      //
      m_envComposeStandaloneCtr.inc(1);
      composeEnvStandalone(envScreenSize);
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
      bool atmosphereless = in.sky.type == SkyType::Atmosphereless;
      if (d.first == NPos)
        Logger::info("[envoracle] SKIPPED (absent fbo or size mismatch) N={} atmosphereless={}", envRefreshInterval, atmosphereless);
      else if (d.first == 0)
        Logger::info("[envoracle] MATCH (0 diff) N={} atmosphereless={}", envRefreshInterval, atmosphereless);
      else
        Logger::info("[envoracle] DIFF={} first=({},{}) N={} atmosphereless={}", d.first, d.second[0], d.second[1], envRefreshInterval, atmosphereless);
    }
  }
}

void BackdropPass::renderParallax(WorldCamera const& camera, Input const& in,
    EnvironmentPainter& envPainter, BackdropParams const& params, bool ablateParallax) {
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
  bool parallaxHasLayers = !in.parallaxLayers.empty();
  Vec2U parallaxScreenSize = m_renderer->screenSize();
  bool parallaxAntiAliasing = params.antiAliasing;
  float parallaxPixelRatio = camera.pixelRatio();
  bool parallaxOracle = params.parallaxOracle;

  // CONTENT-ADAPTIVE refresh interval. What the eye catches in a cached parallax is the per-refresh
  // positional STEP of its FASTEST-drifting layer: renderParallaxLayers scrolls each layer by
  // speed * (epochTime / dayLength), scaled by pixelRatio. Hold that step under a perceptual threshold and
  // the cache is imperceptible on ANY world -- static biomes (most of them) take a large N, fast-drifting
  // ones a small N -- instead of forcing the worst world's limit on every world. Snap DOWN to a validated
  // rung {1,2,4,8,16} (conservative). Config parallaxRefreshInterval: 0 = ADAPTIVE (default), 1 = off/direct,
  // >1 = manual fixed N. Threshold tunable via parallaxMaxDriftStepPx.
  unsigned parallaxRefreshCfg = params.parallaxRefreshInterval;
  float parallaxMaxStepPx = params.parallaxMaxDriftStepPx;

  // The drift rate is a property of the CONTENT, not of frame-to-frame tick jitter. A layer's screen offset
  // is speed * (epochTime / dayLength) * pixelRatio and epochTime advances ~1s per real second, so
  //     px/frame (nominal 60fps) = speed / (dayLength * 60) * pixelRatio.
  // (Do NOT derive this from a per-frame epochTime DELTA: skyRenderData only refreshes on WORLD TICKS, so
  //  that delta is exactly 0 on many render frames -- which made N flap between the static default and the
  //  real value every frame.)
  double parallaxDayLength = (double)in.sky.dayLength;
  unsigned parallaxAutoN = 16;       // static content: only the very slow day/night tint needs refreshing
  float parallaxMaxDriftPx = 0.0f;   // fastest layer's screen-pixel drift per frame
  bool parallaxAnimated = false;
  if (parallaxDayLength > 0.0) {
    for (auto const& layer : in.parallaxLayers) {
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
      in.parallaxLayers.size(), parallaxAnimated);
    m_lastLoggedParallaxN = parallaxRefreshInterval;
  }

  // Shared draw sequence (single lambda -> cache and its oracle reference can't diverge).
  auto drawParallax = [&]() {
    if (ablateParallax) return;
    if (parallaxHasLayers)
      envPainter.renderParallaxLayers(m_parallaxWorldPosition, camera, in.parallaxLayers, in.sky);
  };

  // CONTENT KEY -- everything OTHER than camera/zoom/size that changes the drawn image, and none of which was
  // in the old refresh key. renderParallaxLayers tints every non-unlit/non-lightMapped layer with
  // sky.environmentLight and fades it by floor(255*layer.alpha) (its drawColor computation) -- and
  // layer.alpha is exactly what the biome CROSSFADE and timeOfDayCorrelation animate. So a cached parallax
  // froze its day/night tint and stalled biome crossfades for up to N frames. Hash the same QUANTIZED values
  // the draw itself consumes, so the key moves iff the rendered image would.
  // (epochTime drift is deliberately absent: amortizing it over N frames is the whole point of the cache, and
  //  N is derived from it.)
  ContentKey parallaxKey;
  parallaxKey.mix(in.sky.environmentLight.toRgb());
  parallaxKey.mix(in.parallaxLayers.size());
  for (auto const& layer : in.parallaxLayers)
    parallaxKey.mixQuantized(layer.alpha);
  uint64_t parallaxContentKey = parallaxKey.value();

  // IS THE CAMERA MOVING RIGHT NOW? (vs. "does the cache hold a different position", which is a staleness
  // question and stays true on the first parked frame.)
  bool parallaxCameraMoving = (m_parallaxWorldPosition != m_parallaxPrevPosition)
      || (parallaxPixelRatio != m_parallaxPrevPixelRatio);
  m_parallaxPrevPosition = m_parallaxWorldPosition;
  m_parallaxPrevPixelRatio = parallaxPixelRatio;
  m_parallaxStillFrames = parallaxCameraMoving ? 0 : (m_parallaxStillFrames + 1);

  // MOVING-CAMERA BYPASS. Each layer scrolls by cameraDelta / parallaxValue_i (EnvironmentPainter's per-layer
  // parallaxValue divide),
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

  if (!parallaxCacheActive) {
    // Direct path (byte-identical stock parallax->main): camera moving, AA on, N<=1 oracle-off, or no layers.
    // Invalidate the cache so re-entry force-refreshes.
    m_parallaxCache.invalidate();
    if (parallaxHasLayers && !parallaxParked)
      m_parallaxBypassedCtr.inc(1);
    // CM-1 reconcile: renderEnvironment deferred the env compose for a merge that will NOT happen (parallax is
    // not caching this frame -- moving camera, AA on, no layers, or N<=1). Composite the env cache into "main"
    // now, standalone, so env still reaches "main" exactly once before the direct parallax draws over it. Same
    // passthrough the deferred env compose would have used; env stays byte-identical.
    if (m_envComposeDeferred) {
      composeEnvStandalone(parallaxScreenSize);
      m_envComposeDeferred = false;
    }
    // Cadence::Call, matching the cache arm below. The two arms alternate WITHIN a run (the gate includes
    // parallaxParked, which follows the camera), so a metric declared Frame here and Call there would be a
    // descriptor conflict on one name -- and Frame would be wrong regardless, since neither arm covers
    // every frame once the other can fire.
    m_renderer->gpuTimer().begin("render.pass.parallax.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
    drawParallax();
    m_renderer->gpuTimer().end("render.pass.parallax.gpu_us");
  } else {
    // Cache path (camera parked). INVALIDATION terms force a redraw regardless; the TIME gate is the ordinary
    // N-frame cadence and is the only one the arbiter may defer.
    bool parallaxInvalidated = m_parallaxCache.invalidated(parallaxScreenSize, parallaxPixelRatio)
        || (m_parallaxWorldPosition != m_parallaxCachePosition)
        || (parallaxContentKey != m_parallaxCacheContentKey);
    // The TIME gate: the N-frame cadence, plus a refresh the arbiter below deferred out of the previous
    // frame. shouldRefresh evaluates the cadence unconditionally, so the counter advances every active
    // frame whichever operand decides -- exactly the old separate `++m_parallaxRefreshCounter;` statement.
    bool parallaxTimeGate = m_parallaxCache.shouldRefresh(parallaxRefreshInterval, m_parallaxRefreshDeferred);

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
    (refreshParallax ? m_parallaxRefreshedCtr : m_parallaxSkippedCtr).inc(1);

    if (refreshParallax) {
      // The bracket opens INSIDE the gate, and the cadence is Call, because the act being timed is the
      // REDRAW -- not the frame that may or may not contain one. Straddling the gate recorded the 2-in-3
      // skip frames as ~0us samples: `count` counted frames rather than redraws, and the mean was a blend
      // of ~26 real redraws with ~180 zeros, which is neither a per-frame nor a per-redraw cost. It also
      // advanced the query ring once per frame against a refresh cadence of 3, phase-locking the one
      // expensive sample to a single ring slot so its capture went all-or-nothing. Same defect and same
      // fix as the compose pair (see composeEnvStandalone above), which was corrected on its own evidence.
      m_renderer->gpuTimer().begin("render.pass.parallax.gpu_us",
        MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
      m_renderer->setRenderTarget(m_parallaxCache.name(), parallaxScreenSize);
      m_renderer->clearRenderTarget(Vec4F(0.0f, 0.0f, 0.0f, 0.0f));   // transparent -> premultiplied accumulation
      m_renderer->setBlendMode(BlendMode::PremultiplyInto);
      drawParallax();
      m_renderer->flush();                          // render the parallax quads into the cache under PremultiplyInto
      m_renderer->setBlendMode(BlendMode::Alpha);   // restore the default blend
      m_parallaxCache.recordFilled(parallaxScreenSize, parallaxPixelRatio);
      m_parallaxCachePosition = m_parallaxWorldPosition;
      m_parallaxCacheContentKey = parallaxContentKey;
      m_renderer->gpuTimer().end("render.pass.parallax.gpu_us");
    }

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
        passthroughParams(false));
      m_renderer->switchEffectConfig("world");                        // world effect (binds main)
      m_renderer->setRenderTarget(String("parallaxRef"), parallaxScreenSize);   // -> parallaxRef, effect stays "world"
      m_renderer->setBlendMode(BlendMode::Alpha);
      drawParallax();
      m_renderer->flush();
    }

    // Composite into "main" every frame. CM-1: if the env compose was deferred, do the MERGED pass (env opaque
    // base + parallax premultiplied-over, one full-screen quad via backdropCompose); otherwise the standard
    // premultiplied-over parallax composite over the env already sitting in main.
    // Cadence::Call for the same reason as render.pass.environment.compose.gpu_us -- see the note there. The
    // two are mutually exclusive arms of the compose decision, so neither fires every frame, and declaring
    // either at Frame cadence makes coverage_scale inflate it.
    m_renderer->gpuTimer().begin("render.pass.parallax.compose.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
    if (m_envComposeDeferred) {
      mergedCompose(parallaxScreenSize);
      m_envComposeDeferred = false;
    } else {
      m_renderer->setBlendMode(BlendMode::PremultipliedOver);
      m_renderer->composite("lightingPassthrough", "main", parallaxScreenSize, "inputTexture", m_parallaxCache.name(),
        passthroughParams(true));
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
