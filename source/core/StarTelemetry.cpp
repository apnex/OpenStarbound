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
  // Timer only. Cumulative, so snapshot differencing windows them. NOTE: sum(buckets) == count only when the
  // sampling threads are quiescent (as they are in the unit tests) -- record() bumps count and its bucket
  // with two separate relaxed stores, so a snapshot taken mid-record() on a live, multi-threaded timer can
  // observe them out of order and the two can differ by up to the number of threads in flight, in EITHER
  // direction. That is the same skew `mean` already carries; it is not a bug to chase.
  std::atomic<uint64_t> buckets[Telemetry::HistogramBuckets] = {};
  // Rate (owner-thread writes; snapshot best-effort reads)
  std::atomic<double> rate{0.0};
  // Descriptor state. ALL of the following are guarded by Registry::mutex (written at declare/getOrCreate
  // time, read at snapshot/describe time) -- unlike the value fields above, none of this is on the lock-free
  // hot path, so plain (non-atomic) fields are correct and, unlike std::atomic<bool>, don't mislead a future
  // reader into thinking this is touched outside the lock.
  MetricDesc desc;
  bool declared = false;      // has `desc` been set by a declare() or a typed accessor yet?
  bool descConflict = false;  // two call sites declared this key with different descriptors
  bool typeConflict = false;  // two call sites requested this key as different MetricTypes
  explicit MetricNode(MetricType t) : type(t) {}
};

namespace {
  // First declaration wins. A conflicting second one is a BUG in the call sites, not a runtime condition to
  // paper over: it means two places disagree about what the metric means. Flag it (the caller logs, since
  // this runs under Registry::mutex and must not) and keep the first so the data stays self-consistent.
  //
  // NOTHING ASSERTS THAT NONE SURVIVE. This said the telemetry self-test did; the only test touching the
  // flag asserts a DELIBERATE conflict on a fixture key IS raised, which is the detector working, not the
  // registry being clean. A live conflict reaches a Logger::warn and a snapshot field, and no gate reads
  // either. Stated rather than fixed here because a whole-registry assertion needs a run to assert over.
  //
  // Returns true iff a NEW conflict was just flagged.
  bool applyDesc(MetricNode& n, MetricDesc const& desc) {
    if (!n.declared) {
      n.desc = desc;
      n.declared = true;
      return false;
    } else if (n.desc != desc) {
      n.descConflict = true;
      return true;
    }
    return false;
  }

  // A descriptor declared for a key with no MetricNode yet -- nothing has been sampled, so there is no type
  // to attach it to. Held here until the first counter/gauge/timer/rate call creates the node (with its real
  // MetricType) and adopts it. This is what keeps declare() from ever having to guess a MetricType: guessing
  // (the previous design) silently mistyped any key declared before its first sample, e.g. the planned
  // `declare(key, desc); seq = counter(key);` in markTick would have made a Timer out of what is actually a
  // Counter, and the counter's value would never appear in the snapshot.
  struct PendingDesc {
    MetricDesc desc;
    bool conflict = false;
  };

  // Registry storage: a node-based StableHashMap (std::unordered_map). Two independent
  // guarantees keep raw MetricNode* handles valid for the life of the registry: the map's
  // element nodes never move on rehash (node-based), and the heap-allocated MetricNode the
  // unique_ptr owns is address-stable regardless of the map. Either alone suffices for
  // Constraint 3; together they make handle stability unconditional. The mutex guards
  // registration/snapshot/reset only — never the lock-free value-op path.
  struct Registry {
    Mutex mutex;
    StableHashMap<String, std::unique_ptr<MetricNode>> nodes;
    StableHashMap<String, PendingDesc> pendingDescs;  // declared but not yet sampled; guarded by mutex
    std::atomic<bool> enabled{true};
    std::atomic<bool> deepEnabled{false};

    MetricNode* getOrCreate(String const& key, MetricType type) {
      bool descConflictNow, typeConflictNow;
      MetricNode* n = getOrCreateInner(key, type, MetricDesc{}, /* hasDesc */ false, descConflictNow, typeConflictNow);
      logConflicts(key, descConflictNow, typeConflictNow);
      return n;
    }

