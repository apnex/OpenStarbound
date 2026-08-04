#pragma once

#include "StarCellularLightArray.hpp"

// DIFFERENTIAL ORACLES for the lighting kernel -- independent reimplementations of the production
// spread and point sweeps, used to prove that a GPU/parallel form computes what the CPU sweep does.
//
// THEY ARE NOT TEST-ONLY, which is why they live in source/base and not source/test: StarWorldPainter
// runs both of them at runtime as a 3-way localizer, so that when the GPU/CPU parity check trips it
// can say WHICH side is wrong rather than only that they disagree.
//
// They are split out of StarCellularLightArray because they are APPARATUS, not kernel: they were 38%
// of that header and its .cpp, so a reader could not see what CellularLightArray IS, and every
// consumer of the kernel header parsed the oracle declarations whether it wanted them or not. The
// dependency runs oracle -> kernel and never the other way; SpreadParameters and PointParameters stay
// in the kernel header deliberately, because CellularLightingCalculator DECLARES them as its accessor
// return types and StarGpuLightmapPass carries PointParameters on the shipping GPU path. Moving those
// here would have made production depend on the oracle -- the opposite of the point.
//
// WHAT THEY CANNOT DO. A differential oracle written from production pins DRIFT, not TRUTH: it cannot
// catch a bug that was already there when it was written, because both sides were authored to the
// same understanding. The closed-form assertions in the lighting tests are the complement.

namespace Star {

// Parallel Jacobi-relaxation reference for the colored spread sweep
// (CellularLightArray::calculateLightSpread). Where the production sweep is a
// sequential Gauss-Seidel pass (it reads neighbours already updated this pass),
// this is the pure parallel relaxation a GPU fragment shader runs: every cell
// reads only the PREVIOUS iteration. For this max-propagation operation both
// converge to the same fixed point; this reference exists to PROVE that (and to
// discover the minimal iteration count K) before any shader work.
//
//   emission   : per-cell seeded light, column-major (x * height + y), matching
//                CellularLightArray's internal layout (see seedSpreadLights()).
//   obstacle   : per-cell obstacle flag (1/0), same layout. uint8_t (not bool)
//                to avoid the std::vector<bool> proxy in the hot loop.
//   iterations : number of Jacobi steps (K).
//
// Replicates ColoredLightTraits::spread verbatim, the SOURCE-cell-keyed dropoff
// (air vs obstacle), the sqrt2 diagonal factor, and applies brightnessLimit at
// the end exactly like CellularLightingCalculator::calculate.
List<Vec3F> spreadJacobiReference(List<Vec3F> const& emission, List<uint8_t> const& obstacle,
    size_t width, size_t height, SpreadParameters const& params, unsigned iterations);

// Selects the obstacle raycast pointLightingReference uses for the per-light
// shadow term:
//  - LineAttenuation: a verbatim mirror of CellularLightArray::lineAttenuation
//    (Xiaolin-Wu anti-aliased line + early-exit) over the obstacle grid. Gives
//    near-exact parity with production.
//  - PortableDDA: the GLSL-portable form a fragment shader will run -- a single
//    uniform-loop major-axis DDA with fpart/rfpart straddle weighting and
//    fractional endpoint coverage + early-exit, no recursion, loop bounded by the
//    major-axis cell distance. This is the Xiaolin-Wu-portability de-risk: it must
//    match production within a looser perceptual tolerance. (The endpoint coverage
//    term is required; dropping it diverges by ~26/255 at obstacle cells.)
enum class ObstacleRaycast {
  LineAttenuation,
  PortableDDA
};

// Parallel-friendly per-cell-per-light point reference for the colored point
// sweep (CellularLightArray<ColoredLightTraits>::calculatePointLighting). Runs
// the EXACT production per-cell math (air attenuation, beam cone with
// beamDirection = Vec2F(1,0).rotate(beamAngle), circularized obstacle term,
// single-obstacle boost when !asSpread, additive vs max blend, colored
// hue-preserving subtract) on top of a spread-only 'base', accumulating point
// lights in the given order -- exactly as production reads getLight and writes
// setLight on the post-spread buffer. brightnessLimit is NOT applied here (apply
// it after, like CellularLightingCalculator::calculate). This exists to PROVE the
// point math is reproducible off the production CellularLightArray (a GPU per-
// light-quad shader runs the same math) and to lock the GLSL-portable raycast.
//
//   base     : per-cell spread-only light, column-major (x * height + y),
//              matching CellularLightArray's internal layout.
//   obstacle : per-cell obstacle flag (1/0), same layout. uint8_t.
//   lights   : array-relative point lights, in production insertion order.
//   raycast  : obstacle term form (see ObstacleRaycast).
List<Vec3F> pointLightingReference(List<Vec3F> const& base, List<uint8_t> const& obstacle,
    List<ColoredCellularLightArray::PointLight> const& lights, size_t width, size_t height,
    PointParameters const& params, ObstacleRaycast raycast = ObstacleRaycast::LineAttenuation);

}
