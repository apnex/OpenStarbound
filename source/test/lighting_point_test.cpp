#include "StarCellularLighting.hpp"
#include "StarCellularLightArray.hpp"
#include "StarCellularLightingOracle.hpp"
#include "StarImage.hpp"
#include "StarJson.hpp"
#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

// NOTE: the game_tests harness only registers an Error-level log sink, so
// Logger::info() is swallowed. This is the LOAD-BEARING de-risk PROOF for GPU
// point lighting: it proves (a) a CPU per-cell-per-light point reference using
// the EXACT production math matches production calculatePointLighting, and (b)
// that a GLSL-PORTABLE obstacle raycast (a fixed major-axis DDA with fpart/rfpart
// weighting + early-exit -- the form the fragment shader will use) also matches
// within perceptual tolerance. The error magnitudes + which raycast form meets
// tolerance are printed straight to stdout where they are actually observable.

using namespace Star;

namespace {

// Colored (non-monochrome) point/spread config for the de-risk test. Mirrors
// /lighting.config exactly.
Json pointConfig() {
  return Json::parseJson(R"JSON({
    "spreadPasses": 4, "spreadMaxAir": 8.0, "spreadMaxObstacle": 2.0,
    "pointMaxAir": 12.0, "pointMaxObstacle": 4.0, "pointObstacleBoost": 1.0,
    "pointAdditive": true, "brightnessLimit": 1.4
  })JSON");
}

// Perceptual parity bars (NOT ulp). The CPU reference vs production (both
// float32, SAME light order) is near-exact via the EXISTING lineAttenuation; the
// GLSL-portable DDA is an anti-aliased-line approximation, so it gets a looser
// bar (additive float-add order + eventual RGBA16F is a Task-5 concern).
float const kLineMaxTol = 4.0f / 255.0f;
float const kLineMeanTol = 1.0f / 255.0f;
float const kDdaMaxTol = 6.0f / 255.0f;
float const kDdaMeanTol = 2.0f / 255.0f;

// A point light defined in WORLD coordinates (converted to array-relative in
// buildScene, mirroring CellularLightingCalculator::addPointLight).
struct PointLightDef {
  Vec2F worldPos;
  Vec3F value;
  float beam;
  float beamAngle;
  float beamAmbience;
};

struct SpreadLightDef {
  Vec2F worldPos;
  Vec3F value;
};

// Main scene: a vertical obstacle wall, two plain omni point lights (one overlaps
// the other so the additive blend accumulates), and one beam light pointing into
// the wall. A single spread light gives a nonzero spread base, exercising the
// point-on-spread additive path. Light intensities <= 1.0 so the spread fully
// dies within ceil(spreadMaxAir) = 8 cells -- which makes the Jacobi spread base
// BIT-EXACT vs production at K = 8 (proven by lighting_spread_test), so this test
// isolates the POINT math.
PointLightDef const kPointLights[] = {
  {{16.0f, 24.0f}, {0.90f, 0.70f, 0.50f}, 0.0f, 0.0f, 0.0f},                 // warm plain omni
  {{26.0f, 14.0f}, {0.80f, 0.85f, 0.95f}, 0.0f, 0.0f, 0.0f},                 // cool plain omni (overlaps -> additive)
  {{50.0f, 24.0f}, {1.00f, 0.60f, 0.60f}, 2.0f, (float)Constants::pi, 0.2f}, // red beam pointing -x into the wall
};
SpreadLightDef const kSpreadLights[] = {
  {{12.0f, 38.0f}, {0.60f, 0.55f, 0.40f}},
};
int const kWallWorldX = 32;
int const kWallWorldY0 = 16;
int const kWallWorldY1 = 40;

struct PointScene {
  RectI queryRegion;
  RectI calcRegion;
  size_t width = 0;
  size_t height = 0;
  Vec2I arrayMin;                          // query origin in array (calc-region) coords
  PointParameters params{0, 0, 0, false, 0, 0, 0};
  Lightmap reference;                      // production CPU spread + point + brightnessLimit
  List<Vec3F> base;                        // Jacobi spread-only base (K = ceil(spreadMaxAir))
  List<uint8_t> obstacle;                  // obstacle flags, array column-major (x * height + y)
  List<ColoredCellularLightArray::PointLight> lights; // array-relative, production order
};

