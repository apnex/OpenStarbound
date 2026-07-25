# Unified CPU + GPU Telemetry Model — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give CPU the same first-class, self-describing, self-validating telemetry the GPU already has, so a render lever's cost on both resources can be A/B'd unattended and its effect on the frame-time *tail* — not just the mean — can be seen.

**Architecture:** Every metric declares four fields at **registration** (`domain`, `owner`, `cadence`, `role`), never inferred at sample time — GPU results are recorded by the main thread ~3 frames late, so inference would mislabel them. Owners declare a denominator and a total, making cross-thread and cross-domain sums unrepresentable. Timers gain 64-bucket HdrHistogram-style histograms so percentiles are windowable. The main frame loop gains a closed CPU budget. A self-test asserts closure, cadence and histogram consistency.

**Tech Stack:** C++ (clang, `build/linux-release-clang`), gtest (`core_tests`), Python 3 (`scripts/telemetry-window.py`), SDL3/OpenGL.

**Spec:** `docs/superpowers/specs/2026-07-25-unified-telemetry-model-design.md`

---

## Before you start

**Read the spec first.** This plan implements it; the spec explains *why* each field exists, and every "why"
here is a compression of it.

**Environment rules that are not optional in this repo:**

- **The Director must be OUT of the game before any build.** Check first:
  ```bash
  pgrep -a -i starbound   # must print nothing
  ```
- **All builds and profile runs are E-core pinned**, or the machine thermally throttles and the numbers lie:
  `taskset -c 6-15 nice -n 19`.
- **Never `git add -A`.** The `.gitignore` here has swept a 314 MB harness and a player save into a
  public-bound branch before. Add files explicitly, always.
- **Use the Edit tool for file changes, not `sed`/`perl`.**
- `mv`/`cp` are shell-aliased to interactive `-i`. Use `command mv` / `command cp` in scripts.

**Standard build command** (used in every task; adjust `--target` per task):

```bash
cd /root/frackin/OpenStarbound && \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
  cmake --build build/linux-release-clang --target core_tests -j 8
```

**Standard unit-test command:**

```bash
cd /root/frackin/OpenStarbound && taskset -c 6-15 dist/core_tests --gtest_filter='Telemetry.*'
```

