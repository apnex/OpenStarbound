#pragma once

#include "StarEither.hpp"
#include "StarRect.hpp"
#include "StarImage.hpp"
#include "StarJson.hpp"
#include "StarColor.hpp"
#include "StarInterpolation.hpp"
#include "StarCellularLightArray.hpp"
#include "StarThread.hpp"

namespace Star {

STAR_EXCEPTION(LightmapException, StarException);

// CDL highlight tonemap: identity for max-channel <= 1 (normal scenes untouched),
// smooth rolloff of the excess above 1 toward the white-point (asymptote at `white`),
// hue-preserving (uniform RGB scale). Replaces the proportional brightnessLimit clamp
// when lightingTonemap is enabled. Mirrored byte-for-byte in lightingPassthrough.frag.
Vec3F tonemapHighlights(Vec3F color, float white);

class Lightmap {
public:
  Lightmap();
  Lightmap(unsigned width, unsigned height);
  Lightmap(Lightmap const& lightMap);
  Lightmap(Lightmap&& lightMap) noexcept;
  
  Lightmap& operator=(Lightmap const& lightMap);
  Lightmap& operator=(Lightmap&& lightMap) noexcept;

  operator ImageView();

  void set(unsigned x, unsigned y, float v);
  void set(unsigned x, unsigned y, Vec3F const& v);
  void add(unsigned x, unsigned y, Vec3F const& v);
  Vec3F get(unsigned x, unsigned y) const;

  bool empty() const;

  Vec2U size() const;
  unsigned width() const;
  unsigned height() const;
  float* data();

private:
  size_t len() const;

  std::unique_ptr<float[]> m_data;
  unsigned m_width;
  unsigned m_height;
};

inline void Lightmap::set(unsigned x, unsigned y, float v) {
  if (x >= m_width || y >= m_height) {
    throw LightmapException(strf("[{}, {}] out of range in Lightmap::set", x, y));
    return;
  }
  float* ptr = m_data.get() + (y * m_width * 3 + x * 3);
  ptr[0] = ptr[1] = ptr[2] = v;
}

inline void Lightmap::set(unsigned x, unsigned y, Vec3F const& v) {
  if (x >= m_width || y >= m_height) {
    throw LightmapException(strf("[{}, {}] out of range in Lightmap::set", x, y));
    return;
  }
  float* ptr = m_data.get() + (y * m_width * 3 + x * 3);
  ptr[0] = v.x();
  ptr[1] = v.y();
  ptr[2] = v.z();
}

inline void Lightmap::add(unsigned x, unsigned y, Vec3F const& v) {
  if (x >= m_width || y >= m_height) {
    throw LightmapException(strf("[{}, {}] out of range in Lightmap::add", x, y));
    return;
  }
  float* ptr = m_data.get() + (y * m_width * 3 + x * 3);
  ptr[0] += v.x();
  ptr[1] += v.y();
  ptr[2] += v.z();
}

inline Vec3F Lightmap::get(unsigned x, unsigned y) const {
  if (x >= m_width || y >= m_height) {
    throw LightmapException(strf("[{}, {}] out of range in Lightmap::get", x, y));
    return Vec3F();
  }
  float* ptr = m_data.get() + (y * m_width * 3 + x * 3);
  return Vec3F(ptr[0], ptr[1], ptr[2]);
}


inline bool Lightmap::empty() const {
  return m_width == 0 || m_height == 0;
}

inline Vec2U Lightmap::size() const {
  return { m_width, m_height };
}

inline unsigned Lightmap::width() const {
  return m_width;
}

inline unsigned Lightmap::height() const {
  return m_height;
}

inline float* Lightmap::data() {
  return m_data.get();
}

inline size_t Lightmap::len() const {
  return m_width * m_height * 3;
}

// Produce lighting values from an integral cellular grid.  Allows for floating
// positional point and cellular light sources, as well as pre-lighting cells
// individually.
class CellularLightingCalculator {
public:
  explicit CellularLightingCalculator(bool monochrome = false);

  typedef ColoredCellularLightArray::Cell Cell;

