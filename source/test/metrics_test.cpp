#include "StarMetricSample.hpp"

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
