#ifndef STAR_TELEMETRY_HPP
#define STAR_TELEMETRY_HPP

#include "StarString.hpp"
#include "StarJson.hpp"

namespace Star {

struct MetricNode; // defined in the .cpp

// WHAT A METRIC IS, declared once at registration and never inferred at sample time.
//
// Inference is not merely inconvenient here, it is WRONG: GPU query results are read back and recorded by the
// MAIN thread several frames after the GPU did the work -- see OpenGlRenderer::GlGpuTimer, which polls a
// rotating query ring via glGetQueryObjectuiv(GL_QUERY_RESULT_AVAILABLE). Stamping the recording thread, the
// obvious design, would therefore label every GPU sample as CPU/main. Declaration is both correct and
// cheaper, costing nothing on the sampling path.
enum class MetricDomain : uint8_t { Unknown, Cpu, Gpu };

// The LOGICAL budget a sample belongs to -- deliberately not an OS thread. WorldClient::lightingCalc() runs on
// its own thread or inline on the main thread depending on WorldClient::m_asyncLighting (set by
// setAsyncLighting); it belongs to the `Lighting` budget either way. "Which budget does this cost land in"
// was always the question; "which thread ran it" never was.
// Count is a SENTINEL, not an owner. It exists so ownerName() can be a table with a static_assert on its
// size, which makes "added an owner, forgot to name it" a COMPILE ERROR rather than a silent one. The
// switch it replaced compiled clean with a case missing: -Wswitch warns, but this build has no -Werror and
// the warning drowns in the output. That mattered more here than for the other three descriptor enums,
// because ownerName's fallback was "unknown" -- which silently drops the metric out of its owner's budget
// and quietly under-reports the whole. (domain/cadence/role keep their switches on purpose: their
// fallbacks are deliberately SAFE -- cadence falls back to "call", which is unscaled and cannot inflate,
// and role to "detail", which is excluded from sums and so under-counts rather than over-counts.)
// Keep Count last; nothing may be added after it.
enum class MetricOwner : uint8_t { Unknown, Frame, Gl, Sim, Lighting, Process, Count };

// Which of the owner's tick counters this metric's count is checked against. NOT used to compute per-frame
// cost -- that is always total / frames. Cadence exists so a gated pass sampling 60% of frames is reported as
// 60% COVERAGE rather than mistaken for a metric that is 40% broken.
enum class MetricCadence : uint8_t { Call, Frame, Tick, Recompute };

// Whether this metric is a whole, a part of a whole, or neither.
//   Total  -- IS the owner's whole; excluded from the sum of parts.
//   Budget -- a part; sums with its siblings and must close against the owner's Total.
//   Detail -- nested inside a Budget part; never summed. render.frame.us and render.interface.us both nest
//             inside cpu.frame.render.us, so summing all three would double-count.
enum class MetricRole : uint8_t { Detail, Budget, Total };

struct MetricDesc {
  MetricDomain domain = MetricDomain::Unknown;
  MetricOwner owner = MetricOwner::Unknown;
  MetricCadence cadence = MetricCadence::Call;
  MetricRole role = MetricRole::Detail;
};

inline bool operator==(MetricDesc const& a, MetricDesc const& b) {
  return a.domain == b.domain && a.owner == b.owner && a.cadence == b.cadence && a.role == b.role;
}
inline bool operator!=(MetricDesc const& a, MetricDesc const& b) { return !(a == b); }

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