  void setMonochrome(bool monochrome);

  void setParameters(Json const& config);

  // Call 'begin' to start a calculation for the given region.
  //
  // ADAPTIVE BORDER (#170). The calculation region is the query region padded by borderCells(), which is
  // ceil(max(spreadMaxAir, pointMaxAir)) = 48 on shipped config -- turning a 128x64 query into a 224x160
  // calculation, a 4.375x multiplier every O(cells) phase pays. 48 is the reach of a point light at
  // intensity 1.0: the worst case the CONFIG can express, not the worst case a SCENE contains. Measured
  // across four bookmarks the point requirement was 20-28.
  //
  // Pass `pointBorderNeeded` -- max over lights of pointBorderFor() below -- and the border is clamped
  // into [spreadBorderCells(), borderCells()]. The floor is not negotiable: ambient light propagates
  // inward from the boundary by spreadMaxAir regardless of what lights exist.
  //
  // THE CLAMP IS NOT SYMMETRIC, AND THE TEXT HERE USED TO CLAIM IT WAS (#217). Only the CEILING is a
  // degrade-to-today: an over-estimate is clamped to borderCells() and reproduces the static border
  // exactly. An UNDER-estimate is clamped to spreadBorderCells(), which is NOT today's behaviour -- and
  // the border is the membership test for off-region point lights, not a work budget. Both executors
  // discard a light whose centre falls outside the grid (CellularLightArray::calculatePointLighting,
  // both specializations; GpuLightmapPass), so an under-sized border silently DELETES lights from the
  // frame. Soundness of the estimate is the caller's obligation: derive it with pointBorderFor, or the
  // caller and the engine will disagree about how far a light reaches.
  //
  // Omit the argument for the static, unconditional border. Covered by lighting_border_test.cpp.
  void begin(RectI const& queryRegion, Maybe<unsigned> pointBorderNeeded = {});

  // The border, in cells, that this one light requires of `queryRegion` -- 0 if it cannot reach at all.
  // Fold the max over the scene and hand that to begin().
  //
  // It lives here, beside the engine it has to agree with, because both places that computed it by hand
  // used the channel MEAN while the engine's reach uses the channel MAX (ColoredLightTraits::
  // maxIntensity), under-crediting a saturated light threefold (#217). `position` must already be
  // wrapped into the query region's frame by the caller -- world geometry is not visible from here.
  static unsigned pointBorderFor(RectI const& queryRegion, Vec2F const& position, Vec3F const& color, float pointMaxAir);

  // Once begin is called, this will return the region that could possibly
  // affect the target calculation region.  All lighting values should be set
  // for the given calculation region before calling 'calculate'.
  RectI calculationRegion() const;

  size_t baseIndexFor(Vec2I const& position);

  void setCellIndex(size_t cellIndex, Vec3F const& light, bool obstacle);

  // Bulk column write: resolve the monochrome/Either branch ONCE, then write `count`
  // contiguous cells starting at cellIndex (column-major, so a column is contiguous).
  // Byte-identical to calling setCellIndex per cell.
  void setCellColumn(size_t cellIndex, Vec3F const* lights, bool const* obstacles, size_t count);

  void addSpreadLight(Vec2F const& position, Vec3F const& light);
  void addPointLight(Vec2F const& position, Vec3F const& light, float beam, float beamAngle, float beamAmbience, bool asSpread = false);

  // Finish the calculation, and put the resulting color data in the given
  // output image.  The image will be reset to the size of the region given in
  // the call to 'begin', and formatted as RGB24.
  void calculate(Image& output);
  // Same as above, but the color data in a float buffer instead.
  void calculate(Lightmap& output);

  void setupImage(Image& image, PixelFormat format = PixelFormat::RGB24) const;

