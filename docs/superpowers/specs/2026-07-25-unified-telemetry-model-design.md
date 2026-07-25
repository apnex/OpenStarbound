# Unified CPU + GPU Telemetry Model — Design

**Status:** approved by the Director 2026-07-25 (sections 1–3 approved in sequence).
**Task:** #162–#165. **Supersedes nothing.** Related: [[render-harness]], task #141 (P-5, the GPU trunk).

## 1. Problem

The live render-profile harness (`scripts/render-profile.sh`, `15641226`) can A/B GPU load unattended to ~1%.
It cannot say what a lever costs on CPU, nor whether the frame was limited by the resource the lever moved.
Render optimisations are triaged and traded against CPU, so half the evidence is missing.

The gap is structural, not a missing metric. Four defects in today's telemetry, each with evidence from the
2026-07-25 session:

**D1 — The CPU side has no whole and no denominator.** Walking the real frame loop
(`StarMainApplication_sdl.cpp:728-784`), almost nothing is timed: `processEvents`, `platformServices->update`,
`m_application->update()` (the entire client sim tick), `startFrame`, `finishFrame`, ImGui render,
`SDL_GL_SwapWindow`, and `sleepPrecise` are all untimed, and no metric measures loop-iteration wall time.
`render.frame.us` covers only the in-world render portion. The GPU side got its whole from P-5
(`render.frame.gpu_span_us`); CPU never got the equivalent.

**D2 — CPU and GPU are structurally asymmetric but share one flat namespace.** CPU samples arrive via
`Telemetry::timer(k).record()` / `TelemetryScope`; GPU samples arrive via a separate `GpuTimer` object
(`StarRenderDiagnostics.hpp`) using `GL_TIME_ELAPSED`, and are then forwarded into the *same* flat `timers`
bucket (`StarRenderer_opengl.cpp:1066`). The only discriminator between "600 µs of core" and "600 µs of GPU"
is a **filename suffix** (`.gpu_us` vs `.us`), which `telemetry-window.py` pattern-matches with
`k.endswith(".gpu_us")`. A naming convention is doing a type system's job.

**D3 — Ownership and coverage are not in the data.**
- Thread ownership had to be recovered by grepping source: `tick.server.*` belongs to `WorldServerThread`.
- Ownership is *config-dependent*: `m_asyncLighting` defaults false (`StarWorldClient.cpp:61`), and when off
  `lightingCalc()` runs inline on the main thread (`:574`). The same metric belongs to a different thread
  depending on a runtime flag.
- `render.frame.gpu_span_us` samples ~67% of frames, because readback is deliberately non-blocking and ~3
  frames late. Nothing in the snapshot says so. **This caused a real error:** dividing pass totals by the
  gpu_span *sample* count instead of the *frame* count inflated every per-frame figure ~1.5× and made the
  parts sum to **119% of the whole** — a finding about arithmetic wearing the costume of a finding about the
  renderer.

**D4 — Means only, and no self-validation.** Every metric reports count/total/min/max, and `max` is a running
high-water mark that is **not windowable**. So a lever that improves mean frame time 5% while doubling p99
reports as a clean win. This project's hardest bugs — flicker, hitching, `lightingPromoteMinIntensity`,
temporal lighting decoupling — have all been *episodic*, not average. Meanwhile the render subsystem has
oracles that fail loudly and the telemetry subsystem has none, despite two of this session's measurement
errors being denominator/aggregation mistakes.

## 2. The metric model

**Identity is declared at registration, never inferred at sample time.**

This is forced by D2: GPU query results are read back and recorded **by the main thread**, ~3 frames after the
GPU did the work (`StarRenderer_opengl.cpp:1050-1067`). Any scheme that stamps the *recording* thread — a
thread-local lookup, the obvious design — would label every GPU sample "main thread". Declaration is both
correct and cheaper: zero hot-path cost.

Four fields, declared once when a metric is registered:

