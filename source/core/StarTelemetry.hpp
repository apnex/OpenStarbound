#ifndef STAR_TELEMETRY_HPP
#define STAR_TELEMETRY_HPP

#include "StarString.hpp"
#include "StarJson.hpp"

namespace Star {

struct MetricNode; // defined in the .cpp

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

  // Master cheap-counter gate (default true). Instrumentation may check this to skip work,
  // but inc/set are already nearly free, so checking is optional.
  static bool enabled();
  static void setEnabled(bool e);

  static bool deepEnabled();          // opt-in timer gate (default false)
  static void setDeepEnabled(bool e);

  static void markTick(String const& threadTag); // bumps tick.<threadTag>.seq

  // Read-only full metric tree. Buckets: "counters", "gauges", "timers", "rates".
  static Json snapshot();

  // Zero all metric values (tests / a measurement window). Off the hot path only.
  static void reset();

private:
  Telemetry();
  static Telemetry& instance();
};

}

#endif
