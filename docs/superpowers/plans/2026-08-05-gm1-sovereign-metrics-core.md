# GM-1 Sovereign Metrics Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `source/metrics/` module with no dependency above `core` that reads true GPU engine-busy time from the kernel, states what every number physically is, refuses to report zero when it cannot measure, and ships a `metrics --pid N` CLI that works against the shipped, uninstrumented game.

**Architecture:** Two independent kernel readers for the same physical quantity — per-client (`/proc/<pid>/fdinfo`, deduped by `drm-client-id`) and system-wide (i915 PMU via `perf_event_open`). Every reading is a `MetricSample` that cannot exist without `measures` (the physical quantity) and `validWhen` (the condition under which it is that quantity). The two readers cross-check each other; that mutual disagreement is the module's keystone test.

**Tech Stack:** C++17, Star core primitives (`String`, `StringMap`, `Maybe`), Linux `/proc` and `perf_event_open`, gtest via `core_tests`, CMake.

**Source spec:** `docs/superpowers/specs/2026-08-05-sovereign-metrics-design.md`

---

## Background the implementer needs

Three numbers this project reported recently were artefacts, and this module exists because of the third:

`/proc/<pid>/fdinfo` contains one file per open fd. When a process dups the DRM device, **every one of those fds reports the same `drm-client-id` and the same identical `drm-engine-render` total**. starbound holds four. Summing across fds therefore multiplies the answer by four, which produced a "GPU is 96.8% busy" reading whose true value was 24.2%. It was only caught because the next run read 117.3% and a single engine cannot exceed 100%.

Real fdinfo content from this machine (pid 474422, four fds, one client):

```
/proc/474422/fdinfo/6:  drm-client-id: 158   drm-engine-render: 3213824640 ns
/proc/474422/fdinfo/7:  drm-client-id: 158   drm-engine-render: 3213824640 ns
/proc/474422/fdinfo/8:  drm-client-id: 158   drm-engine-render: 3213824640 ns
/proc/474422/fdinfo/9:  drm-client-id: 158   drm-engine-render: 3213824640 ns
```

**The dedup rule is the module's reason to exist. Task 3 pins it as a test.**

Second, `render.pass.compose.gpu_us` reported `0us` for its entire existence, because it timed a bracket around a conditional that never ran, and an empty bracket is indistinguishable from free work. **Hence: a reader that cannot read must say so. It must never return zero.** Task 5 pins that.

---

## File structure

| file | responsibility |
|---|---|
| `source/metrics/StarMetricSample.hpp` | the reading type; `measures` + `validWhen` are constructor arguments, so a sample without them does not compile |
| `source/metrics/StarBusyReading.hpp` | reader result: available-or-reason, never a bare number |
| `source/metrics/StarClientBusyReader.hpp/.cpp` | `/proc/<pid>/fdinfo` per-client engine busy, deduped by `drm-client-id` |
| `source/metrics/StarEngineBusyReader.hpp/.cpp` | i915 PMU system-wide engine busy via `perf_event_open` |
| `source/metrics/metrics_main.cpp` | the `metrics` CLI |
| `source/metrics/CMakeLists.txt` | `star_metrics` library + `metrics` executable |
| `source/test/metrics_test.cpp` | unit tests, fixture-driven, no GPU required |

---

### Task 1: Register `source/metrics/` in the component register

The `tree_map` gate generates the target directory tree from a component register and fails when the tree and the register disagree. `source/` has exactly six registered members today. Adding a directory without a register row turns that gate red.

**Files:**
- Modify: `docs/superpowers/specs/2026-08-01-target-state-system-architecture.md` (the component register — `scripts/spec-model.py:45` declares this path)

- [ ] **Step 1: Find the component register table**

```bash
cd /root/frackin/OpenStarbound
grep -n "rendering" docs/superpowers/specs/2026-08-01-target-state-system-architecture.md | head -20
```

