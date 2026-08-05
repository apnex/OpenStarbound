# Sovereign metrics: design

**Status:** approved (Director, 2026-08-05). GM-1 is specified here; GM-2..GM-5 are scoped but not
designed.

**Goal.** A metrics subsystem that owns measurement as a duty, states what each number physically is
and when it is valid, and can be checked against an instrument that does not share its assumptions.

---

## 1. Why this exists

A day of GPU work was spent on two numbers that were both artefacts.

**The 4.04x parallax step.** `render.pass.parallax.gpu_us` appeared to step 4.04x at t=31.1min and
stay there, with a static camera, unchanged cache counters, zero GL errors, and no other metric
moving. It read as a renderer defect. It was the meter: the GPU-timer bracket straddled the cache
gate, so 87% of records were empty ~0us brackets; not-ready query results were discarded silently
and the discard is magnitude-correlated; and the query ring depth (3) aliased against the refresh
cadence (3), phase-locking the one expensive sample to a single slot so its capture went
all-or-nothing. The histogram settled it: the heavy lobe never moved (2997 -> 3125us, 1.04x); only
the capture rate did (26% -> 98%). Fixed in GPUTIMER-1 (9b3c9428), gated in GPUTIMER-2 (dcb7daf6).

**The 96.8% GPU-busy figure.** Used to validate the corrected timers, and wrong by 4x:
`/proc/<pid>/fdinfo` repeats the same `drm-client-id` on every fd that dups the device, starbound
holds four, and the sampler summed them. It announced itself on the next run as 117.3% -- a single
engine cannot exceed 100%. Corrected in GPUTIMER-3 (8261ce4e).

**The +47% instrument cost.** Reported from a single ON cell that was the first run of a fresh
script -- cold shader cache, not instrument cost. Replicated properly across four locations, the
effect is -0.16pp: zero within noise, consistent with #172's independent +2.16%.

Three failures, one shape: **a number was quoted without an independent instrument that could
contradict it.** Each was caught only when something outside the measurement disagreed -- a
histogram, an arithmetic impossibility, a replication. None was caught by the oracles, because every
oracle was green throughout and a zero is a plausible number.

### The standing lesson

`render.pass.parallax.gpu_us` was never *broken* in the sense of failing. It reported a number whose
name said "GPU microseconds of the parallax pass" and whose value meant "elapsed span of a bracket
that may contain no work, sampled through a magnitude-biased filter". The gap between those two
sentences is the defect, and nothing in the system was capable of holding it.

**So the metric contract must carry the meaning, not the name.**

---

## 2. What the physics allows

Not a preference -- a constraint that eliminates otherwise attractive designs.

* Kernel-side accounting (i915 PMU, fdinfo) is exact, cheap, and needs no game instrumentation, but
  it is **aggregate over a window**. It cannot be split per-pass.
* Splitting per-pass requires markers **on the GPU timeline**: in-process queries, or hardware
  counters, or saturation.
* `GL_TIME_ELAPSED` around a pass measures the elapsed span between two markers on the GPU timeline,
  **idle included**. With the frame loop paced to 60fps (`TickRateApproacher(60.0f, 1.0f)`,
  `StarMainApplication_sdl.cpp:1386`) the GPU drains and waits inside every bracket, and the query
  counts the wait. Measured: per-pass timers sum to ~100% of the 16.2ms frame period while true GPU
  busy is 23.8%.

Therefore no out-of-process design can deliver per-pass attribution, and no in-process timer is
valid as a cost unless the GPU is saturated.

---

## 3. Architecture: split by validity, reconciled

Two tiers, with the boundary drawn at *what makes a number true* rather than at where the code lives.

| tier | source | measures | valid when | cost |
|---|---|---|---|---|
| **Truth** | i915 PMU + fdinfo, out of process | GPU engine busy time, whole process | always | zero in-game |
| **Split** | in-process GPU-timeline markers | per-pass share of GPU work | only under saturation | in-harness only |