**The non-regression guard.** Telemetry is non-functional: it must not change a single pixel. After every
stage that touches rendering or the frame loop:

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-gate.sh
```
Expected: `GATE: PASS`, with `envoracle`/`paralloracle`/`spreadoracle` each showing a nonzero pass count and
`DIFF=0`, and `GL_INVALID lines: 0`. **A pixel difference means the instrumentation perturbed rendering — stop
and fix it, do not proceed.**

---

## File structure

| File | Responsibility | Change |
|---|---|---|
| `source/core/StarTelemetry.hpp` | Public metric API: handles, `MetricDesc`, declaration entry points | Modify — add descriptor types, overloads, `declare()`, histogram accessor |
| `source/core/StarTelemetry.cpp` | Registry, lock-free value ops, snapshot serialisation, owner table | Modify — descriptor storage, histogram, schema-v2 snapshot, owner specs |
| `source/core/StarTelemetryReporter.cpp` | Writes the snapshot JSON, samples environment facts into `meta` | Modify — add `meta` (vsync/clock/temp) and refresh `cpu.process.total_us` |
| `source/core/StarTelemetryReporter.hpp` | Reporter interface | Modify — `writeSnapshot` gains an optional `meta` argument |
| `source/application/StarMainApplication_sdl.cpp` | The frame loop | Modify — the seven `cpu.frame.*` budget scopes + `cpu.frame.updates` |
| `source/application/StarRenderer_opengl.cpp` | GPU timer readback → Telemetry | Modify — declare GPU metrics; unchanged record path |
| `source/client/StarClientApplication.cpp` | Client render/update; existing render timers | Modify — declarations + `cpu.wait.lighting.us` |
| ~10 further `source/**` files | Existing metric registration sites | Modify — one-line declarations only |
| `source/test/telemetry_test.cpp` | The instrument's oracle | Modify — descriptor, histogram, closure, cadence tests |
| `scripts/telemetry-window.py` | Snapshot consumer: windows, closes, reports percentiles | Rewrite — schema v2, owner-aware, percentile columns |
| `scripts/render-profile.sh` | Live profile runner | Modify — pass vsync state through to `meta` |

**Not touched:** `source/frontend/StarClientCommandProcessor.cpp` (the `/telemetry` HUD reads counters by name,
not the snapshot) and the `/debug` `LogMap` HUD.

---

## Stage A — the descriptor model

### Task 1: MetricDesc types and the declaration API

**Files:**
- Modify: `source/core/StarTelemetry.hpp`
- Modify: `source/core/StarTelemetry.cpp`
- Test: `source/test/telemetry_test.cpp`

- [ ] **Step 1: Write the failing test**

Append to `source/test/telemetry_test.cpp`:

```cpp
TEST(Telemetry, DeclareAttachesDescriptorAndIsIdempotent) {
  telemetrySetUp();
  MetricDesc d{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::declare("test.desc.a", d);
  // Re-declaring with the SAME descriptor is a no-op, not an error: a metric registered from three
  // call sites (render.drawable.parts.rebuilt is registered from three) must declare consistently.
  Telemetry::declare("test.desc.a", d);
  EXPECT_EQ(Telemetry::describe("test.desc.a"), d);
}

TEST(Telemetry, FirstDeclarationWinsAndMismatchIsRecorded) {
  telemetrySetUp();
  MetricDesc gpu{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget};
  MetricDesc cpu{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::declare("test.desc.b", gpu);
  Telemetry::declare("test.desc.b", cpu);   // conflicting -- first wins, conflict is recorded
  EXPECT_EQ(Telemetry::describe("test.desc.b"), gpu);
  EXPECT_TRUE(Telemetry::snapshot().getObject("metrics").getObject("test.desc.b").getBool("descConflict"));
}

TEST(Telemetry, UndeclaredMetricIsOwnerUnknown) {
  telemetrySetUp();
  Telemetry::counter("test.desc.undeclared").inc();
  EXPECT_EQ(Telemetry::describe("test.desc.undeclared").owner, MetricOwner::Unknown);
}

TEST(Telemetry, TypedAccessorOverloadDeclaresInline) {
  telemetrySetUp();
  MetricDesc d{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  Telemetry::timer("test.desc.c", d).record(10);
  EXPECT_EQ(Telemetry::describe("test.desc.c"), d);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /root/frackin/OpenStarbound && \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8
```
Expected: **compile error** — `MetricDesc`, `MetricDomain`, `MetricOwner`, `MetricCadence`, `MetricRole`,
`Telemetry::declare` and `Telemetry::describe` do not exist.

- [ ] **Step 3: Add the descriptor types to the header**

In `source/core/StarTelemetry.hpp`, immediately after `struct MetricNode; // defined in the .cpp`:

```cpp
// WHAT A METRIC IS, declared once at registration and never inferred at sample time.
//
// Inference is not merely inconvenient here, it is WRONG: GPU query results are read back and recorded by the
// MAIN thread roughly three frames after the GPU did the work (StarRenderer_opengl.cpp:1050-1067), so stamping
// the recording thread -- the obvious design -- would label every GPU sample as CPU/main. Declaration is both
// correct and cheaper, costing nothing on the sampling path.
enum class MetricDomain : uint8_t { Cpu, Gpu };

// The LOGICAL budget a sample belongs to -- deliberately not an OS thread. WorldClient::lightingCalc() runs on
// its own thread or inline on the main thread depending on m_asyncLighting (StarWorldClient.cpp:61,574); it
// belongs to the `Lighting` budget either way. "Which budget does this cost land in" was always the question;
// "which thread ran it" never was.
enum class MetricOwner : uint8_t { Unknown, Frame, Gl, Sim, Lighting, Process };

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
  MetricDomain domain = MetricDomain::Cpu;
  MetricOwner owner = MetricOwner::Unknown;
  MetricCadence cadence = MetricCadence::Call;
  MetricRole role = MetricRole::Detail;
};

inline bool operator==(MetricDesc const& a, MetricDesc const& b) {
  return a.domain == b.domain && a.owner == b.owner && a.cadence == b.cadence && a.role == b.role;
}
inline bool operator!=(MetricDesc const& a, MetricDesc const& b) { return !(a == b); }
```

- [ ] **Step 4: Add the declaration API to the Telemetry class**

In `source/core/StarTelemetry.hpp`, replace the four accessor declarations inside `class Telemetry`:

```cpp
  // Idempotent registration/lookup by dotted key. First call registers (brief mutex).
  static TelemetryCounter counter(String const& key);
  static TelemetryGauge gauge(String const& key);
  static TelemetryTimer timer(String const& key);
  static TelemetryRate rate(String const& key);
```

with:

```cpp
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
  static MetricDesc describe(String const& key);
```

- [ ] **Step 5: Store the descriptor on the node**

In `source/core/StarTelemetry.cpp`, in `struct MetricNode`, after `MetricType type;`:

```cpp
  MetricDesc desc;                    // guarded by Registry::mutex (written at declare, read at snapshot)
  bool declared = false;              // ditto
  std::atomic<bool> descConflict{false};  // two call sites declared the same key differently
```

- [ ] **Step 6: Implement declaration and the overloads**

In `source/core/StarTelemetry.cpp`, add to `struct Registry` after `getOrCreate`:

```cpp
    // First declaration wins. A conflicting second one is a BUG in the call sites, not a runtime condition to
    // paper over: it means two places disagree about what the metric means. Flag it loudly and keep the first
    // so the data stays self-consistent; TelemetryDescriptorsAreConsistent asserts none survive.
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
```

Add `#include "StarLogging.hpp"` to the includes at the top of `source/core/StarTelemetry.cpp`.

Then add the public entry points, after the existing four accessors:

```cpp
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
```

- [ ] **Step 7: Emit the descriptor in the snapshot (schema v2)**

In `source/core/StarTelemetry.cpp`, replace the whole body of `Json Telemetry::snapshot()` with:

```cpp
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
```

- [ ] **Step 8: Fix the pre-existing tests that read the old schema**

Four existing tests read `counters`/`gauges`/`timers`. Update them in `source/test/telemetry_test.cpp`:

```cpp
TEST(Telemetry, SnapshotEmitsCountersAndGaugesBuckets) {
  telemetrySetUp();
  Telemetry::counter("test.counter.x").inc(7);
  Telemetry::gauge("test.gauge.y").set(42);
  Json snap = Telemetry::snapshot();
  EXPECT_EQ(snap.getObject("metrics").getObject("test.counter.x").getUInt("value"), 7u);
  EXPECT_EQ(snap.getObject("metrics").getObject("test.gauge.y").getInt("value"), 42);
  EXPECT_EQ(snap.getObject("meta").getUInt("schema"), 2u);
}
```

In `TimerRecordsCountTotalMinMax`, replace
`Json tj = snap.getObject("timers").get("test.timer");` with
`Json tj = snap.getObject("metrics").get("test.timer");`

In `DeepScopeRecordsOnlyWhenDeepEnabled`, replace both
`Telemetry::snapshot().getObject("timers").get("test.deep")` with
`Telemetry::snapshot().getObject("metrics").get("test.deep")`

In `ReporterWritesSnapshotJsonFile`, replace
`read.getObject("counters").get("test.report.c").toUInt()` with
`read.getObject("metrics").getObject("test.report.c").getUInt("value")`

- [ ] **Step 9: Build and run the tests**

```bash
cd /root/frackin/OpenStarbound && \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 && \
taskset -c 6-15 dist/core_tests --gtest_filter='Telemetry.*'
```
Expected: all `Telemetry.*` tests PASS, including the four new ones.

- [ ] **Step 10: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/core/StarTelemetry.hpp source/core/StarTelemetry.cpp source/test/telemetry_test.cpp && \
git commit -m "telemetry: declare what a metric IS at registration (schema v2)

Four fields -- domain, owner, cadence, role -- declared once when a metric is
registered, never inferred at sample time. Inference would be wrong, not merely
awkward: GPU query results are recorded by the MAIN thread ~3 frames after the GPU
did the work, so stamping the recording thread labels every GPU sample as CPU.

Owners declare a denominator (what counts their ticks) AND a total (the whole that
role=budget parts close against). For 'frame' those are one metric read two ways;
for 'gl' they are different metrics -- GPU work is counted per frame but its whole
is the GPU frame span. Conflating them is what produced a parts-sum of 119%.

First declaration wins; a conflicting one is flagged rather than silently merged."
```

---

### Task 2: Histograms

**Files:**
- Modify: `source/core/StarTelemetry.hpp`
- Modify: `source/core/StarTelemetry.cpp`
- Test: `source/test/telemetry_test.cpp`

- [ ] **Step 1: Write the failing test**

Append to `source/test/telemetry_test.cpp`:

```cpp
TEST(Telemetry, HistogramBucketBoundaries) {
  // Bucket i covers [2^h * (1 + m/4), 2^h * (1 + (m+1)/4)) for h = i/4, m = i%4.
  // h is floor(log2(v)) and m is the two bits below the MSB, so the index is exact and integer-only.
  EXPECT_EQ(Telemetry::histogramBucket(0), 0u);     // clamped: a 0us sample is real (sub-microsecond work)
  EXPECT_EQ(Telemetry::histogramBucket(1), 0u);     // 2^0 * 1.00
  EXPECT_EQ(Telemetry::histogramBucket(4), 8u);     // h=2, m=0 -> 4*2+0
  EXPECT_EQ(Telemetry::histogramBucket(5), 9u);     // h=2, m=1  (5 = 0b101)
  EXPECT_EQ(Telemetry::histogramBucket(6), 10u);    // h=2, m=2  (6 = 0b110)
  EXPECT_EQ(Telemetry::histogramBucket(7), 11u);    // h=2, m=3  (7 = 0b111)
  EXPECT_EQ(Telemetry::histogramBucket(8), 12u);    // h=3, m=0
  EXPECT_EQ(Telemetry::histogramBucket(65535), 63u);   // top of range
  EXPECT_EQ(Telemetry::histogramBucket(1000000), 63u); // a 1-second frame clamps into the top bucket
  EXPECT_EQ(Telemetry::histogramBucket(-5), 0u);       // a negative delta is nonsense; do not index OOB
}

TEST(Telemetry, TimerFillsHistogramAndSumMatchesCount) {
  telemetrySetUp();
  auto t = Telemetry::timer("test.hist");
  for (int64_t v : {1, 4, 4, 5, 8, 100, 100000})
    t.record(v);
  Json m = Telemetry::snapshot().getObject("metrics").getObject("test.hist");
  JsonArray buckets = m.getArray("buckets");
  uint64_t sum = 0;
  for (auto const& b : buckets)
    sum += b.toUInt();
  // The histogram is the instrument's own checksum: if it disagrees with count, a sample was lost or
  // double-counted somewhere between record() and the snapshot.
  EXPECT_EQ(sum, m.getUInt("count"));
  EXPECT_EQ(sum, 7u);
  EXPECT_EQ(buckets.at(8).toUInt(), 2u);   // the two 4us samples
  EXPECT_EQ(buckets.at(63).toUInt(), 1u);  // the 100ms sample
}

TEST(Telemetry, HistogramIsEmptyForCounters) {
  telemetrySetUp();
  Telemetry::counter("test.hist.counter").inc();
  EXPECT_FALSE(Telemetry::snapshot().getObject("metrics").getObject("test.hist.counter").contains("buckets"));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /root/frackin/OpenStarbound && \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8
```
Expected: **compile error** — `Telemetry::histogramBucket` does not exist.

- [ ] **Step 3: Declare the histogram constants and helper**

In `source/core/StarTelemetry.hpp`, inside `class Telemetry`'s public section, after `static void markTick(...)`:

```cpp
  // Timers carry a histogram so the TAIL is visible, not just the mean. Every hard bug in this project has
  // been episodic -- flicker, hitching -- and a lever that improves mean frame time while doubling p99 reads
  // as a clean win against means alone. Buckets are cumulative counters, so differencing two snapshots windows
  // them, which is what makes p99 and a windowed max exist at all (the `max` field is a run-long high-water
  // mark and is NOT windowable).
  //
  // 64 buckets, HdrHistogram-style: bucket i covers [2^h * (1 + m/4), 2^h * (1 + (m+1)/4)) for h = i/4,
  // m = i%4. That spans 1us..65535us -- a cheap pass through a visible hitch. Integer-only (one clz, a shift
  // and a mask): this runs on the hot path, so no floating point.
  static constexpr size_t HistogramBuckets = 64;
  static size_t histogramBucket(int64_t micros);
```

- [ ] **Step 4: Implement the bucket function and per-node storage**

In `source/core/StarTelemetry.cpp`, in `struct MetricNode`, after the timer fields:

```cpp
  // Timer only. Cumulative, so snapshot differencing windows them.
  std::atomic<uint64_t> buckets[Telemetry::HistogramBuckets];
```

and in the `MetricNode` constructor body:

```cpp
  explicit MetricNode(MetricType t) : type(t) {
    for (auto& b : buckets)
      b.store(0, std::memory_order_relaxed);
  }
```

Add the bucket function, above `void TelemetryTimer::record`:

```cpp
size_t Telemetry::histogramBucket(int64_t micros) {
  if (micros <= 1)
    return 0;
  uint64_t v = (uint64_t)micros;
  int h = 63 - __builtin_clzll(v);              // floor(log2(v))
  if (h >= 16)
    return HistogramBuckets - 1;                // >= 65536us: the "something went very wrong" bucket
  uint64_t m = (v >> (h - 2)) & 0x3u;           // the two bits below the MSB; h >= 1 here, and h >= 2
  if (h < 2)                                    // 2us and 3us: no room for two sub-bits below the MSB
    m = (v >> h) & 0x3u;
  return (size_t)(h * 4 + m);
}
```

**Note on `h < 2`:** for `v = 2` (h=1) and `v = 3` (h=1) there is only one bit below the MSB, so `h - 2` would
shift by a negative amount — undefined behaviour. The branch keeps those samples in buckets 4 and 5 rather
than reading off the end of the value.

In `TelemetryTimer::record`, add the bucket increment:

```cpp
void TelemetryTimer::record(int64_t micros) {
  if (!m_node) return;
  m_node->count.fetch_add(1, std::memory_order_relaxed);
  m_node->total.fetch_add(micros, std::memory_order_relaxed);
  atomicMin(m_node->tmin, micros);
  atomicMax(m_node->tmax, micros);
  m_node->buckets[Telemetry::histogramBucket(micros)].fetch_add(1, std::memory_order_relaxed);
}
```

- [ ] **Step 5: Emit buckets in the snapshot**

In `Telemetry::snapshot()`, inside the `MetricType::Timer` branch, after the `m["max"] = ...` line:

```cpp
      // Emitted trimmed of trailing zeros: a 64-entry array per timer, mostly zeros, would triple the
      // snapshot for no information. The consumer zero-pads.
      size_t last = 0;
      for (size_t i = 0; i < HistogramBuckets; ++i)
        if (n->buckets[i].load(std::memory_order_relaxed))
          last = i + 1;
      JsonArray buckets;
      buckets.reserve(last);
      for (size_t i = 0; i < last; ++i)
        buckets.append(Json((uint64_t)n->buckets[i].load(std::memory_order_relaxed)));
      m["buckets"] = Json(std::move(buckets));
```

- [ ] **Step 6: Zero the buckets in reset()**

In `Telemetry::reset()`, inside the per-node loop, after `n->count.store(0, ...)`:

```cpp
    for (auto& b : n->buckets)
      b.store(0, std::memory_order_relaxed);
```

- [ ] **Step 7: Build and run the tests**

```bash
cd /root/frackin/OpenStarbound && \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 && \
taskset -c 6-15 dist/core_tests --gtest_filter='Telemetry.*'
```
Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/core/StarTelemetry.hpp source/core/StarTelemetry.cpp source/test/telemetry_test.cpp && \
git commit -m "telemetry: per-timer histograms so the tail is visible

Every metric was a mean. max is a run-long high-water mark and is not windowable,
so a lever that improved mean frame time while doubling p99 read as a clean win --
and every hard bug in this campaign has been episodic, not average.

64 buckets, HdrHistogram-style (octave + two sub-bits), spanning 1us..65535us.
Integer-only: one clz, a shift and a mask, because this is the hot path. Buckets
are cumulative, so differencing two snapshots windows them -- which is what makes
p99 and a windowed max exist at all.

The bucket sum doubles as the instrument's own checksum against count."
```

---

## Stage B — migrate the existing metrics

### Task 3: Declare the CPU metrics

**Files:**
- Modify: `source/game/StarWorldServerThread.cpp:270,292,312`, `source/game/StarWorldServer.cpp:849`
- Modify: `source/game/StarWorldClient.cpp:742,1977,1978,1995,1996,2163,2164`
- Modify: `source/base/StarCellularLighting.cpp:108,118,164,165`
- Modify: `source/base/StarCellularLightArray.hpp:395,400`
- Modify: `source/base/StarAnimatedPartSet.cpp:288,289,349,350,351`
- Modify: `source/game/StarNetworkedAnimator.cpp` (9 counter sites)
- Modify: `source/rendering/StarWorldPainter.cpp:22,240`, `source/rendering/StarBackdropPass.cpp:146,147,384,385,386`
- Modify: `source/rendering/StarGpuLightmapPass.cpp:15,16,17`
- Modify: `source/application/StarRenderer_opengl.cpp:1505,1506`
- Modify: `source/client/StarClientApplication.cpp:552,558,610`
- Modify: `source/core/StarTelemetry.cpp` (`markTick`)

- [ ] **Step 1: Apply the declaration table**

Each site changes from `Telemetry::X("key")` to `Telemetry::X("key", desc)`. Use exactly these descriptors —
they are `{domain, owner, cadence, role}`:

| key | descriptor |
|---|---|
| `tick.server.compute.us` | `{Cpu, Sim, Tick, Budget}` |
| `tick.server.commit.us` | `{Cpu, Sim, Tick, Budget}` |
| `tick.server.publish.us` | `{Cpu, Sim, Tick, Budget}` |
| `tick.server.sync.us` | `{Cpu, Sim, Tick, Budget}` |
| `lighting.cpu.total.us` | `{Cpu, Lighting, Recompute, Total}` |
| `lighting.cpu.gather.us` | `{Cpu, Lighting, Recompute, Budget}` |
| `lighting.cpu.spread.us` | `{Cpu, Lighting, Recompute, Budget}` |
| `lighting.cpu.point.us` | `{Cpu, Lighting, Recompute, Budget}` |
| `lighting.cpu.post.us` | `{Cpu, Lighting, Recompute, Budget}` |
| `lighting.upload.us` | `{Cpu, Frame, Frame, Detail}` |
| `lighting.gpu.cpu_cost.us` | `{Cpu, Frame, Frame, Detail}` |
| `render.frame.us` | `{Cpu, Frame, Frame, Detail}` |
| `render.world.painter.us` | `{Cpu, Frame, Frame, Detail}` |
| `render.interface.us` | `{Cpu, Frame, Frame, Detail}` |
| `lighting.cells` | `{Cpu, Lighting, Call, Detail}` |
| `render.particle.count` | `{Cpu, Frame, Frame, Detail}` |
| every `animator.*` counter | `{Cpu, Sim, Call, Detail}` |
| every `render.drawable.*` counter | `{Cpu, Frame, Call, Detail}` |
| every `render.cache.*` counter | `{Cpu, Frame, Frame, Detail}` |
| every `lighting.lights.*`, `lighting.temporal.*`, `lighting.cpu.calc.*`, `lighting.gpu.*` counter | `{Cpu, Lighting, Recompute, Detail}` |
| `render.flush.count`, `render.flush.primitives` | `{Cpu, Frame, Call, Detail}` |

**Why `render.frame.us` is `Detail`, not `Budget`:** it nests inside `cpu.frame.render.us` (Task 5). Declaring
it `Budget` would double-count it against the frame total.

**Why the `animator.*` counters are owner `Sim`:** they are incremented during entity updates on the server
tick, not during render.

Worked example — `source/game/StarWorldServerThread.cpp:292`:

```cpp
    static auto t = Telemetry::timer("tick.server.compute.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
```

Worked example — `source/client/StarClientApplication.cpp:558`:

```cpp
      static auto renderFrameTimer = Telemetry::timer("render.frame.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
```

Worked example — `source/rendering/StarBackdropPass.cpp:146`:

```cpp
    static auto envRefreshed = Telemetry::counter("render.cache.env.refreshed",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
```

**Sites registering the same key more than once** (`render.drawable.parts.rebuilt` at
`StarNetworkedAnimator.cpp:971,1027,1086`; `render.drawable.parts.cached` at `:970,1085`;
`render.drawable.parts.rebuilt.rekey` at `:1031,1087`) must all use the **identical** descriptor. The
`FirstDeclarationWinsAndMismatchIsRecorded` machinery will flag a mismatch, and Task 7's consistency test will
fail the build if one survives.

- [ ] **Step 2: Declare the tick counters inside markTick**

`markTick` builds its key with `strf`, so it declares rather than using an accessor overload. In
`source/core/StarTelemetry.cpp`, in `markTick`, replace `seq = counter(strf("tick.{}.seq", threadTag));` with:

```cpp
    String key = strf("tick.{}.seq", threadTag);
    // The tag names the tick thread; map it to the logical budget that thread drives. An unrecognised tag is
    // left Unknown rather than guessed -- a wrong owner is worse than an absent one.
    MetricOwner owner = threadTag == "server" ? MetricOwner::Sim
                      : threadTag == "client" ? MetricOwner::Frame
                                              : MetricOwner::Unknown;
    declare(key, MetricDesc{MetricDomain::Cpu, owner, MetricCadence::Tick, MetricRole::Detail});
    seq = counter(key);
```

- [ ] **Step 3: Build everything**

```bash
cd /root/frackin/OpenStarbound && pgrep -a -i starbound; \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
  cmake --build build/linux-release-clang --target starbound core_tests game_tests render_surface_tests -j 8
```
Expected: `pgrep` prints nothing (Director out of game), build exit 0.

- [ ] **Step 4: Verify no pixel changed**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-gate.sh
```
Expected: `GATE: PASS` with nonzero pass counts on all three oracles and `GL_INVALID lines: 0`.

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/game source/base source/rendering source/client source/application source/core && \
git commit -m "telemetry: declare all existing CPU metrics

Every registration site now says what its metric IS. Ownership stops being tribal
knowledge recoverable only by grepping source -- and stops being config-dependent:
lighting.cpu.* is owner=Lighting whether lightingCalc() runs on its own thread or
inline on main (m_asyncLighting), because owner is the logical budget, not a thread.

render.frame.us / render.world.painter.us / render.interface.us are Detail, not
Budget: they nest inside cpu.frame.render.us and would otherwise double-count."
```

---

### Task 4: Declare the GPU metrics

**Files:**
- Modify: `source/application/StarRenderer_opengl.cpp:1066`
- Modify: `source/rendering/StarWorldPass.cpp:46`
- Modify: `source/rendering/StarWorldPainter.cpp:269`
- Modify: `source/rendering/StarBackdropPass.cpp:137,195`
- Modify: `source/rendering/StarGpuLightmapPass.cpp:64,155,219,237`
- Modify: `source/client/StarClientApplication.cpp:599`

- [ ] **Step 1: Declare each GPU key at the pass that owns it**

GPU values are recorded generically inside `OpenGlRenderer` (`StarRenderer_opengl.cpp:994`), which has no idea
what any given pass means — so each pass declares its own key once, via a function-local static, next to where
it calls `gpuTimer().begin()`.

Descriptors:

| key | descriptor |
|---|---|
| `render.frame.gpu_span_us` | `{Gpu, Gl, Frame, Total}` |
| `render.pass.world.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.pass.environment.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.pass.environment.compose.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.pass.parallax.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.pass.parallax.compose.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.pass.compose.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.pass.interface.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.frame.clear.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `render.frame.blit.gpu_us` | `{Gpu, Gl, Frame, Budget}` |
| `lighting.gpu.spread.gpu_us` | `{Gpu, Gl, Recompute, Budget}` |
| `lighting.gpu.point.gpu_us` | `{Gpu, Gl, Recompute, Budget}` |
| `lighting.gpu.compose.gpu_us` | `{Gpu, Gl, Recompute, Budget}` |
| `lighting.gpu.upscale.gpu_us` | `{Gpu, Gl, Recompute, Budget}` |

**Why the lighting GPU passes are cadence `Recompute` but owner `Gl`:** they are GPU work inside the GPU frame,
so they close against `render.frame.gpu_span_us` like every other pass — but they run per lightmap recompute,
not per frame, so their count must be checked against recomputes. Owner answers "what whole do I belong to";
cadence answers "how often should I have fired". Per-frame cost is `total ÷ frames` regardless of either.

Worked example — `source/rendering/StarWorldPass.cpp:46`:

```cpp
  static bool const declared = [] {
    Telemetry::declare("render.pass.world.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
    return true;
  }();
  (void)declared;
  m_renderer->gpuTimer().begin("render.pass.world.gpu_us");
```

Add `#include "StarTelemetry.hpp"` to any of these files that does not already include it.

Worked example — `source/application/StarRenderer_opengl.cpp`, immediately before line 1066's `record` call:

```cpp
        if (t1 > t0) {
          static bool const declared = [] {
            Telemetry::declare("render.frame.gpu_span_us",
              MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Total});
            return true;
          }();
          (void)declared;
          Telemetry::timer("render.frame.gpu_span_us").record((int64_t)((t1 - t0) / 1000));
        }
```

- [ ] **Step 2: Build**

```bash
cd /root/frackin/OpenStarbound && pgrep -a -i starbound; \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
  cmake --build build/linux-release-clang --target starbound core_tests -j 8
```
Expected: build exit 0.

- [ ] **Step 3: Verify no pixel changed**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-gate.sh
```
Expected: `GATE: PASS`.

- [ ] **Step 4: Verify the declarations actually landed on real data**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-profile.sh 30 declare-check && \
python3 -c "
import json,glob,sys
f=sorted(glob.glob('harness/storage-perf/telemetry/*.json'))[-1]
d=json.load(open(f))
bad=[k for k,v in d['metrics'].items() if v['owner']=='unknown']
conf=[k for k,v in d['metrics'].items() if v.get('descConflict')]
print('schema', d['meta']['schema'], '| metrics', len(d['metrics']))
print('UNDECLARED:', bad or 'none')
print('CONFLICTS :', conf or 'none')
sys.exit(1 if bad or conf else 0)
"
```
Expected: `UNDECLARED: none`, `CONFLICTS : none`, exit 0. Any key listed here is a registration site missed in
Task 3 or 4.

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/application source/rendering source/client && \
git commit -m "telemetry: declare the GPU metrics at the passes that own them

GPU values are recorded generically inside OpenGlRenderer, which cannot know what
any given pass means -- so each pass declares its own key next to its begin() call.

render.frame.gpu_span_us is role=Total: it IS the GL owner's whole, so it is
excluded from the sum of parts rather than counted as one of them. The lighting GPU
passes are owner=Gl (they are GPU work inside the GPU frame and close against the
span) but cadence=Recompute (they fire per lightmap recompute, not per frame)."
```

---

## Stage C — the frame budget

### Task 5: Instrument the main loop

**Files:**
- Modify: `source/application/StarMainApplication_sdl.cpp:728-784`

- [ ] **Step 1: Add the budget scopes**

Replace the loop body from `while (true) {` through the `Thread::sleepPrecise` call with:

```cpp
      // THE FRAME BUDGET. cpu.frame.total.us is the denominator for owner=frame: one loop iteration IS one
      // frame, and nothing else is. The other six are role=budget parts that close against it; whatever is
      // left over is reported as unattributed rather than quietly absorbed.
      //
      // Declared here rather than at first use so the descriptors live in one readable block.
      static auto tTotal  = Telemetry::timer("cpu.frame.total.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Total});
      static auto tInput  = Telemetry::timer("cpu.frame.input.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget});
      static auto tUpdate = Telemetry::timer("cpu.frame.update.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget});
      static auto tRender = Telemetry::timer("cpu.frame.render.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget});
      static auto tFinish = Telemetry::timer("cpu.frame.finish.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget});
      static auto tSwap   = Telemetry::timer("cpu.frame.swap.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget});
      static auto tIdle   = Telemetry::timer("cpu.frame.idle.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget});
      static auto cUpdates = Telemetry::counter("cpu.frame.updates",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});

      int64_t frameStart = Time::monotonicMicroseconds();

      bool quit = false;
      while (true) {
        {
          TelemetryScope s(tInput);
          cleanup();

          for (auto const& event : processEvents())
            m_application->processInput(event);

          if (m_platformServices)
            m_platformServices->update();

          if (m_cursorVisible || m_platformServices->overlayActive())
            SDL_ShowCursor();
          else
            SDL_HideCursor();

          ImGui_ImplOpenGL3_NewFrame();
          ImGui_ImplSDL3_NewFrame();
        }

        {
          TelemetryScope s(tUpdate);
          int updatesBehind = max<int>(round(m_updateTicker.ticksBehind()), 1);
          updatesBehind = min<int>(updatesBehind, m_maxFrameSkip + 1);
          // Frame-skip means update() runs 1..N times per frame. Without this count a CPU regression can hide
          // as MORE SKIPPING rather than more time per frame, and the budget would look unchanged.
          cUpdates.inc((uint64_t)updatesBehind);
          for (int i = 0; i < updatesBehind; ++i) {
            //since frame-skipping is a thing, we have to begin a new ImGui frame here to prevent duplicate elements made by updates
            if (i != 0)
              ImGui::EndFrame();
            ImGui::NewFrame();
            m_application->update();
            m_updateRate = m_updateTicker.tick();
          }
        }

        {
          TelemetryScope s(tRender);
          m_renderer->startFrame();
          m_application->render();
        }
        {
          TelemetryScope s(tFinish);
          m_renderer->finishFrame();
          ImGui::Render();
          ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        {
          // THE BOUND VERDICT. With vsync OFF this blocks only when the GPU queue is full, so a large value
          // means the CPU is waiting on the GPU. With vsync ON it blocks for the frame pace instead and means
          // something entirely different -- which is why meta.vsync is written into every snapshot.
          TelemetryScope s(tSwap);
          SDL_GL_SwapWindow(m_sdlWindow);
        }
        m_renderRate = m_renderTicker.tick();

        if (m_quitRequested) {
          Logger::info("Application: quit requested");
          quit = true;
        }

        if (m_signalHandler.interruptCaught()) {
          Logger::info("Application: Interrupt caught");
          quit = true;
        }

        if (quit) {
          Logger::info("Application: quitting...");
          break;
        }

        int64_t spareMilliseconds = round(m_updateTicker.spareTime() * 1000);
        if (spareMilliseconds > 0) {
          TelemetryScope s(tIdle);
          Thread::sleepPrecise(spareMilliseconds);
        }

        // Closes the frame and opens the next in one clock read: the total must cover EVERYTHING, including
        // the loop bookkeeping between the phases, or the unattributed remainder becomes meaningless.
        int64_t now = Time::monotonicMicroseconds();
        tTotal.record(now - frameStart);
        frameStart = now;
      }
```

Add `#include "StarTelemetry.hpp"` and `#include "StarTime.hpp"` to the includes of
`source/application/StarMainApplication_sdl.cpp` if not already present.

- [ ] **Step 2: Build**

```bash
cd /root/frackin/OpenStarbound && pgrep -a -i starbound; \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target starbound -j 8
```
Expected: build exit 0.

- [ ] **Step 3: Verify no pixel changed**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-gate.sh
```
Expected: `GATE: PASS`. **The frame loop is the most invasive change in this plan — if the gate fails here, the
instrumentation reordered something. Stop and fix.**

- [ ] **Step 4: Verify the budget closes on real data**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-profile.sh 45 budget-check && \
python3 -c "
import json,glob
f=sorted(glob.glob('harness/storage-perf/telemetry/*.json'))[-1]
m=json.load(open(f))['metrics']
tot=m['cpu.frame.total.us']
parts={k:v for k,v in m.items() if v.get('owner')=='frame' and v.get('role')=='budget' and v['type']=='timer'}
s=sum(v['total'] for v in parts.values())
print(f\"total {tot['total']}  parts {s}  unattributed {tot['total']-s} ({100*(tot['total']-s)/tot['total']:.1f}%)\")
for k,v in sorted(parts.items(), key=lambda kv:-kv[1]['total']):
    print(f\"  {k:<28} {v['total']/tot['count']:8.1f} us/frame\")
"
```
Expected: unattributed is **positive and under 25%** of total. Negative means a phase is double-counted;
above 25% means the loop instrumentation missed a phase.

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/application/StarMainApplication_sdl.cpp && \
git commit -m "telemetry: close the CPU frame budget

The CPU side had no whole and no denominator. processEvents, the entire client sim
tick, startFrame, finishFrame, ImGui render, SDL_GL_SwapWindow and sleepPrecise were
all untimed, and nothing measured loop-iteration wall time -- render.frame.us covers
only the in-world render portion. The GPU got its whole from P-5; this is the CPU
equivalent.

cpu.frame.total.us is recorded as one clock read that closes a frame and opens the
next, so it covers the loop bookkeeping between phases too -- otherwise the
unattributed remainder means nothing.

cpu.frame.swap.us is the bound verdict: with vsync off it blocks only on a full GPU
queue. cpu.frame.updates counts the frame-skip multiplier, without which a CPU
regression can hide as more skipping rather than more time."
```

---

### Task 6: The wait timer and the environment meta

**Files:**
- Modify: `source/client/StarClientApplication.cpp:543-545`
- Modify: `source/core/StarTelemetryReporter.hpp`
- Modify: `source/core/StarTelemetryReporter.cpp`
- Modify: `source/client/StarClientApplication.cpp:492` (the `writeSnapshot` call)
- Modify: `source/frontend/StarClientCommandProcessor.cpp:647` (the `/telemetry snapshot` call)

- [ ] **Step 1: Time the lighting handoff**

A blocked main thread is billed as frame cost, which is right for frame time and wrong for "what do I
optimise". In `source/client/StarClientApplication.cpp`, replace lines 543-545:

```cpp
      m_worldPainter->render(m_renderData, [&]() -> bool {
        return worldClient->waitForLighting(&m_renderData);
      });
```

with:

```cpp
      // Work versus wait. cpu.frame.render.us bills this block whether the thread was computing or blocked on
      // the lighting thread; that is correct for frame time and useless for choosing a lever. Detail, not
      // Budget: it is a slice of cpu.frame.render.us, not a sibling of it.
      static auto waitLighting = Telemetry::timer("cpu.wait.lighting.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
      m_worldPainter->render(m_renderData, [&]() -> bool {
        TelemetryScope s(waitLighting);
        return worldClient->waitForLighting(&m_renderData);
      });
```

- [ ] **Step 2: Let the reporter carry environment facts**

In `source/core/StarTelemetryReporter.hpp`, change the declaration to:

```cpp
  // `meta` carries facts ABOUT THE RUN that no counter can express -- vsync state, GPU clock, package
  // temperature. They are read once at snapshot time. cpu.frame.swap.us means GPU backpressure with vsync off
  // and frame pacing with vsync on: same metric, opposite meanings, so the reader must not be left to
  // remember which run this was.
  static String writeSnapshot(String const& dir, JsonObject meta = {}, uint64_t stamp = 0);
```

- [ ] **Step 3: Implement meta merging and the process-CPU sample**

Replace the body of `TelemetryReporter::writeSnapshot` in `source/core/StarTelemetryReporter.cpp`:

```cpp
String TelemetryReporter::writeSnapshot(String const& dir, JsonObject meta, uint64_t stamp) {
  String subdir = File::relativeTo(dir, "telemetry");
  if (!File::isDirectory(subdir))
    File::makeDirectory(subdir);
  uint64_t s = stamp ? stamp : (uint64_t)Time::monotonicMilliseconds();
  String path = File::relativeTo(subdir, strf("telemetry-{}.json", s));

  // The core-contention blind spot a single-thread budget cannot see: if the server thread saturates a core, a
  // CPU-cheaper render path frees nothing, and the frame budget alone would never say so. Recorded as a
  // COUNTER rather than a meta field so the generic differencing path windows it with no special case.
  #ifndef STAR_SYSTEM_WINDOWS
  {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
      uint64_t us = (uint64_t)ru.ru_utime.tv_sec * 1000000u + (uint64_t)ru.ru_utime.tv_usec
                  + (uint64_t)ru.ru_stime.tv_sec * 1000000u + (uint64_t)ru.ru_stime.tv_usec;
      auto c = Telemetry::counter("cpu.process.total_us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Process, MetricCadence::Call, MetricRole::Total});
      // getrusage is already cumulative; set it rather than adding, so repeated snapshots do not compound.
      uint64_t prior = c.value();
      if (us > prior)
        c.inc(us - prior);
    }
  }
  #endif

  Json snap = Telemetry::snapshot();
  if (!meta.empty()) {
    JsonObject merged = snap.getObject("meta");
    for (auto const& kv : meta)
      merged[kv.first] = kv.second;
    snap = snap.set("meta", Json(std::move(merged)));
  }
  File::writeFile(snap.repr(2, true), path); // pretty=2, sort=true
  return path;
}
```

Add to the includes of `source/core/StarTelemetryReporter.cpp`:

```cpp
#ifndef STAR_SYSTEM_WINDOWS
#include <sys/resource.h>
#endif
```

- [ ] **Step 4: Pass vsync through from the client**

In `source/client/StarClientApplication.cpp`, replace line 492's
`TelemetryReporter::writeSnapshot(m_root->toStoragePath(""));` with:

```cpp
      TelemetryReporter::writeSnapshot(m_root->toStoragePath(""), JsonObject{
        {"vsync", Json(m_root->configuration()->get("vsync", true).optBool().value(true))}
      });
```

In `source/frontend/StarClientCommandProcessor.cpp`, replace line 647's
`String p = TelemetryReporter::writeSnapshot(Root::singleton().toStoragePath(""));` with:

```cpp
    String p = TelemetryReporter::writeSnapshot(Root::singleton().toStoragePath(""), JsonObject{
      {"vsync", Root::singleton().configuration()->get("vsync", true).optBool().value(true)}
    });
```

- [ ] **Step 5: Build and run tests**

```bash
cd /root/frackin/OpenStarbound && pgrep -a -i starbound; \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
  cmake --build build/linux-release-clang --target starbound core_tests -j 8 && \
taskset -c 6-15 dist/core_tests --gtest_filter='Telemetry.*'
```
Expected: build exit 0, all tests PASS.

- [ ] **Step 6: Verify the gate and the new fields**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-gate.sh && \
./scripts/render-profile.sh 30 meta-check && \
python3 -c "
import json,glob
f=sorted(glob.glob('harness/storage-perf/telemetry/*.json'))[-1]
d=json.load(open(f))
print('meta:', d['meta'])
print('process cpu us:', d['metrics']['cpu.process.total_us']['value'])
print('wait lighting  :', d['metrics']['cpu.wait.lighting.us']['mean'], 'us mean')
"
```
Expected: `GATE: PASS`; `meta` contains `schema: 2` and `vsync: false`; both new metrics present and non-zero.

- [ ] **Step 7: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/client/StarClientApplication.cpp source/core/StarTelemetryReporter.hpp \
        source/core/StarTelemetryReporter.cpp source/frontend/StarClientCommandProcessor.cpp && \
git commit -m "telemetry: wait timer, process CPU, and run context in meta

cpu.wait.lighting.us separates work from blocking at the one handoff we already
name (waitForLighting). Billing a wait as frame cost is right for frame time and
useless for choosing a lever.

cpu.process.total_us covers the core-contention blind spot: if the server thread
saturates a core, a CPU-cheaper render path frees nothing and a single-thread
budget would never say so. A counter, not a meta field, so the generic differencing
path windows it.

meta.vsync because cpu.frame.swap.us means GPU backpressure with vsync off and
frame pacing with vsync on -- the same number with opposite meanings."
```

---

## Stage D — the oracle and the consumer

### Task 7: The instrument's self-test

**Files:**
- Test: `source/test/telemetry_test.cpp`

- [ ] **Step 1: Write the tests**

Append to `source/test/telemetry_test.cpp`:

```cpp
// THE INSTRUMENT'S ORACLE. The renderer has oracles that fail loudly; telemetry had none, and two measurement
// errors in the 2026-07-25 session were exactly the class an oracle catches -- a parts-sum of 119% of the
// whole, and a metric divided by the wrong denominator. These are those errors as failing tests.

TEST(Telemetry, BudgetPartsCloseAgainstOwnerTotal) {
  telemetrySetUp();
  MetricDesc total{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Total};
  MetricDesc part{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  MetricDesc detail{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail};

  Telemetry::timer("cpu.frame.total.us", total).record(1000);
  Telemetry::timer("cpu.frame.input.us", part).record(200);
  Telemetry::timer("cpu.frame.render.us", part).record(600);
  // A Detail metric nests INSIDE a Budget part. If closure counted it, the parts would exceed the whole --
  // which is exactly the double-count the role field exists to prevent.
  Telemetry::timer("render.frame.us", detail).record(550);

  Json snap = Telemetry::snapshot();
  JsonObject metrics = snap.getObject("metrics");
  int64_t parts = 0;
  for (auto const& kv : metrics) {
    Json m = kv.second;
    if (m.getString("type") == "timer" && m.getString("owner") == "frame"
        && m.getString("domain") == "cpu" && m.getString("role") == "budget")
      parts += m.getInt("total");
  }
  int64_t whole = metrics.get("cpu.frame.total.us").getInt("total");
  EXPECT_EQ(parts, 800);
  EXPECT_LE(parts, whole);
  EXPECT_GE(whole - parts, 0);
}

TEST(Telemetry, OwnersDeclareDenominatorAndTotal) {
  telemetrySetUp();
  Json owners = Telemetry::snapshot().getObject("owners");
  // These are DIFFERENT questions: the denominator counts ticks, the total is the whole parts close against.
  // For `gl` they are different metrics -- GPU work is counted per frame but its whole is the GPU frame span.
  EXPECT_EQ(owners.getObject("frame").getString("denominator"), "cpu.frame.total.us");
  EXPECT_EQ(owners.getObject("frame").getString("total"), "cpu.frame.total.us");
  EXPECT_EQ(owners.getObject("gl").getString("denominator"), "cpu.frame.total.us");
  EXPECT_EQ(owners.getObject("gl").getString("total"), "render.frame.gpu_span_us");
  // `sim` has no measured whole; it must not invent one.
  EXPECT_FALSE(owners.getObject("sim").contains("total"));
}

TEST(Telemetry, CadenceCountNeverExceedsDenominator) {
  telemetrySetUp();
  MetricDesc total{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Total};
  MetricDesc part{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Budget};
  auto frames = Telemetry::timer("cpu.frame.total.us", total);
  auto gated = Telemetry::timer("cpu.frame.render.us", part);
  for (int i = 0; i < 10; ++i)
    frames.record(1000);
  for (int i = 0; i < 6; ++i)   // a refresh-gated pass fires on only some frames
    gated.record(100);

  Json metrics = Telemetry::snapshot().getObject("metrics");
  uint64_t denom = metrics.getObject("cpu.frame.total.us").getUInt("count");
  uint64_t count = metrics.getObject("cpu.frame.render.us").getUInt("count");
  // UNDER is legitimate and expected -- a gated pass or an async GPU readback samples only some frames, which
  // is what the coverage figure reports. OVER is always a bug: the span was opened twice in one frame and the
  // metric should have been declared cadence=Call. A symmetric "count ~= denominator" assertion would flag
  // every gated pass in the tree as broken.
  EXPECT_LE(count, denom);
  EXPECT_EQ(count, 6u);
  EXPECT_EQ(denom, 10u);
}

TEST(Telemetry, NoDescriptorConflictsAmongDeclaredMetrics) {
  telemetrySetUp();
  Json metrics = Telemetry::snapshot().getObject("metrics");
  for (auto const& kv : metrics)
    EXPECT_FALSE(kv.second.getBool("descConflict")) << "conflicting declarations for " << kv.first.utf8();
}
```

- [ ] **Step 2: Build and run**

```bash
cd /root/frackin/OpenStarbound && \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 && \
taskset -c 6-15 dist/core_tests --gtest_filter='Telemetry.*'
```
Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add source/test/telemetry_test.cpp && \
git commit -m "telemetry: the instrument gets the oracle the renderer already has

Budget closure, owner denominator-vs-total, cadence bound and descriptor-conflict
tests. These are this session's two measurement errors written as failing tests: a
parts-sum of 119% of the whole, and dividing by the wrong denominator.

The cadence assertion is deliberately ASYMMETRIC. count <= denominator: under is
legitimate (a refresh-gated pass or an async GPU readback samples only some frames,
which is what coverage reports); over is always a bug. A symmetric 'count ~= frames'
would flag every gated pass in the tree as broken."
```

---

### Task 8: Rewrite the windowing consumer

**Files:**
- Rewrite: `scripts/telemetry-window.py`

- [ ] **Step 1: Replace the script**

Replace the whole of `scripts/telemetry-window.py` with:

```python
#!/usr/bin/env python3
"""Difference two cumulative telemetry snapshots into a per-frame cost table.

Every counter, timer and histogram bucket in a snapshot is CUMULATIVE since process start, so a single snapshot
reports the average over the whole run -- world load, shader compilation and atlas warm-up included. Those
dominate the early seconds and drag every per-pass number toward a value that describes no moment of play.
Differencing two snapshots taken inside the steady state gives the cost over THAT window and nothing else.

This consumer knows NOTHING about the engine. It reads `owners` for each budget's denominator and total, and
`metrics` for each metric's domain/owner/cadence/role. That is the point of schema v2: the previous version
discriminated GPU metrics by a filename suffix and hard-coded which metric counted frames, and got it wrong.

  telemetry-window.py <snapshot-dir> [--label NAME] [--first N] [--last N] [--json OUT]
"""
import argparse
import json
import os
import sys

SCHEMA = 2
BUCKETS = 64


def load(path):
    with open(path) as f:
        return json.load(f)


def bucket_bounds(i):
    """Lower and upper microsecond bound of histogram bucket i (see Telemetry::histogramBucket)."""
    h, m = divmod(i, 4)
    lo = (2 ** h) * (1 + m / 4)
    hi = (2 ** h) * (1 + (m + 1) / 4)
    return lo, hi


def percentile(buckets, q):
    """Estimate the q-th percentile (0..1) from windowed bucket counts, interpolating within the bucket."""
    total = sum(buckets)
    if not total:
        return 0.0
    target, seen = q * total, 0
    for i, c in enumerate(buckets):
        if not c:
            continue
        if seen + c >= target:
            lo, hi = bucket_bounds(i)
            frac = (target - seen) / c
            return lo + (hi - lo) * frac
        seen += c
    return bucket_bounds(len(buckets) - 1)[1]


def window(a, b):
    """Delta between two snapshots, carrying each metric's descriptor forward."""
    out = {}
    for name, mb in b.get("metrics", {}).items():
        ma = a.get("metrics", {}).get(name, {})
        d = {k: mb.get(k) for k in ("type", "domain", "owner", "cadence", "role")}
        if mb.get("type") == "timer":
            dc = mb.get("count", 0) - ma.get("count", 0)
            if dc <= 0:
                continue
            ba, bb = ma.get("buckets", []), mb.get("buckets", [])
            ba = ba + [0] * (BUCKETS - len(ba))
            bb = bb + [0] * (BUCKETS - len(bb))
            d.update(count=dc,
                     total=mb.get("total", 0) - ma.get("total", 0),
                     buckets=[y - x for x, y in zip(ba, bb)])
            d["mean"] = d["total"] / dc
        elif mb.get("type") in ("counter", "gauge", "rate"):
            va, vb = ma.get("value", 0), mb.get("value", 0)
            # A gauge is a level, not an accumulation: its delta is meaningless, so carry the latest reading.
            d["value"] = vb if mb.get("type") == "gauge" else vb - va
        out[name] = d
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("snapdir")
    ap.add_argument("--label", default="profile")
    ap.add_argument("--first", type=int, default=None)
    ap.add_argument("--last", type=int, default=None)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    files = sorted(f for f in os.listdir(args.snapdir) if f.endswith(".json"))
    if len(files) < 2:
        print(f"need >=2 snapshots in {args.snapdir}, found {len(files)}", file=sys.stderr)
        return 1

    # Trim the ends when affordable: the first snapshot sits closest to load, the last may be a partial
    # interval cut short by the kill.
    lo = args.first if args.first is not None else (1 if len(files) >= 4 else 0)
    hi = args.last if args.last is not None else (len(files) - 2 if len(files) >= 4 else len(files) - 1)
    a, b = load(os.path.join(args.snapdir, files[lo])), load(os.path.join(args.snapdir, files[hi]))

    schema = b.get("meta", {}).get("schema", 0)
    if schema != SCHEMA:
        # Refuse rather than mis-window. A pre-v2 snapshot has no descriptors, and guessing them is how the
        # 119% error happened in the first place.
        print(f"snapshot schema {schema}, expected {SCHEMA} -- rebuild and re-capture", file=sys.stderr)
        return 2

    owners, w = b.get("owners", {}), window(a, b)
    meta = b.get("meta", {})

    print(f"\n=== {args.label} === window: {files[lo]} -> {files[hi]}  ({hi - lo} intervals)")
    print(f"    vsync={meta.get('vsync')}  gpuClockMhz={meta.get('gpuClockMhz')}  "
          f"packageTempC={meta.get('packageTempC')}\n")

    violations = []
    for owner in sorted({m["owner"] for m in w.values() if m.get("owner") not in (None, "unknown")}):
        spec = owners.get(owner, {})
        denom_name, total_name = spec.get("denominator"), spec.get("total")
        denom = w.get(denom_name, {}).get("count", 0) if denom_name else 0
        if not denom:
            continue
        rows = [(k, v) for k, v in w.items() if v.get("owner") == owner and v.get("type") == "timer"]
        if not rows:
            continue

        print(f"  [owner: {owner}]  {denom} ticks ({denom_name})")
        print(f"  {'metric':<40} {'dom':>4} {'role':>7} {'calls':>8} {'cover':>6} "
              f"{'us/tick':>9} {'p50':>8} {'p99':>9}")
        print(f"  {'-'*40} {'-'*4} {'-'*7} {'-'*8} {'-'*6} {'-'*9} {'-'*8} {'-'*9}")

        for k, v in sorted(rows, key=lambda kv: -kv[1]["total"]):
            per_tick = v["total"] / denom
            cover = 100.0 * v["count"] / denom
            p50 = percentile(v["buckets"], 0.50)
            p99 = percentile(v["buckets"], 0.99)
            print(f"  {k:<40} {v['domain']:>4} {v['role']:>7} {v['count']:>8} {cover:>5.0f}% "
                  f"{per_tick:>9.1f} {p50:>8.1f} {p99:>9.1f}")
            # ASSERTION 2 (cadence bound): under is legitimate -- a gated pass or an async readback samples
            # only some ticks, which the coverage column reports. Over means the span opened twice per tick.
            if v.get("cadence") in ("frame", "tick", "recompute") and v["count"] > denom:
                violations.append(f"{k}: count {v['count']} > denominator {denom} "
                                  f"(declared cadence={v['cadence']}; should it be 'call'?)")
            # ASSERTION 3 (histogram consistency): the buckets are the instrument's own checksum.
            if sum(v["buckets"]) != v["count"]:
                violations.append(f"{k}: histogram sum {sum(v['buckets'])} != count {v['count']}")

        # ASSERTION 1 (budget closure).
        if total_name and total_name in w:
            whole = w[total_name]["total"]
            for dom in sorted({v["domain"] for _, v in rows}):
                parts = sum(v["total"] for _, v in rows if v["role"] == "budget" and v["domain"] == dom)
                un = whole - parts
                pct = 100.0 * parts / whole if whole else 0.0
                print(f"\n  {dom} accounted: {parts/denom:8.1f} us/tick of {whole/denom:.1f} "
                      f"({pct:.1f}%) -- unattributed {un/denom:.1f} us/tick")
                if un < 0:
                    violations.append(f"{owner}/{dom}: parts exceed the whole by {-un} us "
                                      f"-- a Detail metric declared as Budget, or a double-counted phase")
                elif whole and un / whole > 0.25:
                    violations.append(f"{owner}/{dom}: {100*un/whole:.0f}% unattributed "
                                      f"-- the instrumentation is missing a phase")
        print()

    # The bound verdict, from the data rather than from the reader's memory of which run this was.
    tot, swap, idle = w.get("cpu.frame.total.us"), w.get("cpu.frame.swap.us"), w.get("cpu.frame.idle.us")
    if tot and swap:
        sp, ip = swap["mean"], (idle["mean"] if idle else 0.0)
        if meta.get("vsync"):
            verdict = "vsync ON -- swap measures frame PACING, not backpressure; bound is not determinable"
        elif sp > 0.20 * tot["mean"]:
            verdict = f"GPU-BOUND (swap {sp:.0f}us = {100*sp/tot['mean']:.0f}% of frame)"
        elif ip < 0.05 * tot["mean"]:
            verdict = f"CPU-BOUND (idle {ip:.0f}us, swap {sp:.0f}us -- no headroom, not waiting on GPU)"
        else:
            verdict = f"HEADROOM ({100*ip/tot['mean']:.0f}% idle)"
        print(f"  VERDICT: {verdict}")
        print(f"  frame: mean {tot['mean']:.0f}us  p50 {percentile(tot['buckets'],0.50):.0f}us  "
              f"p99 {percentile(tot['buckets'],0.99):.0f}us  "
              f"-> {1e6/tot['mean']:.0f} fps mean")

    if violations:
        print("\n  !! ORACLE VIOLATIONS -- do not quote these numbers:")
        for v in violations:
            print(f"     {v}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"label": args.label, "window": [files[lo], files[hi]],
                       "meta": meta, "owners": owners, "metrics": w, "violations": violations}, f, indent=2)
        print(f"\n  wrote {args.json}")

    return 3 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Sample GPU clock and package temperature in the harness**

These stay OUT of the engine on purpose: the paths are Linux- and driver-specific (`/sys/class/drm/card*/…`
differs between the i915 and xe drivers, `/sys/class/hwmon/…` differs by platform), and a game engine has no
business growing sysfs-scraping for a profiling convenience.

In `scripts/render-profile.sh`, immediately after the `rm -f "$SNAPDIR"/*.json` line that opens the
measurement window, insert:

```bash
# Environment sidecar: GPU clock and package temperature bracketing the window. The ~7% drift between distant
# profile runs is currently explained as "thermal state", which is a guess; this makes it checkable. Sampled
# here rather than in the engine because these paths are driver- and platform-specific.
read_gpu_mhz() {
  cat /sys/class/drm/card*/gt_cur_freq_mhz 2>/dev/null | head -1 ||
  cat /sys/class/drm/card*/device/tile0/gt0/freq0/cur_freq 2>/dev/null | head -1 ||
  echo null
}
read_pkg_temp() {
  for h in /sys/class/hwmon/hwmon*; do
    [ "$(cat "$h/name" 2>/dev/null)" = "coretemp" ] || continue
    t=$(cat "$h/temp1_input" 2>/dev/null) && { echo $((t / 1000)); return; }
  done
  echo null
}
GPU_START=$(read_gpu_mhz); TEMP_START=$(read_pkg_temp)
```

and immediately after the `sleep "$SECONDS_TO_RUN"` line:

```bash
cat > "$SNAPDIR/../env-sidecar.json" <<EOF
{ "gpuClockMhzStart": ${GPU_START:-null}, "gpuClockMhzEnd": $(read_gpu_mhz),
  "packageTempCStart": ${TEMP_START:-null}, "packageTempCEnd": $(read_pkg_temp) }