Identify the table whose rows name `source/` components (the one `scripts/spec-model.py`'s `components()` parses). Read ten rows around a `rendering` row to learn the exact column order before editing.

- [ ] **Step 2: Add the `metrics` row**

Add one row matching the existing column shape exactly. The component is named `metrics`; its zone is the same zone as `core` (it is a substrate, not a game system); its description:

```
sovereign measurement -- kernel-side engine busy, the metric contract, and the `metrics` CLI. Depends on nothing above core; the renderer does not link it.
```

- [ ] **Step 3: Regenerate the generated blocks**

```bash
cd /root/frackin/OpenStarbound
python3 scripts/tree-map.py --inject
```

Expected: `tree-map: written -- tree, zones`

- [ ] **Step 4: Verify the gate is green**

```bash
cd /root/frackin/OpenStarbound
python3 scripts/tree-map.py --check; echo "EXIT=$?"
```

Expected: `tree-map: OK -- the tree and the zone tally match the register` and `EXIT=0`

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound
git add docs/superpowers/specs/2026-08-01-target-state-system-architecture.md
git commit -m "GM-1: register source/metrics/ as a component

A sovereign directory is a register row, not a mkdir. tree_map generates the
target tree from the register and fails when they disagree. [#229]"
```

---

### Task 2: `MetricSample` — a reading cannot exist without its meaning

`render.pass.parallax.gpu_us` was named "GPU microseconds of the parallax pass" and meant "elapsed span of a bracket that may contain no work, sampled through a magnitude-biased filter". Nothing could hold that gap. This type holds it.

**Files:**
- Create: `source/metrics/StarMetricSample.hpp`
- Create: `source/test/metrics_test.cpp`
- Modify: `source/test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `source/test/metrics_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Register the test file and run to verify it fails**

In `source/test/CMakeLists.txt`, add `metrics_test.cpp` to `star_core_tests_SOURCES`, keeping the list alphabetical (it sits after `lighting_closedform_test.cpp` and before `mixer_test.cpp` — check the actual neighbours before inserting).

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -5
```

Expected: FAIL — `StarMetricSample.hpp: No such file or directory`

- [ ] **Step 3: Write the header**

Create `source/metrics/StarMetricSample.hpp`:

```cpp
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

  String  key;
  double  value;
  String  unit;          // "ns", "ratio", "hz"
  String  measures;      // the physical quantity, in words
  String  validWhen;     // the condition under which the value IS that quantity
  String  source;        // "fdinfo:drm-engine-render", "i915-pmu:rcs0-busy"
  int64_t tMonotonicNs;
};

}
```

- [ ] **Step 4: Create the module CMakeLists and wire it in**

Create `source/metrics/CMakeLists.txt`:

```cmake
INCLUDE_DIRECTORIES (
    ${STAR_EXTERN_INCLUDES}
    ${STAR_CORE_INCLUDES}
    ${STAR_METRICS_INCLUDES}
  )

SET (star_metrics_HEADERS
    StarBusyReading.hpp
    StarClientBusyReader.hpp
    StarEngineBusyReader.hpp
    StarMetricSample.hpp
  )

SET (star_metrics_SOURCES
    StarClientBusyReader.cpp
    StarEngineBusyReader.cpp
  )

ADD_LIBRARY (star_metrics OBJECT ${star_metrics_SOURCES} ${star_metrics_HEADERS})
```

In `source/CMakeLists.txt`, beside the existing `SET (STAR_CORE_INCLUDES ...)` declaration add:

```cmake
SET (STAR_METRICS_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/metrics)
```

and beside the existing `ADD_SUBDIRECTORY (core)` add `ADD_SUBDIRECTORY (metrics)`. Read the surrounding lines first and match their ordering convention.

In `source/test/CMakeLists.txt`, add `${STAR_METRICS_INCLUDES}` to the test target's `INCLUDE_DIRECTORIES` and `$<TARGET_OBJECTS:star_metrics>` to the `core_tests` sources, matching how `star_core` is referenced there.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -3
cd dist && ./core_tests --gtest_filter='MetricSampleTest.*'
```

Expected: `[  PASSED  ] 2 tests.`
(Test binaries must be run from `dist/`.)

- [ ] **Step 6: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/metrics/StarMetricSample.hpp source/metrics/CMakeLists.txt source/CMakeLists.txt source/test/metrics_test.cpp source/test/CMakeLists.txt
git commit -m "GM-1: MetricSample -- a reading carries its meaning or does not exist

measures (the physical quantity) and validWhen (the condition under which the
value IS that quantity) are constructor arguments, and empty ones throw. The
defect being closed is a metric whose name and meaning were different sentences
and nothing could hold the gap. [#229]"
```

---

### Task 3: `ClientBusyReader` — the dedup rule, pinned

**Files:**
- Create: `source/metrics/StarBusyReading.hpp`
- Create: `source/metrics/StarClientBusyReader.hpp`, `source/metrics/StarClientBusyReader.cpp`
- Modify: `source/test/metrics_test.cpp`

- [ ] **Step 1: Write the failing test**

Append to `source/test/metrics_test.cpp`:

```cpp
#include "StarClientBusyReader.hpp"
#include "StarFile.hpp"

namespace {
  // Build a fake /proc/<pid>/fdinfo tree. The four-fds-one-client shape is not hypothetical:
  // it is what starbound presents, and summing it produced a 96.8% reading whose truth was 24.2%.
  String makeFdinfoDir(String const& name, List<pair<int, int64_t>> const& fds) {
    String dir = File::temporaryDirectory();
    for (size_t i = 0; i < fds.size(); ++i)
      File::writeFile(strf("drm-driver:\ti915\ndrm-client-id:\t{}\ndrm-engine-render:\t{} ns\n",
                           fds[i].first, fds[i].second),
                      File::relativeTo(dir, toString(i)));
    return dir;
  }
}

TEST(ClientBusyReaderTest, DeduplicatesFdsSharingOneClientId) {
  // Four fds, ONE client, identical totals -- the exact shape that inflated a reading 4x.
  auto dir = makeFdinfoDir("dup", {{158, 3213824640}, {158, 3213824640},
                                   {158, 3213824640}, {158, 3213824640}});
  auto r = ClientBusyReader::readFdinfoDir(dir);
  ASSERT_TRUE(r.available) << r.unavailableReason.utf8Ptr();
  EXPECT_EQ(r.clients, 1u);
  EXPECT_EQ(r.engineNs.get("render"), 3213824640);   // 1x, NOT 4x
  File::removeDirectoryRecursive(dir);
}

TEST(ClientBusyReaderTest, SumsDistinctClients) {
  auto dir = makeFdinfoDir("multi", {{158, 1000}, {158, 1000}, {201, 500}});
  auto r = ClientBusyReader::readFdinfoDir(dir);
  ASSERT_TRUE(r.available);
  EXPECT_EQ(r.clients, 2u);
  EXPECT_EQ(r.engineNs.get("render"), 1500);
  File::removeDirectoryRecursive(dir);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -5
```

Expected: FAIL — `StarClientBusyReader.hpp: No such file or directory`

- [ ] **Step 3: Write `StarBusyReading.hpp`**

```cpp
#pragma once

#include "StarMap.hpp"
#include "StarString.hpp"

namespace Star {

// A reader result that cannot be mistaken for a measurement of zero.
//
// render.pass.compose.gpu_us reported 0us for its entire existence, because it bracketed a
// conditional that never ran and an empty bracket looks exactly like free work. `available` is
// therefore not a convenience: it is the difference between "measured nothing" and "could not
// measure", which no caller may conflate.
struct BusyReading {
  static BusyReading unavailable(String reason) {
    BusyReading r;
    r.unavailableReason = std::move(reason);
    return r;
  }

  bool available = false;
  String unavailableReason;        // non-empty exactly when !available
  StringMap<int64_t> engineNs;     // "render" -> busy nanoseconds, summed over distinct clients
  unsigned clients = 0;
};

}
```

- [ ] **Step 4: Write `StarClientBusyReader.hpp`**

```cpp
#pragma once

#include "StarBusyReading.hpp"

namespace Star {

// Per-client GPU engine busy time from /proc/<pid>/fdinfo.
//
// THE DEDUP RULE. Every fd that dups the DRM device reports the SAME drm-client-id and the SAME
// engine totals; starbound holds four. Summing across fds multiplies the answer by the number of
// dups. That produced a "96.8% busy" figure whose true value was 24.2%, and it was caught only
// because a later run read 117.3% -- a single engine cannot exceed 100%. One entry per client.
class ClientBusyReader {
public:
  // Reads /proc/<pid>/fdinfo. Returns BusyReading::unavailable(reason) if the process is gone, the
  // directory is unreadable, or no fd carries drm-engine-* lines (a non-DRM or non-Intel process).
  static BusyReading read(int pid);

  // Same logic against an arbitrary directory of fdinfo-shaped files, so the dedup rule is testable
  // without a GPU, a driver, or root.
  static BusyReading readFdinfoDir(String const& dir);
};

}
```

- [ ] **Step 5: Write `StarClientBusyReader.cpp`**

```cpp
#include "StarClientBusyReader.hpp"

#include "StarFile.hpp"
#include "StarLexicalCast.hpp"

namespace Star {

BusyReading ClientBusyReader::readFdinfoDir(String const& dir) {
  if (!File::isDirectory(dir))
    return BusyReading::unavailable(strf("fdinfo directory '{}' is not readable", dir));

  // clientId -> engine -> ns. Keyed by client so a dup contributes once, and LAST WRITE WINS rather
  // than accumulating: the dups carry identical totals, so summing them is the bug.
  Map<String, StringMap<int64_t>> byClient;

  for (auto const& entry : File::dirList(dir)) {
    String text;
    try {
      text = File::readFileString(File::relativeTo(dir, entry.first));
    } catch (StarException const&) {
      continue;   // fds close underneath us constantly; a vanished fd is not an error
    }

    String clientId;
    StringMap<int64_t> engines;
    for (auto const& line : text.split('\n')) {
      auto parts = line.split('\t');
      if (parts.size() < 2)
        continue;
      String key = parts.at(0).trim();
      String val = parts.at(1).trim();
      if (key == "drm-client-id:") {
        clientId = val;
      } else if (key.beginsWith("drm-engine-") && key.endsWith(":")) {
        String engine = key.substr(11, key.size() - 12);      // drm-engine-render: -> render
        auto num = val.splitAny(" \t").maybeFirst();          // "3213824640 ns" -> "3213824640"
        if (num)
          engines[engine] = lexicalCast<int64_t>(*num);
      }
    }
    if (!clientId.empty() && !engines.empty())
      byClient[clientId] = engines;
  }

  if (byClient.empty())
    return BusyReading::unavailable(
      strf("no drm-engine-* accounting in '{}' -- not a DRM client, or a driver that does not "
           "publish per-client engine time", dir));

  BusyReading r;
  r.available = true;
  r.clients = (unsigned)byClient.size();
  for (auto const& c : byClient)
    for (auto const& e : c.second)
      r.engineNs[e.first] += e.second;
  return r;
}

BusyReading ClientBusyReader::read(int pid) {
  String dir = strf("/proc/{}/fdinfo", pid);
  if (!File::isDirectory(dir))
    return BusyReading::unavailable(strf("no such process, or /proc/{}/fdinfo unreadable", pid));
  return readFdinfoDir(dir);
}

}
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -3
cd dist && ./core_tests --gtest_filter='ClientBusyReaderTest.*'
```

Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 7: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/metrics/StarBusyReading.hpp source/metrics/StarClientBusyReader.hpp source/metrics/StarClientBusyReader.cpp source/metrics/CMakeLists.txt source/test/metrics_test.cpp
git commit -m "GM-1: ClientBusyReader, with the 4x dedup bug pinned as a test

Four fds sharing one drm-client-id must yield 1x the busy time. Summing them
produced a 96.8% GPU-busy reading whose true value was 24.2%, caught only when a
later run read 117.3% and a single engine cannot exceed 100%. [#229]"
```

---

### Task 4: Unavailability is never zero

**Files:**
- Modify: `source/test/metrics_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(ClientBusyReaderTest, MissingProcessIsUnavailableNotZero) {
  auto r = ClientBusyReader::read(0x7FFFFFFF);   // a pid that cannot exist
  EXPECT_FALSE(r.available);
  EXPECT_FALSE(r.unavailableReason.empty());
  EXPECT_TRUE(r.engineNs.empty());              // NOT {"render": 0}
}

TEST(ClientBusyReaderTest, NonDrmDirectoryIsUnavailableNotZero) {
  String dir = File::temporaryDirectory();
  File::writeFile("pos:\t0\nflags:\t0100000\n", File::relativeTo(dir, "0"));
  auto r = ClientBusyReader::readFdinfoDir(dir);
  EXPECT_FALSE(r.available);
  EXPECT_TRUE(r.unavailableReason.contains("drm-engine"));
  File::removeDirectoryRecursive(dir);
}
```

- [ ] **Step 2: Run to verify**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -3
cd dist && ./core_tests --gtest_filter='ClientBusyReaderTest.*'
```

Expected: `[  PASSED  ] 4 tests.` — Task 3's implementation already satisfies these; the tests exist to make the behaviour load-bearing rather than incidental, so that a later refactor cannot quietly turn "cannot measure" back into "measured zero".

- [ ] **Step 3: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/test/metrics_test.cpp
git commit -m "GM-1: assert unavailable never reads as zero

render.pass.compose.gpu_us reported 0us for its whole existence because an empty
bracket is indistinguishable from free work. These two tests make the distinction
load-bearing rather than incidental. [#229]"
```

---

### Task 5: Delta sampling discards a backwards counter

A client that exits and restarts resets its counters. Extrapolating across that produces a huge bogus delta.

**Files:**
- Modify: `source/metrics/StarClientBusyReader.hpp`, `source/metrics/StarClientBusyReader.cpp`
- Modify: `source/test/metrics_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
TEST(BusyDeltaTest, ForwardDeltaIsTheDifference) {
  BusyReading a, b;
  a.available = b.available = true;
  a.clients = b.clients = 1;
  a.engineNs["render"] = 1000;
  b.engineNs["render"] = 3000;
  auto d = busyDelta(a, b, 4000);
  ASSERT_TRUE(d.available);
  EXPECT_EQ(d.engineNs.get("render"), 2000);
}

TEST(BusyDeltaTest, BackwardsCounterIsDiscardedNotReported) {
  BusyReading a, b;
  a.available = b.available = true;
  a.clients = b.clients = 1;
  a.engineNs["render"] = 3000;
  b.engineNs["render"] = 1000;      // client restarted
  auto d = busyDelta(a, b, 4000);
  EXPECT_FALSE(d.available);
  EXPECT_TRUE(d.unavailableReason.contains("backwards"));
}

TEST(BusyDeltaTest, UnavailableEndpointPoisonsTheDelta) {
  BusyReading a; a.available = true; a.clients = 1; a.engineNs["render"] = 1000;
  auto d = busyDelta(a, BusyReading::unavailable("process exited"), 4000);
  EXPECT_FALSE(d.available);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -5
```

Expected: FAIL — `use of undeclared identifier 'busyDelta'`

- [ ] **Step 3: Declare `busyDelta` in `StarBusyReading.hpp`**

Add inside `namespace Star`, after the struct:

```cpp
// Difference two readings taken wallNs apart. Either endpoint being unavailable poisons the result,
// and so does a counter that moved backwards (the client restarted): extrapolating across a reset
// invents a delta the hardware never did.
BusyReading busyDelta(BusyReading const& a, BusyReading const& b, int64_t wallNs);
```

- [ ] **Step 4: Implement it in `StarClientBusyReader.cpp`**

```cpp
BusyReading busyDelta(BusyReading const& a, BusyReading const& b, int64_t wallNs) {
  if (!a.available)
    return BusyReading::unavailable(strf("start sample unavailable: {}", a.unavailableReason));
  if (!b.available)
    return BusyReading::unavailable(strf("end sample unavailable: {}", b.unavailableReason));
  if (wallNs <= 0)
    return BusyReading::unavailable("non-positive sampling window");

  BusyReading d;
  d.available = true;
  d.clients = b.clients;
  for (auto const& e : b.engineNs) {
    int64_t before = a.engineNs.maybe(e.first).value(0);
    if (e.second < before)
      return BusyReading::unavailable(
        strf("engine '{}' counter went backwards ({} -> {}): the client restarted, and a delta "
             "across a reset is invented, not measured", e.first, before, e.second));
    d.engineNs[e.first] = e.second - before;
  }
  return d;
}
```

- [ ] **Step 5: Run to verify it passes**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -3
cd dist && ./core_tests --gtest_filter='BusyDeltaTest.*'
```

Expected: `[  PASSED  ] 3 tests.`

- [ ] **Step 6: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/metrics/StarBusyReading.hpp source/metrics/StarClientBusyReader.cpp source/test/metrics_test.cpp
git commit -m "GM-1: busyDelta -- a reset counter is discarded, not extrapolated [#229]"
```

---

### Task 6: `EngineBusyReader` — i915 PMU, honest about privilege

The PMU is system-wide, so it sees GPU work this process did not cause. That is exactly why it is a **cross-check on**, not a replacement for, the per-client reader.

On this host `/proc/sys/kernel/perf_event_paranoid` is `2`, so `perf_event_open` on the i915 PMU fails without `CAP_PERFMON`. It must say so.

**Files:**
- Create: `source/metrics/StarEngineBusyReader.hpp`, `source/metrics/StarEngineBusyReader.cpp`
- Modify: `source/test/metrics_test.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include "StarEngineBusyReader.hpp"

// Runs on any machine. Either the PMU opens and reports, or it declines with a reason naming why.
// What it must NEVER do is report a number it did not obtain.
TEST(EngineBusyReaderTest, EitherReadsOrExplainsItself) {
  EngineBusyReader reader;
  auto r = reader.open("rcs0-busy");
  if (!r.available) {
    EXPECT_FALSE(r.unavailableReason.empty());
    EXPECT_TRUE(r.unavailableReason.contains("perf_event") ||
                r.unavailableReason.contains("i915"))
        << r.unavailableReason.utf8Ptr();
  } else {
    EXPECT_TRUE(r.engineNs.contains("rcs0"));
  }
}

TEST(EngineBusyReaderTest, UnknownEventIsUnavailable) {
  EngineBusyReader reader;
  auto r = reader.open("no-such-event");
  EXPECT_FALSE(r.available);
  EXPECT_FALSE(r.unavailableReason.empty());
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -5
```

Expected: FAIL — `StarEngineBusyReader.hpp: No such file or directory`

- [ ] **Step 3: Write `StarEngineBusyReader.hpp`**

```cpp
#pragma once

#include "StarBusyReading.hpp"

namespace Star {

// System-wide GPU engine busy time from the i915 PMU (/sys/bus/event_source/devices/i915).
//
// SCOPE, stated because it is the difference between this and ClientBusyReader: the PMU counts the
// whole device, including work other processes caused. It is therefore the CROSS-CHECK on the
// per-client reader, not a substitute for it. When exactly one GPU client is running the two measure
// the same physical quantity by different kernel paths, and must agree -- see the mutual-validation
// test. Two instruments that can disagree is the property the old GPU telemetry never had, and its
// absence is why a 4x error and a 4.04x artefact both survived a full day.
//
// PRIVILEGE: perf_event_open against the i915 PMU needs perf_event_paranoid <= 0 or CAP_PERFMON.
// This is not a portability footnote -- on the development host paranoid is 2, so the ordinary path
// is UNAVAILABLE, and it must say so rather than read zero.
class EngineBusyReader {
public:
  ~EngineBusyReader();

  // Opens the named i915 PMU event ("rcs0-busy", "actual-frequency-gt0", "rc6-residency-gt0") and
  // takes an initial reading. Returns unavailable(reason) when the PMU, the event, or the privilege
  // is missing.
  BusyReading open(String const& event);

  // Reads the currently-open counter. Unavailable if open() did not succeed.
  BusyReading sample();

private:
  int m_fd = -1;
  String m_engine;
};

}
```

- [ ] **Step 4: Write `StarEngineBusyReader.cpp`**

```cpp
#include "StarEngineBusyReader.hpp"

#include "StarFile.hpp"
#include "StarLexicalCast.hpp"

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Star {

static String const PmuRoot = "/sys/bus/event_source/devices/i915";

EngineBusyReader::~EngineBusyReader() {
  if (m_fd >= 0)
    ::close(m_fd);
}

BusyReading EngineBusyReader::open(String const& event) {
  if (!File::isDirectory(PmuRoot))
    return BusyReading::unavailable(strf("no i915 PMU at {} -- not an Intel GPU, or i915 not loaded",
                                         PmuRoot));

  String typePath = File::relativeTo(PmuRoot, "type");
  String cfgPath = File::relativeTo(File::relativeTo(PmuRoot, "events"), event);
  if (!File::isFile(cfgPath))
    return BusyReading::unavailable(strf("i915 PMU has no event '{}'", event));

  uint32_t type;
  uint64_t config;
  try {
    type = lexicalCast<uint32_t>(File::readFileString(typePath).trim());
    // events/<name> contains e.g. "config=0x0"
    config = lexicalCast<uint64_t>(File::readFileString(cfgPath).trim().split('=').at(1).trim(),
                                   std::hex);
  } catch (StarException const& e) {
    return BusyReading::unavailable(strf("i915 PMU descriptor unreadable: {}", outputException(e, false)));
  }

  perf_event_attr attr = {};
  attr.size = sizeof(attr);
  attr.type = type;
  attr.config = config;
  attr.read_format = 0;

  long fd = syscall(__NR_perf_event_open, &attr, -1 /*any pid*/, 0 /*cpu 0*/, -1, 0);
  if (fd < 0) {
    int err = errno;
    String paranoid = "unknown";
    try {
      paranoid = File::readFileString("/proc/sys/kernel/perf_event_paranoid").trim();
    } catch (StarException const&) {}
    return BusyReading::unavailable(
      strf("perf_event_open(i915:{}) failed: {}; perf_event_paranoid={} (needs <=0 or CAP_PERFMON)",
           event, strerror(err), paranoid));
  }

  m_fd = (int)fd;
  m_engine = event.splitAny("-").maybeFirst().value(event);   // "rcs0-busy" -> "rcs0"
  return sample();
}

BusyReading EngineBusyReader::sample() {
  if (m_fd < 0)
    return BusyReading::unavailable("i915 PMU counter is not open");
  uint64_t v = 0;
  ssize_t n = ::read(m_fd, &v, sizeof(v));
  if (n != (ssize_t)sizeof(v))
    return BusyReading::unavailable(strf("short read from i915 PMU counter ({} bytes)", n));

  BusyReading r;
  r.available = true;
  r.clients = 0;                 // system-wide: no client attribution
  r.engineNs[m_engine] = (int64_t)v;
  return r;
}

}
```

Add `StarEngineBusyReader.cpp` to `star_metrics_SOURCES` in `source/metrics/CMakeLists.txt`.

- [ ] **Step 5: Run to verify it passes**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8 2>&1 | tail -3
cd dist && ./core_tests --gtest_filter='EngineBusyReaderTest.*'
```

Expected: `[  PASSED  ] 2 tests.` — on this host the first test takes the unavailable branch (paranoid=2), which is the point: it asserts the refusal is explained.

- [ ] **Step 6: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/metrics/StarEngineBusyReader.hpp source/metrics/StarEngineBusyReader.cpp source/metrics/CMakeLists.txt source/test/metrics_test.cpp
git commit -m "GM-1: EngineBusyReader -- i915 PMU, and honest when it cannot open

perf_event_paranoid is 2 on the dev host, so the ordinary path is unavailable and
says so, naming the paranoid level. A privileged instrument that silently reads
zero is worse than one that is absent. [#229]"
```

---

### Task 7: The `metrics` CLI

The sovereignty proof: if it measures the shipped, uninstrumented game, the module genuinely does not depend on us.

**Files:**
- Create: `source/metrics/metrics_main.cpp`
- Modify: `source/metrics/CMakeLists.txt`

- [ ] **Step 1: Write `metrics_main.cpp`**

```cpp
#include "StarClientBusyReader.hpp"
#include "StarEngineBusyReader.hpp"
#include "StarMetricSample.hpp"
#include "StarTime.hpp"

#include <cstdio>

using namespace Star;

// `metrics --pid N --for SECS [--json]`
//
// Measures a process's GPU engine busy time from OUTSIDE it. The target needs no instrumentation,
// which is the whole claim: this works against the shipped game, and cannot perturb what it reads.
int main(int argc, char** argv) {
  int pid = -1;
  double secs = 5.0;
  bool json = false;
  for (int i = 1; i < argc; ++i) {
    String a = argv[i];
    if (a == "--pid" && i + 1 < argc) pid = lexicalCast<int>(argv[++i]);
    else if (a == "--for" && i + 1 < argc) secs = lexicalCast<double>(argv[++i]);
    else if (a == "--json") json = true;
    else { fprintf(stderr, "usage: metrics --pid N [--for SECS] [--json]\n"); return 64; }
  }
  if (pid < 0) { fprintf(stderr, "usage: metrics --pid N [--for SECS] [--json]\n"); return 64; }

  int64_t t0 = Time::monotonicNanoseconds();
  auto a = ClientBusyReader::read(pid);
  if (!a.available) { fprintf(stderr, "metrics: %s\n", a.unavailableReason.utf8Ptr()); return 3; }
  Thread::sleep((unsigned)(secs * 1000));
  auto b = ClientBusyReader::read(pid);
  int64_t wall = Time::monotonicNanoseconds() - t0;

  auto d = busyDelta(a, b, wall);
  if (!d.available) { fprintf(stderr, "metrics: %s\n", d.unavailableReason.utf8Ptr()); return 3; }

  List<MetricSample> out;
  for (auto const& e : d.engineNs) {
    out.append(MetricSample(strf("gpu.engine.{}.busy_ns", e.first), (double)e.second, "ns",
                            strf("{} engine busy time, summed over this process's DRM clients", e.first),
                            "always", "fdinfo:drm-engine", t0));
    out.append(MetricSample(strf("gpu.engine.{}.busy_ratio", e.first),
                            (double)e.second / (double)wall, "ratio",
                            strf("{} engine busy time as a fraction of wall clock", e.first),
                            "always", "fdinfo:drm-engine", t0));
  }

  if (json) {
    printf("{\"pid\":%d,\"wall_ns\":%lld,\"clients\":%u,\"samples\":[", pid, (long long)wall, d.clients);
    for (size_t i = 0; i < out.size(); ++i)
      printf("%s{\"key\":\"%s\",\"value\":%.6f,\"unit\":\"%s\",\"measures\":\"%s\",\"valid_when\":\"%s\",\"source\":\"%s\"}",
             i ? "," : "", out[i].key.utf8Ptr(), out[i].value, out[i].unit.utf8Ptr(),
             out[i].measures.utf8Ptr(), out[i].validWhen.utf8Ptr(), out[i].source.utf8Ptr());
    printf("]}\n");
  } else {
    printf("pid %d, %.1fs, %u DRM client(s)\n", pid, wall / 1e9, d.clients);
    for (auto const& s : out)
      printf("  %-34s %14.6f %-6s  [%s; valid: %s]\n", s.key.utf8Ptr(), s.value, s.unit.utf8Ptr(),
             s.measures.utf8Ptr(), s.validWhen.utf8Ptr());
  }
  return 0;
}
```

- [ ] **Step 2: Add the executable target**

Append to `source/metrics/CMakeLists.txt`:

```cmake
ADD_EXECUTABLE (metrics metrics_main.cpp $<TARGET_OBJECTS:star_metrics> $<TARGET_OBJECTS:star_core>)
TARGET_LINK_LIBRARIES (metrics ${STAR_EXT_LIBS} ${STAR_CORE_LIBS})
```

Match the link-variable names used by `source/json_tool/CMakeLists.txt`; read that file first and copy its shape.

- [ ] **Step 3: Build and run it against a live process**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target metrics -j 8 2>&1 | tail -3
# against any running GPU client; if starbound is not up, any GL app will do
cd dist && ./metrics --pid $(pgrep -x starbound | head -1) --for 5
```

Expected: a table naming each engine, its busy nanoseconds and ratio, with `measures` and `valid` shown on every row. On a healthy idle desktop the render ratio is well under 1.0. **A ratio above 1.0 means the dedup regressed** — that is the original bug's signature.

- [ ] **Step 4: Verify the failure path exits non-zero**

```bash
cd /root/frackin/OpenStarbound/dist && ./metrics --pid 2147483647 --for 1; echo "EXIT=$?"
```

Expected: a message naming the missing process, and `EXIT=3` — not `EXIT=0` with zeros.

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/metrics/metrics_main.cpp source/metrics/CMakeLists.txt
git commit -m "GM-1: the metrics CLI -- measures a process from outside it

The sovereignty proof: the target needs no instrumentation, so this works against
the shipped game and cannot perturb what it reads. A ratio above 1.0 would mean
the fd dedup regressed; that is the original bug's signature. [#229]"
```

---

### Task 8: Mutual validation on hardware

**The keystone.** Two independent kernel paths to one physical quantity, required to agree. Every defect in the spec's §1 was caught by an independent instrument disagreeing; this makes that automatic rather than lucky.

**Files:**
- Create: `scripts/metrics-mutual-check.sh`

- [ ] **Step 1: Write the check**

```bash
#!/usr/bin/env bash
# Two kernel paths to one quantity, required to agree.
#
# ClientBusyReader (fdinfo, per-client) and EngineBusyReader (i915 PMU, system-wide) measure GPU
# render-engine busy time by different mechanisms. With exactly ONE GPU client running they measure
# the same thing, and must agree. Disagreement between independent instruments is how every defect in
# the GPUTIMER series was actually found -- this makes it a check rather than a coincidence.
#
# Needs CAP_PERFMON or perf_event_paranoid <= 0 for the PMU arm. Where that is unavailable the check
# SKIPS LOUDLY: it never reports agreement it did not observe.
set -u
cd "$(dirname "$0")/.." || exit 2
PID=${1:-$(pgrep -x starbound | head -1)}
[ -z "$PID" ] && { echo "metrics-mutual-check: no target pid"; exit 2; }

OUT=$(dist/metrics --pid "$PID" --for 20 --json) || { echo "metrics-mutual-check: client reader failed"; exit 1; }
CLIENT=$(printf '%s' "$OUT" | python3 -c '
import json,sys
d=json.load(sys.stdin)
print(next(s["value"] for s in d["samples"] if s["key"]=="gpu.engine.render.busy_ratio"))')

PMU=$(python3 scripts/pmu-render-busy.py --for 20 2>/dev/null)
if [ -z "$PMU" ]; then
  echo "metrics-mutual-check: SKIPPED -- i915 PMU unavailable (perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid))"
  echo "  NOT a pass: the cross-check did not run."
  exit 0
fi

python3 - "$CLIENT" "$PMU" <<'PY'
import sys
c,p=float(sys.argv[1]),float(sys.argv[2])
d=abs(c-p)
# The bound is MEASURED when this lands, not guessed -- an unwatched tolerance is an untested one
# (GATE-TOLERANCE-1, #223). Record the observed spread in the commit that sets it.
BOUND=0.05
print("  fdinfo per-client render busy ratio : %.4f" % c)
print("  i915 PMU render busy ratio          : %.4f" % p)
print("  |difference|                        : %.4f (bound %.4f)" % (d,BOUND))
sys.exit(0 if d<=BOUND else 1)
PY
```

- [ ] **Step 2: Write the PMU sampler it calls**

Create `scripts/pmu-render-busy.py` — a ~30-line `perf_event_open` reader via `ctypes` against `/sys/bus/event_source/devices/i915` (`type` from `type`, `config` from `events/rcs0-busy`), printing the busy ratio over `--for` seconds, or nothing at all on failure. The C++ `EngineBusyReader` is the shipped path; this script exists so the cross-check does not test the C++ reader against itself.

- [ ] **Step 3: Run it**

```bash
cd /root/frackin/OpenStarbound
chmod +x scripts/metrics-mutual-check.sh
scripts/metrics-mutual-check.sh; echo "EXIT=$?"
```

Expected on this host: `SKIPPED -- i915 PMU unavailable (perf_event_paranoid=2)` and `EXIT=0`, with the explicit "NOT a pass" line. Run it again under `sudo` to exercise the comparing path, and **record the observed difference in the commit message** — that observation is what sets `BOUND`, replacing the placeholder 0.05.

- [ ] **Step 4: Commit**

```bash
cd /root/frackin/OpenStarbound
git add scripts/metrics-mutual-check.sh scripts/pmu-render-busy.py
git commit -m "GM-1: mutual validation -- two kernel paths, one quantity, must agree

fdinfo per-client and i915 PMU system-wide measure GPU render busy by different
mechanisms; with one GPU client they must agree. Every defect in the GPUTIMER
series was caught by an independent instrument disagreeing. This makes that a
check rather than luck. Skips LOUDLY without CAP_PERFMON -- it never reports
agreement it did not observe. [#229]"
```

---

### Task 9: Close out — whole gate set, and the module's own docs

**Files:**
- Create: `docs/metrics/README.md`
- Modify: `.github/workflows/gates.yml`

- [ ] **Step 1: Write `docs/metrics/README.md`**

One page: what `source/metrics/` is for, the two readers and how they differ in scope, the dedup rule with the four-fd example, the never-return-zero rule, and how to run the CLI and the mutual check. Link the design spec.

- [ ] **Step 2: Register the mutual check as a gate**

Add to `.github/workflows/gates.yml`, following the shape of the existing `render_gate_verdicts` step:

```yaml
      # Two kernel paths to one quantity. Skips loudly without CAP_PERFMON rather than reporting an
      # agreement it never observed -- a check that cannot run must not read as a pass.
      - name: metrics_mutual -- the two kernel readers agree, or say why they could not compare
        run: scripts/metrics-mutual-check.sh
```

- [ ] **Step 3: Run the whole gate set**

```bash
cd /root/frackin/OpenStarbound
scripts/ci/run-gates.sh > /tmp/gates.log 2>&1; echo "EXIT=$?"; tail -3 /tmp/gates.log
```

Expected: `EXIT=0` and `run-gates: OK -- 30/30 gates green`. If `arch_graph_fresh` is red, run `python3 scripts/arch-graph.py --inject docs/architecture/system-boundaries.md` and re-run — new source files change the generated mass/tree blocks.

- [ ] **Step 4: Run the full test suite**

```bash
cd /root/frackin/OpenStarbound/dist && ./core_tests 2>&1 | tail -5
```

Expected: all tests pass, including the 11 new `MetricSampleTest` / `ClientBusyReaderTest` / `BusyDeltaTest` / `EngineBusyReaderTest` cases.

- [ ] **Step 5: Commit**

```bash
cd /root/frackin/OpenStarbound
git add docs/metrics/README.md .github/workflows/gates.yml docs/architecture/system-boundaries.md
git commit -m "GM-1: docs + register the mutual-validation gate

30/30 gates green. [#229]"
```

---

## Self-review

**Spec coverage.** §4.1 home and register → Task 1. §4.2 `MetricSample` → Task 2; `ClientBusyReader` → Task 3; `EngineBusyReader` → Task 6; CLI → Task 7. §4.3 data flow (`busyDelta`, no reaching into the target) → Tasks 5, 7. §4.4 error handling: PMU unavailable → Task 6; fdinfo absent → Task 4; PID exits → Task 5; backwards counter → Task 5. §4.5 tests 1–5 → Tasks 2–6; test 6 mutual validation → Task 8. §4.6 out-of-scope items have no tasks, correctly.

**Two gaps found and closed while reviewing.** The mutual check originally compared the C++ `EngineBusyReader` against the C++ CLI — both this module's code, which is testing an instrument against itself, the exact failure this module exists to prevent. Task 8 Step 2 now adds an independent Python PMU sampler. And the `BOUND` in Task 8 began as a guessed 0.05; it is now explicitly a placeholder to be replaced by an observed spread, per GATE-TOLERANCE-1.

**Type consistency.** `BusyReading{available, unavailableReason, engineNs, clients}` is used identically in Tasks 3–7. `busyDelta(a, b, wallNs)` is declared in Task 5 Step 3 and used in Task 7. `ClientBusyReader::read(int)` / `::readFdinfoDir(String)` and `EngineBusyReader::open(String)` / `::sample()` match between headers and call sites.

**Known risk the implementer must expect.** The exact Star API spellings (`File::dirList` return shape, `String::splitAny`, `StringMap::maybe`, `Time::monotonicNanoseconds`, `Thread::sleep`) are written from convention, not verified against these headers. Expect to correct spellings at first compile; the logic and the tests are the load-bearing parts, not the API names.