| field | values | meaning |
|---|---|---|
| `domain` | `cpu` \| `gpu` | Which resource was consumed. Never addable across values. Replaces the `.gpu_us` suffix convention. |
| `owner` | `frame` \| `sim` \| `lighting` \| `gl` \| `process` | Which **logical budget** the sample belongs to. |
| `cadence` | `frame` \| `tick` \| `recompute` \| `call` | Which of the owner's tick counters this metric's `count` should be checked against. Not used for the per-frame share, which is always `total ÷ frames`. |
| `role` | `total` \| `budget` \| `detail` | `total` **is** the owner's whole; `budget` parts close against it; `detail` hangs off a budget part and is never summed. |

### Why `owner` is logical, not a physical thread

A logical owner dissolves D3's config-dependency entirely. Whether `lightingCalc()` runs on its own thread or
inline on the main thread, it belongs to the **lighting** budget either way. Physical thread identity was
never the question being asked; "which budget does this cost land in" was. When lighting runs inline it is
*nested inside* `cpu.frame.update.us`, and that nesting is expressed by `role`, not by thread identity.

### Why `role` exists

`render.frame.us`, `render.interface.us` and `render.world.painter.us` all nest **inside**
`cpu.frame.render.us`. Summing them together double-counts. Only `role=budget` metrics participate in their
owner's closure check; `role=detail` metrics hang off them for attribution. Today's GPU "accounted" line sums
every `.gpu_us` metric and avoids double-counting only by the accident that `GL_TIME_ELAPSED` cannot nest —
on the CPU side the nesting is real and immediate.

### Owners declare a denominator *and* a total

These are two different things, and conflating them is a bug waiting to happen. The **denominator** counts the
owner's ticks (how many frames happened). The **total** is the whole that `role=budget` parts must close
against. For owner `frame` they happen to be the same metric read two ways; for owner `gl` they are different
metrics entirely — GPU work is *counted* per frame but its *whole* is the GPU frame span.

| owner | denominator (count of ticks) | total (the whole) |
|---|---|---|
| `frame` | `cpu.frame.total.us` (count) | `cpu.frame.total.us` (sum) |
| `gl` | `cpu.frame.total.us` (count) | `render.frame.gpu_span_us` (sum) |
| `sim` | `tick.server.seq` | — (no measured whole; parts reported unclosed) |
| `lighting` | `lighting.temporal.recomputed` | `lighting.cpu.total.us` (sum) |
| `process` | — | — (cumulative; windowed by differencing) |

An owner with no declared total reports its parts without a closure check rather than inventing one.

A consumer groups by owner, divides by that owner's own denominator, and **structurally cannot** produce a
cross-thread or cross-domain sum. The 119% error becomes unrepresentable rather than merely documented.

## 3. The frame budget

Owner `frame`, domain `cpu`, cadence `frame`, role `budget`, mapped onto `StarMainApplication_sdl.cpp:728-784`:

| metric | wraps |
|---|---|
| `cpu.frame.total.us` | loop iteration → next iteration. **The denominator.** |
| `cpu.frame.input.us` | `cleanup` + `processEvents` + `processInput` + `platformServices->update` + ImGui `NewFrame` |
| `cpu.frame.update.us` | the whole `updatesBehind` loop — every client sim tick this frame |
| `cpu.frame.render.us` | `startFrame` + `application->render()` |
| `cpu.frame.finish.us` | `finishFrame()` — GL flush + blit |
| `cpu.frame.swap.us` | `SDL_GL_SwapWindow` — **the bound verdict** |
| `cpu.frame.idle.us` | `sleepPrecise(spareTime)` — headroom |

Plus counter `cpu.frame.updates` (owner `frame`, cadence `frame`): frame-skip runs `update()` 1..N times per
frame, so a CPU regression can hide as *more skipping* rather than more time. Without the count it is invisible.

`unattributed = total − Σ(role=budget)`, reported explicitly, exactly as the GPU table already does.

### Work versus wait

A blocked main thread is billed as frame cost — correct for frame time, wrong for "what do I optimise".
Explicit wait timers at the known handoffs, `role=detail`, owner `frame`:

- `cpu.wait.lighting.us` — around `worldClient->waitForLighting()` (`StarClientApplication.cpp:544`).

Further wait timers are added where a handoff is identified; this is targeted instrumentation, not a general
tracing system.

### Context that stops a number being misread

Environment facts go in `meta` (they describe the run, and are read once at snapshot time); measurements go in
`metrics` (they are windowed by the generic differencing path, with no special case in the consumer):