EOF
```

- [ ] **Step 3: Merge the sidecar in the consumer**

In `scripts/telemetry-window.py`, replace the line `owners, w = b.get("owners", {}), window(a, b)` and the
`meta` line that follows it with:

```python
    owners, w = b.get("owners", {}), window(a, b)
    meta = dict(b.get("meta", {}))
    # Environment facts the engine deliberately does not read (driver- and platform-specific sysfs paths).
    sidecar = os.path.join(args.snapdir, os.pardir, "env-sidecar.json")
    if os.path.exists(sidecar):
        try:
            meta.update(load(sidecar))
        except (OSError, ValueError):
            pass  # a missing or malformed sidecar must never invalidate a real measurement
```

and update the header print to show the bracketing values:

```python
    print(f"    vsync={meta.get('vsync')}  "
          f"gpuMHz={meta.get('gpuClockMhzStart')}->{meta.get('gpuClockMhzEnd')}  "
          f"pkgTempC={meta.get('packageTempCStart')}->{meta.get('packageTempCEnd')}\n")
```

- [ ] **Step 4: Run it against a real capture**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-profile.sh 60 v2-check
```
Expected: per-owner tables for `frame`, `gl`, `sim` and `lighting`; a header line showing vsync plus the GPU
clock and temperature bracketing the window; a `VERDICT:` line; **no** `ORACLE VIOLATIONS` block; exit 0.

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound && \
git add scripts/telemetry-window.py scripts/render-profile.sh && \
git commit -m "telemetry: schema-v2 consumer -- owner-aware, percentiles, oracle

