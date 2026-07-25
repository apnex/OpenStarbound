#include "StarTelemetry.hpp"
#include "StarTelemetryReporter.hpp"
#include "StarJson.hpp"
#include "StarThread.hpp" // for thread smoke test
#include "StarFile.hpp"
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
  EXPECT_EQ(snap.getObject("metrics").get("test.counter.x").getUInt("value"), 7u);
  EXPECT_EQ(snap.getObject("metrics").get("test.gauge.y").getInt("value"), 42);
  EXPECT_EQ(snap.get("meta").getUInt("schema"), 2u);
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

TEST(Telemetry, TimerRecordsCountTotalMinMax) {
  telemetrySetUp();
  auto t = Telemetry::timer("test.timer");
  t.record(100);
  t.record(50);
  t.record(150);
  Json snap = Telemetry::snapshot();
  Json tj = snap.getObject("metrics").get("test.timer");
  EXPECT_EQ(tj.getUInt("count"), 3u);
  EXPECT_EQ(tj.getInt("total"), 300);
  EXPECT_EQ(tj.getInt("min"), 50);
  EXPECT_EQ(tj.getInt("max"), 150);
  EXPECT_EQ(tj.getInt("mean"), 100);
}

TEST(Telemetry, DeepScopeRecordsOnlyWhenDeepEnabled) {
  telemetrySetUp();
  Telemetry::setDeepEnabled(false);
  {
    auto t = Telemetry::timer("test.deep");
    TelemetryScope s(t);
  } // no record when deep disabled
  EXPECT_EQ(Telemetry::snapshot().getObject("metrics").get("test.deep").getUInt("count"), 0u);

  Telemetry::setDeepEnabled(true);
  {
    auto t = Telemetry::timer("test.deep");
    TelemetryScope s(t);
  }
  EXPECT_GE(Telemetry::snapshot().getObject("metrics").get("test.deep").getUInt("count"), 1u);
  Telemetry::setDeepEnabled(false);
}

TEST(Telemetry, MarkTickBumpsSeqCounter) {
  telemetrySetUp();
  Telemetry::markTick("server");
  Telemetry::markTick("server");
  EXPECT_EQ(Telemetry::counter("tick.server.seq").value(), 2u);
}

TEST(Telemetry, CounterIsThreadSafe) {
  telemetrySetUp();
  auto c = Telemetry::counter("test.threaded");
  const int kThreads = 8, kPer = 10000;
  List<ThreadFunction<void>> threads;
  for (int i = 0; i < kThreads; ++i)
    threads.append(Thread::invoke("telem", [c]() mutable { for (int j = 0; j < kPer; ++j) c.inc(); }));
  threads.clear(); // joins on destruction
  EXPECT_EQ(c.value(), (uint64_t)(kThreads * kPer));
}

TEST(Telemetry, ReporterWritesSnapshotJsonFile) {
  telemetrySetUp();
  Telemetry::counter("test.report.c").inc(5);
  String dir = File::temporaryDirectory();
  String path = TelemetryReporter::writeSnapshot(dir); // returns the file path written
  ASSERT_TRUE(File::exists(path));
  Json read = Json::parse(File::readFileString(path));
  EXPECT_EQ(read.getObject("metrics").get("test.report.c").getUInt("value"), 5u);
  File::remove(path);
  File::removeDirectoryRecursive(dir);
}

