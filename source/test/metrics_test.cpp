#include "StarClientBusyReader.hpp"
#include "StarEngineBusyReader.hpp"
#include "StarMetricSample.hpp"

#include "StarFile.hpp"

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
  EXPECT_EQ(r.engineNs.get("render"), 3213824640);   // 1x, NOT 4x
}

TEST(ClientBusyReaderTest, SumsDistinctClients) {
  auto dir = makeFdinfoDir({{158, 1000}, {158, 1000}, {201, 500}});
  auto r = ClientBusyReader::readFdinfoDir(dir);
  ASSERT_TRUE(r.available);
  EXPECT_EQ(r.clients, 2u);
  EXPECT_EQ(r.engineNs.get("render"), 1500);
}

// render.pass.compose.gpu_us reported 0us for its whole existence because it bracketed a conditional
// that never ran, and an empty bracket is indistinguishable from free work. The two tests below hold
// "could not measure" apart from "measured zero" at the reader's two failure doors, so a later
// refactor cannot collapse the distinction by returning a default-constructed count.
TEST(ClientBusyReaderTest, MissingProcessIsUnavailableNotZero) {
  auto r = ClientBusyReader::read(0x7FFFFFFF);   // above pid_max on any configuration
  EXPECT_FALSE(r.available);
  EXPECT_FALSE(r.unavailableReason.empty());
  EXPECT_TRUE(r.engineNs.empty());               // NOT {"render": 0}
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
  a.engineNs["render"] = 1000;
  b.engineNs["render"] = 3000;
  auto d = busyDelta(a, b, 4000);
  ASSERT_TRUE(d.available) << d.unavailableReason.utf8Ptr();
  EXPECT_EQ(d.engineNs.get("render"), 2000);
}

// A client that exits and restarts resets its counters to zero. Differencing across that reset
// reports a delta the hardware never did -- and a NEGATIVE one clamped to zero would read as an
// idle GPU, which is the same "measured zero" lie as an empty bracket.
TEST(BusyDeltaTest, BackwardsCounterIsDiscardedNotReported) {
  BusyReading a, b;
  a.available = b.available = true;
  a.clients = b.clients = 1;
  a.engineNs["render"] = 3000;
  b.engineNs["render"] = 1000;      // client restarted
  auto d = busyDelta(a, b, 4000);
  EXPECT_FALSE(d.available) << "reported delta = " << d.engineNs.value("render", 0) << " ns";
  EXPECT_TRUE(d.unavailableReason.contains("backwards")) << d.unavailableReason.utf8Ptr();
}

TEST(BusyDeltaTest, UnavailableEndpointPoisonsTheDelta) {
  BusyReading a; a.available = true; a.clients = 1; a.engineNs["render"] = 1000;
  auto d = busyDelta(a, BusyReading::unavailable("process exited"), 4000);
  EXPECT_FALSE(d.available);
}

// Runs on any machine. Either the PMU opens and reports, or it declines with a reason naming why.
// What it must NEVER do is report a number it did not obtain.
TEST(EngineBusyReaderTest, EitherReadsOrExplainsItself) {
  EngineBusyReader reader;
  auto r = reader.open("rcs0-busy");
  if (!r.available) {
    EXPECT_FALSE(r.unavailableReason.empty());
    EXPECT_TRUE(r.unavailableReason.contains("perf_event") ||
                r.unavailableReason.contains("i915"))
        << r.unavailableReason.utf8Ptr();
  } else {
    EXPECT_TRUE(r.engineNs.contains("rcs0"));
  }
}

TEST(EngineBusyReaderTest, UnknownEventIsUnavailable) {
  EngineBusyReader reader;
  auto r = reader.open("no-such-event");
  EXPECT_FALSE(r.available);
  EXPECT_FALSE(r.unavailableReason.empty());
}

TEST(EngineBusyReaderTest, SampleBeforeOpenIsUnavailable) {
  EngineBusyReader reader;
  auto r = reader.sample();
  EXPECT_FALSE(r.available);
  EXPECT_FALSE(r.unavailableReason.empty());
}
