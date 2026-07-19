#include "StarCellularLighting.hpp"

#include "gtest/gtest.h"

using namespace Star;

// Unit coverage for the CDL highlight tonemap operator (tonemapHighlights):
// value-preserving rolloff — identity for max-channel <= 1 (normal scenes
// untouched), smooth compression of the excess above 1 toward the white-point,
// hue-preserving (uniform RGB scale). Mirrored byte-for-byte in
// lightingPassthrough.frag. white == brightnessLimit (1.4 in the shipped config).

namespace {
  float maxChannel(Vec3F const& c) {
    float m = c[0];
    if (c[1] > m) m = c[1];
    if (c[2] > m) m = c[2];
    return m;
  }
}

TEST(CellularLightingTonemap, ZeroStaysZero) {
  Vec3F out = tonemapHighlights(Vec3F(0.0f, 0.0f, 0.0f), 1.4f);
  EXPECT_FLOAT_EQ(out[0], 0.0f);
  EXPECT_FLOAT_EQ(out[1], 0.0f);
  EXPECT_FLOAT_EQ(out[2], 0.0f);
}

TEST(CellularLightingTonemap, NormalRangeUntouched) {
  // max channel 0.5 <= 1.0 must pass through unchanged (no midtone darkening)
  Vec3F c(0.5f, 0.3f, 0.1f);
  Vec3F out = tonemapHighlights(c, 1.4f);
  EXPECT_NEAR(out[0], 0.5f, 1e-6f);
  EXPECT_NEAR(out[1], 0.3f, 1e-6f);
  EXPECT_NEAR(out[2], 0.1f, 1e-6f);
}

TEST(CellularLightingTonemap, HighlightsRolledOffHuePreserved) {
  Vec3F c(2.0f, 1.0f, 0.5f);          // max channel 2.0 > 1.0
  Vec3F out = tonemapHighlights(c, 1.4f);
  float scale = out[0] / c[0];        // uniform scale => hue preserved
  EXPECT_NEAR(out[1] / c[1], scale, 1e-5f);
  EXPECT_NEAR(out[2] / c[2], scale, 1e-5f);
  EXPECT_LT(maxChannel(out), 1.4f);   // under the white-point ceiling
  EXPECT_GT(maxChannel(out), 1.0f);   // but brighter than the [0,1] knee
}

TEST(CellularLightingTonemap, MonotonicAndBoundedByWhite) {
  float prev = -1.0f;
  for (float i = 1.0f; i <= 50.0f; i += 0.5f) {
    float m = maxChannel(tonemapHighlights(Vec3F(i, i, i), 1.4f));
    EXPECT_GE(m, prev);   // monotonic non-decreasing
    EXPECT_LT(m, 1.4f);   // never reaches/exceeds the white-point ceiling
    prev = m;
  }
}