    // Combined declare + getOrCreate for the typed accessor overloads (Telemetry::counter(key, desc), etc.):
    // one lock acquisition instead of declare() followed by getOrCreate().
    MetricNode* getOrCreateDeclared(String const& key, MetricType type, MetricDesc const& desc) {
      bool descConflictNow, typeConflictNow;
      MetricNode* n = getOrCreateInner(key, type, desc, /* hasDesc */ true, descConflictNow, typeConflictNow);
      logConflicts(key, descConflictNow, typeConflictNow);
      return n;
    }

    // Declares WITHOUT creating a node -- see PendingDesc above. Does not log; the caller does, after
    // this returns, so Logger::warn never runs while `mutex` is held.
    void declare(String const& key, MetricDesc const& desc) {
      bool conflictNow = false;
      {
        MutexLocker locker(mutex);
        auto it = nodes.find(key);
        if (it != nodes.end()) {
          conflictNow = applyDesc(*it->second, desc);
        } else {
          auto pit = pendingDescs.find(key);
          if (pit == pendingDescs.end()) {
            pendingDescs[key] = PendingDesc{desc, false};
          } else if (pit->second.desc != desc) {
            pit->second.conflict = true;
            conflictNow = true;
          }
        }
      }
      if (conflictNow)
        Logger::warn("Telemetry: '{}' declared twice with different descriptors -- keeping the first", key);
    }

    MetricDesc describe(String const& key) {
      MutexLocker locker(mutex);
      auto it = nodes.find(key);
      if (it != nodes.end())
        return it->second->desc;
      auto pit = pendingDescs.find(key);
      if (pit != pendingDescs.end())
        return pit->second.desc;
      return MetricDesc{};
    }

  private:
    // Shared body of getOrCreate/getOrCreateDeclared. Finds or creates the node (adopting any pending
    // descriptor on creation), then -- if hasDesc -- applies `desc` to it exactly as declare() would to an
    // existing node; a pending descriptor just adopted still counts as "already declared", so a `desc` that
    // disagrees with it is correctly flagged as a conflict rather than silently overwriting it.
    MetricNode* getOrCreateInner(String const& key, MetricType type, MetricDesc const& desc, bool hasDesc,
                                  bool& descConflictNow, bool& typeConflictNow) {
      descConflictNow = false;
      typeConflictNow = false;
      MutexLocker locker(mutex);
      MetricNode* n;
      auto it = nodes.find(key);
      if (it != nodes.end()) {
        n = it->second.get();
        if (n->type != type && !n->typeConflict) {
          n->typeConflict = true;
          typeConflictNow = true;
        }
      } else {
        auto node = std::make_unique<MetricNode>(type);
        n = node.get();
        auto pit = pendingDescs.find(key);
        if (pit != pendingDescs.end()) {
          n->desc = pit->second.desc;
          n->declared = true;
          n->descConflict = pit->second.conflict;
          pendingDescs.erase(pit);
        }
        nodes[key] = std::move(node);
      }
      if (hasDesc)
        descConflictNow = applyDesc(*n, desc);
      return n;
    }

