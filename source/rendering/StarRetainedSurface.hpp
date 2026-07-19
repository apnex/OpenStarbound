#pragma once

#include "StarString.hpp"
#include "StarVector.hpp"

namespace Star {

// A retained render-surface cache's DECISION STATE — the part that was hand-rolled twice in
// StarWorldPainter (the env cache and the parallax cache). It owns the named cache framebuffer's
// refresh bookkeeping: a frame counter for the fixed/adaptive N-cadence gate, and the SHARED
// structural invalidation key (the size + camera pixelRatio the cache was last filled at).
//
// It is deliberately a pure value object: no Renderer, no GL. The per-pass GL (setRenderTarget /
// clear / draw / blend / flush / composite) is genuinely per-pass, not duplicated, and stays with
// the caller; so does any pass-specific extra invalidation term (parallax's camera position +
// content-key), because it has no env counterpart to dedup against. Keeping the GL out makes this
// unit-testable off the GPU and makes the byte-identical migration a pure relocation of state.
//
// Usage per active frame:
//   bool refresh = surf.invalidated(size, pixelRatio) || surf.cadenceHit(refreshInterval);
//   if (refresh) { /* setRenderTarget(surf.name(), size); clear; draw; ... */ surf.recordFilled(size, pixelRatio); }
//   /* composite surf.name() into the target */
// A consumer whose cache is bypassed that frame (direct path) calls surf.invalidate() so the next
// entry force-refreshes instead of compositing stale content; the shared framebuffer-generation
// drop likewise calls surf.invalidate() on every retained surface.
class RetainedSurface {
public:
  // initialPixelRatio is the pre-fill sentinel for the invalidation key: a value no real camera pixelRatio
  // equals, so a freshly-constructed surface invalidates. Defaults to -1.0f (the env cache, and the robust
  // choice); the parallax cache passes 0.0f to reproduce its EXACT pre-migration init so the extraction stays
  // strictly byte-identical (an adversarial byte-identity check flagged that unifying both caches onto one
  // sentinel diverged in an unreachable corner).
  explicit RetainedSurface(String cacheName, float initialPixelRatio = -1.0f)
    : m_name(std::move(cacheName)), m_pixelRatio(initialPixelRatio) {}

  // The name of the renderer framebuffer this cache draws into and composites from.
  String const& name() const { return m_name; }

  // Has the shared structural key moved since the last recordFilled? (a resize, or a camera zoom
  // change — the env pass scales stars/orbiters by pixelRatio, so a zoom alters the cached image at
  // an unchanged screen size). Force-refresh state (invalidate(), or a first frame) reads as changed.
  bool invalidated(Vec2U size, float pixelRatio) const {
    return size != m_size || pixelRatio != m_pixelRatio;
  }

  // The ordinary N-frame cadence gate: true once every refreshInterval frames. Advances the counter,
  // so call exactly once per frame the cache is on the retained path. refreshInterval is clamped to
  // >= 1 by callers; guard here too so a stray 0 can't divide-by-zero.
  bool cadenceHit(unsigned refreshInterval) {
    if (refreshInterval < 1)
      refreshInterval = 1;
    return (m_counter++ % refreshInterval) == 0;
  }

  // Record the structural key the cache was just filled at (call on a refresh frame, after drawing).
  void recordFilled(Vec2U size, float pixelRatio) {
    m_size = size;
    m_pixelRatio = pixelRatio;
  }

  // Force a refresh on the next entry: zero the recorded size so invalidated() is true. Used by the
  // direct-path bypass and the shared framebuffer-generation drop.
  void invalidate() {
    m_size = {0, 0};
  }

private:
  String m_name;
  uint64_t m_counter = 0;
  Vec2U m_size = {0, 0};
  float m_pixelRatio;   // pre-fill sentinel set by the ctor (initialPixelRatio); recordFilled overwrites it
};

}
