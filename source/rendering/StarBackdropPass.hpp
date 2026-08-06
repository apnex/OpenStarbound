#pragma once

// AIR-GAP CONTRACT (1). StarWorldRenderData.hpp is DELIBERATELY ABSENT: it drags in eleven headers --
// StarEntity, StarWorldTiles, StarCellularLighting, StarThread, StarParticle and the rest of the sim
// vocabulary -- and this pass needs exactly two of them. That is the whole buildability argument made
// concrete: a pass that includes the fat struct cannot compile without the world it draws.
#include "StarSkyRenderData.hpp"
#include "StarParallax.hpp"
#include "StarTelemetry.hpp"
#include "StarEnvironmentPainter.hpp"
#include "StarRenderer.hpp"
#include "StarRetainedSurface.hpp"

namespace Star {

STAR_CLASS(BackdropPass);

// The sky/environment + parallax backdrop pass. Owns the two retained caches (env + parallax) and the
// cross-surface refresh arbiter that couples them. It has TWO entry points, not one, because the world's
// lightmap phase runs BETWEEN the env and parallax draws in a frame: renderEnvironment() is called early,
// renderParallax() after lighting. Both share m_cacheFrameBufferGeneration (an FBO-generation drop invalidates
// both caches) and m_envRefreshedThisFrame (the arbiter defers a purely time-gated parallax refresh off any
// frame the env cache is already redrawing on) -- which is exactly why they are one pass.
//
// The EnvironmentPainter (the actual draw engine for sky/parallax) is passed in by reference; this pass owns
// only the caching/compose/arbiter decision state, not the painter.
// AIR-GAP CONTRACT (2): THE PASS'S CONFIG, RESOLVED AT THE BOUNDARY.
//
// These eight knobs used to be read inside the pass body via Root::singleton(), which is the
// reach-into-the-world coupling the decomposition exists to remove: it makes the pass impossible to
// exercise without a Root, and impossible to reason about without knowing what a global might return.
// WorldPainter -- the composition root, which is ALLOWED to know about Root -- now fills this per frame
// and hands the pass a value. The pass becomes a pure function of its parameters.
//
// RESOLVED PER FRAME AT THE BOUNDARY, NOT INJECTED AT CONSTRUCTION, and the distinction is load-bearing.
// The target-state doc prescribed constructor injection; that would have SHIPPED A REGRESSION, because
// every one of these is live-tunable mid-session (`/rendercache envrefresh`, `/rendercache
// parallaxrefresh`, the antiAliasing client option polled every frame). Freezing them at construction
// would silently break the console knobs the campaign has been using to A/B its own levers.
//
// This is the same shape WorldPainter already uses for GpuLightmapPass, which is why that pass scores a
// clean sheet -- a fact worth stating plainly, since it means the clean sheet was BOUGHT by the
// orchestrator doing the reads rather than earned by the pass avoiding them. The honest measure is "pass
// bodies are pure functions of their parameters", which this makes true for BackdropPass too.
struct BackdropParams {
  unsigned envRefreshInterval = 1;
  bool envOracle = false;
  float envMaxDriftStepPx = 0.75f;
  bool composeMerge = true;
  bool antiAliasing = false;
  bool parallaxOracle = false;
  unsigned parallaxRefreshInterval = 0;
  float parallaxMaxDriftStepPx = 0.75f;
};

class BackdropPass {
public:
  explicit BackdropPass(Renderer* renderer);

  // WorldPainter::renderInit() is called AGAIN on every world entry and on renderer recreation, but the
  // WorldPainter (and its single BackdropPass) is long-lived. The pre-extraction code kept this cache/arbiter
  // state as inline WorldPainter members that renderInit never reset, so it PERSISTED across world entries.
  // WorldPainter therefore constructs BackdropPass once and calls setRenderer() on subsequent renderInits to
  // refresh the (possibly recreated) renderer WITHOUT wiping the retained state -- reconstructing instead would
  // reset the caches mid-session and diverge (stale-sky / park-timing) from the pre-extraction behavior.
  void setRenderer(Renderer* renderer) { m_renderer = renderer; }

  // Force both retained caches to redraw on their next frame. Called from WorldPainter::renderInit so a fresh
  // world (or a recreated renderer) never composites the PREVIOUS world's cached sky. The frame-loop FBO-
  // generation drop already covers a renderer whose generation bumped; this also covers a SAME-renderer world
  // entry, where the env cache's refresh key (size + pixelRatio + counter, no content term) would otherwise
  // hold world A's sky for up to N-1 frames. (Parallax self-heals via its content key + camera position.)
  // Also resets the clause-2 warn budget (#181). The budget existed to stop a broken caller logging every
  // frame, but as a process-lifetime static a four-line burst bought SESSION-LONG silence -- including
  // across world re-entry, which is exactly when an operator would most expect a fresh diagnosis. World
  // entry is a new context; the rate limit should be too.
  void invalidateCaches() {
    m_envCache.invalidate();
    m_parallaxCache.invalidate();
    m_clause2WarnBudget = 4;
  }

