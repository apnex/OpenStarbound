#include "StarCellularLighting.hpp"
#include "StarTelemetry.hpp"
#include "StarJson.hpp"
#include "gtest/gtest.h"

using namespace Star;

namespace {

Json lightingConfig() {
  return Json::parseJson(R"JSON({
    "spreadPasses": 3, "spreadMaxAir": 8.0, "spreadMaxObstacle": 2.0,
    "pointMaxAir": 12.0, "pointMaxObstacle": 4.0, "pointObstacleBoost": 1.0,
    "pointAdditive": true, "brightnessLimit": 1.4
  })JSON");
}

// TelemetryTimer has no read accessor; timers are read via the snapshot tree
// (same idiom as telemetry_test.cpp / server_test.cpp). Registering the key
// first makes the snapshot lookup safe even before instrumentation exists.
uint64_t timerCount(String const& key) {
  Telemetry::timer(key);
  return Telemetry::snapshot().getObject("metrics").get(key).getUInt("count");
}

} // namespace

TEST(LightingTelemetry, PhaseTimersAndCountsPopulate) {
  Telemetry::reset();
  Telemetry::setEnabled(true);
  Telemetry::setDeepEnabled(true);

  CellularLightingCalculator calc;
  calc.setParameters(lightingConfig());
  calc.setMonochrome(false);
  // begin() zero-fills the cell grid, so all cells default to no-obstacle.
  calc.begin(RectI::withSize(Vec2I(0, 0), Vec2I(32, 24)));
  calc.addSpreadLight(Vec2F(8, 8), Vec3F(1.0f, 0.8f, 0.6f));
  calc.addPointLight(Vec2F(16, 12), Vec3F(0.9f, 0.9f, 1.0f), 0.0f, 0.0f, 0.0f);
  Lightmap out;
  calc.calculate(out);

  EXPECT_GT(timerCount("lighting.cpu.spread.us"), 0u);
  EXPECT_GT(timerCount("lighting.cpu.point.us"), 0u);
  EXPECT_GT(timerCount("lighting.cpu.post.us"), 0u);
  EXPECT_EQ(Telemetry::counter("lighting.lights.spread").value(), 1u);
  EXPECT_EQ(Telemetry::counter("lighting.lights.point").value(), 1u);
  EXPECT_GT(Telemetry::gauge("lighting.cells").value(), 0);
  Telemetry::setDeepEnabled(false);
  Telemetry::setEnabled(false);
}

TEST(LightingTelemetry, TimersSilentWithoutDeepTracing) {
  Telemetry::reset();
  Telemetry::setEnabled(true);
  Telemetry::setDeepEnabled(false); // deep OFF: timers must not record (counters still may)

  CellularLightingCalculator calc;
  calc.setParameters(lightingConfig());
  calc.setMonochrome(true); // also exercises the scalar instantiation sharing the same keys
  calc.begin(RectI::withSize(Vec2I(0, 0), Vec2I(16, 16)));
  calc.addSpreadLight(Vec2F(4, 4), Vec3F::filled(1.0f));
  Lightmap out;
  calc.calculate(out);

  EXPECT_EQ(timerCount("lighting.cpu.spread.us"), 0u);
  EXPECT_EQ(timerCount("lighting.cpu.point.us"), 0u);
  Telemetry::setEnabled(false);
}
