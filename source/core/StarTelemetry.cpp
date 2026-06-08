#include "StarTelemetry.hpp"
#include "StarThread.hpp"

#include <atomic>

namespace Star {

enum class MetricType { Counter, Gauge, Timer, Rate };

struct MetricNode {
  MetricType type;
  // Counter
  std::atomic<uint64_t> counter{0};
  // Gauge
  std::atomic<int64_t> gauge{0};
  // (Timer/Rate fields added in Task 2)
  explicit MetricNode(MetricType t) : type(t) {}
};

namespace {
  // Registry storage: a node-based StableHashMap (std::unordered_map) — its nodes never
  // move on rehash, and the heap-allocated MetricNode pointees the unique_ptrs own are
  // address-stable regardless, so raw MetricNode* handles stay valid. (Star's flat HashMap
  // cannot hold move-only unique_ptr values — its bucket vector reallocation falls back to
  // the value copy ctor — so StableHashMap is the correct substrate here.) The mutex guards
  // registration/snapshot/reset only.
  struct Registry {
    Mutex mutex;
    StableHashMap<String, std::unique_ptr<MetricNode>> nodes;
    std::atomic<bool> enabled{true};

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

TelemetryCounter Telemetry::counter(String const& key) {
  return TelemetryCounter(registry().getOrCreate(key, MetricType::Counter));
}
TelemetryGauge Telemetry::gauge(String const& key) {
  return TelemetryGauge(registry().getOrCreate(key, MetricType::Gauge));
}

bool Telemetry::enabled() { return registry().enabled.load(std::memory_order_relaxed); }
void Telemetry::setEnabled(bool e) { registry().enabled.store(e, std::memory_order_relaxed); }

Json Telemetry::snapshot() {
  JsonObject counters;
  JsonObject gauges;
  MutexLocker locker(registry().mutex);
  for (auto const& pair : registry().nodes) {
    MetricNode* n = pair.second.get();
    if (n->type == MetricType::Counter)
      counters[pair.first] = Json((uint64_t)n->counter.load(std::memory_order_relaxed));
    else if (n->type == MetricType::Gauge)
      gauges[pair.first] = Json((int64_t)n->gauge.load(std::memory_order_relaxed));
  }
  return JsonObject{
    {"counters", std::move(counters)},
    {"gauges", std::move(gauges)}
  };
}

void Telemetry::reset() {
  MutexLocker locker(registry().mutex);
  for (auto const& pair : registry().nodes) {
    MetricNode* n = pair.second.get();
    n->counter.store(0, std::memory_order_relaxed);
    n->gauge.store(0, std::memory_order_relaxed);
  }
}

}
