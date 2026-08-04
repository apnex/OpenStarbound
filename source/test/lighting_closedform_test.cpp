#include "StarCellularLighting.hpp"
#include "StarCellularLightArray.hpp"
#include "StarJson.hpp"

#include "gtest/gtest.h"

#include <cmath>

// CLOSED-FORM ASSERTIONS ON PRODUCTION LIGHTING OUTPUT.
//
// Every other lighting test in this suite is DIFFERENTIAL: it compares production against
// pointLightingReference or spreadJacobiReference, or CPU against GPU. Those are the right
// instrument for drift -- they turn red when one of three implementations moves. What they
// structurally cannot catch is a shared misunderstanding: the references were written FROM
// production, so a rule that was wrong when they were authored is wrong identically on both sides
// and every differential test stays green.
//
// These assertions are the complement. Each expected value is derived HERE, from the attenuation
// rule, and never from any implementation. A reader can check the arithmetic without opening the
// engine. They are deliberately few and deliberately hand-checkable -- their value is that they
// are readable as statements about the MODEL.
//
// THE MODEL, for an unobstructed cell with no spread light present, one white point light of
// intensity 1, beam 0, asSpread false:
//
//   attenuation      = distance / pointMaxAir            (linear in Euclidean distance)
//   obstacle term    = 0                                 (no obstacles on the ray)
//   ColoredLightTraits::subtract((1,1,1), a) has max 1-a (hue-preserving, so max drops by exactly a)
//   pointAdditive    => cell += that value
//   brightnessLimit  => the vector is scaled so max == limit, if it exceeded it
//
//   => cell light max == 1 - distance / pointMaxAir
//
// DISTANCE IS MEASURED FROM THE CELL CENTRE. Production forms blockPos = (x + 0.5, y + 0.5), so a
// light placed on a cell centre (a .5 world coordinate) gives whole-number distances to other cell
// centres. The array-relative conversion cancels out of the difference, so world coordinates can be
// used directly below.

namespace Star {

namespace {
  float const kPointMaxAir = 12.0f;
  float const kBrightnessLimit = 1.4f;

  // pointObstacleBoost and the spread parameters are present because setParameters requires them;
  // no test here places an obstacle or a spread light, so they never enter an expected value.
  Json closedFormConfig() {
    return Json::parseJson(R"JSON({
      "spreadPasses": 0, "spreadMaxAir": 8.0, "spreadMaxObstacle": 2.0,
      "pointMaxAir": 12.0, "pointMaxObstacle": 4.0, "pointObstacleBoost": 1.0,
      "pointAdditive": true, "brightnessLimit": 1.4
    })JSON");
  }

  RectI const kQuery = RectI::withSize(Vec2I(0, 0), Vec2I(48, 48));

  // One white unit light per call site, on a cell CENTRE so distances come out whole.
  Lightmap litBy(List<Vec2F> const& lightCentres) {
    CellularLightingCalculator calc;
    calc.setParameters(closedFormConfig());
    calc.setMonochrome(false);
    calc.begin(kQuery);
    for (auto const& c : lightCentres)
      calc.addPointLight(c, Vec3F(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f, false);
    Lightmap out;
    calc.calculate(out);
    return out;
  }

  // Light on the centre of cell (20, 24).
  Vec2F const kLight = Vec2F(20.5f, 24.5f);

  // fp32 accumulates a few ULP through magnitude() and the proportional subtract; this is far
  // tighter than one 8-bit level (1/255 = 0.0039), so it still pins the value and not merely a band.
  float const kTol = 1e-5f;
}

// The core rule: brightness falls off LINEARLY with distance, reaching zero at pointMaxAir.
// Three points on the line, so a change of slope fails even if a change of intercept would not.
TEST(LightingClosedForm, AirAttenuationIsLinearInDistance) {
  Lightmap map = litBy({kLight});

  // Cell (23,24) centre is (23.5,24.5); the light is at (20.5,24.5). Distance 3.
  EXPECT_NEAR(map.get(23, 24).max(), 1.0f - 3.0f / kPointMaxAir, kTol);   // 0.75
  // Cell (26,24): distance 6.
  EXPECT_NEAR(map.get(26, 24).max(), 1.0f - 6.0f / kPointMaxAir, kTol);   // 0.50
  // Cell (29,24): distance 9.
  EXPECT_NEAR(map.get(29, 24).max(), 1.0f - 9.0f / kPointMaxAir, kTol);   // 0.25
}

// Distance is EUCLIDEAN, not Manhattan or Chebyshev. The three would agree on an axis-aligned
// offset, which is why this case exists: at (+3,+3) Manhattan says 6 and Chebyshev says 3, and
// only sqrt(18) is right. This is the assertion that would catch a "circularization" mistake.
TEST(LightingClosedForm, DiagonalDistanceIsEuclidean) {
  Lightmap map = litBy({kLight});

  float const diagonal = std::sqrt(18.0f);   // (+3,+3) from the light
  EXPECT_NEAR(map.get(23, 27).max(), 1.0f - diagonal / kPointMaxAir, kTol);

  // State the discrimination rather than implying it: the wrong metrics give visibly other answers.
  EXPECT_GT(std::abs((1.0f - diagonal / kPointMaxAir) - (1.0f - 6.0f / kPointMaxAir)), 0.1f);
  EXPECT_GT(std::abs((1.0f - diagonal / kPointMaxAir) - (1.0f - 3.0f / kPointMaxAir)), 0.1f);
}

// The zero-distance branch is a special case in production (it skips attenuation entirely rather
// than computing a division by zero), so it needs its own assertion: a light exactly on a cell
// centre leaves that cell at full intensity.
TEST(LightingClosedForm, LightOnACellCentreIsUndimmed) {
  Lightmap map = litBy({kLight});
  EXPECT_NEAR(map.get(20, 24).max(), 1.0f, kTol);
}

// The rule reaches exactly zero at pointMaxAir, and production discards the cell rather than
// writing a negative value. Checks the boundary and one cell beyond it.
TEST(LightingClosedForm, NoLightBeyondPointMaxAir) {
  Lightmap map = litBy({kLight});

  // Distance 12 == pointMaxAir: attenuation is exactly 1, which fails the `attenuation < 1` test.
  EXPECT_NEAR(map.get(32, 24).max(), 0.0f, kTol);
  EXPECT_NEAR(map.get(35, 24).max(), 0.0f, kTol);
  // And a cell just inside is still lit, so the test is not passing because everything is dark.
  EXPECT_NEAR(map.get(31, 24).max(), 1.0f - 11.0f / kPointMaxAir, kTol);
}

// brightnessLimit is applied as a proportional scale on the final value, not a per-channel clamp,
// so the max lands exactly on the limit. Two additive lights are needed to exceed 1.
TEST(LightingClosedForm, BrightnessLimitScalesTheSumToTheLimit) {
  // One light on the cell centre (contributes 1.0), one at distance 3 (contributes 0.75).
  Lightmap map = litBy({kLight, Vec2F(23.5f, 24.5f)});

  // Unclamped the cell would be 1.75; the limit is 1.4.
  EXPECT_NEAR(map.get(20, 24).max(), kBrightnessLimit, kTol);

  // The sum genuinely exceeded the limit -- otherwise this test would pass on an unclamped build.
  EXPECT_GT(1.0f + (1.0f - 3.0f / kPointMaxAir), kBrightnessLimit);
}

}