TEST(Telemetry, DeclareAttachesDescriptorAndIsIdempotent) {
  telemetrySetUp();
  MetricDesc d{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::declare("test.desc.a", d);
  // Re-declaring with the SAME descriptor is a no-op, not an error: a metric registered from three
  // call sites (render.drawable.parts.rebuilt is registered from three) must declare consistently.
  Telemetry::declare("test.desc.a", d);
  EXPECT_EQ(Telemetry::describe("test.desc.a"), d);
}

TEST(Telemetry, FirstDeclarationWinsAndMismatchIsRecorded) {
  telemetrySetUp();
  MetricDesc gpu{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget};
  MetricDesc cpu{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::declare("test.desc.b", gpu);
  Telemetry::declare("test.desc.b", cpu);   // conflicting -- first wins, conflict is recorded
  EXPECT_EQ(Telemetry::describe("test.desc.b"), gpu);
  // Nothing has been sampled yet, so the key has no node and correctly does not appear in the snapshot
  // (declare() only ever creates a PENDING descriptor -- see StarTelemetry.cpp). Materialize the node via
  // an accessor, as any real caller eventually would, to observe the conflict flag on the wire.
  Telemetry::counter("test.desc.b");
  EXPECT_TRUE(Telemetry::snapshot().getObject("metrics").get("test.desc.b").getBool("descConflict"));
}

TEST(Telemetry, UndeclaredMetricIsOwnerUnknown) {
  telemetrySetUp();
  Telemetry::counter("test.desc.undeclared").inc();
  EXPECT_EQ(Telemetry::describe("test.desc.undeclared").owner, MetricOwner::Unknown);
}

TEST(Telemetry, TypedAccessorOverloadDeclaresInline) {
  telemetrySetUp();
  MetricDesc d{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::timer("test.desc.c", d).record(10);
  EXPECT_EQ(Telemetry::describe("test.desc.c"), d);
}

// Regression for the exact sequence markTick() uses: declare() first against a key with no node yet (held
// pending, since declare() must never guess a MetricType), then a TYPED accessor creates the node. Before
// the fix, declare() created the node itself and guessed MetricType::Timer; a subsequent counter() handed
// back that wrongly-typed node, and inc() wrote to a field snapshot() never reads for a Timer -- the
// counter's value was permanently invisible in the JSON.
TEST(Telemetry, DeclareThenTypedAccessorProducesTheDeclaredType) {
  telemetrySetUp();
  MetricDesc d{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Total};
  Telemetry::declare("test.desc.seq", d);
  Telemetry::counter("test.desc.seq").inc(3);
  Json m = Telemetry::snapshot().getObject("metrics").get("test.desc.seq");
  EXPECT_EQ(m.getString("type"), "counter");
  EXPECT_EQ(m.getUInt("value"), 3u);
  EXPECT_EQ(Telemetry::describe("test.desc.seq"), d);
}

// Nothing else asserts the descriptor->JSON string mapping; transposing two returns in ownerName/roleName
// would pass the rest of the suite silently. Exercises the GPU domain and a non-frame owner (gl) as well.
TEST(Telemetry, SnapshotSerializesDescriptorFieldNames) {
  telemetrySetUp();
  MetricDesc gpu{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::timer("test.desc.names", gpu).record(5);
  Json m = Telemetry::snapshot().getObject("metrics").get("test.desc.names");
  EXPECT_EQ(m.getString("type"), "timer");
  EXPECT_EQ(m.getString("domain"), "gpu");
  EXPECT_EQ(m.getString("owner"), "gl");
  EXPECT_EQ(m.getString("cadence"), "frame");
  EXPECT_EQ(m.getString("role"), "budget");
}

// Defense-in-depth added alongside the declare()/pendingDescs fix: two call sites requesting the same key
// as different MetricTypes is a genuine bug (not the desc-conflict case above), and must be flagged rather
// than silently letting the second accessor's writes land in fields the first type's snapshot branch never
// reads.
TEST(Telemetry, MismatchedAccessorTypeIsFlaggedNotSilent) {
  telemetrySetUp();
  Telemetry::counter("test.desc.typeconflict").inc(1);
  Telemetry::timer("test.desc.typeconflict").record(5); // wrong type for the same key
  Json m = Telemetry::snapshot().getObject("metrics").get("test.desc.typeconflict");
  EXPECT_TRUE(m.getBool("typeConflict"));
  EXPECT_EQ(m.getString("type"), "counter"); // the first type registered wins
  EXPECT_EQ(m.getUInt("value"), 1u);         // unaffected by the timer.record() call
}