The consumer now knows NOTHING about the engine: it reads owners for each budget's
denominator and total, and each metric's own descriptor. The previous version
discriminated GPU metrics by a filename suffix and hard-coded which metric counted
frames -- and got the denominator wrong, producing a parts-sum of 119%.

Adds p50/p99 columns from the windowed histograms, a coverage column so a gated
pass reads as 60% COVERAGE rather than 40% broken, and the bound verdict derived
from swap/idle with meta.vsync resolving what swap means.

Runs the closure, cadence and histogram assertions on real data and exits non-zero
on a violation rather than printing a plausible number."
```

---

### Task 9: End-to-end verification

**Files:** none modified — this task proves the whole works.

- [ ] **Step 1: Full build and every test suite**

```bash
cd /root/frackin/OpenStarbound && pgrep -a -i starbound; \
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
  cmake --build build/linux-release-clang --target starbound core_tests game_tests render_surface_tests -j 8 && \
taskset -c 6-15 dist/core_tests && \
taskset -c 6-15 dist/render_surface_tests && \
./scripts/game-tests.sh
```
Expected: build exit 0; `core_tests` 235+/235+ PASS (the new Telemetry tests raise the count);
`render_surface_tests` 9/9; `game_tests` 91/91.

- [ ] **Step 2: The render gate — nothing may have changed on screen**

```bash
cd /root/frackin/OpenStarbound && ./scripts/render-gate.sh
```
Expected: `GATE: PASS`, three oracles with nonzero passes and `DIFF=0`, `GL_INVALID lines: 0`.

- [ ] **Step 3: Prove the instrumentation did not cost measurable performance**

Deep tracing is what arms the timers, so compare with it off (the shipping default) against the
pre-change baseline.

```bash
cd /root/frackin/OpenStarbound && \
python3 -c "
import json
p='harness/storage-perf/starbound.config'
d=json.load(open(p)); d['telemetryDeepTracing']=False
json.dump(d,open(p,'w'),indent=2,sort_keys=True); print('deep tracing OFF')
" && ./scripts/render-profile.sh 60 deep-off
```
Expected: `cpu.frame.total.us` within the measured ~1% run-to-run noise floor of a `deep-off` run on the
pre-change binary. If it is outside that, the always-on path picked up cost it should not have.

Restore deep tracing:

```bash
cd /root/frackin/OpenStarbound && python3 -c "
import json
p='harness/storage-perf/starbound.config'
d=json.load(open(p)); d['telemetryDeepTracing']=True
json.dump(d,open(p,'w'),indent=2,sort_keys=True); print('deep tracing ON')
"
```

- [ ] **Step 4: Prove the A/B still works, now on both resources**

Re-run the known-magnitude lever that validated the harness, and confirm it now moves *both* columns
coherently:

```bash
cd /root/frackin/OpenStarbound && \
./scripts/render-profile.sh 60 spread32-v2 --set lightingGpuSpreadIterations=32 && \
./scripts/render-profile.sh 60 spread16-v2 --set lightingGpuSpreadIterations=16 && \
python3 -c "
import json
a=json.load(open('harness/profiles/spread32-v2.json'))['metrics']
b=json.load(open('harness/profiles/spread16-v2.json'))['metrics']
for k in ('lighting.gpu.spread.gpu_us','cpu.frame.total.us','cpu.frame.swap.us','cpu.process.total_us'):
    if k in a and k in b:
        va = a[k].get('mean', a[k].get('value'))
        vb = b[k].get('mean', b[k].get('value'))
        print(f'{k:<32} {va:>12.1f} -> {vb:>12.1f}')
