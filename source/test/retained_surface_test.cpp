#include "StarRetainedSurface.hpp"

#include "gtest/gtest.h"

using namespace Star;

// RetainedSurface is a pure decision-state object (no GL), so its refresh logic is proven here off
// the GPU — the strengthening the L1 render-surface extraction never got. The byte-identical env /
// parallax migrations are gated separately by the in-frame render oracles.

TEST(RetainedSurfaceTest, NameRoundTrips) {
  RetainedSurface s("envCache");
  EXPECT_EQ(s.name(), "envCache");
}

TEST(RetainedSurfaceTest, FreshSurfaceIsInvalidated) {
  // Never filled: the recorded size is {0,0}, so any real query differs -> the first entry refreshes.
  RetainedSurface s("c");
  EXPECT_TRUE(s.invalidated({640, 360}, 1.0f));
}

TEST(RetainedSurfaceTest, RecordFilledClearsInvalidation) {
  RetainedSurface s("c");
  s.recordFilled({640, 360}, 1.0f);
  EXPECT_FALSE(s.invalidated({640, 360}, 1.0f));  // same key -> valid
  EXPECT_TRUE(s.invalidated({641, 360}, 1.0f));   // resize
  EXPECT_TRUE(s.invalidated({640, 360}, 2.0f));   // zoom (pixelRatio) change -> cached image differs
}

TEST(RetainedSurfaceTest, InvalidateForcesRefresh) {
  RetainedSurface s("c");
  s.recordFilled({640, 360}, 1.0f);
  ASSERT_FALSE(s.invalidated({640, 360}, 1.0f));
  s.invalidate();                                  // direct-path bypass / generation drop
  EXPECT_TRUE(s.invalidated({640, 360}, 1.0f));
}

// The cadence is asserted through shouldRefresh because that is the only door: cadenceHit is private,
// so no consumer can take the frame counter without also taking the bound it exists to impose.
TEST(RetainedSurfaceTest, CadenceHitsEveryN) {
  RetainedSurface s("c");
  EXPECT_TRUE(s.shouldRefresh(4, false));    // frame 0
  EXPECT_FALSE(s.shouldRefresh(4, false));   // 1
  EXPECT_FALSE(s.shouldRefresh(4, false));   // 2
  EXPECT_FALSE(s.shouldRefresh(4, false));   // 3
  EXPECT_TRUE(s.shouldRefresh(4, false));    // 4
  EXPECT_FALSE(s.shouldRefresh(4, false));   // 5
  EXPECT_FALSE(s.shouldRefresh(4, false));   // 6
  EXPECT_FALSE(s.shouldRefresh(4, false));   // 7
  EXPECT_TRUE(s.shouldRefresh(4, false));    // 8
}

TEST(RetainedSurfaceTest, CadenceN1AlwaysHits) {
  RetainedSurface s("c");
  for (int i = 0; i < 5; ++i)
    EXPECT_TRUE(s.shouldRefresh(1, false));
}

TEST(RetainedSurfaceTest, CadenceZeroClampsToOne) {
  // A stray 0 must not divide-by-zero; it behaves as N=1 (always hits).
  RetainedSurface s("c");
  for (int i = 0; i < 3; ++i)
    EXPECT_TRUE(s.shouldRefresh(0, false));
}

TEST(RetainedSurfaceTest, CadenceAndInvalidationAreIndependent) {
  // The counter advances regardless of invalidation state, and vice versa.
  RetainedSurface s("c");
  s.recordFilled({100, 100}, 1.0f);
  EXPECT_TRUE(s.shouldRefresh(4, false));          // frame 0 hits
  EXPECT_FALSE(s.invalidated({100, 100}, 1.0f));   // key unchanged
  EXPECT_FALSE(s.shouldRefresh(4, false));         // frame 1: counter advanced independently
}

TEST(RetainedSurfaceTest, ForcedRefreshDoesNotStopTheCounter) {
  // A forced refresh must not RESET or SKIP the cadence, or a surface that is forced often enough
  // silently loses its ceiling: the counter would keep being re-based and the modulo test would stop
  // landing. Frame 0 hits on cadence anyway; frames 1 and 2 are forced; frame 4 must still be the next
  // cadence hit, exactly as if nothing had been forced.
  RetainedSurface s("c");
  EXPECT_TRUE(s.shouldRefresh(4, false));   // 0 -- cadence
  EXPECT_TRUE(s.shouldRefresh(4, true));    // 1 -- forced
  EXPECT_TRUE(s.shouldRefresh(4, true));    // 2 -- forced
  EXPECT_FALSE(s.shouldRefresh(4, false));  // 3 -- neither
  EXPECT_TRUE(s.shouldRefresh(4, false));   // 4 -- cadence, unmoved by the two forces
}

