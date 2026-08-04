# PR 570 — findings ledger

**Generated**, not transcribed — from the three workflow outputs preserved at
`/root/analysis/osb-pr570/raw/`. Regenerate rather than hand-edit; hand edits will be lost and
will disagree with the analysis.

Subject: OpenStarbound PR 570 (`psychosomat`), snapshot at `/root/analysis/osb-pr570/`, compared
against our fork at `566fce3d` on `integration`. Third-party code, no licence — ideas only, never
their code. See `/root/analysis/osb-pr570/PROVENANCE.txt`.

**Every row is a decision or an explicit no-action.** The `next steps` column lists the legal
options for that row; nothing else is a valid move without recording why.

## Where the decisions are

| section | rows | needing a decision |
|---|---:|---:|
| A. Bug-fix / removal claims checked | 12 | 2 |
| B. Claims enumerated but NOT checked | 35 | 35 |
| C. Head-to-head axis verdicts | 21 | 8 |
| D. Overlap topics | 66 | 12 |
| E. Recommendations (verified) | 21 | 10 |

## Open decisions — the working list

Every row below needs a call. Rows not listed here are closed with no action. Work top-down; the
order is severity, then cost-to-close, then hygiene.

| row | what is being decided | filed |
|---|---|---|
| **A05** | sector-unload-uaf (serious) | #210 |
| **A07** | per-sample-shading-removed (moderate) | #211 |
| **C05** | close the simplicity gap on: Recomputing only the changed part of the lightmap: their cd206aca "sc… | — |
| **C07** | close the quality gap on: Cutting the CPU cost of the cellular light computation | — |
| **C08** | close the simplicity gap on: Cutting the CPU cost of the cellular light computation | — |
| **C09** | close the performance gap on: Cutting the CPU cost of the cellular light computation | — |
| **C11** | close the simplicity gap on: point lights: cost versus fidelity | — |
| **C14** | close the simplicity gap on: AA-mode change at runtime: PR570 a86cc0f8 "Fix MSAA crash on SSAA tog… | — |
| **C16** | close the quality gap on: Proving the cellular lighting correct and fast | — |
| **C17** | close the simplicity gap on: Proving the cellular lighting correct and fast | — |
| **D11** | Automated differential oracle for the scroll path — they-are-ahead | — |
| **D13** | CPU spread+point cost itself — we-lack-entirely | — |
| **D22** | Incremental point-light add/remove diff (signed +/-1 flood) — we-lack-entirely | — |
| **D29** | Point-light count cap — we-lack-entirely | — |
| **D30** | asyncLighting default and the sector-unload race — they-are-ahead | — |
| **D31** | Template duplication between the Scalar and Colored point specializations — they-are-ahead | — |
| **D34** | Dead vendored dependencies — they-are-ahead | — |
| **D45** | Runtime VSync toggle in the graphics menu — we-lack-entirely | — |
| **D55** | Registering the overlay so a pristine clone finds it — we-lack-entirely | #196 |
| **D59** | Vendored tinyformat.h — they-are-ahead | — |
| **D60** | lib/linux/libcrypto.a — they-are-ahead | — |
| **D64** | Dependency-provenance document — we-lack-entirely | — |
| **E01** | Extract the A2 scroll math (delta -> overlap copy + the two margin rects) from WorldClien… | — |
| **E02** | Meter the gather-cache outcome: three Telemetry counters (hit / scroll / full) plus a rea… | — |
| **E03** | In WorldClient::shiftAndGatherMargin, zero only the vacated L-region of the scratch buffe… | — |
| **E04** | A full-recalc oracle for the WorldClient gather cache: drive lightingStableGather and the… | — |
| **E05** | Close the sector-unload / lighting-thread race: hold m_lightMapPrepMutex across the unloa… | #210 |
| **E06** | Warn (or hard-fail in the harness) at loadConfig when a framebuffer that declares "multis… | — |
| **E07** | Register the vcpkg overlay ports declaratively in-repo (either "overlay-ports" in source/… | — |
| **E08** | Record an explicit retirement condition for each vcpkg overlay port (in the portfile or a… | — |
| **E09** | Make the allocator dependency a vcpkg manifest FEATURE driven by the CMake option (gate j… | — |
| **E10** | Write a per-artefact provenance table for source/extern (version, upstream URL, why-vendo… | — |
| **B01-B35** | check or drop 35 unexamined claims | — |

---

## A. Bug-fix and removal claims — checked against our tree

| id | claim | was it real | our status | sev | next steps |
|---|---|---|---|---|---|
| **A01** | `msaa-main-only` — The defect is real and we proved it on hardware — but we fixed it on 2026-07-13/14, three weeks before their commit, with a strictly more general per-framebuffer opt-in (`multisampled`) rat… | real | **we-already-fixed-it** | none | CLOSE (no action) |
| **A02** | `restore-ray-traced-point-light` — Claim's structural assertions all check out: commits 1-2 never touch StarCellularLightArray.hpp, and e847309b's own TODO.md diff records the Dial/flood-fill being written (3.1/3.2/3.3) and … | cannot-determine | **not-applicable-to-us** | none | CLOSE (no action) |
| **A03** | `beam-direction-uninit` — Their fix is present at PR head (StarCellularLightArray.hpp:432 computes `direction` before the beam test), but the broken state it fixes never existed in any commit of this PR — it was a t… | cannot-determine | **not-applicable-to-us** | none | CLOSE (no action) |
| **A04** | `soa-channel-aliasing` — The claimed SoA channel-aliasing bug (point phase writing dst[0][1]/dst[0][2] into the red channel's next two cells) exists nowhere in the PR snapshot — base, any of the 8 commits, full.dif… | cannot-determine | **not-applicable-to-us** | none | CLOSE (no action) |
| **A05** | `sector-unload-uaf` — Real defect, and we still carry it verbatim. The lighting thread gathers tiles via a worker-pool fan-out that dereferences raw `Array*` pointers into sector storage, while `WorldClient::upd… | real | **we-still-have-it** | serious | FIX · DEFER · ACCEPT-RISK (record why) |
| **A06** | `async-default-reverted` — Their commit 1 flipped m_asyncLighting to true and commit 5 flipped it back to false, so the "fix" reverts a regression they introduced inside their own PR — base was already false, and the… | overstated | **not-applicable-to-us** | none | CLOSE (no action) |
| **A07** | `per-sample-shading-removed` — The unadvertised half is real in substance — glEnable(GL_SAMPLE_SHADING)/glMinSampleShading are GL 4.0 entry points called with no capability guard, while the sibling framebuffer multisampl… | overstated | **we-still-have-it** | moderate | FIX · DEFER · ACCEPT-RISK (record why) |
| **A08** | `point-layer-spread-feedback` — The described feedback loop is a real hazard of THEIR new persistent/scrolled lightmap design, but it was never a live defect: the point-channel split and the lightAtIndex() seed change lan… | overstated | **not-applicable-to-us** | none | CLOSE (no action) |
| **A09** | `scrolled-strip-point-zeroing` — The zeroing loop is real and load-bearing inside their new scrolled path, but it fixes no pre-existing defect — it lands in the same commit (cd206aca) that introduces m_pointChannels, scrol… | overstated | **not-applicable-to-us** | none | CLOSE (no action) |
| **A10** | `border-entry-bands` — The hazard they guard against is real and correctly identified — their point phase is window-scoped while the cell array is the query region padded by borderCells(), so cells crossing from … | overstated | **not-applicable-to-us** | none | CLOSE (no action) |
| **A11** | `lastpointlights-coordinate-shift` — The shift line is a genuine, load-bearing invariant of THEIR new scrolling atlas — but it never fixed a defect: `m_lastPointLights`, `calculateIncremental` and `scroll()` are all net-new in… | overstated | **not-applicable-to-us** | none | CLOSE (no action) |
| **A12** | `pending-lights-lifetime` — Both "lifetime fixes" are original code in the commits that introduced the fields they guard, not fixes of any defective state — and the begin() clear is attributed to the wrong commit (e84… | overstated | **not-applicable-to-us** | none | CLOSE (no action) |

### A — evidence for the rows needing a decision

**A05 · `sector-unload-uaf`** — severity serious

- our evidence: `/root/frackin/OpenStarbound/source/game/StarWorldClient.cpp:1417-1421`
- filed as: #210

**A07 · `per-sample-shading-removed`** — severity moderate

- our evidence: `/root/frackin/OpenStarbound/source/application/StarRenderer_opengl.cpp:926 (glMinSampleShading(1.f), no GLEW guard); companion calls at :925, :928, :929; function spans :918-933`
- filed as: #211

---

## B. Claims enumerated but NOT checked

47 claims were enumerated; 12 were checked. The cap was mine and is logged, not hidden. Each row
below is an open question: nobody has established whether it is real or whether we carry it.

| id | claim slug | next steps |
|---|---|---|
| **B01** | `tile-version-counter` | CHECK · DROP (record why) |
| **B02** | `jemalloc-gcc16` | CHECK · DROP (record why) |
| **B03** | `test-dump-path-windows` | CHECK · DROP (record why) |
| **B04** | `vsync-ui` | CHECK · DROP (record why) |
| **B05** | `hdr-layout-claim-unsupported` | CHECK · DROP (record why) |
| **B06** | `lightmap-upload-version-gate` | CHECK · DROP (record why) |
| **B07** | `fullbright-version-gate-regression` | CHECK · DROP (record why) |
| **B08** | `point-light-cap-and-cull` | CHECK · DROP (record why) |
| **B09** | `lightmap-buffer-reuse` | CHECK · DROP (record why) |
| **B10** | `unset-sentinel-in-clip-rect` | CHECK · DROP (record why) |
| **B11** | `ci-vcpkg-binary-cache` | CHECK · DROP (record why) |
| **B12** | `dead-dependency-cleanup` | CHECK · DROP (record why) |
| **B13** | `rm-tinyformat-header` | CHECK · DROP (record why) |
| **B14** | `rm-tinyformat-cmake-entry` | CHECK · DROP (record why) |
| **B15** | `rm-libcrypto-binary` | CHECK · DROP (record why) |
| **B16** | `rm-libcrypto-implicit-linkpath` | CHECK · DROP (record why) |
| **B17** | `rm-mimalloc-vcpkg-dep` | CHECK · DROP (record why) |
| **B18** | `rm-mimalloc-residual-buildpath` | CHECK · DROP (record why) |
| **B19** | `rm-cellularlightarray-cpp` | CHECK · DROP (record why) |
| **B20** | `rm-aos-cell-storage` | CHECK · DROP (record why) |
| **B21** | `rm-lighttraits-spread-multiply` | CHECK · DROP (record why) |
| **B22** | `rm-per-sample-shading` | CHECK · DROP (record why) |
| **B23** | `rm-msaa-from-nonmain-framebuffers` | CHECK · DROP (record why) |
| **B24** | `rm-point-lights-over-cap` | CHECK · DROP (record why) |
| **B25** | `rm-offscreen-light-sources` | CHECK · DROP (record why) |
| **B26** | `rm-redundant-lightmap-upload` | CHECK · DROP (record why) |
| **B27** | `rm-commented-vcpkg-cache-ci` | CHECK · DROP (record why) |
| **B28** | `rm-todo-md-dev-notes` | CHECK · DROP (record why) |
| **B29** | `rm-dial-floodfill-invisible` | CHECK · DROP (record why) |
| **B30** | `rm-async-lighting-default` | CHECK · DROP (record why) |
| **B31** | `rm-lightingtilegather-noarg` | CHECK · DROP (record why) |
| **B32** | `rm-intra-pr-lighting-api` | CHECK · DROP (record why) |
| **B33** | `rm-lightmap-realloc` | CHECK · DROP (record why) |
| **B34** | `rm-tinyformat-attribution-left-behind` | CHECK · DROP (record why) |
| **B35** | `noise-clangformat-inflates-deletions` | CHECK · DROP (record why) |

---

## C. Head-to-head — one row per axis

Seven convergent pairs, judged on quality / simplicity / performance. All four `theirs` verdicts
survived an adversarial challenge; none was overturned.

| id | pair | axis | winner | why | next steps |
|---|---|---|---|---|---|
| **C01** | Keeping the lightmap stable while the view scrolls | quality | **tie** | Theirs wins the proof half outright: cellular_lighting_test.cpp:215 asserts scrolled == full-recalc per cell over 5 scroll steps with obstacles, and their gather is ONE region-parameterised function. We have zero automated coverage of the shift/margin math (s… | NO ACTION |
| **C02** | Keeping the lightmap stable while the view scrolls | simplicity | **ours** | Theirs is +428/-36 over 7 files and introduces a second storage layer (m_pointChannels, StarCellularLightArray.hpp:247) that changes getLight's meaning for every existing caller and forces setSpreadLightingPoints to stop using getLight. The rect derivation (S… | NO ACTION |
| **C03** | Keeping the lightmap stable while the view scrolls | performance | **ours** | We measured; they did not. Ours: lighting.cpu.gather.us ~93us with the cache on vs ~237us baseline (-60%) taken during ACTIVE scroll (210-289 recompute-frames/5s), with the baseline windows independently matching the pre-lever ~237us from a separate profile (… | NO ACTION |
| **C04** | Recomputing only the changed part of the lightmap: their cd… | quality | **ours** | Their lever very probably never runs, and nothing in their build could tell them. `tilesOrEnvChanged` (StarWorldClient.cpp:1844) compares `environmentLight` by float equality, and Sky::environmentLight() is a continuous SinWeight interpolation of dayCycle() (… | NO ACTION |
| **C05** | Recomputing only the changed part of the lightmap: their cd… | simplicity | **theirs** | Their patch is +428/-36 across 4 production files (~324 production lines + 104 test) and their lightingCalc is ~200 lines. Critically they PARAMETERISED the existing gather by region — `lightingTileGather(RectI const&)` (:1765) — so there is one copy of the p… | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C06** | Recomputing only the changed part of the lightmap: their cd… | performance | **ours** | Ours is measured, theirs is not. Our A2 scroll-shift: lighting.cpu.gather.us ~237us -> ~93us (-60%) on active-scroll windows (210-289 recompute-frames/5s), telemetry-windowed in-game A/B with the baseline windows independently cross-checking the pre-lever pro… | NO ACTION |
| **C07** | Cutting the CPU cost of the cellular light computation | quality | **theirs** | They attacked the phase that actually dominates and pinned the risky paths with equivalence oracles. `cellular_lighting_test.cpp:162 incrementalMovingLightMatchesFullRecalc` and `:215 scrolledStripsMatchFullRecalc` check the fast paths cell-by-cell against a … | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C08** | Cutting the CPU cost of the cellular light computation | simplicity | **theirs** | Production-surface cleanliness, counted: our two lighting-array files are 675 + 532 = 1207 lines, of which ~455 (38%) is GPU-oracle scaffolding shipped inside the production translation unit — spreadJacobiReference/PointParameters/ObstacleRaycast/pointLightin… | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C09** | Cutting the CPU cost of the cellular light computation | performance | **theirs** | Theirs is the only side with any before/after on CellularLightArray::calculate(), because ours provably never enters it. Verified arithmetic: calculateLightSpread self-enlarges the query rect by ceil(spreadMaxAir)=32 clamped to the array (StarCellularLightArr… | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C10** | point lights: cost versus fidelity | quality | **ours** | Fidelity: we ray-trace strictly MORE lights and drop none (StarWorldClient.cpp:2308-2311, promote 0.5 default-on); they cap point lights at 128 and drop the rest sorted by `a.color.max()` (tree StarWorldClient.cpp:1828-1832) — an unstable key on content whose… | NO ACTION |
| **C11** | point lights: cost versus fidelity | simplicity | **theirs** | Counting places that must change together to change the point-light model: theirs is ONE — tree/source/base/StarCellularLightArray.hpp:401-475, a single template covering scalar+colored, serial+parallel, full+incremental+scrolled. Ours is FOUR, in two languag… | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C12** | point lights: cost versus fidelity | performance | **cannot-determine** | Both sides measured; neither ran the other's workload or hardware, so no head-to-head exists. Theirs (reproducible, committed: tree/source/test/cellular_lighting_bench.cpp, mt19937 seed 42, CLI grid/iters): 280x175, spread 5.48->1.50 ms at 2 passes and 11.5->… | MEASURE · ACCEPT unknown |
| **C13** | AA-mode change at runtime: PR570 a86cc0f8 "Fix MSAA crash o… | quality | **ours** | Same root defect, but theirs stops at one third of it and proves none of it. PROOF: ours measured on an Intel Arc/Mesa headless A/B with AA on in both legs and one line different -- GL_INVALID_OPERATION at bind 60267 -> 0, mean world luminance 0.016682 -> 0.2… | NO ACTION |
| **C14** | AA-mode change at runtime: PR570 a86cc0f8 "Fix MSAA crash o… | simplicity | **theirs** | For the one defect BOTH actually fixed, theirs is the smaller correct shape: one line in one file, one place to change, and the policy cannot be mis-declared because there is no declaration. Ours splits the same rule across two artifacts that must agree -- th… | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C15** | AA-mode change at runtime: PR570 a86cc0f8 "Fix MSAA crash o… | performance | **tie** | Neither side measured a performance delta for this change, and there is essentially nothing to measure: both fixes run once per settings toggle inside loadConfig, which already destroys and reallocates every framebuffer. Theirs reports no numbers of any kind.… | NO ACTION |
| **C16** | Proving the cellular lighting correct and fast | quality | **theirs** | Three concrete gaps in ours that theirs closes. (1) GROUND TRUTH: every assertion in lighting_point_test.cpp/lighting_spread_test.cpp compares a reference against production `calculate()` — production IS the oracle, and both sides read the same params, so an … | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C17** | Proving the cellular lighting correct and fast | simplicity | **theirs** | 436 lines total (364 test + 72 bench), one file, four local helpers, zero additions to production classes for testability — they call setObstacle/addPointLight/calculate/getLight/scroll/applyPointLightDiff, all of which the game calls. A reader verifies `EXPE… | CLOSE THE GAP · DECLINE (record why) · DEFER |
| **C18** | Proving the cellular lighting correct and fast | performance | **ours** | Both measured; ours measured better, theirs measured more reproducibly. Ours: A-B-A interleaved 90s runs at a fixed warp point with an untouched control phase and a stated noise floor — export 123.9/125.1 -> 71.9/82.3, convert 104.2/106.0 -> 62.9/68.4, params… | NO ACTION |
| **C19** | Lightmap computed relative to the frame: sync-by-default + … | quality | **ours** | Their gate is defeated by an input that changes every frame by ~1e-5. `tilesOrEnvChanged` compares `m_sky->environmentLight().toRgbF()` by exact float equality (tree StarWorldClient.cpp:1844); Color stores Vec4F (tree StarColor.hpp:149) and environmentLight i… | NO ACTION |
| **C20** | Lightmap computed relative to the frame: sync-by-default + … | simplicity | **ours** | Ours: one 64-line self-contained header, two inputs plus a floor, one call site, one file to change to change behaviour. Theirs (skip half only): 3 free comparators in an anonymous namespace (~45 lines), 7 new WorldClient members, 8 `m_tileVersion.fetch_add` … | NO ACTION |
| **C21** | Lightmap computed relative to the frame: sync-by-default + … | performance | **ours** | Ours is the only side with a measured number for THIS lever: in-game RAPL A/B at 2 locations, GPU -6.9% / package -1.9%, replicated, visuals clean (docs/board.md:766), plus per-scene engagement rates from telemetry (recompute 56% calm / 77% machines / 100% we… | NO ACTION |

### C — pairs where a convergence was disputed

- **Cutting the CPU cost of the cellular light computation** — judged NOT a real convergence. CONVERGED=false, and the reason is the finding. Both branches are labelled 'lighting CPU', but they hit disjoint parts of the pipeline. Theirs rewrote CellularLightArray::calculate() — the spread sweep and the point flood. Ours never enters it: I verified by arithmetic that L4 leaves both sweeps doing bit-for-bit identical work (the 192x128 spread rect is invariant under the border change because calculateLightSpread self-enlarges by ceil(spreadMaxAir)=32 and clamps to the array; the point rect is the query rect either way), and B1/B2/L3b are gather and convert. That is also exactly why our render oracle came back pixel-identical, and why our -23.6% commit message lists gather/export/conver…

---

## D. Overlap topics — theirs vs ours

| id | theme | topic | verdict | next steps |
|---|---|---|---|---|
| **D01** | Scrollable lightmap atlas + dirty-strip r… | What state is retained across frames | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D02** | Scrollable lightmap atlas + dirty-strip r… | Drift / accumulated error | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D03** | Scrollable lightmap atlas + dirty-strip r… | Newly-exposed strip re-gather | **equivalent** | NO ACTION |
| **D04** | Scrollable lightmap atlas + dirty-strip r… | Shift mechanics | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D05** | Scrollable lightmap atlas + dirty-strip r… | Environment (sky) light under scroll | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D06** | Scrollable lightmap atlas + dirty-strip r… | Frame-level 'nothing changed, skip' gate | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D07** | Scrollable lightmap atlas + dirty-strip r… | Behaviour of the atlas under our actual content (FU) | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D08** | Scrollable lightmap atlas + dirty-strip r… | Window-size stability (the thing that decides whether a retained grid survives) | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D09** | Scrollable lightmap atlas + dirty-strip r… | Calculation border sizing | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D10** | Scrollable lightmap atlas + dirty-strip r… | Point-vs-spread layer separation (so a re-spread strip cannot absorb point light) | **equivalent** | NO ACTION |
| **D11** | Scrollable lightmap atlas + dirty-strip r… | Automated differential oracle for the scroll path | **they-are-ahead** | ADOPT the idea · DECLINE (record why) · DEFER |
| **D12** | Scrollable lightmap atlas + dirty-strip r… | Measured performance evidence | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D13** | Scrollable lightmap atlas + dirty-strip r… | CPU spread+point cost itself | **we-lack-entirely** | BUILD it · DECLINE (record why) · DEFER |
| **D14** | Scrollable lightmap atlas + dirty-strip r… | Output/upload side | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D15** | Scrollable lightmap atlas + dirty-strip r… | Large jump / teleport handling | **equivalent** | NO ACTION |
| **D16** | Scrollable lightmap atlas + dirty-strip r… | Cache-key coverage of tile changes | **equivalent** | NO ACTION |
| **D17** | Scrollable lightmap atlas + dirty-strip r… | Sector streaming in without an epoch/version bump | **equivalent** | NO ACTION |
| **D18** | CellularLightArray rewrite and point-ligh… | Where the point-light cost is paid | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D19** | CellularLightArray rewrite and point-ligh… | AoS -> SoA storage conversion with a write-through CellRef proxy | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D20** | CellularLightArray rewrite and point-ligh… | Producer-side skip gate | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D21** | CellularLightArray rewrite and point-ligh… | Scrolled/dirty-region incremental recompute | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D22** | CellularLightArray rewrite and point-ligh… | Incremental point-light add/remove diff (signed +/-1 flood) | **we-lack-entirely** | BUILD it · DECLINE (record why) · DEFER |
| **D23** | CellularLightArray rewrite and point-ligh… | Two-layer light (base+spread vs point in separate channels) | **equivalent** | NO ACTION |
| **D24** | CellularLightArray rewrite and point-ligh… | Calculation-region size / border padding | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D25** | CellularLightArray rewrite and point-ligh… | Lightmap texture upload gating | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D26** | CellularLightArray rewrite and point-ligh… | Output-buffer reuse across frames | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D27** | CellularLightArray rewrite and point-ligh… | Per-frame config/JSON work on the locked lighting path | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D28** | CellularLightArray rewrite and point-ligh… | Out-of-view light-source culling | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D29** | CellularLightArray rewrite and point-ligh… | Point-light count cap | **we-lack-entirely** | BUILD it · DECLINE (record why) · DEFER |
| **D30** | CellularLightArray rewrite and point-ligh… | asyncLighting default and the sector-unload race | **they-are-ahead** | ADOPT the idea · DECLINE (record why) · DEFER |
| **D31** | CellularLightArray rewrite and point-ligh… | Template duplication between the Scalar and Colored point specializations | **they-are-ahead** | ADOPT the idea · DECLINE (record why) · DEFER |
| **D32** | CellularLightArray rewrite and point-ligh… | MSAA per-sample shading | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D33** | CellularLightArray rewrite and point-ligh… | Automated evidence for the change | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D34** | CellularLightArray rewrite and point-ligh… | Dead vendored dependencies | **they-are-ahead** | ADOPT the idea · DECLINE (record why) · DEFER |
| **D35** | CellularLightArray rewrite and point-ligh… | jemalloc vs libstdc++ 16 (std::__throw_bad_alloc removed) | **equivalent** | NO ACTION |
| **D36** | CellularLightArray rewrite and point-ligh… | Reviewability of the change itself | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D37** | GL renderer: MSAA/SSAA confinement, AA/HD… | Confining MSAA to the one framebuffer that is resolved, not sampled | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D38** | GL renderer: MSAA/SSAA confinement, AA/HD… | Evidence that the MSAA defect is real | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D39** | GL renderer: MSAA/SSAA confinement, AA/HD… | Which upstream issue this actually fixes | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D40** | GL renderer: MSAA/SSAA confinement, AA/HD… | AA/HDR toggle rebuilds every FBO and orphans effect samplers | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D41** | GL renderer: MSAA/SSAA confinement, AA/HD… | FBO name leak on every AA/HDR toggle | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D42** | GL renderer: MSAA/SSAA confinement, AA/HD… | The bound-target cache surviving a target rebuild | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D43** | GL renderer: MSAA/SSAA confinement, AA/HD… | A sampler's size uniform after a realloc | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D44** | GL renderer: MSAA/SSAA confinement, AA/HD… | Removing per-sample shading (glEnable(GL_SAMPLE_SHADING) / glMinSampleShading(1.f)) | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D45** | GL renderer: MSAA/SSAA confinement, AA/HD… | Runtime VSync toggle in the graphics menu | **we-lack-entirely** | BUILD it · DECLINE (record why) · DEFER |
| **D46** | GL renderer: MSAA/SSAA confinement, AA/HD… | How a graphics option reaches the renderer | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D47** | GL renderer: MSAA/SSAA confinement, AA/HD… | HDR checkbox position in the graphics menu | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D48** | GL renderer: MSAA/SSAA confinement, AA/HD… | Guarding against a multisampled framebuffer ever being sampled | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D49** | GL renderer: MSAA/SSAA confinement, AA/HD… | `alpha` silently forced true by multisample | **equivalent** | NO ACTION |
| **D50** | GL renderer: MSAA/SSAA confinement, AA/HD… | Unguarded fetchChild on patched-in menu widgets | **equivalent** | NO ACTION |
| **D51** | GL renderer: MSAA/SSAA confinement, AA/HD… | Cancel semantics for graphics options | **equivalent** | NO ACTION |
| **D52** | GL renderer: MSAA/SSAA confinement, AA/HD… | Reviewability of the change | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D53** | Dependency cleanup, vcpkg manifest, and t… | jemalloc vs libstdc++ 16 — the fix itself | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D54** | Dependency cleanup, vcpkg manifest, and t… | Fragility of the fix under a jemalloc bump | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D55** | Dependency cleanup, vcpkg manifest, and t… | Registering the overlay so a pristine clone finds it | **we-lack-entirely** | BUILD it · DECLINE (record why) · DEFER |
| **D56** | Dependency cleanup, vcpkg manifest, and t… | Documenting WHY the overlay exists | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D57** | Dependency cleanup, vcpkg manifest, and t… | Overlay pins a hardcoded SHA and bypasses registry versioning | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D58** | Dependency cleanup, vcpkg manifest, and t… | Sibling Fedora-44 toolchain breakage (libsystemd / glibc C23 _Generic) | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D59** | Dependency cleanup, vcpkg manifest, and t… | Vendored tinyformat.h | **they-are-ahead** | ADOPT the idea · DECLINE (record why) · DEFER |
| **D60** | Dependency cleanup, vcpkg manifest, and t… | lib/linux/libcrypto.a | **they-are-ahead** | ADOPT the idea · DECLINE (record why) · DEFER |
| **D61** | Dependency cleanup, vcpkg manifest, and t… | mimalloc — paying to build an allocator nobody links | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D62** | Dependency cleanup, vcpkg manifest, and t… | The right way to make an unused dependency free | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D63** | Dependency cleanup, vcpkg manifest, and t… | builtin-baseline | **we-are-ahead** | NO ACTION · DOCUMENT the advantage |
| **D64** | Dependency cleanup, vcpkg manifest, and t… | Dependency-provenance document | **we-lack-entirely** | BUILD it · DECLINE (record why) · DEFER |
| **D65** | Dependency cleanup, vcpkg manifest, and t… | Windows CI vcpkg binary cache | **different-tradeoff** | NO ACTION · REVISIT if the tradeoff assumptions change |
| **D66** | Dependency cleanup, vcpkg manifest, and t… | Regression coverage for the GCC-16 case | **equivalent** | NO ACTION |

### D — detail for rows where they lead or we lack

**Automated differential oracle for the scroll path** — they-are-ahead

- theirs: scrolledStripsMatchFullRecalc (cellular_lighting_test.cpp:215-315): five accumulating scroll steps against a fresh full-recalc reference, all 60x40x3 floats compared at 1e-3. Genuinely good, and the one piece of real evidence in the PR.
- ours: No unit-level differential test for shiftAndGatherMargin. We have 19 lighting game_tests (not specific to the shift), a 4-lens adversarial review, and an in-game telemetry A/B. The shift math itself is unpinned by any automated test.

**CPU spread+point cost itself** — we-lack-entirely

- theirs: This is exactly what their atlas attacks — the dominant CPU lighting cost in an upstream-shaped build.
- ours: In the default config we do not pay it at all: GPU mode skips CPU calculate() (StarWorldClient.cpp:2399-2408). With lightingGpu=false it is 3,817 of 4,177 us/recompute and we optimise none of it. So their lever aims at a real cost that exists only in our non-default fallback path.

**Incremental point-light add/remove diff (signed +/-1 flood)** — we-lack-entirely

- theirs: addPointLightContribution(light, +/-1) reuses the same flood so a moved light's old contribution cancels bit-for-bit; a sorted merge-walk over old vs pending produces removed/added lists. Two unit tests pin the symmetry, one walks a light 240 steps against a full recalc.
- ours: Absent. We recompute the point layer from scratch every recompute. The temporal gate suppresses the recompute instead of making it cheaper.

**Point-light count cap** — we-lack-entirely

- theirs: maxPointLights, default 128, applied by sorting Point lights descending on color.max() and truncating. No distance or on-screen term, unstable sort so the retained set can churn every frame, and the key is undeclared in any config asset.
- ours: No cap. Every point light becomes a GPU draw. lighting.gpu.point.gpu_us is the fattest lighting GPU line (~2 ms/recompute at the Director's bases, task #132).

**asyncLighting default and the sector-unload race** — they-are-ahead

- theirs: m_asyncLighting flipped to true then back to false; separately, sector unloading is moved under m_lightMapPrepMutex so the lighting thread cannot hold freed SectorArray2D pointers.
- ours: m_asyncLighting = false (StarWorldClient.cpp:94), same default. But m_tileArray->unloadSector(sector) at :1420 is NOT under m_lightMapPrepMutex, and /asyncLighting is a live toggle, so the identical race is latent in our tree too.

**Template duplication between the Scalar and Colored point specializations** — they-are-ahead

- theirs: Both deleted, replaced by one templated pointLightFlood plus three new traits hooks (spreadDrops, subtractChannels, channels) writing into float[ComponentCount], with if constexpr collapsing the scalar case.
- ours: Both copies retained verbatim. They have already drifted: StarCellularLightArray.cpp:46 tests `light.beam > 0.0001f` (Scalar) while :119 tests `light.beam > 0.0f` (Colored). Numerically negligible, but it is exactly the drift class the unification removes, and nothing in our test suite compares the two instantiations.

**Dead vendored dependencies** — they-are-ahead

- theirs: Deletes lib/linux/libcrypto.a (4,041,132 B, zero CMake references) and source/extern/tinyformat.h (1164 lines, no includes outside extern); drops mimalloc from vcpkg.json; adds extern/README.md pinning vendored versions.
- ours: All three are still present and still dead: /root/frackin/OpenStarbound/lib/linux/libcrypto.a (4,041,132 B, grep over source/ cmake/ CMakeLists.txt finds zero references), source/extern/tinyformat.h (referenced only by source/extern/CMakeLists.txt:26), and mimalloc at source/vcpkg.json:14 while STAR_USE_MIMALLOC defaults OFF (source/CMakeLists.txt:143).

**Runtime VSync toggle in the graphics menu** — we-lack-entirely

- theirs: New vsyncLabel/vsyncCheckbox widgets, callback writes config and calls setVSyncEnabled immediately, 'vsync' added to ConfigKeys and syncGui. All engine plumbing pre-existed; the commit only makes it reachable.
- ours: Absent. The config key, the startup apply, and the adaptive-then-full setVSyncEnabled fallback all exist (StarClientApplication.cpp:68,282,313; StarMainApplication_sdl.cpp:1253-1265), and telemetry already records vsync as run meta — but there is no UI and no runtime re-read. Requires a restart.

**Registering the overlay so a pristine clone finds it** — we-lack-entirely

- theirs: `"overlay-ports": ["./vcpkg-overlay/ports"]` added to source/vcpkg-configuration.json:8, next to the pinned default-registry. Declarative, in-repo, honoured by vcpkg-tool and by lukka/run-vcpkg's vcpkgConfigurationJsonGlob — so it works locally and in CI with no per-invocation flags.
- ours: NOTHING registers it. Only VCPKG_OVERLAY_TRIPLETS is set (source/CMakePresets.json:18); the working build got its overlay from a hand-passed `-DVCPKG_OVERLAY_PORTS=/root/vcpkg-overlay-ports` pointing at a stray out-of-tree copy (build/linux-release-clang/CMakeCache.txt:431). This IS the whole of #196's toolchain half.

**Vendored tinyformat.h** — they-are-ahead

- theirs: Deleted (1164 lines) plus its entry in source/extern/CMakeLists.txt. Verified zero references. Left doc/OPENSOURCE.md:248's attribution dangling.
- ours: Still present at source/extern/tinyformat.h, still listed at source/extern/CMakeLists.txt:26, zero uses in source/. Identical dead weight, still carried.

**lib/linux/libcrypto.a** — they-are-ahead

- theirs: Deleted (4,041,132 B). Argument is `grep` emptiness plus an unshown green build; they did not address that CMAKE_LIBRARY_PATH puts it on an implicit find_library path.
- ours: Still present, byte-for-byte the same 4,041,132 B artefact, zero references outside build/, and on the implicit search path via source/CMakePresets.json:85 inherited by all four Linux presets. Same hazard, not yet removed.

**Dependency-provenance document** — we-lack-entirely

- theirs: New source/extern/README.md: a table of version + origin per vendored artefact plus a 3-bullet policy. Good idea; the execution carries two factual errors (the builtin-baseline claim, and malloc.c documented as a jemalloc shim when its own header says rpmalloc/Mattias Jansson, and it is not even compiled).
- ours: No source/extern/README.md and no equivalent table. docs/architecture/system-boundaries.md:183-187 names a "Toolchain" zone (cmake/vcpkg, 36 build files, 12 declared binaries) but records no per-artefact version or origin. We also have the same uncompiled source/extern/malloc.c sitting next to rpmalloc.c.

---

## E. Recommendations — adversarially verified

Each was given to a skeptic whose default was to reject it. 10 survived, 11 were refuted.

### E1 — survived

| id | recommendation | next steps |
|---|---|---|
| **E01** | Extract the A2 scroll math (delta -> overlap copy + the two margin rects) from WorldClient::shiftAndGatherMargin into a pure, header-only helper, and pin it with a differential unit test: synthetic tile grid, accumulating scroll steps (positive, negative, dia… | ADOPT · DEFER · DECLINE (record why) |
| **E02** | Meter the gather-cache outcome: three Telemetry counters (hit / scroll / full) plus a reason tag for the full path (epoch, dims, anchor jump, first frame), placed at the existing A1/A2 cache branch in WorldClient::lightingCalc. | ADOPT · DEFER · DECLINE (record why) |
| **E03** | In WorldClient::shiftAndGatherMargin, zero only the vacated L-region of the scratch buffer instead of assign()-zeroing the whole scratch before copying the overlap. | ADOPT · DEFER · DECLINE (record why) |
| **E04** | A full-recalc oracle for the WorldClient gather cache: drive lightingStableGather and the shiftAndGatherMargin scroll path through a sequence of camera moves and assert the resulting stable grid is identical to a fresh full gather at each step -- the shape of… | ADOPT · DEFER · DECLINE (record why) |
| **E05** | Close the sector-unload / lighting-thread race: hold m_lightMapPrepMutex across the unloadSector loop in WorldClient::update (or make async mode invalidate the gather cache and refuse to read m_tileArray during unload). | ADOPT · DEFER · DECLINE (record why) |
| **E06** | Warn (or hard-fail in the harness) at loadConfig when a framebuffer that declares "multisampled": true is also named by any effect's frameBufferTextures. | ADOPT · DEFER · DECLINE (record why) |
| **E07** | Register the vcpkg overlay ports declaratively in-repo (either "overlay-ports" in source/vcpkg-configuration.json or VCPKG_OVERLAY_PORTS in the `base` preset of source/CMakePresets.json) so a pristine clone finds the jemalloc/libsystemd overlay without a hand… | ADOPT · DEFER · DECLINE (record why) |
| **E08** | Record an explicit retirement condition for each vcpkg overlay port (in the portfile or a vcpkg-overlay-ports/README) stating what upstream event lets it be deleted: jemalloc "drop when the registry ships >5.3.1 or a GCC-16 fix"; libsystemd "registry is alrea… | ADOPT · DEFER · DECLINE (record why) |
| **E09** | Make the allocator dependency a vcpkg manifest FEATURE driven by the CMake option (gate jemalloc and mimalloc behind features, set VCPKG_MANIFEST_FEATURES from STAR_USE_JEMALLOC / STAR_USE_MIMALLOC before project(), drop the unconditional "mimalloc" at source… | ADOPT · DEFER · DECLINE (record why) |
| **E10** | Write a per-artefact provenance table for source/extern (version, upstream URL, why-vendored, whether compiled), generated/gated rather than hand-maintained — as another entry in the gates.yml script-gate set, diffing the table against star_extern_SOURCES/sta… | ADOPT · DEFER · DECLINE (record why) |

### E2 — refuted

A refuted recommendation is CLOSED unless the stated reason is itself wrong. The reason matters:
some died because we already have it, some because the benefit was never evidenced — and one or
two carry a true fact under a false argument.

| id | recommendation | why it died | next steps |
|---|---|---|---|
| **E11** | Bump m_lightingTileEpoch only when a lighting-relevant tile field actually changes — compare foreground/background material+mod, liquid presence/level, and the two light… | KILLED BY #3 (THE BENEFIT IS UNEVIDENCED — and the specific mechanism is refuted by our own code). Kill #1 does NOT apply: we genuinely lack the filter (/root/frackin/OpenStarbound/source/game/StarWorldClient.cpp:2733 b… | CLOSE · CHALLENGE the refutation |
| **E12** | Quantise/hysteresis the adaptive calculation border (#170) — round borderNeeded up to a multiple of 8 and only shrink after N consecutive frames below bucket — so calcDi… | DIES. Killed by mode 3 (benefit unevidenced — in fact contradicted by our own shipped measurement), with mode 1 assisting (the bucketing idea is already shipped at the level that actually breathes). THE PREMISE IS FALSE… | CLOSE · CHALLENGE the refutation |
| **E13** | A point-light budget for the GPU point pass, ranked by on-screen contribution (intensity attenuated by distance to the query rect), top-N with stable tie-break and hyste… | KILLED BY (1) WE ALREADY HAVE IT — as a named, deliberately-deferred design decision with an explicit evidence gate — reinforced by (3) THE BENEFIT IS UNEVIDENCED against that same gate. 1. The novelty claim ("the one p… | CLOSE · CHALLENGE the refutation |
| **E14** | Unify the two `calculatePointLighting` specializations into one template, importing PR 570's traits-hook mechanism (spreadDrops / subtractChannels / channels() writing i… | Killed by #2, IT DOES NOT APPLY — twice over — with #3 as a supporting kill. (a) The "transferable mechanism" is scaffolding we have no use for. I normalised the two bodies in /root/frackin/OpenStarbound/source/base/Sta… | CLOSE · CHALLENGE the refutation |
| **E15** | Delete the three dead dependencies PR 570 identified — lib/linux/libcrypto.a (4 MB, zero CMake references), source/extern/tinyformat.h (referenced only by source/extern/… | Killed by (1) WE ALREADY HAVE IT on the only load-bearing item, and (3) THE BENEFIT IS UNEVIDENCED on the headline claim. WHAT IS FACTUALLY TRUE IN OUR TREE (I verified all three): - /root/frackin/OpenStarbound/lib/linu… | CLOSE · CHALLENGE the refutation |
| **E16** | Add a VSync checkbox to the graphics menu, wired the way we already wire antiAliasing and hdr: re-read `vsync` from configuration in `ClientApplication::render()` each f… | The MECHANICAL half of the claim checks out — I could not kill it on "we already have it". Verified absences in our tree: no `vsyncCheckbox` anywhere in `source/` or `assets/` (grep over both is empty); `vsync` is absen… | CLOSE · CHALLENGE the refutation |
| **E17** | Measure what glMinSampleShading(1.f) costs per frame with the per-pass GPU timers, and if large, split per-sample shading out of the antiAliasing checkbox into its own s… | DIES — killer #1 (we already have it), with #3 (the benefit's premises are contradicted by our own measurements) as reinforcement. 1) WE ALREADY ENUMERATED AND CLOSED THIS LEVER, BY NAME AND BY THE SAME MECHANISM. The G… | CLOSE · CHALLENGE the refutation |
| **E18** | Make GraphicsMenu's lookups of patched-in widgets optional rather than throwing — one guarded helper used by all of them (antiAliasingCheckbox, hardwareCursorCheckbox, m… | Killed by (2) IT DOES NOT APPLY — as scoped, the fix does not fix the failure it names — compounded by (3) THE BENEFIT IS UNEVIDENCED (the load-bearing asymmetry claim is factually inverted). FOUR independent refutation… | CLOSE · CHALLENGE the refutation |
| **E19** | Give the graphics menu real Cancel semantics for antiAliasing/hdr (and vsync if added): stage them in m_localChanges, preview via Root, commit on accept, restore on dism… | KILLED BY #3 (THE BENEFIT IS UNEVIDENCED — and specifically disproven by our own write-up), with the factual premise also wrong. WHAT IS TRUE: we do lack it. /root/frackin/OpenStarbound/source/frontend/StarGraphicsMenu.… | CLOSE · CHALLENGE the refutation |
| **E20** | Delete source/extern/tinyformat.h with its entry at source/extern/CMakeLists.txt:26, delete lib/linux/libcrypto.a, and fix the now-dangling attribution at doc/OPENSOURCE… | Killed by #3, THE BENEFIT IS UNEVIDENCED — and stronger than unevidenced: the one claim of "real value beyond size" is disproven by our own configured build. The factual predicate checks out. Both files are present and … | CLOSE · CHALLENGE the refutation |
| **E21** | If Windows vcpkg binary caching is ever taken up, set the cache location explicitly (VCPKG_DEFAULT_BINARY_CACHE / VCPKG_BINARY_SOURCES pointing at a workspace-relative d… | Killed by (1) WE ALREADY HAVE IT for the first half, and (2) IT DOES NOT APPLY for the second half. The diagnosis of THEIR patch is correct; the derived action for US is a no-op or wrong. (1) WE ALREADY HAVE IT — the "s… | CLOSE · CHALLENGE the refutation |

### E1 — reasoning for the survivors

**E01** — Extract the A2 scroll math (delta -> overlap copy + the two margin rects) from WorldClient::shiftAndGatherMargin into a pure, header-only helper, and pin it with a differential unit test: synthetic tile grid, accumulating scroll steps (positive, negative, diagonal, corner-only, /delta/ just under d…

- All three kill routes were tried and all three fail. 1) WE ALREADY HAVE IT — refuted. The A2 scroll math lives entirely inside /root/frackin/OpenStarbound/source/game/StarWorldClient.cpp:1957-1993 (shiftAndGatherMargin) plus its dispatch at :2199-2214, and the retained state (GatherCell, m_gatherGrid, m_gatherScratch) is declared at source/game/StarWorldClient.hpp:410-416. A grep for shiftAndGatherMargin / gatherStableColumns / lightingStableGather / GatherCell / m_gatherGrid across source/ and docs/ returns hits ONLY in those two files. Nothing in source/test/ references any of them; nothing in source/test/ constructs a WorldClient at all (the only WorldClient tokens in tests are two comme…

**E02** — Meter the gather-cache outcome: three Telemetry counters (hit / scroll / full) plus a reason tag for the full path (epoch, dims, anchor jump, first frame), placed at the existing A1/A2 cache branch in WorldClient::lightingCalc.

- I tried all three kill routes and none landed. KILL 1 (we already have it) — FAILED. Exhaustive grep of every `Telemetry::counter` call site in `/root/frackin/OpenStarbound/source/` returns 40+ counters, and NONE is a gather-cache counter. The lighting counters we own are exactly: `lighting.lights.sources`, `lighting.temporal.recomputed`, `lighting.temporal.skipped` (StarWorldClient.cpp:2070-2075), `lighting.cpu.calc.{ran,skipped}` (:2395-2397), `lighting.lights.{spread,point}` (StarCellularLighting.cpp:136,147), `lighting.gpu.spread.passes` / `lighting.gpu.point.lights` (StarGpuLightmapPass.cpp:43,45), `lighting.gpu.point.mismatch` (StarWorldPainter.cpp:22). The published-metric table in `…

**E03** — In WorldClient::shiftAndGatherMargin, zero only the vacated L-region of the scratch buffer instead of assign()-zeroing the whole scratch before copying the overlap.

- I tried all three kill routes and none of them lands. 1) WE ALREADY HAVE IT — NO. /root/frackin/OpenStarbound/source/game/StarWorldClient.cpp:1968 still reads `m_gatherScratch.assign((size_t)width * (size_t)height, GatherCell{});` immediately before the overlap copy loop at :1974-1980 and the swap at :1981. Nothing in the tree does partial zeroing of this buffer: grep for `vacated` / `zero only` over source/ + docs/ returns nothing relevant, and the only two hits on `shiftAndGatherMargin` outside the source file are the #122 task title and its board entry (docs/board.md:136, :891) — neither mentions the fill. The A2 commit 7b47322c documents the double-buffer choice but not the zeroing scop…

**E04** — A full-recalc oracle for the WorldClient gather cache: drive lightingStableGather and the shiftAndGatherMargin scroll path through a sequence of camera moves and assert the resulting stable grid is identical to a fresh full gather at each step -- the shape of PR 570's scrolledStripsMatchFullRecalc,…

- SURVIVES all three refutation axes. (1) WE DO NOT HAVE IT. Grep across the whole fork finds lightingGatherCache / lightingStableGather / shiftAndGatherMargin / gatherStableColumns / m_gatherGrid in exactly six places: source/game/StarWorldClient.cpp (impl at 1897-2030, dispatch at 2180-2225), source/game/StarWorldClient.hpp (272-278, 403-420), source/frontend/StarClientCommandProcessor.cpp (the /lighting gathercache toggle), source/game/StarRootLoader.cpp + dist/storage/starbound.config (default true), docs/board.md (#121/#122 task text). ZERO test files, ZERO gate scripts, ZERO design docs or audit findings. Our five lighting tests (source/test/{cellular_lighting,lighting_point,lighting_sp…

**E05** — Close the sector-unload / lighting-thread race: hold m_lightMapPrepMutex across the unloadSector loop in WorldClient::update (or make async mode invalidate the gather cache and refuse to read m_tileArray during unload).

- SURVIVES — none of the three kill paths lands. WHAT WE LACK, PRECISELY - /root/frackin/OpenStarbound/source/game/StarWorldClient.cpp:1417-1421 — the unload loop (`for (auto sector : loadedSectors) { if (!neededSectors.contains(sector)) m_tileArray->unloadSector(sector); }`) runs with NO lighting lock held. - /root/frackin/OpenStarbound/source/game/StarWorldClient.cpp:2077 / :2229 — lightingCalc takes `MutexLocker prepLocker(m_lightMapPrepMutex)` at :2077 and only calls `prepLocker.unlock()` at :2229. The whole tile gather (:2180-2228, dispatching to lightingStableGather :1852 / lightingTileGather :1917 / shiftAndGatherMargin :2018, all of which call `m_tileArray->tileEvalColumnsParallel`) i…

**E06** — Warn (or hard-fail in the harness) at loadConfig when a framebuffer that declares "multisampled": true is also named by any effect's frameBufferTextures.

- NOT KILLED by any of the three. (1) WE DO NOT HAVE IT. Searched exhaustively: loadConfig (/root/frackin/OpenStarbound/source/application/StarRenderer_opengl.cpp:150-223) resolves the per-FBO multisample key at :201 and never consults effects; the frameBufferTextures parse (/root/frackin/OpenStarbound/source/application/StarGlRenderSurface.cpp:504-511) never consults m_targets; /root/frackin/OpenStarbound/source/test/render_surface_test.cpp has zero multisample coverage; /root/frackin/OpenStarbound/scripts/config-lint.py lints only starbound.config getOrDefault keys, not opengl.config; and our own upstream draft /root/frackin/OpenStarbound/docs/upstream/prs/msaa-opt-in.md proposes the opt-in…

**E07** — Register the vcpkg overlay ports declaratively in-repo (either "overlay-ports" in source/vcpkg-configuration.json or VCPKG_OVERLAY_PORTS in the `base` preset of source/CMakePresets.json) so a pristine clone finds the jemalloc/libsystemd overlay without a hand-passed flag; then remove the out-of-tre…

- All three refutation routes were tried and all three failed. ROUTE 1 (we already have it) — REFUTED, with an unusually strong negative. `grep -rni "overlay"` across the whole fork excluding build/ and .git/ returns exactly ONE build-related hit: `/root/frackin/OpenStarbound/source/CMakePresets.json:18` -> `"VCPKG_OVERLAY_TRIPLETS": "${sourceParentDir}/triplets"`. There is no `VCPKG_OVERLAY_PORTS` anywhere, and `/root/frackin/OpenStarbound/source/vcpkg-configuration.json` has only `$schema` + `default-registry` — no `overlay-ports` key. No CMakeUserPresets.json exists at repo root or under source/. No `VCPKG_*` var is set in the environment and there is no VCPKG reference in /root/.bashrc, /…

**E08** — Record an explicit retirement condition for each vcpkg overlay port (in the portfile or a vcpkg-overlay-ports/README) stating what upstream event lets it be deleted: jemalloc "drop when the registry ships >5.3.1 or a GCC-16 fix"; libsystemd "registry is already at 260.2, this is now only -Dwerror=f…

- None of the three kill conditions holds, and the recommendation's factual premises verify byte-for-byte against our tree. (1) WE ALREADY HAVE IT — refuted. /root/frackin/OpenStarbound/vcpkg-overlay-ports/ contains only jemalloc/ and libsystemd/; there is no README and no retirement note. A grep for "drop when/remove when/delete when/retire/once upstream/when upstream/until upstream" over *.md/*.cmake/*.json/*.sh/*.py returns only TSSA KIND-vocabulary hits, nothing about the overlay. What we DO have is the WHY, in four places, none of which states a deletion trigger: the 4-line comment at vcpkg-overlay-ports/jemalloc/portfile.cmake ("Forward-fix for GCC 16 / libstdc++ 16 ... Without this, je…

**E09** — Make the allocator dependency a vcpkg manifest FEATURE driven by the CMake option (gate jemalloc and mimalloc behind features, set VCPKG_MANIFEST_FEATURES from STAR_USE_JEMALLOC / STAR_USE_MIMALLOC before project(), drop the unconditional "mimalloc" at source/vcpkg.json:14) rather than bare-deletin…

- SURVIVES, but only in its mimalloc half; the headline BUILD-1 justification is provably false against our tree. WHAT WE GENUINELY LACK (checked, not assumed): - `grep -rn MANIFEST_FEATURE` over the whole fork returns exactly one hit: /root/frackin/OpenStarbound/docs/board.md:3159, the board text PROPOSING it. Zero hits in source/CMakeLists.txt, source/CMakePresets.json, source/vcpkg.json, .github/workflows/. There is no "features" object in source/vcpkg.json at all. So we do not have this, and #196 is still status: pending. - /root/frackin/OpenStarbound/source/vcpkg.json:14 is a bare unconditional `"mimalloc"`. STAR_USE_MIMALLOC is OFF at source/CMakeLists.txt:143 and is set by ZERO presets…

**E10** — Write a per-artefact provenance table for source/extern (version, upstream URL, why-vendored, whether compiled), generated/gated rather than hand-maintained — as another entry in the gates.yml script-gate set, diffing the table against star_extern_SOURCES/star_extern_HEADERS.

- SURVIVES, with a scope correction. All three death modes were tested against our tree. 1. WE ALREADY HAVE IT — NO. Three partial records exist and none is provenance: - /root/frackin/OpenStarbound/doc/OPENSOURCE.md is the inherited vanilla Starbound licence dump: it names Lua (lua.org), tinyformat, and "jemalloc (cannonware.com)", and names NOTHING of rpmalloc, xxhash, fast_float, curve25519, fmt or the imgui bridge. Stale attribution, zero versions. - /root/frackin/OpenStarbound/scripts/arch-graph.py:174 VENDORED_SUBTREES = ("extern/curve25519","extern/fmt","extern/lua","application/discord","test/gtest"). - /root/frackin/OpenStarbound/scripts/tree-map.py:59 NON_COMPONENT = ("source/extern…

