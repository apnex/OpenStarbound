#ifndef STAR_TELEMETRY_HPP
#define STAR_TELEMETRY_HPP

#include "StarString.hpp"
#include "StarMetricDesc.hpp"
#include "StarJson.hpp"

namespace Star {

struct MetricNode; // defined in the .cpp

// The descriptor vocabulary moved to StarMetricDesc.hpp so the sovereign metrics/ component can
// share it without pulling in this registry. Same types, same meaning, one home.

// Cheap value handles. Hold a stable MetricNode* (see registry storage). Lock-free relaxed ops.
class TelemetryCounter {
public:
  TelemetryCounter() : m_node(nullptr) {}
  explicit TelemetryCounter(MetricNode* node) : m_node(node) {}
  void inc(uint64_t n = 1);
  uint64_t value() const;
private:
  MetricNode* m_node;
};

class TelemetryGauge {
public:
  TelemetryGauge() : m_node(nullptr) {}
  explicit TelemetryGauge(MetricNode* node) : m_node(node) {}
  void set(int64_t v);
  void add(int64_t d);
  int64_t value() const;
private:
  MetricNode* m_node;
};

class TelemetryTimer {
public:
  TelemetryTimer() : m_node(nullptr) {}
  explicit TelemetryTimer(MetricNode* node) : m_node(node) {}
  void record(int64_t micros);
private:
  MetricNode* m_node;
  friend class TelemetryScope;
};

class TelemetryRate {
public:
  TelemetryRate() : m_node(nullptr) {}
  explicit TelemetryRate(MetricNode* node) : m_node(node) {}
  void set(double rate);
private:
  MetricNode* m_node;
};

// RAII timer. Reads the clock and records ONLY when Telemetry::deepEnabled() at construction.
class TelemetryScope {
public:
  explicit TelemetryScope(TelemetryTimer timer);
  ~TelemetryScope();
  TelemetryScope(TelemetryScope const&) = delete;
  TelemetryScope& operator=(TelemetryScope const&) = delete;
private:
  TelemetryTimer m_timer;
  int64_t m_startMicros; // -1 == not timing
};

class Telemetry {
public:
  // Idempotent registration/lookup by dotted key. First call registers (brief mutex).
  static TelemetryCounter counter(String const& key);
  static TelemetryGauge gauge(String const& key);
  static TelemetryTimer timer(String const& key);
  static TelemetryRate rate(String const& key);

  // Same, but also declaring what the metric IS. Prefer these at static registration sites.
  static TelemetryCounter counter(String const& key, MetricDesc const& desc);
  static TelemetryGauge gauge(String const& key, MetricDesc const& desc);
  static TelemetryTimer timer(String const& key, MetricDesc const& desc);
  static TelemetryRate rate(String const& key, MetricDesc const& desc);

  // Declare without taking a handle. For keys whose VALUE is recorded somewhere other than where their
  // meaning is known -- GPU pass timers are begun by the render passes but recorded generically inside
  // OpenGlRenderer, and markTick() builds its key with strf.
  static void declare(String const& key, MetricDesc const& desc);
  // Off the hot path: takes the registry mutex (every other Telemetry read accessor is lock-free relaxed).
  static MetricDesc describe(String const& key);

  // Master cheap-counter gate (default true). Instrumentation may check this to skip work,
  // but inc/set are already nearly free, so checking is optional.
  static bool enabled();
  static void setEnabled(bool e);

  static bool deepEnabled();          // opt-in timer gate (default false)
  static void setDeepEnabled(bool e);

  static void markTick(String const& threadTag); // bumps tick.<threadTag>.seq

  // Timers carry a histogram so the TAIL is visible, not just the mean. Every hard bug in this project has
  // been episodic -- flicker, hitching -- and a lever that improves mean frame time while doubling p99 reads
  // as a clean win against means alone. Buckets are cumulative counters, so differencing two snapshots windows
  // them, which is what makes p99 and a windowed max exist at all (the `max` field is a run-long high-water
  // mark and is NOT windowable).
  //
  // 64 buckets, HdrHistogram-style: bucket i covers [2^h * (1 + m/4), 2^h * (1 + (m+1)/4)) for h = i/4,
  // m = i%4 -- EXCEPT at both ends, where the formula is clamped rather than literal:
  //   - bucket 0 also absorbs 0 and negative input (a 0us sample is real sub-microsecond work; a negative
  //     delta is nonsense but must not index out of bounds), not just its literal [1, 1.25) range.
  //   - bucket 63 is UNBOUNDED above -- [57344, +inf), not [57344, 65536). A consumer computing a percentile
  //     as a bucket midpoint gets a meaningless number the moment p99 lands here, which is exactly the hitch
  //     case this feature exists to catch, so treat bucket 63 as "at least this slow", never as a point value.
  // Buckets 1, 2, 3, 5 and 7 are structurally unreachable (their ranges -- e.g. [1.25, 1.5) -- contain no
  // integer microsecond value) and will read zero forever; that is expected, not a bug to go hunting for.
  // Integer-only (one bit scan, a shift and a mask): this runs on the hot path, so no floating point.
  //
  // sum(buckets) == count exactly only when the sampling threads are quiescent at snapshot time (true in the
  // unit tests). record() bumps count and its bucket with two independent relaxed stores, so a snapshot taken
  // mid-record() on a live, multi-threaded timer can observe them out of order; the two may then differ by up
  // to the number of threads in flight, in EITHER direction. That skew already exists in `mean` today -- it
  // is not new here, just newly visible as two numbers that can disagree.
  static constexpr size_t HistogramBuckets = 64;
  static size_t histogramBucket(int64_t micros);

  // Read-only full metric tree (schema v2): {"meta": {"schema": 2}, "owners": {...}, "metrics": {key: {...}}}.
  // "owners" is a static description of each owner's denominator/total metric keys. "metrics" is a flat map
  // from dotted key to a per-metric object carrying its type, its declared descriptor (domain/owner/cadence/
  // role), any descConflict/typeConflict flag, and its type-specific value fields -- Timer additionally
  // carries "buckets" (histogramBucket() above): cumulative counts, trailing zeros trimmed, consumer
  // zero-pads to HistogramBuckets. Purely additive; schema stays at 2.
  static Json snapshot();

  // Zero all metric values (tests / a measurement window). Off the hot path only.
  static void reset();

private:
  Telemetry();
  static Telemetry& instance();
};

}

#endif
