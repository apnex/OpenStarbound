#pragma once

#include "StarException.hpp"
#include "StarString.hpp"

namespace Star {

STAR_EXCEPTION(MetricsException, StarException);

// A reading that cannot be written without saying what it IS and when it is that.
//
// This shape exists because render.pass.parallax.gpu_us was named "GPU microseconds of the parallax
// pass" and meant "elapsed span of a bracket that may contain no work, sampled through a
// magnitude-biased filter". The gap between the name and the meaning was the defect, and no part of
// the old model was capable of holding it. `measures` and `validWhen` are constructor arguments, so
// the gap cannot be reintroduced by omission -- only by writing something false on purpose.
struct MetricSample {
  MetricSample(String key_, double value_, String unit_, String measures_, String validWhen_,
               String source_, int64_t tMonotonicNs_)
    : key(std::move(key_)), value(value_), unit(std::move(unit_)), measures(std::move(measures_)),
      validWhen(std::move(validWhen_)), source(std::move(source_)), tMonotonicNs(tMonotonicNs_) {
    if (measures.empty())
      throw MetricsException::format("MetricSample '{}' has no `measures`: a number without a "
                                     "physical quantity is not a measurement", key);
    if (validWhen.empty())
      throw MetricsException::format("MetricSample '{}' has no `validWhen`: a number valid under no "
                                     "stated condition cannot be checked", key);
  }

  String key;
  double value;
  String unit;          // "ns", "ratio", "hz"
  String measures;      // the physical quantity, in words
  String validWhen;     // the condition under which the value IS that quantity
  String source;        // "fdinfo:drm-engine-render", "i915-pmu:rcs0-busy"
  int64_t tMonotonicNs;
};

}
