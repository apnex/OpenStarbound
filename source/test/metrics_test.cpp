#include "StarClientBusyReader.hpp"
#include "StarEngineBusyReader.hpp"
#include "StarMetricSample.hpp"
#include "StarThreadBusyReader.hpp"

#include "StarFile.hpp"

#include <unistd.h>

#include "gtest/gtest.h"

using namespace Star;

TEST(MetricSampleTest, CarriesMeaningAlongsideValue) {
  MetricSample s("gpu.engine.render.busy_ns", 1234.0, "ns",
                 "render engine busy time, whole process",
                 "always",
                 "fdinfo:drm-engine-render", 42);
  EXPECT_EQ(s.key, "gpu.engine.render.busy_ns");
  EXPECT_EQ(s.value, 1234.0);
  EXPECT_EQ(s.measures, "render engine busy time, whole process");
  EXPECT_EQ(s.validWhen, "always");
}

// A sample whose meaning is blank is worse than no sample: it reads as authoritative.
TEST(MetricSampleTest, RejectsEmptyMeaning) {
  EXPECT_THROW(MetricSample("k", 1.0, "ns", "", "always", "src", 0), MetricsException);
  EXPECT_THROW(MetricSample("k", 1.0, "ns", "quantity", "", "src", 0), MetricsException);
}

namespace {

// Owns the directory it writes, so an assertion that fires mid-test still removes it; converts to
// String so a fixture is not needed to reach the path.
class TempFdinfoDir {
public:
  // Separators are TABS because that is what the kernel writes. A parser that only handles spaces
  // would pass a space-separated fixture and then read nothing at all from real /proc.
  explicit TempFdinfoDir(List<pair<int64_t, int64_t>> const& fds)
    : m_path(File::temporaryDirectory()) {
    for (size_t i = 0; i < fds.size(); ++i)
      File::writeFile(String(strf("drm-driver:\ti915\n"
                                  "drm-client-id:\t{}\n"
                                  "drm-engine-render:\t{} ns\n",
                                  fds[i].first, fds[i].second)),
                      File::relativeTo(m_path, strf("{}", i)));
  }

  // Verbatim file bodies, for shapes the DRM form cannot express -- an fd that is fdinfo-shaped and
  // carries no engine line at all is exactly the case that must not read as zero busy time.
  explicit TempFdinfoDir(List<String> const& bodies)
    : m_path(File::temporaryDirectory()) {
    for (size_t i = 0; i < bodies.size(); ++i)
      File::writeFile(bodies[i], File::relativeTo(m_path, strf("{}", i)));
  }

  ~TempFdinfoDir() { File::removeDirectoryRecursive(m_path); }

  TempFdinfoDir(TempFdinfoDir const&) = delete;
  TempFdinfoDir& operator=(TempFdinfoDir const&) = delete;

  operator String const&() const { return m_path; }

private:
  String m_path;
};

TempFdinfoDir makeFdinfoDir(List<pair<int64_t, int64_t>> const& fds) {
  return TempFdinfoDir(fds);
}

// Distinctly named rather than overloaded: String is constructible from a braced pair of integers,
// so an overload set would make every existing makeFdinfoDir({{158, ...}}) call ambiguous.
TempFdinfoDir makeRawFdinfoDir(List<String> const& bodies) {
  return TempFdinfoDir(bodies);
}

}

TEST(ClientBusyReaderTest, DeduplicatesFdsSharingOneClientId) {
  // Four fds, ONE client, identical totals -- the exact shape that inflated a reading 4x.
  auto dir = makeFdinfoDir({{158, 3213824640}, {158, 3213824640},
                            {158, 3213824640}, {158, 3213824640}});
  auto r = ClientBusyReader::readFdinfoDir(dir);
  ASSERT_TRUE(r.available) << r.unavailableReason.utf8Ptr();
  EXPECT_EQ(r.clients, 1u);
  EXPECT_EQ(r.busyNs.get("render"), 3213824640);   // 1x, NOT 4x
}