TEST(RetainedSurfaceTest, ShouldRefreshBoundsStalenessAtN) {
  // THE STALENESS BOUND ITSELF, asserted on the decision rather than on the counter underneath it.
  //
  // This is the entire content of the two backdrop cache levers' declared "bounded staleness" trade:
  // with every discretionary reason false -- nothing invalidated, nothing moved, no content change --
  // the cadence alone must still fire within N calls, so a displayed frame is never more than N-1
  // frames old. Until this test the bound lived only in a call-site expression in BackdropPass, which
  // no test constructs; deleting the cadence operand there compiled clean and made staleness unbounded.
  // THE TRAILING GAP IS COUNTED, AND THE REFRESH COUNT IS ASSERTED. The first draft of this test did
  // neither, and an injection caught it: with the cadence operand deleted, shouldRefresh never returned
  // true, so the loop never entered the branch that records a gap, `worst` stayed at its initial 0, and
  // the test reported the bound HELD on a surface that had not refreshed once in 200 frames. A bound
  // check that only samples inside the event it is bounding measures nothing when the event stops.
  static constexpr int Frames = 200;
  for (unsigned n : {1u, 2u, 3u, 4u, 16u}) {
    RetainedSurface s("c");
    unsigned gap = 0, worst = 0, refreshes = 0;
    for (int i = 0; i < Frames; ++i) {
      if (s.shouldRefresh(n, false)) {
        ++refreshes;
        if (gap > worst)
          worst = gap;
        gap = 0;
      } else {
        ++gap;
      }
    }
    if (gap > worst)   // the run ends mid-gap; an unterminated gap is still a gap
      worst = gap;
    // CEIL, not floor: the counter starts at 0 and 0 % n == 0, so the first frame on the retained path
    // is always a refresh. At N=3 over 200 frames that is 67 refreshes, not 66.
    EXPECT_EQ(refreshes, ((unsigned)Frames + n - 1) / n) << "N=" << n << ": wrong number of refreshes";
    EXPECT_LE(worst, n - 1) << "N=" << n << ": went " << worst << " frames without a refresh";
  }
}

TEST(RetainedSurfaceTest, InitialPixelRatioSentinelIsConfigurable) {
  // The pre-fill pixelRatio sentinel is a per-consumer ctor argument: the env cache takes the -1.0f default,
  // the parallax cache passes 0.0f to reproduce its exact pre-migration init. Query at the fresh {0,0} size so
  // the size term does NOT force invalidation and the sentinel is the deciding term -- this is precisely the
  // (unreachable in the real render, but exercised here) corner the adversarial check flagged.
  RetainedSurface envLike("e");             // default sentinel -1.0f
  RetainedSurface parLike("p", 0.0f);       // parallax sentinel 0.0f
  EXPECT_TRUE(envLike.invalidated({0, 0}, 0.0f));    // 0.0f  != -1.0f -> invalidated
  EXPECT_FALSE(envLike.invalidated({0, 0}, -1.0f));  // -1.0f == -1.0f -> sentinel matched
  EXPECT_FALSE(parLike.invalidated({0, 0}, 0.0f));   // 0.0f  == 0.0f  -> sentinel matched (byte-identical to old)
  EXPECT_TRUE(parLike.invalidated({0, 0}, -1.0f));   // -1.0f != 0.0f  -> invalidated
}

// ---------------------------------------------------------------------------------------------------
// ContentKey. L2's other half, and until #182 the one thing this layer shipped with ZERO coverage --
// beside nine tests for its sibling. Its entire stated justification is that "rounding is exactly where
// two hand-written keys drift apart", which makes an untested quantiser the failure it was built to
// prevent. A drifted key's symptom is not a crash: it is a cache that returns a stale frame because two
// different scenes hashed equal, i.e. a silently wrong sky.

TEST(ContentKeyTest, IsDeterministicAcrossInstances) {
  // The whole point of the type: two independently-built keys over the same content must agree, because
  // that comparison is what decides whether the cache redraws.
  ContentKey a, b;
  a.mix(uint64_t(7)); a.mixQuantized(0.25f); a.mix(Vec3B(1, 2, 3));
  b.mix(uint64_t(7)); b.mixQuantized(0.25f); b.mix(Vec3B(1, 2, 3));
  EXPECT_EQ(a.value(), b.value());
  EXPECT_FALSE(a.changedFrom(b.value()));
}

