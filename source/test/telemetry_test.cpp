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
  // 4, not 3: every metric now carries unit/clock/source/boundedness, plus measures/validWhen/whole when
  // declared. The bump is the point -- telemetry-window.py refuses a schema it does not recognise rather
  // than mis-windowing. (3 was the per-DOMAIN totals; a whole keyed by owner alone had been dividing
  // cpu-domain parts by a GPU span.)
  EXPECT_EQ(snap.get("meta").getUInt("schema"), 4u);
}

// THE DESCRIPTOR CONVERGENCE, asserted at the wire rather than in the struct. A field that exists in
// MetricDesc but never reaches a snapshot is invisible to every consumer, which is the same shape as a
// counter that never registers: present in the source, absent from the evidence.
TEST(Telemetry, SnapshotCarriesTheFullDescriptor) {
  Telemetry::reset();
  // DESIGNATED, and this site is the reason the rule exists. It was written positionally -- three bare
  // string literals counted out after eight enums -- which is the one construction shape that can put
  // `validWhen`'s text into `measures` with no compile error, no runtime symptom and a green test. The
  // fixture for the transposition hazard was itself transposable. scripts/metric-desc-lint.py refuses it
  // now, and found this site on its first run.
  Telemetry::counter("test.desc.full", MetricDesc{
    .domain = MetricDomain::Cpu, .owner = MetricOwner::Lighting,
    .cadence = MetricCadence::Recompute, .role = MetricRole::Budget,
    .unit = MetricUnit::Nanoseconds, .clock = MetricClock::ThreadCpu,
    .source = MetricSource::ProcFs, .boundedness = MetricBoundedness::Monotonic,
    .measures = "CPU time in the tile gather", .validWhen = "always",
    .whole = "lighting.cpu.total.us"}).inc(1);

  Json m = Telemetry::snapshot().getObject("metrics").get("test.desc.full");
  EXPECT_EQ(m.getString("unit"), "ns");
  EXPECT_EQ(m.getString("clock"), "thread_cpu");
  EXPECT_EQ(m.getString("source"), "procfs");
  EXPECT_EQ(m.getString("boundedness"), "monotonic");
  // Stated as LITERALS, not built from the same helper as the code under test. A fixture that shares the
  // construction shape cannot catch a transposition of these two adjacent same-typed fields -- which is
  // the one hazard the struct still has, every other field being a distinct enum type.
  EXPECT_EQ(m.getString("measures"), "CPU time in the tile gather");
  EXPECT_EQ(m.getString("validWhen"), "always");
  EXPECT_EQ(m.getString("whole"), "lighting.cpu.total.us");
}

// ABSENT, NOT EMPTY. A key missing from the snapshot says "nobody declared this"; a key present as ""
// says "somebody declared nothing". The ratchet that counts undeclared descriptors has to tell them
// apart, so an undeclared prose field must not materialise as an empty string.
TEST(Telemetry, UndeclaredProseFieldsAreAbsentNotEmpty) {
  Telemetry::reset();
  Telemetry::counter("test.desc.bare",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail}).inc(1);

  Json m = Telemetry::snapshot().getObject("metrics").get("test.desc.bare");
  EXPECT_FALSE(m.contains("measures"));
  EXPECT_FALSE(m.contains("validWhen"));
  EXPECT_FALSE(m.contains("whole"));
  // The enums DO materialise, because Undeclared is a real value meaning "nobody has said yet" and the
  // ratchet counts it. Silence and an explicit "undeclared" are different facts too.
  EXPECT_EQ(m.getString("clock"), "undeclared");
  EXPECT_EQ(m.getString("unit"), "undeclared");
}

