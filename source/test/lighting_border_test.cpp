#include "StarCellularLighting.hpp"
#include "StarCellularLightArray.hpp"
#include "StarJson.hpp"
#include "gtest/gtest.h"

#include <cmath>

// The adaptive calculation border (#170) sizes the region a lighting recompute walks. #217 established
// that the border is not a work budget: it is the membership test for off-region point lights, because
// both executors DROP any light whose centre falls outside the grid --
// CellularLightArray::calculatePointLighting (StarCellularLightArray.cpp, both specializations) and
// GpuLightmapPass. The comment that shipped with #170 asserted the opposite ("costs performance, never
// correctness"), no assert stood behind it, and the two-argument begin() had zero coverage, so nothing
// could contradict it. These tests are that contradiction, kept executable.
using namespace Star;

namespace {

// Shipped /lighting.config values, so borderCells() == ceil(48) == 48 and spreadBorderCells() == 32 --
// the real clamp interval. A scaled-down config would not exercise the floor that does the damage.
Json borderConfig() {
  return Json::parseJson(R"JSON({
    "spreadPasses": 4, "spreadMaxAir": 32.0, "spreadMaxObstacle": 2.0,
    "pointMaxAir": 48.0, "pointMaxObstacle": 4.0, "pointObstacleBoost": 1.0,
    "pointAdditive": true, "brightnessLimit": 1.4
  })JSON");
}

RectI const kQuery = RectI::withSize(Vec2I(0, 0), Vec2I(64, 48));
float const kPointMaxAir = 48.0f;

// A SATURATED light: channel max 1.0, channel mean 0.333. The whole defect lives in that gap -- the
// engine's reach is maxIntensity * pointMaxAir with ColoredLightTraits::maxIntensity == value.max(),
// so this light reaches 48 cells, while a mean-based estimate credits it with 16.
Vec3F const kSaturatedRed = Vec3F(1.0f, 0.0f, 0.0f);

// 40 cells outside the query rect: inside the light's true reach of 48 (so it must contribute 8 cells
// of illumination), but outside the 32-cell spread floor the clamp falls back to.
//
// BOTH SIDES ARE TESTED, and that is not symmetry for its own sake. The region is the query rect padded
// by b, so the array spans world [min-b, max+b) -- half-open. On the MIN side the binding light lands at
// a valid index; on the MAX side it lands exactly one past the end. A min-side-only test passes with or
// without the half-open +1, which is precisely the hole an injection run found in the first draft of
// this file.
float const kLightDistance = 40.0f;
Vec2F const kLightPosMin = Vec2F(-kLightDistance, 24.0f);
Vec2F const kLightPosMax = Vec2F(64.0f + kLightDistance, 24.0f);

// Run one recompute with the given border argument and light, and return the query-region lightmap.
Lightmap renderWith(Maybe<unsigned> pointBorderNeeded, Vec2F const& lightPos);

// Min-side overload, kept so the existing cases read unchanged.
Lightmap renderWith(Maybe<unsigned> pointBorderNeeded) {
  return renderWith(pointBorderNeeded, kLightPosMin);
}

Lightmap renderWith(Maybe<unsigned> pointBorderNeeded, Vec2F const& lightPos) {
  CellularLightingCalculator calc;
  calc.setParameters(borderConfig());
  calc.setMonochrome(false);
  calc.begin(kQuery, pointBorderNeeded);

  // Empty air across the whole calculation region -- no obstacles, so attenuation is pure distance and
  // the light either reaches or is absent. Nothing else can explain a difference between two runs.
  RectI region = calc.calculationRegion();
  for (int x = region.xMin(); x < region.xMax(); ++x)
    for (int y = region.yMin(); y < region.yMax(); ++y)
      calc.setCellIndex(calc.baseIndexFor(Vec2I(x, y)), Vec3F(0.0f, 0.0f, 0.0f), false);

  calc.addPointLight(lightPos, kSaturatedRed, 0.0f, 0.0f, 0.0f, false);

  Lightmap out;
  calc.calculate(out);
  return out;
}

float maxRedAtEdge(Lightmap const& lm, unsigned x) {
  float peak = 0.0f;
  for (unsigned y = 0; y < lm.height(); ++y)
    peak = std::max(peak, lm.get(x, y)[0]);
  return peak;
}

float maxRedAtLeftEdge(Lightmap const& lm) {
  return maxRedAtEdge(lm, 0);
}

float maxRedAtRightEdge(Lightmap const& lm) {
  return maxRedAtEdge(lm, lm.width() - 1);
}