TEST(ContentKeyTest, IsOrderSensitive) {
  // Content that differs only in ORDER must not collide -- otherwise swapping two parallax layers would
  // read as "unchanged" and the cache would keep compositing the old arrangement.
  ContentKey a, b;
  a.mix(uint64_t(1)); a.mix(uint64_t(2));
  b.mix(uint64_t(2)); b.mix(uint64_t(1));
  EXPECT_NE(a.value(), b.value());
}

TEST(ContentKeyTest, EmptyKeyIsTheFnvBasisAndAnyMixMovesIt) {
  // A fresh key must not equal a mixed one -- including mix(0), which a naive `hash ^= v` would leave
  // untouched. FNV multiplies as well as xors, so zero still advances the state.
  ContentKey fresh, zeroed;
  zeroed.mix(uint64_t(0));
  EXPECT_NE(fresh.value(), zeroed.value());
  EXPECT_TRUE(zeroed.changedFrom(fresh.value()));
}

TEST(ContentKeyTest, QuantisesWithinABucketAndSeparatesAcrossOne) {
  // The reason mixQuantized exists: a raw float would move the key on every tick of a slow fade and
  // refresh the cache for a sub-LSB change nobody can see. At scale 255, 0.5 and 0.5+1e-4 land in the
  // same bucket (127); 0.5 and 0.502 do not (127 vs 128).
  ContentKey same1, same2, other;
  same1.mixQuantized(0.5f);
  same2.mixQuantized(0.5f + 1e-4f);
  other.mixQuantized(0.502f);
  EXPECT_EQ(same1.value(), same2.value());
  EXPECT_NE(same1.value(), other.value());
}

TEST(ContentKeyTest, Vec3BAndVec4BDoNotCollideOnTheSameRgb) {
  // Vec4B mixes a fourth byte; Vec3B does not. The same rgb through the two overloads must therefore
  // differ -- an opaque colour and a colour-with-alpha are not the same content.
  ContentKey three, four;
  three.mix(Vec3B(10, 20, 30));
  four.mix(Vec4B(10, 20, 30, 0));
  EXPECT_NE(three.value(), four.value());
}

TEST(ContentKeyTest, QuantiserIsTotalOverOutOfRangeInput) {
  // THE UNTESTED EDGE, and it is reachable rather than theoretical: mixQuantized's three call sites are
  // skyAlpha, dayLevel and parallax layer.alpha -- and layer.alpha comes from mod-authorable JSON, so a
  // negative alpha is an authoring mistake away on a heavily-modded install. `(unsigned)floor(scale * v)`
  // outside the destination range is UNDEFINED BEHAVIOUR, which for a hash means a key that is not a
  // function of its input.
  //
  // THIS TEST FAILED BEFORE THE FIX, and what it exposed was worse than a wrong number: measured under
  // this build's own flags, two keys built from different over-range inputs PRINTED THE SAME VALUE AND
  // COMPARED UNEQUAL, and results differed between -O0 and -O3. The contract asserted here is TOTALITY.
  ContentKey zero, negative;
  zero.mixQuantized(0.0f);
  negative.mixQuantized(-0.1f);
  EXPECT_EQ(negative.value(), zero.value()) << "negative input must clamp to the zero bucket, not wrap";

  // The top end saturates: two enormous values agree with each other rather than landing in arbitrary
  // buckets that could collide with real content.
  ContentKey huge1, huge2;
  huge1.mixQuantized(1e12f);
  huge2.mixQuantized(1e13f);
  EXPECT_EQ(huge1.value(), huge2.value()) << "over-range input must saturate, not wrap";
  EXPECT_NE(huge1.value(), zero.value()) << "saturation must not collide with the zero bucket";

  // The clamp costs nothing where it matters: in-range values never touch either bound.
  ContentKey inRange;
  inRange.mixQuantized(0.5f);
  EXPECT_NE(inRange.value(), zero.value());
}

// NaN IS DELIBERATELY NOT TESTED, and the absence is a result rather than an oversight. This build
// compiles with -ffast-math (-ffinite-math-only), under which clang states plainly that using a NaN at
// all is undefined -- it warns on the literal. No leaf function can restore semantics the whole
// translation unit has been told to assume away, so an assertion here would be an oracle run outside its
// contract: green because the compiler was free to choose, not because the code is right.
