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

TEST(RetainedSurfaceTest, CadenceHitsEveryN) {
  RetainedSurface s("c");
  EXPECT_TRUE(s.cadenceHit(4));    // frame 0
  EXPECT_FALSE(s.cadenceHit(4));   // 1
  EXPECT_FALSE(s.cadenceHit(4));   // 2
  EXPECT_FALSE(s.cadenceHit(4));   // 3
  EXPECT_TRUE(s.cadenceHit(4));    // 4
  EXPECT_FALSE(s.cadenceHit(4));   // 5
  EXPECT_FALSE(s.cadenceHit(4));   // 6
  EXPECT_FALSE(s.cadenceHit(4));   // 7
  EXPECT_TRUE(s.cadenceHit(4));    // 8
}

TEST(RetainedSurfaceTest, CadenceN1AlwaysHits) {
  RetainedSurface s("c");
  for (int i = 0; i < 5; ++i)
    EXPECT_TRUE(s.cadenceHit(1));
}

TEST(RetainedSurfaceTest, CadenceZeroClampsToOne) {
  // A stray 0 must not divide-by-zero; it behaves as N=1 (always hits).
  RetainedSurface s("c");
  for (int i = 0; i < 3; ++i)
    EXPECT_TRUE(s.cadenceHit(0));
}

TEST(RetainedSurfaceTest, CadenceAndInvalidationAreIndependent) {
  // The counter advances regardless of invalidation state, and vice versa.
  RetainedSurface s("c");
  s.recordFilled({100, 100}, 1.0f);
  EXPECT_TRUE(s.cadenceHit(4));                    // frame 0 hits
  EXPECT_FALSE(s.invalidated({100, 100}, 1.0f));   // key unchanged
  EXPECT_FALSE(s.cadenceHit(4));                   // frame 1: counter advanced independently
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