- **`meta.vsync`** — `swap.us` means *GPU backpressure* with vsync off and *frame pacing* with vsync on. Same
  metric, opposite meanings. Putting vsync in the data lets the consumer resolve it instead of the reader
  having to remember which run this was.
- **GPU clock and package temperature** — sampled by the **harness**, not the engine, and merged by the
  consumer. The measured ~7% drift between distant profile runs is currently explained as "thermal state and
  differing sim content", which is a guess; with clocks alongside the window it becomes an answer. These live
  outside the engine deliberately: the paths are Linux- and driver-specific (`/sys/class/drm/card*/…` differs
  between the i915 and xe drivers, `/sys/class/hwmon/…` differs by platform), and a game engine has no
  business growing sysfs-scraping for a profiling convenience. `render-profile.sh` writes a
  `env-sidecar.json` next to the snapshots; `telemetry-window.py` merges it into the reported `meta`.
- **`cpu.process.total_us`** — a *metric* (counter, domain `cpu`, owner `process`, role `budget`), not a meta
  field: cumulative process CPU across all threads (`getrusage(RUSAGE_SELF)`), refreshed at snapshot-write
  time. As a counter it is differenced by the same generic windowing path as everything else, yielding core-µs
  consumed in the window — the core-contention blind spot a single-thread budget cannot see.

### The bound verdict

Derived by the consumer, not hard-coded in the engine:

- vsync off and `swap` ≫ 0 → CPU is waiting on the GPU: **GPU-bound**.
- `idle` ≈ 0 and `swap` ≈ 0 → the CPU is the wall: **CPU-bound**.
- healthy `idle` → neither; the frame has headroom.

## 4. Distribution

Every timer carries a **histogram**: 64 buckets, HdrHistogram-style — octave plus two sub-bits.

- Bucket index: `i = 4·floor(log2(µs)) + (the two bits below the MSB)`, clamped to `[0, 63]`. Bucket `i`
  covers `[2^h · (1 + m/4), 2^h · (1 + (m+1)/4))` for `h = i/4`, `m = i%4`. That spans 1 µs → 65535 µs ≈ 65 ms:
  the right range for frame timing, from a cheap pass to a visible hitch. Anything ≥ 65536 µs lands in the top
  bucket, which is exactly the "something went very wrong" signal.
- **Integer-only, no floating point**: one `__builtin_clzll` plus a shift and mask. This matters because it
  runs on the hot path.
- Storage: 64 × `uint64` per timer ≈ 512 B; at ~40 timers ≈ 20 KB total.
- Cost: one relaxed atomic increment plus the index computation, ~2 ns, gated behind
  `Telemetry::deepEnabled()` like every other timer sample.
- Buckets are cumulative counters, so **differencing two snapshots windows them** — which makes p50/p95/p99/
  p99.9 windowable, and makes a windowed `max` available as the highest non-empty bucket.
- Resolution: bucket width is at worst 25% (at the bottom of an octave) and 14% at the top, so a percentile
  estimate is accurate to ≤ ±12.5%. Ample to detect "p99 doubled", which is the question being asked.

This is the change that most improves what can be reasoned about. Without it, no smoothness claim about any
lever is supportable — only an average one.

## 5. Snapshot schema

One unified `metrics` map replaces the four type-keyed buckets, so CPU and GPU are described identically:

```json
{
  "meta": {
    "schema": 2,
    "vsync": false,
    "gpuClockMhz": 1300,
    "packageTempC": 72
  },
  "owners": {
    "frame":    { "denominator": "cpu.frame.total.us" },
    "gl":       { "denominator": "cpu.frame.total.us" },
    "sim":      { "denominator": "tick.server.seq" },
    "lighting": { "denominator": "lighting.temporal.recomputed" }
  },
  "metrics": {
    "cpu.frame.total.us": {
      "type": "timer", "domain": "cpu", "owner": "frame", "cadence": "frame", "role": "budget",
      "count": 10800, "total": 34668000, "min": 1200, "max": 18400,
      "buckets": [0, 0, 12, 340]
    },
    "render.pass.world.gpu_us": {
      "type": "timer", "domain": "gpu", "owner": "gl", "cadence": "frame", "role": "budget",
      "count": 7601, "total": 6882447, "min": 0, "max": 2449,
      "buckets": [0, 4, 91]
    },
    "render.cache.env.skipped": {
      "type": "counter", "domain": "cpu", "owner": "frame", "cadence": "frame", "role": "detail",
      "value": 8100
    }
  }
}
```

