#include "StarCellularLighting.hpp"
#include "StarTelemetry.hpp"

namespace Star {

Lightmap::Lightmap() : m_width(0), m_height(0) {}

Lightmap::Lightmap(unsigned width, unsigned height) : m_width(width), m_height(height) {
  m_data = std::make_unique<float[]>(len());
}

Lightmap::Lightmap(Lightmap const& lightMap) {
  operator=(lightMap);
}

Lightmap::Lightmap(Lightmap&& lightMap) noexcept {
  operator=(std::move(lightMap));
}

Lightmap& Lightmap::operator=(Lightmap const& lightMap) {
  m_width = lightMap.m_width;
  m_height = lightMap.m_height;
  if (lightMap.m_data) {
    m_data = std::make_unique<float[]>(len());
    memcpy(m_data.get(), lightMap.m_data.get(), len());
  }
  return *this;
}

Lightmap& Lightmap::operator=(Lightmap&& lightMap) noexcept {
  m_width = take(lightMap.m_width);
  m_height = take(lightMap.m_height);
  m_data = take(lightMap.m_data);
  return *this;
}

Lightmap::operator ImageView() {
  ImageView view;
  view.data = (uint8_t*)m_data.get();
  view.size = size();
  view.format = PixelFormat::RGB_F;
  return view;
}

CellularLightingCalculator::CellularLightingCalculator(bool monochrome)
    : m_monochrome(monochrome)
{
    if (monochrome)
        m_lightArray.setRight(ScalarCellularLightArray());
    else
        m_lightArray.setLeft(ColoredCellularLightArray());
}

void CellularLightingCalculator::setMonochrome(bool monochrome) {
  if (monochrome == m_monochrome)
    return;

  m_monochrome = monochrome;
  if (monochrome)
    m_lightArray.setRight(ScalarCellularLightArray());
  else
    m_lightArray.setLeft(ColoredCellularLightArray());

  if (m_config)
    setParameters(m_config);
}

void CellularLightingCalculator::setParameters(Json const& config) {
  m_config = config;
  if (m_monochrome)
    m_lightArray.right().setParameters(
        config.getInt("spreadPasses"),
        config.getFloat("spreadMaxAir"),
        config.getFloat("spreadMaxObstacle"),
        config.getFloat("pointMaxAir"),
        config.getFloat("pointMaxObstacle"),
        config.getFloat("pointObstacleBoost"),
        config.getBool("pointAdditive", false)
      );
  else
    m_lightArray.left().setParameters(
        config.getInt("spreadPasses"),
        config.getFloat("spreadMaxAir"),
        config.getFloat("spreadMaxObstacle"),
        config.getFloat("pointMaxAir"),
        config.getFloat("pointMaxObstacle"),
        config.getFloat("pointObstacleBoost"),
        config.getBool("pointAdditive", false)
      );
}

void CellularLightingCalculator::begin(RectI const& queryRegion, Maybe<unsigned> pointBorderNeeded) {
  // The scaling-law denominators for every O(cells) phase in the lighting pipeline. They are published
  // HERE, by the act that establishes both regions, and deliberately NOT inside calculate(): calculate()
  // is skipped whenever GPU lighting is latched active (StarWorldClient.cpp skipCpuCalc), so a gauge set
  // there freezes at whatever the pre-latch load frames left behind. The two are different quantities and
  // conflating them cost the campaign a 4.375x error: the output lightmap is the QUERY region, but every
  // export/convert/fill loop runs over the border-padded CALCULATION region.
  //
  // Cadence::Recompute, matching the phase they are set inside: begin() is reached only from the lighting
  // recompute path, and the timer bracketing this very call declares Recompute. Declared Call they fired
  // exactly once per recompute while asserting no expectation at all, so they forwent the coverage check
  // their co-located timer gets. Nothing was scaled wrongly by that -- a gauge is a level, carried rather
  // than differenced -- so this under-declared rather than misstated. It matters because lighting.calc.cells
  // is the scene fingerprint used to prove two legs saw the same world, and a fingerprint with no declared
  // cadence cannot be checked for having been sampled the expected number of times.
  static auto cellsGauge = Telemetry::gauge("lighting.cells",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  static auto calcCellsGauge = Telemetry::gauge("lighting.calc.cells",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});

  m_queryRegion = queryRegion;
  // ADAPTIVE BORDER (#170). `full` is the historic unconditional padding, ceil(max(spreadMaxAir,
  // pointMaxAir)). `floorCells` is the part of it no scene can adapt away: ambient light propagating
  // inward from the region boundary, which is a property of the boundary rather than of any light.
  //
  // The clamp is ASYMMETRIC, and this comment used to claim it was not (#217). Only the CEILING is a
  // degrade-to-today: an over-estimate lands on `full` and reproduces the static border exactly. An
  // under-estimate lands on `floorCells`, a third narrower than the point requirement the config can
  // express -- and since the border decides whether an off-region point light is in the grid at all,
  // that deletes lights rather than dimming edges. Callers derive the value with pointBorderFor.
  auto adaptedBorder = [&pointBorderNeeded](size_t full, size_t floorCells) -> int {
    if (!pointBorderNeeded)
      return (int)full;
    return (int)clamp<size_t>((size_t)*pointBorderNeeded, floorCells, full);
  };
  if (m_monochrome) {
    m_calculationRegion = RectI(queryRegion).padded(
        adaptedBorder(m_lightArray.right().borderCells(), m_lightArray.right().spreadBorderCells()));
    m_lightArray.right().begin(m_calculationRegion.width(), m_calculationRegion.height());
  } else {
    m_calculationRegion = RectI(queryRegion).padded(
        adaptedBorder(m_lightArray.left().borderCells(), m_lightArray.left().spreadBorderCells()));
    m_lightArray.left().begin(m_calculationRegion.width(), m_calculationRegion.height());
  }

  cellsGauge.set((int64_t)m_queryRegion.width() * (int64_t)m_queryRegion.height());
  calcCellsGauge.set((int64_t)m_calculationRegion.width() * (int64_t)m_calculationRegion.height());
}

unsigned CellularLightingCalculator::pointBorderFor(
    RectI const& queryRegion, Vec2F const& position, Vec3F const& color, float pointMaxAir) {
  // REACH USES THE CHANNEL MAX, because that is what the engine uses: calculatePointLighting computes
  // maxRange = maxIntensity * pointMaxAir, and ColoredLightTraits::maxIntensity is value.max(). The two
  // hand-rolled copies this replaces used the channel mean, which credits a saturated (1,0,0) light with
  // a reach of 16 against its true 48 (#217).
  float reach = color.max() * pointMaxAir;

  // Chebyshev distance from the query rect out to the light; 0 when it is inside.
  float dx = max(0.0f, max((float)queryRegion.xMin() - position[0], position[0] - (float)queryRegion.xMax()));
  float dy = max(0.0f, max((float)queryRegion.yMin() - position[1], position[1] - (float)queryRegion.yMax()));
  float d = max(dx, dy);

  if (d >= reach)
    return 0;

  // +1 BECAUSE THE ARRAY IS HALF-OPEN ON THE MAX SIDE. The region is queryRegion padded by b, so the
  // array spans world [min-b, max+b) and its last valid index is max+b-1. A light d beyond queryRegion's
  // max edge therefore needs b >= d+1 to land inside it; a border of exactly ceil(d) puts the farthest
  // constraining light one index past the end, where calculatePointLighting's guard discards it.
  return (unsigned)std::ceil(d) + 1;
}

RectI CellularLightingCalculator::calculationRegion() const {
  return m_calculationRegion;
}

void CellularLightingCalculator::addSpreadLight(Vec2F const& position, Vec3F const& light) {
  static auto spreadLightCounter = Telemetry::counter("lighting.lights.spread",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  spreadLightCounter.inc();
  Vec2F arrayPosition = position - Vec2F(m_calculationRegion.min());
  if (m_monochrome)
    m_lightArray.right().addSpreadLight({arrayPosition, light.max()});
  else
    m_lightArray.left().addSpreadLight({arrayPosition, light});
}

void CellularLightingCalculator::addPointLight(Vec2F const& position, Vec3F const& light, float beam, float beamAngle, float beamAmbience, bool asSpread) {
  static auto pointLightCounter = Telemetry::counter("lighting.lights.point",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  pointLightCounter.inc();
  Vec2F arrayPosition = position - Vec2F(m_calculationRegion.min());
  if (m_monochrome)
    m_lightArray.right().addPointLight({arrayPosition, light.max(), beam, beamAngle, beamAmbience, asSpread});
  else
    m_lightArray.left().addPointLight({arrayPosition, light, beam, beamAngle, beamAmbience, asSpread});
}

void CellularLightingCalculator::calculate(Image& output) {
  Vec2S arrayMin = Vec2S(m_queryRegion.min() - m_calculationRegion.min());
  Vec2S arrayMax = Vec2S(m_queryRegion.max() - m_calculationRegion.min());

  if (m_monochrome)
    m_lightArray.right().calculate(arrayMin[0], arrayMin[1], arrayMax[0], arrayMax[1]);
  else
    m_lightArray.left().calculate(arrayMin[0], arrayMin[1], arrayMax[0], arrayMax[1]);

  output.reset(arrayMax[0] - arrayMin[0], arrayMax[1] - arrayMin[1], PixelFormat::RGB24);

  if (m_monochrome) {
    for (size_t x = arrayMin[0]; x < arrayMax[0]; ++x) {
      for (size_t y = arrayMin[1]; y < arrayMax[1]; ++y) {
        output.set24(x - arrayMin[0], y - arrayMin[1], Color::grayf(m_lightArray.right().getLight(x, y)).toRgb());
      }
    }
  } else {
    for (size_t x = arrayMin[0]; x < arrayMax[0]; ++x) {
      for (size_t y = arrayMin[1]; y < arrayMax[1]; ++y) {
        output.set24(x - arrayMin[0], y - arrayMin[1], Color::v3fToByte(m_lightArray.left().getLight(x, y)));
      }
    }
  }
}

void CellularLightingCalculator::calculate(Lightmap& output) {
  Vec2S arrayMin = Vec2S(m_queryRegion.min() - m_calculationRegion.min());
  Vec2S arrayMax = Vec2S(m_queryRegion.max() - m_calculationRegion.min());

  if (m_monochrome)
    m_lightArray.right().calculate(arrayMin[0], arrayMin[1], arrayMax[0], arrayMax[1]);
  else
    m_lightArray.left().calculate(arrayMin[0], arrayMin[1], arrayMax[0], arrayMax[1]);

  // 'post' phase: output copy + brightness cap. Timer records only under deep
  // tracing (TelemetryScope gates itself). The cell gauges live in begin() -- see the note there.
  // Call/Detail for the same two reasons as spread/point (StarCellularLightArray.hpp): conditional on
  // calculate() running, and it will nest inside lighting.cpu.calculate.us.
  static auto postTimer = Telemetry::timer("lighting.cpu.post.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
  TelemetryScope postScope(postTimer);

  output = Lightmap(arrayMax[0] - arrayMin[0], arrayMax[1] - arrayMin[1]);

  float brightnessLimit = m_config.getFloat("brightnessLimit");

  if (m_monochrome) {
    for (size_t x = arrayMin[0]; x < arrayMax[0]; ++x) {
      for (size_t y = arrayMin[1]; y < arrayMax[1]; ++y) {
        auto light = min(m_lightArray.right().getLight(x, y), brightnessLimit);
        output.set(x - arrayMin[0], y - arrayMin[1], light);
      }
    }
  } else {
    for (size_t x = arrayMin[0]; x < arrayMax[0]; ++x) {
      for (size_t y = arrayMin[1]; y < arrayMax[1]; ++y) {
        auto light = m_lightArray.left().getLight(x, y);
        float intensity = ColoredLightTraits::maxIntensity(light);
        if (intensity > brightnessLimit)
          light *= brightnessLimit / intensity;
        output.set(x - arrayMin[0], y - arrayMin[1], light);
      }
    }
  }
}

Vec3F tonemapHighlights(Vec3F color, float white) {
  float i = color[0];
  if (color[1] > i) i = color[1];
  if (color[2] > i) i = color[2];
  if (i <= 1.0f || i <= 0.0f)
    return color;                 // normal range untouched
  float e = i - 1.0f;             // excess above 1
  float k = white - 1.0f;         // headroom toward the white-point
  if (k < 1.0e-4f) k = 1.0e-4f;   // guard white <= 1
  float iOut = 1.0f + k * e / (k + e);  // (1,inf) -> (1, 1+k=white), monotonic
  return color * (iOut / i);
}

SpreadParameters CellularLightingCalculator::spreadParameters() const {
  return SpreadParameters{
      m_config.getFloat("spreadMaxAir"),
      m_config.getFloat("spreadMaxObstacle"),
      m_config.getFloat("brightnessLimit")
    };
}

PointParameters CellularLightingCalculator::pointParameters() const {
  return PointParameters{
      m_config.getFloat("pointMaxAir"),
      m_config.getFloat("pointMaxObstacle"),
      m_config.getFloat("pointObstacleBoost"),
      m_config.getBool("pointAdditive", false),
      m_config.getFloat("spreadMaxAir"),
      m_config.getFloat("spreadMaxObstacle"),
      m_config.getFloat("brightnessLimit")
    };
}

void CellularLightingCalculator::snapshotSpreadInput(List<Vec3F>& emission, List<uint8_t>& obstacle) {
  size_t width = m_calculationRegion.width();
  size_t height = m_calculationRegion.height();
  size_t count = width * height;
  emission.resize(count);
  obstacle.resize(count);

  if (m_monochrome) {
    m_lightArray.right().seedSpreadLights();
    for (size_t i = 0; i < count; ++i) {
      auto const& cell = m_lightArray.right().cellAtIndex(i);
      emission[i] = Vec3F::filled(cell.light);
      obstacle[i] = cell.obstacle ? 1 : 0;
    }
  } else {
    m_lightArray.left().seedSpreadLights();
    for (size_t i = 0; i < count; ++i) {
      auto const& cell = m_lightArray.left().cellAtIndex(i);
      emission[i] = cell.light;
      obstacle[i] = cell.obstacle ? 1 : 0;
    }
  }
}

void CellularLightingCalculator::exportSpreadInputs(Image& emission, Image& obstacle) {
  unsigned width = (unsigned)m_calculationRegion.width();
  unsigned height = (unsigned)m_calculationRegion.height();

  // RGB_F float emission for the GPU spread; RGB24 obstacle mask (no single-channel format exists, the
  // shader reads .r). NOTE: Image::reset does NOT zero-fill on a same-size call -- it early-outs. The loop
  // below writes every pixel, so that is correct and deliberate; do not add a clear back.
  emission.reset(width, height, PixelFormat::RGB_F);
  obstacle.reset(width, height, PixelFormat::RGB24);

  float* emissionData = (float*)emission.data();
  Vec3B const obstacleByte(255, 255, 255);
  Vec3B const airByte(0, 0, 0);

  // Cell index x*height+y (array column-major) -> image pixel (x, y) (row-major). The loops iterate in
  // DESTINATION order (y outer) so the image writes are sequential; the cell reads take the stride
  // instead. That is the right way round -- the writes carry a read-for-ownership cost the reads do not,
  // and the cell array prefetches cleanly. Iterating in source order made every single store land on a
  // different cache line, across a ~538 KB working set swept 5-6 times per call.
  if (m_monochrome) {
    m_lightArray.right().seedSpreadLights();
    for (unsigned y = 0; y < height; ++y) {
      for (unsigned x = 0; x < width; ++x) {
        auto const& cell = m_lightArray.right().cellAtIndex((size_t)x * height + y);
        size_t pixel = ((size_t)y * width + x) * 3;
        emissionData[pixel] = emissionData[pixel + 1] = emissionData[pixel + 2] = cell.light;
        obstacle.set24(x, y, cell.obstacle ? obstacleByte : airByte);
      }
    }
  } else {
    m_lightArray.left().seedSpreadLights();
    for (unsigned y = 0; y < height; ++y) {
      for (unsigned x = 0; x < width; ++x) {
        auto const& cell = m_lightArray.left().cellAtIndex((size_t)x * height + y);
        size_t pixel = ((size_t)y * width + x) * 3;
        emissionData[pixel] = cell.light[0];
        emissionData[pixel + 1] = cell.light[1];
        emissionData[pixel + 2] = cell.light[2];
        obstacle.set24(x, y, cell.obstacle ? obstacleByte : airByte);
      }
    }
  }
}

void CellularLightingCalculator::exportPointLights(List<ColoredCellularLightArray::PointLight>& out) {
  // Mirror exportSpreadInputs' monochrome/colored branch. The colored array
  // already stores ColoredCellularLightArray::PointLight, so copy directly; the
  // monochrome array stores a scalar value, broadcast it to all channels (as the
  // monochrome emission export does) so the GPU point pass sees one struct type.
  out.clear();
  if (m_monochrome) {
    for (auto const& light : m_lightArray.right().pointLights())
      out.append({light.position, Vec3F::filled(light.value), light.beam, light.beamAngle, light.beamAmbience, light.asSpread});
  } else {
    out = m_lightArray.left().pointLights();
  }
}

void CellularLightingCalculator::setupImage(Image& image, PixelFormat format) const {
  Vec2S arrayMin = Vec2S(m_queryRegion.min() - m_calculationRegion.min());
  Vec2S arrayMax = Vec2S(m_queryRegion.max() - m_calculationRegion.min());

  image.reset(arrayMax[0] - arrayMin[0], arrayMax[1] - arrayMin[1], format);
}

void CellularLightIntensityCalculator::setParameters(Json const& config) {
  m_lightArray.setParameters(
      config.getInt("spreadPasses"),
      config.getFloat("spreadMaxAir"),
      config.getFloat("spreadMaxObstacle"),
      config.getFloat("pointMaxAir"),
      config.getFloat("pointMaxObstacle"),
      config.getFloat("pointObstacleBoost"),
      config.getBool("pointAdditive", false)
    );
}

void CellularLightIntensityCalculator::begin(Vec2F const& queryPosition) {
  m_queryPosition = queryPosition;
  m_queryRegion = RectI::withSize(Vec2I::floor(queryPosition - Vec2F::filled(0.5f)), Vec2I(2, 2));
  m_calculationRegion = RectI(m_queryRegion).padded((int)m_lightArray.borderCells());

  m_lightArray.begin(m_calculationRegion.width(), m_calculationRegion.height());
}

RectI CellularLightIntensityCalculator::calculationRegion() const {
  return m_calculationRegion;
}

void CellularLightIntensityCalculator::setCell(Vec2I const& position, Cell const& cell) {
  setCellColumn(position, &cell, 1);
}

void CellularLightIntensityCalculator::setCellColumn(Vec2I const& position, Cell const* cells, size_t count) {
  size_t baseIndex = (position[0] - m_calculationRegion.xMin()) * m_calculationRegion.height() + position[1] - m_calculationRegion.yMin();
  for (size_t i = 0; i < count; ++i)
    m_lightArray.cellAtIndex(baseIndex + i) = cells[i];
}

void CellularLightIntensityCalculator::addSpreadLight(Vec2F const& position, float light) {
  Vec2F arrayPosition = position - Vec2F(m_calculationRegion.min());
  m_lightArray.addSpreadLight({arrayPosition, light});
}

void CellularLightIntensityCalculator::addPointLight(Vec2F const& position, float light, float beam, float beamAngle, float beamAmbience) {
  Vec2F arrayPosition = position - Vec2F(m_calculationRegion.min());
  m_lightArray.addPointLight({arrayPosition, light, beam, beamAngle, beamAmbience, false});
}


float CellularLightIntensityCalculator::calculate() {
  Vec2S arrayMin = Vec2S(m_queryRegion.min() - m_calculationRegion.min());
  Vec2S arrayMax = Vec2S(m_queryRegion.max() - m_calculationRegion.min());

  m_lightArray.calculate(arrayMin[0], arrayMin[1], arrayMax[0], arrayMax[1]);

  // Do 2d lerp to find lighting intensity

  float ll = m_lightArray.getLight(arrayMin[0], arrayMin[1]);
  float lr = m_lightArray.getLight(arrayMin[0] + 1, arrayMin[1]);
  float ul = m_lightArray.getLight(arrayMin[0], arrayMin[1] + 1);
  float ur = m_lightArray.getLight(arrayMin[0] + 1, arrayMin[1] + 1);

  float xl = m_queryPosition[0] - 0.5f - m_queryRegion.xMin();
  float yl = m_queryPosition[1] - 0.5f - m_queryRegion.yMin();

  return lerp(yl, lerp(xl, ll, lr), lerp(xl, ul, ur));
}

}