  // AIR-GAP CONTRACT (1) FOR THIS PASS: a sliced const view of the frame, not the frame.
  //
  // BackdropPass reads exactly two members of WorldRenderData -- skyRenderData and parallaxLayers -- and
  // writes neither. Measured, not assumed. Taking the fat struct therefore bought nothing but a
  // dependency on the entire simulation snapshot, and the eleven headers behind it.
  //
  // A view of const references, not a copy: these are per-call, live for the duration of the call, and
  // copying a parallax layer list per frame to satisfy a contract would be the contract costing more than
  // the coupling it removes. Reference members make that explicit -- this type cannot be stored, and is
  // not meant to be.
  struct Input {
    SkyRenderData const& sky;
    List<ParallaxLayer> const& parallaxLayers;
  };

  // Draw + compose the environment (sky/stars/debris/orbiters) into "main" via the envCache retained surface,
  // refreshing it on the envRefreshInterval cadence / on resize / zoom / FBO-generation drop. Records whether
  // the env cache refreshed this frame, for the parallax arbiter in renderParallax(). ablateEnv suppresses the
  // draws for the rendertest pass-ablation harness.
  void renderEnvironment(WorldCamera const& camera, Input const& in, EnvironmentPainter& envPainter,
      BackdropParams const& params, bool ablateEnv);

  // Draw + compose the parallax layers over "main" via the parallaxCache retained surface. Updates the parallax
  // world position from camera drift; applies the moving-camera bypass, the AA gate, content-adaptive N, and
  // the cross-surface arbiter. ablateParallax suppresses the draws for the rendertest harness.
  void renderParallax(WorldCamera const& camera, Input const& in, EnvironmentPainter& envPainter,
      BackdropParams const& params, bool ablateParallax);

private:
  // CM-1: one full-screen pass sampling BOTH caches into "main" (env opaque base + parallax premultiplied-
  // over), replacing the two sequential composites. Own a screen-space quad buffer (rebuilt on resize) like
  // GpuLightmapPass. Only used on the both-caches-active path; the env compose is deferred from
  // renderEnvironment (m_envComposeDeferred) and issued here so env reaches "main" exactly once.
  void mergedCompose(Vec2U const& size);

  // The OTHER arm of the compose decision: env alone into "main", when the merge cannot happen. Called from
  // BOTH entry points -- renderEnvironment when the merge is off, and renderParallax when the merge was armed
  // but parallax then bypassed. The two call sites were verbatim copies of each other, comment included, which
  // is how a compose decision comes to be described twice and maintained once.
  void composeEnvStandalone(Vec2U const& size);

  RenderBufferPtr m_fullQuadBuffer;
  Vec2U m_fullQuadSize = {0, 0};
  // Set by renderEnvironment when the merge is enabled AND the env cache is active: the env->main compose is
  // skipped there and reconciled in renderParallax (merged if parallax also caches, standalone otherwise).
  bool m_envComposeDeferred = false;
  // Log the merged compose once, when it first engages -- empirical confirmation the lever is live (not a
  // silently-inert default) + observability, mirroring the [parallaxauto] one-shot line.
  bool m_backdropMergeLogged = false;

  Renderer* m_renderer;

  // REGISTERED AT CONSTRUCTION, NOT ON FIRST USE (#181). These were function-local statics inside
  // CONDITIONAL blocks, which meant a path never taken produced an ABSENT key in snapshot() -- and a
  // consumer differencing two snapshots cannot distinguish ABSENT from ZERO. compose_recovered was the
  // worst case: by construction it did not exist until the fault had already fired, so the one metric
  // whose zero is the interesting reading was the one metric that could not report zero.
  //
  // Same bug class as the block-scope statics that never REGISTER in runtime-gated functions. Members
  // registered in the constructor exist from frame zero, whatever the frame does.
  TelemetryCounter m_composeRecovered;
  TelemetryCounter m_envRefreshedCtr;
  TelemetryCounter m_envSkippedCtr;
  TelemetryCounter m_parallaxRefreshedCtr;
  TelemetryCounter m_parallaxSkippedCtr;
  TelemetryCounter m_parallaxBypassedCtr;
  TelemetryCounter m_envComposeStandaloneCtr;

  // Rate limit for the clause-2 diagnostic. A MEMBER, reset by invalidateCaches() on world entry --
  // see the comment there.
  int m_clause2WarnBudget = 4;

  // Renderer framebuffer generation the retained caches were last filled under. A renderer config reload
  // (the hdr / antiAliasing client options, both polled every frame) destroys and re-creates EVERY FBO with
  // UNDEFINED content, which no other refresh-key term can see -- size, camera and counter are all unchanged
  // across it. Both caches invalidate on a bump, or they composite undefined GPU memory.
  uint64_t m_cacheFrameBufferGeneration = 0;

  // Did the env cache do its heavy redraw this frame? Set by renderEnvironment(), read by the parallax refresh
  // arbiter in renderParallax(), which defers a purely time-gated parallax refresh off any frame env is also
  // redrawing on. A member (not a per-call local) because the two draws span the lightmap phase between them.
  bool m_envRefreshedThisFrame = false;