void configure(CellularLightingCalculator& calc, RectI const& queryRegion) {
  calc.setParameters(pointConfig());
  calc.setMonochrome(false);
  calc.begin(queryRegion);

  for (int y = kWallWorldY0; y < kWallWorldY1; ++y)
    calc.setCellIndex(calc.baseIndexFor(Vec2I(kWallWorldX, y)), Vec3F(0.0f, 0.0f, 0.0f), true);

  for (auto const& s : kSpreadLights)
    calc.addSpreadLight(s.worldPos, s.value);
  for (auto const& p : kPointLights)
    calc.addPointLight(p.worldPos, p.value, p.beam, p.beamAngle, p.beamAmbience, false);
}

PointScene buildScene() {
  PointScene scene;
  scene.queryRegion = RectI::withSize(Vec2I(0, 0), Vec2I(64, 48));

  // Reference: full production calculate() (seed -> Gauss-Seidel spread ->
  // calculatePointLighting -> brightnessLimit).
  CellularLightingCalculator refCalc;
  configure(refCalc, scene.queryRegion);
  refCalc.calculate(scene.reference);

  // Snapshot: identical setup, seed-only, read out emission + obstacle, then
  // build the spread base via the Slice-2 Jacobi reference at K = ceil(spreadMaxAir).
  CellularLightingCalculator snapCalc;
  configure(snapCalc, scene.queryRegion);
  scene.calcRegion = snapCalc.calculationRegion();
  scene.width = scene.calcRegion.width();
  scene.height = scene.calcRegion.height();
  scene.params = snapCalc.pointParameters();

  List<Vec3F> emission;
  snapCalc.snapshotSpreadInput(emission, scene.obstacle);
  SpreadParameters sp = snapCalc.spreadParameters();
  unsigned K = (unsigned)std::ceil(sp.spreadMaxAir);
  scene.base = spreadJacobiReference(emission, scene.obstacle, scene.width, scene.height, sp, K);

  // Build the array-relative point lights in PRODUCTION ORDER (insertion order),
  // mirroring CellularLightingCalculator::addPointLight's world->array conversion.
  Vec2F calcMin = Vec2F(scene.calcRegion.min());
  for (auto const& p : kPointLights)
    scene.lights.append({p.worldPos - calcMin, p.value, p.beam, p.beamAngle, p.beamAmbience, false});

  scene.arrayMin = scene.queryRegion.min() - scene.calcRegion.min();
  return scene;
}

// Apply the colored brightnessLimit cap exactly like CellularLightingCalculator::calculate.
Vec3F applyBrightnessLimit(Vec3F light, float brightnessLimit) {
  float intensity = ColoredLightTraits::maxIntensity(light);
  if (intensity > brightnessLimit)
    light *= brightnessLimit / intensity;
  return light;
}

// Run the point reference (with the chosen raycast), cap it, and measure
// per-channel error against the production Lightmap over the query region.
std::pair<float, float> pointError(PointScene const& scene, ObstacleRaycast raycast) {
  List<Vec3F> result = pointLightingReference(scene.base, scene.obstacle, scene.lights,
      scene.width, scene.height, scene.params, raycast);

  double sum = 0.0;
  float maxErr = 0.0f;
  unsigned qw = scene.reference.width();
  unsigned qh = scene.reference.height();
  for (unsigned qx = 0; qx < qw; ++qx) {
    for (unsigned qy = 0; qy < qh; ++qy) {
      Vec3F ref = scene.reference.get(qx, qy);
      size_t ax = (size_t)scene.arrayMin[0] + qx;
      size_t ay = (size_t)scene.arrayMin[1] + qy;
      Vec3F res = applyBrightnessLimit(result[ax * scene.height + ay], scene.params.brightnessLimit);
      for (size_t c = 0; c < 3; ++c) {
        float e = std::fabs(ref[c] - res[c]);
        maxErr = std::max(maxErr, e);
        sum += e;
      }
    }
  }
  float mean = (float)(sum / (double)(qw * qh * 3));
  return {maxErr, mean};
}

} // namespace

