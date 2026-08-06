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
// THE THREE-TERM CONTRACT. `invalidated()` here is only the STRUCTURAL key. A correct retained cache
// needs all three of the following, and this class supplies exactly one of them:
//
//   1. STRUCTURAL  (here)    -- size, pixelRatio. The cached image is the wrong SHAPE.
//   2. MOTION      (caller)  -- the source moved on screen since the fill. Bound the per-refresh STEP in
//                               PIXELS against a perceptual threshold, measured as displacement SINCE THE
//                               FILL rather than as a predicted rate: sky/world data refreshes on WORLD
//                               TICKS, so a per-frame delta reads 0 on many render frames (that is what
//                               made the parallax cache's adaptive N flap every frame). Staleness is
//                               monotone across skipped ticks and needs no fps or velocity assumption.
//   3. CONTENT     (caller)  -- the image changed WITHOUT moving. Hash only the non-positional inputs;
//                               hashing positional ones refreshes every frame while the source moves and
//                               deletes the cache's entire reason to exist.
//
// SHIPPING WITH ONLY (1) AND THE CADENCE IS A REAL DEFECT, NOT A THEORETICAL ONE. The env cache did
// exactly that and reached the Director as visibly choppy stars during ship flight and system warp: with
// no motion term it resampled a backdrop moving ~34 px/frame at 60/N Hz, so the sky teleported ~138 px
// per visible update. It hid for so long because planet-side StarSky pins starOffset/worldOffset to
// EXACTLY {} -- the term it was missing is identically zero in the case everyone tests. Fixed in #177;
// see BackdropPass::renderEnvironment for the worked example of (2) and (3).
//
// Usage per active frame:
//   bool refresh = surf.invalidated(size, pixelRatio) || motion || contentChanged || surf.cadenceHit(N);
//   if (refresh) { /* setRenderTarget(surf.name(), size); clear; draw; ... */ surf.recordFilled(size, pixelRatio); }
//   /* composite surf.name() into the target */
// A consumer whose cache is bypassed that frame (direct path) calls surf.invalidate() so the next
// entry force-refreshes instead of compositing stale content; the shared framebuffer-generation
// drop likewise calls surf.invalidate() on every retained surface.
// L2's SECOND TERM. The three-term contract above says a cache needs structural + motion + content, and
// this class used to supply only the first -- while the content key was hand-written TWICE inside its own
// single consumer, with the same two FNV-1a constants copied out by eye (BackdropPass's env key and
// parallax key). The header's own stated justification for extracting RetainedSurface was that the
// decision state had been "hand-rolled twice"; the identical duplication had simply moved down a level.
//
// DELIBERATELY TAKES Vec4B/Vec3B, NOT Color -- but NOT for the reason this comment used to give.
//
// It claimed that accepting Color would "drag StarColor.hpp across the seam" and cost the core-only link.
// That is false, and was false when written: StarColor.hpp lives in source/core/, so including it would
// have cost this header nothing. A boundary defended by a build claim that does not hold invites the next
// author to test the claim, find it hollow, and conclude the whole boundary is soft.
//
// THE REAL FENCE is that nothing here names StarRenderer.hpp (source/application). That is what
// layer1_layering enforces, and what actually keeps retained_surface_test linkable against core + base
// alone with no GL context. Vec4B/Vec3B is then a narrower judgement rather than a structural one, and
// still a sound one: a four-byte conversion the caller can do itself does not earn a dependency.
//
// The quantisers are here because rounding is exactly where two hand-written keys drift apart.
class ContentKey {
public:
  void mix(uint64_t v) { m_hash = (m_hash ^ v) * 1099511628211ull; }
  void mix(Vec4B const& v) { mix(v[0]); mix(v[1]); mix(v[2]); mix(v[3]); }
  void mix(Vec3B const& v) { mix(v[0]); mix(v[1]); mix(v[2]); }