Neither is trusted alone. The **reconciliation rule** is the system's backbone: a per-pass breakdown
is reported only if its sum agrees with the live kernel-side total within a stated bound. A run that
fails reconciliation is REJECTED, not reported with a caveat. This is exactly what was missing when
#141's frozen-ship result ("parallax costs exactly zero") was generalised to live play.

GM-1 delivers the Truth tier and the contract. The Split tier is GM-4.

---

## 4. GM-1 scope

### 4.1 Home

New directory `source/metrics/`, carried as a row in the component register that drives
`scripts/tree-map.py` (the `tree_map` gate fails otherwise -- `source/` currently has exactly six
registered members). Named `metrics`, not `gpumetrics`: the descriptor model already carries
`MetricDomain::Cpu`, and `source/core/StarTelemetry.*` is the natural later tenant of this directory.

**Dependency rule:** `metrics` depends on nothing above `core`. The renderer does not link it, cannot
perturb it, and does not know it exists. This is checked by the existing layering gate, not by
intent.

### 4.2 Components

**`MetricSample`** -- a reading that cannot be written without its meaning:

```
struct MetricSample {
  String   key;
  double   value;
  String   unit;         // "ns", "ratio", "hz"
  String   measures;     // the PHYSICAL QUANTITY, e.g. "render engine busy time, whole process"
  String   validWhen;    // the CONDITION, e.g. "always" / "GPU saturated" / "single GPU client"
  String   source;       // "i915-pmu:rcs0-busy" / "fdinfo:drm-engine-render"
  int64_t  tMonotonicNs;
};
```

`measures` and `validWhen` are **required, non-empty**. A sample constructed without them does not
compile. This is the whole point: it makes the parallax lie unwritable.

**`ClientBusyReader`** -- reads `/proc/<pid>/fdinfo/*`, **deduplicating by `drm-client-id`**, summing
one entry per client per engine. Unprivileged. Reports per-engine (`render`, `copy`, `compute`,
`video`) busy nanoseconds.

**`EngineBusyReader`** -- i915 PMU via `perf_event_open` against `/sys/bus/event_source/devices/i915`
(type 15 on this host): `rcs0-busy`, `actual-frequency-gt0`, `rc6-residency-gt0`. System-wide, so it
sees GPU work this process did not cause -- which is why it is a cross-check on, not a replacement
for, the per-client reader. Requires `perf_event_paranoid <= 0` or `CAP_PERFMON`; this host is at 2,
so it is privileged-only and MUST degrade honestly.

**`metrics` CLI** -- `metrics --pid <N> --for <secs> [--json|--openmetrics]`. The sovereignty proof:
if it can measure the **shipped, uninstrumented** game, the module genuinely does not depend on us.
This is a capability the current system does not have at all.

### 4.3 Data flow

```
  t0: sample(pid) ─┐
                   ├─> delta / wall_ns ─> busy ratio ─> MetricSample{measures, validWhen}
  t1: sample(pid) ─┘                                          │
                                                              ├─> JSON  (canonical record)
                                                              └─> OpenMetrics (GM-3)
```

Frame count is *not* read from the game: `--for` yields a busy ratio, and us/frame is derived only
when a frame count is supplied explicitly. The reader never reaches into the process it measures.

### 4.4 Error handling

**The rule: a reader that cannot read says so. It never returns zero.**

`render.pass.compose.gpu_us` reported 0us for its entire existence because the dim overlay never ran
and an empty bracket looks exactly like free work. Every unavailability is therefore explicit:

* PMU unavailable (paranoid level, no `CAP_PERFMON`, no i915) -> `unavailable("perf_event_open: EACCES; perf_event_paranoid=2")`
* fdinfo absent, or no `drm-engine-*` lines (non-Intel driver) -> `unavailable(reason)`
* PID exits mid-window -> the sample is discarded and reported discarded, not extrapolated
* counter goes backwards (client restarted) -> discard and report

The CLI exits non-zero when the requested source is unavailable. A caller cannot mistake "could not
measure" for "measured zero".

### 4.5 Testing

Unit, on fixtures, in CI, no GPU required:

