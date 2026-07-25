#include "StarTelemetry.hpp"
#include "StarThread.hpp"
#include "StarTime.hpp"
#include "StarLogging.hpp"

#include <atomic>
#include <limits>

namespace Star {

enum class MetricType { Counter, Gauge, Timer, Rate };

struct MetricNode {
  MetricType type;
  // Counter
  std::atomic<uint64_t> counter{0};
  // Gauge
  std::atomic<int64_t> gauge{0};
  // Timer
  std::atomic<uint64_t> count{0};
  std::atomic<int64_t> total{0};
  std::atomic<int64_t> tmin{INT64_MAX};
  std::atomic<int64_t> tmax{INT64_MIN};
  // Rate (owner-thread writes; snapshot best-effort reads)
  std::atomic<double> rate{0.0};
  MetricDesc desc;                    // guarded by Registry::mutex (written at declare, read at snapshot)
  bool declared = false;              // ditto
  std::atomic<bool> descConflict{false};  // two call sites declared the same key differently
  explicit MetricNode(MetricType t) : type(t) {}
};

namespace {
  // Registry storage: a node-based StableHashMap (std::unordered_map). Two independent
  // guarantees keep raw MetricNode* handles valid for the life of the registry: the map's
  // element nodes never move on rehash (node-based), and the heap-allocated MetricNode the
  // unique_ptr owns is address-stable regardless of the map. Either alone suffices for
  // Constraint 3; together they make handle stability unconditional. The mutex guards
  // registration/snapshot/reset only — never the lock-free value-op path.
  struct Registry {
    Mutex mutex;
    StableHashMap<String, std::unique_ptr<MetricNode>> nodes;
    std::atomic<bool> enabled{true};
    std::atomic<bool> deepEnabled{false};

    MetricNode* getOrCreate(String const& key, MetricType type) {
      MutexLocker locker(mutex);
      auto it = nodes.find(key);
      if (it != nodes.end())
        return it->second.get();
      auto node = std::make_unique<MetricNode>(type);
      MetricNode* raw = node.get();
      nodes[key] = std::move(node);
      return raw;
    }