  // Environment-cache decision state (envRefreshInterval): the N-frame refresh cadence + the structural
  // invalidation key (screen size + camera pixelRatio the cache was last filled at). A mismatch forces a
  // refresh on the first frame + after resize; {0,0} also forces one on the first AA-off frame after MSAA
  // was on. The env draw scales stars/debris/orbiters by camera pixelRatio (starAndDebrisRatio /
  // orbiterAndPlanetRatio), so a zoom alters the cached image at an unchanged screen size -- hence
  // pixelRatio is part of the key, not just size. Bookkeeping owned by RetainedSurface (StarRetainedSurface.hpp).
  RetainedSurface m_envCache{"envCache"};
  // WHAT THE CACHED ENV IMAGE WAS DRAWN AT. The refresh predicate used to be `invalidated || cadence`,
  // and invalidated() means only "size or pixelRatio changed" -- so the env cache had NO MOTION TERM AT
  // ALL. That is harmless planet-side, where StarSky pins starOffset/worldOffset to exactly {} (not
  // merely small), which is why it went unnoticed. During ship flight and system warp the starfield
  // translates ~1000 view-units/s and the whole backdrop was resampled at 60/N Hz, teleporting hundreds
  // of pixels per visible update.
  //
  // These record the drawn-at state so accumulated drift SINCE THE FILL can be measured. Staleness, not
  // a predicted rate: skyRenderData refreshes only on WORLD TICKS, so a per-frame delta reads 0 on many
  // render frames -- the same trap that made the parallax cache's N flap every frame (see the warning in
  // renderParallax). Displacement since the fill is monotone across skipped ticks, needs no fps or
  // velocity assumption, and covers any future mode that moves the backdrop without moving the camera.
  Vec2F m_envCacheStarOffset = {0.0f, 0.0f};
  float m_envCacheStarRotation = 0.0f;
  Vec2F m_envCacheWorldOffset = {0.0f, 0.0f};
  float m_envCacheWorldRotation = 0.0f;
  uint64_t m_envCacheContentKey = 0;

  // Parallax retained-cache (SP-2): like the env cache, but the source scrolls with the camera, so the
  // refresh gate adds a camera-position term (+ content, below) on top of the shared size/pixelRatio(zoom)
  // key. The N-frame cadence + size/pixelRatio structural key live in the shared RetainedSurface (as for the
  // env cache); the scroll-with-camera position has no env counterpart, so it stays a loose member here.
  // Parked camera is bit-stable (StarWorldCamera dead-zone+snap) so exact-equality never fires spuriously.
  // The 0.0f initial pixelRatio sentinel reproduces this cache's EXACT pre-migration init (the env cache takes
  // the primitive's -1.0f default). Adversarial byte-identity verification flagged that unifying both caches
  // onto one sentinel left a state divergence in an unreachable corner (zero-size target + zero-zoom + origin +
  // content-hash 0); passing the original 0.0f keeps the migration strictly byte-identical. Hardening parallax
  // to the stronger -1.0f sentinel, if ever wanted, is a separate deliberate change with its own gate.
  RetainedSurface m_parallaxCache{"parallaxCache", 0.0f};
  Vec2F m_parallaxCachePosition = {0.0f, 0.0f};
  // Everything OTHER than camera/zoom/size that changes the drawn parallax image: renderParallaxLayers tints
  // each non-unlit/non-lightMapped layer with sky.environmentLight and fades it by floor(255*layer.alpha) --
  // and layer.alpha is exactly what the biome CROSSFADE and timeOfDayCorrelation animate. Neither was in the
  // refresh key, so a cached parallax froze its tint and stalled crossfades for up to N frames. Hashed over
  // the same quantities the draw actually quantizes to, so the key moves iff the image would.
  uint64_t m_parallaxCacheContentKey = 0;
  // Last frame's parallax anchor + zoom, so we can ask "is the camera moving RIGHT NOW" -- distinct from
  // "does the cache hold a different position", which stays true on the first parked frame and would
  // otherwise keep the bypass latched on forever. ParkFrames of stillness engages the cache.
  Vec2F m_parallaxPrevPosition = {0.0f, 0.0f};
  float m_parallaxPrevPixelRatio = 0.0f;
  unsigned m_parallaxStillFrames = 0;
  // Cross-surface refresh arbiter: a parallax TIME-gate firing on a frame the env cache is also refreshing
  // would stack both heavy redraws into one frame. Defer it by one frame instead. (The old +N/2 phase offset
  // could not do this: env fires on counter%4==0, so any even N whose half is a multiple of 4 -- notably the
  // static-content default N=16 -- collided on EVERY refresh.)
  bool m_parallaxRefreshDeferred = false;
  // Content-adaptive N: the last N we logged, so the [parallaxauto] line fires on change, not every frame.
  unsigned m_lastLoggedParallaxN = 0;

  // The parallax anchor (drifts from the camera center) + last frame's camera center, used to compute the
  // frame-to-frame drift. Parallax-specific; updated at the top of renderParallax().
  Vec2F m_previousCameraCenter;
  Vec2F m_parallaxWorldPosition;
};

}