"
```
Expected: `lighting.gpu.spread.gpu_us` roughly halves (~733 → ~384 µs/call, as measured on 2026-07-25), and the
CPU columns move only within noise — a pure-GPU lever should not move CPU.

- [ ] **Step 5: Restore the shipped config value and commit the verification**

```bash
cd /root/frackin/OpenStarbound && python3 -c "
import json
p='harness/storage-perf/starbound.config'
d=json.load(open(p)); d['lightingGpuSpreadIterations']=32
json.dump(d,open(p,'w'),indent=2,sort_keys=True); print('restored')
" && git status --short && git log --oneline -8
```
Expected: clean tree (`harness/` is gitignored), eight new commits from this plan.

---

## Self-review

**Spec coverage.** Spec §2 (metric model) → Task 1. §3 frame budget → Task 5; wait timers → Task 6; context
`meta` + process CPU → Task 6; bound verdict → Task 8. §4 distribution → Task 2. §5 schema → Tasks 1, 2, 6.
§6 self-test → Tasks 7, 8. §7 migration → Tasks 3, 4, 8. §8 testing → Tasks 1–9, gathered in Task 9. §9
out-of-scope items appear in no task, correctly.

**Gap found and closed.** The first draft implemented the `meta` plumbing and `vsync` (Task 6) but no task read
the GPU clock or package temperature, so they would have printed as `None` forever. Rather than carry it, Task
8 Steps 2–3 sample them **in the harness** and merge them in the consumer — which is also the better design:
those sysfs paths are driver- and platform-specific, and the engine has no business scraping them. The spec was
amended to match.

**Type consistency.** `MetricDesc{domain, owner, cadence, role}` field order is identical in every task.
`Telemetry::declare`, `Telemetry::describe`, `Telemetry::histogramBucket`, `Telemetry::HistogramBuckets` are
each defined in Task 1 or 2 and used consistently after. `TelemetryReporter::writeSnapshot(dir, meta, stamp)`
is declared in Task 6 Step 2 and called with that signature in Step 4. Python `window()` emits
`count/total/mean/buckets` for timers and `value` for counters/gauges; every reader in Task 8 and Task 9 uses
those names.

**Ordering.** Each stage leaves the tree green: Stage A is core-only and unit-tested; Stage B is mechanical
declaration with the render gate as the guard; Stage C touches the frame loop with the gate as the guard;
Stage D is test-and-consumer only.
