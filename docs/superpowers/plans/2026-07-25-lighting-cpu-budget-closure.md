# Lighting CPU Budget Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close owner `lighting`'s CPU budget from 47.6% accounted to ≥97%, then land the levers the closed budget exposes.

**Architecture:** Nine contiguous, exhaustive `role=Budget` phase timers inside `WorldClient::lightingCalc()`, one of them frame-cadence (the pre-gate prologue) so the frame-cadence Total closes exactly against a mix of frame- and recompute-cadence parts. No telemetry schema change. Then four byte-identical levers measured against that budget.

**Tech Stack:** C++20, the in-tree `Telemetry`/`TelemetryScope` subsystem, `scripts/render-gate.sh` (byte-identity oracles), `scripts/render-profile.sh` (live offscreen profile), `scripts/telemetry-window.py` (closure oracle), gtest (`core_tests`, `game_tests`).

**Spec:** `docs/superpowers/specs/2026-07-25-lighting-cpu-budget-closure-design.md`

---

## Standing constraints — read before ANY task

- **Never `git add -A` or `git add .`** — the `.gitignore` previously swept a 314 MB harness and a player save into a public-bound branch. Always name files explicitly.
- **The Director must be out of the game before any build.** Check with `pgrep -f 'starbound|OpenStarbound'` and stop if anything is running.
- **All builds are E-core pinned.** A prior unpinned run hit 105 °C.
  ```bash
  cd /root/frackin/OpenStarbound
  VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
    cmake --build build/linux-release-clang --target star_game core_tests game_tests starbound -j 8
  ```
- **Use the Edit tool, not `sed`/`perl`** for file changes.
- **Use `command mv` / `command cp`** (both are aliased to interactive `-i`).
- **Push to `origin` only.** `upstream` push is disabled.
- Every commit message ends with `[#168]`.

## File structure

| File | Responsibility | Tasks |
|---|---|---|
| `source/base/StarCellularLighting.cpp` | calculator; owns `begin()` where the scaling-law gauges belong | 1, 2, 8, 9 |
| `source/base/StarCellularLightArray.hpp` | `spread`/`point` timer descriptors | 2 |
| `source/rendering/StarGpuLightmapPass.cpp` | `lighting.gpu.cpu_cost.us` descriptor | 2 |
| `source/rendering/StarWorldPainter.cpp` | `lighting.upload.us` descriptor | 2 |
| `source/game/StarWorldClient.cpp` | `lightingCalc()` — the nine phases, `floatToHalf`, the publish moves | 3, 7, 9, 10 |
| `source/test/lighting_telemetry_test.cpp` | lighting-metric assertions | 1, 2 |
| `source/test/telemetry_test.cpp` | owner-row contract guard | 4 |

---

# STAGE 1 — INSTRUMENTATION (byte-identical)

## Task 1: Re-site the scaling-law gauges

**Why:** `lighting.cells` is set inside `calculate()`, which is skipped in the shipping GPU config, so it reports a stale pre-latch value. It also reports the **query** region while every O(cells) phase runs over the border-padded **calculation** region — 4.375× larger at 128×64. It is the denominator for every per-cell claim the levers will make.

**Files:**
- Modify: `source/base/StarCellularLighting.cpp` (`begin()` at :92-101; `calculate()` at :166-171)
- Test: `source/test/lighting_telemetry_test.cpp`

- [ ] **Step 1: Write the failing assertion**

In `source/test/lighting_telemetry_test.cpp`, inside `TEST(LightingTelemetry, PhaseTimersAndCountsPopulate)`, replace the single `lighting.cells` assertion with:

```cpp
  // The calculation region is the query region padded by borderCells() on all four sides, so it is
  // strictly larger. Asserting the RELATION (not a magic number) is what catches a gauge that has gone
  // stale or is reporting the wrong region -- the defect that hid a 4.375x factor from every per-cell
  // figure in the campaign.
  EXPECT_GT(Telemetry::gauge("lighting.cells").value(), 0);
  EXPECT_GT(Telemetry::gauge("lighting.calc.cells").value(),
            Telemetry::gauge("lighting.cells").value());
```

- [ ] **Step 2: Add a test proving the gauges are live WITHOUT calculate()**

Add this new test immediately after `PhaseTimersAndCountsPopulate`:

```cpp
// The regression guard for the stale-gauge defect: in the shipping GPU config calculate() is skipped
// entirely, so a gauge set inside calculate() reports whatever the last pre-latch frame left behind.
// begin() is the act that establishes both regions, so begin() is where they must be published.
TEST(LightingTelemetry, CellGaugesArePublishedByBeginNotCalculate) {
  Telemetry::reset();
  Telemetry::setEnabled(true);

  CellularLightingCalculator calc;
  calc.setParameters(lightingConfig());
  calc.setMonochrome(false);
  calc.begin(RectI::withSize(Vec2I(0, 0), Vec2I(32, 24)));
  // NOTE: no calculate() call -- this is the GPU-lighting path.

  EXPECT_EQ(Telemetry::gauge("lighting.cells").value(), 32 * 24);
  EXPECT_GT(Telemetry::gauge("lighting.calc.cells").value(), 32 * 24);

  Telemetry::setEnabled(false);
}
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target game_tests -j 8
./build/linux-release-clang/source/test/game_tests --gtest_filter='LightingTelemetry.*'
```
Expected: `CellGaugesArePublishedByBeginNotCalculate` FAILS (`lighting.calc.cells` is 0, and `lighting.cells` is 0 because `calculate()` was never called).

- [ ] **Step 4: Move the gauge into `begin()`**

In `source/base/StarCellularLighting.cpp`, replace `begin()` (currently :92-101) with:

```cpp
void CellularLightingCalculator::begin(RectI const& queryRegion) {
  // The scaling-law denominators for every O(cells) phase in the lighting pipeline. They are published
  // HERE, by the act that establishes both regions, and deliberately NOT inside calculate(): calculate()
  // is skipped whenever GPU lighting is latched active (StarWorldClient.cpp skipCpuCalc), so a gauge set
  // there freezes at whatever the pre-latch load frames left behind. The two are different quantities and
  // conflating them cost the campaign a 4.375x error: the output lightmap is the QUERY region, but every
  // export/convert/fill loop runs over the border-padded CALCULATION region.
  static auto cellsGauge = Telemetry::gauge("lighting.cells",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
  static auto calcCellsGauge = Telemetry::gauge("lighting.calc.cells",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});

  m_queryRegion = queryRegion;
  if (m_monochrome) {
    m_calculationRegion = RectI(queryRegion).padded((int)m_lightArray.right().borderCells());
    m_lightArray.right().begin(m_calculationRegion.width(), m_calculationRegion.height());
  } else {
    m_calculationRegion = RectI(queryRegion).padded((int)m_lightArray.left().borderCells());
    m_lightArray.left().begin(m_calculationRegion.width(), m_calculationRegion.height());
  }

  cellsGauge.set((int64_t)m_queryRegion.width() * (int64_t)m_queryRegion.height());
  calcCellsGauge.set((int64_t)m_calculationRegion.width() * (int64_t)m_calculationRegion.height());
}
```

- [ ] **Step 5: Delete the stale gauge from `calculate()`**

In `source/base/StarCellularLighting.cpp` (currently :166-171), delete these three lines:

```cpp
  static auto cellsGauge = Telemetry::gauge("lighting.cells",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
  cellsGauge.set(int64_t((arrayMax[0] - arrayMin[0]) * (arrayMax[1] - arrayMin[1])));
```