// Proves the CPU per-cell-per-light point reference -- using the EXACT production
// math + the EXISTING Xiaolin-Wu lineAttenuation for the obstacle term -- matches
// production calculatePointLighting near-exactly (same light order, CPU float32).
TEST(LightingPoint, ReferenceMatchesProduction) {
  PointScene scene = buildScene();
  ASSERT_FALSE(scene.reference.empty());

  auto error = pointError(scene, ObstacleRaycast::LineAttenuation);
  std::printf("[LightingPoint] lineAttenuation: maxErr=%.4f/255, meanErr=%.4f/255 "
      "(tol max=4/255, mean=1/255)\n", error.first * 255.0f, error.second * 255.0f);
  std::fflush(stdout);

  EXPECT_LE(error.first, kLineMaxTol);
  EXPECT_LE(error.second, kLineMeanTol);
}

// Proves the GLSL-PORTABLE obstacle raycast (fixed major-axis DDA with fpart/rfpart
// straddle weighting + early-exit; no recursion, loop bounded by maxRange -- the
// form the fragment shader will run) matches production within perceptual
// tolerance. This is the load-bearing Xiaolin-Wu-portability de-risk. Records
// which raycast form meets tolerance.
TEST(LightingPoint, PortableDDAMatchesProduction) {
  PointScene scene = buildScene();
  ASSERT_FALSE(scene.reference.empty());

  auto line = pointError(scene, ObstacleRaycast::LineAttenuation);
  auto dda = pointError(scene, ObstacleRaycast::PortableDDA);

  std::printf("[LightingPoint] portable DDA:   maxErr=%.4f/255, meanErr=%.4f/255 "
      "(tol max=6/255, mean=2/255)\n", dda.first * 255.0f, dda.second * 255.0f);
  std::printf("[LightingPoint] RAYCAST FORM LOCKED: lineAttenuation %s; portable-DDA %s\n",
      (line.first <= kLineMaxTol && line.second <= kLineMeanTol) ? "MEETS near-exact" : "FAILS near-exact",
      (dda.first <= kDdaMaxTol && dda.second <= kDdaMeanTol) ? "MEETS perceptual (USE THIS ON GPU)" : "FAILS perceptual");
  std::fflush(stdout);

  EXPECT_LE(dda.first, kDdaMaxTol)
      << "portable DDA raycast diverges from Xiaolin-Wu beyond perceptual tolerance "
         "-- point-on-GPU would need rethinking (keep point lighting on CPU).";
  EXPECT_LE(dda.second, kDdaMeanTol);
}

