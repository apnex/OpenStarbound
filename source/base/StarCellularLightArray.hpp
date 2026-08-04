#pragma once

#include "StarList.hpp"
#include "StarVector.hpp"
#include "StarTelemetry.hpp"

namespace Star {

// Operations for simple scalar lighting.
struct ScalarLightTraits {
  typedef float Value;

  static float spread(float source, float dest, float drop);
  static float subtract(float value, float drop);

  static float maxIntensity(float value);
  static float minIntensity(float value);

  static float max(float v1, float v2);
};

// Operations for 3 component (colored) lighting.  Spread and subtract are
// applied proportionally, so that color ratios stay the same, to prevent hues
// changing as light spreads.
struct ColoredLightTraits {
  typedef Vec3F Value;

  static Vec3F spread(Vec3F const& source, Vec3F const& dest, float drop);
  static Vec3F subtract(Vec3F value, float drop);

  static float maxIntensity(Vec3F const& value);
  static float minIntensity(Vec3F const& value);

  static Vec3F max(Vec3F const& v1, Vec3F const& v2);
};

template <typename LightTraits>
class CellularLightArray {
public:
  typedef typename LightTraits::Value LightValue;

  struct Cell {
    LightValue light;
    bool obstacle;
  };

  struct SpreadLight {
    Vec2F position;
    LightValue value;
  };

  struct PointLight {
    Vec2F position;
    LightValue value;
    float beam;
    float beamAngle;
    float beamAmbience;
    bool asSpread;
  };

  void setParameters(unsigned spreadPasses, float spreadMaxAir, float spreadMaxObstacle,
      float pointMaxAir, float pointMaxObstacle, float pointObstacleBoost, bool pointAdditive);

  // The border around the target lighting array where initial lighting / light
  // source data is required.  Based on parameters.
  size_t borderCells() const;
  // The spread-only component of borderCells(): the structural floor under the adaptive border (#170).
  size_t spreadBorderCells() const;

  // Begin a new calculation, setting internal storage to new width and height
  // (if these are the same as last time this is cheap).  Always clears all
  // existing light and collision data.
  void begin(size_t newWidth, size_t newHeight);

  // Position is in index space, spread lights will have no effect if they are
  // outside of the array.  Integer points are assumed to be on the corners of
  // the grid (not the center)
  void addSpreadLight(SpreadLight const& spreadLight);
  void addPointLight(PointLight const& pointLight);

  // Directly set the lighting values for this position.
  void setLight(size_t x, size_t y, LightValue const& light);

  // Get current light value.  Call after calling calculate() to pull final
  // data out.
  LightValue getLight(size_t x, size_t y) const;

  // Set obstacle values for this position
  void setObstacle(size_t x, size_t y, bool obstacle);
  bool getObstacle(size_t x, size_t y) const;

  Cell const& cell(size_t x, size_t y) const;
  Cell& cell(size_t x, size_t y);

  Cell const& cellAtIndex(size_t index) const;
  Cell& cellAtIndex(size_t index);

  // Read-only access to the configured point lights (array-relative position,
  // in insertion order). Exposed so the GPU point pass can export the same list
  // production's calculatePointLighting consumes.
  List<PointLight> const& pointLights() const;

  // Calculate lighting in the given sub-rect, in order to properly do spread
  // lighting, and initial lighting must be given for the ambient border this
  // given rect, and the array size must be at least that large.  xMax / yMax
  // are not inclusive, the range is [xMin, xMax) and [yMin, yMax).
  void calculate(size_t xMin, size_t yMin, size_t xMax, size_t yMax);

  // Seeds the configured spread lights into the cell grid (the pre-sweep
  // "emission" state), exactly as the first step of calculate() does. Exposed
  // so the Jacobi spread reference (spreadJacobiReference) can be checked
  // against the exact emission the production sweep consumes. Does NOT run the
  // spread or point passes. Seeding is idempotent (max-based).
  void seedSpreadLights();

private:
  // Set 4 points based on interpolated light position and free space
  // attenuation.
  void setSpreadLightingPoints();

