#pragma once

#include "StarWorldRenderData.hpp"
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
  void invalidateCaches() { m_envCache.invalidate(); m_parallaxCache.invalidate(); }

  // Draw + compose the environment (sky/stars/debris/orbiters) into "main" via the envCache retained surface,
  // refreshing it on the envRefreshInterval cadence / on resize / zoom / FBO-generation drop. Records whether
  // the env cache refreshed this frame, for the parallax arbiter in renderParallax(). ablateEnv suppresses the
  // draws for the rendertest pass-ablation harness.
  void renderEnvironment(WorldCamera const& camera, WorldRenderData& renderData, EnvironmentPainter& envPainter,
      bool ablateEnv);

  // Draw + compose the parallax layers over "main" via the parallaxCache retained surface. Updates the parallax
  // world position from camera drift; applies the moving-camera bypass, the AA gate, content-adaptive N, and
  // the cross-surface arbiter. ablateParallax suppresses the draws for the rendertest harness.
  void renderParallax(WorldCamera const& camera, WorldRenderData& renderData, EnvironmentPainter& envPainter,
      bool ablateParallax);

private:
  // CM-1: one full-screen pass sampling BOTH caches into "main" (env opaque base + parallax premultiplied-
  // over), replacing the two sequential composites. Own a screen-space quad buffer (rebuilt on resize) like
  // GpuLightmapPass. Only used on the both-caches-active path; the env compose is deferred from
  // renderEnvironment (m_envComposeDeferred) and issued here so env reaches "main" exactly once.
  void mergedCompose(Vec2U const& size);
  RenderBufferPtr m_fullQuadBuffer;
  Vec2U m_fullQuadSize = {0, 0};
  // Set by renderEnvironment when the merge is enabled AND the env cache is active: the env->main compose is
  // skipped there and reconciled in renderParallax (merged if parallax also caches, standalone otherwise).
  bool m_envComposeDeferred = false;
  // Log the merged compose once, when it first engages -- empirical confirmation the lever is live (not a
  // silently-inert default) + observability, mirroring the [parallaxauto] one-shot line.
  bool m_backdropMergeLogged = false;

  Renderer* m_renderer;

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
