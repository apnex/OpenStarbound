#include "StarTelemetry.hpp"
#include "StarJson.hpp"
#include "gtest/gtest.h"

using namespace Star;

// NOTE: the suite is named "Telemetry" (bare TEST, not a TEST_F fixture named
// "TelemetryTest") so the project gate `--gtest_filter='Telemetry.*'` selects these.
// A fixture would yield suite "TelemetryTest" (the glob's literal '.' won't match the
// 'T' of "Test"), and a fixture literally named "Telemetry" would collide with the
// Star::Telemetry class under `using namespace Star`. Each test inlines the original
// fixture SetUp: reset() + setEnabled(true).
static void telemetrySetUp() { Telemetry::reset(); Telemetry::setEnabled(true); }

TEST(Telemetry, CounterIncrementsAndIsIdempotentByKey) {
  telemetrySetUp();
  auto a = Telemetry::counter("test.counter.a");
  auto b = Telemetry::counter("test.counter.a"); // same key -> same node
  a.inc();
  b.inc(3);
  EXPECT_EQ(a.value(), 4u);
  EXPECT_EQ(b.value(), 4u);
  auto c = Telemetry::counter("test.counter.b"); // distinct key -> independent
  EXPECT_EQ(c.value(), 0u);
}

TEST(Telemetry, GaugeSetsAndAdds) {
  telemetrySetUp();
  auto g = Telemetry::gauge("test.gauge");
  g.set(10);
  EXPECT_EQ(g.value(), 10);
  g.add(-4);
  EXPECT_EQ(g.value(), 6);
}

TEST(Telemetry, SnapshotEmitsCountersAndGaugesBuckets) {
  telemetrySetUp();
  Telemetry::counter("test.counter.x").inc(7);
  Telemetry::gauge("test.gauge.y").set(42);
  Json snap = Telemetry::snapshot();
  EXPECT_EQ(snap.getObject("counters").get("test.counter.x").toUInt(), 7u);
  EXPECT_EQ(snap.getObject("gauges").get("test.gauge.y").toInt(), 42);
}

TEST(Telemetry, ResetZeroesAllMetrics) {
  telemetrySetUp();
  Telemetry::counter("test.r").inc(5);
  Telemetry::gauge("test.rg").set(9);
  Telemetry::reset();
  EXPECT_EQ(Telemetry::counter("test.r").value(), 0u);
  EXPECT_EQ(Telemetry::gauge("test.rg").value(), 0);
}

TEST(Telemetry, SnapshotIsReadOnly) {
  telemetrySetUp();
  auto c = Telemetry::counter("test.ro");
  c.inc(2);
  (void)Telemetry::snapshot();
  EXPECT_EQ(c.value(), 2u); // snapshot must not mutate
}