  // Spreads light out in an octagonal based cellular automata
  void calculateLightSpread(size_t xmin, size_t ymin, size_t xmax, size_t ymax);

  // Loops through each light and adds light strength based on distance and
  // obstacle attenuation.  Calculates within the given sub-rect
  void calculatePointLighting(size_t xmin, size_t ymin, size_t xmax, size_t ymax);

  // Run Xiaolin Wu's anti-aliased line drawing algorithm from start to end,
  // summing each block that would be drawn to to produce an attenuation.  Not
  // circularized.
  float lineAttenuation(Vec2F const& start, Vec2F const& end, float perObstacleAttenuation, float maxAttenuation);

  size_t m_width;
  size_t m_height;
  unique_ptr<Cell[]> m_cells;
  List<SpreadLight> m_spreadLights;
  List<PointLight> m_pointLights;

  unsigned m_spreadPasses;
  float m_spreadMaxAir;
  float m_spreadMaxObstacle;
  float m_pointMaxAir;
  float m_pointMaxObstacle;
  float m_pointObstacleBoost;
  bool m_pointAdditive;
};

typedef CellularLightArray<ColoredLightTraits> ColoredCellularLightArray;
typedef CellularLightArray<ScalarLightTraits> ScalarCellularLightArray;

// Parameters consumed by the Jacobi spread reference (see spreadJacobiReference).
struct SpreadParameters {
  float spreadMaxAir;
  float spreadMaxObstacle;
  float brightnessLimit;
};

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

// Parameters consumed by the point-lighting reference (see pointLightingReference).
// Mirrors the point/spread dropoffs + brightnessLimit from the active config.
struct PointParameters {
  float pointMaxAir;
  float pointMaxObstacle;
  float pointObstacleBoost;
  bool pointAdditive;
  float spreadMaxAir;
  float spreadMaxObstacle;
  float brightnessLimit;
};

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

inline float ScalarLightTraits::spread(float source, float dest, float drop) {
  return std::max(source - drop, dest);
}

inline float ScalarLightTraits::subtract(float c, float drop) {
  return std::max(c - drop, 0.0f);
}

inline float ScalarLightTraits::maxIntensity(float value) {
  return value;
}

inline float ScalarLightTraits::minIntensity(float value) {
  return value;
}

inline float ScalarLightTraits::max(float v1, float v2) {
  return std::max(v1, v2);
}

inline Vec3F ColoredLightTraits::spread(Vec3F const& source, Vec3F const& dest, float drop) {
  float maxChannel = std::max(source[0], std::max(source[1], source[2]));
  if (maxChannel <= 0.0f)
    return dest;

  drop /= maxChannel;
  return Vec3F(
      std::max(source[0] - source[0] * drop, dest[0]),
      std::max(source[1] - source[1] * drop, dest[1]),
      std::max(source[2] - source[2] * drop, dest[2])
    );
}

inline Vec3F ColoredLightTraits::subtract(Vec3F c, float drop) {
  float max = std::max(std::max(c[0], c[1]), c[2]);
  if (max <= 0.0f)
    return c;

  for (size_t i = 0; i < 3; ++i) {
    float pdrop = (drop * c[i]) / max;
    if (c[i] > pdrop)
      c[i] -= pdrop;
    else
      c[i] = 0;
  }
  return c;
}

inline float ColoredLightTraits::maxIntensity(Vec3F const& value) {
  return value.max();
}

inline float ColoredLightTraits::minIntensity(Vec3F const& value) {
  return value.min();
}

inline Vec3F ColoredLightTraits::max(Vec3F const& v1, Vec3F const& v2) {
  return vmax(v1, v2);
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::setParameters(unsigned spreadPasses, float spreadMaxAir, float spreadMaxObstacle,
    float pointMaxAir, float pointMaxObstacle, float pointObstacleBoost, bool pointAdditive) {
  m_spreadPasses = spreadPasses;
  m_spreadMaxAir = spreadMaxAir;
  m_spreadMaxObstacle = spreadMaxObstacle;
  m_pointMaxAir = pointMaxAir;
  m_pointMaxObstacle = pointMaxObstacle;
  m_pointObstacleBoost = pointObstacleBoost;
  m_pointAdditive = pointAdditive;
}

template <typename LightTraits>
size_t CellularLightArray<LightTraits>::borderCells() const {
  return (size_t)ceil(max(0.0f, max(m_spreadMaxAir, m_pointMaxAir)));
}

template <typename LightTraits>
size_t CellularLightArray<LightTraits>::spreadBorderCells() const {
  // The STRUCTURAL floor under the adaptive border (#170). borderCells() above is the max of two
  // independent requirements, and only one of them is a property of the lights present:
  //
  //   pointMaxAir  is how far a point light at intensity 1.0 reaches. A scene containing no such light
  //                does not need that much padding, and measured scenes need 20-28 of the shipped 48.
  //   spreadMaxAir is how far AMBIENT light propagates inward from the region boundary. That is not a
  //                property of any particular light -- it is a property of the boundary itself, so it
  //                holds no matter what the scene contains and can never be adapted away.
  //
  // Hence: adaptive border = clamp(point requirement, spreadBorderCells(), borderCells()).
  return (size_t)ceil(max(0.0f, m_spreadMaxAir));
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::begin(size_t newWidth, size_t newHeight) {
  m_spreadLights.clear();
  m_pointLights.clear();
  starAssert(newWidth > 0 && newHeight > 0);

  if (!m_cells || newWidth != m_width || newHeight != m_height) {
    m_width = newWidth;
    m_height = newHeight;

    m_cells.reset(new Cell[m_width * m_height]());

  } else {
    std::fill(m_cells.get(), m_cells.get() + m_width * m_height, Cell{LightValue{}, false});
  }
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::addSpreadLight(SpreadLight const& spreadLight) {
  m_spreadLights.append(spreadLight);
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::addPointLight(PointLight const& pointLight) {
  m_pointLights.append(pointLight);
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::setLight(size_t x, size_t y, LightValue const& lightValue) {
  cell(x, y).light = lightValue;
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::setObstacle(size_t x, size_t y, bool obstacle) {
  cell(x, y).obstacle = obstacle;
}

template <typename LightTraits>
auto CellularLightArray<LightTraits>::getLight(size_t x, size_t y) const -> LightValue {
  return cell(x, y).light;
}

template <typename LightTraits>
bool CellularLightArray<LightTraits>::getObstacle(size_t x, size_t y) const {
  return cell(x, y).obstacle;
}

template <typename LightTraits>
auto CellularLightArray<LightTraits>::cell(size_t x, size_t y) const -> Cell const & {
  starAssert(x < m_width && y < m_height);
  return m_cells[x * m_height + y];
}

template <typename LightTraits>
auto CellularLightArray<LightTraits>::cell(size_t x, size_t y) -> Cell & {
  starAssert(x < m_width && y < m_height);
  return m_cells[x * m_height + y];
}

template <typename LightTraits>
auto CellularLightArray<LightTraits>::cellAtIndex(size_t index) const -> Cell const & {
  starAssert(index < m_width * m_height);
  return m_cells[index];
}

template <typename LightTraits>
auto CellularLightArray<LightTraits>::cellAtIndex(size_t index) -> Cell & {
  starAssert(index < m_width * m_height);
  return m_cells[index];
}

template <typename LightTraits>
auto CellularLightArray<LightTraits>::pointLights() const -> List<PointLight> const & {
  return m_pointLights;
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::calculate(size_t xMin, size_t yMin, size_t xMax, size_t yMax) {
  // Both template instantiations (colored/scalar) intentionally report into the same
  // telemetry keys: per-instantiation function-local statics resolve to one registry
  // node per key. Timers record only under deep tracing (TelemetryScope gates itself).
  setSpreadLightingPoints();
  {
    // Cadence::Call, Role::Detail -- BOTH deliberate.
    //   Call: this phase runs only when the CPU calculate() runs, which the shipping GPU config skips.
    //     Declared Recompute, a single self-healing CPU frame inside a window gives count=1 against
    //     ~1100 expected, and the consumer's coverage_scale divides by 0.0009 -- inflating this timer
    //     ~1100x and firing "parts exceed the whole" on entirely correct data. Absence here is the
    //     branch not being taken, not a lost sample.
    //   Detail: this will nest inside lighting.cpu.calculate.us, the Budget part added by the phase
    //     instrumentation. Summing both would double-count.
    static auto spreadTimer = Telemetry::timer("lighting.cpu.spread.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
    TelemetryScope scope(spreadTimer);
    calculateLightSpread(xMin, yMin, xMax, yMax);
  }
  {
    static auto pointTimer = Telemetry::timer("lighting.cpu.point.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
    TelemetryScope scope(pointTimer);
    calculatePointLighting(xMin, yMin, xMax, yMax);
  }
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::setSpreadLightingPoints() {
  for (SpreadLight const& light : m_spreadLights) {
    // - 0.5f to correct for lights being on the grid corners and not center
    int minX = floor(light.position[0] - 0.5f);
    int minY = floor(light.position[1] - 0.5f);
    int maxX = minX + 1;
    int maxY = minY + 1;

    float xdist = light.position[0] - minX - 0.5f;
    float ydist = light.position[1] - minY - 0.5f;

    // Pick falloff here based on closest block obstacle value (probably not
    // best)
    Vec2I pos(light.position.floor());
    float oneBlockAtt;
    if (pos[0] >= 0 && pos[0] < (int)m_width && pos[1] >= 0 && pos[1] < (int)m_height && getObstacle(pos[0], pos[1]))
      oneBlockAtt = 1.0f / m_spreadMaxObstacle;
    else
      oneBlockAtt = 1.0f / m_spreadMaxAir;

    // "pre fall-off" a 2x2 area of blocks to smooth out floating point
    // positions using the cellular algorithm

    if (minX >= 0 && minX < (int)m_width && minY >= 0 && minY < (int)m_height)
      setLight(minX, minY, LightTraits::max(getLight(minX, minY), LightTraits::subtract(light.value, oneBlockAtt * (2.0f - (1.0f - xdist) - (1.0f - ydist)))));

    if (minX >= 0 && minX < (int)m_width && maxY >= 0 && maxY < (int)m_height)
      setLight(minX, maxY, LightTraits::max(getLight(minX, maxY), LightTraits::subtract(light.value, oneBlockAtt * (2.0f - (1.0f - xdist) - (ydist)))));

    if (maxX >= 0 && maxX < (int)m_width && minY >= 0 && minY < (int)m_height)
      setLight(maxX, minY, LightTraits::max(getLight(maxX, minY), LightTraits::subtract(light.value, oneBlockAtt * (2.0f - (xdist) - (1.0f - ydist)))));

    if (maxX >= 0 && maxX < (int)m_width && maxY >= 0 && maxY < (int)m_height)
      setLight(maxX, maxY, LightTraits::max(getLight(maxX, maxY), LightTraits::subtract(light.value, oneBlockAtt * (2.0f - (xdist) - (ydist)))));
  }
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::seedSpreadLights() {
  setSpreadLightingPoints();
}

template <typename LightTraits>
void CellularLightArray<LightTraits>::calculateLightSpread(size_t xMin, size_t yMin, size_t xMax, size_t yMax) {
  starAssert(m_width > 0 && m_height > 0);

  float dropoffAir = 1.0f / m_spreadMaxAir;
  float dropoffObstacle = 1.0f / m_spreadMaxObstacle;
  float dropoffAirDiag = 1.0f / m_spreadMaxAir * Constants::sqrt2;
  float dropoffObstacleDiag = 1.0f / m_spreadMaxObstacle * Constants::sqrt2;

  // enlarge x/y min/max taking into ambient spread of light
  xMin = xMin - min(xMin, (size_t)ceil(m_spreadMaxAir));
  yMin = yMin - min(yMin, (size_t)ceil(m_spreadMaxAir));
  xMax = min(m_width, xMax + (size_t)ceil(m_spreadMaxAir));
  yMax = min(m_height, yMax + (size_t)ceil(m_spreadMaxAir));

  for (unsigned p = 0; p < m_spreadPasses; ++p) {
    // Spread right and up and diag up right / diag down right
    for (size_t x = xMin + 1; x < xMax - 1; ++x) {
      size_t xCellOffset = x * m_height;
      size_t xRightCellOffset = (x + 1) * m_height;

      for (size_t y = yMin + 1; y < yMax - 1; ++y) {
        auto cell = cellAtIndex(xCellOffset + y);
        auto& cellRight = cellAtIndex(xRightCellOffset + y);
        auto& cellUp = cellAtIndex(xCellOffset + y + 1);
        auto& cellRightUp = cellAtIndex(xRightCellOffset + y + 1);
        auto& cellRightDown = cellAtIndex(xRightCellOffset + y - 1);

        float straightDropoff = cell.obstacle ? dropoffObstacle : dropoffAir;
        float diagDropoff = cell.obstacle ? dropoffObstacleDiag : dropoffAirDiag;

        cellRight.light = LightTraits::spread(cell.light, cellRight.light, straightDropoff);
        cellUp.light = LightTraits::spread(cell.light, cellUp.light, straightDropoff);

        cellRightUp.light = LightTraits::spread(cell.light, cellRightUp.light, diagDropoff);
        cellRightDown.light = LightTraits::spread(cell.light, cellRightDown.light, diagDropoff);
      }
    }

    // Spread left and down and diag up left / diag down left
    for (size_t x = xMax - 2; x > xMin; --x) {
      size_t xCellOffset = x * m_height;
      size_t xLeftCellOffset = (x - 1) * m_height;

      for (size_t y = yMax - 2; y > yMin; --y) {
        auto cell = cellAtIndex(xCellOffset + y);
        auto& cellLeft = cellAtIndex(xLeftCellOffset + y);
        auto& cellDown = cellAtIndex(xCellOffset + y - 1);
        auto& cellLeftUp = cellAtIndex(xLeftCellOffset + y + 1);
        auto& cellLeftDown = cellAtIndex(xLeftCellOffset + y - 1);

        float straightDropoff = cell.obstacle ? dropoffObstacle : dropoffAir;
        float diagDropoff = cell.obstacle ? dropoffObstacleDiag : dropoffAirDiag;

        cellLeft.light = LightTraits::spread(cell.light, cellLeft.light, straightDropoff);
        cellDown.light = LightTraits::spread(cell.light, cellDown.light, straightDropoff);

        cellLeftUp.light = LightTraits::spread(cell.light, cellLeftUp.light, diagDropoff);
        cellLeftDown.light = LightTraits::spread(cell.light, cellLeftDown.light, diagDropoff);
      }
    }
  }
}

template <typename LightTraits>
float CellularLightArray<LightTraits>::lineAttenuation(Vec2F const& start, Vec2F const& end,
    float perObstacleAttenuation, float maxAttenuation) {
  // Run Xiaolin Wu's line algorithm from start to end, summing over colliding
  // blocks using perObstacleAttenuation.
  float obstacleAttenuation = 0.0;

  // Apply correction because integer coordinates are lower left corner.
  float x1 = start[0] - 0.5;
  float y1 = start[1] - 0.5;
  float x2 = end[0] - 0.5;
  float y2 = end[1] - 0.5;

  float dx = x2 - x1;
  float dy = y2 - y1;

  if (fabs(dx) < fabs(dy)) {
    if (y2 < y1) {
      swap(y1, y2);
      swap(x1, x2);
    }

    float gradient = dx / dy;

    // first end point
    float yend = round(y1);
    float xend = x1 + gradient * (yend - y1);
    float ygap = rfpart(y1 + 0.5);
    int ypxl1 = yend;
    int xpxl1 = ipart(xend);

    if (cell(xpxl1, ypxl1).obstacle)
      obstacleAttenuation += rfpart(xend) * ygap * perObstacleAttenuation;

    if (cell(xpxl1 + 1, ypxl1).obstacle)
      obstacleAttenuation += fpart(xend) * ygap * perObstacleAttenuation;

    if (obstacleAttenuation >= maxAttenuation)
      return maxAttenuation;

    float interx = xend + gradient;

    // second end point
    yend = round(y2);
    xend = x2 + gradient * (yend - y2);
    ygap = fpart(y2 + 0.5);
    int ypxl2 = yend;
    int xpxl2 = ipart(xend);

    if (cell(xpxl2, ypxl2).obstacle)
      obstacleAttenuation += rfpart(xend) * ygap * perObstacleAttenuation;

    if (cell(xpxl2 + 1, ypxl2).obstacle)
      obstacleAttenuation += fpart(xend) * ygap * perObstacleAttenuation;

    if (obstacleAttenuation >= maxAttenuation)
      return maxAttenuation;

    for (int y = ypxl1 + 1; y < ypxl2; ++y) {
      int interxIpart = ipart(interx);
      float interxFpart = interx - interxIpart;
      float interxRFpart = 1.0 - interxFpart;

      if (cell(interxIpart, y).obstacle)
        obstacleAttenuation += interxRFpart * perObstacleAttenuation;
      if (cell(interxIpart + 1, y).obstacle)
        obstacleAttenuation += interxFpart * perObstacleAttenuation;

      if (obstacleAttenuation >= maxAttenuation)
        return maxAttenuation;

      interx += gradient;
    }
  } else {
    if (x2 < x1) {
      swap(x1, x2);
      swap(y1, y2);
    }

    float gradient = dy / dx;

    // first end point
    float xend = round(x1);
    float yend = y1 + gradient * (xend - x1);
    float xgap = rfpart(x1 + 0.5);
    int xpxl1 = xend;
    int ypxl1 = ipart(yend);

    if (cell(xpxl1, ypxl1).obstacle)
      obstacleAttenuation += rfpart(yend) * xgap * perObstacleAttenuation;

    if (cell(xpxl1, ypxl1 + 1).obstacle)
      obstacleAttenuation += fpart(yend) * xgap * perObstacleAttenuation;

    if (obstacleAttenuation >= maxAttenuation)
      return maxAttenuation;

    float intery = yend + gradient;

    // second end point
    xend = round(x2);
    yend = y2 + gradient * (xend - x2);
    xgap = fpart(x2 + 0.5);
    int xpxl2 = xend;
    int ypxl2 = ipart(yend);

    if (cell(xpxl2, ypxl2).obstacle)
      obstacleAttenuation += rfpart(yend) * xgap * perObstacleAttenuation;

    if (cell(xpxl2, ypxl2 + 1).obstacle)
      obstacleAttenuation += fpart(yend) * xgap * perObstacleAttenuation;

    if (obstacleAttenuation >= maxAttenuation)
      return maxAttenuation;

    for (int x = xpxl1 + 1; x < xpxl2; ++x) {
      int interyIpart = ipart(intery);
      float interyFpart = intery - interyIpart;
      float interyRFpart = 1.0 - interyFpart;

      if (cell(x, interyIpart).obstacle)
        obstacleAttenuation += interyRFpart * perObstacleAttenuation;
      if (cell(x, interyIpart + 1).obstacle)
        obstacleAttenuation += interyFpart * perObstacleAttenuation;

      if (obstacleAttenuation >= maxAttenuation)
        return maxAttenuation;

      intery += gradient;
    }
  }

  return min(obstacleAttenuation, maxAttenuation);
}

}
