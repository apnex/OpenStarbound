# Lighting CPU budget: closure + levers (#168)

**Status:** design, Director-approved scope (instrument + levers)
**Supersedes nothing. Depends on:** the unified telemetry model (#166),
`docs/telemetry/architecture.md`, `docs/superpowers/specs/2026-07-25-unified-telemetry-model-design.md`.
**Blocks:** #161 (Jacobi lightmap-spread), whose lever choice is measured against this budget.

---

## 1. The problem

Owner `lighting` reports **52% of its CPU cost unattributed** on live data. Every other owner closes
(`frame` 100.0%, `gl` 99.3%, `sim` clean). Choosing a lever against a budget where half the cost is
invisible means optimising the visible half and guessing at the rest.

Capture `harness/profiles/idle-cadence-fix.json` (1500 frames, 1116 recomputes, 384 gate skips):

```
lighting.cpu.total.us   count=1500  total=615104     (Cpu/Lighting/Frame/Total)
lighting.cpu.gather.us  count=1116  total=292457     (Cpu/Lighting/Recompute/Budget)
  -> cpu accounted: 262.0 us/tick of 551.0 (47.6%) -- unattributed 289.0 us/tick
```

## 2. What the arithmetic actually does today

`telemetry-window.py` computes, for owner `lighting`:

- `denom = tick_count(lighting.temporal.recomputed) = 1116` (`:208`)
- `whole = coverage_scale(lighting.cpu.total.us)` → cadence `frame`, expected 1500, coverage 1.0 →
  **unscaled, and accumulated over 1500 frames** (`:266`)
- `parts = coverage_scale(lighting.cpu.gather.us)` → cadence `recompute`, coverage 1.0 → unscaled,
  accumulated over 1116 recomputes (`:268`)

Coverage scaling is a no-op on both. The distortion is that a numerator summed over **1500** events is
divided by a denominator counting **1116**. Writing `R = 1116`, `S = 384`, `T_r`/`T_s` for the mean
total-scope duration on recompute/skipped frames:

```
whole            = R·T_r + S·T_s
un_per_recompute = (T_r − gather_mean) + (S/R)·T_s
289.0            = (T_r − 262.0) + 0.34409·T_s
```

Skip-path share of the residual is `T_s / 839.9`. Static analysis puts `T_s` at ~3–5 µs (§3.5), so the
384 skips explain **~1%** of the gap. The residual is genuinely-missing recompute-path phases.

## 3. Findings

Six defects, all pre-existing, all found by static analysis during this design.

### 3.1 `lighting.cells` is stale, and measures the wrong region (CRITICAL)

`StarCellularLighting.cpp:166-170` registers and sets the gauge **inside `calculate()`**, which is
skipped whenever `skipCpuCalc` is true (`StarWorldClient.cpp:2179`) — i.e. always, in the shipping GPU
config. The reported 8192 is a leftover from the pre-latch load frames.

It is also the wrong quantity. It reports `(arrayMax − arrayMin)`, the **query** region — the output
lightmap size. Every O(cells) phase in `lightingCalc()` runs over the **calculation** region, which
`begin()` pads by `borderCells() = ceil(max(spreadMaxAir, pointMaxAir))` on all four sides
(`StarCellularLighting.cpp:92-101`, `StarCellularLightArray.hpp:307-310`). With the shipped
`/lighting.config:lighting` values `spreadMaxAir=32`, `pointMaxAir=48`, that is **+48 per side, +96 per
axis**: a 128×64 query region is a 224×160 = **35,840-cell** calculation region, **4.4× the gauge**.

This is the denominator for every per-cell claim the levers in §7 will make. It must be fixed first.

### 3.2 `lighting.cpu.{spread,point,post}.us` — conditional phases declared `Recompute`

`StarCellularLightArray.hpp:395,401` and `StarCellularLighting.cpp:166` declare
`{Cpu, Lighting, Recompute, Budget}`, but they only execute when the CPU calc runs. Today they window
to zero delta and are dropped (`telemetry-window.py:79-80`), so nothing is visibly wrong.

The GPU path is self-healing: a single failed GPU frame re-arms the CPU calc for one frame
(`StarWorldClient.cpp:2167-2185`). One such recompute inside a window gives `count=1, expected=1116` →
`coverage_scale` divides by 0.0009 and **inflates that timer ~1100×**, tripping `parts exceed the whole`
on entirely correct data. This is the `cpu.frame.idle.us` mistake: absence here is genuine gating, not
sampling loss.

### 3.3 `lighting.gpu.cpu_cost.us` — declared `Frame`, fires per lightmap update

`StarGpuLightmapPass.cpp:15-16` declares `MetricCadence::Frame`, but `processFull` is reached only
inside `if (lightMapUpdated)` (`StarWorldPainter.cpp:228-232`). Live: count 1099 of 1500 frames →
coverage 73% → the consumer scales the total **up by 1.36×**. Reported 458 µs/frame; actual 336. The
metric is `role=Detail` so closure is unaffected, but the printed figure is wrong by 36% today.

### 3.4 `lighting.upload.us` — same defect

`StarWorldPainter.cpp:241-242`, `MetricCadence::Frame`, reached only inside `if (lightMapUpdated)` and
then only when the GPU path is inactive and the CPU lightMap is non-empty.

### 3.5 The temporal gate sorts, every frame

`TemporalLightingGate::signatureOf` (`StarTemporalLightingGate.hpp:21-32`) does not hash. It reserves a
`List<pair<Vec2I,uint8_t>>` (~1 KB malloc at 84 lights) and `std::sort`s it — ~540 comparisons — on
**every frame**, before the gate decision. Cost class 1.5–4 µs/frame. Small, but it is the entire
measurable content of the skip path and it is currently untimed.

### 3.6 `begin()` is outside every existing scope

`m_lightingCalculator.begin(lightRange)` at `StarWorldClient.cpp:2028` sits between the temporal gate
and `gatherScope` (which opens at `:2030`). It `std::fill`s the whole calculation region —
35,840 × `sizeof(Cell)` 16 B ≈ **573 KB per recompute** — and no timer covers it.

## 4. Closure mechanism: a frame-cadence Budget part, not a second Total

The obvious fix — give `lighting` two Totals, one per-frame and one per-recompute — **is not
representable**:

- `StarTelemetry.cpp:445-450` writes `owners[String(ownerName(spec.owner))] = ...` into a `StringMap`.
  Two `MetricOwner::Lighting` rows silently collapse to the last one.
- `telemetry-window.py:207` unpacks `total` as a scalar; `:265` does `total_name in w`, which raises
  `TypeError: unhashable type: 'list'` for an array.
- `:267-278` reuses one `whole` across every domain in the owner's rows.

Re-scoping the single Total to open *after* the gate is a one-word edit, but it destroys things
deliberately built: the skip path (25.6% of frames) becomes unmeasurable, `whole/1500` stops being a
usable per-frame figure, it reverts commit `ed833d21`, and it falsifies three documentation sites that
cite this metric as the canonical legitimately-mixed-cadence Total
(`docs/telemetry/architecture.md:147-150`, the v2 design spec `:91-95`, `telemetry-window.py:239-243`).

**Decision: keep `lighting.cpu.total.us` frame-cadence, and add a frame-cadence Budget part covering
the pre-gate prologue.** Then

```
whole = R·T_r + S·T_s
parts = R·(sum of recompute parts) + S·prologue
```

closes exactly, because `coverage_scale` scales each part against **its own** cadence expectation.
No schema change, nothing lost, and the mixed-cadence machinery stays exercised by a live instance.

## 5. Phase decomposition

Nine Budget parts, **contiguous and exhaustive** between the opening of `totalScope`
(`StarWorldClient.cpp:1993`) and the end of the function — so closure is exact rather than approximate.
One scaling law per phase, so each maps to one lever class.

| metric | span | cadence | scaling law |
|---|---|---|---|
| `lighting.cpu.prologue.us` | `:1994-2022` prologue + temporal gate | **Frame** | O(lights) — the sort; the only part paid on skipped frames |
| `lighting.cpu.params.us` | `:2024-2027` config reads + `setParameters` + `setMonochrome` | Recompute | O(1) — asset lookup, JSON clone, 7 keyed reads |
| `lighting.cpu.begin.us` | `:2028` `begin()` | Recompute | O(calcCells) — the 573 KB fill |
| `lighting.cpu.gather.us` | `:2029-2076` (existing, unchanged) | Recompute | O(calcCells) |
| `lighting.cpu.lights.us` | `:2078-2127` unlock + promote config + both add-light loops | Recompute | **O(lights)** |
| `lighting.cpu.export.us` | `:2129-2143` `exportSpreadInputs` + `exportPointLights` + border | Recompute | O(calcCells) — transposing scatter |
| `lighting.cpu.convert.us` | `:2144-2165` fp16 loop + R8 extraction | Recompute | O(calcCells·3) + O(calcCells) |
| `lighting.cpu.calculate.us` | `:2167-2185` the CPU calc (or its skip) | Recompute | O(calcCells·passes) — ≈0 in GPU mode |
| `lighting.cpu.publish.us` | `:2186-2199` lock acquire + moves | Recompute | O(cells) + lock wait |

Naming follows the de-facto `lighting.cpu.<phase>.us` family. `lighting.cpu.calc.us` is deliberately
**not** used: `lighting.cpu.calc.{ran,skipped}` already exist as counters and the prefix collision
reads as a type conflict even though it is not one.

### 5.1 Two rules the arc learned the hard way

**Conditional phases wrap the branch, they do not sit inside it.** `export`, `convert` and `calculate`
each run under a condition (`lightingGpu`, `!skipCpuCalc`). A scope placed *inside* the branch would
have to be `cadence=Call` to avoid `coverage_scale` inflating it (§3.2). Instead each scope **encloses
the `if`**, so it fires on every recompute, reports 100% coverage, and records ≈0 when its branch is
not taken. Cost: one extra predictable branch test. Benefit: **owner `lighting` becomes
coverage-scale-free end to end** — every part reads 100%, so any deviation is a real signal rather than
something a reader must interpret. This removes the single largest source of arithmetic error this arc
has hit three times.

**Nested timers are `Detail`, never `Budget`.** `spread`/`point`/`post` sit inside the new
`calculate` part and are demoted to `MetricRole::Detail`, or they would double-count into `parts` and
trip `parts exceed the whole` (`telemetry-window.py:273-275`).

### 5.2 Known inclusion: `publish` contains lock wait

`lighting.cpu.publish.us` covers `MutexLocker mapLocker(m_lightMapMutex)` as well as the moves. The
only other locker is `WorldClient::waitForLighting()` (`:1537`), which holds the mutex across a
preview-tile patch loop and five moves, so the wait is real and can stall. Timing it conflates
contention with work — but the wait is genuine wall-clock cost on the lighting thread and must be
inside some part for closure to hold. It is documented at the call site. If the number comes back fat,
splitting wait from work is a follow-up, not a precondition.

## 6. Scaling-law denominators

A phase cost without its denominator is a number, not a lever. Three gauges/counters, all set where the
quantity is actually established:

| metric | value | site | fixes |
|---|---|---|---|
| `lighting.cells` | query-region cells (output lightmap size) | moved to `begin()` | §3.1 staleness |
| `lighting.calc.cells` | **calculation-region** cells — the true O(cells) denominator | `begin()` | §3.1 wrong region |
| `lighting.lights.sources` | `lights.size()` — the true O(lights) denominator | `lightingCalc()` | `lights.{spread,point}` double-count promoted lights |

## 7. Where the 289 µs actually is

Static-analysis model, **to be replaced by the §5 measurement** — this is the hypothesis the
instrumentation exists to test, not a result. Per recompute at C = 35,840 cells:

| Phase | Est. µs | Scaling | Traffic |
|---|---|---|---|
| `exportSpreadInputs` — 2 resets + transposing copy | **110–150** | O(C) | 573 KB read, 538 KB written, 538 KB memset |
| fp16 conversion loop | **65–120** | O(C·3), compute-bound | 107,520 conversions |
| `begin()` cell fill | **35–55** | O(C) | 573 KB zeroed |
| resize zero-fill (half + R8 buffers) | 15–20 | O(C) | 251 KB |
| R8 extraction | 10–25 | O(C) | 108 KB read, 36 KB written |
| config `get`s ×~10 | 1–3 | O(1) | — |
| `assets()->json` + `Json::set` + `setParameters` | 1–3 | O(1) | — |
| light add-loops (84 lights) | 1–2 | O(lights) | — |
| publish moves/frees | 1–3 | O(1) | — |
| `signatureOf` sort (every frame, incl. skips) | 2–5 | O(lights log lights) | — |

**≈3.8 MB of memory traffic + 107,520 conversions per recompute.** 3.8 MB / 289 µs ≈ 13 GB/s — a
self-consistent single-thread L2/L3-resident rate. **The gap is memory traffic and the half-conversion.
All the JSON/config/asset/light-loop work together is under 2% of it.**

Correction to an earlier reading in this design: `setParameters`/`Json::set` was initially flagged as a
prime suspect. It is ~1–3 µs. It remains worth deleting — ~10 heap allocations and a global-mutex
acquire per recompute to recompute a constant — but on hygiene grounds, not magnitude.

## 8. The levers

Each measured against the closed budget from §5, on the live profile harness, as its own commit.

**Scope note.** The Director approved "instrument + all three levers" against a ranking that named
`setParameters`, `exportSpreadInputs` and `floatToHalf`. §7 then measured `setParameters` at ~1–3 µs and
found the buffer-emptying root cause worth ~50–90 µs. The three shipped levers are therefore L1–L3
below; the JSON hoist is demoted to L0 hygiene rather than dropped. Count and effort are unchanged;
the substitution is on evidence, and is flagged here rather than made silently.

### L0 — hygiene: stop recomputing a constant (`params`)

`StarWorldClient.cpp:2026` calls
`setParameters(root.assets()->json("/lighting.config:lighting").set("pointAdditive", newLighting))`
every recompute: an `Assets::json` lookup under the **global** `m_assetsMutex` (plus a `freshen()` clock
write under that lock), a `Json::set` that deep-copies the whole 7-key object
(`StarJson.cpp:779-783`), and 7 more string-keyed lookups in `setParameters`. ~10 heap allocations to
produce a value that is invariant unless `newLighting` or `monochrome` changes.

Cache the composed `Json` and the derived parameters on those two bools plus the assets reload epoch.
**Byte-identical.** ~1–3 µs — landed for correctness of the `params` phase, not for the number.

### L1 — stop emptying the buffers at publish (the biggest, and it is one root cause)

The publish block `std::move`s five buffers into the published members
(`StarWorldClient.cpp:2192-2196`). `Image::operator=(Image&&)` `take()`s the source's data **and its
dimensions** (`StarImage.cpp:236-244`), and `List`/vector moves leave capacity 0. So on the *next*
recompute every one of them is empty, which means:

- `emission.reset()` / `obstacle.reset()` cannot take their same-size early-out
  (`StarImage.cpp:250-275`) → `malloc` + **memset 537,600 B**, immediately overwritten.
- `m_pendingLightingEmissionHalf.resize(n)` → `operator new(215,040)` + **value-init zero-fill**,
  immediately overwritten.
- `m_pendingLightingObstacleR8.resize(cells)` → `new(35,840)` + **zero-fill**, immediately overwritten.

That is **~788 KB zeroed per recompute for nothing**, from a single root cause. Fix: ping-pong the
pending/published buffers (swap rather than move), so every `reset`/`resize` hits its same-size
early-out in steady state. **Byte-identical**, and it fixes three sites at once.

Note the comment at `StarCellularLighting.cpp:258-259` ("reset() zero-fills") is only true *by
accident* here — `Image::reset` does not zero-fill on a same-size call. Correct it with the fix.

### L2 — `exportSpreadInputs`: the transpose

`StarCellularLighting.cpp:254-290` iterates `x` outer / `y` inner over a **column-major** cell array
while writing into a **row-major** image at `(y·width + x)·3`. Consecutive inner iterations write
addresses **2,688 B apart** (emission) and 672 B apart (obstacle) — every store lands on a different
cache line, and the loop sweeps the whole 538 KB working set 5–6 times.

Fix: iterate in destination order, or tile the transpose. **Byte-identical** — same output bytes,
different traversal. Independent of L1; both apply.

### L3 — `floatToHalf`: branchless first, F16C only with sign-off

`StarWorldClient.cpp:32-46` is a scalar bit-twiddle with two branches, run 107,520 times per recompute.
Build is `-O3 -ffast-math` with **no `-march`** (`CMakeLists.txt:295-296`), so the compiler targets
baseline x86-64 and cannot emit F16C even though the Arrow Lake CPU has it.

**L3a (this task): make the algorithm branchless** — replace the two early-outs with select arithmetic
so GCC, Clang *and* MSVC auto-vectorise the loop with no intrinsics, no `__builtin_cpu_supports`
dispatch and no MSVC shim. The tree contains zero SIMD today and has already taken one MSVC portability
incident over `__builtin_clzll`. **Byte-identical by construction.**

**L3b (NOT in this task — needs Director sign-off): F16C `vcvtps2ph`.** 8 floats per instruction,
estimated **~5 µs against 65–170 — a 10–30× cut**, by far the largest single lever available. It is
**not byte-identical**: `vcvtps2ph` rounds half-to-**even** and emits proper subnormals, while this
function rounds half **away from zero** and flushes subnormals to signed zero. Expected divergence is
1 ULP on roughly 1-in-8192 values (~13 texels per recompute) plus the subnormal range below 6.1e-5.
The render gate's `spreadoracle` would report DIFF, correctly. Shipping it means accepting a bounded
output change validated by a quality comparison instead of byte-identity — a separate design cycle.

### L4 (filed, not scoped here) — the border multiplier

Every O(C) row above scales with `C = (Qw + 96)(Qh + 96)`, i.e. **4.375× the query region** at 128×64,
because `borderCells() = ceil(max(spreadMaxAir 32, pointMaxAir 48)) = 48` pads all four sides. Shrinking
the border, or exporting only the sub-region the GPU actually samples, moves **all five top rows at
once** and dwarfs L1–L3 combined. This is an architectural change to the lighting region contract, not
a local optimisation. File against #161's measurement phase.

## 9. Cadence-hygiene fixes

Five descriptor corrections, each its own labelled commit, independent of the new instrumentation:

| metric | change | why |
|---|---|---|
| `lighting.cpu.spread.us` | `Recompute` → `Call`, `Budget` → `Detail` | §3.2 + now nested in `calculate` |
| `lighting.cpu.point.us` | `Recompute` → `Call`, `Budget` → `Detail` | §3.2 |
| `lighting.cpu.post.us` | `Recompute` → `Call`, `Budget` → `Detail` | §3.2 |
| `lighting.gpu.cpu_cost.us` | `Frame` → `Call` | §3.3 — corrects a live 36% overstatement |
| `lighting.upload.us` | `Frame` → `Call` | §3.4 |

## 10. Verification

1. **`scripts/render-gate.sh` must stay PASS** for every commit. Telemetry is non-functional and L1–L3
   are byte-identical, so all three oracles must report nonzero MATCH/EXACT and zero DIFF. Assert each
   oracle *ran* — an unarmed oracle prints SKIPPED and greps as a pass.
2. **`core_tests` + `game_tests` green.** Note `lighting_telemetry_test.cpp:43-45` asserts
   `count > 0` for `spread`/`point`/`post`; those tests drive `CellularLightingCalculator` directly, so
   the §8 demotions do not break them. There is currently **no** automated guard on the lighting owner's
   contract — §10 adds one.
3. **Live profile closure.** `scripts/render-profile.sh` with `telemetryDeepTracing=true`; owner
   `lighting` must report **≥97% accounted** with every part at 100% coverage.
4. **Lever A/B.** Each of L1–L3 measured as its own before/after pair against the closed budget. Per
   `docs/telemetry/architecture.md`, CPU drifts ~10% between non-adjacent runs — **matching scene
   content is not evidence that CPU timings are comparable.** Any claimed win needs an A-B-A replicate.

## 11. New test

`core_tests`: pin the `lighting` owner row (`denominator` and `total` keys) in the snapshot's `owners`
object, mirroring `OwnersDeclareDenominatorAndTotal` which today covers only `frame`/`gl`/`sim`
(`telemetry_test.cpp:310-321`). Re-scoping the Total or renaming the denominator currently breaks
nothing and no test notices.

## 12. Success criteria — the "perfection" target

1. **Closure at the frame budget's bar:** owner `lighting` ≥97% accounted, target ~100%, matching
   `frame`'s 100.0%.
2. **Every part 100% coverage** — no coverage scaling anywhere in the owner.
3. **Closes under both `lightingGpu` states.** With GPU on, `calculate` ≈0 and `export`/`convert` carry
   the cost; with GPU off, the reverse. Both must close. A budget that closes only in the default
   config is the config-gated-branch trap for the third time in this campaign.
4. **One scaling law per phase**, with its denominator recorded live (§6) — so a phase cost converts to
   a per-cell or per-light figure without a stale gauge lying by 4.4×.
5. **L1–L3 landed byte-identical**, each with an A-B-A-replicated number. L3b (F16C) and L4 (the border
   multiplier) are deliberately *not* in this task: both change output or contract and need their own
   design cycle. Their magnitudes are recorded here so the follow-up is chosen on evidence.
6. **The budget re-closes after the levers** — the levers move phase magnitudes, not attribution.

## 13. Explicitly out of scope

Real lighting CPU that owner `lighting` will still not see, and cannot, because it happens outside
`lighting.cpu.total.us` and the model has no second Total (§4):

- `WorldClient::render()` walks **every entity** for light sources (`StarWorldClient.cpp:534-541`) and
  gathers particle lights (`:551`, a fresh `List` allocation per frame) — on the **render thread**,
  billed to `cpu.frame.render.us`.
- That same block blocks on `m_lightMapPrepMutex`, which `lightingCalc()` holds from `:1990` to `:2078`
  — the main thread's stall is charged to `cpu.frame.render.us` with no attribution to lighting.
- The `maxEmission` scan over the whole emission grid on the render thread
  (`StarWorldPainter.cpp:143-150`), outside `processFull`'s scope.
- `adjustLighting` → `TilePainter.cpp:38-55`, a per-render-tile RMW over the CPU lightmap.
- `LogMap::set` on every lighting-thread wakeup (`StarWorldClient.cpp:2211`) — takes a **global** mutex
  and allocates a `String`, outside `totalScope`.

Filed as a separate task. Attributing them would require a second Total per owner, which is a telemetry
model change, not a lighting change.