Leave `postTimer` and `TelemetryScope postScope(postTimer);` exactly as they are. Update the surrounding comment, which currently reads "the gauge is set unconditionally", to:

```cpp
  // 'post' phase: output copy + brightness cap. Timer records only under deep
  // tracing (TelemetryScope gates itself). The cell gauges live in begin() -- see the note there.
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target game_tests -j 8
./build/linux-release-clang/source/test/game_tests --gtest_filter='LightingTelemetry.*'
```
Expected: PASS, 3 tests.

- [ ] **Step 7: Commit**

```bash
cd /root/frackin/OpenStarbound
git add source/base/StarCellularLighting.cpp source/test/lighting_telemetry_test.cpp
git commit -m "telemetry: publish the cell gauges from begin(), not the skipped calculate() [#168]

lighting.cells was set inside calculate(), which the shipping GPU config skips entirely -- so it
reported a stale value from the pre-latch load frames. It also reported the QUERY region while every
O(cells) phase runs over the border-padded CALCULATION region (borderCells=48 per side => 4.375x at
128x64). It is the denominator for every per-cell claim, so it was silently scaling them all wrong.

Adds lighting.calc.cells as the true O(cells) denominator, and a test that drives begin() WITHOUT
calculate() -- the exact shape of the shipping path that hid the defect."
```

---

## Task 2: Cadence-hygiene fixes

**Why:** Five descriptors declare a cadence they do not actually fire at. Two of them overstate a live number by 36% today; three are a latent ~1100× inflation waiting for one self-healing CPU-lighting frame. Absence caused by a branch not taken is **genuine gating**, not sampling loss, and `coverage_scale` must not scale it up. This is the `cpu.frame.idle.us` fix applied to five more sites.

**Files:**
- Modify: `source/base/StarCellularLightArray.hpp:394-407`
- Modify: `source/base/StarCellularLighting.cpp` (`postTimer`, ~:166)
- Modify: `source/rendering/StarGpuLightmapPass.cpp:15-16`
- Modify: `source/rendering/StarWorldPainter.cpp:241-242`

- [ ] **Step 1: Demote the two calculator timers**

In `source/base/StarCellularLightArray.hpp`, change the `spreadTimer` and `pointTimer` descriptors from `{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget}` to `{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail}`, and put this comment immediately above the `spreadTimer` registration:

```cpp
    // Cadence::Call, Role::Detail -- BOTH deliberate.
    //   Call: this phase runs only when the CPU calculate() runs, which the shipping GPU config skips.
    //     Declared Recompute, a single self-healing CPU frame inside a window gives count=1 against
    //     ~1100 expected, and the consumer's coverage_scale divides by 0.0009 -- inflating this timer
    //     ~1100x and firing "parts exceed the whole" on entirely correct data. Absence here is the
    //     branch not being taken, not a lost sample.
    //   Detail: this nests inside lighting.cpu.calculate.us, which is the Budget part. Summing both
    //     would double-count.
```

- [ ] **Step 2: Demote the post timer**

In `source/base/StarCellularLighting.cpp`, change `postTimer`'s descriptor from `MetricCadence::Recompute, MetricRole::Budget` to `MetricCadence::Call, MetricRole::Detail`, with the same rationale in one line:

```cpp
  // Call/Detail for the same two reasons as spread/point (StarCellularLightArray.hpp): conditional on
  // calculate() running, and nested inside lighting.cpu.calculate.us.
```

- [ ] **Step 3: Fix the two render-thread timers**

In `source/rendering/StarGpuLightmapPass.cpp:15-16`, change `MetricCadence::Frame` to `MetricCadence::Call` and add:

```cpp
  // Cadence::Call, not Frame: processFull is reached only inside `if (lightMapUpdated)`
  // (StarWorldPainter.cpp), so it fires on lightmap-publish frames, not every frame. Declared Frame it
  // measured 1099 of 1500 frames -- 73% coverage -- and the consumer scaled the total UP by 1.36x,
  // inventing cost for frames the pass genuinely did not run on. Reported 458 us/frame; actual 336.
```

In `source/rendering/StarWorldPainter.cpp:241-242`, change `MetricCadence::Frame` to `MetricCadence::Call` and add:

```cpp
        // Cadence::Call: doubly conditional -- inside `if (lightMapUpdated)` AND only on the CPU-lightMap
        // fallback path. Same defect as lighting.gpu.cpu_cost.us; see the note there.
```

- [ ] **Step 4: Build and run both test suites**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests game_tests -j 8
./build/linux-release-clang/source/test/core_tests
./build/linux-release-clang/source/test/game_tests
```
Expected: all green. `lighting_telemetry_test.cpp:43-45` asserts `count > 0` for spread/point/post; those tests drive `CellularLightingCalculator` directly and are unaffected by a descriptor change.

- [ ] **Step 5: Commit**

```bash
git add source/base/StarCellularLightArray.hpp source/base/StarCellularLighting.cpp \
        source/rendering/StarGpuLightmapPass.cpp source/rendering/StarWorldPainter.cpp
git commit -m "telemetry: five conditional phases were declared at a cadence they never fire at [#168]

coverage_scale corrects SAMPLING LOSS by dividing by coverage. For a phase that is simply gated off,
that invents cost. Five metrics had it backwards:

  lighting.cpu.{spread,point,post}.us  Recompute -> Call  (also Budget -> Detail; they nest inside the
      new lighting.cpu.calculate.us). Conditional on the CPU calculate(), which the GPU config skips.
      One self-healing CPU frame in a window would have inflated them ~1100x.
  lighting.gpu.cpu_cost.us             Frame -> Call. Fires inside if(lightMapUpdated): 1099 of 1500
      frames, scaled UP 1.36x. Reported 458 us/frame against an actual 336 -- wrong TODAY.
  lighting.upload.us                   Frame -> Call. Same, doubly conditional.