1. **The 4x bug, pinned.** A synthetic `fdinfo` tree with four fds sharing one `drm-client-id` must
   yield 1x the busy time, not 4x. This is the regression test for GPUTIMER-3.
2. **Multi-client.** Two distinct client-ids sum; the same client across fds does not.
3. **Unavailability is not zero.** Every failure path returns `unavailable`, and the CLI exits
   non-zero. Asserted, because this is the class that produced a metric reporting 0 forever.
4. **Backwards counter** is discarded, not reported as a huge delta.
5. **Schema.** A `MetricSample` cannot be constructed without non-empty `measures` and `validWhen`.

On hardware, in the harness:

6. **MUTUAL VALIDATION.** With exactly one GPU client running, `EngineBusyReader` (PMU, system-wide)
   and `ClientBusyReader` (fdinfo, per-client) measure the same physical quantity by different
   kernel paths. They must agree within a stated bound. **This is the check the old system never
   had.** Two independent instruments disagreeing is how every defect in §1 was actually caught, and
   this makes that disagreement automatic rather than lucky.

The bound is measured and recorded when the test is written, not guessed -- an unmeasured tolerance
is an untested one (the GATE-TOLERANCE-1 lesson, #223).

### 4.6 Out of scope for GM-1

Stated so the boundary is a decision and not an omission: no per-pass attribution (GM-4/GM-5), no
schema retrofit of existing metrics (GM-2), no OpenMetrics or socket transport (GM-3), no changes to
the renderer's GPU timers beyond what GPUTIMER-1/2 already landed, no frame-pacing change.

---

## 5. Roadmap

| | sub-project | delivers |
|---|---|---|
| **GM-1** | sovereign core, kernel readers, CLI | trustworthy absolute GPU cost for any PID |
| **GM-2** | versioned schema + conformance gate | `measures`/`validWhen` mandatory across all metrics |
| **GM-3** | exporters: OpenMetrics + IPC socket | external tooling consumes without linking us |
| **GM-4** | in-process scope timing, corrected | disjoint-query checking; saturated frozen replay |
| **GM-5** | ablation harness | the standing per-pass budget, reconciled against GM-1 |

**GM-4 carries a known unfixed hazard:** `GL_EXT_disjoint_timer_query` exposes `GL_GPU_DISJOINT_EXT`,
which reports when a timer result was invalidated by frequency change or preemption. Our code has
never checked it, and this GPU clocks between 933MHz and 2350MHz. Every historical `GL_TIME_ELAPSED`
sample may have been silently disjoint.

**GM-5 has a known limit:** ablation cannot attribute the world pass, because disabling it does not
leave a comparable scene. The budget it produces will be partial, and must say so.

---

## 6. Evidence

Measured this session, on this hardware (Intel Arc Pro 130T/140T, Arrow Lake-P, i915), at
2560x1440, zoom 3, shipped default config, GL timers OFF, N=3:

| location | GPU busy | us/frame | spread |
|---|---|---|---|
| Surface Outpost | 23.83% | 3861 | ±5.0% |
| Ocean-Lab | 16.52% | 2676 | ±1.3% |
| Lava Refinery | 15.11% | 2448 | ±0.7% |
| Ship | 14.83% | 2402 | N=2, PROVISIONAL |
| Ocean Factory | 12.56% | 2034 | ±1.0% |

Ship is marked provisional because its third repeat had not completed when this was written. It is
recorded rather than omitted, and marked rather than rounded into the others -- an N=2 cell reported
as though it were N=3 is how the "+47% instrument cost" claim happened.

Instrument cost (timers ON minus OFF), four locations: -0.83, +0.28, +0.21, -0.30pp. Mean -0.16pp.

Surface Outpost's relative spread falls from ±27-55% (P6, defective instrument) to ±5%. Most of the
"surface non-stationarity" that motivated a 45-minute time-series investigation was the meter.

**Superseded by this document:** every per-pass GPU figure taken before 9b3c9428, and any claim
resting on `render.frame.gpu_span_us` as a measure of GPU work.