TEST(ClientBusyReaderTest, SumsDistinctClients) {
  auto dir = makeFdinfoDir({{158, 1000}, {158, 1000}, {201, 500}});
  auto r = ClientBusyReader::readFdinfoDir(dir);
  ASSERT_TRUE(r.available);
  EXPECT_EQ(r.clients, 2u);
  EXPECT_EQ(r.busyNs.get("render"), 1500);
}

// render.pass.compose.gpu_us reported 0us for its whole existence because it bracketed a conditional
// that never ran, and an empty bracket is indistinguishable from free work. The two tests below hold
// "could not measure" apart from "measured zero" at the reader's two failure doors, so a later
// refactor cannot collapse the distinction by returning a default-constructed count.
TEST(ClientBusyReaderTest, MissingProcessIsUnavailableNotZero) {
  auto r = ClientBusyReader::read(0x7FFFFFFF);   // above pid_max on any configuration
  EXPECT_FALSE(r.available);
  EXPECT_FALSE(r.unavailableReason.empty());
  EXPECT_TRUE(r.busyNs.empty());               // NOT {"render": 0}
}

TEST(ClientBusyReaderTest, NonDrmDirectoryIsUnavailableNotZero) {
  // A real non-DRM fd: fdinfo-shaped, parses cleanly, carries no engine line.
  auto dir = makeRawFdinfoDir({String("pos:\t0\nflags:\t0100000\n")});
  auto r = ClientBusyReader::readFdinfoDir(dir);
  EXPECT_FALSE(r.available);
  EXPECT_TRUE(r.unavailableReason.contains("drm-engine")) << r.unavailableReason.utf8Ptr();
}

TEST(BusyDeltaTest, ForwardDeltaIsTheDifference) {
  BusyReading a, b;
  a.available = b.available = true;
  a.clients = b.clients = 1;
  a.busyNs["render"] = 1000;
  b.busyNs["render"] = 3000;
  auto d = busyDelta(a, b, 4000);
  ASSERT_TRUE(d.available) << d.unavailableReason.utf8Ptr();
  EXPECT_EQ(d.busyNs.get("render"), 2000);
}

// A client that exits and restarts resets its counters to zero. Differencing across that reset
// reports a delta the hardware never did -- and a NEGATIVE one clamped to zero would read as an
// idle GPU, which is the same "measured zero" lie as an empty bracket.
TEST(BusyDeltaTest, BackwardsCounterIsDiscardedNotReported) {
  BusyReading a, b;
  a.available = b.available = true;
  a.clients = b.clients = 1;
  a.busyNs["render"] = 3000;
  b.busyNs["render"] = 1000;      // client restarted
  auto d = busyDelta(a, b, 4000);
  EXPECT_FALSE(d.available) << "reported delta = " << d.busyNs.value("render", 0) << " ns";
  EXPECT_TRUE(d.unavailableReason.contains("backwards")) << d.unavailableReason.utf8Ptr();
}

TEST(BusyDeltaTest, UnavailableEndpointPoisonsTheDelta) {
  BusyReading a; a.available = true; a.clients = 1; a.busyNs["render"] = 1000;
  auto d = busyDelta(a, BusyReading::unavailable("process exited"), 4000);
  EXPECT_FALSE(d.available);
}

namespace {

// A test window, not a measurement window: long enough to run several polls, short enough that the
// one test which reaches the poll loop costs ~0.4s with the warm-up busyOver charges on top of it.
// Nothing here asserts a busy VALUE -- that is what the 20-run comparison against
// scripts/pmu-render-busy.py under real load is for, and a value assertion here would be an
// assertion about whatever else the machine was doing.
double const TestWindowSeconds = 0.1;

// Reads a sysfs attribute with the test's OWN code, rather than borrowing the reader's helper: a
// test that asked the reader what the unit was could only ever agree with it.
// File::readFileString is not usable here -- sysfs answers lseek(SEEK_END) with the page size while
// reads return 0, so atEnd never flips and it spins forever.
Maybe<String> readSysfsAttribute(String const& path) {
  try {
    FilePtr file = File::open(path, IOMode::Read);
    std::string text;
    char buffer[128];
    while (size_t got = file->read(buffer, sizeof(buffer)))
      text.append(buffer, got);
    return String(text).trim();
  } catch (StarException const&) {
    return {};
  }
}

String const I915EventDir = "/sys/bus/event_source/devices/i915/events";

}

