#include "StarClientBusyReader.hpp"
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