    // First declaration wins. A conflicting second one is a BUG in the call sites, not a runtime condition to
    // paper over: it means two places disagree about what the metric means. Flag it loudly and keep the first
    // so the data stays self-consistent; the telemetry self-test asserts none survive.
    void declare(String const& key, MetricType type, MetricDesc const& desc) {
      MutexLocker locker(mutex);
      auto it = nodes.find(key);
      MetricNode* n;
      if (it == nodes.end()) {
        auto node = std::make_unique<MetricNode>(type);
        n = node.get();
        nodes[key] = std::move(node);
      } else {
        n = it->second.get();
      }
      if (!n->declared) {
        n->desc = desc;
        n->declared = true;
      } else if (n->desc != desc) {
        n->descConflict.store(true, std::memory_order_relaxed);
        Logger::warn("Telemetry: '{}' declared twice with different descriptors -- keeping the first", key);
      }
    }
  };
}

Telemetry::Telemetry() {}

Telemetry& Telemetry::instance() {
  static Telemetry s_telemetry;
  return s_telemetry;
}

// One Registry, function-local static (constructed on first use, before instance()).
static Registry& registry() {
  static Registry s_registry;
  return s_registry;
}

void TelemetryCounter::inc(uint64_t n) {
  if (m_node) m_node->counter.fetch_add(n, std::memory_order_relaxed);
}
uint64_t TelemetryCounter::value() const {
  return m_node ? m_node->counter.load(std::memory_order_relaxed) : 0;
}

void TelemetryGauge::set(int64_t v) {
  if (m_node) m_node->gauge.store(v, std::memory_order_relaxed);
}
void TelemetryGauge::add(int64_t d) {
  if (m_node) m_node->gauge.fetch_add(d, std::memory_order_relaxed);
}
int64_t TelemetryGauge::value() const {
  return m_node ? m_node->gauge.load(std::memory_order_relaxed) : 0;
}

static void atomicMin(std::atomic<int64_t>& a, int64_t v) {
  int64_t cur = a.load(std::memory_order_relaxed);
  while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}
static void atomicMax(std::atomic<int64_t>& a, int64_t v) {
  int64_t cur = a.load(std::memory_order_relaxed);
  while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

void TelemetryTimer::record(int64_t micros) {
  if (!m_node) return;
  m_node->count.fetch_add(1, std::memory_order_relaxed);
  m_node->total.fetch_add(micros, std::memory_order_relaxed);
  atomicMin(m_node->tmin, micros);
  atomicMax(m_node->tmax, micros);
}

void TelemetryRate::set(double r) {
  if (m_node) m_node->rate.store(r, std::memory_order_relaxed);
}

TelemetryScope::TelemetryScope(TelemetryTimer timer)
  : m_timer(timer), m_startMicros(Telemetry::deepEnabled() ? Time::monotonicMicroseconds() : -1) {}
TelemetryScope::~TelemetryScope() {
  if (m_startMicros >= 0)
    m_timer.record(Time::monotonicMicroseconds() - m_startMicros);
}

TelemetryCounter Telemetry::counter(String const& key) {
  return TelemetryCounter(registry().getOrCreate(key, MetricType::Counter));
}
TelemetryGauge Telemetry::gauge(String const& key) {
  return TelemetryGauge(registry().getOrCreate(key, MetricType::Gauge));
}
TelemetryTimer Telemetry::timer(String const& key) {
  return TelemetryTimer(registry().getOrCreate(key, MetricType::Timer));
}
TelemetryRate Telemetry::rate(String const& key) {
  return TelemetryRate(registry().getOrCreate(key, MetricType::Rate));
}

TelemetryCounter Telemetry::counter(String const& key, MetricDesc const& desc) {
  registry().declare(key, MetricType::Counter, desc);
  return counter(key);
}
TelemetryGauge Telemetry::gauge(String const& key, MetricDesc const& desc) {
  registry().declare(key, MetricType::Gauge, desc);
  return gauge(key);
}
TelemetryTimer Telemetry::timer(String const& key, MetricDesc const& desc) {
  registry().declare(key, MetricType::Timer, desc);
  return timer(key);
}
TelemetryRate Telemetry::rate(String const& key, MetricDesc const& desc) {
  registry().declare(key, MetricType::Rate, desc);
  return rate(key);
}

void Telemetry::declare(String const& key, MetricDesc const& desc) {
  // Type is only used when the key does not exist yet; a declare-first key is a Timer by default and is
  // corrected by the first typed accessor call. Every declare() caller in the tree declares a timer.
  registry().declare(key, MetricType::Timer, desc);
}

MetricDesc Telemetry::describe(String const& key) {
  MutexLocker locker(registry().mutex);
  auto it = registry().nodes.find(key);
  return it == registry().nodes.end() ? MetricDesc{} : it->second->desc;
}

bool Telemetry::enabled() { return registry().enabled.load(std::memory_order_relaxed); }
void Telemetry::setEnabled(bool e) { registry().enabled.store(e, std::memory_order_relaxed); }

bool Telemetry::deepEnabled() { return registry().deepEnabled.load(std::memory_order_relaxed); }
void Telemetry::setDeepEnabled(bool e) { registry().deepEnabled.store(e, std::memory_order_relaxed); }

void Telemetry::markTick(String const& threadTag) {
  // A tick thread always passes a stable tag, so cache the seq-counter handle thread-locally:
  // registration (the only mutex-taking path) runs once per thread, then inc() is lock-free —
  // honoring the lock-free-hot-path constraint for this per-tick call. (If a thread ever marked
  // ticks under two different tags it would keep using the first; tick threads don't do that.)
  thread_local TelemetryCounter seq;
  thread_local bool initialized = false;
  if (!initialized) {
    seq = counter(strf("tick.{}.seq", threadTag));
    initialized = true;
  }
  seq.inc();
}

namespace {
  char const* domainName(MetricDomain d) { return d == MetricDomain::Gpu ? "gpu" : "cpu"; }
  char const* ownerName(MetricOwner o) {
    switch (o) {
      case MetricOwner::Frame: return "frame";
      case MetricOwner::Gl: return "gl";
      case MetricOwner::Sim: return "sim";
      case MetricOwner::Lighting: return "lighting";
      case MetricOwner::Process: return "process";
      default: return "unknown";
    }
  }
  char const* cadenceName(MetricCadence c) {
    switch (c) {
      case MetricCadence::Frame: return "frame";
      case MetricCadence::Tick: return "tick";
      case MetricCadence::Recompute: return "recompute";
      default: return "call";
    }
  }
  char const* roleName(MetricRole r) {
    switch (r) {
      case MetricRole::Total: return "total";
      case MetricRole::Budget: return "budget";
      default: return "detail";
    }
  }
  char const* typeName(MetricType t) {
    switch (t) {
      case MetricType::Counter: return "counter";
      case MetricType::Gauge: return "gauge";
      case MetricType::Timer: return "timer";
      default: return "rate";
    }
  }

  // An owner's DENOMINATOR counts its ticks; its TOTAL is the whole that role=budget parts close against.
  // These are different questions and conflating them is how a consumer ends up dividing GPU pass costs by the
  // GPU span's own sample count. For `frame` they are the same metric read two ways (count vs sum); for `gl`
  // they are different metrics entirely -- GPU work is COUNTED per frame but its WHOLE is the GPU frame span.
  // An owner with no total reports its parts unclosed rather than inventing a whole.
  struct OwnerSpec { MetricOwner owner; char const* denominator; char const* total; };
  constexpr OwnerSpec c_ownerSpecs[] = {
    {MetricOwner::Frame,    "cpu.frame.total.us",           "cpu.frame.total.us"},
    {MetricOwner::Gl,       "cpu.frame.total.us",           "render.frame.gpu_span_us"},
    {MetricOwner::Sim,      "tick.server.seq",              nullptr},
    {MetricOwner::Lighting, "lighting.temporal.recomputed", "lighting.cpu.total.us"},
  };
}

Json Telemetry::snapshot() {
  JsonObject metrics;
  MutexLocker locker(registry().mutex);
  for (auto const& pair : registry().nodes) {
    MetricNode* n = pair.second.get();
    JsonObject m{
      {"type", Json(String(typeName(n->type)))},
      {"domain", Json(String(domainName(n->desc.domain)))},
      {"owner", Json(String(ownerName(n->desc.owner)))},
      {"cadence", Json(String(cadenceName(n->desc.cadence)))},
      {"role", Json(String(roleName(n->desc.role)))},
      {"descConflict", Json(n->descConflict.load(std::memory_order_relaxed))}
    };
    if (n->type == MetricType::Counter) {
      m["value"] = Json((uint64_t)n->counter.load(std::memory_order_relaxed));
    } else if (n->type == MetricType::Gauge) {
      m["value"] = Json((int64_t)n->gauge.load(std::memory_order_relaxed));
    } else if (n->type == MetricType::Timer) {
      uint64_t c = n->count.load(std::memory_order_relaxed);
      int64_t tot = n->total.load(std::memory_order_relaxed);
      m["count"] = Json((uint64_t)c);
      m["total"] = Json((int64_t)tot);
      m["mean"] = Json((int64_t)(c ? tot / (int64_t)c : 0));
      m["min"] = Json((int64_t)(c ? n->tmin.load(std::memory_order_relaxed) : 0));
      m["max"] = Json((int64_t)(c ? n->tmax.load(std::memory_order_relaxed) : 0));
    } else {
      m["value"] = Json(n->rate.load(std::memory_order_relaxed));
    }
    metrics[pair.first] = std::move(m);
  }

  JsonObject owners;
  for (auto const& spec : c_ownerSpecs) {
    JsonObject o;
    if (spec.denominator) o["denominator"] = Json(String(spec.denominator));
    if (spec.total) o["total"] = Json(String(spec.total));
    owners[String(ownerName(spec.owner))] = std::move(o);
  }

  return JsonObject{
    {"meta", JsonObject{{"schema", Json((uint64_t)2)}}},
    {"owners", std::move(owners)},
    {"metrics", std::move(metrics)}
  };
}

void Telemetry::reset() {
  MutexLocker locker(registry().mutex);
  for (auto const& pair : registry().nodes) {
    MetricNode* n = pair.second.get();
    n->counter.store(0, std::memory_order_relaxed);
    n->gauge.store(0, std::memory_order_relaxed);
    n->count.store(0, std::memory_order_relaxed);
    n->total.store(0, std::memory_order_relaxed);
    n->tmin.store(INT64_MAX, std::memory_order_relaxed);
    n->tmax.store(INT64_MIN, std::memory_order_relaxed);
    n->rate.store(0.0, std::memory_order_relaxed);
  }
}

}