  // Test/tooling hooks for the Jacobi spread reference (see
  // spreadJacobiReference in StarCellularLightingOracle.hpp). spreadParameters()
  // returns the spread dropoff + brightnessLimit pulled from the active config.
  SpreadParameters spreadParameters() const;
  // pointParameters() returns the point/spread dropoffs + brightnessLimit pulled
  // from the active config, consumed by the point-lighting reference (see
  // pointLightingReference in StarCellularLightingOracle.hpp).
  PointParameters pointParameters() const;
  // Runs ONLY the spread-light seeding step and copies the resulting per-cell
  // emission (light) and obstacle grids over the full calculation region, in
  // the array's column-major (x * height + y) layout. Mutates internal cell
  // state (idempotent seeding); does NOT run the spread or point passes.
  void snapshotSpreadInput(List<Vec3F>& emission, List<uint8_t>& obstacle);

  // GPU-spread input export (Slice 2): seeds the spread lights into the cell
  // grid (the pre-sweep emission state) then copies the full calculation region
  // into upload images. 'emission' is reset to RGB_F (per-cell Vec3F light) and
  // 'obstacle' to RGB24 with each obstacle cell 255 and air 0 (the engine has no
  // single-channel pixel format; the GPU shader samples .r). The array's
  // column-major (x * height + y) cell maps to image pixel (x, y). Mutates
  // internal cell state (idempotent seeding); does NOT run the spread or point
  // passes -- call BEFORE calculate(), which would overwrite the cells.
  void exportSpreadInputs(Image& emission, Image& obstacle);

  // GPU-point input export (Slice 3): copies the configured point lights
  // (array-relative position, in insertion order) for the GPU per-light-quad
  // point pass -- the same list production's calculatePointLighting consumes.
  // 'out' is cleared first. The colored array's lights are copied directly; the
  // monochrome array's scalar value is broadcast to all channels (mirroring
  // exportSpreadInputs' monochrome branch). Does NOT run any lighting pass.
  void exportPointLights(List<ColoredCellularLightArray::PointLight>& out);

private:
  Json m_config;
  bool m_monochrome;
  Either<ColoredCellularLightArray, ScalarCellularLightArray> m_lightArray;
  RectI m_queryRegion;
  RectI m_calculationRegion;
};

// Produce light intensity values using the same algorithm as
// CellularLightingCalculator.  Only calculates a single point at a time, and
// uses scalar lights with no color calculation.
class CellularLightIntensityCalculator {
public:
  typedef ScalarCellularLightArray::Cell Cell;

  void setParameters(Json const& config);

  void begin(Vec2F const& queryPosition);

  RectI calculationRegion() const;

  void setCell(Vec2I const& position, Cell const& cell);
  void setCellColumn(Vec2I const& position, Cell const* cells, size_t count);

  void addSpreadLight(Vec2F const& position, float light);
  void addPointLight(Vec2F const& position, float light, float beam, float beamAngle, float beamAmbience);

  float calculate();

private:
  ScalarCellularLightArray m_lightArray;
  Vec2F m_queryPosition;
  RectI m_queryRegion;;
  RectI m_calculationRegion;
};

inline size_t CellularLightingCalculator::baseIndexFor(Vec2I const& position) {
  return (position[0] - m_calculationRegion.xMin()) * m_calculationRegion.height() + position[1] - m_calculationRegion.yMin();
}

inline void CellularLightingCalculator::setCellIndex(size_t cellIndex, Vec3F const& light, bool obstacle) {
  if (m_monochrome)
    m_lightArray.right().cellAtIndex(cellIndex) = ScalarCellularLightArray::Cell{light.sum() / 3, obstacle};
  else
    m_lightArray.left().cellAtIndex(cellIndex) = ColoredCellularLightArray::Cell{light, obstacle};
}

inline void CellularLightingCalculator::setCellColumn(size_t cellIndex, Vec3F const* lights, bool const* obstacles, size_t count) {
  if (m_monochrome) {
    auto& arr = m_lightArray.right();
    for (size_t i = 0; i < count; ++i)
      arr.cellAtIndex(cellIndex + i) = ScalarCellularLightArray::Cell{lights[i].sum() / 3, obstacles[i]};
  } else {
    auto& arr = m_lightArray.left();
    for (size_t i = 0; i < count; ++i)
      arr.cellAtIndex(cellIndex + i) = ColoredCellularLightArray::Cell{lights[i], obstacles[i]};
  }
}

}