bool sameLightmap(Lightmap const& a, Lightmap const& b) {
  if (a.width() != b.width() || a.height() != b.height())
    return false;
  for (unsigned x = 0; x < a.width(); ++x) {
    for (unsigned y = 0; y < a.height(); ++y) {
      Vec3F va = a.get(x, y), vb = b.get(x, y);
      for (size_t c = 0; c < 3; ++c) {
        if (std::fabs(va[c] - vb[c]) > 1e-6f)
          return false;
      }
    }
  }
  return true;
}

} // namespace

// THE REFUTATION. #170's comment claimed a bad border "costs performance, never correctness". Under the
// static border the light contributes; under an under-sized border it is gone. If this test ever starts
// passing trivially (both sides dark), the scene stopped exercising the defect -- check the guard below.
TEST(LightingBorderTest, UnderSizedBorderDropsPointLightEntirely) {
  Lightmap statik = renderWith({});          // historic unconditional 48
  Lightmap floored = renderWith(unsigned(0)); // any under-estimate clamps to the 32 spread floor

  // Guard: the static border really does light the edge, so the comparison below is meaningful.
  ASSERT_GT(maxRedAtLeftEdge(statik), 0.0f)
      << "scene no longer exercises the defect: the light does not reach under the static border";

  EXPECT_EQ(0.0f, maxRedAtLeftEdge(floored))
      << "expected the light to be dropped whole by the off-grid guard";
  EXPECT_FALSE(sameLightmap(statik, floored))
      << "border under-estimate must change the OUTPUT -- this is the claim #170 denied";
}

// THE ESTIMATOR. pointBorderFor must credit the light with its channel-MAX reach, matching
// ColoredLightTraits::maxIntensity. A mean-based estimate returns 0 here (0.333 * 48 = 16 < 40).
TEST(LightingBorderTest, PointBorderForUsesChannelMaxNotMean) {
  unsigned border = CellularLightingCalculator::pointBorderFor(kQuery, kLightPosMin, kSaturatedRed, kPointMaxAir);
  EXPECT_GT(border, 0u) << "a saturated light inside its true reach must constrain the border";
  EXPECT_GE(border, (unsigned)std::ceil(kLightDistance))
      << "border must be at least the distance to the light";
}

// THE BINDING LIGHT. The array is half-open on the max side, so a light at distance d needs a border of
// d+1 to land inside it. Feeding pointBorderFor's answer to begin() must reproduce the static-border
// result exactly -- if it is short by one, the farthest light is silently dropped.
TEST(LightingBorderTest, EstimatedBorderReproducesStaticBorderExactly) {
  unsigned border = CellularLightingCalculator::pointBorderFor(kQuery, kLightPosMin, kSaturatedRed, kPointMaxAir);
  EXPECT_TRUE(sameLightmap(renderWith({}), renderWith(border)))
      << "the estimated border must not change the output the static border produces";
}

// THE HALF-OPEN EDGE. This is the case the min-side tests above cannot see. The array spans
// [min-b, max+b), so on the MAX side the binding light needs b >= d+1; a border of exactly ceil(d) puts
// it one index past the end and calculatePointLighting discards it. Dropping the +1 from pointBorderFor
// leaves every other test in this file green -- verified by injection -- so without this case the
// half-open correction would be unproven.
TEST(LightingBorderTest, MaxSideBindingLightLandsInsideTheArray) {
  unsigned border = CellularLightingCalculator::pointBorderFor(kQuery, kLightPosMax, kSaturatedRed, kPointMaxAir);

  Lightmap statik = renderWith({}, kLightPosMax);
  ASSERT_GT(maxRedAtRightEdge(statik), 0.0f)
      << "scene no longer exercises the defect: the light does not reach under the static border";

  EXPECT_GT(maxRedAtRightEdge(renderWith(border, kLightPosMax)), 0.0f)
      << "the max-side binding light was dropped -- the border is short by the half-open +1";
  EXPECT_TRUE(sameLightmap(statik, renderWith(border, kLightPosMax)))
      << "the estimated border must reproduce the static border on the max side too";
}

// The far side of the same rule: a light beyond its own reach must NOT inflate the border, or the lever
// buys nothing. (1,0,0) at 60 cells reaches 48, so it cannot touch the query region.
TEST(LightingBorderTest, OutOfReachLightDoesNotConstrainTheBorder) {
  EXPECT_EQ(0u, CellularLightingCalculator::pointBorderFor(
                    kQuery, Vec2F(-60.0f, 24.0f), kSaturatedRed, kPointMaxAir));
}