// Runs on any machine. Either the PMU opens and the window reports, or it declines with a reason
// naming why. What it must NEVER do is report a number it did not obtain.
TEST(EngineBusyReaderTest, EitherReadsOrExplainsItself) {
  EngineBusyReader reader;
  auto opened = reader.open("rcs0-busy");
  if (!opened.opened) {
    EXPECT_FALSE(opened.reason.empty());
    EXPECT_TRUE(opened.reason.contains("perf_event") || opened.reason.contains("i915"))
        << opened.reason.utf8Ptr();
    return;
  }

  auto window = reader.busyOver(TestWindowSeconds);
  ASSERT_TRUE(window.busy.available) << window.busy.unavailableReason.utf8Ptr();
  EXPECT_TRUE(window.busy.busyNs.contains("rcs0"));
  // The denominator is the clock the poll loop observed, so it covers at least the window asked for.
  // Anything shorter would mean the loop never ran and the reading is two adjacent reads again.
  EXPECT_GE(window.wallNs, (int64_t)(TestWindowSeconds * 1e9));
}

TEST(EngineBusyReaderTest, UnknownEventIsUnavailable) {
  EngineBusyReader reader;
  auto opened = reader.open("no-such-event");
  EXPECT_FALSE(opened.opened);
  EXPECT_FALSE(opened.reason.empty());
}

TEST(EngineBusyReaderTest, MeasureBeforeOpenIsUnavailable) {
  EngineBusyReader reader;
  auto window = reader.busyOver(TestWindowSeconds);
  EXPECT_FALSE(window.busy.available);
  EXPECT_FALSE(window.busy.unavailableReason.empty());
  EXPECT_EQ(window.wallNs, 0);
  EXPECT_TRUE(window.busy.busyNs.empty());     // NOT {"rcs0": 0}
}

// A window that is not a duration is a caller error, and it is refused before the counter state is
// even consulted -- which is what lets this run on a machine with no i915 at all.
TEST(EngineBusyReaderTest, NonPositiveWindowIsRefused) {
  EngineBusyReader reader;
  auto window = reader.busyOver(0.0);
  EXPECT_FALSE(window.busy.available);
  EXPECT_TRUE(window.busy.unavailableReason.contains("window"))
      << window.busy.unavailableReason.utf8Ptr();
}

// The unit is the DRIVER's declaration, not a guess from the event's name. actual-frequency-gt0 is
// declared in 'M' -- megahertz -- and a ratio of megahertz against wall-clock nanoseconds is not a
// busy fraction, so the reader must refuse it and say what it actually found.
//
// The assertion on the unit string is conditional on this host exposing the event, because a CI
// runner with no i915 refuses one step earlier, at the PMU. The refusal itself is asserted
// everywhere; only the naming of 'M' needs the hardware present, and it needs no privilege, since
// the unit is checked before perf_event_open.
TEST(EngineBusyReaderTest, NonTimeEventIsRefusedByItsDeclaredUnit) {
  EngineBusyReader reader;
  auto opened = reader.open("actual-frequency-gt0");
  ASSERT_FALSE(opened.opened) << "a megahertz counter was accepted into a nanosecond field";
  ASSERT_FALSE(opened.reason.empty());

  auto unit = readSysfsAttribute(String(strf("{}/actual-frequency-gt0.unit", I915EventDir)));
  if (!unit)
    return;
  EXPECT_TRUE(opened.reason.contains(strf("'{}'", *unit)))
      << "refusal does not name the unit " << unit->utf8Ptr() << ": " << opened.reason.utf8Ptr();
  EXPECT_TRUE(opened.reason.contains("nanoseconds")) << opened.reason.utf8Ptr();
}