TEST(Telemetry, ResetZeroesAllMetrics) {
  telemetrySetUp();
  Telemetry::counter("test.r").inc(5);
  Telemetry::gauge("test.rg").set(9);
  auto t = Telemetry::timer("test.rt");
  t.record(100);
  Telemetry::reset();
  EXPECT_EQ(Telemetry::counter("test.r").value(), 0u);
  EXPECT_EQ(Telemetry::gauge("test.rg").value(), 0);
  // reset() is telemetrySetUp() (see above), so a regression here makes the whole suite order-dependent
  // instead of failing loudly -- a stale bucket from `100` surviving reset would silently pollute whatever
  // this key's next test records into it.
  t.record(4);
  JsonArray buckets = Telemetry::snapshot().getObject("metrics").get("test.rt").getArray("buckets");
  uint64_t sum = 0;
  for (auto const& b : buckets)
    sum += b.toUInt();
  EXPECT_EQ(sum, 1u);
  EXPECT_EQ(buckets.at(8).toUInt(), 1u);
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

TEST(Telemetry, SnapshotIsStampedOnBothClocks) {
  // WITHOUT THESE TWO FIELDS A RUN OF SNAPSHOTS IS A PILE OF FILES, NOT A SERIES. The only time a
  // snapshot used to carry was its FILENAME, which is a property of the filesystem: copy or archive the
  // file -- which the harness now does, per leg -- and its position on the axis is gone.
  //
  // Both are asserted, because they do different jobs and either alone silently loses one. Monotonic is
  // the ruler (immune to NTP steps, so it is what an interval is measured with); epoch is the join axis
  // (survives a reboot, and is the base the out-of-process samplers publish). scripts/telemetry-window.py
  // prefers tEpochNs over mtime and reads tMonotonicNs for every interval's duration, so a regression here
  // would not crash it -- it would quietly fall back to mtime and keep producing numbers.
  // EXPLICIT, DISTINCT stamps for the two writes. The filename is telemetry-<monotonicMillis>.json, so two
  // snapshots taken inside one millisecond RESOLVE TO THE SAME PATH and the second silently overwrites the
  // first -- which this test hit on its first run. Harmless at the ~5s report interval and not this task's
  // to fix, but it is a real collision on any faster path, so the test does not depend on the clock ticking
  // between two adjacent calls. The stamps below only NAME the files; both snapshots are stamped from the
  // real clocks regardless, which is the property under test.
  telemetrySetUp();
  String dir = File::temporaryDirectory();
  String path = TelemetryReporter::writeSnapshot(dir, {}, 1);
  Json meta = Json::parse(File::readFileString(path)).getObject("meta");

  // Bounded below by a date already in the past when this was written, so the arm fails on a zero, an
  // absent field or a value in the wrong unit -- not merely on a missing key. A stamp of 0 is the
  // failure mode that would otherwise read as "January 1970" and plot as a single point at the origin.
  ASSERT_TRUE(meta.contains("tEpochNs"));
  EXPECT_GT(meta.getInt("tEpochNs"), 1700000000000000000LL);   // 2023-11-14, nanoseconds
  ASSERT_TRUE(meta.contains("tMonotonicNs"));
  EXPECT_GT(meta.getInt("tMonotonicNs"), 0);

  // AND THE CALLER CANNOT SHADOW THEM. They are stamped after the caller's meta merges, precisely so a
  // joiner can trust them; without this arm that ordering is a comment rather than a property.
  String path2 = TelemetryReporter::writeSnapshot(dir, JsonObject{{"tEpochNs", Json(1)}}, 2);
  Json meta2 = Json::parse(File::readFileString(path2)).getObject("meta");
  EXPECT_GT(meta2.getInt("tEpochNs"), 1700000000000000000LL);

  File::remove(path);
  File::remove(path2);
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

// The mechanism ClientApplication::renderTestMotionVerdict now depends on. That lambda reads three
// render.cache.parallax.* keys whose owner is BackdropPass's constructor, in another translation
// unit; it used to pass a full MetricDesc, making it a second registration site for keys it does not
// own -- safe only while the hand-copied descriptor stayed identical. It now uses the one-argument
// overload, and the whole point is that a READ asserts no descriptor and so cannot disagree with one.
//
// Asserted rather than assumed, because "reading does not re-declare" is exactly the kind of claim
// this project has repeatedly found to be false in the tree while true in a comment.
TEST(Telemetry, ReadAfterDeclareDoesNotConflict) {
  telemetrySetUp();
  MetricDesc const owned{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail};
  Telemetry::counter("test.desc.owned", owned).inc(3);
  // The reader: no descriptor, just the value. This is the cross-TU read.
  EXPECT_EQ(Telemetry::counter("test.desc.owned").value(), 3u);
  Json m = Telemetry::snapshot().getObject("metrics").get("test.desc.owned");
  EXPECT_FALSE(m.getBool("descConflict"));
  EXPECT_FALSE(m.getBool("typeConflict"));
  // ...and the read did not erase what the owner declared.
  EXPECT_EQ(Telemetry::describe("test.desc.owned").owner, MetricOwner::Frame);
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

TEST(Telemetry, HistogramBucketBoundaries) {
  // Bucket i covers [2^h * (1 + m/4), 2^h * (1 + (m+1)/4)) for h = i/4, m = i%4.
  // h is floor(log2(v)) and m is the two bits below the MSB, so the index is exact and integer-only.
  EXPECT_EQ(Telemetry::histogramBucket(0), 0u);     // clamped: a 0us sample is real (sub-microsecond work)
  EXPECT_EQ(Telemetry::histogramBucket(1), 0u);     // 2^0 * 1.00
  // 2 and 3 are the h<2 cases: fewer than two bits exist below the msb, so the index is synthesised by a LEFT
  // shift. Getting this wrong is undefined behaviour (a negative right-shift count), not just a wrong bucket.
  EXPECT_EQ(Telemetry::histogramBucket(2), 4u);     // h=1, m=0
  EXPECT_EQ(Telemetry::histogramBucket(3), 6u);     // h=1, m=2
  EXPECT_EQ(Telemetry::histogramBucket(4), 8u);     // h=2, m=0 -> 4*2+0
  EXPECT_EQ(Telemetry::histogramBucket(5), 9u);     // h=2, m=1  (5 = 0b101)
  EXPECT_EQ(Telemetry::histogramBucket(6), 10u);    // h=2, m=2  (6 = 0b110)
  EXPECT_EQ(Telemetry::histogramBucket(7), 11u);    // h=2, m=3  (7 = 0b111)
  EXPECT_EQ(Telemetry::histogramBucket(8), 12u);    // h=3, m=0
  EXPECT_EQ(Telemetry::histogramBucket(65535), 63u);   // top of range
  EXPECT_EQ(Telemetry::histogramBucket(1000000), 63u); // a 1-second frame clamps into the top bucket
  EXPECT_EQ(Telemetry::histogramBucket(-5), 0u);       // a negative delta is nonsense; do not index OOB
}

TEST(Telemetry, TimerFillsHistogramAndSumMatchesCount) {
  telemetrySetUp();
  auto t = Telemetry::timer("test.hist");
  for (int64_t v : {1, 4, 4, 5, 8, 100, 100000})
    t.record(v);
  Json m = Telemetry::snapshot().getObject("metrics").get("test.hist");
  JsonArray buckets = m.getArray("buckets");
  uint64_t sum = 0;
  for (auto const& b : buckets)
    sum += b.toUInt();
  // The histogram is the instrument's own checksum against count -- but only exactly, as asserted here,
  // when the sampling thread is quiescent at snapshot time (single-threaded, as this test is). record()
  // bumps count and its bucket with two separate relaxed stores, so on a LIVE multi-threaded timer a
  // snapshot taken mid-record() can catch them out of order; sum and count may then differ by up to the
  // number of threads in flight, in either direction. That is not this test's concern.
  EXPECT_EQ(sum, m.getUInt("count"));
  EXPECT_EQ(sum, 7u);
  EXPECT_EQ(buckets.at(8).toUInt(), 2u);   // the two 4us samples
  EXPECT_EQ(buckets.at(63).toUInt(), 1u);  // the 100ms sample
}

// Pins the trailing-zero trim, the one piece of genuinely new logic in snapshot()'s Timer branch and the wire
// contract the consumer zero-pads against ("never trim" would also pass TimerFillsHistogramAndSumMatchesCount,
// since that test's highest sample already reaches bucket 63).
TEST(Telemetry, HistogramArrayIsTrimmedAfterHighestFilledBucket) {
  telemetrySetUp();
  Telemetry::timer("test.hist.trim").record(1); // bucket 0 only
  JsonArray buckets = Telemetry::snapshot().getObject("metrics").get("test.hist.trim").getArray("buckets");
  EXPECT_EQ(buckets.size(), 1u);
  EXPECT_EQ(buckets.at(0).toUInt(), 1u);
}

TEST(Telemetry, HistogramIsEmptyForCounters) {
  telemetrySetUp();
  Telemetry::counter("test.hist.counter").inc();
  EXPECT_FALSE(Telemetry::snapshot().getObject("metrics").get("test.hist.counter").contains("buckets"));
}

// THE INSTRUMENT'S ORACLE. The renderer has oracles that fail loudly; telemetry had none, and two measurement
// errors in this arc were exactly the class an oracle catches -- a parts-sum of 119% of the whole, and a
// metric divided by the wrong denominator. These are those errors as failing tests.

TEST(Telemetry, BudgetPartsCloseAgainstOwnerTotal) {
  telemetrySetUp();
  MetricDesc total{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Total};
  MetricDesc part{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  MetricDesc detail{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail};

  Telemetry::timer("test.budget.total.us", total).record(1000);
  Telemetry::timer("test.budget.input.us", part).record(200);
  Telemetry::timer("test.budget.render.us", part).record(600);
  // A Detail metric nests INSIDE a Budget part. If closure counted it, the parts would exceed the whole --
  // which is exactly the double-count the role field exists to prevent.
  Telemetry::timer("test.budget.nested.us", detail).record(550);

  JsonObject metrics = Telemetry::snapshot().getObject("metrics");
  int64_t parts = 0;
  for (auto const& kv : metrics) {
    Json m = kv.second;
    if (m.getString("type") == "timer" && m.getString("owner") == "frame"
        && m.getString("domain") == "cpu" && m.getString("role") == "budget")
      parts += m.getInt("total");
  }
  int64_t whole = metrics.get("test.budget.total.us").getInt("total");
  EXPECT_EQ(parts, 800);
  EXPECT_LE(parts, whole);
  EXPECT_GE(whole - parts, 0);
}

TEST(Telemetry, OwnersDeclareDenominatorAndTotal) {
  telemetrySetUp();
  JsonObject owners = Telemetry::snapshot().getObject("owners");
  // THESE ARE DIFFERENT QUESTIONS AND, FOR `frame`, DIFFERENT METRICS -- which is the assertion.
  //
  // The DENOMINATOR counts ticks, and one loop iteration is one frame, so cpu.frame.total.us is right:
  // it fires exactly once per frame whatever the frame did.
  //
  // The TOTAL is the whole the budget parts close against, and total.us cannot be it. It is a PACING
  // period containing Thread::sleepPrecise, so it does not shrink when work does -- the saving moves
  // into the sleep and the period reads identical. Measured: 16,212us of pace holding 5,275us of work.
  // With idle declared a Budget part of that same whole the sum reached ~100% no matter what any part
  // did, so the closure oracle ran, compared, and could not fail.
  //
  // cpu.frame.work.us is the span from the top of the iteration to the moment the sleep begins. A lever
  // that removes work shrinks it, so the closure can now be WRONG, which is the only condition under
  // which it can also be right.
  EXPECT_EQ(owners.get("frame").getString("denominator"), "cpu.frame.total.us");
  EXPECT_EQ(owners.get("frame").getObject("totals").get("cpu").toString(), "cpu.frame.work.us");
  EXPECT_EQ(owners.get("gl").getString("denominator"), "cpu.frame.total.us");

  // `gl` DECLARES NO WHOLE IN EITHER DOMAIN, and both absences are the assertion.
  //
  // The GPU one used to say render.frame.gpu_span_us. That is a GL_TIME_ELAPSED span -- elapsed
  // TIMELINE between two GPU markers, stalls and gaps included -- and it reported ~16,200us, the frame
  // PERIOD, at 0.22%, 11.39%, 23.39% and 32.93% real engine busy alike. A denominator that does not
  // move when the quantity it denominates changes by 1.4x is not a denominator, so every "% of the GPU
  // frame" derived from it was a ratio against a constant. The row is withdrawn rather than replaced:
  // an absent whole is reported by the consumer's no-whole branch, which teaches a reader something
  // true, where a wrong one taught them something false and looked identical.
  //
  // The CPU one was never there. The consumer sums budget parts per domain, so before totals were
  // keyed by domain a cpu-domain part under `gl` would have been divided by a GPU span and printed as
  // a plausible percentage.
  //
  // A whole may be added back to EITHER domain only when one is MEASURED for this owner -- for gpu
  // that means the per-client drm-engine counters or the PMU, both of which measure work. Never to
  // make a column non-empty. scripts/metric-desc-lint.py --check-timeline refuses the re-promotion
  // mechanically; this asserts the table that would have to change alongside it.
  EXPECT_FALSE(owners.get("gl").getObject("totals").contains("gpu"));
  EXPECT_FALSE(owners.get("gl").getObject("totals").contains("cpu"));
  // `sim` HAD no measured whole, and this line used to assert it must not invent one. #175 gave it a real
  // one -- tick.server.total.us wraps the server loop body minus the pacing sleep -- so the assertion
  // inverts. The principle the original comment was protecting is unchanged and worth restating: an owner
  // must not declare a total it does not MEASURE. This one is measured.
  EXPECT_EQ(owners.get("sim").getString("denominator"), "tick.server.seq");
  EXPECT_EQ(owners.get("sim").getObject("totals").get("cpu").toString(), "tick.server.total.us");
}

// The lighting owner's contract had no automated guard: its denominator counts RECOMPUTES while its
// total accumulates over FRAMES, and that mismatch is deliberate -- it is the one live instance of a
// legitimately mixed-cadence owner, and the reason the consumer checks each metric against its OWN
// cadence rather than the owner's ticks. Silently "fixing" it by re-scoping the Total would delete the
// only measurement of the temporal gate's skip path. Pin it.
TEST(Telemetry, LightingOwnerDeclaresRecomputeDenominatorAndFrameTotal) {
  telemetrySetUp();
  JsonObject owners = Telemetry::snapshot().getObject("owners");
  ASSERT_TRUE(owners.contains("lighting"));
  EXPECT_EQ(owners.get("lighting").getString("denominator"), "lighting.temporal.recomputed");
  EXPECT_EQ(owners.get("lighting").getObject("totals").get("cpu").toString(), "lighting.cpu.total.us");
}

TEST(Telemetry, CadenceCountNeverExceedsDenominator) {
  telemetrySetUp();
  MetricDesc total{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Total};
  MetricDesc part{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  auto frames = Telemetry::timer("test.cadence.total.us", total);
  auto gated = Telemetry::timer("test.cadence.gated.us", part);
  for (int i = 0; i < 10; ++i)
    frames.record(1000);
  for (int i = 0; i < 6; ++i)   // a refresh-gated pass fires on only some frames
    gated.record(100);

  JsonObject metrics = Telemetry::snapshot().getObject("metrics");
  uint64_t denom = metrics.get("test.cadence.total.us").getUInt("count");
  uint64_t count = metrics.get("test.cadence.gated.us").getUInt("count");
  // UNDER is legitimate and expected -- a gated pass or an async GPU readback samples only some frames, which
  // is what the coverage figure reports. OVER is always a bug: the span was opened twice in one frame and the
  // metric should have been declared cadence=Call. A symmetric "count ~= denominator" assertion would flag
  // every gated pass in the tree as broken.
  EXPECT_LE(count, denom);
  EXPECT_EQ(count, 6u);
  EXPECT_EQ(denom, 10u);
}

// A DECLARATION AFTER REGISTRATION must still land. OpenGlRenderer may register a GPU key generically on an
// earlier frame than the pass that describes it, so accessor-first is a normal ordering, not an edge case.
TEST(Telemetry, DeclareAfterRegistrationStillApplies) {
  telemetrySetUp();
  Telemetry::timer("test.late.decl").record(5);          // registered with no descriptor
  EXPECT_EQ(Telemetry::describe("test.late.decl").owner, MetricOwner::Unknown);
  MetricDesc d{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::declare("test.late.decl", d);               // ...declared afterwards
  EXPECT_EQ(Telemetry::describe("test.late.decl"), d);
  EXPECT_EQ(Telemetry::snapshot().getObject("metrics").get("test.late.decl").getString("domain"), "gpu");
}
