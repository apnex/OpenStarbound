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
// Logger::info() is swallowed. This is a de-risk PROOF test whose whole point is
// to record the convergence error and the minimal iteration count K, so the
// numbers are printed straight to stdout where they are actually observable.

using namespace Star;

namespace {

// Colored (non-monochrome) lighting config for the spread de-risk test.
// spreadMaxAir 8 -> ceil(spreadMaxAir) = 8 = the candidate locked iteration
// count K for the GPU shader.
Json spreadConfig() {
  return Json::parseJson(R"JSON({
    "spreadPasses": 4, "spreadMaxAir": 8.0, "spreadMaxObstacle": 2.0,
    "pointMaxAir": 12.0, "pointMaxObstacle": 4.0, "pointObstacleBoost": 1.0,
    "pointAdditive": true, "brightnessLimit": 1.4
  })JSON");
}

// Perceptual parity bar (NOT ulp): the sweep-vs-relax difference is algorithmic.
float const kMaxTol = 6.0f / 255.0f;
float const kMeanTol = 1.5f / 255.0f;

// One colored scene shared by both tests: two non-monochrome spread lights with
// a partial obstacle wall between them. Built twice from identical setup so the
// emission snapshot and the Gauss-Seidel reference never share mutated state.
struct SpreadScene {
  RectI queryRegion;
  RectI calcRegion;
  size_t width = 0;
  size_t height = 0;
  Vec2I arrayMin;            // query origin in array (calculation-region) coords
  SpreadParameters params{0.0f, 0.0f, 0.0f};
  Lightmap reference;        // production CPU sweep (Gauss-Seidel) + brightnessLimit
  List<Vec3F> emission;      // seeded pre-sweep light, array layout
  List<uint8_t> obstacle;    // obstacle flags, array layout
};

// Identical setup applied to every calculator instance.
void configure(CellularLightingCalculator& calc, RectI const& queryRegion) {
  calc.setParameters(spreadConfig());
  calc.setMonochrome(false);
  // begin() zero-fills the cell grid, so all cells default to air / no light.
  calc.begin(queryRegion);

  // Partial vertical obstacle wall at world x = 20, rows [10, 24); gaps above
  // and below let light bend around it, exercising the source-cell-keyed
  // obstacle dropoff. Light values are <= 1.0 so spread dies within
  // ceil(spreadMaxAir) = 8 cells -- which is exactly what makes K = 8 enough.
  for (int y = 10; y < 24; ++y)
    calc.setCellIndex(calc.baseIndexFor(Vec2I(20, y)), Vec3F(0.0f, 0.0f, 0.0f), true);

  calc.addSpreadLight(Vec2F(16.0f, 16.0f), Vec3F(0.95f, 0.78f, 0.55f)); // warm, left of wall
  calc.addSpreadLight(Vec2F(34.0f, 16.0f), Vec3F(0.55f, 0.78f, 0.95f)); // cool, right of wall
}

SpreadScene buildScene() {
  SpreadScene scene;
  scene.queryRegion = RectI::withSize(Vec2I(0, 0), Vec2I(48, 32));

  // Reference: full production sweep (seed -> Gauss-Seidel spread -> brightnessLimit).
  // No point lights are added, so calculate() is pure spread + cap.
  CellularLightingCalculator refCalc;
  configure(refCalc, scene.queryRegion);
  refCalc.calculate(scene.reference);

  // Snapshot: identical setup, seed only, read out emission + obstacle.
  CellularLightingCalculator snapCalc;
  configure(snapCalc, scene.queryRegion);
  scene.calcRegion = snapCalc.calculationRegion();
  scene.width = scene.calcRegion.width();
  scene.height = scene.calcRegion.height();
  scene.params = snapCalc.spreadParameters();
  snapCalc.snapshotSpreadInput(scene.emission, scene.obstacle);

  scene.arrayMin = scene.queryRegion.min() - scene.calcRegion.min();
  return scene;
}