// ---------------------------------------------------------------------------------------------------
// ThreadBusyReader -- CPU busy, attributed to the declared owner vocabulary.
// ---------------------------------------------------------------------------------------------------

namespace {

// A /proc/<pid>/task-shaped directory: one subdirectory per tid, each holding a `stat` file.
class TempTaskDir {
public:
  // (tid, comm, utime ticks, stime ticks)
  explicit TempTaskDir(List<tuple<int, String, int64_t, int64_t>> const& tasks)
    : m_path(File::temporaryDirectory()) {
    for (auto const& t : tasks) {
      String sub = File::relativeTo(m_path, toString(get<0>(t)));
      File::makeDirectory(sub);
      // Fields 3..15 with utime at 14 and stime at 15, matching the kernel's layout. The filler is
      // real-shaped rather than zeros so an off-by-one in the reader lands on a NUMBER and produces a
      // wrong answer, not a parse failure that would look like a different bug entirely.
      File::writeFile(String(strf("{} ({}) S 1 1 1 0 -1 4194304 100 0 0 0 {} {} 0 0 20 0 5 0 0\n",
                                  get<0>(t), get<1>(t), get<2>(t), get<3>(t))),
                      File::relativeTo(sub, "stat"));
    }
  }
  ~TempTaskDir() { try { File::removeDirectoryRecursive(m_path); } catch (...) {} }
  TempTaskDir(TempTaskDir const&) = delete;
  TempTaskDir& operator=(TempTaskDir const&) = delete;
  operator String const&() const { return m_path; }
private:
  String m_path;
};

int64_t nsPerTickForTest() {
  long hz = sysconf(_SC_CLK_TCK);
  return hz > 0 ? 1'000'000'000LL / (int64_t)hz : 10'000'000LL;
}

}

TEST(ThreadBusyReaderTest, MapsMeasuredThreadNamesToOwners) {
  // The names are what the kernel actually reports, truncated to 15 characters -- matching the names
  // the engine SETS would match nothing.
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("starbound"), MetricOwner::Frame);
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("WorldServerThre"), MetricOwner::Sim);
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("WorldClient::li"), MetricOwner::Lighting);
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("SDLAudioP15"), MetricOwner::Unknown);
}

TEST(ThreadBusyReaderTest, TheDriverThreadIsNotTheMainThread) {
  // "starboun:gdrv0" shares a prefix with "starbound", so a prefix test in the other order bills every
  // GPU driver tick to Frame. This is the arm that pins the ordering.
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("starboun:gdrv0"), MetricOwner::Gl);
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("starboun:disk$0"), MetricOwner::Gl);
}

TEST(ThreadBusyReaderTest, SimulationLookalikesStayUnknownOnPurpose) {
  // Both are simulation work by any reasonable reading, and owner `sim`'s declared TOTAL brackets the
  // WorldServerThread loop and nothing else. Billing them to Sim would put parts under a whole that
  // does not contain them, which is the one error the closure check cannot catch because it IS the
  // closure check.
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("UniverseServer/"), MetricOwner::Unknown);
  EXPECT_EQ(ThreadBusyReader::ownerOfThread("SystemWorldServ"), MetricOwner::Unknown);
}

TEST(ThreadBusyReaderTest, ParsesACommContainingSpacesAndParentheses) {
  // THE FIELD THAT BREAKS EVERY NAIVE PARSER. The kernel does not escape the thread name, so a name
  // with spaces shifts every later field and utime lands on some other number -- plausible, numeric,
  // silently wrong. The name ends at the LAST ')' and nowhere else.
  TempTaskDir dir({{101, "we (are) many", 30, 12}});
  auto r = ThreadBusyReader::readTaskDir(dir);
  ASSERT_TRUE(r.available) << r.unavailableReason.utf8Ptr();
  EXPECT_EQ(r.busyNs.get("unknown"), 42 * nsPerTickForTest())
      << "a comm with spaces shifted the field offsets";
}