Same class as the cpu.frame.idle.us fix (baa41b7c)."
```

---

## Task 3: The nine phase timers

**Why:** This is the task. Nine contiguous, exhaustive Budget parts between the opening of `totalScope` and the end of the function, so `whole = R·T_r + S·T_s` closes exactly against `parts = R·(recompute parts) + S·prologue`.

**Files:**
- Modify: `source/game/StarWorldClient.cpp:1975-2200` (`lightingCalc()`)

**Design constraints the implementer must not violate:**

1. **Contiguity.** Every statement between `TelemetryScope totalScope(totalTimer)` and the closing brace must be inside exactly one phase block. Gaps leak into the residual; overlaps double-count and trip `parts exceed the whole`.
2. **Conditional phases wrap the `if`, they do not sit inside it.** `export`, `convert` and `calculate` all run under a condition. A scope *inside* the branch would need `cadence=Call`; a scope *around* the branch fires every recompute, reports 100% coverage, and records ≈0 when the branch is not taken. This makes owner `lighting` coverage-scale-free end to end. Do it this way.
3. **The prologue phase is the only `MetricCadence::Frame` part.** It covers the work paid on gate-skipped frames too.
4. Do **not** touch `lighting.cpu.total.us`'s descriptor. It stays `Frame/Total`.

- [ ] **Step 1: Add the eight new timer registrations**

In `source/game/StarWorldClient.cpp`, immediately after the existing `gatherTimer` registration (currently :1987-1988), add:

```cpp
  // The phases below are CONTIGUOUS and EXHAUSTIVE across the body of this function: every microsecond
  // between totalScope opening and the closing brace lands in exactly one of them. That is what makes the
  // closure exact rather than approximate. If you add a statement here, it goes INSIDE a phase.
  //
  // prologue is the only Frame-cadence part. It covers the work paid on EVERY frame, including the ones
  // the temporal gate skips -- which is precisely why lighting.cpu.total.us can stay Frame-cadence while
  // everything else is Recompute: the consumer scales each part against its OWN cadence, so a mixed-cadence
  // parts list closes against a frame-cadence whole exactly. (Two Totals per owner are not representable:
  // StarTelemetry.cpp's owner table is keyed by owner name and the last row wins.)
  static auto prologueTimer = Telemetry::timer("lighting.cpu.prologue.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Frame, MetricRole::Budget});
  static auto paramsTimer = Telemetry::timer("lighting.cpu.params.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto beginTimer = Telemetry::timer("lighting.cpu.begin.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto lightsTimer = Telemetry::timer("lighting.cpu.lights.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto exportTimer = Telemetry::timer("lighting.cpu.export.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto convertTimer = Telemetry::timer("lighting.cpu.convert.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // Named 'calculate', not 'calc': lighting.cpu.calc.{ran,skipped} already exist as counters and the
  // prefix collision reads as a type conflict even though it is not one.
  static auto calculateTimer = Telemetry::timer("lighting.cpu.calculate.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // Includes the m_lightMapMutex acquisition, not just the moves. waitForLighting() holds that mutex
  // across a preview-tile patch loop on the render thread, so the wait is real and can stall -- but it is
  // genuine wall-clock cost on the lighting thread and must be inside a part for closure to hold. A fat
  // number here means CONTENTION, which is a different lever from anything else in this budget.
  static auto publishTimer = Telemetry::timer("lighting.cpu.publish.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // The true O(lights) denominator. lighting.lights.{spread,point} count ADDS, and a promoted light adds
  // one of each, so neither is the source count.
  static auto lightSourceCounter = Telemetry::counter("lighting.lights.sources",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
```

- [ ] **Step 2: Restructure the prologue into a block**

Replace the body from `MutexLocker prepLocker(m_lightMapPrepMutex);` (currently :1990) through the end of the temporal-gate block (currently :2022) with:

```cpp
  MutexLocker prepLocker(m_lightMapPrepMutex);
  if (!m_pendingLightReady.load())
    return;
  auto& root = Root::singleton();
  TelemetryScope totalScope(totalTimer);

  // Declared out here because they must outlive the prologue block; assigned inside it so the prologue
  // phase actually covers the moves and the config fetch.
  RectI lightRange;
  List<LightSource> lights;
  List<std::pair<Vec2F, Vec3F>> particleLights;
  ConfigurationPtr configuration;
  {
    TelemetryScope prologueScope(prologueTimer);
    m_pendingLightReady = false;
    lightRange = m_pendingLightRange;
    lights = std::move(m_pendingLights);
    particleLights = std::move(m_pendingParticleLights);
    configuration = root.configuration();

    // --- Temporal lighting decoupling: skip this recompute when the scene is calm (only flicker /
    // particle-motion / ambient changed) and we are between the floor cadence; nothing is republished, so
    // the render thread reuses the previously-published lightmap. Off (flag off / floorMs<=0) => recompute
    // every frame (byte-identical). Flicker-tolerant: the activity signature ignores light colour.
    // NOTE this whole block, including signatureOf's allocate-and-std::sort over every light, runs on
    // EVERY frame -- it is the cost the prologue phase exists to measure. ---
    bool temporalEnabled = configuration->get("lightingTemporalDecouple").optBool().value(true);
    double temporalFloorMs = configuration->get("lightingTemporalFloorMs", 33.0).toDouble();
    int64_t nowMs = Time::monotonicMilliseconds();
    uint64_t epoch = m_lightingTileEpoch.load(std::memory_order_relaxed);
    auto sig = TemporalLightingGate::signatureOf(lights);
    if (!TemporalLightingGate::shouldRecompute(
            m_temporalBaseline, temporalEnabled, temporalFloorMs, epoch, lightRange, sig, nowMs)) {
      temporalSkipped.inc(1);
      return; // calm -> reuse the previously-published lightmap (both scopes close via RAII)
    }
    temporalRecomputed.inc(1);
    m_temporalBaseline = {true, epoch, lightRange, std::move(sig), nowMs};
  }
```

Leave the `temporalRecomputed` / `temporalSkipped` registrations where they are, hoisted above this block.

- [ ] **Step 3: Restructure params + begin**

Replace the four lines currently at :2024-2028 (`bool newLighting` … `m_lightingCalculator.begin(lightRange);`) with:

```cpp
  // lightingGpu and lightingGpuShadowCompare are read HERE, once, rather than again further down. Two
  // benefits: it closes the gap between the lights and export phases (contiguity), and it removes a real
  // latent inconsistency -- lightingGpu was read at two points with the whole gather between them, so a
  // mid-function /command-set flip could pair a promote decision with the opposite export decision.
  bool newLighting = false;
  bool lightingGpu = false;
  bool shadowCompare = false;
  {
    TelemetryScope paramsScope(paramsTimer);
    newLighting = configuration->get("newLighting").optBool().value(true);
    bool monochrome = configuration->get("monochromeLighting").toBool();
    lightingGpu = configuration->get("lightingGpu").optBool().value(false);
    shadowCompare = configuration->get("lightingGpuShadowCompare").optBool().value(false);
    m_lightingCalculator.setParameters(root.assets()->json("/lighting.config:lighting").set("pointAdditive", newLighting));
    m_lightingCalculator.setMonochrome(monochrome);
  }
  {
    TelemetryScope beginScope(beginTimer);
    m_lightingCalculator.begin(lightRange);
  }
```

- [ ] **Step 4: Fold the unlock and the light loops into the lights phase**

The existing gather block (`{ TelemetryScope gatherScope(gatherTimer); ... }`, :2029-2076) is unchanged. Replace everything from `prepLocker.unlock();` (:2078) through the end of the particle-light loop (:2127) with:

```cpp
  {
    TelemetryScope lightsScope(lightsTimer);
    prepLocker.unlock();
    lightSourceCounter.inc(lights.size());

    // CDL (lightingPromoteDynamic): promote static fill (Spread) lights to dynamic by a fraction
    // p in [0,1] -- (1-p) soft spread + p full directional point. p=0 off (Spread unchanged,
    // byte-identical); p~0.15 ~= the old hybrid; p=1 full Point (the mod's look). Gated on lightingGpu:
    // in confirmed GPU mode the CPU calculate() below is skipped, so this feeds the GPU point pass
    // without flooding the CPU raycast. Non-Spread lights (already Point/PointAsSpread, incl. mod-set)
    // are untouched -- no double-promote.
    float promoteFraction = 0.0f;
    float promoteMinIntensity = 0.0f;
    if (lightingGpu) {
      // Read defensively: an interim build persisted this key as a bool, so coerce bool->fraction
      // (true=>0.5, false=>0) rather than throwing toFloat() on a type-mismatched persisted value.
      Json pd = configuration->get("lightingPromoteDynamic");
      promoteFraction = pd.isType(Json::Type::Bool) ? (pd.toBool() ? 0.5f : 0.0f) : pd.optFloat().value(0.0f);
      // Floor below which a Spread light is NOT promoted to a dynamic point: ultra-dim fill lights
      // (e.g. item drops at 20/255 ~= 0.078) gain nothing from sharp point rendering and flicker on a
      // jittery emitter -> keep them soft spreads. 0 disables the floor (promote everything).
      promoteMinIntensity = configuration->get("lightingPromoteMinIntensity", 0.1f).toFloat();
    }
    promoteFraction = promoteFraction < 0.0f ? 0.0f : (promoteFraction > 1.0f ? 1.0f : promoteFraction);

    for (auto const& light : lights) {
      Vec2F position = m_geometry.nearestTo(Vec2F(m_lightingCalculator.calculationRegion().min()), light.position);
      // Promote only "feature" Spread lights: skip the floor (item-drop-class fill) -> pure spread.
      bool promote = promoteFraction > 0.0f && light.color.max() >= promoteMinIntensity;
      if (light.type == LightType::Spread && promote) {
        if (promoteFraction < 1.0f)
          m_lightingCalculator.addSpreadLight(position, light.color * (1.0f - promoteFraction));
        m_lightingCalculator.addPointLight(position, light.color * promoteFraction, light.pointBeam, light.beamAngle, light.beamAmbience);
      } else if (light.type == LightType::Spread) {
        m_lightingCalculator.addSpreadLight(position, light.color);
      } else {
        if (light.type == LightType::PointAsSpread) {
          if (!newLighting)
            m_lightingCalculator.addSpreadLight(position, light.color);
          else { // hybrid (used for auto-converted object lights) - 85% spread, 15% point (* .15 is applied in the calculation code)
            m_lightingCalculator.addSpreadLight(position, light.color * 0.85f);
            m_lightingCalculator.addPointLight(position, light.color, light.pointBeam, light.beamAngle, light.beamAmbience, true);
          }
        } else {
          m_lightingCalculator.addPointLight(position, light.color, light.pointBeam, light.beamAngle, light.beamAmbience);
        }
      }
    }

    for (auto const& lightPair : particleLights) {
      Vec2F position = m_geometry.nearestTo(Vec2F(m_lightingCalculator.calculationRegion().min()), lightPair.first);
      m_lightingCalculator.addSpreadLight(position, lightPair.second);
    }
  }
```

- [ ] **Step 5: Split export from convert, and wrap the conditionals from outside**

Replace everything from `// GPU lighting (Slice 2/3)` (:2129) through the closing brace of the `if (lightingGpu)` block (:2165) with:

```cpp
  // GPU lighting (Slice 2/3): when the lightingGpu flag is on, export the seeded emission + obstacle
  // grids and the point-light list for the GPU passes. exportSpreadInputs must run BEFORE calculate(),
  // which overwrites the cells with the spread result.
  //
  // Both scopes ENCLOSE their `if` rather than sitting inside it. A scope inside the branch would have to
  // be Cadence::Call to stop coverage_scale inflating it when GPU lighting is off; enclosing it means the
  // phase fires on every recompute, reports 100% coverage, and simply records ~0 when the branch is not
  // taken. That keeps the whole owner free of coverage scaling, which is where this campaign's arithmetic
  // errors have repeatedly come from. Cost: one predictable branch test.
  //
  // export and convert are separate phases because they have DIFFERENT scaling laws and different levers:
  // export is a cache-hostile transposing scatter over the calc region, convert is a compute-bound
  // per-element conversion over 3x that count.
  int lightMapBorder = 0;
  {
    TelemetryScope exportScope(exportTimer);
    if (lightingGpu) {
      m_lightingCalculator.exportSpreadInputs(m_pendingLightingEmission, m_pendingLightingObstacle);
      m_lightingCalculator.exportPointLights(m_pendingLightingPointLights);
      // Border (cells) between the calc-region-sized GPU result and the query region the world shader
      // samples. calculationRegion == queryRegion(lightRange).padded(borderCells), so this is exactly
      // borderCells. Computed here from the calculator's geometry and carried in renderData; WorldPainter
      // must NOT reverse-derive it from the CPU lightMap width, which is empty when the CPU calc is skipped.
      lightMapBorder = ((int)m_lightingCalculator.calculationRegion().width() - (int)lightRange.width()) / 2;
    }
  }
  {
    TelemetryScope convertScope(convertTimer);
    if (lightingGpu) {
      // Convert the RGB_F emission grid to 16-bit half-floats HERE (lighting thread, idle) so the render
      // thread uploads RGB16F -- half the per-frame transfer/store. No precision loss (the spread FBOs
      // are already 16F). The RGB_F emission is still kept for the auto-K scan + shadow-compare reference.
      {
        float const* ef = (float const*)m_pendingLightingEmission.data();
        size_t n = (size_t)m_pendingLightingEmission.size()[0] * m_pendingLightingEmission.size()[1] * 3;
        m_pendingLightingEmissionHalf.resize(n);
        uint16_t* hf = m_pendingLightingEmissionHalf.ptr();
        for (size_t i = 0; i < n; ++i)
          hf[i] = floatToHalf(ef[i]);
      }
      // Extract the obstacle mask's R channel (RGB24 0/255) into a single-channel R8 buffer so the GPU
      // upload is R8 (a third the bytes); the shaders already read obstacle as .r.
      {
        uint8_t const* ob = (uint8_t const*)m_pendingLightingObstacle.data();
        size_t cells = (size_t)m_pendingLightingObstacle.size()[0] * m_pendingLightingObstacle.size()[1];
        m_pendingLightingObstacleR8.resize(cells);
        uint8_t* r8 = m_pendingLightingObstacleR8.ptr();
        for (size_t i = 0; i < cells; ++i)
          r8[i] = ob[i * 3];   // R channel of each RGB24 texel
      }
    }
  }
```

- [ ] **Step 6: Wrap calculate and publish**

Replace everything from `bool skipCpuCalc = ...` (:2179) to the end of the function with:

```cpp
  {
    TelemetryScope calculateScope(calculateTimer);
    bool skipCpuCalc = lightingGpu && !shadowCompare && m_gpuLightingActive.load(std::memory_order_relaxed);
    if (skipCpuCalc) {
      calcSkipped.inc(1);
    } else {
      m_lightingCalculator.calculate(m_pendingLightMap);
      calcRan.inc(1);
    }
  }
  {
    TelemetryScope publishScope(publishTimer);
    MutexLocker mapLocker(m_lightMapMutex);
    m_lightMinPosition = lightRange.min();
    m_lightMap = std::move(m_pendingLightMap);
    m_lightingInputsValid = lightingGpu;
    if (lightingGpu) {
      m_lightingEmission = std::move(m_pendingLightingEmission);
      m_lightingObstacle = std::move(m_pendingLightingObstacle);
      m_lightingPointLights = std::move(m_pendingLightingPointLights);
      m_lightingEmissionHalf = std::move(m_pendingLightingEmissionHalf);
      m_lightingObstacleR8 = std::move(m_pendingLightingObstacleR8);
      m_lightingBorder = lightMapBorder;
    }
  }
}
```

Leave the `calcRan` / `calcSkipped` registrations and their explanatory comment where they are, above this block.

- [ ] **Step 7: Build**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 \
  cmake --build build/linux-release-clang --target star_game core_tests game_tests starbound -j 8
```
Expected: clean build, no warnings about unused variables.

- [ ] **Step 8: Run the byte-identity gate**

```bash
cd /root/frackin/OpenStarbound
command cp build/linux-release-clang/source/client/starbound dist/starbound
scripts/render-gate.sh
```
Expected: all three oracles report `ran>0`, `pass>0`, `DIFF=0`, `SKIPPED=0`, and the script prints PASS. Telemetry is non-functional, so any DIFF means the restructure changed behaviour — most likely a moved config read or a phase boundary that shifted a statement.

- [ ] **Step 9: Run both test suites**

```bash
./build/linux-release-clang/source/test/core_tests
./build/linux-release-clang/source/test/game_tests
```
Expected: all green.

- [ ] **Step 10: Commit**

```bash
git add source/game/StarWorldClient.cpp
git commit -m "telemetry: nine contiguous phases close the lighting CPU budget [#168]

Owner lighting reported 47.6% accounted -- one Budget part (gather) against a whole that covers the
entire function. The other eight phases had no timer at all.

The phases are CONTIGUOUS and EXHAUSTIVE between totalScope opening and the closing brace, so the
closure is exact rather than approximate. prologue is the only Frame-cadence part: it covers the work
paid on gate-skipped frames, which is what lets the Frame-cadence Total close against a mixed-cadence
parts list without a schema change (two Totals per owner are not representable -- the owner table is
keyed by owner name, last row wins).

export/convert/calculate wrap their `if` from OUTSIDE rather than sitting inside it. Inside, each would
need Cadence::Call to stop coverage_scale inflating it when the branch is off; outside, each fires every
recompute at 100% coverage and records ~0. The whole owner is now coverage-scale-free -- which is where
this campaign's arithmetic errors have repeatedly come from.

Folded in: lightingGpu/lightingGpuShadowCompare are read once in params instead of twice with the whole
gather between them. Needed for phase contiguity, and it removes a real latent inconsistency where a
mid-function config flip could pair a promote decision with the opposite export decision.

Adds lighting.lights.sources -- the true O(lights) denominator. lights.{spread,point} count ADDS, and a
promoted light adds one of each.

render-gate.sh PASS (3/3 oracles, DIFF=0). core_tests + game_tests green."
```

---

## Task 4: Guard the lighting owner contract in `core_tests`

**Why:** `OwnersDeclareDenominatorAndTotal` (`telemetry_test.cpp:310-321`) pins `frame`/`gl`/`sim` but never touches `lighting`. Re-scoping the Total or renaming the denominator currently breaks nothing and no test notices.

**Files:**
- Modify: `source/test/telemetry_test.cpp`

- [ ] **Step 1: Write the test**

Add to `source/test/telemetry_test.cpp`, immediately after `OwnersDeclareDenominatorAndTotal`:

```cpp
// The lighting owner's contract had no automated guard: its denominator counts RECOMPUTES while its
// total accumulates over FRAMES, and that mismatch is deliberate -- it is the one live instance of a
// legitimately mixed-cadence owner, and the reason ASSERTION 2 in telemetry-window.py checks each metric
// against its OWN cadence rather than the owner's ticks. Silently "fixing" it by re-scoping the Total
// would delete the only measurement of the temporal gate's skip path. Pin it.
TEST(Telemetry, LightingOwnerDeclaresRecomputeDenominatorAndFrameTotal) {
  Telemetry::reset();
  Json owners = Telemetry::snapshot().getObject("owners");
  ASSERT_TRUE(owners.contains("lighting"));
  Json lighting = owners.get("lighting");
  EXPECT_EQ(lighting.getString("denominator"), "lighting.temporal.recomputed");
  EXPECT_EQ(lighting.getString("total"), "lighting.cpu.total.us");
}
```

- [ ] **Step 2: Build and run**

```bash
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target core_tests -j 8
./build/linux-release-clang/source/test/core_tests --gtest_filter='Telemetry.*'
```
Expected: PASS. If the assertion names do not match, read `c_ownerSpecs` in `source/core/StarTelemetry.cpp` and use the literal strings from there — do not change the table to match the test.

- [ ] **Step 3: Commit**

```bash
git add source/test/telemetry_test.cpp
git commit -m "test: pin the lighting owner's denominator/total contract [#168]

The one owner whose Total and denominator differ in CADENCE (frames vs recomputes) was the only one
with no test. That mismatch is load-bearing -- it is why the consumer checks each metric against its own
cadence -- and re-scoping the Total would silently delete the temporal gate's skip-path measurement."
```

---

## Task 5: Baseline capture — prove the budget closes

**Why:** The instrumentation is worthless until it is shown to close on live data, and every lever in Stage 2 is measured against this capture.

- [ ] **Step 1: Confirm the harness config**

```bash
cd /root/frackin/OpenStarbound
python3 -c "
import json; c=json.load(open('harness/storage-perf/starbound.config'))
for k in ['telemetryDeepTracing','telemetryEnabled','vsync','lightingGpu','envOracle','parallaxOracle','lightingSpreadOracle']:
    print(f'  {k} = {c.get(k)!r}')
"
```
Expected: `telemetryDeepTracing=True`, `telemetryEnabled=True`, `vsync=False`, `lightingGpu=True`, all three oracles `False`. If deep tracing is off, every new timer records nothing and the budget will look worse, not better.

- [ ] **Step 2: Capture**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
scripts/render-profile.sh 120 lighting-closed --warp exploring
```

- [ ] **Step 3: Check closure**

```bash
python3 -c "
import json; d=json.load(open('harness/profiles/lighting-closed.json'))
print('violations:', d['violations'])
w=d['metrics']
den=w['lighting.temporal.recomputed']['value']; tot=w['lighting.cpu.total.us']
parts={k:v for k,v in w.items() if v.get('owner')=='lighting' and v.get('role')=='budget'}
s=sum(v['total'] for v in parts.values())
print(f'recomputes={den} whole={tot[\"total\"]} parts={s} closed={100*s/tot[\"total\"]:.1f}%')
for k,v in sorted(parts.items(), key=lambda kv:-kv[1]['total']):
    print(f'  {k:<34} {v[\"cadence\"]:<9} count={v[\"count\"]:<6} {v[\"total\"]/den:8.1f} us/recompute')
print('calc.cells =', w.get('lighting.calc.cells',{}).get('value'), ' cells =', w.get('lighting.cells',{}).get('value'))
"
```

**Acceptance:**
- `violations` contains no `lighting/cpu` entry.
- closed ≥ **97%**.
- Every recompute-cadence part has `count == recomputes`; `prologue` has `count == frame count`.
- `lighting.calc.cells` > `lighting.cells` and both are non-zero and plausible.

If closure is below 97%, **do not proceed to Stage 2** — find the gap. The most likely causes, in order: a statement left outside every phase block; a phase whose scope does not enclose its `if`; deep tracing off.

- [ ] **Step 4: Record the baseline**

```bash
git add harness/profiles/lighting-closed.json
git commit -m "measure: lighting CPU budget baseline, closed [#168]

Records the per-phase numbers every Stage-2 lever is measured against."
```

If `harness/profiles/` is gitignored, skip the commit and instead paste the table into task #168 as a comment. **Do not** `git add -f` anything under `harness/`.

---

# STAGE 2 — LEVERS (each byte-identical, each its own commit)

> Every lever in this stage is byte-identical, so `scripts/render-gate.sh` must stay PASS for each one individually. Run it per commit, not once at the end.
>
> **On measurement:** CPU drifts ~10% between non-adjacent profile runs. Matching scene content is NOT evidence that two CPU timings are comparable — that error was made and retracted earlier in this campaign. Any claimed win needs an A-B-A replicate: baseline, lever, baseline again. If the two baselines differ by more than the claimed win, the win is not established.

## Task 6: L0 — stop recomputing a constant every recompute

**Why:** `setParameters(assets->json(...).set(...))` costs ~1–3 µs but does it with ~10 heap allocations and an acquisition of the **global** assets mutex, per recompute, to produce a value that only changes when `newLighting` or `monochrome` changes. Landed for hygiene, not magnitude.

**Files:**
- Modify: `source/game/StarWorldClient.hpp` (new members), `source/game/StarWorldClient.cpp` (the `params` phase)

- [ ] **Step 1: Add the cache members**

In `source/game/StarWorldClient.hpp`, alongside the other lighting-thread-private members, add:

```cpp
  // Lighting-thread-private cache for the calculator's parameter Json. The composed value depends only on
  // newLighting and monochrome, but it was rebuilt every recompute: an Assets::json lookup under the
  // GLOBAL assets mutex (plus a freshen() clock write under that lock), a Json::set that deep-copies the
  // whole config object, and 7 string-keyed lookups inside setParameters.
  bool m_lightingParamsValid = false;
  bool m_lightingParamsNewLighting = false;
  bool m_lightingParamsMonochrome = false;
```

- [ ] **Step 2: Gate the recompose**

In `source/game/StarWorldClient.cpp`, inside the `params` phase block from Task 3, replace the two calculator calls with:

```cpp
    // Recompose only when an input actually changed. setParameters/setMonochrome are idempotent, so
    // skipping them when nothing changed is byte-identical.
    if (!m_lightingParamsValid || newLighting != m_lightingParamsNewLighting
        || monochrome != m_lightingParamsMonochrome) {
      m_lightingCalculator.setParameters(root.assets()->json("/lighting.config:lighting").set("pointAdditive", newLighting));
      m_lightingCalculator.setMonochrome(monochrome);
      m_lightingParamsValid = true;
      m_lightingParamsNewLighting = newLighting;
      m_lightingParamsMonochrome = monochrome;
    }
```

- [ ] **Step 3: Invalidate on asset reload**

Find where `WorldClient` responds to a Root reload (search for `m_lightingCalculator.setParameters` at `StarWorldClient.cpp:2274-2275`). Add `m_lightingParamsValid = false;` immediately before those existing calls, so a `/reload` re-reads the asset.

- [ ] **Step 4: Build, gate, test**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target starbound core_tests game_tests -j 8
command cp build/linux-release-clang/source/client/starbound dist/starbound
scripts/render-gate.sh && ./build/linux-release-clang/source/test/core_tests && ./build/linux-release-clang/source/test/game_tests
```
Expected: gate PASS (3/3 oracles, DIFF=0), tests green.

- [ ] **Step 5: Commit**

```bash
git add source/game/StarWorldClient.hpp source/game/StarWorldClient.cpp
git commit -m "lighting: cache the calculator parameters instead of recomposing them per recompute [#168]

Every recompute did an Assets::json lookup under the GLOBAL assets mutex (plus a freshen() clock write
while holding it), a Json::set that deep-copies the whole 7-key config object, and 7 string-keyed
lookups in setParameters -- ~10 heap allocations to produce a value that changes only when newLighting
or monochrome changes. Invalidated on reload. Byte-identical; gate PASS."
```

## Task 7: L1 — stop emptying the buffers at publish

**Why:** the biggest lever, and it is one root cause behind three separate zero-fills. The publish block `std::move`s five buffers away, so on the next recompute `Image::reset` cannot take its same-size early-out and `resize` cannot take its capacity early-out — **~788 KB zeroed per recompute, immediately overwritten**.

**Files:**
- Modify: `source/game/StarWorldClient.cpp` (the publish phase), `source/base/StarCellularLighting.cpp:258-259` (the comment)

- [ ] **Step 1: Swap instead of move**

In the publish phase block, replace the five `std::move` assignments with swaps so the previous frame's allocations return to the pending buffers:

```cpp
    // SWAP the five GPU buffers, not move. Moving leaves the pending ones empty (Image::operator=(Image&&)
    // takes the source's data AND its dimensions; vector move leaves capacity 0), so next recompute every
    // reset()/resize() below misses its same-size early-out and re-allocates + ZERO-FILLS ~788 KB that is
    // then immediately overwritten in full. Swapping hands last frame's correctly-sized buffers back.
    //
    // m_lightMap KEEPS its move -- do NOT swap it. It is the one buffer that is not rewritten every
    // recompute: calculate() is skipped in GPU mode, so m_pendingLightMap stays empty, and moving is what
    // makes m_lightMap empty too. That emptiness is load-bearing -- see the lightMapBorder note in the
    // export phase: WorldPainter must not reverse-derive the border from a CPU lightMap width, and an
    // empty map is how the GPU path signals "there is no CPU lightmap this frame". A swap would hand it
    // stale non-empty data from two frames ago and the world would render against the wrong geometry.
    m_lightMinPosition = lightRange.min();
    m_lightMap = std::move(m_pendingLightMap);
    m_lightingInputsValid = lightingGpu;
    if (lightingGpu) {
      std::swap(m_lightingEmission, m_pendingLightingEmission);
      std::swap(m_lightingObstacle, m_pendingLightingObstacle);
      std::swap(m_lightingPointLights, m_pendingLightingPointLights);
      std::swap(m_lightingEmissionHalf, m_pendingLightingEmissionHalf);
      std::swap(m_lightingObstacleR8, m_pendingLightingObstacleR8);
      m_lightingBorder = lightMapBorder;
    }
```

- [ ] **Step 2: Verify the consumers tolerate a non-empty pending buffer**

This is the correctness-critical step. A swap is only safe for a buffer that is **fully rewritten** every recompute — otherwise stale content from two frames ago is published as current. Before building, confirm by reading the code that each swapped buffer qualifies:

- `exportSpreadInputs` (`StarCellularLighting.cpp:254-290`) writes **every** pixel of `m_pendingLightingEmission` and `m_pendingLightingObstacle` — it does, via the full `x`/`y` double loop.
- `exportPointLights` (`:293-305`) starts with `out.clear()` — it does.
- The `floatToHalf` and R8 loops write all of `[0, n)` after `resize(n)` — they do.

And confirm the buffer that is **excluded**: `m_pendingLightMap` is written only by `calculate()`, which `skipCpuCalc` skips in the shipping GPU config, so it does **not** qualify and must keep its `std::move`. Verify the consumer side of that emptiness in `waitForLighting()` (`StarWorldClient.cpp:1537`) and the `m_lightingInputsValid` / `lightMapBorder` path in `StarWorldPainter.cpp`.

If any swapped buffer turns out to have a partial-write path, **stop and report** — that one must revert to `std::move`, and its zero-fill must be addressed per-site instead (allocate without clearing).

- [ ] **Step 3: Fix the misleading comment**

In `source/base/StarCellularLighting.cpp:258-259`, replace `// RGB_F float emission for the GPU spread; RGB24 obstacle mask (no single-channel format exists, the shader reads .r). reset() zero-fills.` with:

```cpp
  // RGB_F float emission for the GPU spread; RGB24 obstacle mask (no single-channel format exists, the
  // shader reads .r). NOTE: Image::reset does NOT zero-fill on a same-size call -- it early-outs. The loop
  // below writes every pixel, so that is correct and deliberate; do not add a clear back.
```

- [ ] **Step 4: Build, gate, test**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target starbound core_tests game_tests -j 8
command cp build/linux-release-clang/source/client/starbound dist/starbound
scripts/render-gate.sh && ./build/linux-release-clang/source/test/core_tests && ./build/linux-release-clang/source/test/game_tests
```
Expected: gate PASS. A DIFF here means a consumer *did* rely on cleared content — revert and go per-site.

- [ ] **Step 5: Commit**

```bash
git add source/game/StarWorldClient.cpp source/base/StarCellularLighting.cpp
git commit -m "lighting: swap the published buffers instead of moving them [#168]

The publish block moved five buffers away, leaving the pending ones empty -- Image::operator=(Image&&)
takes the source's dimensions as well as its data, and a vector move leaves capacity 0. So on the next
recompute every reset()/resize() missed its early-out and re-allocated + zero-filled ~788 KB which the
export and conversion loops then overwrote in full.

One root cause behind three separate zero-fills (emission RGB_F, obstacle RGB24, the fp16 and R8
buffers). Swapping hands last frame's correctly-sized allocations back, so every early-out hits in
steady state. Verified every swapped buffer is rewritten in full each recompute.

m_lightMap deliberately KEEPS its move: calculate() is skipped in GPU mode so m_pendingLightMap is not
rewritten, and the resulting emptiness is how the GPU path signals 'no CPU lightmap this frame'. A swap
there would publish stale geometry. Byte-identical; gate PASS."
```

## Task 8: L2 — untranspose the export loop

**Why:** `exportSpreadInputs` iterates `x` outer / `y` inner over a column-major cell array while writing row-major at `(y·width + x)·3`. Consecutive inner iterations write addresses ~2,688 B apart, so every store lands on a different cache line, and the loop sweeps a 538 KB working set 5–6 times.

**Files:**
- Modify: `source/base/StarCellularLighting.cpp:254-290`

- [ ] **Step 1: Reorder to destination order**

Swap the loop nesting so `y` is outer and `x` inner, making the image writes sequential. The cell reads become strided instead — which is the better trade, because the cell array (573 KB read) is read-only and prefetches cleanly, while the writes are the ones with a read-for-ownership cost. Preserve the monochrome/colored branch structure exactly.

```cpp
  if (m_monochrome) {
    m_lightArray.right().seedSpreadLights();
    for (unsigned y = 0; y < height; ++y) {
      for (unsigned x = 0; x < width; ++x) {
        auto const& cell = m_lightArray.right().cellAtIndex((size_t)x * height + y);
        size_t pixel = ((size_t)y * width + x) * 3;
        emissionData[pixel] = emissionData[pixel + 1] = emissionData[pixel + 2] = cell.light;
        obstacle.set24(x, y, cell.obstacle ? obstacleByte : airByte);
      }
    }
  } else {
    m_lightArray.left().seedSpreadLights();
    for (unsigned y = 0; y < height; ++y) {
      for (unsigned x = 0; x < width; ++x) {
        auto const& cell = m_lightArray.left().cellAtIndex((size_t)x * height + y);
        size_t pixel = ((size_t)y * width + x) * 3;
        emissionData[pixel] = cell.light[0];
        emissionData[pixel + 1] = cell.light[1];
        emissionData[pixel + 2] = cell.light[2];
        obstacle.set24(x, y, cell.obstacle ? obstacleByte : airByte);
      }
    }
  }
```

Update the `// Cell index x * height + y (array column-major) -> image pixel (x, y).` comment to:

```cpp
  // Cell index x*height+y (array column-major) -> image pixel (x, y) (row-major). The loops iterate in
  // DESTINATION order (y outer) so the image writes are sequential; the cell reads take the stride
  // instead. That is the right way round -- the writes carry a read-for-ownership cost the reads do not,
  // and the cell array prefetches cleanly. Iterating in source order made every single store land on a
  // different cache line.
```

- [ ] **Step 2: Build, gate, test**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target starbound core_tests game_tests -j 8
command cp build/linux-release-clang/source/client/starbound dist/starbound
scripts/render-gate.sh && ./build/linux-release-clang/source/test/core_tests && ./build/linux-release-clang/source/test/game_tests
```
Expected: gate PASS — same output bytes, different traversal order.

- [ ] **Step 3: Commit**

```bash
git add source/base/StarCellularLighting.cpp
git commit -m "lighting: iterate the spread-input export in destination order [#168]

The loop read a column-major cell array in source order (x outer) while writing a row-major image, so
consecutive inner iterations wrote addresses ~2,688 B apart -- a cache line per store, over a 538 KB
working set swept 5-6 times. Iterating y-outer makes the writes sequential and moves the stride onto the
read side, which is the cheaper place for it. Byte-identical; gate PASS."
```

## Task 9: L3a — make `floatToHalf` branchless

**Why:** 107,520 calls per recompute of a scalar bit-twiddle whose two branches block auto-vectorisation. Build is `-O3` with **no `-march`**, so explicit F16C is unavailable without a dispatch layer this tree has no precedent for.

**Files:**
- Modify: `source/game/StarWorldClient.cpp:28-46`

- [ ] **Step 1: Rewrite branchlessly**

Replace `floatToHalf` with a select-arithmetic version producing **bit-identical** results to the current one for every input. The two branches to eliminate are `exp <= 0` (→ signed zero) and `exp >= 31` (→ signed inf); the `mant & 0x1000` round-up stays but becomes an unconditional add of a computed 0/1.

```cpp
// IEEE-754 float32 -> float16 (half) with round-to-nearest. Used to pre-convert the GPU-lighting
// emission grid on the lighting thread so the render thread uploads RGB16F (half the bytes). Lighting
// values are non-negative and moderate, so the simple range handling (flush tiny to 0, clamp big to
// inf) is sufficient; the spread pipeline already runs at 16F precision.
//
// BRANCHLESS on purpose. This runs calcCells*3 times per recompute (~107,520 at a 128x64 query region),
// and the two range branches were what stopped the compiler vectorising the caller's loop. Every result
// is bit-identical to the branching version -- this is a byte-identical change, verified by the render
// gate's spreadoracle, NOT a semantics change.
//
// Deliberately NOT F16C (_mm_cvtps_ph): it rounds half-to-EVEN and emits proper subnormals, while this
// rounds half AWAY FROM ZERO and flushes subnormals to signed zero. Those differ by 1 ULP on ~1-in-8192
// values, so it is an output change requiring a quality argument rather than byte-identity. It is also
// unreachable without -march/-mf16c plus runtime dispatch plus an MSVC path, and this tree has no SIMD
// and has already taken one MSVC portability incident (__builtin_clzll). Tracked separately.
static uint16_t floatToHalf(float f) {
  uint32_t x;
  memcpy(&x, &f, sizeof(x));
  uint32_t sign = (x >> 16) & 0x8000u;
  int32_t exp = (int32_t)((x >> 23) & 0xffu) - 127 + 15;
  uint32_t mant = x & 0x7fffffu;

  // Normal-range result, computed unconditionally. The round-up is the dropped bits' MSB, added rather
  // than branched on; a mantissa carry propagates into the exponent field exactly as `++h` did, because
  // the fields are contiguous.
  uint32_t normal = sign | ((uint32_t)exp << 10) | (mant >> 13);
  normal += (mant >> 12) & 1u;

  // Select without branching: underflow (exp <= 0) -> signed zero; overflow (exp >= 31) -> signed inf.
  // Masks are all-ones or all-zero from the sign bit of the comparison, so no cmov is required either.
  uint32_t underflow = (uint32_t)(int32_t)(-(exp <= 0));
  uint32_t overflow = (uint32_t)(int32_t)(-(exp >= 31));
  uint32_t result = (normal & ~(underflow | overflow))
                  | (sign & underflow)
                  | ((sign | 0x7c00u) & overflow);
  return (uint16_t)result;
}
```

- [ ] **Step 2: Prove bit-identity exhaustively before trusting the gate**

The gate only exercises the float values a frozen scene happens to produce. Write a throwaway check that covers **every** `float32` bit pattern:

```bash
cd /root/frackin/OpenStarbound
cat > /tmp/claude-0/-home-apnex/halfcheck.cpp <<'EOF'
#include <cstdint>
#include <cstring>
#include <cstdio>
static uint16_t oldF(float f) {
  uint32_t x; memcpy(&x, &f, sizeof(x));
  uint32_t sign = (x >> 16) & 0x8000u;
  int32_t exp = (int32_t)((x >> 23) & 0xffu) - 127 + 15;
  uint32_t mant = x & 0x7fffffu;
  if (exp <= 0) return (uint16_t)sign;
  if (exp >= 31) return (uint16_t)(sign | 0x7c00u);
  uint16_t h = (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
  if (mant & 0x1000u) ++h;
  return h;
}
static uint16_t newF(float f) {
  uint32_t x; memcpy(&x, &f, sizeof(x));
  uint32_t sign = (x >> 16) & 0x8000u;
  int32_t exp = (int32_t)((x >> 23) & 0xffu) - 127 + 15;
  uint32_t mant = x & 0x7fffffu;
  uint32_t normal = sign | ((uint32_t)exp << 10) | (mant >> 13);
  normal += (mant >> 12) & 1u;
  uint32_t underflow = (uint32_t)(int32_t)(-(exp <= 0));
  uint32_t overflow  = (uint32_t)(int32_t)(-(exp >= 31));
  uint32_t result = (normal & ~(underflow | overflow)) | (sign & underflow) | ((sign | 0x7c00u) & overflow);
  return (uint16_t)result;
}
int main() {
  uint64_t bad = 0, firstBad = 0;
  for (uint64_t i = 0; i <= 0xFFFFFFFFull; ++i) {
    uint32_t b = (uint32_t)i; float f; memcpy(&f, &b, sizeof(f));
    if (oldF(f) != newF(f)) { if (!bad) firstBad = b; ++bad; }
  }
  printf("mismatches: %llu (first bit pattern 0x%08llx)\n",
         (unsigned long long)bad, (unsigned long long)firstBad);
  return bad != 0;
}
EOF
taskset -c 6-15 nice -n 19 g++ -O2 -o /tmp/claude-0/-home-apnex/halfcheck /tmp/claude-0/-home-apnex/halfcheck.cpp
taskset -c 6-15 nice -n 19 /tmp/claude-0/-home-apnex/halfcheck
```
Expected: `mismatches: 0`. **If this reports any mismatch, stop** — fix the branchless form until it is exhaustively identical. Do not proceed on the gate alone; the gate cannot reach every bit pattern.

- [ ] **Step 3: Build, gate, test**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target starbound core_tests game_tests -j 8
command cp build/linux-release-clang/source/client/starbound dist/starbound
scripts/render-gate.sh && ./build/linux-release-clang/source/test/core_tests && ./build/linux-release-clang/source/test/game_tests
```

- [ ] **Step 4: Commit**

```bash
git add source/game/StarWorldClient.cpp
git commit -m "lighting: branchless floatToHalf so the conversion loop can vectorise [#168]

107,520 calls per recompute; the two range branches were what stopped the compiler vectorising the
caller's loop. The branches become select arithmetic and the round-up becomes an unconditional add.

Verified bit-identical against the branching version over ALL 2^32 float bit patterns -- the render gate
alone cannot reach them, so it is not sufficient evidence here.

NOT F16C: vcvtps2ph rounds half-to-even and emits subnormals where this rounds half-away-from-zero and
flushes them, so it is an output change needing a quality argument, and it is unreachable without
-march plus runtime dispatch plus an MSVC path. Tracked separately."
```

## Task 10: Final measurement and close-out

- [ ] **Step 1: A-B-A replicate**

```bash
pgrep -f 'starbound|OpenStarbound' && echo "DIRECTOR IN GAME -- STOP" && exit 1
cd /root/frackin/OpenStarbound
scripts/render-profile.sh 120 levers-A --warp exploring
scripts/render-profile.sh 120 levers-B --warp exploring
scripts/render-profile.sh 120 levers-C --warp exploring
```

- [ ] **Step 2: Compare against the Stage-1 baseline**

```bash
python3 -c "
import json
def load(n):
    d=json.load(open(f'harness/profiles/{n}.json')); w=d['metrics']
    den=w['lighting.temporal.recomputed']['value']
    return {k:v['total']/den for k,v in w.items() if v.get('owner')=='lighting' and v.get('role')=='budget'}, den
base,_=load('lighting-closed')
runs=[load(n)[0] for n in ('levers-A','levers-B','levers-C')]
keys=sorted(set(base)|set(runs[0]))
print(f'{\"phase\":<34}{\"before\":>10}{\"A\":>10}{\"B\":>10}{\"C\":>10}{\"spread\":>9}')
for k in keys:
    vs=[r.get(k,0) for r in runs]
    print(f'{k:<34}{base.get(k,0):10.1f}{vs[0]:10.1f}{vs[1]:10.1f}{vs[2]:10.1f}{max(vs)-min(vs):9.1f}')
"
```

**Read it honestly.** The `spread` column is the run-to-run noise floor. A phase whose before→after delta is smaller than its own spread has **not** been shown to improve. Report those as "no effect established", not as small wins.

- [ ] **Step 3: Confirm the budget still closes**

Re-run the Task 5 Step 3 closure check against `levers-A`. Acceptance: still ≥97%, no `lighting/cpu` violation. The levers move phase magnitudes; if attribution moved, something is double-counted.

- [ ] **Step 4: Update the telemetry architecture doc**

In `docs/telemetry/architecture.md`, add the lighting owner's phase table to the section that documents the frame and gl budgets, and add the "conditional phases wrap the `if`" rule to §7 Traps, citing this task.

- [ ] **Step 5: File the follow-ups**

Create tasks for the work this arc deliberately excluded, each citing the spec:
1. **L3b — F16C `vcvtps2ph`**: ~10–30× on the conversion phase, but not byte-identical (half-to-even vs half-away-from-zero, subnormals vs flush). Needs a quality oracle and Director sign-off on a bounded output change.
2. **L4 — the border multiplier**: every O(cells) phase scales with `(Qw+96)(Qh+96)`, 4.375× the query region at 128×64. Shrinking the border or exporting only the sampled sub-region moves every phase at once. Architectural; gate #161's measurement phase on it.
3. **Producer-side lighting CPU**: the entity light-source walk (`StarWorldClient.cpp:534-541`), the particle light gather (`:551`), the render-thread `maxEmission` scan (`StarWorldPainter.cpp:143-150`), `adjustLighting` (`TilePainter.cpp:38-55`), and the `LogMap::set` global-mutex hit on every lighting wakeup (`:2211`). All real lighting cost billed to owner `frame`. Attributing them needs a second Total per owner — a telemetry model change.

- [ ] **Step 6: Commit and push**

```bash
cd /root/frackin/OpenStarbound
git add docs/telemetry/architecture.md
git commit -m "docs(telemetry): the lighting owner's phase budget + the wrap-the-if rule [#168]"
git push origin integration
```

- [ ] **Step 7: Close #168**

Update task #168 with: the closure percentage achieved, the per-phase table, each lever's A-B-A result (including any that showed no effect), and the three follow-up task IDs.