// Run the Jacobi reference at K iterations and measure per-channel error against
// the production sweep over the query region. Returns {maxAbs, meanAbs}.
std::pair<float, float> jacobiError(SpreadScene const& scene, unsigned K) {
  List<Vec3F> jacobi = spreadJacobiReference(scene.emission, scene.obstacle,
      scene.width, scene.height, scene.params, K);

  double sum = 0.0;
  float maxErr = 0.0f;
  unsigned qw = scene.reference.width();
  unsigned qh = scene.reference.height();
  for (unsigned qx = 0; qx < qw; ++qx) {
    for (unsigned qy = 0; qy < qh; ++qy) {
      Vec3F ref = scene.reference.get(qx, qy);
      size_t ax = (size_t)scene.arrayMin[0] + qx;
      size_t ay = (size_t)scene.arrayMin[1] + qy;
      Vec3F jac = jacobi[ax * scene.height + ay];
      for (size_t c = 0; c < 3; ++c) {
        float e = std::fabs(ref[c] - jac[c]);
        maxErr = std::max(maxErr, e);
        sum += e;
      }
    }
  }
  float mean = (float)(sum / (double)(qw * qh * 3));
  return {maxErr, mean};
}

} // namespace

// Proves the parallel Jacobi relaxation (what the GPU shader will run) reaches
// the production Gauss-Seidel sweep within the perceptual bar at the candidate
// locked iteration count K = ceil(spreadMaxAir).
TEST(LightingSpread, JacobiConvergesToSweep) {
  SpreadScene scene = buildScene();
  ASSERT_FALSE(scene.reference.empty());

  unsigned K = (unsigned)std::ceil(scene.params.spreadMaxAir);
  auto error = jacobiError(scene, K);
  float maxErr = error.first;
  float meanErr = error.second;

  std::printf("[LightingSpread] K=%u (ceil(spreadMaxAir)): maxErr=%.4f/255, meanErr=%.4f/255 "
      "(tol max=6/255, mean=1.5/255)\n", K, maxErr * 255.0f, meanErr * 255.0f);
  std::fflush(stdout);

  EXPECT_LE(maxErr, kMaxTol);
  EXPECT_LE(meanErr, kMeanTol);
}

// Sweeps K upward and records the MINIMAL iteration count that meets tolerance.
// This LOCKS K for the GPU shader. If nothing converges within the budget, the
// GPU-spread approach is invalidated and this fails loudly with the data.
TEST(LightingSpread, MinimalConvergedKWithinBudget) {
  SpreadScene scene = buildScene();
  ASSERT_FALSE(scene.reference.empty());

  unsigned const kSearchMax = 16;
  unsigned minimalK = 0;
  for (unsigned K = 1; K <= kSearchMax; ++K) {
    auto error = jacobiError(scene, K);
    std::printf("[LightingSpread]   K=%2u -> maxErr=%7.4f/255, meanErr=%7.4f/255%s\n",
        K, error.first * 255.0f, error.second * 255.0f,
        (error.first <= kMaxTol && error.second <= kMeanTol) ? "  <= CONVERGED" : "");
    if (error.first <= kMaxTol && error.second <= kMeanTol) {
      minimalK = K;
      break;
    }
  }

  ASSERT_GT(minimalK, 0u)
      << "Jacobi did not converge within tolerance at any K <= " << kSearchMax
      << " -- this would INVALIDATE the GPU spread approach.";

  unsigned ceilMaxAir = (unsigned)std::ceil(scene.params.spreadMaxAir);
  std::printf("[LightingSpread] MINIMAL converged K = %u (ceil(spreadMaxAir) = %u)\n",
      minimalK, ceilMaxAir);
  std::fflush(stdout);
  EXPECT_LE(minimalK, ceilMaxAir)
      << "minimal K (" << minimalK << ") exceeds ceil(spreadMaxAir) (" << ceilMaxAir
      << "); K = ceil(spreadMaxAir) would be insufficient for the shader.";
}

// ── Task 2: emission + obstacle export ───────────────────────────────────────