// Focused obstacle-term proof: a single omni light behind a wall. The cells in
// the wall's shadow must match production shadowing (the obstacle raycast works),
// and the shadow must be REAL (shadowed cell far darker than a near, lit cell)
// so the test is not vacuous.
TEST(LightingPoint, ShadowBehindWall) {
  RectI queryRegion = RectI::withSize(Vec2I(0, 0), Vec2I(48, 48));

  // Reference: production calculate() with one omni light + a wall, no spread.
  CellularLightingCalculator refCalc;
  refCalc.setParameters(pointConfig());
  refCalc.setMonochrome(false);
  refCalc.begin(queryRegion);
  for (int y = 12; y < 36; ++y)
    refCalc.setCellIndex(refCalc.baseIndexFor(Vec2I(26, y)), Vec3F(0.0f, 0.0f, 0.0f), true);
  refCalc.addPointLight(Vec2F(20.0f, 24.0f), Vec3F(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f, false);
  Lightmap reference;
  refCalc.calculate(reference);

  // Snapshot + base + array-relative light, same as the main scene.
  CellularLightingCalculator snapCalc;
  snapCalc.setParameters(pointConfig());
  snapCalc.setMonochrome(false);
  snapCalc.begin(queryRegion);
  for (int y = 12; y < 36; ++y)
    snapCalc.setCellIndex(snapCalc.baseIndexFor(Vec2I(26, y)), Vec3F(0.0f, 0.0f, 0.0f), true);
  snapCalc.addPointLight(Vec2F(20.0f, 24.0f), Vec3F(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f, false);

  RectI calcRegion = snapCalc.calculationRegion();
  size_t width = calcRegion.width();
  size_t height = calcRegion.height();
  PointParameters params = snapCalc.pointParameters();
  List<Vec3F> emission;
  List<uint8_t> obstacle;
  snapCalc.snapshotSpreadInput(emission, obstacle);
  SpreadParameters sp = snapCalc.spreadParameters();
  List<Vec3F> base = spreadJacobiReference(emission, obstacle, width, height, sp,
      (unsigned)std::ceil(sp.spreadMaxAir));

  Vec2F calcMin = Vec2F(calcRegion.min());
  List<ColoredCellularLightArray::PointLight> lights;
  lights.append({Vec2F(20.0f, 24.0f) - calcMin, Vec3F(1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f, false});

  List<Vec3F> result = pointLightingReference(base, obstacle, lights, width, height, params,
      ObstacleRaycast::LineAttenuation);

  Vec2I arrayMin = queryRegion.min() - calcRegion.min();

  // 1) Full-region parity: the obstacle term is replicated everywhere (shadow included).
  double sum = 0.0;
  float maxErr = 0.0f;
  for (unsigned qx = 0; qx < reference.width(); ++qx) {
    for (unsigned qy = 0; qy < reference.height(); ++qy) {
      Vec3F ref = reference.get(qx, qy);
      size_t ax = (size_t)arrayMin[0] + qx;
      size_t ay = (size_t)arrayMin[1] + qy;
      Vec3F res = applyBrightnessLimit(result[ax * height + ay], params.brightnessLimit);
      for (size_t c = 0; c < 3; ++c) {
        float e = std::fabs(ref[c] - res[c]);
        maxErr = std::max(maxErr, e);
        sum += e;
      }
    }
  }
  float mean = (float)(sum / (double)(reference.width() * reference.height() * 3));

  // 2) The shadow is REAL: cell behind the wall (world 30,24) is dark; a near
  // unobstructed cell (world 23,24) is bright.
  Vec3F prodShadow = reference.get(30, 24);
  Vec3F prodLit = reference.get(23, 24);
  Vec3F refShadow = result[((size_t)arrayMin[0] + 30) * height + ((size_t)arrayMin[1] + 24)];

  std::printf("[LightingPoint] shadow: full-region maxErr=%.4f/255 meanErr=%.4f/255; "
      "prodShadow.max=%.4f prodLit.max=%.4f\n",
      maxErr * 255.0f, mean * 255.0f, prodShadow.max(), prodLit.max());
  std::fflush(stdout);

  EXPECT_LE(maxErr, kLineMaxTol);
  EXPECT_LE(mean, kLineMeanTol);
  EXPECT_LT(prodShadow.max(), 0.1f) << "wall did not shadow the cell behind it";
  EXPECT_GT(prodLit.max(), 0.4f) << "near unobstructed cell should be brightly lit";
  EXPECT_LT(refShadow.max(), 0.1f) << "reference must reproduce the production shadow";
}

// Proves CellularLightingCalculator::exportPointLights hands the GPU point pass
// the exact point-light list that was added: same count, same insertion order,
// and per-light array-relative position (world - calcRegion.min), value, beam,
// beamAngle, beamAmbience, asSpread -- bit-for-bit what addPointLight stored.
TEST(LightingPoint, ExportPointLightsMatchesAdded) {
  struct AddedLight {
    Vec2F worldPos;
    Vec3F value;
    float beam;
    float beamAngle;
    float beamAmbience;
    bool asSpread;
  };
  // Varied position / value / beam / asSpread (one plain omni, one beam, one
  // asSpread-hybrid point) so each field is exercised independently.
  AddedLight const added[] = {
    {{18.0f, 22.0f}, {0.90f, 0.70f, 0.50f}, 0.0f, 0.0f, 0.0f, false},
    {{40.0f, 30.0f}, {0.30f, 0.80f, 0.95f}, 2.0f, (float)Constants::pi, 0.25f, false},
    {{12.5f, 9.0f},  {1.00f, 0.40f, 0.40f}, 1.5f, (float)Constants::pi * 0.5f, 0.10f, true},
  };

  RectI queryRegion = RectI::withSize(Vec2I(0, 0), Vec2I(64, 48));
  CellularLightingCalculator calc;
  calc.setParameters(pointConfig());
  calc.setMonochrome(false);
  calc.begin(queryRegion);

  // A wall, so the scene mirrors a real calc (not load-bearing for the export).
  for (int y = kWallWorldY0; y < kWallWorldY1; ++y)
    calc.setCellIndex(calc.baseIndexFor(Vec2I(kWallWorldX, y)), Vec3F(0.0f, 0.0f, 0.0f), true);

  for (auto const& a : added)
    calc.addPointLight(a.worldPos, a.value, a.beam, a.beamAngle, a.beamAmbience, a.asSpread);

  List<ColoredCellularLightArray::PointLight> lights;
  calc.exportPointLights(lights);

  ASSERT_EQ(lights.size(), sizeof(added) / sizeof(added[0]));

  Vec2F calcMin = Vec2F(calc.calculationRegion().min());
  for (size_t i = 0; i < lights.size(); ++i) {
    auto const& got = lights[i];
    auto const& want = added[i];
    Vec2F wantPos = want.worldPos - calcMin; // addPointLight's world->array conversion
    EXPECT_FLOAT_EQ(got.position[0], wantPos[0]) << "light " << i << " position.x";
    EXPECT_FLOAT_EQ(got.position[1], wantPos[1]) << "light " << i << " position.y";
    EXPECT_FLOAT_EQ(got.value[0], want.value[0]) << "light " << i << " value.r";
    EXPECT_FLOAT_EQ(got.value[1], want.value[1]) << "light " << i << " value.g";
    EXPECT_FLOAT_EQ(got.value[2], want.value[2]) << "light " << i << " value.b";
    EXPECT_FLOAT_EQ(got.beam, want.beam) << "light " << i << " beam";
    EXPECT_FLOAT_EQ(got.beamAngle, want.beamAngle) << "light " << i << " beamAngle";
    EXPECT_FLOAT_EQ(got.beamAmbience, want.beamAmbience) << "light " << i << " beamAmbience";
    EXPECT_EQ(got.asSpread, want.asSpread) << "light " << i << " asSpread";
  }
}

// Slice 4 regression guard. The GPU lightmap is calc-region-sized; WorldPainter offsets it by a
// "border" (cells) to sample the query region. WorldClient computes and carries that border as
// (calcWidth - queryWidth) / 2. This MUST equal the true per-axis padding between the calc region
// and the query region (queryRegion.min - calcRegion.min) on BOTH axes. The original Slice-4 bug
// reverse-derived the border from the CPU lightMap width instead -- which is empty (=> garbage
// offset, black world) once the redundant CPU calc is skipped. Lock the geometry so the carried
// value can't silently drift.
TEST(LightingPoint, GpuBorderMatchesCalcQueryPadding) {
  RectI const queries[] = {
    RectI::withSize(Vec2I(0, 0), Vec2I(64, 48)),
    RectI::withSize(Vec2I(-13, 7), Vec2I(33, 91)),    // non-zero, asymmetric origin
    RectI::withSize(Vec2I(5, -20), Vec2I(128, 16)),
  };
  for (auto const& queryRegion : queries) {
    CellularLightingCalculator calc;
    calc.setParameters(pointConfig());
    calc.setMonochrome(false);
    calc.begin(queryRegion);
    RectI calcRegion = calc.calculationRegion();

    int border = ((int)calcRegion.width() - (int)queryRegion.width()) / 2;  // WorldClient's formula
    Vec2I pad = queryRegion.min() - calcRegion.min();                       // WorldPainter's true offset

    EXPECT_GT(border, 0) << "border must be positive (spread padding) for query origin "
                         << queryRegion.min()[0] << "," << queryRegion.min()[1];
    EXPECT_EQ(border, pad[0]) << "carried border vs x-padding";
    EXPECT_EQ(border, pad[1]) << "carried border vs y-padding";
    EXPECT_EQ((int)calcRegion.width() - (int)queryRegion.width(),
              (int)calcRegion.height() - (int)queryRegion.height()) << "padding symmetric across axes";
  }
}