  // Hash the QUANTISED value the draw itself consumes, so the key moves iff the rendered image would.
  // A raw float would move on every tick of a slow fade and refresh the cache for a sub-LSB change.
  //
  // THE CLAMP IS NOT DEFENSIVE PADDING; WITHOUT IT THIS FUNCTION IS NOT A FUNCTION. Converting a float
  // to `unsigned` is UNDEFINED BEHAVIOUR outside the destination range, and #182 measured what that
  // costs under this build's own flags (-O3 -ffast-math): two keys built from different over-range
  // inputs PRINTED THE SAME VALUE AND COMPARED UNEQUAL, and out-of-range results differed between -O0
  // and -O3. A content hash that disagrees with itself is worse than a wrong one.
  //
  // IT IS REACHABLE, not theoretical. The three call sites are skyAlpha, dayLevel and parallax
  // layer.alpha -- and layer.alpha comes from mod-authorable parallax JSON, so a negative alpha is an
  // authoring mistake away on a heavily-modded install. The symptom would not be a crash: it is a
  // parallax cache whose key is garbage, so it either thrashes or spuriously matches and composites a
  // stale sky.
  //
  // 4294967040.0f, not 4294967295.0f, and that distinction is the whole bug in miniature: 4294967295 is
  // NOT representable as a float and rounds UP to 2^32, so clamping to it would itself be out of range.
  // 0xFFFFFF00 is the largest float below 2^32. Verified total and IDENTICAL at -O0 and -O3 -ffast-math;
  // in-range values are bit-unchanged, because nothing in [0, 2^32) touches either bound.
  //
  // NaN is deliberately NOT claimed: this build compiles with -ffast-math (-ffinite-math-only), under
  // which clang says using a NaN at all is undefined. That is a build-wide property no leaf function can
  // fix, so asserting NaN behaviour here would be an oracle run outside its contract.
  void mixQuantized(float v, float scale = 255.0f) {
    float q = floor(scale * v);
    q = (q >= 0.0f) ? q : 0.0f;
    q = (q <= 4294967040.0f) ? q : 4294967040.0f;
    mix((uint64_t)(unsigned)q);
  }

  uint64_t value() const { return m_hash; }
  bool changedFrom(uint64_t previous) const { return m_hash != previous; }

private:
  uint64_t m_hash = 1469598103934665603ull;   // FNV-1a offset basis
};

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

  // THE REFRESH DECISION, AND THE ONLY DOOR TO IT. `forced` carries every discretionary reason the
  // caller has — invalidation, motion, a moved content key — and this ORs the N-frame cadence onto it.
  //
  // THE CADENCE OPERAND IS THE STALENESS BOUND, which is why it lives here and not at the call site.
  // A retained surface serves an image drawn on some earlier frame; what makes that a bounded trade
  // rather than an open-ended one is that the cadence fires unconditionally every N frames whatever
  // else is true. That bound is the entire content of the backdrop cache levers' declared "output
  // differs by bounded staleness" claim. It used to be one operand of an expression inside
  // BackdropPass, which no test constructs: deleting it compiled clean, passed every gate, and made
  // staleness unbounded. Now a consumer cannot obtain the frame counter without also obtaining the
  // ceiling — cadenceHit is private, and this is the only caller.
  //
  // `forced` is evaluated by the caller and the cadence is evaluated HERE, unconditionally, before the
  // OR. Short-circuiting the cadence behind `forced` would stop the counter on forced frames and shift
  // the whole N-cadence phase, so a surface forced often enough would drift off its own schedule.
  // RetainedSurfaceTest.ForcedRefreshDoesNotStopTheCounter holds that.
  bool shouldRefresh(unsigned refreshInterval, bool forced) {
    bool cadence = cadenceHit(refreshInterval);
    return forced || cadence;
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
  // PRIVATE ON PURPOSE — see shouldRefresh. The ordinary N-frame cadence gate: true once every
  // refreshInterval frames. Advances the counter, so it is called exactly once per frame the cache is on
  // the retained path, which shouldRefresh guarantees by being its only caller. refreshInterval is
  // clamped to >= 1 by callers; guard here too so a stray 0 can't divide-by-zero.
  bool cadenceHit(unsigned refreshInterval) {
    if (refreshInterval < 1)
      refreshInterval = 1;
    return (m_counter++ % refreshInterval) == 0;
  }

  String m_name;
  uint64_t m_counter = 0;
  Vec2U m_size = {0, 0};
  float m_pixelRatio;   // pre-fill sentinel set by the ctor (initialPixelRatio); recordFilled overwrites it
};

}