// exportSpreadInputs seeds the spread lights into the cell grid (the pre-sweep
// emission state) and copies the full calculation region into two upload images:
// emission (RGB_F, per-cell Vec3F light) and obstacle (RGB24, 255 obstacle / 0
// air; the engine has no single-channel pixel format, so the shader will sample
// .r). Cells map from the array's column-major (x * height + y) layout to image
// pixel (x, y). This proves the GPU-upload grids match the cell state the
// production sweep actually consumes.
TEST(LightingSpread, ExportSpreadInputsMatchesCells) {
  RectI queryRegion = RectI::withSize(Vec2I(0, 0), Vec2I(48, 32));

  CellularLightingCalculator calc;
  configure(calc, queryRegion);
  RectI calcRegion = calc.calculationRegion();
  unsigned width = (unsigned)calcRegion.width();
  unsigned height = (unsigned)calcRegion.height();

  Image emission;
  Image obstacle;
  calc.exportSpreadInputs(emission, obstacle);

  // Calculation-region sized, correct formats.
  ASSERT_EQ(emission.pixelFormat(), PixelFormat::RGB_F);
  ASSERT_EQ(emission.width(), width);
  ASSERT_EQ(emission.height(), height);
  ASSERT_EQ(obstacle.width(), width);
  ASSERT_EQ(obstacle.height(), height);

  // Cross-check every cell against the seed-only snapshot, which reads cell.light
  // / cell.obstacle directly (the getLight/getObstacle equivalents) over the same
  // column-major layout.
  CellularLightingCalculator snapCalc;
  configure(snapCalc, queryRegion);
  List<Vec3F> emissionList;
  List<uint8_t> obstacleList;
  snapCalc.snapshotSpreadInput(emissionList, obstacleList);
  ASSERT_EQ(emissionList.size(), (size_t)width * height);

  float const* edata = (float const*)emission.data();
  size_t emissionMismatches = 0;
  size_t obstacleMismatches = 0;
  bool sawLight = false;
  for (unsigned x = 0; x < width; ++x) {
    for (unsigned y = 0; y < height; ++y) {
      size_t cellIndex = (size_t)x * height + y;
      Vec3F expected = emissionList[cellIndex];
      size_t pix = ((size_t)y * width + x) * 3;
      if (edata[pix] != expected[0] || edata[pix + 1] != expected[1] || edata[pix + 2] != expected[2])
        ++emissionMismatches;
      if (edata[pix] > 0.0f || edata[pix + 1] > 0.0f || edata[pix + 2] > 0.0f)
        sawLight = true;

      uint8_t obByte = obstacle.get24(x, y)[0];
      if (obByte != (obstacleList[cellIndex] ? 255 : 0))
        ++obstacleMismatches;
    }
  }
  EXPECT_EQ(emissionMismatches, 0u) << "emission image disagrees with seeded cell light";
  EXPECT_EQ(obstacleMismatches, 0u) << "obstacle image disagrees with seeded cell obstacle";
  EXPECT_TRUE(sawLight) << "emission has no light anywhere -- seeding failed";

  // Spot-check: the obstacle wall (world x = 20, rows [10, 24)) is marked 255.
  for (int wy = 10; wy < 24; ++wy) {
    unsigned ax = (unsigned)(20 - calcRegion.xMin());
    unsigned ay = (unsigned)(wy - calcRegion.yMin());
    EXPECT_EQ(obstacle.get24(ax, ay)[0], 255) << "wall cell not marked at world (20," << wy << ")";
  }
  // ... and an air cell far from the wall is 0.
  {
    unsigned ax = (unsigned)(5 - calcRegion.xMin());
    unsigned ay = (unsigned)(5 - calcRegion.yMin());
    EXPECT_EQ(obstacle.get24(ax, ay)[0], 0) << "air cell wrongly marked obstacle";
  }

  // Spot-check: emission is nonzero at a seeded spread-light position. The warm
  // light at world (16, 16) seeds the 2x2 corner cells around it.
  {
    unsigned ax = (unsigned)(16 - calcRegion.xMin());
    unsigned ay = (unsigned)(16 - calcRegion.yMin());
    size_t pix = ((size_t)ay * width + ax) * 3;
    EXPECT_GT(edata[pix] + edata[pix + 1] + edata[pix + 2], 0.0f)
        << "no emission at seeded light position";
  }
}