`buckets` is emitted trimmed of trailing zeros. Counters and gauges carry the descriptor fields but no
histogram.

## 6. Self-test — the instrument's oracle

The render subsystem has oracles that fail loudly; telemetry has none, and two of this session's measurement
errors were exactly the class an oracle catches. Added to `source/test/telemetry_test.cpp`:

1. **Budget closure** — for each owner and domain, `Σ(role=budget) ≤ owner total` and `unattributed ≥ 0`.
   In the unit test the samples are injected, so closure is asserted **exactly**. On real data
   (`telemetry-window.py`) the remainder must lie in `[0, 25%]` of total: negative means double-counting or a
   mis-declared `role`, and a remainder above a quarter of the frame means the loop instrumentation missed a
   phase. This is the 119% error as a failing test.
2. **Cadence consistency** — a metric declared `cadence=frame` must satisfy `count ≤ denominator count`.
   *Under* is legitimate and expected — a refresh-gated pass or an async GPU readback samples only some frames,
   which is exactly what the coverage figure reports. *Over* is always a bug: it means the span is opened more
   than once per frame and the metric should have been declared `cadence=call`.
3. **Histogram consistency** — `Σ(buckets) == count` for every timer.
4. **Domain isolation** — the consumer-facing aggregation helper refuses to sum across `domain` or `owner`;
   asserted by test, so the guarantee is enforced rather than documented.

`telemetry-window.py` runs assertions 1 and 3 on real snapshots and reports a violation loudly rather than
printing a plausible number.

## 7. Consumers and migration

Only **two** consumers read the snapshot JSON: `StarTelemetryReporter` (the writer) and
`scripts/telemetry-window.py`. The `/telemetry` HUD is **unaffected** — it reads counters directly by name
(`StarClientApplication.cpp:616-627`), not the snapshot.

Migration:
- All existing metric registrations gain their four declarations. Mechanical, one line each, ~40 sites.
- `telemetry-window.py` reads `meta`/`owners`/`metrics`, drops the `.gpu_us` suffix test and the hard-coded
  denominator, and gains percentile columns.
- `meta.schema: 2` lets the window tool reject a pre-change snapshot rather than silently mis-window it.
- Numbers captured before the change do not line up with ones after. In practice this costs little: every
  number quoted is windowed within a single session, and cross-session absolutes were already invalid
  (~7% run-to-run drift).

## 8. Testing

- **Unit** (`core_tests`, `telemetry_test.cpp`): the four self-test assertions above; histogram bucket
  boundary cases (0 µs, 1 µs, 65 ms, overflow); registration-time declaration immutability; snapshot schema
  shape.
- **Integration**: a live profile run must show budget closure on real data — `cpu.frame.*` parts summing to
  `cpu.frame.total.us` with a small positive unattributed remainder. A negative or wildly large remainder
  means the loop instrumentation missed a phase.
- **Non-regression**: telemetry is non-functional, so the render gate (`scripts/render-gate.sh`) must remain
  byte-identical across the change. Any pixel difference means instrumentation perturbed rendering.
- **Cost**: with deep tracing off, a profile run before and after must be within the measured ~1% noise floor.
  With deep tracing on, the added cost is reported rather than assumed.

## 9. Out of scope, deliberately

- **Per-frame time series** (CSV ring buffer). Powerful for episodic problems, but histograms cover most of
  that need at a fraction of the build. Earn it when a specific episodic bug demands it.
- **Memory / allocation axis.** Render levers do trade time for churn, but that is a separate arc.
- **Per-thread budgets for `sim` and `lighting`.** The model supports them; the metrics are deferred until a
  lever shows *"frame time didn't move but process CPU dropped"*, which is the evidence that earns them.
- **Attributing the ~600 µs unattributed GPU.** A finding to chase, not a telemetry feature.
- **Input injection** for the harness. Unrelated, and the reason motion-dependent levers remain out of reach.
