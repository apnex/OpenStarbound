#include "StarTelemetry.hpp"
#include "StarThread.hpp"
#include "StarTime.hpp"

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

bool Telemetry::enabled() { return registry().enabled.load(std::memory_order_relaxed); }
void Telemetry::setEnabled(bool e) { registry().enabled.store(e, std::memory_order_relaxed); }

bool Telemetry::deepEnabled() { return registry().deepEnabled.load(std::memory_order_relaxed); }
void Telemetry::setDeepEnabled(bool e) { registry().deepEnabled.store(e, std::memory_order_relaxed); }

void Telemetry::markTick(String const& threadTag) {
  counter(strf("tick.{}.seq", threadTag)).inc();
}

Json Telemetry::snapshot() {
  JsonObject counters;
  JsonObject gauges;
  JsonObject timers;
  JsonObject rates;
  MutexLocker locker(registry().mutex);
  for (auto const& pair : registry().nodes) {
    MetricNode* n = pair.second.get();
    if (n->type == MetricType::Counter) {
      counters[pair.first] = Json((uint64_t)n->counter.load(std::memory_order_relaxed));
    } else if (n->type == MetricType::Gauge) {
      gauges[pair.first] = Json((int64_t)n->gauge.load(std::memory_order_relaxed));
    } else if (n->type == MetricType::Timer) {
      uint64_t c = n->count.load(std::memory_order_relaxed);
      int64_t tot = n->total.load(std::memory_order_relaxed);
      int64_t mn = c ? n->tmin.load(std::memory_order_relaxed) : 0;
      int64_t mx = c ? n->tmax.load(std::memory_order_relaxed) : 0;
      timers[pair.first] = JsonObject{
        {"count", Json((uint64_t)c)}, {"total", Json((int64_t)tot)},
        {"mean", Json((int64_t)(c ? tot / (int64_t)c : 0))},
        {"min", Json((int64_t)mn)}, {"max", Json((int64_t)mx)}
      };
    } else if (n->type == MetricType::Rate) {
      rates[pair.first] = Json(n->rate.load(std::memory_order_relaxed));
    }
  }
  return JsonObject{
    {"counters", std::move(counters)},
    {"gauges", std::move(gauges)},
    {"timers", std::move(timers)},
    {"rates", std::move(rates)}
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