TEST(ThreadBusyReaderTest, AttributesPerOwnerAndConservesTheTotal) {
  TempTaskDir dir({{1, "starbound", 100, 20},        // frame    120
                   {2, "WorldServerThre", 50, 10},   // sim       60
                   {3, "WorldClient::li", 5, 0},     // lighting   5
                   {4, "starboun:gdrv0", 2, 1},      // gl         3
                   {5, "SDLAudioP15", 7, 0}});       // unknown    7
  auto r = ThreadBusyReader::readTaskDir(dir);
  ASSERT_TRUE(r.available) << r.unavailableReason.utf8Ptr();
  int64_t const k = nsPerTickForTest();
  EXPECT_EQ(r.busyNs.get("frame"), 120 * k);
  EXPECT_EQ(r.busyNs.get("sim"), 60 * k);
  EXPECT_EQ(r.busyNs.get("lighting"), 5 * k);
  EXPECT_EQ(r.busyNs.get("gl"), 3 * k);
  EXPECT_EQ(r.busyNs.get("unknown"), 7 * k);
  EXPECT_EQ(r.threads, 5u);

  // CONSERVATION. Every thread's time reaches exactly one owner, so the owners sum to the threads.
  // A reader that dropped what it could not name would under-report by an amount invisible precisely
  // because it is missing -- this is the arm that makes that impossible rather than unlikely.
  int64_t sum = 0;
  for (auto const& o : r.busyNs)
    sum += o.second;
  EXPECT_EQ(sum, 195 * k);
}

TEST(ThreadBusyReaderTest, NothingParsableIsUnavailableNotZero) {
  // The distinction the whole type exists for: "could not measure" must never arrive as "measured
  // nothing", which is a number a caller would happily plot.
  String empty = File::temporaryDirectory();
  auto r = ThreadBusyReader::readTaskDir(empty);
  EXPECT_FALSE(r.available);
  EXPECT_TRUE(r.busyNs.empty());
  EXPECT_FALSE(r.unavailableReason.empty());
  File::removeDirectoryRecursive(empty);
}

TEST(ThreadBusyReaderTest, ProcessTotalIsTheGroupAggregateNotTheLiveSum) {
  // BURN A MEASURABLE AMOUNT FIRST. Read straight away this failed at "0 vs 0" -- run alone under a
  // filter the test process has used less than one 10ms tick, so the assertion below was green in the
  // full suite and red on its own. A test whose verdict depends on which other tests ran is not a
  // test. Fifty milliseconds of arithmetic is several ticks on any USER_HZ a platform reports.
  volatile int64_t sink = 0;
  for (int64_t i = 0; i < 40'000'000; ++i)
    sink += i;
  (void)sink;

  auto own = ThreadBusyReader::processBusyNs((int)getpid());
  ASSERT_TRUE(own.isValid()) << "could not read this process's own /proc stat";
  EXPECT_GT(*own, 0) << "a running test process cannot have used zero CPU";

  // AND IT IS NOT THE SAME QUANTITY AS read()'s SUM. /proc/<pid>/stat retains the time of threads that
  // have exited; /proc/<pid>/task lists only the living, so the aggregate is >= the live sum and the
  // difference is what no owner can still be charged for. Asserting the INEQUALITY rather than a value
  // keeps the arm true on a process that has never reaped a thread, where the two are equal.
  auto live = ThreadBusyReader::read((int)getpid());
  ASSERT_TRUE(live.available) << live.unavailableReason.utf8Ptr();
  int64_t sum = 0;
  for (auto const& o : live.busyNs)
    sum += o.second;
  EXPECT_GE(*own, sum - 2 * 10'000'000LL)
      << "the group aggregate fell below the live sum by more than read skew: " << *own << " vs " << sum;
}

TEST(ThreadBusyReaderTest, AMissingProcessHasNoTotalRatherThanZero) {
  EXPECT_FALSE(ThreadBusyReader::processBusyNs(0x7FFFFFFF).isValid());
}