    void logConflicts(String const& key, bool descConflictNow, bool typeConflictNow) {
      if (descConflictNow)
        Logger::warn("Telemetry: '{}' declared twice with different descriptors -- keeping the first", key);
      if (typeConflictNow) {
        Logger::error(
            "Telemetry: '{}' requested as a different metric type than it was first registered as -- keeping the original type",
            key);
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

// __builtin_clzll is GNU/Clang-only; MSVC (real MSVC, not clang-cl -- clang-cl already defines __clang__ and
// takes the builtin path above) has no equivalent, so it gets the intrinsic bit-scan instead. Same split the
// vendored fast_float.h and fmt/format.h make for their own leading-zero counts.
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline int msbIndex(uint64_t v) { unsigned long i; _BitScanReverse64(&i, v); return (int)i; }
#else
static inline int msbIndex(uint64_t v) { return 63 - __builtin_clzll(v); }
#endif

size_t Telemetry::histogramBucket(int64_t micros) {
  if (micros <= 0)
    return 0;
  uint64_t v = (uint64_t)micros;
  int h = msbIndex(v);                          // floor(log2(v)); v > 0 so the bit scan is defined
  if (h >= 16)
    return HistogramBuckets - 1;                // >= 65536us: the "something went very wrong" bucket
  // The two sub-bits BELOW the msb. For h >= 2 they are already there; for h < 2 (v = 1, 2, 3) there are not
  // two bits to take, so shift LEFT to synthesise them. Selecting the branch before shifting matters: a right
  // shift by (h - 2) with h < 2 is a negative shift count and therefore undefined behaviour, not merely wrong.
  uint64_t m = h >= 2 ? ((v >> (h - 2)) & 0x3u) : ((v << (2 - h)) & 0x3u);
  return (size_t)(h * 4 + m);
}

void TelemetryTimer::record(int64_t micros) {
  if (!m_node) return;
  m_node->count.fetch_add(1, std::memory_order_relaxed);
  m_node->total.fetch_add(micros, std::memory_order_relaxed);
  atomicMin(m_node->tmin, micros);
  atomicMax(m_node->tmax, micros);
  m_node->buckets[Telemetry::histogramBucket(micros)].fetch_add(1, std::memory_order_relaxed);
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
  return TelemetryCounter(registry().getOrCreateDeclared(key, MetricType::Counter, desc));
}
TelemetryGauge Telemetry::gauge(String const& key, MetricDesc const& desc) {
  return TelemetryGauge(registry().getOrCreateDeclared(key, MetricType::Gauge, desc));
}
TelemetryTimer Telemetry::timer(String const& key, MetricDesc const& desc) {
  return TelemetryTimer(registry().getOrCreateDeclared(key, MetricType::Timer, desc));
}
TelemetryRate Telemetry::rate(String const& key, MetricDesc const& desc) {
  return TelemetryRate(registry().getOrCreateDeclared(key, MetricType::Rate, desc));
}

void Telemetry::declare(String const& key, MetricDesc const& desc) {
  // Declares WITHOUT creating a node and WITHOUT guessing a MetricType. If `key` has no node yet, the
  // descriptor is held pending (Registry::pendingDescs) until the first counter/gauge/timer/rate call
  // creates the node with its real type and adopts it.
  registry().declare(key, desc);
}

MetricDesc Telemetry::describe(String const& key) {
  return registry().describe(key);
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
    String key = strf("tick.{}.seq", threadTag);
    // The tag names the tick thread; map it to the logical budget that thread drives. An unrecognised tag is
    // left Unknown rather than guessed -- a wrong owner is worse than an absent one.
    MetricOwner owner = threadTag == "server" ? MetricOwner::Sim
                      : threadTag == "client" ? MetricOwner::Frame
                                              : MetricOwner::Unknown;
    declare(key, MetricDesc{MetricDomain::Cpu, owner, MetricCadence::Tick, MetricRole::Detail});
    seq = counter(key);
    initialized = true;
  }
  seq.inc();
}

namespace {
  // These five switches deliberately have NO `default:`. -Wswitch (part of -Wall, on for this project) then
  // flags any enumerator added to MetricDomain/MetricOwner/MetricCadence/MetricRole/MetricType in the future
  // that isn't also added here -- catching a forgotten JSON name at compile time instead of silently
  // serializing the new value as e.g. "unknown"/"call"/"detail"/"counter". The trailing return after each
  // switch exists only to satisfy -Wreturn-type for the technically-reachable (but never actually occurring in
  // correct code) case of an enum value outside its declared set; it is not a substitute for handling a real
  // enumerator.
  char const* domainName(MetricDomain d) {
    switch (d) {
      case MetricDomain::Unknown: return "unknown";
      case MetricDomain::Cpu: return "cpu";
      case MetricDomain::Gpu: return "gpu";
    }
    return "unknown";
  }
  // A TABLE, not a switch -- deliberately the odd one out among the four descriptor namers. The switch this
  // replaced compiled clean with a case missing (-Wswitch warns; this build has no -Werror and the warning
  // drowns), and its trailing fallback returned "unknown", which silently drops the metric out of its
  // owner's budget and under-reports the whole with no signal anywhere. The static_assert below turns
  // "added an owner, forgot to name it" into a COMPILE ERROR. Order must match the enum.
  constexpr char const* c_ownerNames[] = {"unknown", "frame", "gl", "sim", "lighting", "process"};
  static_assert(sizeof(c_ownerNames) / sizeof(*c_ownerNames) == (size_t)MetricOwner::Count,
    "MetricOwner gained a value without a name in c_ownerNames -- add it, in enum order.");

  char const* ownerName(MetricOwner o) {
    auto i = (size_t)o;
    // Defensive, not expected: a descriptor built from a garbage cast. Still names it rather than indexing
    // out of bounds, and "unknown" is the honest answer for a value the model does not define.
    return i < (size_t)MetricOwner::Count ? c_ownerNames[i] : "unknown";
  }
  char const* cadenceName(MetricCadence c) {
    switch (c) {
      case MetricCadence::Call: return "call";
      case MetricCadence::Frame: return "frame";
      case MetricCadence::Tick: return "tick";
      case MetricCadence::Recompute: return "recompute";
    }
    return "call";
  }
  // TABLES WITH A static_assert, following ownerName's precedent rather than domainName's. The switch
  // form's fallback is silent: add an enumerator, forget the case, and the metric reports "undeclared"
  // forever while looking declared. For UNIT that is not a safe fallback the way cadence's "call" is --
  // a nanosecond metric mis-reported as undeclared is read as microseconds by a consumer that hardcodes
  // them, which is the 1000x defect this field exists to end.
  char const* unitName(MetricUnit u) {
    static char const* const names[] = {"undeclared", "ns", "us", "bytes", "kib", "count", "ratio", "hz"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)MetricUnit::Hertz + 1,
                  "MetricUnit gained an enumerator and unitName was not updated");
    return names[(size_t)u];
  }
  char const* clockName(MetricClock c) {
    static char const* const names[] = {"undeclared", "n/a", "wall", "thread_cpu", "process_cpu",
                                        "gpu_engine", "gpu_timeline"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)MetricClock::GpuTimeline + 1,
                  "MetricClock gained an enumerator and clockName was not updated");
    return names[(size_t)c];
  }
  char const* sourceName(MetricSource s) {
    static char const* const names[] = {"undeclared", "in_process", "procfs", "sysfs", "perf_event",
                                        "gl_query"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)MetricSource::GlQuery + 1,
                  "MetricSource gained an enumerator and sourceName was not updated");
    return names[(size_t)s];
  }
  char const* boundednessName(MetricBoundedness b) {
    static char const* const names[] = {"undeclared", "monotonic", "level", "high_water_mark"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)MetricBoundedness::HighWaterMark + 1,
                  "MetricBoundedness gained an enumerator and boundednessName was not updated");
    return names[(size_t)b];
  }

  char const* roleName(MetricRole r) {
    switch (r) {
      case MetricRole::Detail: return "detail";
      case MetricRole::Budget: return "budget";
      case MetricRole::Total: return "total";
    }
    return "detail";
  }
  char const* typeName(MetricType t) {
    switch (t) {
      case MetricType::Counter: return "counter";
      case MetricType::Gauge: return "gauge";
      case MetricType::Timer: return "timer";
      case MetricType::Rate: return "rate";
    }
    return "counter";
  }

  // An owner's DENOMINATOR counts its ticks; its TOTAL is the whole that role=budget parts close against.
  // These are different questions and conflating them is how a consumer ends up dividing GPU pass costs by the
  // GPU span's own sample count. For `frame` they are the same metric read two ways (count vs sum); for `gl`
  // they are different metrics entirely -- GPU work is COUNTED per frame but its WHOLE is the GPU frame span.
  // An owner with no total reports its parts unclosed rather than inventing a whole.
  //
  // Only owners that bear a budget get a row here: Process and Unknown are absent on purpose, meaning "not a
  // budget-bearing owner" -- there is no denominator/total to report for either.
  //
  // All SIX metric keys named below are now REGISTERED and load-bearing (this comment previously said "four"
  // -- while listing five -- and claimed they did not exist yet; both were true when written and neither is
  // true now):
  //   cpu.frame.total.us            StarMainApplication_sdl.cpp
  //   render.frame.gpu_span_us      StarRenderer_opengl.cpp
  //   tick.server.seq               StarWorldServerThread.cpp (markTick at the head of the run() loop body)
  //   tick.server.total.us          StarWorldServerThread.cpp
  //   lighting.temporal.recomputed  StarWorldClient.cpp
  //   lighting.cpu.total.us         StarWorldClient.cpp
  //
  // `sim` HELD A DENOMINATOR AND NO TOTAL until #175: it had parts and no whole, so nothing it measured could
  // be closed or attributed and one phase sat at 98.4% of the four that existed. Note what the total is NOT --
  // it wraps the server loop body minus the pacing sleep, so it is busy with respect to PACING but includes
  // blocked time. Six lock acquisitions live inside it, each named tick.server.lock.*, so a reader wanting
  // pure work computes busy = total - blocked rather than trusting the total to be it.
  // The last two are the denominator and total behind the lighting owner's measured closure, so do not read
  // this table as aspirational. It remains a STATIC DESCRIPTION rather than a lookup -- nothing here resolves
  // a key at runtime, and a typo would silently drop an owner's whole table (telemetry-window.py skips an
  // owner whose denominator windows to zero), which is why the keys are also pinned by unit tests.
  // THE TOTAL IS KEYED BY (OWNER, DOMAIN). The denominator is keyed by owner alone. They answer different
  // questions -- the denominator counts an owner's TICKS, the total is the whole its parts CLOSE AGAINST --
  // and only the second varies by domain.
  //
  // Until this split there was ONE total per owner, while the consumer summed parts PER DOMAIN against it:
  // telemetry-window.py computes `whole` once, then `for dom in ...: parts = sum(... if domain == dom)`.
  // So a cpu-domain Budget metric under owner `gl` would have been divided by render.frame.gpu_span_us and
  // printed as "cpu accounted: N us/tick of <a GPU span>" -- labelled by domain, denominated by another.
  //
  // It never fired because exactly one Cpu/Gl metric exists and it is role=Detail. Making CPU first-class
  // fires it on the first run, which is why this is fixed BEFORE those readers land rather than after they
  // produce a number nobody can trust.
  //
  // A domain with no row here has NO DECLARED WHOLE, which is a legitimate state: owner `gl` has no
  // cpu-domain Budget parts to close. The consumer must SAY SO rather than skip the domain -- an unclosable
  // budget that prints nothing reads exactly like a closed one.
  struct OwnerSpec { MetricOwner owner; char const* denominator; };
  constexpr OwnerSpec c_ownerSpecs[] = {
    {MetricOwner::Frame,    "cpu.frame.total.us"},
    {MetricOwner::Gl,       "cpu.frame.total.us"},
    {MetricOwner::Sim,      "tick.server.seq"},
    {MetricOwner::Lighting, "lighting.temporal.recomputed"},
  };

  // (Gl, Gpu) HAS NO ROW, AND ITS ABSENCE IS THE STATEMENT. It named render.frame.gpu_span_us, which is
  // a GL_TIME_ELAPSED span -- elapsed timeline, not work -- and therefore cannot be a whole anything
  // closes against. Measured: it reports ~16,200us, the frame PERIOD, at 0.22%, 11.39%, 23.39% and
  // 32.93% real engine busy alike, so it is constant across a 1.4x change in the very quantity it was
  // the denominator for.
  //
  // A row here is a CLAIM that the named metric is the owner's whole. Removing it is not losing a
  // number; it is withdrawing a claim that was false. telemetry-window's no-whole branch prints that
  // gl/gpu declares no total, which is a reader learning something true instead of dividing by a
  // constant. Restoring a GPU whole means measuring GPU WORK -- the per-client drm-engine counters or
  // the PMU -- not re-promoting a bracket.
  struct OwnerTotalSpec { MetricOwner owner; MetricDomain domain; char const* total; };
  constexpr OwnerTotalSpec c_ownerTotals[] = {
    {MetricOwner::Frame,    MetricDomain::Cpu, "cpu.frame.total.us"},
    {MetricOwner::Sim,      MetricDomain::Cpu, "tick.server.total.us"},
    {MetricOwner::Lighting, MetricDomain::Cpu, "lighting.cpu.total.us"},
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
      {"unit", Json(String(unitName(n->desc.unit)))},
      {"clock", Json(String(clockName(n->desc.clock)))},
      {"source", Json(String(sourceName(n->desc.source)))},
      {"boundedness", Json(String(boundednessName(n->desc.boundedness)))},
      {"descConflict", Json(n->descConflict)},
      {"typeConflict", Json(n->typeConflict)}
    };
    // ABSENT, not empty. A key that is missing says "nobody declared this"; a key present as "" says
    // "somebody declared nothing", and those are different facts. The ratchet that counts undeclared
    // descriptors has to be able to tell them apart.
    if (n->desc.measures) m["measures"] = Json(String(n->desc.measures));
    if (n->desc.validWhen) m["validWhen"] = Json(String(n->desc.validWhen));
    if (n->desc.whole) m["whole"] = Json(String(n->desc.whole));
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
      // Emitted trimmed of trailing zeros: a 64-entry array per timer, mostly zeros, would triple the
      // snapshot for no information. The consumer zero-pads.
      size_t last = 0;
      for (size_t i = 0; i < HistogramBuckets; ++i)
        if (n->buckets[i].load(std::memory_order_relaxed))
          last = i + 1;
      JsonArray buckets;
      for (size_t i = 0; i < last; ++i)
        buckets.append(Json((uint64_t)n->buckets[i].load(std::memory_order_relaxed)));
      m["buckets"] = Json(std::move(buckets));
    } else if (n->type == MetricType::Rate) {
      m["value"] = Json(n->rate.load(std::memory_order_relaxed));
    }
    metrics[pair.first] = std::move(m);
  }

  JsonObject owners;
  for (auto const& spec : c_ownerSpecs) {
    JsonObject o;
    if (spec.denominator) o["denominator"] = Json(String(spec.denominator));
    JsonObject totals;
    for (auto const& t : c_ownerTotals) {
      if (t.owner == spec.owner && t.total)
        totals[String(domainName(t.domain))] = Json(String(t.total));
    }
    // `totals` is emitted even when EMPTY. An owner with no declared whole in any domain is a real state
    // and the consumer must be able to see it; omitting the key would make "no whole declared" and "an
    // older schema" the same shape on the wire.
    o["totals"] = Json(std::move(totals));
    owners[String(ownerName(spec.owner))] = std::move(o);
  }

  return JsonObject{
    {"meta", JsonObject{{"schema", Json((uint64_t)4)}}},
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
    for (auto& b : n->buckets)
      b.store(0, std::memory_order_relaxed);
    n->rate.store(0.0, std::memory_order_relaxed);
    // Conflict flags are diagnostic state about a measurement WINDOW, like the values above, not about the
    // metric's declared shape (desc/declared are left alone): a conflict flagged before reset() must not
    // haunt every snapshot for the rest of the process.
    n->descConflict = false;
    n->typeConflict = false;
  }
  // A conflict flagged in pendingDescs (declare() called twice with different descriptors for a key with no
  // node yet) is the same kind of window state as descConflict/typeConflict above -- clear it too, or a
  // pre-reset conflict would survive reset() and land on the node the moment it's later created. The pending
  // descriptor itself is left alone, exactly like node desc/declared above: it is structural, not window state.
  for (auto& p : registry().pendingDescs)
    p.second.conflict = false;
}

}
