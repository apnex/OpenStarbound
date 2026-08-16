# Telemetry — subsystem architecture

**Status:** current as of 2026-07-25. Companion to `docs/render/`, which documents the rendering layers this
instrument measures.

---

## 1. What this is, and what it is not

Telemetry is a **sovereign, fork-owned subsystem**. It is not vanilla Starbound and it is not upstream
OpenStarbound.

Verified 2026-07-25: `origin/main` — our mirror of upstream — contains **no telemetry files whatsoever** in
`source/core/`. The first commit is our own `fd9e398c` (2026-06-08, *"feat(telemetry): substrate registry +
Counter/Gauge + snapshot + reset"*), from the GPU-ladder Rung 0 work. Fourteen commits, all ours.

It is also **deliberately not upstreamable**. Unlike the render-surface work — where fixes are drafted for
upstream in `docs/upstream/` — this exists to serve this fork's performance campaign. That is freedom, not a
loss: no API-compatibility constraint, no divergence pressure, and a schema break costs us almost nothing
because we own every consumer.

### The division of labour with `LogMap`

Vanilla's only instrument is `LogMap` (`source/core/StarLogging.hpp`, which **is** upstream). Both are kept,
because they answer different questions:

| | `LogMap` (vanilla) | `Telemetry` (ours) |
|---|---|---|
| answers | *what is it right now* | *what did it cost over this window* |
| shape | one string per key, overwritten every frame | count / total / min / max / histogram, cumulative |
| destination | the on-screen debug HUD | a JSON file a script can difference |
| survives the frame | no | yes |

The render code still writes to both. Do not collapse them.

### Where it lives

| file | responsibility | lines |
|---|---|---|
| `source/core/StarTelemetry.hpp` | public contract: handles, `MetricDesc`, declaration | 176 |
| `source/core/StarTelemetry.cpp` | registry, lock-free value ops, snapshot, owner table | 486 |
| `source/core/StarTelemetryReporter.cpp` | JSON artifact face; process-CPU sample; `meta` merge | 60 |
| `source/test/telemetry_test.cpp` | the instrument's own oracle | 356 |
| `scripts/telemetry-window.py` | the only real consumer: windows, closes, reports percentiles | 332 |
| `scripts/render-profile.sh` | the live instrument (see §6) | 148 |
| `scripts/render-gate.sh` | the frozen instrument (see §6) | 55 |

`source/application/StarRenderDiagnostics.hpp` is **not** part of this subsystem — it is the renderer's own
instruments contract (`GpuTimer`, `RenderOracle`). `GpuTimer` is a *producer* that feeds telemetry; telemetry
is the *sink*. That header exists because these were once seven virtuals on the `Renderer` contract itself,
which forced every backend to implement a pixel differ in order to draw a triangle.

---

## 2. The metric model

Every metric declares four fields. **Identity is declared at registration, never inferred at sample time.**

| field | values | meaning |
|---|---|---|
| `domain` | `Unknown` \| `Cpu` \| `Gpu` | which resource was consumed |
| `owner` | `Unknown` \| `Frame` \| `Gl` \| `Sim` \| `Lighting` \| `Process` | which **logical budget** it belongs to |
| `cadence` | `Call` \| `Frame` \| `Tick` \| `Recompute` | how often it **should** have fired |
| `role` | `Detail` \| `Budget` \| `Total` | whole, part of a whole, or neither |

`source/core/StarTelemetry.hpp:17,23,28,35,37`.

### Why declaration, not inference

Inference is not merely awkward here — it is **wrong**. GPU query results are read back and recorded by the
**main thread**, roughly three frames after the GPU did the work (`source/application/StarRenderer_opengl.cpp`,
the readback inside `GlGpuTimer::begin`). A thread-local scheme — the obvious design — would label every GPU
sample as CPU. Declaration is both correct and cheaper: it costs nothing on the sampling path.

### Why `owner` is a logical budget, not an OS thread

`WorldClient::lightingCalc()` runs on its own thread or **inline on the main thread** depending on
`m_asyncLighting` (`source/game/StarWorldClient.cpp:61`, `:574`). It belongs to the `Lighting` budget either
way. *Which budget does this cost land in* was always the question; *which thread ran it* never was.

### Why `Unknown` is the zero value of `domain` and `owner`

A default of `Cpu` would be a confident lie for any not-yet-declared GPU metric — exactly the mislabelling the
design exists to prevent, arriving by another route. **"Not yet declared" must be visible in the data, never
silently plausible.**

### Why `role` has three values

`Total` **is** the owner's whole and is excluded from the sum of parts. `Budget` parts sum and must close
against it. `Detail` nests inside a Budget part and is never summed — `render.frame.us`,
`render.interface.us` and `render.world.painter.us` all sit *inside* `cpu.frame.render.us`, so summing them as
siblings would double-count.

### Declaration must not create a node

`Telemetry::declare()` (`source/core/StarTelemetry.cpp:102`) **parks** the descriptor in `pendingDescs`
(`:80`) until the first typed accessor creates the node with the *real* type, which then adopts it.

The obvious implementation — create the node as a `Timer` and let the first accessor "correct" it — does not
work, because `getOrCreate` returns an existing node **without checking the requested type**. `declare()` then
`counter()` would yield a Timer node wrapped in a counter handle: `inc()` writes `node->counter` while
`snapshot()` dispatches on `type == Timer` and emits count/total/mean/min/max, **all zero**. That is not
hypothetical — `markTick` does exactly declare-then-counter on `tick.server.seq`, which is the declared
**denominator** for owner `sim`. A consumer dividing by a confident zero is the worst failure this subsystem
can have.

`typeConflict` (`:39`) is the defence-in-depth: two call sites requesting one key as different `MetricType`s
is a bug, and is now flagged rather than silent.

---

## 3. Owners declare a denominator *and* a total

These are different questions and conflating them is how a consumer divides GPU pass costs by the GPU span's
own sample count. The **denominator** counts the owner's ticks; the **total** is the whole that `role=Budget`
parts close against. `source/core/StarTelemetry.cpp:394`.

| owner | denominator (ticks) | total (the whole) |
|---|---|---|
| `frame` | `cpu.frame.total.us` (count) | `cpu.frame.total.us` (sum) |
| `gl` | `cpu.frame.total.us` (count) | `render.frame.gpu_span_us` (sum) |
| `sim` | `tick.server.seq` | `tick.server.total.us` (sum) |
| `lighting` | `lighting.temporal.recomputed` | `lighting.cpu.total.us` (sum) |

For `frame` they are one metric read two ways. For `gl` they are **different metrics entirely** — GPU work is
*counted* per frame but its *whole* is the GPU frame span. An owner with no declared total reports its parts
unclosed rather than inventing a whole. `process` and `unknown` are absent on purpose: not budget-bearing.

### What "lighting CPU" is the total of

**`lighting.cpu.total.us` is the whole of owner `lighting`. It is NOT all the CPU this process spends on
lighting**, and the difference is most of it. Three costs sit under owner `frame`, on the render thread:
producing the light sources, scanning for the spread iteration count, and driving the GPU pass. At Desert Town
the last of those alone is **larger than the lighting thread and the producer side combined** (#171, #271).

The union of the two is named **`lighting.cpu.union.us`** and declared with `MetricDesc::whole` at each member
(#269). It is an **AGGREGATE, not a budget**, and the distinction decides how it may be read:

| | answers | value | can it fail? |
|---|---|---|---|
| a **budget** | who pays | its parts close against a MEASURED whole | yes — the gap IS the closure |
| an **aggregate** | what for | its value **is** the sum of its members | no — nothing independent to close against |

So declaring the union buys a definition, not a check. The check that makes it real is **membership**, held by
`scripts/metric-desc-lint.py --check-union-membership`: exhaustive (no member silently dropped) and
non-overlapping (nothing counted twice). Seven members, and the seventh place is the instructive one —
`lighting.produce.particles.us` is lighting CPU by any plain reading and is **not** a member, because it is
nested inside `lighting.produce.prep.us`, which is.

```
lighting.cpu.total.us          owner lighting  — a member AND that owner's own Total; both are true
lighting.produce.entities.us   owner frame     — the entity light-source walk
lighting.produce.prep.us       owner frame     — the handoff, INCLUDING lock acquisition
lighting.produce.adjust.us     owner frame
lighting.upload.us             owner frame     — CPU-lightMap fallback path only
lighting.gpu.spread_scan.us    owner frame     — runs BEFORE cpu_cost's scope, so disjoint from it
lighting.gpu.cpu_cost.us       owner frame     — the thirteen lighting.gpu.drive.* parts nest inside it
```

**No owner total changed to express this**, which is why it is expressible at all. A second `(frame, cpu)` Total
would silently overwrite `cpu.frame.work.us` in the owner table; the union spans owners and never becomes one.
Every member but the first is `role=Detail`, which is never summed into a budget, so no closure on this page
moves.

Summed over #171's two banked Desert Town legs (`docs/evidence/lighting-produce-first-reading.md`), the union is
**262.7–268.3 µs/frame**, of which `lighting.cpu.total.us` is **25.8–28.6%**. One member, `lighting.upload.us`,
is **ABSENT rather than zero** on both legs: GPU lighting was on, so the CPU-lightMap fallback never executed.
A member that did not run and a member that ran and cost nothing are different facts, and the union must be read
with that distinction intact.

#### Reading the published lighting percentages against it

Every lighting percentage published before #269 is denominated on **`lighting.cpu.total.us`**, not on the union.
None is wrong; each has a narrower reach than its summary sentence suggests.

- **#168's "closed 99.6%"** is the closure of `lighting.cpu.total.us` against its own nine phases — the equation
  in §7 below. It says nothing about whether that key is all of lighting, and it is not.
- **#168's "cut 16.9% by four levers"** is a cut to `lighting.cpu.total.us` per recompute at `00-Ocean-Lab`.
  The four levers (params cache, buffer ring, export order, branchless `floatToHalf`) act inside `lightingCalc`'s
  `params`/`export`/`convert` phases and change how buffers are filled, not how many cells exist — so no other
  union member can move, and **restating the denominator is the whole correction**. Its reach over the union is
  smaller in proportion, but *that* number is not derived here: the −16.9% is per-recompute at `00-Ocean-Lab`
  and the union's composition is per-frame at Desert Town. Dividing one by the other would be a cross-scene,
  cross-cadence quotient — a new error wearing a fix's clothes.
- **#170/#217's "−10.1% lighting CPU"** is `lighting.cpu.total.us` at `04-Ocean Factory`, and it is the one that
  **restating cannot repair**. The border lever cuts `lighting.calc.cells` by 27.8%, and two union members scale
  with cells — `spread_scan` (an O(cells) scan) and `cpu_cost` (repack is O(texels)). Both move with the lever
  and neither is in that A/B, so the union-denominated figure cannot be derived from −10.1%; it must be measured.
  The likely direction is that −10.1% **understates** the lever, since the largest member falls with it. That is
  a hypothesis with a sign, not a result.

### The `sim` owner: what its total is, and what it is not

`sim` had a denominator and **no total** until #175 — parts with no whole, so nothing it measured could be
closed and `tick.server.compute.us` sat at 98.4% of the four parts that existed. That figure was never an
attribution; it was the absence of one.

`tick.server.total.us` wraps the `WorldServerThread::run()` loop body **minus the pacing sleep**. Two properties
follow, and the second is the one people get wrong:

- **The sleep is excluded structurally, not by subtraction.** It is the last statement of the loop body and the
  body has exactly one control-flow path, so the scope simply closes before it. This avoids the
  `cpu.frame.total.us` trap on the client side, where the total is 65% sleep and an A/B on it reports no change
  for a real regression.
- **It is busy with respect to PACING, not with respect to BLOCKING.** Six lock acquisitions live inside it.
  Each is named `tick.server.lock.*`, so `busy = total − blocked` is recoverable, but the total itself is
  busy+blocked and must be read that way. An earlier draft of this design asserted the total was "busy by
  construction"; that was wrong, and on a contended multiplayer server it would be wrong by a lot.

`tick.server.lock.us` is a **latency** metric, not a cost metric. `readChunks`/`unloadAll` hold `m_mutex` across
another thread's disk work, so one acquisition can run for seconds — its p99 says how long the world stopped
ticking, not how much CPU anything used.

The 27 Budget parts partition the total exactly once. The sixteen inside `WorldServer::update` are `cadence=Call`
and their handles are declared at **file scope**: that function is entered only when `dt > 0 && !paused`, and a
block-scope static inside a function that is never entered does not merely go unrecorded — it never *registers*,
so the metric would be absent from the snapshot rather than present with a zero. Absent reads as "no such
phase". Measured at `00-Ocean-Lab`, 4807 ticks: **closure 99.76%**, 5.4 µs/tick unattributed, with
`compute.entities` at 1483 µs/tick (65.2%), `netsync` 9.7%, `liquid` 8.5%, `wiring` 5.2%, and all six locks
together 0.74 µs/tick (0.03%) in single-player.

### Cadence is arithmetic, not just a bounds check

A `count` below the expected tick count has **two causes needing opposite arithmetic**:

- **Sampling loss** — the work *happened*, we failed to observe it. `render.frame.gpu_span_us` fires every
  frame but its GL query resolves asynchronously, so only ~67% of readings land.
- **Genuine gating** — the work *did not happen*. A refresh-gated pass contributes nothing to a skipped frame.

```
expected       = tick count for THIS metric's own cadence   (not the owner's)
coverage       = count / expected
per_owner_tick = (total / coverage) / owner_ticks
```

Treating both the same way suppressed the *whole* more than its *parts* and produced a GL closure of **122%**.
With the rule above, the same capture reads **97.0%**. The consumer reports the **raw** coverage fraction, so a
scaled figure is always visible as scaled.

A metric may legitimately carry a cadence **different from its owner's** natural tick: `lighting.cpu.total.us`
times the whole `lightingCalc()` call, which happens per *frame*, while its parts sit inside the temporal gate
and fire per *recompute*. The owner's denominator sets the table's **unit**; each metric's cadence sets its
**own** expectation.

### The `lighting` owner: how a frame-cadence total closes against recompute-cadence parts

`lighting` is the only owner whose total and denominator differ in cadence, and it is the worked example for
the rule above. `lighting.cpu.total.us` opens *before* the temporal gate, so it accrues on every frame
including the ~26% the gate skips. Its parts sit *after* the gate and fire per recompute. Two Totals per owner
are **not representable** (`StarTelemetry.cpp`'s owner table is keyed by owner name — a second row silently
overwrites the first, and the consumer unpacks `total` as a scalar), so the closure is made exact instead by
giving the pre-gate work its own **frame-cadence Budget part**:

```
whole = R·T_recompute + S·T_skip
parts = R·(recompute parts) + S·prologue        R = recomputes, S = gate skips
```

`coverage_scale` scales each part against *its own* cadence, so the mixed-cadence parts list closes against the
frame-cadence whole exactly. Measured: **99.6% accounted, 1.8 µs/recompute unattributed** (from 47.6% before
the phases existed), with every part at 100% coverage.

**This closes `lighting.cpu.total.us` against its own phases, and that is its entire scope.** It is not a
statement about lighting CPU as a whole — that is `lighting.cpu.union.us`, of which this key is one of seven
members and, at Desert Town, **25.8–28.6%**. See §3, "What lighting CPU is the total of".

| phase | cadence | scaling law |
|---|---|---|
| `lighting.cpu.prologue.us` | **frame** | O(lights) — the temporal gate's signature sort; the only part paid on skipped frames |
| `lighting.cpu.params.us` | recompute | O(1) — asset lookup + JSON compose |
| `lighting.cpu.begin.us` | recompute | O(calcCells) — the cell-grid fill |
| `lighting.cpu.gather.us` | recompute | O(calcCells) |
| `lighting.cpu.lights.us` | recompute | **O(lights)** |
| `lighting.cpu.export.us` | recompute | O(calcCells) |
| `lighting.cpu.convert.us` | recompute | O(calcCells·3) |
| `lighting.cpu.calculate.us` | recompute | O(calcCells·passes) — ≈0 when GPU lighting is latched |
| `lighting.cpu.publish.us` | recompute | O(cells) + lock wait |

The denominators for those laws are `lighting.calc.cells` (the border-padded **calculation** region) and
`lighting.lights.sources`. Do not use `lighting.cells` for a per-cell figure — it is the **query** region, and
the calculation region is 4.375× larger at a 128×64 query window.

---

## 4. The threading contract

| state | discipline |
|---|---|
| counter / gauge / timer / rate **values** | lock-free, `std::memory_order_relaxed` |
| registration, declaration, `snapshot()`, `reset()` | guarded by `Registry::mutex` |
| `desc`, `declared`, `descConflict`, `typeConflict` | plain (non-atomic) — **only** touched under that mutex |
| `describe()` | takes the mutex; **off the hot path** |

**Handle stability is unconditional.** `MetricNode*` handles stay valid because the registry is a node-based
`StableHashMap` (elements never move on rehash) *and* each node is a heap-allocated `unique_ptr` (address-stable
regardless of the map). Either alone would suffice; together it is unconditional.

`Logger` calls never happen while holding the registry mutex — a conflict is captured inside the critical
section and logged after it closes.

**`TelemetryScope` reads the clock only when `Telemetry::deepEnabled()`.** With deep tracing off — the shipping
default — each scope is one relaxed atomic load and a stack store. That is what makes six scopes acceptable in
the game's main loop.

### The one documented RMW

`TelemetryReporter::writeSnapshot`'s `getrusage` block does `value()` → compare → `inc(delta)`. Each op is
atomic; the *sequence* is not. It is safe **only** because both production callers reach it on the client's
main thread. Adding a caller on any other thread requires a compare-exchange or fetch-max. This is written down
at the code, not left in a review.

---

## 5. Wire format (schema 2)

```json
{ "meta":    { "schema": 2, "vsync": false },
  "owners":  { "frame": { "denominator": "cpu.frame.total.us", "total": "cpu.frame.total.us" }, ... },
  "metrics": { "<key>": { "type","domain","owner","cadence","role","descConflict","typeConflict",
                          "count","total","mean","min","max","buckets":[...] } } }
```

One flat `metrics` map replaced the four type-keyed buckets, so CPU and GPU are described **identically**.
`buckets` is emitted trimmed of trailing zeros; the consumer zero-pads.

**The consumer is required to know nothing about the engine.** It reads `owners` and each metric's descriptor.
It must never pattern-match metric names — the pre-v2 consumer discriminated GPU metrics by the `.gpu_us`
filename suffix and hard-coded which metric counted frames, and got the denominator wrong.

Consumers: `TelemetryReporter` (writer) and `scripts/telemetry-window.py` (reader). The `/telemetry` HUD is
**not** one — it reads counters by name and is unaffected by schema changes.

Environment facts go in `meta` (read once, describe the run); measurements go in `metrics` (windowed by the
generic differencing path). GPU clock and package temperature are sampled by the **harness**, not the engine —
those sysfs paths are driver- and platform-specific and a game engine has no business scraping them.

---

## 6. Two instruments, and when each applies

| | `scripts/render-gate.sh` | `scripts/render-profile.sh` |
|---|---|---|
| world | **frozen** | **running** |
| answers | *is it identical?* | *is it faster?* |
| A/B | in-process, two legs, one run | two runs, `--set` flipped between |
| resolves | 1 pixel / 1 LSB | ~1% GPU load |
| Director needed | no | no |

Both boot offscreen on the **real** GPU — SDL3's `offscreen` driver yields GL 4.6 core on the Intel Arc via
Mesa/EGL, not llvmpipe. The axis is **frozen vs live**, not offscreen vs windowed.

### Resolution — and it is NOT uniform across metrics

Two identical **back-to-back** 60 s runs: whole-frame GPU span **0.59%**, spread pass **0.73%**, parallax
compose **0.79%**, world pass **2.9%**, spread µs/call **0.07%**.

But an **A-B-A replicate** (45 s legs, A and A2 two runs apart with a full restart between) shows the drift is
far larger for some metrics, and it is *not* the same for CPU and GPU:

| metric | replication error (A vs A2) | resolvable? |
|---|---:|---|
| `lighting.gpu.spread.gpu_us` | 2.2% | yes |
| `render.pass.world.gpu_us` | 0.2% | yes |
| `render.frame.gpu_span_us` | 1.5% | only for effects ≳5% |
| `render.pass.environment.gpu_us` | 5.4% | marginal |
| `render.pass.parallax.gpu_us` | 20.6% | **no** — the pass is ~30 µs, mostly noise |
| `cpu.frame.render.us` | **9.7%** | marginal |
| `cpu.frame.update.us` | **10.3%** | **no** |

**CPU metrics drift ~10% between non-adjacent runs — an order of magnitude worse than the GPU passes.** The
sim is live and its work varies even when the scene *content* counters (particles, lights, drawables, flushes)
all match within ±3%. Matching content is **not** evidence that CPU timings are comparable.

**Protocol, therefore:**
- **Compare adjacent legs only.** Runs minutes apart drift ~7% on thermal state and sim content.
- **Any CPU claim needs an A-B-A replicate.** Report the effect against the *measured replication error*, not
  against the back-to-back GPU noise floor. A single A→B pair is sufficient for a large GPU-pass effect and is
  **not** sufficient for CPU.
- This was learned the hard way on 2026-07-25: a spread-iteration A/B appeared to show a 16.6% CPU render win,
  which the A-B-A revealed as ~10% drift. The claim was retracted before it reached a decision.

### A per-pass GPU win does not imply a frame win

The same A/B: halving `lightingGpuSpreadIterations` cut `lighting.gpu.spread.gpu_us` by **45.3%** (replication
error 2.2% — unambiguous), and simultaneously made `render.pass.world.gpu_us` **12.4% slower** (replication
error 0.2% — also unambiguous). `render.frame.gpu_span_us` did not move beyond its own drift.

`GL_TIME_ELAPSED` brackets are **not additive**: each forces a sync, and shortening one pass redistributes
stalls into its neighbours. So per-pass µs is a diagnostic for *where* work is, not a currency you can bank.
**The whole-frame span is the only GPU number a lever's value should be argued from** — and on this lever it
says there is no frame-level win to bank.

**Cannot do: input injection.** The camera is stationary at a teleport bookmark, so any lever whose cost
appears only in motion — parallax moving-camera bypass, scroll-shift/texture-upload, traversal, combat — is
unreachable and still needs the Director in the chair.

---

## 7. Traps, with the evidence

Every one of these cost real time on 2026-07-25. They are recorded so they are not rediscovered.

**The denominator trap — it fired THREE times in one day.**
1. In `telemetry-window.py`: per-frame figures inflated ~1.5×, parts summing to **119% of the whole**, from
   dividing by the `gpu_span` *sample* count (~70% coverage) instead of the frame count.
2. As the `declare()`-guesses-a-type defect.
3. In the verification snippet written *specifically to catch that class of error* — **115%**, from dividing a
   full-coverage cumulative sum by a partial-coverage one.

> **Normalise per tick first. Never cumulative-sum over cumulative-sum.**

**`max` is a run-long high-water mark and is NOT windowable.** This is the entire reason histograms exist.

**A declare inside a config-gated branch is not a declaration.** It bit twice: `environment.compose` in the
`backdropComposeMerge == false` branch (defaults off); `parallax.gpu_us` inside `if (!parallaxCacheActive)`,
which never runs when `parallaxOracle` is on — and the render gate's own config has it on. **The blind spot was
precisely the intersection the two verification runs do not jointly cover**: the gate runs oracle-on but checks
only oracle diffs; the profile check runs oracle-off. *Now structurally impossible* — `GpuTimer::begin` takes
the descriptor (`source/application/StarRenderDiagnostics.hpp:48`), so a timing cannot be started without one.

**`cpu.frame.total.us` is the PACE, not the cost.** `Thread::sleepPrecise(spareTime)` runs even with vsync off.
Measured: total 16393 µs/frame of which **idle was 10619 — 65% of the frame is sleep**. A +1 ms regression moves
busy 5774→6774 and idle 10619→9619 while total reads 16393 **both times**: an A/B on total reports **NO CHANGE**
for a real regression. **Quote `busy = total − idle`.**

**Bucket 63 is unbounded above** — `[57344, ∞)`, not `[57344, 65536)`. Interpolating a midpoint inside it caps
every hitch at 65 ms, which is exactly the case histograms exist to expose. A percentile landing there is a
**lower bound** and renders as `>=`.

**The histogram checksum is SLACK, not exact.** `record()` bumps `count` first and `buckets` last, both relaxed
with no fence, so a live snapshot skews by up to one per in-flight thread, **in either direction**. Asserting
exact equality fires on correct data.

**Quiescence is a frozen-world concept** and never fires with the sim running.

**A conditional phase must WRAP its `if`, not sit inside it.** Three phases in `lightingCalc()` run under a
condition (`export`/`convert` under `lightingGpu`, `calculate` under `!skipCpuCalc`). A scope placed *inside*
the branch fires only when the branch is taken, so at `cadence=Recompute` the consumer reads the shortfall as
sampling loss and scales the total up — inventing cost for work that never happened. Placed *around* the `if`,
the phase fires every recompute, reports 100% coverage, and simply records ≈0 when the branch is skipped. Cost:
one predictable branch test. Benefit: **owner `lighting` is coverage-scale-free end to end**, so any coverage
below 100% is a real signal rather than something a reader must interpret. `cadence=Call` is the alternative
and is correct, but it prints `n/a` coverage and leaves the arithmetic to the reader.

Five metrics had the inverse of this wrong — declared at a cadence they never fire at. `lighting.gpu.cpu_cost.us`
and `lighting.upload.us` were `Frame` but fire only inside `if (lightMapUpdated)`: at 1099 of 1500 frames the
consumer scaled them **up by 1.36×**, so the printed figure was 458 µs/frame against an actual 336 — wrong in
live output, not merely latent. `lighting.cpu.{spread,point,post}.us` were `Recompute` but only run when the CPU
calc runs, which the shipping GPU config skips: a single self-healing CPU frame inside a window would have given
`count=1` against ~1100 expected and inflated them **~1100×**.

**`calc.cells` is NOT a scene fingerprint — `lights.sources` is.** A live A/B is only valid if both captures
saw the same scene, and the harness player's position **persists between runs**, so consecutive captures are
not automatically at the same place. `lighting.calc.cells` looks like the natural comparability check and is
useless for it: it is set by the query window and grid bucketing, so it reads an identical 35840 at wildly
different locations. Using it as the check let an invalid A/B through — baseline at 82.6 lights/recompute
against an "after" at 30.0, where `lighting.cpu.gather.us` fell **73% on code that was never touched**.
`lighting.lights.sources` is the metric that actually tracks scene load. **Pin the location with `--warp` to a
real bookmark, and assert `lights.sources` matches across the pair before believing any delta.** A phase that
moved when nothing touched it is the tell that the comparison, not the code, is what changed.

**A gauge set inside a skipped code path reports a stale value forever.** `lighting.cells` was set inside
`calculate()`, which GPU lighting skips entirely, so it froze at whatever the pre-latch load frames left behind.
It also reported the **query** region while every O(cells) loop runs over the border-padded **calculation**
region — 4.375× larger. It was the denominator for every per-cell figure in the campaign. **Publish a
descriptor from the act that establishes what it describes** (here, `begin()`), and prefer asserting a
*relation* between two gauges over a magic number — `calc.cells > cells` catches both the staleness and the
wrong-region bug, and cannot rot.

**A SATURATING signal masks everything behind it — and `glGetError` is one.** Per the GL spec, once an error
flag is set no further error is recorded until `glGetError` clears it. A *single* per-frame error therefore
hides every other GL error the engine can raise, for the whole run. That is exactly what happened (#131): three
`GL_INVALID_VALUE`s per composite draw — `renderGlBuffer` feeding the `-1` of an INACTIVE vertex attribute into
a `GLuint` index parameter — meant the render gate's GL check could never mean anything, and left the
long-standing "OpenGL errors during shutdown" line unattributable. **Drain a saturating signal at every phase
boundary you want to attribute to, and drain it unconditionally** — this one sat behind `if (DebugEnabled)` =
`!NDEBUG`, dead-code-eliminated from every release build.

**`glGetError` cannot localise; a synchronous KHR_debug callback can.** `glGetError` says only *an error
happened somewhere since the last drain*, so bisecting by drain placement narrows to a code region and then
stops — and with rate-limited logging the bisect can even mis-read *which* region, because the log budget is
consumed by whichever drain reaches it first. `glDebugMessageCallback` + `GL_DEBUG_OUTPUT_SYNCHRONOUS` fires
*inside* the offending driver call, so a stack dump names the call site and the driver names the parameter:
`GL_INVALID_VALUE in glEnableVertexAttribArray(index)`. Shipped opt-in as `STAR_GL_DEBUG=1`
(`source/application/StarRenderer_opengl.cpp`). **Reach for it first, not last.**

---

## 8. The self-test

The renderer has oracles that fail loudly. This subsystem now has its own — because two measurement errors in
this arc were exactly the class an oracle catches.

**Unit** (`source/test/telemetry_test.cpp`, 25 tests): budget closure; owner denominator-vs-total; the cadence
bound; declare-after-registration; histogram bucket boundaries and sum-vs-count; descriptor round-trip and
first-declaration-wins; the type-guess regression.

**On real data** (`scripts/telemetry-window.py`): budget closure, cadence bound and histogram consistency run
against every windowed capture, and the tool **exits non-zero** on a violation rather than printing a plausible
number.

### The cadence rule is deliberately asymmetric

`count ≤ expected`. **Under is legitimate** — a gated pass or an async readback observes only some ticks, which
is what coverage reports. **Over is always a bug**: the span was opened twice per tick and the metric should
have been declared `cadence=Call`. A symmetric "count ≈ expected" would flag every gated pass in the tree as
broken.

### Why there is no "no conflicts among declared metrics" unit test

An earlier draft had one. It was removed for two independent reasons: `core_tests` links `star_core` and never
registers the *client's* metrics, so it would iterate a handful of `test.*` keys while appearing to guard the
~62 real ones; and `reset()` clears the conflict flags, so `telemetrySetUp()` guaranteed it passed before it
was evaluated.

**An oracle that cannot fail is worse than no oracle**, because it teaches you to ignore it — the same family
as the unarmed pixel oracle that once grepped as `DIFF=0`. The real conflict check runs against live captures.

---

## 9. Deliberately out of scope

- **Per-frame time series.** Powerful for episodic problems, but histograms cover most of that need at a
  fraction of the build. Earn it when a specific episodic bug demands it.
- **Memory / allocation axis.** Render levers do trade time for churn, but that is a separate arc.
- **Per-thread budgets for `sim` and `lighting`.** The model supports them; the metrics are deferred until a
  lever shows *"frame time didn't move but process CPU dropped"* — the evidence that earns them.
- **Attributing the residual unattributed GPU.** A finding to chase, not a telemetry feature.
- **Input injection** for the harness. The reason motion-dependent levers remain out of reach (§6).

---

Related: `docs/render/layer1-architecture.md`, `docs/superpowers/specs/2026-07-25-unified-telemetry-model-design.md`.
