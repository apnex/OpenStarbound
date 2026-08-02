# Board — the durable task index

**Generated. Do not hand-edit.** Regenerate with `scripts/board-export.py`; the script is the
mechanism, this file is the artefact. It carries no timestamp on purpose, so a regeneration
diffs only when the board actually changed — git already records when.

## Why this file exists

The live board is the Claude Code task store at `~/.claude/tasks/<session-uuid>/<id>.json` —
outside the repo, in no git history, keyed by **session UUID**. But the identifiers it hands
out are permanent: commit messages end with `[#131]`, and the architecture docs cite
`task #168` as the record of why something was built. Without this file every one of those is
a pointer into a directory that is not versioned, not pushed, and not backed up — dangling but
still reading as though it resolves. Ids **#1–#63 are already absent from disk**.

## How to read it

- **Ids are per-store, not global.** Each session store has its own id-space. Two stores can
  both hold a `#4`. The `Store` column disambiguates; `[#NNN]` commit stamps refer to the
  primary store.
- **A bare `#NNN` in prose is not necessarily one of these.** The same namespace carries
  upstream PR and issue numbers (`#542`, `#561`, `#204`). `#104` is *both* a task here and an
  upstream issue cited elsewhere. Only the explicit `task #NNN` form is auto-resolved into the
  Cited column; treat everything else as ambiguous.
- **Commits are matched on the `[#NNN]` stamp across all branches** (`git log --all`), because
  the 2026-07-19 reorg rewrote history and work can be reachable only from another branch.
- **Status is what the store says**, which is not always what the tree says. A content audit on
  2026-07-25 found three statuses wrong in both directions. Verify against tree content before
  trusting a status to mean work did or did not ship.

**145 tasks** across 2 store(s): 3 in_progress, 27 pending, 115 completed

- `c29c1332-648a-42c6-87f0-1a6f14884fb0` — 144 tasks, ids 64–208
- `6c8fc9cc-f25d-49cb-9d3e-7a1bcae0c776` — 1 tasks, ids 4–4

---

## Integrity

A self-check, so the drift this file exists to prevent is *visible* rather than something
someone has to go and discover. It is the same discipline as the render oracles: a check that
reports but does not surface is not a check.

**Commit ids cited in task text:** 138, of which **39 resolve to nothing** in either repository.

That is expected and mostly harmless: TWO history rewrites destroyed these ids while preserving every byte of content — the 2026-07-19 whole-fork reorg, and an earlier one around 2026-07-18 that rebuilt the 2026-07-14 stretch of `dev/upstream-merge`. What matters is not that an id is dead but whether anyone can still say what it *was*. `docs/board-anchors.json` answers that, id by id:

- **22** — re-anchored to a live commit
- **9** — a deployed-binary MD5, never a commit
- **3** — an A/B render frame hash, never a commit
- **3** — dead, with no live equivalent that could be defended
- **2** — NOT YET INVESTIGATED

**Unexplained ids: 2.**  ← investigate these; they are citations nobody can resolve.

| Task | Unexplained ids |
|-----:|:----------------|
| [#199](#c29c1332-199) | `555c692b` |
| [#207](#c29c1332-207) | `66b9715a` |

**A caveat the anchors carry, and the reason they are not just a lookup table:** 22 of the re-anchored commits are *not ancestors of* `integration`. They survive only on `dev/upstream-merge` / `reorg/tooling`. On `integration` the whole Layer-1 arc is one squashed commit, `083c6340`. So citing the fine-grained commit alone is misleading in a second way, and each anchor records the HEAD carrier as well.

Mappings resting on message-matching rather than a direct id link were sent to an adversarial auditor instructed to refute them: **7 audited, 3 overturned** to `unresolvable`. A wrong anchor is worse than an absent one — it is authoritative-looking and points at the wrong commit, which is the exact failure this file exists to remove.

**Completed tasks citing no commit and no doc:** 79 of 115.

Not a defect count. Much of this campaign's completed work was *investigation* whose
deliverable was a conclusion — "determinism-locked, DEFER" is a finished task that correctly
touches no code. The number is worth watching only for tasks whose text claims code shipped;
those should carry a `[#NNN]` stamp, and from the stamping convention onward they do.

---

## The table

| Id | Store | Status | Subject | Commits | Cited in |
|---:|:------|:-------|:--------|:--------|:---------|
| [#64](#c29c1332-64) | `c29c1332` | done | forEach v1: template forEachEntity (#1) + const&amp; (#5) + EntityMap oracle | — | — |
| [#65](#c29c1332-65) | `c29c1332` | done | forEach: deterministic micro-benchmark (clean A/B number) | — | — |
| [#66](#c29c1332-66) | `c29c1332` | open | forEach v2 #2: zero-contributor collision/force broad-phase early-out | — | — |
| [#67](#c29c1332-67) | `c29c1332` | done | forEach v2 #4: stamp-based dedup (delete per-query std::sort) | — | — |
| [#68](#c29c1332-68) | `c29c1332` | done | forEach v2 #3: skip box-test for fully-contained interior cells | — | — |
| [#69](#c29c1332-69) | `c29c1332` | open | forEach v2 #7: EntityMapSpatialHashSectorSize granularity sweep | — | — |
| [#70](#c29c1332-70) | `c29c1332` | done | UniqueEffect Lua-context churn optimization (~21% of exploring WST) | — | `board.md` |
| [#71](#c29c1332-71) | `c29c1332` | done | L1: reserve() callback maps before registerCallback storm | — | — |
| [#72](#c29c1332-72) | `c29c1332` | done | L3: move-not-copy in addCallbacks | — | — |
| [#73](#c29c1332-73) | `c29c1332` | done | StatusEffectChurnBench micro-bench (L1/L3 clean A/B) | — | — |
| [#74](#c29c1332-74) | `c29c1332` | done | L2: Proto-cache engine + T1/T4 tests (core_tests) | — | — |
| [#75](#c29c1332-75) | `c29c1332` | open | L2 dense-workload magnitude + verify toggle fix live (opportunistic) | — | — |
| [#76](#c29c1332-76) | `c29c1332` | done | Build-window: L2 default-on + C++ fallback consistency (flags KEPT) | — | — |
| [#77](#c29c1332-77) | `c29c1332` | done | Move 1: Entity de-RTTI via entityCast virtual-accessor downcast | — | — |
| [#78](#c29c1332-78) | `c29c1332` | done | Follow-up: extend entityCast to residual per-candidate cast sites | — | — |
| [#79](#c29c1332-79) | `c29c1332` | done | Collision/movement cluster investigation (~12% WST, Move-0 result) | — | — |
| [#80](#c29c1332-80) | `c29c1332` | done | Build collision levers (gated: user OUT of game) | — | — |
| [#81](#c29c1332-81) | `c29c1332` | done | L2 collision arena (~2.55%) — needs movement verification harness first | — | — |
| [#82](#c29c1332-82) | `c29c1332` | open | L2 collision arena — optional live A/B confirmation + future terrain test-harness | — | — |
| [#83](#c29c1332-83) | `c29c1332` | done | L-WIND-A: gate Plant wind computation to slave/render branch (~1.85% dead store) | — | — |
| [#84](#c29c1332-84) | `c29c1332` | open | L-WIND-A: server profile now EXISTS (#175); measurement attempted and REFUSED by the new fingerprint — needs an in-proc… | `9346f86a` `35808327` | — |
| [#85](#c29c1332-85) | `c29c1332` | done | L4: column-amortized freshenCollision pass-1 dirty scan (~2.34%) — SHIPPED | — | — |
| [#86](#c29c1332-86) | `c29c1332` | done | Liquid WorkingCell churn (~1.7%) — INVESTIGATED: determinism-locked, DEFER cluster | — | — |
| [#87](#c29c1332-87) | `c29c1332` | done | L-LIQ-A: try_emplace in workingCell() — SHIPPED (ba67824, byte-identical) | — | — |
| [#88](#c29c1332-88) | `c29c1332` | done | Animation-on-server (~4.1%) — INVESTIGATED: NOT skippable (load-bearing); shipped L-ANIM-ITER+WAKE byte-identical (ba67… | — | — |
| [#89](#c29c1332-89) | `c29c1332` | done | Object::update cluster (~4.5%) — INVESTIGATED; shipped L-OBJ-1+L-OBJ-2 byte-identical copy-elision (067008f) | — | — |
| [#90](#c29c1332-90) | `c29c1332` | open | SKIP-0 shipped; SIX sub-levers outstanding (SKIP-1/2/3 + L-OBJ-3/4/5) — all located, none started | — | — |
| [#91](#c29c1332-91) | `c29c1332` | done | collisionSeparate sort (~2.3%) — INVESTIGATED: DETERMINISM-LOCKED, DEFER (no byte-identical win) | — | — |
| [#92](#c29c1332-92) | `c29c1332` | done | CDL: explore GPU lighting substrate (brainstorm step 1 — context) | — | — |
| [#93](#c29c1332-93) | `c29c1332` | done | CDL: clarifying questions + 2-3 approaches (brainstorm) | — | — |
| [#94](#c29c1332-94) | `c29c1332` | done | CDL: present design + get approval (brainstorm) | — | — |
| [#95](#c29c1332-95) | `c29c1332` | done | CDL: write + commit design spec; spec self-review; user review | — | — |
| [#96](#c29c1332-96) | `c29c1332` | done | CDL: transition to writing-plans (brainstorm terminal) | — | — |
| [#97](#c29c1332-97) | `c29c1332` | done | CDL-T1: tonemap operator (C++) + unit tests [TDD core] | — | — |
| [#98](#c29c1332-98) | `c29c1332` | done | CDL-T2: lightingPromoteDynamic + lightingTonemap flags + /lighting toggles | — | — |
| [#99](#c29c1332-99) | `c29c1332` | done | CDL-T3: GPU compose tonemap (lightingPassthrough.frag) + CPU mirror | — | — |
| [#100](#c29c1332-100) | `c29c1332` | done | CDL-T4: promote Spread-&gt;PointAsSpread at dispatch | — | — |
| [#101](#c29c1332-101) | `c29c1332` | done | CDL-T5: integration — game_tests + byte-identity + adversarial review + deploy dev | — | — |
| [#102](#c29c1332-102) | `c29c1332` | done | CDL-T6 (in-game): live A/B (promote × tonemap, 4 combos) + decide defaults + cherry-pick to feat/gpu-lighting-standalone | — | — |
| [#103](#c29c1332-103) | `c29c1332` | done | Tier-0 M1 aggregate A/B: build baseline binary at fc3d55e (merge-base w/ main, | — | — |
| [#104](#c29c1332-104) | `c29c1332` | open | FU-Lua native-offload decision = DEFER (not no-go), adversarially verified | — | — |
| [#105](#c29c1332-105) | `c29c1332` | done | GPU timer telemetry + dirty-gated lighting SPIKE — authored on | — | — |
| [#106](#c29c1332-106) | `c29c1332` | done | Point-pass GPU optimization — Part 1 BYTE-EQUIVALENT (authored on | — | — |
| [#107](#c29c1332-107) | `c29c1332` | done | Dirty-REGION lighting — Stage 2 implemented+fixed, building; next: oracle-gated verify | — | — |
| [#108](#c29c1332-108) | `c29c1332` | done | Propagate flicker fix (lightingPromoteMinIntensity, 4c45f1a) to gpu-lighting-standalone + dev | — | — |
| [#109](#c29c1332-109) | `c29c1332` | done | Temporal lighting decoupling — brainstorm → design → plan → implement (flicker-robust GPU-load cut) | — | `board.md` |
| [#110](#c29c1332-110) | `c29c1332` | done | GPU-render-ladder: mission design + spec (brainstorm) | — | — |
| [#111](#c29c1332-111) | `c29c1332` | done | GPU-ladder Rung 0: per-pass GPU-timer telemetry + RAPL harness | — | — |
| [#112](#c29c1332-112) | `c29c1332` | done | GPU-ladder Rung 1 (R-A): bicubic→bilinear lightmap sample | — | — |
| [#113](#c29c1332-113) | `c29c1332` | done | GPU-ladder Rung 2 (R-D+R-E): overdraw trim | — | — |
| [#114](#c29c1332-114) | `c29c1332` | done | GPU-ladder Rung 3 (R-C): opaque tile z-prepass [CONDITIONAL] | — | — |
| [#115](#c29c1332-115) | `c29c1332` | done | GPU-ladder backlog: R-F/R-G/R-I (evidence-gated) | — | — |
| [#116](#c29c1332-116) | `c29c1332` | done | GPU-ladder Rung 1b (R-A Form 2): bicubic-upscale lightmap | — | — |
| [#117](#c29c1332-117) | `c29c1332` | done | CPU arc — lighting tile-gather lever: investigate + design options | — | — |
| [#118](#c29c1332-118) | `c29c1332` | done | CPU arc — lighting-gather lever: BUILD B1/B2 + A1/A2 (when user out of game) | — | — |
| [#119](#c29c1332-119) | `c29c1332` | done | B1: per-column setCellColumn (hoist Either/monochrome branch) | — | — |
| [#120](#c29c1332-120) | `c29c1332` | done | B2: memoize vertical material runs in gather | — | — |
| [#121](#c29c1332-121) | `c29c1332` | done | A1: persistent stable grid + env-light overlay + cache-hit | — | — |
| [#122](#c29c1332-122) | `c29c1332` | done | A2: scroll shift + margin gather (exploring win) | — | — |
| [#123](#c29c1332-123) | `c29c1332` | done | Combat server-thread call-graph capture (ARMED — fires on user fight signal) | — | — |
| [#124](#c29c1332-124) | `c29c1332` | open | Brainstorm LuaEngine marshalling-efficiency lever (combat/exploring Lua tax) | — | — |
| [#125](#c29c1332-125) | `c29c1332` | done | RB-STREAM DONE AND SHIPPED: VBO orphaning cut the GPU frame 74-81% at the Director's bases | — | — |
| [#126](#c29c1332-126) | `c29c1332` | done | Fix pre-existing game_tests breakage (SpawnTest hang + ItemComparison) | — | — |
| [#127](#c29c1332-127) | `c29c1332` | done | Texture-upload churn (#127) — stable-grid lever SHIPPED (91e3fca, cluster 7.09%→2.37%) | — | — |
| [#128](#c29c1332-128) | `c29c1332` | done | Lighting GPU-pass dispatch CPU lever (~13% render-thread frame CPU) | — | — |
| [#129](#c29c1332-129) | `c29c1332` | done | L2 VAO-format bake (#129) — MEASURED NULL, cleanly REMOVED (revert 73c3ec4) | — | — |
| [#130](#c29c1332-130) | `c29c1332` | **active** | Base In A Box — Reforged: sovereign mod fork (scan/print/dup) | — | — |
| [#131](#c29c1332-131) | `c29c1332` | done | GL_INVALID_VALUE ROOT-CAUSED AND FIXED: inactive vertex attribute location -1 fed to a GLuint index | `e02d4484` `ba0d22ef` `84203421` `32b8f849` `7b15c880` `1df96d68` | — |
| [#132](#c29c1332-132) | `c29c1332` | done | Idle-GPU floor investigation: profile static-scene per-pass GPU cost (base/ship) → floor-reduction levers&lt;/subject&g… | — | `board.md` |
| [#133](#c29c1332-133) | `c29c1332` | done | FBO-2 RE-AUDIT DONE: items 2+3 closed (3 fixed in a4106470); item 1 survives as a named hazard -&gt; #197 | `a4106470` | `board.md` `2026-07-14-render-surface-subsystem-design.md` |
| [#134](#c29c1332-134) | `c29c1332` | done | Design the "perfect" env-cache / retained-surface implementation (brainstorm → spec → plan)&lt;/subject&gt; &lt;paramet… | — | — |
| [#135](#c29c1332-135) | `c29c1332` | open | SP-2c: UN-HOLD — P-3 has not landed, so nothing is obsoleted; still NOT_STARTED | — | — |
| [#136](#c29c1332-136) | `c29c1332` | open | P-1: RE-SCOPED to perceptual items only — (b) and (f) are substrate-decided (audit delta[12]) | `4e95c50c` | — |
| [#137](#c29c1332-137) | `c29c1332` | done | P-2 DONE: Air-Gap seam closed for BackdropPass, half-closed for WorldPass; residual metered (see #191) | `3a30d7d8` `a0f0089b` `b2cabd8d` `e6edbe11` `d4b47d7e` `37306207` `c2208b71` `da0125b2` `1e46f71c` `af9d54a9` `aba06048` | — |
| [#138](#c29c1332-138) | `c29c1332` | open | P-3 NOT STARTED — and two feasibility spikes must run BEFORE any parallax shader work is authorised | — | — |
| [#139](#c29c1332-139) | `c29c1332` | done | P-4 PHASE 1 DONE: GL-state assertion pass shipped + gate-read (f02a69f5); phases 2-4 (depth) split to #198 | `f02a69f5` | — |
| [#140](#c29c1332-140) | `c29c1332` | done | P-0 DONE: headless render harness — built, and exercised hard all through #166/#168 | — | — |
| [#141](#c29c1332-141) | `c29c1332` | done | P-5: THE TRUNK — half the GPU frame is unattributed; instrument it before choosing any more levers | — | `2026-07-25-unified-telemetry-model-design.md` |
| [#142](#c29c1332-142) | `c29c1332` | done | FBO-1: FBO subsystem hardening — honour explicit size, gate oracle surfaces, diagnosable failures | — | — |
| [#143](#c29c1332-143) | `c29c1332` | done | CM-1: merge the env + parallax composes into one full-screen pass (MEASURED: ~2ms/frame) | — | — |
| [#144](#c29c1332-144) | `c29c1332` | open | UM-1: upstream merge landed — remaining audit findings (27 confirmed) | — | — |
| [#145](#c29c1332-145) | `c29c1332` | done | RS-0 DONE: the design gate passed 2026-07-14; spec approved, committed, and now implemented by L1/L2 | — | `layer1-vs-vanilla-assessment.md` |
| [#146](#c29c1332-146) | `c29c1332` | done | GT-1: ItemTest.ItemComparison — exactMatch compares post-buildscript vs pre-buildscript parameters | `abf6e2eb` | — |
| [#147](#c29c1332-147) | `c29c1332` | done | J-2: obstacle flag in the lightmap's unused alpha (17 -&gt; 9 taps) — BLOCKED on a lighting bit-identity oracle | — | — |
| [#148](#c29c1332-148) | `c29c1332` | done | RI-1: Renderer contract — delete the dead, exile the instrumentation | — | — |
| [#149](#c29c1332-149) | `c29c1332` | done | ENV-1: RETRACTED — env oracle was run outside its contract (N=4); at N=1 it is 380 MATCH / 0 DIFF | — | — |
| [#150](#c29c1332-150) | `c29c1332` | done | RT-0: PROVEN on hardware — D7 confirmed (60,267 × GL_INVALID_OPERATION; world goes black). Explains #204/#285/#510; NOT… | — | — |
| [#151](#c29c1332-151) | `c29c1332` | done | RT-1: env-cache AA gate DELETED (byte-identical under AA); parallax AA gate KEPT (loses per-sample shading) | — | — |
| [#152](#c29c1332-152) | `c29c1332` | done | F1+F2a+F3a DONE: GlFrameBuffer, GlPass, GlTargets extracted and certified | — | — |
| [#153](#c29c1332-153) | `c29c1332` | done | GATE-1: SOLVED, but not the way I planned — in-process A/B of two code paths, not a golden hash | — | — |
| [#154](#c29c1332-154) | `c29c1332` | done | F3b DONE: GlEffects extracted; load no longer binds by accident (c1d4d0533, c6ffbdc3d) | — | — |
| [#155](#c29c1332-155) | `c29c1332` | done | RB-1 DONE (4fcf71033): borrowedFrom guard; proven GREEN on hardware (fullbright probe: target stays 448x320, not 1x1) | — | — |
| [#156](#c29c1332-156) | `c29c1332` | done | RB-2/RB-3 DONE (691392929): compile+link before replace; shader fallback fixed | — | — |
| [#157](#c29c1332-157) | `c29c1332` | done | RB-4 DONE (a1ec80598): passes early-out fixed; upstream draft filed to docs/upstream (NOT published) | — | — |
| [#158](#c29c1332-158) | `c29c1332` | done | RB-5a/b/c DONE (91bed0d2a, f46500f90): AA-toggle orphan samplers + last size-rule copy + dup uniform owner | — | — |
| [#159](#c29c1332-159) | `c29c1332` | done | RB-6/RB-7 DONE (b74937721, d05f21682): texture internalFormat record + pass notified of target rebuild | — | — |
| [#160](#c29c1332-160) | `c29c1332` | done | L1 FINISH (§9): steps 1-6 to bring Layer 1 to the Layer-2/3 bar — Director-approved | — | — |
| [#161](#c29c1332-161) | `c29c1332` | open | Jacobi improvements NOT STARTED — and #168 showed the lighting CPU budget contains no Jacobi at all | — | `2026-07-19-compose-merge-and-rs0-finish-design.md` |
| [#162](#c29c1332-162) | `c29c1332` | done | CPU-1: explore context — what the frame loop times today, thread ownership, frame-skip | — | — |
| [#163](#c29c1332-163) | `c29c1332` | done | CPU-2: clarifying questions + 2-3 approaches (brainstorm) | — | — |
| [#164](#c29c1332-164) | `c29c1332` | done | CPU-3: present design + get Director approval | — | — |
| [#165](#c29c1332-165) | `c29c1332` | done | CPU-4: transition to writing-plans (brainstorm terminal) | — | — |
| [#166](#c29c1332-166) | `c29c1332` | done | CPU-5 DONE: unified telemetry model shipped; one verification step deliberately not run (see #172) | `84ca718f` `1bfa935d` `5e8f66d3` `b3c88551` `497e398c` `313095f1` `5eb17518` `61531413` `39992d4b` `4797f33d` `e2966ad2` | — |
| [#167](#c29c1332-167) | `c29c1332` | done | CPU-6: bind the GPU descriptor to the recording call — delete the reachability bug class | `a94a7a8c` | `2026-07-25-unified-telemetry-model.md` |
| [#168](#c29c1332-168) | `c29c1332` | done | CPU-7 DONE: lighting CPU budget closed (99.6% GPU-on / 100.0% GPU-off), then cut 16.9% by four levers | `2e9c514e` `f7521455` `4eb4e1c3` `bf9d0fb4` `0c8d5e7c` `419b0f63` `73beb625` `165f07b3` `83c16487` `25a7605f` `065d462b` `53ad8da0` `34ffb7bd` `0571b2c9` `9422b768` `582991af` | `board.md` `2026-07-25-lighting-cpu-budget-closure.md` |
| [#169](#c29c1332-169) | `c29c1332` | open | L3b: F16C vcvtps2ph for the fp16 emission convert — needs Director sign-off (output changes) | — | — |
| [#170](#c29c1332-170) | `c29c1332` | done | L4 DONE: adaptive calculation border shipped — 4.375x -&gt; 3.000x, -23.6% lighting CPU (03cec1c0) | `03cec1c0` `a9854185` | `board.md` |
| [#171](#c29c1332-171) | `c29c1332` | open | Producer-side lighting CPU is billed to owner `frame` and cannot be attributed without a telemetry model change | — | — |
| [#172](#c29c1332-172) | `c29c1332` | done | Telemetry deep-off cost: MEASURED — arming costs +2.16%, within noise; the deep gate works | — | — |
| [#173](#c29c1332-173) | `c29c1332` | open | RB-FLUSH: setScissorRect flushes per widget (~92/frame) — orphaning killed the stall COST, not the flush COUNT | — | — |
| [#174](#c29c1332-174) | `c29c1332` | done | P-0b DONE: motion gate shipped (7ce03361) -- bypass PROVEN engaged; G9 SATISFIED, the split is authorised | `7ce03361` | — |
| [#175](#c29c1332-175) | `c29c1332` | done | SIM-1 DONE: server-tick budget closed 99.76% across 27 phases; compute.entities is the real 65% | `90d8d236` `4eb7b96c` | — |
| [#177](#c29c1332-177) | `c29c1332` | done | ENV-CHOP DONE: env cache had no motion term; ship flight/warp CONFIRMED SMOOTH in game | `1014c3b2` `82711412` `0a027243` `00f575ad` | — |
| [#178](#c29c1332-178) | `c29c1332` | done | GATE-STALE DONE: render-gate.sh now asserts the build happened, not just the run (2643b1ce) | `2643b1ce` | — |
| [#179](#c29c1332-179) | `c29c1332` | done | DOC-DRIFT DONE: Air-Gap counts generated into the doc and gated by render_docs_fresh (c89be289) | `2ef87860` `e8d6860a` `7c0e8340` `c89be289` | — |
| [#180](#c29c1332-180) | `c29c1332` | done | AX-A7 DONE: the clause-2 recovery now recovers, and can be executed (6b6e8b72) | `6b6e8b72` | — |
| [#181](#c29c1332-181) | `c29c1332` | done | AX-A1-COUNTERS DONE: gate reads the contract violation; counters registered at construction (472fd263) | `472fd263` | — |
| [#182](#c29c1332-182) | `c29c1332` | done | AX-A14 DONE: ContentKey's quantiser was UNDEFINED, not merely untested — fixed + 6 tests (13ed9399) | `13ed9399` | — |
| [#183](#c29c1332-183) | `c29c1332` | done | AX-A3-RATCHET DONE: all 17 singleton reads metered, relocation is no longer a route to green (8eddcb37) | `8eddcb37` | — |
| [#184](#c29c1332-184) | `c29c1332` | done | AX-A3-WORLDPASS DONE (declaration): four consumed inputs documented at the signature (252bed68) | `252bed68` | — |
| [#185](#c29c1332-185) | `c29c1332` | done | AX-A2-CONFIG DONE: getOrDefault + config_declared gate; newLighting declared, antiAliasing moved down (d46fee26) | `d46fee26` | — |
| [#186](#c29c1332-186) | `c29c1332` | done | AX-A3-TIDY DONE: dead includes deleted, compose call site folded, false L2 build claim corrected (252bed68) | `252bed68` | — |
| [#187](#c29c1332-187) | `c29c1332` | done | AX-A6 DONE: label rule + preset filter asserted at configure time, both proven to fire (786d4342) | `786d4342` | — |
| [#188](#c29c1332-188) | `c29c1332` | done | AX-A4-SPEC DONE: constructor injection retracted in place, per G10 (8781759c) | `8781759c` | — |
| [#189](#c29c1332-189) | `c29c1332` | done | AX-A12 DONE: docs/render/README.md index + both chains cross-link (8781759c) | `8781759c` | — |
| [#190](#c29c1332-190) | `c29c1332` | done | AX-A8-PRISTINE DONE: G1 closed via six-platform CI from actions/checkout; local clone blocked by #196 (704199ef) | `704199ef` | — |
| [#191](#c29c1332-191) | `c29c1332` | open | DTO-2 RE-SCOPED: blocker 2 DONE (b2ac6c27); blocker 1 is bigger than filed -- it reaches TileDrawer in the game layer | `334bc38d` `b2ac6c27` | `2026-08-01-target-state-system-architecture.md` |
| [#192](#c29c1332-192) | `c29c1332` | done | CI-1 DONE: lint ported to Python, all three gates registered via ${Python3_EXECUTABLE} (786d4342) | `45da57fc` `786d4342` | — |
| [#193](#c29c1332-193) | `c29c1332` | done | CI-2 DONE: STAR_EXT_GUI_LIBS_CORE split + CMake assertion; test no longer links Steam (786d4342) | `786d4342` | — |
| [#194](#c29c1332-194) | `c29c1332` | done | CI-3 DONE: absolute 15us bound -&gt; 4x ratio; both ends measured, injection proves it fires (f87a6848) | `45da57fc` `f87a6848` | — |
| [#195](#c29c1332-195) | `c29c1332` | done | CI-4 DONE: Gates workflow runs the 4 script gates on every push; ceilings read via --from-cmake (1328b3f5) | `1328b3f5` | — |
| [#196](#c29c1332-196) | `c29c1332` | open | BUILD-1: a pristine Linux build cannot bootstrap on this workstation -- jemalloc 5.3.1 vs libstdc++ 16 | — | — |
| [#197](#c29c1332-197) | `c29c1332` | open | FBO-3: effect-parameter state persists per effect and nothing asserts it -- the `world` effect is the exposure | — | — |
| [#198](#c29c1332-198) | `c29c1332` | open | P-4 phases 2-4: depth-buffer architecture -- ZERO TRACE, survey before designing | — | — |
| [#199](#c29c1332-199) | `c29c1332` | open | HEADLESS-1 DEFERRED: sink-gating landed (f7609bb7); a real headless client is its own engineering effort | `f7609bb7` | `2026-08-02-tssa-levelling-analysis.md` |
| [#200](#c29c1332-200) | `c29c1332` | open | ARCH-1: whole-system boundary document + arch-graph generator + gate | `7d22d2bb` | — |
| [#201](#c29c1332-201) | `c29c1332` | open | CI-5: boundary_fresh was RED on Windows from the day it landed -- native path separator | `e7368000` | — |
| [#202](#c29c1332-202) | `c29c1332` | open | RI-2: two L1 files were unclaimed by the layer table -- the #137 failure, second instance | `52360eee` | — |
| [#203](#c29c1332-203) | `c29c1332` | open | ARCH-2: shape + cohesion tests, actions reordered by value, and a recursive-scan defect | `07d248f4` `cfa7603b` `38a147d9` `80084408` `c3ef055d` `7fdae50d` `98b6692a` `5e893499` | — |
| [#204](#c29c1332-204) | `c29c1332` | **active** | TSSA (#204): target-state architecture spec — structure DONE, content is the remaining work | `42b60f5d` `298bdd16` `1b5f6f00` `c359fc30` `63248364` `1a1c89d8` `6a53b039` `5388cbd0` `b2db3904` `7dc30a06` `b0591b19` `299e8b47` `0acb3c7b` `a71484af` `706e970f` `5d6f5b97` `94a3eec4` `e0a6cdec` `ca7e0011` `a51b3659` `364519c3` `2ed13794` `a3eaa447` `6c30f710` `47e33692` `6539382d` `3751ccaa` `f88ff14d` `351e7696` `ad6426e7` `0fe309ee` `c1e89fdc` `d15a3816` `56b9a1ca` `6956aab7` `01694c2c` `bb04e8b5` `91d18ec6` `fbe4c41b` `1cf8987d` `50f09efe` `94c1af57` `24c79ca8` `dc4dc3e7` `4a333db7` `3ac919ed` `4b4982bd` `736030b7` `d8b8d550` `cb93b33f` `a588e8ab` `8f9e1ce3` `d7708b7a` `e0b827df` `beb51779` `8523181d` `a6c7dba1` `a513ef6f` `618f99e3` `3f07aeec` `b03248c5` `7155637b` `a1911e89` `88bc52f2` `7482dc5f` `41047638` `9882a77a` `1da322da` `77669e22` `1a0f118f` `d14f7f88` `c9c2770f` `739b4aba` `14b941a6` `2e01529d` `b3647d73` `6d181196` `151b8d56` `ebfe4547` `5538062c` `22c5e6d5` `02f2d8d6` `ac6cafd1` `e7f5ab49` `8ffffc5e` `cc9a5532` `2f42bca7` `f053671f` `55da339e` `a6342adb` `376eb9c9` `d1671a77` `e1107569` `9823f106` `c71a06f3` `cd43f4ba` `e635efbf` `f0cb85d9` `4534874d` `ab53f943` `998d52fe` `8fc507c0` | — |
| [#205](#c29c1332-205) | `c29c1332` | done | TSSA-0 DONE: Part I written, 312 -&gt; 735 lines; six axioms verified and five became decisions | `9c8eb88f` `f7e61c8e` `947a34b1` | — |
| [#206](#c29c1332-206) | `c29c1332` | done | TSSA-1 DONE: 18-section TOC derived, ratified, implemented, cross-refs renumbered | — | `2026-08-02-tssa-levelling-analysis.md` `tssa-frame.md` |
| [#207](#c29c1332-207) | `c29c1332` | **active** | TSSA-2: review closed; levelling pass — §3 DONE, §§1/2/6 assessed level | `337f5494` `d2eadae4` `9442b154` `248b9a74` `ed5e7125` `cdea71c1` `de57756e` `82955390` `ef074409` `41b7adfd` `3ee6021d` `170a17cb` `a0e43c2c` `eab0b4da` `5f2933ad` `a96a3575` `6d5100fb` `78a8a4b5` `c16a0f07` `51415bfe` `f5faf4cf` `5db63949` `b1b34d9a` `550a3b5e` `b976ae79` `8bc6590e` `12f08312` `0c96063d` `b86a1ad0` `4011f1af` `8e742ae7` `176d60be` `45af8da3` `a58ef402` `3f767902` `f6744d04` `6184248e` `7952fb50` `669dbc29` `fdad9c5d` `b8f61e9f` `b69cd406` `62fe4dbd` | — |
| [#208](#c29c1332-208) | `c29c1332` | open | TSSA-3: register fields, aggregate review, re-home the anchoring gates | — | — |
| [#4](#6c8fc9cc-4) | `6c8fc9cc` | open | L1-FIX: the four false comments, the makeDoubled face leak, the GlPass field bag, and the per-draw glTexParameteri hoist | — | — |

---

## The record

The stored description of every task, verbatim. This is the part that makes a `[#NNN]` in the
git history resolvable from the repo alone. Descriptions are fenced rather than inlined because
some records contain literal markup from malformed task creation, and losing bytes to a
renderer would defeat the purpose.

### Store `c29c1332-648a-42c6-87f0-1a6f14884fb0`

<a id="c29c1332-64"></a>

#### #64 — forEach v1: template forEachEntity (#1) + const&amp; (#5) + EntityMap oracle

status: **completed**

```
Lever #1: make EntityMap::forEachEntity a header template (de-std::function the per-entity callback so direct-lambda callers inline). Lever #5: World query callbacks by const&. Add EntityMap-level brute-force oracle (game_tests). Behavior-equivalent; SpatialHash2D.* + new EntityMap.* oracles green. Branch perf/foreach-template-callback.
```

<a id="c29c1332-65"></a>

#### #65 — forEach: deterministic micro-benchmark (clean A/B number)

status: **completed**

```
Standalone forEach micro-bench (fixed PRNG, ~965 entities, fixed query mix) → ns/query, E-core-pinned. Isolates per-query levers from activity noise. Measure v1 + each per-query lever.
```

<a id="c29c1332-66"></a>

#### #66 — forEach v2 #2: zero-contributor collision/force broad-phase early-out

status: **pending**

```
DEPRIORITIZED by live measurement. forEach #2 (zero-contributor collision/force broad-phase early-out) targets forEachMovingCollision + query<PhysicsEntity> + updateForceRegions, which measured only ~0.2% of the WST in the 2026-06-20 live exploring profile (chunk-streaming workload). #2's win is workload-conditional (collision-dense settled activity), NOT the dominant exploring cost. Keep as a future lever for collision-heavy workloads; measure with a runtime toggle before building. The forEach per-query stack (#1/#5/#4/#3) already integrated at 373f9b3 collapsed the named symbol 22.5%→0.5%. Superseded in priority by task #70 (UniqueEffect context churn ~21%).
```

<a id="c29c1332-67"></a>

#### #67 — forEach v2 #4: stamp-based dedup (delete per-query std::sort)

status: **completed**

```
mutable queryStamp on Entry + m_queryCounter; delete the unconditional sort+skip-equal dedup. Re-entrancy (local counter) + thread-safety (verify WorldClient map). + re-entrancy/multi-sector oracle tests.
```

<a id="c29c1332-68"></a>

#### #68 — forEach v2 #3: skip box-test for fully-contained interior cells

status: **completed**

```
Precompute interior cell sub-rect; append all entries without r.intersects for fully-contained cells (only perimeter ring tested). Match includeEdges semantics. + boundary oracle tests.
```

<a id="c29c1332-69"></a>

#### #69 — forEach v2 #7: EntityMapSpatialHashSectorSize granularity sweep

status: **pending**

```
Sweep sector size 8/16/24/32 (measured, after #3/#4). Oracle is size-independent; the one lever whose sign needs live/bench data. Last.
```

<a id="c29c1332-70"></a>

#### #70 — UniqueEffect Lua-context churn optimization (~21% of exploring WST)

status: **completed**

- cited in `docs/board.md`

```
UniqueEffect Lua-context churn lever cluster (~21% exploring WST). L1 (reserve) + L3 (move) + L2 (Proto cache) all DONE + INTEGRATED + DEFAULT-ON (dev/perf-integration HEAD 9f0f070). Adversarial review SHIP; live engagement confirmed. Flags kept as kill-switches. Bench: L1 -41%, L3 -64%, L2 -31%/ctx. Remaining (separate tasks/future): L4/L6/L8 next levers; #75 opportunistic dense A/B; same-session toggle bug (launch-time-reliable only). Closing the core L1/L2/L3 cluster.
```

<a id="c29c1332-71"></a>

#### #71 — L1: reserve() callback maps before registerCallback storm

status: **completed**

```
L1: add LuaCallbacks::reserve(size_t) (core: StarLua.hpp/.cpp forwarding to m_callbacks.reserve) and call reserve() at the top of the 4 hot-path builders before the registerCallback storm — makeStatusControllerCallbacks (39), makeEntityCallbacks (9), makeUniqueEffectCallbacks (12), actor-movement builder (42). Byte-identical maps (reserve sizes to ≤0.7 fill); eliminates the 8→16→32→64 doubling rehashes. Behavior-equivalent, default-ON (no toggle, like forEach #1/#4/#3).
```

<a id="c29c1332-72"></a>

#### #72 — L3: move-not-copy in addCallbacks

status: **completed**

```
L3: rewrite LuaBaseComponent::addCallbacks (game/scripting/StarLuaComponents.cpp:36) to move-not-copy. New form: auto result = m_callbacks.insert(groupName, std::move(callbacks)); if (!result.second) throw dup; if (m_context) m_context->setCallbacks(groupName, result.first->second). Preserves insert-then-throw-on-duplicate semantics exactly and passes the stored element (const&) to setCallbacks, eliminating the per-group LuaCallbacks copy. Behavior-equivalent, default-ON.
```

<a id="c29c1332-73"></a>

#### #73 — StatusEffectChurnBench micro-bench (L1/L3 clean A/B)

status: **completed**

```
New test/status_context_test.cpp (registered in game_tests in test/CMakeLists.txt). StatusEffectChurnBench mirroring SpatialHashBench: own gtest suite (--gtest_filter), fixed iteration count, std::chrono::steady_clock ns timing, volatile sink. Two in-binary A/B sub-benches: (1) CallbackBuild — build a representative K=39 callback LuaCallbacks via registerCallback storm WITH reserve(K) vs WITHOUT (isolates L1's rehash savings); (2) CallbackCopy — copy vs std::move a built K-callback LuaCallbacks (isolates L3's per-group copy savings). Report ns/op + delta for each. Run E-core-pinned: taskset -c 6-15 ./game_tests --gtest_filter='StatusEffectChurnBench.*'.
```

<a id="c29c1332-74"></a>

#### #74 — L2: Proto-cache engine + T1/T4 tests (core_tests)

status: **completed**

```
L2 Proto cache implemented: engine (StarLua.cpp/.hpp: extern-C lua internals, pushClonedChunk, contextLoadCached, clearProtoCache/cachedProtoCount/protoCacheHits, LuaContext::loadCached, ~LuaEngine flush) + game toggle (StarLuaRoot.cpp/.hpp ScriptCache: setProtoCacheEnabled/protoCacheEnabled/protoCacheDirty, clear/unloadScript dirty, loadContextScript deferred-flush+branch, restart read; StarRootLoader scriptProtoCacheEnabled default-OFF). Tests T1/T1b/T4/T4b (lua_test, core_tests) + ProtoLoad bench (status_context_test, game_tests). Adversarial review = SHIP (no blocking). Applied I-1 hardening (CORRECTED: guard loadScript dirty on scripts.contains() — the review's blanket version would thrash the cache during warm-up) + N-1 doc. Final rebuild + retest in progress, then commit + merge.
```

<a id="c29c1332-75"></a>

#### #75 — L2 dense-workload magnitude + verify toggle fix live (opportunistic)

status: **pending**

```
Two opportunistic items for the next in-game session (NOT gates; L2 is default-ON):
(1) Verify the same-session toggle FIX (commit f136c6b): setConfiguration(scriptProtoCacheEnabled, false/true) + /serverreload -> check the new INFO log 'Lua Proto cache: ENABLED/disabled' confirms the flip (and a profile shows loadCached/no luaU_undump when ON). If it logs 'disabled' after a true set -> config-persistence root cause to chase. Mechanism already proven by ProtoCacheToggle.* regression tests.
(2) Dense-churn OFF<->ON A/B for the real L2 WST magnitude (the light new-world session only had ~0.17% addUniqueEffect churn; projection ~4-5% WST in heavy churn). Use the now-fixed toggle: OFF (set false + /serverreload, log 'disabled') -> capture; ON (set true + /serverreload, log 'ENABLED') -> capture; compare loadContextScript/luaU_undump. E-core-pinned record+symbolize.
```

<a id="c29c1332-76"></a>

#### #76 — Build-window: L2 default-on + C++ fallback consistency (flags KEPT)

status: **completed**

```
User wants flags KEPT in config (not removed) — clean+correct, default-on. dormancy+netDelta canonical config already flipped on (commit b44ac4b, build-free). Remaining C++ default flips (need out-of-game rebuild): (1) StarRootLoader.cpp scriptProtoCacheEnabled false->true (L2 root-config default; flag stays alongside scriptProfilingEnabled); (2) StarWorldServer.cpp:1604 netDeltaDirtyVersionEarlyOut getBool fallback false->true + :1607 entityDormancyEnabled fallback false->true (so the code's stated default matches the config — keep the validate fallbacks false). Do NOT remove any toggle. Then: rebuild core_tests+game_tests, re-run T1-T4 + EntityDormancy(11) + StatTest + benches, commit on dev/perf-integration, redeploy dev starbound binary, update HANDOVER + lever specs (drop all 'default-OFF'/'byte-identical until flipped' language). All three already running ON in the dev session.
```

<a id="c29c1332-77"></a>

#### #77 — Move 1: Entity de-RTTI via entityCast virtual-accessor downcast

status: **completed**

```
Replace per-candidate dynamic_pointer_cast (as<T>) in the World/EntityMap entity-query family with virtual-accessor downcasts (entityCast<T> + EntityDowncast trait), extending the asTileEntity/asWireEntity pattern with 11 new accessors. Behavior-equivalent, ships unconditional. Build game_tests (exit 0), EntityMap.VirtualAccessorEquivalence + 7 existing EntityMap + 11 EntityDormancy tests pass. Adversarial review in progress, then commit.
```

<a id="c29c1332-78"></a>

#### #78 — Follow-up: extend entityCast to residual per-candidate cast sites

status: **completed**

```
Move 1 committed (7c45e89) covers the World/EntityMap query TEMPLATE family. Residual per-candidate dynamic_cast still in non-template callers, behavior-identical, convertible to entityCast for the full hot-path win: StarEntityMap.cpp forEachEntityAtTile inner as<TileEntity> (:191) + :234/:282/:313, StarWorldGeneration.cpp:1194-1198, StarWorldServer.cpp:1229, StarWorldLuaBindings.cpp:100 (gated callScript selector). Consider adding asInteractiveEntity accessor for StarEntityMap.cpp:280 (no accessor today). Also: lineQuery lambda -> EntityPtr const& (StarWorld.hpp:254, pre-existing nit) and a one-line invariant comment near the asX() accessors (public-virtual/single-subobject equivalence invariant). Needs an atTile equivalence test + its own review pass.
```

<a id="c29c1332-79"></a>

#### #79 — Collision/movement cluster investigation (~12% WST, Move-0 result)

status: **completed**

```
Move 0 (d607320, exploring re-profile) confirmed de-RTTI (~0.57% RTTI residual) and surfaced tile-collision/movement as the top measured reducible engine lever in traversal-exploring (~12% WST self): MovementController::queryCollisions $_1/$_2 (~5.2%), WorldServer::freshenCollision (2.0%), ServerTile::getCollision (1.7%), collisionSeparate+sort (~2.2%). Read-only Workflow to map the path + produce a ranked, behavior-equivalence/desync-gated design. Profile: /tmp/dertti-explore.data.
```

<a id="c29c1332-80"></a>

#### #80 — Build collision levers (gated: user OUT of game)

status: **completed**

```
Per the #79 design. Recommended order — Session 1 (byte-identical, no behavior change possible): L1 inline ServerTile::getCollision/isColliding (1.73%, no flag), L5 hoist sortDistance recompute out of collisionSeparate (keep per-call sort), L3 hoist wrap-offset out of consumeBlock (crossesWrap-guarded, translate-by-0 identity). Session 2 (structural, default-ON flag): L2 in-place arena for m_workingCollisions (2.55%), maybe L4 freshenCollision pass-1 scan reduction (2.03%, must mirror to WorldClient). Build verification harness (no movement test exists): in-binary A/B oracle (floor/wall/slope/platform/SEAM) + record-replay golden trace + master/slave parity, all BIT-EXACT. SCOPED OUT: the collisionSeparate std::sort (1.37%, unstable+tie-laden+sequence load-bearing = desync hazard), SAT, marching-squares gen. BUILD ONLY WHEN starbound NOT running (E-core thermal rule).
```

<a id="c29c1332-81"></a>

#### #81 — L2 collision arena (~2.55%) — needs movement verification harness first

status: **completed**

```
From #79 design. In-place arena for m_workingCollisions (replace queryCollisions drain/refill recycle with a liveCount overwrite; thread [0,liveCount) span through collisionMove/collisionSeparate/ground-slope scan/the sort). NOT byte-identical-by-construction (structural) — must build a movement A/B verification harness first: either a World-mock unit A/B over queryCollisions+collisionMove, or a default-ON flag + validate-shadow (compute old drain-path vs new arena, assert byte-identical m_workingCollisions, log mismatch) + live dev playtest (the #4-netDelta pattern). Ship default-ON with a flag (structural). Biggest single collision win (~2.55%). Gated: user OUT of game for builds. Context: L1+L5 shipped 276ba1b; L3 dropped (review proved desync via sticky poly wrap-frame).
```

<a id="c29c1332-82"></a>

#### #82 — L2 collision arena — optional live A/B confirmation + future terrain test-harness

status: **pending**

```
L2 shipped 69bcaaf (default-ON), verified byte-identical by perspective-diverse adversarial review. Runtime verification gap: the offline test WorldServer (size+storage ctor) has no terrain generator and foreground placement needs support, so a bit-exact unit A/B wasn't achievable. Optional belt-and-suspenders: (a) live dev A/B — flip collisionArenaEnabled OFF vs ON in dev play, confirm movement/feel identical (the flag enables this); (b) a future proper-terrain test harness (TestUniverse-based, or a WorldServer built from a real WorldTemplate with a biome) to add a bit-exact movement A/B + a validate-shadow, reusable for any future MovementController lever. Low priority — the review is the gate; this is extra confidence on a desync-critical path.
```

<a id="c29c1332-83"></a>

#### #83 — L-WIND-A: gate Plant wind computation to slave/render branch (~1.85% dead store)

status: **completed**

```
Per windLevel investigation (wjsdj5oez). Plant::update unconditionally computes m_windLevel = world()->windLevel(tilePos) + m_windTime fmod, but m_windLevel/m_windTime are read ONLY by branchRotation() -> Plant::render (client), are NOT in setupNetStates (never networked), and the master (server) never renders -> dead store costing ~1.85% of the exploring WST (windLevel = a TileSectorArray::tile() pmod+sector-deref, NOT Perlin). Fix: move the wind block (StarPlant.cpp ~727-744) into the else/slave branch of update(); preserve the master-side m_tileDamageStatus.recover and the slave-side m_netGroup.tickNetInterpolation. Byte-identical (render-only, un-networked, no physics; grep-confirmed only StarPlant.cpp:730 + the Lua binding call windLevel). Ship default-ON behind plantWindServerSkip (structural-lever policy + kill-switch; worst case if a plant were ever client-mastered = it stops swaying, cosmetic). Verify: static dead-store proof (done) + build + suite + re-profile (windLevel self 1.85%->~0). GATED: build only when user OUT of game. Perlin 2.01% (terrain gen) is SEPARATE + scoped out (genuine noise, mostly irreducible; L-PERLIN-D ~0.3-0.5% determinism-risky, deferred). Also correct the docs' 'wind/Perlin ~3.9%' framing (two independent costs).
```

<a id="c29c1332-84"></a>

#### #84 — L-WIND-A: server profile now EXISTS (#175); measurement attempted and REFUSED by the new fingerprint — needs an in-process A/B

status: **pending**

- `9346f86a` docs(board): regenerate -- #84 revisited [#84]
- `35808327` sim: add sim.entities.live -- the scene fingerprint an A/B needs, and it worked immediately

```
REVISITED 2026-07-25. Half this task's premise is now false, and the other half has a sharper reason.

RESOLVED: "NO server-thread profile exists at all" — false as of #175. Owner `sim` closes at 99.76% across 27 phases, and this task's own suggested reframe (give the server tick the #166/#168 treatment) is what shipped.

STILL NOT MEASURED, and now we know exactly why. Two A/B rounds run at bookmark `explore`, lever flipped via plantWindServerSkip in assets/opensb/worldserver.config.patch (the harness loads assets/opensb as a LOOSE source, so a repo edit reaches it; restored with git checkout after each round):

  Round 1 (no fingerprint): compute.entities ON 401.3 / OFF 425.6 = "+5.7% win".
    INVALID. Every control moved the same way -- netsync +14.5%, damage +9.4%, liquid +7.3% -- and
    animator ops/tick (a load proxy) varied 26% across runs, correlating +0.83 with the phase under
    test. The heaviest-load run happened to be lever-OFF. The win was the scene.

  Round 2 (sim.entities.live armed, commit 35808327): entity count 92,93 (ON) vs 98,94 (OFF) --
    6.5% spread, systematically higher in OFF. netsync control +20.2%, MORE than the lever's
    apparent +11.6%. The fingerprint gate REFUSED TO CERTIFY. Correct behaviour.

THE STRUCTURAL PROBLEM, stated plainly: this lever needs PLANTS, so it can only be measured in a vegetated biome -- which is exactly where entity population is least stable run-to-run (spawning, chunk streaming). A station or outpost has a stable population and no plants. Those two requirements are in direct tension, and no number of repeated runs fixes a systematic arm-vs-arm population difference.

THE WAY OUT is the render side's GATE-1 answer: an IN-PROCESS A/B. Flip PlantWind::serverSkip between two measured windows within a SINGLE run, so both arms see literally the same world, the same entities and the same tick. std::atomic<bool> already, so the flip is free and safe. That is the next step for this task and it generalises to every server lever with a runtime flag (#82's collisionArenaEnabled, #90's damageSourceSkipEnabled, etc.) -- the same shared blocker this task originally identified, one level further along.

SHIPPED HERE: sim.entities.live (35808327), the sim analogue of lighting.lights.sources. It caught a false positive on its first use.

DO NOT record "L-WIND-A measured at N%" anywhere. It is unmeasured. The static dead-store proof in #83 stands on its own; the magnitude does not.
```

<a id="c29c1332-85"></a>

#### #85 — L4: column-amortized freshenCollision pass-1 dirty scan (~2.34%) — SHIPPED

status: **completed**

```
SHIPPED ceef459 (unconditional, byte-identical). Replaced freshenCollision's per-tile modifyTile pass-1 dirty scan with read-only column-amortized tileEachColumns (skips invalid/unloaded like modifyTile's null guard -> same tile set; raw-x coords; order-independent combine; pass-2 unchanged). Mirrored WorldServer + WorldClient. A first attempt used const tileEach -> adversarial review caught a divergence (tileEach substitutes m_default for invalid positions, and WorldTile() inits collisionCacheDirty=TRUE, so m_default is dirty -> ballooned freshenRegion -> clears+regens clean tiles near world edges, a perf regression; desync-safe but mislabeled). Switched to tileEachColumns (avoids m_default); re-review = BYTE_IDENTICAL. Build clean; game_tests 79/80 (pre-existing ItemComparison). Win-measurement opportunistic (re-profile, in-game).
```

<a id="c29c1332-86"></a>

#### #86 — Liquid WorkingCell churn (~1.7%) — INVESTIGATED: determinism-locked, DEFER cluster

status: **completed**

```
Workflow wvo86l1dp (3 agents, read-only) complete. CONCLUSION: the ~1.7% liquid cluster (LiquidWorld::cell 1.04% + m_workingCells emplace 0.63%) is a DETERMINISM-LOCKED structural cost, NOT byte-identically reducible.

- cell() (1.04%): genuine irreducible per-tile read (getServerTile sector lookup + blocksLiquidFlow + Variant), ALREADY deduped to once-per-unique-tile by the res.second guard + adjacentCell neighbour-ptr cache. No "call fewer times" win exists.
- emplace/rehash storm (0.63%): the real capacity-retain target — finish() take(m_workingCells) frees the bucket array every tick (StarAlgorithm.hpp:327), so the std::unordered_map regrows from empty → per-tick rehash chain. BUT the bucket_count trajectory is LOAD-BEARING for deterministic liquid evolution: finish() setFlow iteration order → m_nextActiveCells (FlatHashSet, insertion-ordered) → m_activeCells (OrderedHashSet) → next-tick UNSTABLE y-only std::sort same-y tie order → RandomSource consumption order → liquid levels. Any reserve/double-buffer/clear-retain changes bucket_count → changes order → SILENT DESYNC. Exactly the L3 / L4-tileEach desync class caught twice this session. SpatialHash #12 was safe ONLY because its consumer was made order-insensitive (stamp-dedup); liquid has no such neutralizing layer.

LEVERS: L-LIQ-A (try_emplace in workingCell:532, drop throwaway Maybe<WorkingCell>() temporary) = ONLY safe change, byte-identical by construction, but near-ZERO measurable win (doesn't touch rehash cost). L-LIQ-B (shrink node)/C (de-virtualize cell) = sub-0.1%, effort>>win. L-LIQ-D (capacity-retain) = REJECT desync-unsafe. L-LIQ-E (canonical sort) = out-of-scope (changes output, breaks save replay).

RECOMMENDATION: DEFER the cluster. Fold L-LIQ-A into the NEXT real build window as a free byte-identical cleanup (not a dedicated build). Liquid is effectively irreducible for byte-equivalent thermal work. Full result: /tmp/claude-0/-home-apnex/c29c1332-648a-42c6-87f0-1a6f14884fb0/tasks/wvo86l1dp.output (copy to docs before tmp reap if we act on L-LIQ-A).
```

<a id="c29c1332-87"></a>

#### #87 — L-LIQ-A: try_emplace in workingCell() — SHIPPED (ba67824, byte-identical)

status: **completed**

```
One-line byte-identical cleanup from the #86 liquid investigation. In LiquidCellEngine::workingCell() (source/base/StarCellularLiquid.hpp:532), replace `auto res = m_workingCells.insert(make_pair(p, Maybe<WorkingCell>()));` with `auto res = m_workingCells.try_emplace(p);` (Base::try_emplace reachable via MapMixin public inheritance; precedent StarInput.cpp:291). Skips constructing the throwaway pair on the common cache-hit path (each tile touched ~5x). The `if (res.second)` materialization is unchanged.

Byte-identical by construction (try_emplace and insert share the libstdc++ _Hashtable insertion path → identical bucket trajectory + node linking; only mapped-value construction differs, which is order-irrelevant). Ships UNCONDITIONAL (micro-opt rule, git-revert is kill-switch). Expected thermal delta ~0 (does NOT touch the dominant rehash cost). DO NOT spend a dedicated build slot — bundle with the next real lever build. Gated on user OUT of game.
```

<a id="c29c1332-88"></a>

#### #88 — Animation-on-server (~4.1%) — INVESTIGATED: NOT skippable (load-bearing); shipped L-ANIM-ITER+WAKE byte-identical (ba67824)

status: **completed** · metadata: `{"conclusion": "NOT render-only dead work. Server (master) reads computed animator transforms for gameplay: Monster::damageSources animationDamageParts poly+knockback (StarMonster.cpp:309-319 -&gt; DamageManager:80); Vehicle movingCollision/forceRegions/damageSources/loungeAnchor via finalPartTransformation (StarVehicle.cpp:466,515,534,559 -&gt; ActorMovementController:720-724,798-802 + DamageManager); Humanoid arm/hand m_useAnimation path (StarHumanoid.cpp:1799,1866) -&gt; ActiveItem projectile/damage spawn. FU Lua 'animator' table on MASTER scripts exposes partPoint/partPoly/transformPoint/currentRotationAngle (StarNetworkedAnimatorLuaBindings.cpp:61,166,171) attached to Monster/NPC/Object/Vehicle/ActiveItem/StatusController. update() also nets light/emitter active + drives Transition state auto-advance (netted stateIndex). Blanket skip NOT byte-equivalent; existing hasActiveAnimationWork() dormancy is the safe path. signalRegion in profile is unrelated WorldServer/Spawner code."}`

```
Workflow w7znw3iq8 (4 map + 3 adversarial refute + synth): blanket server-skip REFUTED on 3 counts (high confidence) — (a) NetworkedAnimator::update writes networked NetElementBool m_lights/m_particleEmitters .active every tick outside the dynamicTarget guard (cpp:1652-1668); (b) AnimatedPartSet::update Transition auto-advance mutates the netted activeStateIndex (StarAnimatedPartSet.cpp:226-232); (c) server-authoritative gameplay + master FU Lua read update-computed transforms (Monster::damageSources poly+knockback via partTransformation→DamageManager every tick; Vehicle movingCollision/forceRegions/loungeAnchor; Humanoid hand→ActiveItem projectile spawn; animator Lua table partPoint/transformPoint/currentRotationAngle/animationStateFrame on all major entity types). partTransformation blends local anim-affine + netted group-affine + local rotation — no safe transform/state split. hasActiveAnimationWork() dormancy is already the byte-safe coarse skip. L-WIND-A does NOT generalize.

SHIPPED (ba67824, byte-identical, unconditional): L-ANIM-ITER (transform-group loop) + L-ANIM-WAKE (hasActiveAnimationWork) via new AnimatedPartSet::forEachStateTypeUntil — kills per-call keys() StringList alloc + redundant m_stateTypes hash lookups (the profiled ~1.4% FlatHashTable::find). Adversarial byte-identity review (w5cebe718) = SHIP/high-confidence all 3. DEFERRED: L-ANIM-CACHE (ROI B, default-ON flag) — unresolved frame-keyed-property open question (drop-if-unprovable). REJECTED: L-ANIM-SKIP (blanket skip). Full result: /tmp/.../w7znw3iq8.output.
```

<a id="c29c1332-89"></a>

#### #89 — Object::update cluster (~4.5%) — INVESTIGATED; shipped L-OBJ-1+L-OBJ-2 byte-identical copy-elision (067008f)

status: **completed**

```
Workflow wcyayuhh6: ~3 of ~4.5% recoverable. Cluster dominated by COPYING, not recompute. SHIPPED 067008f (byte-identical, unconditional; review whk14nvho = SHIP/high both): L-OBJ-1 currentOrientation() by-value shared_ptr → const& + de-virtualize (resolved 3-4x/object/tick); L-OBJ-2 materialSpaces() by-value vector → const& across base+Object override + caller (sole caller compares-and-discards). game_tests 79/1-known-fail.

DEFERRED (flagged, real obligations) → see #90: L-OBJ-3 damageSources touch-source memo keyed on orientation (default-ON flag; MUST return copy — DamageManager mutates result in place; brief's 'transform-independent' premise was WRONG, it merges orientation->touchDamageConfig); L-OBJ-4 liquid-timer config-gate (dormancy flag); L-OBJ-5 client-gate light flicker+emission timers (L-WIND-A pattern). SCOPED OUT: the 2 virtual thunks (structural multiple-inheritance; L-OBJ-2 recovers materialSpaces cost anyway), nodeCount body (constant); REJECTED m_animationTimer freeze (networked frame desync). ADJACENT BIGGER WIN flagged → #90: DamageManager::update calls damageSources() for EVERY entity EVERY tick; a cheap hasDamageSources() short-circuit could beat L-OBJ-3 (lives in DamageManager). Full result: /tmp/.../wcyayuhh6.output.
```

<a id="c29c1332-90"></a>

#### #90 — SKIP-0 shipped; SIX sub-levers outstanding (SKIP-1/2/3 + L-OBJ-3/4/5) — all located, none started

status: **pending**

```
RETITLED 2026-07-25 by content audit at c9b2b024. The parent shipped; every deferred sub-lever is still
outstanding, and all six are now precisely located so none needs re-finding.

=== PARENT SKIP-0: CONFIRMED SHIPPED (content, not SHA) ===
virtual base source/game/interfaces/StarEntity.hpp:190 + StarEntity.cpp:92; the braceless gate at
source/game/StarDamageManager.cpp:84-91; flag DamageSourceSkip::enabled (StarDamageManager.cpp:11,
StarWorldServer.cpp:1625, assets/opensb/worldserver.config.patch:48); all six overriders return true.

=== THE SIX OUTSTANDING, with exact sites ===
 SKIP-1  Object per-instance precompute. source/game/StarObject.hpp:125 is still a bare `return true;`,
         and Object::damageSources() (StarObject.cpp:1229-1242) still does the per-call jsonMerge of
         touchDamageConfig with no ctor-time precompute. Re-verify the customOrientations counterexample
         before designing — that is what made this non-trivial.
 SKIP-2  Monster exact predicate. StarMonster.hpp:88 literally reads
         `return true; // L-DMG-SKIP-0 (exact predicate = deferred L-DMG-SKIP-2)`, and
         Monster::damageSources (StarMonster.cpp:287-298) still evaluates all 3 terms live.
 SKIP-3  selfDamageNotifications. The loop at StarDamageManager.cpp:136 sits OUTSIDE the gate; the comment
         at :88-89 confirms that is deliberate, so this is a scoped decision to revisit, not an oversight.
 L-OBJ-3 No memo of the touch source anywhere in StarObject.cpp.
 L-OBJ-4 The liquid timer ticks unconditionally at StarObject.cpp:427-429 with the config test still
         inside checkLiquidBroken (:1524-1529) — no config gate.
 L-OBJ-5 StarObject.cpp:458-462 ticks m_lightFlickering and m_emissionTimers on BOTH master and slave,
         with no PlantWind-style server skip (contrast StarPlant.cpp:15-16 + :735-740, which is the
         pattern to copy).

SUGGESTED ENTRY POINT: L-OBJ-5 is the closest analogue to an already-shipped, already-validated lever
(L-WIND-A) and is a slave-side dead store, so it is the cheapest to make byte-identical. SKIP-1 is the
highest-value but carries the customOrientations landmine.

All of these are SERVER-tick levers, so they need a WST capture to size — and note #84 records that no
server-thread profile has been taken since the render/lighting work began.
```

<a id="c29c1332-91"></a>

#### #91 — collisionSeparate sort (~2.3%) — INVESTIGATED: DETERMINISM-LOCKED, DEFER (no byte-identical win)

status: **completed**

```
Workflow wlyd3ss71. VERDICT: determinism-locked, ship nothing. collisionMove calls collisionSeparate up to 7x/move, each re-sorting m_workingCollisions IN PLACE by sortDistance with std::sort (UNSTABLE). Empirically proven (the agent compiled+ran a libstdc++ test): std::sort is NOT idempotent on its own output when ties exist → call N processes sort^N(fill), a chain. Hoisting to one sort needs sort(S1)==S1 (idempotence), which FAILS — and grid-aligned tile sortPositions produce abundant exact sortDistance ties. The per-call permutation is load-bearing: collisionSeparate accumulates SAT corrections in poly-iteration order → resolved position/velocity (NetElementFloat, master path) → DESYNC. Levers: L-A hoist REJECTED (non-idempotence); L-B N<=16-gated hoist byte-identical only on the COLD path (introsort recurses >16 → misses the hot cost), nets ~0.2-0.4% on a minority of moves; L-C broad-phase any-intersect early-out = only zero-desync candidate touching hot cost but measurement-gated (its own O(n) scan may be net-negative), speculative, flag+A/B; L-D size<=1 early-out byte-identical but worthless (misses hot path); L-E stable_sort/partial_sort REJECTED (changes tie order → desync). Confirms L5's verbatim-keep was correct. Full result /tmp/.../wlyd3ss71.output.
```

<a id="c29c1332-92"></a>

#### #92 — CDL: explore GPU lighting substrate (brainstorm step 1 — context)

status: **completed**

```
Ground the "engine-native cumulative dynamic lights" feature design in the actual GPU lighting code: StarGpuLightmapPass.cpp + shaders (lightingSpread/Point/Passthrough/world.frag), StarCellularLighting/LightArray (model + accumulation semantics), the lightingGpu config flag + A/B mechanism, addPointLight path + the point-light cap (≈32), static-fill vs Point paths, object lightType/pointLight (ObjectDatabase:549), CPU-fallback parity, and where tonemapping is/isn't today. Read-only.
```

<a id="c29c1332-93"></a>

#### #93 — CDL: clarifying questions + 2-3 approaches (brainstorm)

status: **completed**

```
After grounding: resolve the open design decisions one at a time — cap policy when ALL lights are dynamic (raise/remove/cluster), tonemap algorithm + insertion point (accumulation pass vs world.frag apply), the promote-rule (mirror the mod's non-zero lightColor/activeLightColor selection vs a cleaner engine rule), flag granularity/semantics (single toggle vs separable), GPU-only feasibility. Propose 2-3 approaches with a recommendation.
```

<a id="c29c1332-94"></a>

#### #94 — CDL: present design + get approval (brainstorm)

status: **completed**

```
Present the design in sections (architecture / promote mechanism / GPU accumulation+tonemap / A/B flag / fallback+parity / testing), approval per section. HARD GATE: no implementation until approved.
```

<a id="c29c1332-95"></a>

#### #95 — CDL: write + commit design spec; spec self-review; user review

status: **completed**

```
Write the validated design to kubebound specs (docs/superpowers/specs or kubebound/specs/ per project convention), self-review (placeholders/consistency/scope/ambiguity), commit, then user reviews the written spec before planning.
```

<a id="c29c1332-96"></a>

#### #96 — CDL: transition to writing-plans (brainstorm terminal)

status: **completed**

```
Only after spec approved: invoke writing-plans to create the implementation plan. Then build behind a default-ON/A-B config flag with adversarial review (the established lever discipline), gated on out-of-game builds.
```

<a id="c29c1332-97"></a>

#### #97 — CDL-T1: tonemap operator (C++) + unit tests [TDD core]

status: **completed**

```
Plan 2026-06-22 Task 1: add Star::tonemapHighlights(Vec3F,float) to StarCellularLighting.{hpp,cpp} (value-preserving highlight rolloff, identity<=1, asymptote at white-point), + cellular_lighting_test.cpp with 4 invariant tests (zero, normal-range-untouched, hue-preserved rolloff, monotonic+bounded). TDD: failing test first.
```

<a id="c29c1332-98"></a>

#### #98 — CDL-T2: lightingPromoteDynamic + lightingTonemap flags + /lighting toggles

status: **completed**

```
Plan Task 2: RootLoader defaults (both false) + asset config keys + live reads (StarWorldClient ~1854) + /lighting sub-commands (ClientCommandProcessor ~696-742). Mirror the lightingGpu* pattern. Build-verify.
```

<a id="c29c1332-99"></a>

#### #99 — CDL-T3: GPU compose tonemap (lightingPassthrough.frag) + CPU mirror

status: **completed**

```
Plan Task 3: uniform-gated tonemap branch in lightingPassthrough.frag (mirrors tonemapHighlights), plumb via PointParameters/processFull + WorldPainter; CPU mirror at StarCellularLighting.cpp:~184 for shadow-compare parity. Flag-off = byte-identical clamp. Build.
```

<a id="c29c1332-100"></a>

#### #100 — CDL-T4: promote Spread-&gt;PointAsSpread at dispatch

status: **completed**

```
Plan Task 4: at StarWorldClient.cpp:1827-1843, reclassify Spread->PointAsSpread when lightingPromoteDynamic && gpuLightingActive. Flag-off = byte-identical. Build.
```

<a id="c29c1332-101"></a>

#### #101 — CDL-T5: integration — game_tests + byte-identity + adversarial review + deploy dev

status: **completed**

```
Plan Task 5: full build + game_tests (expect 83 pass / 1 known ItemComparison), adversarial byte-identity+parity review (flags-off byte-identical, GPU/CPU tonemap match, pointAdditive untouched, promote gated on GPU), deploy dev. (CDL-T6 live A/B + cherry-pick deferred to in-game.)
```

<a id="c29c1332-102"></a>

#### #102 — CDL-T6 (in-game): live A/B (promote × tonemap, 4 combos) + decide defaults + cherry-pick to feat/gpu-lighting-standalone

status: **completed**

```
Deferred to in-game. In the dev build at a dense FU base: /lighting promotedynamic {off,on} × /lighting tonemap {off,on}. Visual eval (promote = directional shadows/occlusion upgrade? tonemap = wash-out gone, normal scenes unchanged?) vs the [oSB] Cumulative Dynamic Lights mod (3444407977, downloaded) as reference. E-core-pinned render/lighting-thread GPU-cost profile of promote-on (the heavy-dynamic-lights workload; also the ideal scene to finally A/B the GPU-lighting offload headline). Then decide flag defaults (keep both OFF, or default-ON the tonemap if a strict improvement). Then cherry-pick the 5 CDL commits (a952aea f064be0 b59513e e13e832 f895868) onto feat/gpu-lighting-standalone + build-verify (manual resolve possible on diverged shader files). Engine HEAD f895868; deployed to dev.
```

<a id="c29c1332-103"></a>

#### #103 — Tier-0 M1 aggregate A/B: build baseline binary at fc3d55e (merge-base w/ main,

status: **completed**

```
Tier-0 M1 aggregate A/B — CAPTURED + ADVERSARIALLY VERIFIED (panel wlkufhccd). CORRECTED RESULT (my initial framing overstated it): at ONE lighting-dense FU-base spot, both frame-limited 40fps (MangoHud cap, NOT GPU-bound), neither CPU-bound (<0.7 cores): dev uses ~20% less CPU TIME/cycles per frame than vanilla baseline e3b7021 (n=1). task-clock -21.3%, cycles -22.1% are the SAME measurement at pinned 4.3GHz (not 2 confirmations). instructions -38.3% is an IPC ARTIFACT (1.51->1.20), a mechanism detail NOT a time/heat saving — do not headline it. Attribution VERIFIED strong: the ~20% is ~100% the GPU lighting OFFLOAD (CellularLightArray ~23% of baseline self-time -> ~1.3% in dev); byte-equivalent micro-levers barely moved cycles. CRITICAL: it's an OFFLOAD not elimination — CPU lighting moved to the same Arc iGPU package, so NO thermal win is established (CPU measured only; GPU/package power NOT measured). 'not halved' is SCOPED to this CPU-idle spot, NOT campaign-wide — a CPU-bound dense-lighting scene would likely save more (vanilla cellular lighting balloons; dev flat on GPU). Rigor gaps: n=1, counters ~72% multiplexed, dev 2.1x page-faults (state drift), two-variable (binary+assets+CDL+telemetry-on). For a DEFENSIBLE number: CPU-bound scene + repeats + telemetry-off pass + GPU/package power. Raw data /tmp/m1-perf/. NEXT: GPU baseline/telemetry (uncapped + perf i915/rcs0-busy + GALLIUM_HUD, then GL_TIME_ELAPSED per-pass timer-query feature) — this is the missing half of the thermal question. M2 flag-attribution (#82/#84) still pending.
```

<a id="c29c1332-104"></a>

#### #104 — FU-Lua native-offload decision = DEFER (not no-go), adversarially verified

status: **pending**

```
FU-Lua native-offload decision = DEFER (not no-go), adversarially verified (panel w46s8gaeh REFUTED my "not justified" framing). Measured: FU-Lua ~8% of total CPU / ~5.5% of one core, flat across quiet/B1/B2 (0.64→0.66→0.69 cores), no named item-network hotspot. CORRECTED FRAMING: (1) capture is vsync-limited → sim thread only ~0.12 cores, 7-8x frame-budget headroom → GUARANTEED not to stress sim → "box not stressed" is CIRCULAR, can't bound at-scale cost. (2) "no hotspot" is an ARTIFACT — FU transport is generic Lua folded into luaV_execute/luaH_get (undifferentiated; it IS the documented #1 FU hotspot per mod-hypervisor.md), cost is inside the 8%, unnamed. (3) 8% is a self-time FLOOR (excludes FU-driven net-delta tail: queueUpdatePackets/netStorePump/writeNetDelta) AND an upper bound on the FU-ADDRESSABLE prize (folds in vanilla Lua-VM cost the offload can't touch) → true addressable <8%, unmeasured. (4) two bases = stability at ONE complexity tier, NOT the scaling curve. (5) RIGHT metric = single-thread WorldServerThread occupancy (~0.12 → watch toward 1.0), NOT process-fraction. (6) dirty-gate lighting is "AND not INSTEAD" — different thread/axis. GATE to reconsider: re-profile the target-scale factory with framerate UNCAPPED (vsync off) + track WST single-core occupancy + verified item throughput; reconsider only if WST nears saturation. Cheap add-ons from existing dwarf: --children inclusive rollup (running) + machinery ON/OFF A/B. Raw data /tmp/fulua/. Persist to kubebound FU-Lua decision note.
```

<a id="c29c1332-105"></a>

#### #105 — GPU timer telemetry + dirty-gated lighting SPIKE — authored on

status: **completed**

```
GPU timer telemetry + dirty-gated lighting spike — authored on feat/gpu-lighting-standalone, built clean, playtested: validate.mismatch=0 (5min soak), active skip-rate ~0.4%, GPU timers reveal POINT PASS = 1.68ms/frame (66% of ~2.55ms GPU lighting; spikes 15ms), spread 0.85ms, compose 0.02ms; lighting is GPU-bound (2.55ms GPU vs 0.7ms CPU submit). DISPOSITION (per no-dormant-default-off-debt rule): (1) TELEMETRY = valuable now -> commit on feat + cherry-pick to dev/perf-integration + rebuild dev. (2) DIRTY-GATE = STAKED (correct but idle-only, ~0 active value on a cool box) -> commit to a dedicated feat/dirty-region-foundation branch (NOT feat-main, NOT dev) so no shipping line carries dormant code. Stake record: /root/kubebound/2026-06-26-staked-dirty-gated-lighting.md (mission=dirty-REGION lighting; default-ON triggers = skip-rate>=25% AND broader 0-mismatch soak; mission-start trigger = active-play lighting binding at scale). NEXT (pending user go): execute commit-split + cherry-pick telemetry to dev + rebuild dev. Then choose next mission: direct point-pass GPU lever (cheaper, active-play, no scaffolding) vs dirty-REGION.
```

<a id="c29c1332-106"></a>

#### #106 — Point-pass GPU optimization — Part 1 BYTE-EQUIVALENT (authored on

status: **completed**

```
Point-pass GPU optimization Part 1 (byte-equivalent) — SHIPPED. feat/gpu-lighting-standalone 1626034 -> cherry-picked clean to dev/perf-integration 1a3e4e4 -> built + deployed to /home/apnex/OpenStarbound/dev/ (binary + lightingPoint.frag/.config 21:41; uniforms bind-verified). 1A: hoisted per-fragment-constant ALU (cos/sin beamDirection, maxIntensity, perBlockAtten x2, oneMinusBeamAmbience) to CPU uniforms; removed 6 dead uniforms. 1B: bbox cap min(maxIntensity,1)*airReach (provably byte-identical). VERIFIED via code proof (line-by-line shader equivalence; 4/5 uniforms bit-exact, cos/sin ULP on beam-lights/8-bit only) + 14 lighting game_tests pass. Shadow-compare was structurally unable to ULP-gate (promote0=empty point pass; promote0.5=CDL gap swamps); confirmed the GPU-vs-CPU gap is pre-existing pipeline (CDL ~2/3 + spread-Jacobi/16F floor ~1/3), not Part 1. WIN MAGNITUDE UNMEASURED CLEANLY (all A/Bs confounded by scene/shadow-overhead) — expected ~3-10% point.gpu_us + bbox helps the 15.5ms spike; read cleanly later via /telemetry on in normal play, promote held 0.5. NEXT byte-identical code levers (no CDL reversal): spread obstacle-precompute (kill ~256x redundant per-iteration obstacle re-fetch), then dirty-REGION (staked feat/dirty-region-foundation).
```

<a id="c29c1332-107"></a>

#### #107 — Dirty-REGION lighting — Stage 2 implemented+fixed, building; next: oracle-gated verify

status: **completed**

```
STAKED (outcome: measured dead-end). Dirty-region partial recompute is correctness-proven (Stage-0 tracker 19287 checks/0 under-reports; Stage-2 oracle self-check passed, 0 mismatches) BUT the opportunity is ~zero in real FU play: the force-full meter measured 98% of edit frames force-full due to per-frame entity LIGHT FLICKER (FU lights randomize intensity every frame) -> partial path never ran (0 partial frames over 4200 edit frames, user stationary). Hits the design's own kill criteria. Decision (user): stake the obvious wins, do NOT carry the default-off scaffolding forward as debt. Disposition: dev keeps CDL+telemetry+Part1 (already there); land ONLY the flicker-fix core on dev (real always-on fix; drop its LightFingerprint hunk); dirty-region Stages 0-2+oracle+meters stay ARCHIVED on feat/dirty-region (NOT merged). Forward direction = temporal decoupling (task #109). Docs: /root/kubebound/2026-06-26-dirty-region-lighting-DESIGN.md + 2026-06-27-stage2-plan.md (record the flicker stake outcome).
```

<a id="c29c1332-108"></a>

#### #108 — Propagate flicker fix (lightingPromoteMinIntensity, 4c45f1a) to gpu-lighting-standalone + dev

status: **completed**

```
Flicker fix (lightingPromoteMinIntensity, commit d8c2d28) now ON dev/perf-integration — rode along with the temporal-lighting FF merge. Dev no longer has the item-drop flicker bug. (gpu-lighting-standalone branch propagation not pursued; dev is the live integration target.)
```

<a id="c29c1332-109"></a>

#### #109 — Temporal lighting decoupling — brainstorm → design → plan → implement (flicker-robust GPU-load cut)

status: **completed**

- cited in `docs/board.md`

```
Temporal lighting decoupling — SHIPPED to dev/perf-integration (FF merge of feat/temporal-lighting @ 5ecb5aa). Verified GO: in-game RAPL A/B (2 locations) GPU -6.9% / pkg -1.9%, replicated; visuals clean. 5 temporal-gate unit tests + 19 lighting tests pass on merged binary. Deployed (sha a322c611) to /home/apnex/OpenStarbound/dev + dev-feat; pre-temporal dev binary backed up.
```

<a id="c29c1332-110"></a>

#### #110 — GPU-render-ladder: mission design + spec (brainstorm)

status: **completed**

```
Measure-gated, leverage-first GPU/render efficiency ladder. Design APPROVED; spec at /root/kubebound/2026-06-27-gpu-render-ladder-mission.md. Frontier analysis: 2026-06-27-gpu-render-frontier.md. Next: user spec review → writing-plans.
```

<a id="c29c1332-111"></a>

#### #111 — GPU-ladder Rung 0: per-pass GPU-timer telemetry + RAPL harness

status: **completed**

```
Byte-identical, default-on. Per-pass GL_TIME_ELAPSED brackets (lightmapGen|worldPass|parallax/env|compose+blit) on the existing timer ring (StarRenderer_opengl.cpp:819-855), surfaced via Star::Telemetry gauges + /debug HUD line. Document the RAPL A/B claim protocol. Enables per-pass attribution for every later rung.
```

<a id="c29c1332-112"></a>

#### #112 — GPU-ladder Rung 1 (R-A): bicubic→bilinear lightmap sample

status: **completed**

```
R-A SHIPPED to dev via Form 2. Form 1 (plain bilinear) was strong power (GPU -21/-24%) but blocky → staked default-off (kept as /lighting bilinear dev toggle). Form 2 (bicubic-upscale, #116) is the shipped smooth version.
```

<a id="c29c1332-113"></a>

#### #113 — GPU-ladder Rung 2 (R-D+R-E): overdraw trim

status: **completed**

```
R-D shipped (c6ad85a): kept byte-identical transparent-layer skip (floor(255*alpha)==0), unconditional. The alpha-threshold lever (renderParallaxMinAlpha) was MEASURED a dead-end — parallax is opaque-overdraw-bound (timer 3285→3317us flat across minalpha 0→0.2), so the knob was removed (no default-off debt). R-E dropped (compose=0us). R-C (Rung 3) deferred (world pass modest after R-A). GPU-render ladder effectively complete; R-A (bicubic-upscale) is the banked win.
```

<a id="c29c1332-114"></a>

#### #114 — GPU-ladder Rung 3 (R-C): opaque tile z-prepass [CONDITIONAL]

status: **completed**

```
DEFERRED (decided, not building). R-C opaque-tile z-prepass targets the world pass, but Rung-0 timers show world is only 1.2-3.4ms after R-A (not dominant) → not worth the L-effort + blend-order risk. Revisit only if a future profile shows world-pass overdraw dominating again.
```

<a id="c29c1332-115"></a>

#### #115 — GPU-ladder backlog: R-F/R-G/R-I (evidence-gated)

status: **completed**

```
ASSESSED / parked. R-G (draw-call batching/VAO) + R-I (adjustLighting fold) = skip: low GPU-watt, CPU-side micro-opts. R-F (instanced particles) = evidence-gated: calm scenes had 4-10 particles (tiny); only worth building if a particle-heavy COMBAT capture shows client_render_world_painter spiking with client_render_particle_count (measurable free via the existing /debug HUD — no new code).
```

<a id="c29c1332-116"></a>

#### #116 — GPU-ladder Rung 1b (R-A Form 2): bicubic-upscale lightmap

status: **completed**

```
SHIPPED to dev/perf-integration (13f6efe). lightingUpscale effect bicubic-upscales the composed lightmap to an Nx linear FBO; world does 1 bilinear tap. Default lightingWorldUpscale=2 (smooth at 2x and 4x). Claimed GPU ~6-11% / pkg ~1-4% (n=1, cores noise), smooth = bicubic quality. Bug found+fixed: effect wasn't registered (loadEffectConfig) → silent bilinear-passthrough fallback; also guarded switchEffectConfig. Deployed binary+assets to dev+dev-feat.
```

<a id="c29c1332-117"></a>

#### #117 — CPU arc — lighting tile-gather lever: investigate + design options

status: **completed**

```
DONE — investigation + design options in 2026-06-27-exploring-profile-lighting-gather.md (5b130e4). VERDICT VIABLE: gather is tile-only, entity-light flicker doesn't apply (separate stage); win = CPU gather ~8% exploring, complementary to temporal; visual-only. Options: A scroll-incremental cached gather (structural, MVP=shift-on-scroll+full-on-edit, no new tracker; MED risk, M-L); B cheaper per-column gather (byte-identical micro-opts: hoist Either resolution, memoize last-material radiantLight, cut worker-pool per-task overhead; LOW risk, S-M). Next: brainstorm-approve → plan → build when user OUT of game.
```

<a id="c29c1332-118"></a>

#### #118 — CPU arc — lighting-gather lever: BUILD B1/B2 + A1/A2 (when user out of game)

status: **completed**

```
BUILT+committed B1(7af0214) B2(14af710) A1(e065165) A2(7b47322) on feat/lighting-gather; adversarial review GO-WITH-FIXES (fixes folded into A2); deployed full B+A binary to dev-feat (sha 9d2e0fa0); dev untouched (f4e43e72). REMAINING: user in-game A/B on dev-feat (visual correctness while walking/day-night/edit + lighting.cpu.gather.us off-vs-on) → then land to dev (FF merge feat/lighting-gather→dev/perf-integration) + deploy dev+dev-feat, or default-off/stake if visuals fail.
```

<a id="c29c1332-119"></a>

#### #119 — B1: per-column setCellColumn (hoist Either/monochrome branch)

status: **completed**

```
Add CellularLightingCalculator::setCellColumn (decl + inline) mirroring setCellIndex; rewrite lightingTileGather to stage column into WorldSectorSize stack buffers + single setCellColumn. Byte-identical (preserve pos[1]+y>undergroundLevel verbatim). Build, game_tests lighting filter, commit.
```

<a id="c29c1332-120"></a>

#### #120 — B2: memoize vertical material runs in gather

status: **completed**

```
Memoize fg/bg radiantLight across identical (materialId,modId) runs in the gather column loop. Byte-identical (radiantLight pure). Build, lighting game_tests, commit, deploy dev-feat, in-game lighting.cpu.gather.us A/B.
```

<a id="c29c1332-121"></a>

#### #121 — A1: persistent stable grid + env-light overlay + cache-hit

status: **completed**

```
lightingGatherCache flag + /lighting gathercache toggle. Stable grid {stableLight,obstacle,skyExposed} keyed by epoch/anchor/dims. applyStableToCells re-applies environmentLight via skyExposed. Cache-hit skips gather on still camera. Build, test, deploy, in-game visual + gather.us check.
```

<a id="c29c1332-122"></a>

#### #122 — A2: scroll shift + margin gather (exploring win)

status: **completed**

```
On scroll (anchor changed, same dims/epoch): memmove-shift stable grid by integer-tile delta + gather only the L-shaped margin. Force-full on size-breathe/zoom/epoch/large-jump. Adversarial-review the shift math via workflow before in-game. Build, test, deploy, in-game A/B gather.us off vs on + visual leading-edge check. Keep default-on iff win+clean.
```

<a id="c29c1332-123"></a>

#### #123 — Combat server-thread call-graph capture (ARMED — fires on user fight signal)

status: **completed**

```
User wants a combat perf call-graph captured next time they're in a fight, to open the tick.server.compute (FU sim/Lua) lever set. ARMED: run scratchpad/combat-capture.sh [30] the moment the user signals 'now'/'fighting' during a sustained fight. E-core pinned record AND report (taskset -c 6-15). Then perf report filtered to WorldServerThread (caller call-graph) to find what's hot inside tick.server.compute. Game currently running PID 35130.
```

<a id="c29c1332-124"></a>

#### #124 — Brainstorm LuaEngine marshalling-efficiency lever (combat/exploring Lua tax)

status: **pending**

```
Combat call-graph (2026-07-05, 2 captures) → no dominant native lever; the concentrated actionable cost is the engine-side C++↔Lua marshalling layer: createWrappedFunction (0.42%), Variant<Lua> destruct/doCall/assign churn (0.33%+), jsonContainerToTable, callback-registry FlatHashTable dispatch, LuaHandle dtor. Engine-only, behavior-preserving, benefits ALL FU Lua (combat+exploring). Distinct from #104 (invasive native-offload of FU logic). Brainstorm→design→build when user wants to push the campaign. Findings: /root/kubebound/2026-07-05-combat-callgraph-findings.md
```

<a id="c29c1332-125"></a>

#### #125 — RB-STREAM DONE AND SHIPPED: VBO orphaning cut the GPU frame 74-81% at the Director's bases

status: **completed**

```
CLOSED 2026-07-25. This task carried a WRONG status twice and both were mine; recording that so the record is trustworthy.

THE WORK IS SHIPPED, DEFAULT-ON, AND MEASURED.
  code:    source/application/StarRenderer_opengl.cpp:1311-1322 (BUFFER ORPHANING, guarded by NoVboOrphan)
           setVboOrphan wired at source/client/StarClientApplication.cpp:507
  default: "renderVboOrphan": true  (source/game/StarRootLoader.cpp:102)
  telemetry: render.flush.count + render.flush.primitives in flushImmediatePrimitives()
  origin commit: d1a145bf (2026-07-13) "perf(render): orphan the immediate VBO -- the GPU frame drops 74-81%"
  in integration via: 083c6340 (the render/layer1 reorg, which carried the whole OpenGlRenderer backend)

MEASURED on the real GPU at the Director's three actual bases, frozen scene, whole-frame GPU span with
per-pass timers off:
    Ocean Lab        11,524us -> 2,909us    -8,615us   (-74%)
    Lava Refinery    12,905us -> 2,433us   -10,472us   (-81%)
    Surface Outpost  12,789us -> 3,205us    -9,584us   (-74%)
At 60fps vsync: ~69-77% GPU busy -> ~15-19%. That IS task #132's original complaint ("idle at base shows
GPU 50-60% for a static 2D scene") and it is solved.

THE FIX, one line: glBufferData(capacity, nullptr, GL_STREAM_DRAW) before the glBufferSubData tells the
driver the old contents are dead, so it returns a FRESH backing store instead of stalling until the
in-flight draw finishes reading the old one. Byte-identical by construction -- the orphan marks the old
contents dead, glBufferSubData writes the same bytes into [0,size), and the draw reads only
[0,vertexCount) inside that range. A/B gate: Lava Refinery and Surface Outpost byte-identical MATCH;
Ocean Lab reports a DIFF that its own NULL CONTROL also reports (14,320px, identical settings both legs)
-- it is underwater and the water animates client-side, so the DIFF is not attributable to the change.

THE DIAGNOSIS (preserved, and it was right): flushImmediatePrimitives re-writes the SINGLE SHARED
immediate VBO and immediately draws from it. Widget::render -> setupDrawRegion -> setScissorRect calls it
for EVERY widget; the HUD is ~92 widgets across 5 panes. ~21 extra flushes/frame, ~8,296us, ~400us per
flush carrying TWELVE QUADS. Twelve quads cannot cost 400us of rasterisation -- it was 21 stalls, never
rendering cost.

=== TWO WRONG STATUSES, BOTH MINE ===
1. It sat 'in_progress' for days saying "revive feat/render-buffer-stream (8d9cb4b)" when the lever had
   already shipped by a different route.
2. WORSE: during the 2026-07-25 hygiene sweep I rewrote it as "the IMPLEMENTATION is lost, rebuild it",
   concluding that from a missing branch, an unresolvable SHA, an empty reflog and no matching dangling
   commit. All four observations were true and the conclusion was still wrong: the reorg REWROTE HISTORY,
   so pre-reorg SHAs do not resolve even for work that shipped. I never grepped the tree for the code.
   ABSENCE OF A BRANCH IS NOT ABSENCE OF THE WORK. Check content, not structure.

STILL OPEN, genuinely (filed as the residual, not part of this task): the flush COUNT is untouched.
setScissorRect still flushes per widget, ~92 times, and scissor is not a vertex-format change -- the
primitives could be batched per scissor rect, or the scissor folded into the draw. Orphaning removed the
COST of each stall; removing the flushes themselves is a separate and possibly further win. See #173.
```

<a id="c29c1332-126"></a>

#### #126 — Fix pre-existing game_tests breakage (SpawnTest hang + ItemComparison)

status: **completed**

```
DONE 2026-07-14 (649e2965). Phase 0 gate work for the render-surface programme.

  before: hangs forever at SpawnTest.RandomCelestialWorld; suite unusable; no verdict, ever
  after:  91 tests, 22.8s, 90 PASS / 1 FAIL

INVOCATION GOTCHA (part of why it read as "broken"): game_tests MUST be run from dist/ -- it needs
sbinit.config in the CWD. From the repo root it aborts with "Could not perform initial Root load".
  cd dist && ./game_tests

THREE ROOT CAUSES, audited DOWN (A8 Law of Fallback), not patched at the symptom:

1. THE GATE DEFECT (source/test/StarTestUniverse.cpp): TestUniverse::warpPlayer had
   `while (isTeleporting() || playerWorld().empty())` with NO exit. When the warp could never complete the
   test HUNG rather than failed, taking the whole suite with it. A gate that cannot return a verdict
   certifies nothing. Now bounded (30s) + throws with context. This is what made the two bugs below
   visible at all.

2. ENGINE BUG -- Player::shipSpecies() returned "" for a freshly created player. m_shipSpecies is assigned
   ONLY on the disk-load path (StarPlayer.cpp:265). A player that never round-trips through storage has an
   empty ship species -> server does speciesShips.get("") -> MapException -> the client's ship world dies
   -> the warp never completes. Live play masks it (a new character is saved before it is played, and the
   serializer at :2555 already applies the right fallback). Fixed in the accessor with the same fallback.
   GENUINE UPSTREAM BUG -- candidate for a PR.

3. ENGINE/TEST BUG -- the headless Lua environment was incomplete. assets/opensb/scripts/opensb/player/
   copy_paste.lua calls input.bindDown() every update, but the "input" callback table is installed by
   ClientApplication (the GUI app) and nothing else -- so ANY UniverseClient built directly (TestUniverse,
   a headless client, a bot) hands the shipped player scripts a nil `input` and they throw once per frame.
   Input's ctor is headless-safe (no SDL). TestUniverse now owns one and installs the same callbacks a real
   client does. (A3 Air-Gap: the script declares a dependency; the environment must satisfy it.)

REMAINING RED (1/91) -- ItemTest.ItemComparison. NOT swept, tracked as #146:
   Item::matches(descriptor, exactMatch=true) compares the item's POST-buildscript parameters against the
   descriptor's PRE-buildscript ones (tryCreateItem -> itemConfig() runs the buildscript; cf. the "Seed
   could've been changed by the buildscript" comment at StarItemDatabase.cpp:238). Once a buildscript
   touches parameters they can never be equal. Pre-existing, unrelated to rendering, and fixing it means
   DECIDING what "exact match" is supposed to mean -- an engine-design call, not a bugfix.

VERIFIED: core_tests 226/226; game_tests 90/91; renderer untouched and still bit-identical (env oracle
60 MATCH / 0 DIFF, parallax oracle 60 EXACT / 0 DIFF).</description>
</invoke>
```

<a id="c29c1332-127"></a>

#### #127 — Texture-upload churn (#127) — stable-grid lever SHIPPED (91e3fca, cluster 7.09%→2.37%)

status: **completed**

```
Stack classification (2026-07-05, warm legacy combat capture) proved the kernel GEM/shmem/page-alloc cluster is ~98% _mesa_TexSubImage/TexImage — TEXTURE uploads, not vertex buffers (which staked #125). Something re-uploads textures continuously in steady state. Suspects: texture-atlas compressionPass (finishFrame), per-drawable dynamic texture creation (drawable cache interplay), font/glyph uploads. Investigate: who calls TexSubImage per frame (perf script stack classification above st_TexSubImage), volume/frequency, then design lever. Thermally-coupled iGPU path. Findings context: /root/kubebound/2026-07-05-render-buffer-churn-findings.md OUTCOME section.
```

<a id="c29c1332-128"></a>

#### #128 — Lighting GPU-pass dispatch CPU lever (~13% render-thread frame CPU)

status: **completed**

```
Stage-1 (L1 includeVBTextures + L3 persistent fullQuad buffer + L5 cached uniform handles) SHIPPED byte-identical, FF-merged dev/perf-integration @ 5d86ec5 (binary e68355d9). Win ~20-28% combat dispatch (mechanism), -44% observed cross-session. Stage-2 = L2 engine-wide VAO bake, now tracked separately.
```

<a id="c29c1332-129"></a>

#### #129 — L2 VAO-format bake (#129) — MEASURED NULL, cleanly REMOVED (revert 73c3ec4)

status: **completed**

```
Stage-0 default-OFF spike, then settled at user's base with two independent in-game methods (~435 draws/frame, dead-still controlled A/B): windowed telemetry = pooled +0.6% / sign-flipped adjacent pairs (noise); perf sampling (18.8k samples/state, E-core pinned) = renderGlBuffer self 0.82%→0.70% (~0.12pp noise) while total libGL/mesa/driver ROSE 1.54%→2.19%. Null root cause: glVertexAttribPointer is a cheap client-side state write (~tens of ns); ~3,700 attrib calls/frame removed = <0.2% render CPU. No code-cleanliness rescue (adds a parallel flagged path; cleaner only if default-ON, which the null forbids). Per default-ON-or-remove rule → reverted 4 commits (tree byte-identical to pre-L2 5d86ec5), archived on feat/l2-vao-bake, render.draws counter dropped. PENDING: rebuild (E-core, out-of-game) + redeploy — also lands the 93f45e0 json-underflow engine fix into the live binary. Then #124 Lua Tier-1.
```

<a id="c29c1332-130"></a>

#### #130 — Base In A Box — Reforged: sovereign mod fork (scan/print/dup)

status: **in_progress**

```
Sovereign apnex_ fork of the scan/print/dup capability (BiaB 729460427 + MiaB 729456260) for OSB+FU. Repo /home/apnex/base-in-a-box-reforged (moved from /root — /root unreadable by the apnex game process). STAGE 0 (extraction + scaffold + baseline) DONE + VERIFIED IN-GAME 2026-07-07: mod loads clean, scan→print loop works, ZERO LuaExceptions, and recipes are craftable normally at the Engineer's Table with NO /learnblueprint needed (defaultBlueprints applied fine for the existing char — earlier concern was wrong). build_fork.py reproducible; verify.sh green; deployed loose to dev+dev-feat sbinit (BiaB/MiaB unsubscribed + stale sbinit pak entries removed by me). NEXT = HARDENING BACKLOG: (2) usability/discoverability — no in-game instructions, confusing names (Receiver='Scanning Marker'); (3) author known issues (can't scan trees/unbreakable; leaves blocks in protected areas; print-on-self); (4) FU round-trip fidelity — scan/print FU custom blocks/materials/liquids/objects faithfully + large-area perf (the utility core). Each = own design→fix→verify cycle.
```

<a id="c29c1332-131"></a>

#### #131 — GL_INVALID_VALUE ROOT-CAUSED AND FIXED: inactive vertex attribute location -1 fed to a GLuint index

status: **completed**

- `e02d4484` docs(board): self-check the board -- dangling commit ids and missing evidence
- `ba0d22ef` docs: make the task board durable -- generated docs/board.md + exporter
- `84203421` docs(telemetry): record the two #131 traps -- saturating glGetError, and KHR_debug for localisation [#131]
- `32b8f849` render: fix the per-frame GL_INVALID_VALUE -- inactive vertex attributes
- `7b15c880` gl: bisect the per-frame GL_INVALID_VALUE to application-&gt;render() [#131]
- `1df96d68` gl: drain errors every frame, count them, and make the gate actually FAIL on them [#131]

```
CLOSED 2026-07-25 (32b8f849 fix, 84203421 docs; pushed to origin/integration).

ROOT CAUSE (proven, not inferred): glGetAttribLocation returns -1 for an INACTIVE attribute -- one absent from the shader, or declared and never read. lightingPassthrough.vert declares all four vertex inputs and reads only vertexPosition, so three of its four locations are -1 on every fullscreen composite. renderGlBuffer passed those into glEnableVertexAttribArray / glVertexAttribPointer / glVertexAttribIPointer, whose index parameter is a GLuint: -1 arrives as 0xFFFFFFFF, always >= GL_MAX_VERTEX_ATTRIBS -> GL_INVALID_VALUE, three per composite draw, every frame.

FIX: skip attributes whose location is < 0. Byte-identical -- those calls were already failing and therefore already doing nothing, and an inactive attribute is never read by the program.

VERIFIED: render gate PASS with and without the interface; envoracle 83/83, paralloracle 20/20, spreadoracle 206/206, 0 DIFF; GL error lines 0 (drains) AND 0 (independent synchronous KHR_debug callback, which sees every error, not just the first per drain window); the shutdown line is gone; core_tests 251/251, game_tests 92/92.

TOOL SHIPPED: STAR_GL_DEBUG=1 arms glDebugMessageCallback + GL_DEBUG_OUTPUT_SYNCHRONOUS with a printStack dump. This is what made the diagnosis conclusive -- glGetError can only say "an error happened somewhere since the last drain", so drain-placement bisect narrows to a region and stops. Off by default (forces synchronous driver behaviour).

WHY IT MATTERED: the GL error flag SATURATES -- once set, nothing further is recorded until glGetError clears it. One per-frame error masked every other GL error in the engine, which is what made "OpenGL errors during shutdown" unattributable and left the render gate's GL assertion unable to mean anything. The gate's GL check is now a real gate for the first time.

CORRECTED EN ROUTE: the earlier drain-placement bisect reported the errors as living in renderParallax. That reading was an artefact of logGlErrorSummary's 16-burst log budget being consumed by whichever drain reached it first; the KHR_debug callback put them in renderGlBuffer, reached from several call sites. Recorded as a trap in docs/telemetry/architecture.md section 7.
```

<a id="c29c1332-132"></a>

#### #132 — Idle-GPU floor investigation: profile static-scene per-pass GPU cost (base/ship) → floor-reduction levers&lt;/subject&gt; &lt;parameter nam…

_Stored subject exceeds the heading; reproduced verbatim:_

```
Idle-GPU floor investigation: profile static-scene per-pass GPU cost (base/ship) → floor-reduction levers</subject>
<parameter name="description">User intuition (2026-07-12): idle/light gameplay at base/ship shows CPU ~8-10% but GPU 50-60% on an Intel Arc Pro 130T/140T (Arrow Lake-P) — too high for a static 2D scene; GPU holds 1300MHz (of 2350), vsync on. Reopens the GPU arc with a FLOOR-reduction framing (leverage-first ladder targeted levers, never the static-scene floor). Tooling: the game's always-on per-pass GPU timers (GL_TIME_ELAPSED) surface in telemetry snapshots — render.pass.{world,parallax,environment,compose}.gpu_us (count=frames) + lighting.gpu.{point,spread,compose,upscale}.gpu_us (count=recomputes). Method = same telemetry-windowing as the L2 A/B: user stands still, /telemetry snapshot ×2 ~20s apart, difference → per-pass µs/frame at that static location. Session-avg (mixed-scene) hint: env~1614 + parallax~1939 + world~1839 per frame + lighting point~2019/spread~814 per recompute ≈ ~7.4ms/frame ≈ 44% of 60fps budget — every pass runs full-cost every frame even when static. Candidate floor levers (TBD by data): full-frame/parallax/world caching when static (present cached texture until something changes), point-light pass cost, environment pass. Next: capture idle-base window, then idle-ship, identify fattest static-redundant pass, design a "static scene sips power" lever.
```

status: **completed**

- cited in `docs/board.md`

```
DATA COLLECTED (2026-07-12) — 4-location idle per-pass GPU matrix (µs/frame), Intel Arc Pro 130T/140T, vsync60, GPU held 1300-1400MHz idle. UNDERGROUND: env2307(OCCLUDED=waste) parallax1076 world1959 lightPoint1750 spread812 recompute77% TOTAL~7950(48%). OCEAN: env1599 parallax4062 world1723 lightPoint1152 recompute56% TOTAL~8960(54%). TERRESTRIAL(weather): env1937 parallax3949 world1472 lightPoint2090 recompute100%(weather defeats temporal) TOTAL~10014(60%). SHIP(orbit): env7635(!! 84% of floor — orbital starfield+planetHorizon+orbiters) parallax0 world553 lightPoint569 recompute49% TOTAL~9095(55%). KEY FINDINGS: (1) ENVIRONMENT pass is #1 cost + most variable (1.6-7.6ms); occluded-waste underground, near-static surface, expensive-but-slow-deterministic-motion in orbit — needs ADAPTIVE handling (skip-occluded / low-rate-refresh / split-twinkle). renderStars twinkle + moving orbital path = cheap-dynamic over expensive-static. (2) PARALLAX #2 (0-4ms), huge on surfaces, static when camera still. (3) lighting recompute rate tracks scene dynamism (calm56%->machines77%->weather100%) — temporal-decouple defeated by FU machine flicker + weather. (4) static-redundant core (env+parallax+world) = 67-90% of idle floor across locations — the addressable opportunity = retained/cached background layers, adaptive refresh. NEXT: merge with render-arch-understanding workflow (wf_5427cf77, running) → understand-the-space doc → design cycle (approach NOT pre-judged: targeted per-pass levers vs structural retained-layer compositor). Design principle emerging: separate cheap-dynamic from expensive-static per layer, cache the static.
```

<a id="c29c1332-133"></a>

#### #133 — FBO-2 RE-AUDIT DONE: items 2+3 closed (3 fixed in a4106470); item 1 survives as a named hazard -&gt; #197

status: **completed**

- `a4106470` render(L1): a sampler's size uniform is now a function of the texture bound to it
- cited in `docs/board.md`
- cited in `docs/superpowers/specs/2026-07-14-render-surface-subsystem-design.md`

```
RE-AUDITED 2026-07-26 against the CURRENT decomposed renderer, per this task's own instruction not to re-derive from the 2026-07-12 file references. All three unknown items resolved with evidence.

ITEM 4 (the real prize, RetainedSurface) -- was already SHIPPED. Unchanged.

ITEM 3 -- CLOSED, and it had a live residual that is now fixed (a4106470).
The quirk as filed (screen-sized FBO reporting textureSize={0,0}) is gone: GlSurface::size()
(StarGlRenderSurface.cpp:159) returns actual allocated storage and is documented "never (0,0) on a live
surface"; sizeFor() is the single home of the size rule. F1/#142 did cover it.
What survived was a different hole in the same place: textureSizeUniform was written only by whoever
CHANGED a sampler, and nothing wrote it when the TEXTURE changed underneath a sampler that was not
re-bound -- which happens on every renderer config reload, because GlEffects::rebindBorrows re-points
borrowed samplers and CANNOT upload a uniform (it is not the pass and holds no bound program).
Verified LATENT: 5 shipped effects declare a textureSizeUniform, but no shipped effect declares
frameBufferTextures (mod-facing path only), every in-tree sampler with a size uniform is re-bound per use,
and the two lighting targets sampled through one have fixed overrideSize. Fixed by replaying each
sampler's size in GlPass::bindEffect, where the program is already told about screenSize and scriptables.
Byte-identical on hardware (envoracle 89/89, paralloracle 20/20, spreadoracle 206/206, DIFF=0).

ITEM 2 -- CLOSED, shipped before this audit and by name.
Renderer::composite(effect, dstFbo, dstSize, srcSampler, srcFbo, params) exists at StarRenderer.hpp:199,
implemented at StarRenderer_opengl.cpp:844, docstring "Collapses the hand-rolled 'sample one FBO into
another via a passthrough effect + full-screen quad' pattern." SEVEN consumers: GpuLightmapPass:166,255;
BackdropPass:55,58,111,627,650. The "~4 hand-rolled sites" are gone. One deliberate non-user remains --
mergedCompose's backdropCompose draw samples TWO textures (env + parallax) and cannot use a one-sampler
helper. That is a legitimate exception, not residue.

ITEM 1 -- REDUCED, not closed. Split out to #197.
The case that actually bit is solved: composite() sets its params explicitly on every call, so the shared
lightingPassthrough effect is bleed-safe across consumers -- that is what its contract says and what
passthroughParams(bool) delivers. The environmentCompose.config workaround this task cites no longer
exists; #143 replaced it with backdropCompose, which declares ZERO effectParameters.
The HAZARD is structurally intact though: EffectParameter::parameterValue is per-effect and persists for
the effect's lifetime, and nothing resets it at frame start or at bind. The exposure is the `world`
effect: bound at TEN sites (BackdropPass, GpuLightmapPass, WorldPainter, ClientApplication) and declaring
SEVEN parameters. WorldPainter's fullbright branch deliberately leaves lightMapScale / lightMapOffset /
lightmapBilinear / lightmapUpscale stale, relying on the shader ignoring them when lightMapEnabled=false.
Correct today; a live coupling between config, shader and a C++ branch with nothing asserting it.

Related: #144 carries the same defect class for frameBufferTextures across loadConfig.</description>
<parameter name="activeForm">Re-auditing the FBO hardening items
```

<a id="c29c1332-134"></a>

#### #134 — Design the "perfect" env-cache / retained-surface implementation (brainstorm → spec → plan)&lt;/subject&gt; &lt;parameter name="description…

_Stored subject exceeds the heading; reproduced verbatim:_

```
Design the "perfect" env-cache / retained-surface implementation (brainstorm → spec → plan)</subject>
<parameter name="description">After the hasty env-cache probe failed in-game (washed-out at N=1 on ocean, white on ship, red/black under supersampling; reverted to known-good e39c36c5), user wants a PROPER, perfection-grade implementation. Brainstorming checklist: (1) explore context — mostly done (task #132 cost matrix, arch-understanding doc, #133 shared-primitive footguns), GAP = full root-cause of the 3 probe failures is a REQUIRED first step (my applyCap-bleed hypothesis was WRONG — each effect gets its own glCreateProgram, so the washout cause is still unknown; do not guess). (2) clarifying questions one at a time. (3) propose 2-3 approaches. (4) present design. (5) write spec docs/superpowers/specs/. (6) self-review. (7) user review. (8) transition to writing-plans. Key emergent design pillars to weigh: an AUTOMATED bit-identity oracle (dual-run + GPU memcmp, like the CDL validate-oracle / drawable-cache shadowCompare) as the correctness backbone that would have caught the washout offline; hardening the shared render-target/compose primitive first (#133); env cache as a clean consumer designed to extend to parallax/world. HARD GATE: no implementation until design approved.</parameter>
<parameter name="activeForm">Designing the perfect env-cache implementation
```

status: **completed**

```
DONE: brainstorm→spec→plan complete. Root-caused the probe failure (surface-lifecycle, not compose; ship-white = transparent sky + blind alpha=1 replace over non-black envCache; red/black = pre-existing AA×lighting bug). Spec committed kubebound 2f47a6e (specs/2026-07-12-env-cache-design.md); plan committed 3f63085 (plans/2026-07-12-env-cache-plan.md). Design = oracle-first (automated pixel-memcmp as debugging instrument + deploy gate), fix to zero-diff, N-sweep measure, AA-off target + direct fallback. Phase 2 = #133 hardening + parallax/world extensibility. IMPLEMENTATION PENDING = execute the plan (feat/env-cache @ 46b8606 is the WIP start point). Handover: kubebound/2026-07-12-HANDOVER.md.</parameter>
</invoke>
```

<a id="c29c1332-135"></a>

#### #135 — SP-2c: UN-HOLD — P-3 has not landed, so nothing is obsoleted; still NOT_STARTED

status: **pending**

```
STATUS CORRECTED 2026-07-25 by content audit. This was parked as "ON HOLD (likely obsoleted by P-3 #138)".
That premise is false: **P-3 has not landed** (#138 audited NOT_STARTED — parallax is still N separate
per-layer immediate-mode draws), so nothing has obsoleted this and the hold has no basis.

STILL NOT STARTED, and precisely locatable. The full parallax refresh-decision state is
source/rendering/StarBackdropPass.hpp:99-119 — RetainedSurface m_parallaxCache (size+pixelRatio key),
m_parallaxCachePosition, m_parallaxCacheContentKey (tint/alpha hash), m_parallaxStillFrames,
m_parallaxRefreshDeferred, m_lastLoggedParallaxN. The gate body (StarBackdropPass.cpp:266-320) derives N
from CONTENT DRIFT ONLY — layer.speed / dayLength / pixelRatio, capped 1..16, clamped to 4 when animated.
There is no visibility or occlusion term anywhere in it.

SO THE IDEA IS INTACT: refresh less often when the parallax is largely occluded by foreground terrain,
because drift you cannot see does not need redrawing.

DECIDE, DO NOT DRIFT. Three coherent options, and the cost of leaving it parked is that it silently rots:
 (a) Implement it against the existing gate — the insertion point is one more term in the same N
     derivation, so it is small.
 (b) Genuinely close it as WONTFIX on the grounds that P-3 (#138) is intended to delete the whole
     parallax cache including this gate, making the work throwaway. That is a legitimate call, but make
     it explicitly rather than by parking.
 (c) Keep it deferred but re-gate it on #138 REACHING A DESIGN, not on #138 being "likely" to land.

Related: #136 (the P-1 bugs in this same gate, code merged, awaiting the Director's visual check), #138.
```

<a id="c29c1332-136"></a>

#### #136 — P-1: RE-SCOPED to perceptual items only — (b) and (f) are substrate-decided (audit delta[12])

status: **pending**

- `4e95c50c` telemetry: the two compose arms are mutually exclusive -- Cadence::Call, not Frame [#136]

```
STATUS CORRECTED 2026-07-25. This was marked in_progress as though code work remained. It does not: the code is in `integration` and shipping.

RE-SCOPED 2026-07-26 by the M7 axiom audit (A13 Director Intent Amplification, delta[12]). Shipped work was
parked on the Director's eyeballs for two items the substrate already decides -- and the Director is the
one non-scalable resource in this campaign, so that is measurable throughput lost.

VERIFIED PRESENT IN integration (2026-07-25):
  frameBufferGeneration        source/application/StarGlRenderSurface.hpp
  bypassed_moving              source/rendering/StarBackdropPass.cpp
  ParkFrames                   source/rendering/StarBackdropPass.cpp
  render.cache.parallax.*      source/frontend/StarClientCommandProcessor.cpp
It survived the 2026-07-19 branch reorg into the trunk. (The original branch fix/backdrop-cache-bugs
still exists on origin, but its commit 8bf7777 and the binary sha ba54397f no longer resolve -- SHAs
shifted in the history purge. Resolve by message, not by SHA.)

ALSO: the current integration build is DEPLOYED to /home/apnex/OpenStarbound/dev, which is the install the
Director actually plays. So the fixes are live and checkable right now.

=== WHAT ACTUALLY REMAINS ON THE DIRECTOR (perceptual, Claude cannot perform these) ===
  (a) baseline: sky + parallax look normal
  (c) toggle HDR and/or antiAliasing in options -> NO garbage flash in the backdrop (was: composited
      undefined GPU memory for up to N frames after any FBO realloc)
  (d) walk around -> parallax normal (now on the direct/vanilla path while moving)
  (e) walk across a biome boundary -> parallax crossfades smoothly, does not freeze (was: the parallax
      key omitted the tint/alpha content the crossfade animates)

=== CLOSED WITHOUT THE DIRECTOR ===
  (b) ZOOM -> sky updates immediately. CLOSED AS SUBSTRATE-DECIDED: the root cause was the env key omitting
      pixelRatio, and source/test/retained_surface_test.cpp:22-28 now asserts that exact pixelRatio
      invariant OFF-GPU, in core_tests, in CI. A passing unit test asserting the invariant outranks an
      eyeball check of the same invariant.
  (f) /telemetry snapshot x2 for the parked-vs-moving split. CONVERTED, not closed: it becomes the counters
      oracle specified in #174 (refreshed+skipped+bypassed == frame count, bypassed_moving > 0). A
      counter-reading is something the substrate can do; it should never have been on the Director.

ORACLE GAP (still open, feeds P-2 #137): none of the four bugs were catchable by the existing oracles.
Both are DIFFERENTIAL -- reference and cache-under-test share the same draw lambda at the same frame
position under the same ambient GL state -- so they certify only "cache path == direct path GIVEN
identical ambient state". Refresh-key omissions, FBO lifecycle and ambient-state changes all cancel
exactly. An oracle that cannot catch the failure mode is not a gate. P-2 must add (a) a GL-state
assertion pass at end-of-render and (b) a golden full-frame hash, before any code motion.

FREE PERF NOT TAKEN: animated parallax layers always draw frame 0 (StarEnvironmentPainter.cpp:262-263),
so the animation never advances and the parallaxAnimated -> clamp N<=4 guard protects nothing while
costing cache efficiency. Left alone pending P-3 (#138).

DESIGNED TO BE DELETED: if P-3 (#138) lands, the bypass and the whole parallax cache go away.
```

<a id="c29c1332-137"></a>

#### #137 — P-2 DONE: Air-Gap seam closed for BackdropPass, half-closed for WorldPass; residual metered (see #191)

status: **completed**

- `3a30d7d8` docs(board): regenerate -- Air-Gap seam stages A/B/C closed, #191 filed [#137]
- `a0f0089b` render(L3): WorldPass takes a sliced input that declares what it consumes
- `b2cabd8d` render(L3): BackdropPass takes a sliced view -- contract (1), first pass
- `e6edbe11` docs(render): file the axiom audit, so the guardrails commit messages cite actually exist
- `d4b47d7e` render(gate): claim the three unlayered files -- the residual was 17, not 15
- `37306207` render(L3): the lightmap pass derives its own K, and takes its config as one value
- `c2208b71` render(L3): no pass reaches for a global -- WorldPass takes its assets, 17 reads to 15
- `da0125b2` fix(render): commit the lint body and doc that aba06048 CLAIMED and did not contain
- `1e46f71c` render(L3): close the Air-Gap seam for BackdropPass -- 8 singleton reads to 0
- `af9d54a9` render(L2): finish Layer 2 in code -- one content key, one compose parameter block
- `aba06048` render: make the architecture gates actually run, and ratchet the L3 residual

```
CLOSED 2026-07-26 across stages A/B/C, six commits, every one gate-green on `integration`.

WHAT SHIPPED
  252bed68  interface hygiene: renderWorld's consumption declared (4 sinks, not 1); dead StarRoot/
            StarConfiguration includes deleted; the standalone env compose folded from two verbatim
            copies into composeEnvStandalone(); L2's false build claim corrected; RetainedSurface.hpp
            added to star_rendering_HEADERS; the GpuLightmapPass rename STRUCK (Gpu is load-bearing --
            there is a live CPU lightmap path -- so the docs were corrected to the code).
  c2208b71  WorldPass takes its assets handle. 17 -> 15 reads, a REMOVAL not a relocation: WorldPainter
            already held a fresh handle at both call sites. NO L3 PASS REACHES FOR A GLOBAL.
  37306207  the lightmap dispatch prologue split: config -> LightmapParams at the boundary (processFull
            13 params -> 8), the O(cells) auto-K scan INTO the pass as spreadIterationsFor(), the
            cross-path shadowCompare diagnostic left in the orchestrator on purpose. LightmapResult now
            reports the K it chose, so the parity reference cannot derive a different one.
  d4b47d7e  the three unlayered files claimed. Residual 15 -> 17, and nothing got worse: two
            Root::singleton() reads in StarAssetTextureGroup that no layer had ever counted.
  b2cabd8d  BackdropPass::Input -- 2 members of 35, pure reads. StarWorldRenderData.hpp DROPPED: eleven
            transitive headers to two. PROVEN, not asserted: a standalone TU including only
            StarBackdropPass.hpp passes -fsyntax-only with no StarWorldRenderData.hpp in its include set.
  a0f0089b  WorldPass::Input -- non-const refs for the four consumed members, const for the two views,
            passed by value (Input const& would be misleading; constness does not propagate through
            reference members). renderParticles/renderBars sliced the same way.

Also 8eddcb37 (#183) beforehand, which is what made the ratchet able to tell a removal from a relocation.

WHAT IS HONESTLY NOT DONE, carried by #191: WorldPass still includes the fat struct, blocked on
TilePainter (7 signatures, 3 members used) and on EntityDrawables being DEFINED inside
StarWorldRenderData.hpp. Contract (1) is DONE for BackdropPass and HALF-DONE for WorldPass. The clean
per-layer branches still cannot be regenerated from the trunk until #191 lands.

STILL GATED, unchanged: the BackdropPass split, on #174. G9 was narrowed 2026-07-26 (Director-approved,
recorded in docs/render/axiom-alignment-audit.md) from "no L3 boundary change" to "no L3 SPLIT" -- the
narrowing is to G9's own rationale, which is entirely about code motion across paths the motionless
harness cannot execute.
```

<a id="c29c1332-138"></a>

#### #138 — P-3 NOT STARTED — and two feasibility spikes must run BEFORE any parallax shader work is authorised

status: **pending**

```
CONFIRMED NOT_STARTED 2026-07-25 by content audit at c9b2b024, with a sequencing gate added.

NOTHING HAS LANDED. Parallax is still N separate per-layer immediate-mode draws:
source/rendering/StarEnvironmentPainter.cpp:238-249 `renderParallaxLayers(...) { for (auto& layer : layers)
{ ... } }` pushing into m_renderer->immediatePrimitives(); the only per-layer trim is the R-D
fully-transparent skip at :248. No data-driven layer-walking shader exists — the complete effect set is
assets/opensb/rendering/effects/{interface,world,lightingPassthrough,lightingPoint,lightingSpread,
lightingUpscale,backdropCompose}.frag and none samples a layer array. No sampler-budget or atlas work
(`TEXTURE_2D_ARRAY|compiled parallax|single-pass compiled` returns ZERO across source/ and docs/). No
shadow/dual-run pass for it.

CONVERSELY, THE SCAFFOLDING P-3 WAS MEANT TO DELETE IS ALIVE AND STILL BEING EXTENDED: backdropComposeMerge
default true (StarRootLoader.cpp:113), adaptive-N (StarBackdropPass.cpp:279-320), the parallax retained
cache and the cross-surface arbiter (StarBackdropPass.hpp:99-119). So "P-3 will delete this anyway" is not
currently a reason to skip work elsewhere — it is why #135 has been un-parked.

=== GATE: RUN TWO FEASIBILITY SPIKES FIRST ===
Do NOT authorise parallax shader work until both are answered, because either can kill the design:
 1. ATLAS BLEED UNDER NEAREST filtering. A single-pass layer-walking shader needs the layer textures
    reachable from one draw — atlas or 2D array. Parallax tiles wrap and repeat; prove that atlasing does
    not bleed neighbours at tile seams under the sampling this renderer actually uses.
 2. TRUE EFFECTIVE OVERDRAW FACTOR, measured on the offscreen microbench — not assumed. The whole premise
    is that one pass beats N passes; if the layers are mostly transparent and already early-out (the R-D
    skip at :248 exists precisely because they are), the win may be far smaller than the layer count
    suggests.

Note the campaign's precedent: #129 (L2 VAO-format bake) was built, MEASURED NULL, and cleanly reverted.
A spike that says "do not build this" is a success, not a waste.

SEQUENCING: behind #137 (the Air-Gap contract — this changes BackdropPass substantially and should not
land on top of an unfinished extraction seam) and behind #139's Phase 1(b) GL-state assertion pass, which
is the only oracle that can catch the ambient-state class of bug this work will generate.
```

<a id="c29c1332-139"></a>

#### #139 — P-4 PHASE 1 DONE: GL-state assertion pass shipped + gate-read (f02a69f5); phases 2-4 (depth) split to #198

status: **completed**

- `f02a69f5` render(L1): the GL-state assertion pass -- the gate the pixel oracles cannot be

```
PHASE 1 IS NOW COMPLETE, all three parts:
 (a) absolute golden full-frame hash -- already existed, shipped as P-0 (#140)
 (c) trustworthy cost instrument -- already existed (#166 telemetry model + PASS_MASK ablation)
 (b) GL-STATE ASSERTION PASS -- BUILT 2026-07-26, f02a69f5.

WHAT SHIPPED. GlPass::auditGlState + OpenGlRenderer::auditGlState, called unconditionally from
finishFrame(). Compares BELIEF vs REALITY rather than absolute end-of-frame values: draw framebuffer,
viewport extent and current program against the pass's bind cache; scissor enable against m_scissorRect;
blending-enabled against an expectation (setBlendMode records nothing, so there is no belief to compare).
The belief-vs-reality framing is the important part -- GlPass is a cache whose binds EARLY-OUT, so a
disagreement does not merely mis-report, it skips the bind that would have fixed it. RB-1, RB-4, RB-7 and
the resetToScreen hazard were all that desync.

FINDING FROM ITS OWN FIRST RUN, kept in the code: after invalidate(), a null m_target means "no belief",
not "believes screen" -- only the {0,0} viewport sentinel separates them. That made the sentinel part of
GlPass's INTERFACE, now named holdsBelief(). Nine boot-frame false positives, zero defects; the renderer's
cache tells the truth on every rendering frame.

PROVEN BOTH WAYS on hardware, and the injected run is the argument for the whole task:
  clean    -- 0 desyncs, GATE: PASS, exit 0
  injected -- 9 desyncs, GATE: FAIL, exit 1, while envoracle 101/101, paralloracle 20/20 and spreadoracle
              227/227 ALL reported DIFF=0.
A real ambient-state desync that every pixel oracle called perfect. STAR_RENDERTEST_GLSTATE_DESYNC=1 is
the knob. render-gate.sh has a `=== gl state ===` block that fails on nonzero (G6).

UNBLOCKS: this was the oracle #136 named as required before ANY further render code motion, and the
sequencing gate #138 sits behind. #197 (effect-parameter statefulness) was considered for folding in and
deliberately NOT folded -- see the note there; its predicate is not crisp and it would have shipped with an
allowlist for its only in-tree case.

PHASES 2-4 (the depth-buffer architecture) are untouched and unrelated to Phase 1; split to #198 so this
task closes on what it actually delivered rather than staying open on a different project.</description>
<parameter name="activeForm">Building the GL-state assertion pass
```

<a id="c29c1332-140"></a>

#### #140 — P-0 DONE: headless render harness — built, and exercised hard all through #166/#168

status: **completed**

```
CLOSED 2026-07-25 by board audit. Marked pending long after the capability shipped and became the backbone of the campaign.

THE CAPABILITY EXISTS AND IS IN DAILY USE:
  scripts/render-gate.sh        frozen-world byte-identity gate; 3 pixel oracles (envoracle, paralloracle,
                                spreadoracle) + GL error check. Guards every byte-identical commit.
  scripts/render-profile.sh     live offscreen profile on the real GPU, sim running, unattended. --warp
                                pins a location, --set flips config for an A/B.
  scripts/telemetry-window.py   schema-v2 windowing consumer: owner budgets, coverage scaling, percentiles,
                                and the closure oracle.

EVIDENCE OF USE, 2026-07-25 alone: the gate certified 8 separate byte-identical commits across #168 (3/3
oracles, DIFF=0, SKIPPED=0 every time, with real nonzero ran counts). render-profile.sh produced the
baseline, the interleaved 4-run lever A/B, the lightingGpu=false closure check, and the deep-tracing
on/off comparison -- all unattended, all on the real GPU. It is the reason #168 could establish a 16.9%
win with a stated noise floor instead of a plausible-sounding number.

IMPROVED 2026-07-25 (c9b2b024): --warp is now validated against a bookmark cache BEFORE booting a world
(a typo used to cost a full load and report as a crash), ambiguous substrings warn, every run's log is
archived under its label, and the measured location is printed (or 'NOT PINNED' when it is not).

RESIDUAL, filed elsewhere: the ORACLE GAP noted in #136 -- the pixel oracles are differential and cannot
catch refresh-key omissions, FBO lifecycle bugs or ambient GL-state changes, because reference and
cache-under-test cancel those exactly. A GL-state assertion pass and a golden full-frame hash belong to
#137, not here.
```

<a id="c29c1332-141"></a>

#### #141 — P-5: THE TRUNK — half the GPU frame is unattributed; instrument it before choosing any more levers

status: **completed**

- cited in `docs/superpowers/specs/2026-07-25-unified-telemetry-model-design.md`

```
ANSWERED 2026-07-13. Instrument built + measured. dev/perf-integration @ e17f9b5c.

THE INSTRUMENT: whole-frame GPU span via GL_TIMESTAMP (which, unlike the per-pass GL_TIME_ELAPSED timers, CAN coexist with them -- the per-pass ones cannot nest, so they could never report the frame TOTAL and therefore could never reveal what they FAIL to account for). Plus STAR_NO_PERPASS_GPU_TIMERS=1 to suppress the per-pass queries and make the instrument's OWN cost measurable rather than modelled. Measured in the headless harness ([[render-harness]]) on a FROZEN SHIP SCENE == the Director's exact idle-at-ship complaint, vsync off, real Intel Arc GPU, ~1600 frames/run, 3 interleaved pairs.

=== FINDING 1: THE "3.4x OVER-REPORT" IS REFUTED ===
The per-pass timers do NOT inflate by 3.4x. They account for 83% of the instrumented frame (8,193us of 9,895us). Every magnitude quoted in this campaign was being divided by a constant that direct measurement does not support. RETIRE THAT CONSTANT.

=== FINDING 2: NO MISSING HALF-FRAME. THE AUDIT'S "WORLD B" DOES NOT EXIST ===
TRUE frame GPU cost (timers OFF): 11,658 / 11,784 / 11,832 us => ~11.7ms.
Against a 16.7ms vsync budget that is ~70% busy; the Director reports 50-60% at idle (vsync-on clocks lower). It RECONCILES. There is no unattributed half-frame.

=== FINDING 3: THE INSTRUMENT MAKES THE FRAME FASTER BY ~2.2ms (19%) ===
Reproducible across 3 interleaved pairs (timers ON 9,210/9,474/9,937 vs OFF 11,658/11,784/11,832). Counter-intuitive; NOT YET EXPLAINED. Likely mechanism: each GL_TIME_ELAPSED bracket calls flushImmediatePrimitives(), re-partitioning the frame's GPU submissions. CONSEQUENCE: per-pass numbers are measured on a PERTURBED frame -- indicative, not exact. Do not quote them to three digits. NEEDS ROOT-CAUSE (see #142).

=== FINDING 4: THE WORLD PASS DOMINATES, AND THE CAMPAIGN HAS BEEN OPTIMIZING ELSEWHERE ===
Per-frame GPU cost, frozen ship (lighting timers correctly weighted by fires/frames -- they count RECOMPUTES, not frames; conflating the two made the first pass of this analysis nonsense):

    world (tiles + entities)       3,973us   40%   <-- BIGGEST. NEVER TARGETED.
    environment (sky)              1,708us   17%
    unattributed (ui+clear+blit)   1,702us   17%   <-- never instrumented until now
    lighting.point                 1,180us   12%
    environment.compose              558us    6%
    lighting.spread + upscale        747us    8%
    parallax                             0us    0%   <-- ZERO on the ship (no parallax layers)

THE UNCOMFORTABLE CONSEQUENCE: the parallax retained cache, the moving-camera bypass, adaptive-N, the compose merge and the entire single-pass-parallax investigation all targeted a pass that costs EXACTLY ZERO in the Director's stated complaint scenario. The env cache (the one lever that did target a real cost here) remains sound.

REDIRECTS:
- #139 (depth prepass): currently aimed at the BACKDROP. It should be aimed at the WORLD pass, whose BACKGROUND tiles are precisely what FOREGROUND tiles occlude -- and the engine already computes that occlusion (StarTilePainter.cpp:179-182, occludesBehind), though the flag is a LIAR and must be backed by real texel-alpha coverage (vanilla's own lightblocker.material declares occludesBelow:true on a 100%-transparent block).
- The 1,702us unattributed block (interface + clears + blit) is now the #3 cost and has never been looked at. On a STATIC scene the interface is redrawn from scratch every frame.

CAVEAT: measured at the SHIP. A surface world (42 parallax layers, dense terrain) will rank differently -- parallax and world both rise. Re-run the same measurement at the Director's BASE before finalizing the lever order.</parameter>
</invoke>
```

<a id="c29c1332-142"></a>

#### #142 — FBO-1: FBO subsystem hardening — honour explicit size, gate oracle surfaces, diagnosable failures

status: **completed** · blocks: #143

```
DONE 2026-07-14. Two commits on dev/perf-integration, deployed to dev + dev-feat (binary md5 df159908).

cb1332ce — ROOT CAUSE of the intermittent "OpenGL framebuffer is not complete!" startup crash:
OpenGlRenderer::m_hdrSetting was declared with NO initializer (StarRenderer_opengl.hpp). Its only writer,
setMainHDR, is called from ClientApplication::render() -- AFTER renderInit's first loadConfig, which injects it
into every framebuffer's config as "hdrSetting". FBOs with "hdr":"Enabled" ignore it (why lightingGpuB ALWAYS
worked); "hdr":"FromSetting" ones pass the garbage byte through verbatim (settingModeValue returns it as-is), and
clang lowers `hdr ? GL_FLOAT : GL_UNSIGNED_BYTE` to `GL_UNSIGNED_BYTE + 5*hdr` -- so a garbage byte of 116 yields
0x1645, NOT a GL type enum. glTexImage2D fails GL_INVALID_ENUM, allocates nothing, FBO reports incomplete.
Intermittent because the garbage varied per run. LATENT UPSTREAM (vanilla declares "main" as FromSetting); our
GPU-lighting + cache work took it from 1 roll of the dice to 5. Reproduced deterministically in the headless
harness. Cleanly upstreamable.

Same commit, independent defects found en route:
 - setScreenSize reallocated EVERY framebuffer to screen res, ignoring the config's explicit "size" (the ctor read
   it into a local and discarded it). Now GlFrameBuffer::fixedSize + setScreenSize skips them. Verified every
   consumer (StarGpuLightmapPass.cpp:62,81,135,154) passes an explicit size -> end state unchanged.
 - envRef/parallaxRef are oracle-only surfaces (oracles default off) but were allocated unconditionally as
   screen-sized HDR targets. Now "devOnly":true + Renderer::setOracleSurfaces, armed from ClientApplication::render
   (loadConfig rebuilds the FBO set; it must NEVER run mid-frame -- I wrote that bug and caught it pre-build).
 - GlFrameBuffer failures named nothing. Now: drain the (sticky) GL error queue first, probe after each GL call,
   report WHICH call failed with name/size/format/status. This is what actually found the bug.
Net at 1440p+HDR: 8 FBOs -> 6, ~214MB -> ~117MB.

392377d7 — pass-ablation harness (surfaced by 4-lens adversarial review, 16 findings / 14 refuted / 2 confirmed):
 - strtol(mask, 10) parsed base-10 while the default is written 0xF -> EVERY hex input parsed to 0 = ablate ALL
   passes, the silent inverse of intent. Now base 0.
 - bits 2 (world) and 3 (lighting) were DOCUMENTED but never implemented (ablateWorld computed then (void)'d);
   setting them measured an un-ablated frame and would report those passes as free. Removed from code and comment.
 - An ablated run now warns once, so it cannot be mistaken for a normal measurement.
This mattered because ablation is the gate for #143 (the per-pass GL timers are not additive and cannot budget).

VERIFICATION (headless harness, offscreen, real Arc GPU):
 - clean startup, exit 0; 6 framebuffers normally, 8 when an oracle is armed
 - env oracle:      60 MATCH / 0 DIFF  (camera pinned)
 - parallax oracle: 60 EXACT / 0 DIFF  (camera pinned)
 - ablation: default/0x3 -> no ablation; 0x2 -> env ablated; 0x1 -> parallax ablated (luminance 0.2470)
NOTE: cross-run pixel-hash A/B is NOT usable at a populated base -- the unpaused LOAD phase lets the sim diverge
(entities=213/215/216 across runs). Use the in-frame oracles.

REMAINING: user confirms the crash is gone in live play (it was intermittent, so absence over time is the only proof).</parameter>
</invoke>
```

<a id="c29c1332-143"></a>

#### #143 — CM-1: merge the env + parallax composes into one full-screen pass (MEASURED: ~2ms/frame)

status: **completed** · blocked by: #142

```
DONE 2026-07-20. CM-1 shipped on render/decomposition (752bc251 effect + e214ce45 merge + 235f0fd2 hardening fold). backdropCompose two-texture effect merges env+parallax into one full-screen pass when both caches active (default-ON backdropComposeMerge); oracle-gated (env MATCH/0, parallax DIFF=0, spread MATCH/0) + read-only 4-lens adversarial verify (parallax oracle confirmed non-circular; 5 robustness/doc folds). MEASURED via harness A/B on the REAL Arc GPU (Arrow Lake): ~303 us/frame (~0.3ms) whole-frame GPU saving — NOT the disputed ~2ms (inflated non-additive timer sum). In-game confirmation deferred (director will test another time). R2 dropped (→#161), R4=GlSurface done. Arc CLOSED.
```

<a id="c29c1332-144"></a>

#### #144 — UM-1: upstream merge landed — remaining audit findings (27 confirmed)

status: **pending**

```
DONE 2026-07-14: dev/upstream-merge @ 854b4b7f. Merged 83 upstream commits (fc3d55e..2c7f972b) + shader unification.
Deployed to dev-feat (f5f8b76c); dev left on the pre-merge binary (3b01c4ca) as the fallback. Safety tag: pre-upstream-merge.
Gates: build clean, core_tests 226/226, env oracle 60 MATCH/0 DIFF, parallax oracle 60 EXACT/0 DIFF, zero shader errors.

CONFIRMED FINDINGS NOT YET ACTIONED (from the 5-lens audit, 27 survived adversarial challenge):

CORRECTION TO AN EARLIER CLAIM: upstream's #542 double-buffering does NOT subsume our lightmap ping-pong.
switchEffectConfig early-returns when the effect is already current, so N consecutive draws get ONE swap, not N.
#542 expresses a SINGLE-PASS feedback shader; our Jacobi spread loop needs N-ITERATION ping-pong
(StarGpuLightmapPass.cpp:34 `targets[2] = {"lightingGpu","lightingGpuB"}`). Ours is strictly more expressive. KEEP IT.

HIGH VALUE:
 - SYNTHESIS (the real prize, moderate): build ONE swap-aware name->face resolver that serves BOTH the declarative
   Effect::doubleBuffered path AND our imperative setRenderTarget/setEffectTextureFromTarget/readFrameBuffer.
   Today a "double" FBO is invisible to our imperative API. BLOCKER: setRenderTarget reallocs `texture` but never
   `altTexture` -- the moment lightingGpu gains "double":true they desync in size. Symmetric allocation must land
   with it.
 - switchEffectConfig NEVER sets the viewport (latent bug on BOTH sides, moderate). An effect declaring
   frameBuffer + sizeDiv renders into a smaller FBO with a stale full-screen viewport. Our consumers are immune
   only by accident (setRenderTarget always overrides and DOES set the viewport). Blocks #542 on non-screen-sized
   surfaces.
 - Effect framebuffer-textures go STALE across loadConfig (trivial, latent both sides): Effect::textures[].textureValue
   caches a RefPtr to a GlLoneTexture that loadConfig destroys -- the effect samples an orphaned GL texture.
   Minimal fix: drop the `undefined ||` guard in switchEffectConfig's frameBufferTextures loop so it always re-resolves.

UPSTREAM CONTRIBUTION (beyond the diagnostics PR on upstream/fbo-diagnostics):
 - OUR per-FBO `multisampled` opt-in is a PREREQUISITE for upstream's own #542 to work at all. Upstream forces
   m_multiSampling onto EVERY framebuffer; a GL_TEXTURE_2D_MULTISAMPLE bound to a plain sampler2D reads ZERO. They
   never notice because they ship only "main" (blit-resolved, never sampled) -- but #542 exists precisely to let an
   effect SAMPLE an FBO it writes. Turn AA on + add a "double" FBO upstream and it reads black. This is the bug that
   made OUR entire world render black. Worth a PR.
 - Upstream ships ZERO "double" framebuffers and zero frameBufferTextures consumers -- #542 is a mod-facing API with
   no in-tree exercise, untested by their own render path. (Hence its defects: leaked altId, dead blitted, no viewport.)

OUR OWN DEBT (audit was asked to judge us honestly):
 - NoVboOrphan is a mutable file-scope global written by an instance method, not thread-safe, and per our own
   "default-on or remove" rule should be made unconditional and the A/B apparatus DELETED.
 - setEffectTextureHalfRGB / setEffectTextureR8 are 45 lines of near-identical copy-paste differing only in
   (internalFormat, format, type). Unify at uploadTextureImage, carrying the #127 same-size TexSubImage fast path
   down to every caller.
 - Shader-compile fallback is DEAD CODE: the catch block passes raw GLSL source where a map KEY is expected, so a
   compile failure yields an empty program, not the default shader.

PROCESS TRAP (worth a script): the harness reads assets from the REPO (harness/sbinit.config points opensb at
assets/opensb) while the live game reads ONLY dev/opensb + dev-feat/opensb. So the harness and the live game can run
DIFFERENT SHADERS and the harness is the one that's right. A repo-only asset edit is a silent no-op in game. Want
scripts/deploy-install.sh <install> --verify as the single sanctioned repo->install path.</description>
<parameter name="activeForm">Tracking upstream merge follow-ups
```

<a id="c29c1332-145"></a>

#### #145 — RS-0 DONE: the design gate passed 2026-07-14; spec approved, committed, and now implemented by L1/L2

status: **completed**

- cited in `docs/render/layer1-vs-vanilla-assessment.md`

```
CLOSED 2026-07-25 by content audit. This task's ONLY deliverable was an approved design, and it exists.

THE DELIVERABLE: docs/superpowers/specs/2026-07-14-render-surface-subsystem-design.md — 269 lines, opens
"Status: approved (director, 2026-07-14). Supersedes: task #133, #137 (sovereignty extraction), and the
ad-hoc half of #143". Carries the three-layer architecture (Surface / RetainedSurface / passes), the
rejected-alternatives section ("What this is NOT" — not a Jacobi replacement, not a render graph), the
axiom mapping, per-layer verification gates and the sequencing. The brainstorm checklist is fully
discharged: items 4/5/6 in the spec, item 7 (self-review) visible as the folded-in "Findings from the
fork-side Layer-1 hardening (2026-07-18)" section, item 9 (transition to writing-plans) as
docs/superpowers/plans/2026-07-19-compose-merge-and-rs0-finish.md.

NOT OBSOLETE — the opposite. I had it listed as "possibly superseded by the decomposition"; that is
backwards. L1/L2 IMPLEMENT this spec. The Layer-1 Surface it designed is built and hardened:
GlSurface (source/application/StarGlRenderSurface.hpp:91) with the RAII Face struct, writeFace()/readFace(),
swap()/writingBack()/doubled() and the size oracle, plus GlTargets, GlPass, GlEffects and GlLoneTexture;
canonical record docs/render/layer1-architecture.md; standalone tests source/test/render_surface_test.cpp
(9 TESTs, render_surface_tests target) and source/test/retained_surface_test.cpp (9 TESTs).

RESIDUAL, NOT part of this task: the unshipped RS-0 IMPLEMENTATION items — R2 (lightingGpu onto a doubled
surface + swap()) and the remaining upstream #542 defects. If those are wanted, open them as their own
task; do not reopen the design gate.
```

<a id="c29c1332-146"></a>

#### #146 — GT-1: ItemTest.ItemComparison — exactMatch compares post-buildscript vs pre-buildscript parameters

status: **completed**

- `abf6e2eb` test: game_tests 91/91 — fix the 3 pre-existing failures [#146]

```
DONE 2026-07-20 (abf6e2eb). All 3 game_tests failures fixed; scripts/game-tests.sh -> 91/91 PASSED (22s). Two root causes: (1) ItemComparison — pre-existing UPSTREAM failure (perfectlygenericitem ObjectItem injects scriptStorage:{} via retainObjectParametersInItem:true; raw JsonObject== exactMatch rejects it vs pre-construction descriptors). Fixed with a clean() helper in item_test.cpp that strips scriptStorage. (2) ConstructItems + RootTest.All — game_tests fails on any error-level log; ~40 broken third-party workshop mods in sbinit.config. Fixed via scripts/game-tests.sh running against a clean asset set (base + opensb) using harness/sbinit-test.config (local, gitignored like sbinit.config). Render gate keeps the full mod set. Test-only changes; starbound binary + dev deploy unaffected.</parameter>
</invoke>
```

<a id="c29c1332-147"></a>

#### #147 — J-2: obstacle flag in the lightmap's unused alpha (17 -&gt; 9 taps) — BLOCKED on a lighting bit-identity oracle

status: **completed**

```
DONE 2026-07-14. J-1 = 6d2856c2, J-2 = 5d7a09d7. Deployed dev-feat (verified via scripts/deploy-install.sh).

THE LEVER, SHIPPED: each cell's obstacle flag now rides in the ALPHA of emission, and the spread writes it
back out in its own alpha -- so from iteration 1 a neighbour's light AND its obstacle-ness arrive in ONE
lightState tap. The obstacle sampler leaves the hot loop: 17 taps/texel -> 9. Emission uploads as RGBA16F
(packed inside GpuLightmapPass from buffers it already receives; the lighting thread is untouched).

MEASURED, honestly. Clean A/B -- SAME binary, only the `obstacleInAlpha` uniform flipped, median of 3:
    packed alpha  (9 taps):  742us  [717, 760]
    obstacle smpl (17 taps): 900us  [884, 926]
Disjoint ranges, ~33 iterations both. -158us/recompute = -17.6% of the spread (~117us/frame at the observed
0.74 recomputes/frame).

I PREDICTED ~47% FROM THE TAP COUNT AND WAS WRONG. The obstacle texture is R8 and its eight neighbour reads
hit adjacent texels, so the cache was already absorbing most of that cost. TAP COUNTS RANK; THEY DO NOT
BUDGET -- the same lesson as the non-additive GPU timers. (The A/B also disproves driver hoisting of both
texture fetches through the uniform ternary: if it hoisted, the legs would time identically. They do not.)

PRECONDITION (J-1, 6d2856c2): the spread had to move to BlendMode::None first. Under the ambient
BlendMode::Alpha a fragment with alpha=0 blends away to NOTHING rather than being written -- a 0/1 flag in
alpha would have SILENTLY DELETED EVERY AIR CELL (obstacles light, air freezes). J-1 also fixed a real
latent bug: setBlendMode(Alpha) was restored INSIDE `if (!lights.empty())`, so with the spread setting None
outside that block, a lights-empty frame would have left blending disabled for the compose and the whole
world draw.

THE GATE BUILT (reusable capital) -- a LIGHTING BIT-IDENTITY ORACLE. `/lighting spreadoracle on`:
runs the spread BOTH ways in the SAME frame on the SAME inputs, parks the reference leg in a devOnly
"lightingRef" surface, and pixel-compares via compareFrameBuffers. RESULT: 119 MATCH / 0 DIFF.

It was NOT optional. J-2's identity is not provable by argument: setEffectTextureFromTarget binds a
framebuffer's texture object directly and NEVER applies the effect's declared filtering, so `lightState`
samples with lightingGpu's LINEAR filter despite lightingSpread.config declaring "nearest". A linear tap
landing fractionally off a texel centre would interpolate the flag (0.5 between obstacle and air) where the
genuinely-nearest obstacle sampler reads a hard 0/1. The oracle proved the coordinates ARE exact (linear
degenerates to nearest here). Empirical, not assumed.

WHY IT IS CAPITAL: in-frame by construction => immune to the sim divergence that makes cross-run frame
hashes worthless; and unlike lightingGpuShadowCompare (GPU vs CPU, permanently red for an unrelated known
reason -- can detect change, cannot certify identity) it certifies IDENTITY. It is the gate for the
compute-shader Jacobi (33 global round-trips -> ~4-8 via workgroup shared memory), the big remaining bet.

SIDE FIXES: setEffectTextureHalfRGB -> setEffectTextureHalf(..., channels) so RGB/RGBA share ONE upload path
instead of becoming a third near-identical copy. GlLoneTexture gains uploadChannels -- without it a 3->4
channel switch at the same size would TexSubImage RGBA into RGB storage: silent corruption, not an error.

FOLLOW-UP (small): the spread still uploads the R8 obstacle texture even though the packed path never
samples it (the oracle's reference leg needs it). Skipping it when the oracle is off saves ~262KB/recompute,
but leaves a declared-but-unbound sampler -- deliberately not taken for a marginal gain.

NOTED FOR LAYER 1 (#145): filtering is a property of the TEXTURE OBJECT, so one framebuffer cannot be
"nearest" for the spread and "linear" for the bicubic upscale. Per-BINDING filtering belongs in the Surface
design -- and setEffectTextureFromTarget silently ignoring the effect's declared filtering is a real bug the
Surface layer should make unrepresentable.</description>
</invoke>
```

<a id="c29c1332-148"></a>

#### #148 — RI-1: Renderer contract — delete the dead, exile the instrumentation

status: **completed**

```
APPROVED 2026-07-14. Outcome of the 15-agent decomposition panel (workflow wf_074b38e0-821).

VERDICT: split, but smaller than expected. Net-negative LOC.

DO:
1. DELETE setGatedFrameBufferClears end-to-end. VERIFIED DEAD: m_gatedClearsActive written once (StarRenderer_opengl.cpp:1055), read ZERO times; GlFrameBuffer::clearGated (hpp:246) never assigned (ctor never parses the key); assets/opensb/rendering/opengl.config:14 inert. ROOT CAUSE: clearGated is SUBSUMED BY devOnly -- envRef is the only carrier and is devOnly:true, so it is not ALLOCATED unless the oracle is armed; an unallocated FBO is not in m_frameBuffers, so startFrame's clear loop never sees it. Gating the clear of a surface that does not exist is a no-op by construction. Deletion is behaviour-identical. Sites: StarRenderer.hpp:202-205, StarRenderer_opengl.hpp:54,244-246,350-351, StarRenderer_opengl.cpp:1054-1057, StarWorldPainter.cpp:193-198, opengl.config:14.
2. EXILE the 6 instrumentation virtuals into two sealed sub-objects (same pattern as the GlFrameBuffer seal): GpuTimer (begin/end/lastMicros -- 27 per-frame sites, default-ON shipped telemetry, PERMANENT) and RenderOracle (setEnabled/compare/read -- 11 sites, default-OFF dev scaffolding, TEMPORARY). Renderer 43 -> ~33 virtuals. Zero hot-path change, mod contract untouched.

DO NOT (all refuted with evidence):
- The surfaces/effects seam is FALSE. switchEffectConfig (effect op) resolves framebuffers + makeDoubled/swap/bind (cpp:669-682); switchGlFrameBuffer (surface op) writes m_screenSizeUniform, an effect-program uniform (:1985). ONE GL state machine. A split needs mutual friendship = Air-Gap violated by construction.
- DE-VIRTUALIZATION: worth ~0.89ns/call, <0.1% frame. And `final`+LTO devirtualizes nothing (call sites go through Renderer*). WORSE: LTO under -ffast-math (on in EVERY build config, CMakeLists:292-338) could reassociate the knife-edge compare at StarGpuLightmapPass.cpp:72 feeding our bit-identity-certified lit pixels. Would silently un-certify the lighting for 6us. KILLED.
- rendererId() deletion: it is UPSTREAM's contract (origin/main:129). Diverges the fork for a log line. REJECTED (sided with the pragmatist judge over the axioms judge).

MERGE-BURDEN FACT (inverts the naive fear): StarRenderer.hpp = 7 upstream commits / +10-9 since 2024-01. CMakeLists.txt = 58 commits / +840-731. The interface is the CHEAPEST file in the tree to touch; CMakeLists is the expensive one -- exactly where the LTO graft wanted to go.

CERTIFY: core_tests 226/226; env oracle 60 MATCH/0 DIFF; parallax 60 EXACT/0 DIFF; spread oracle MATCH/0 DIFF; game_tests 90/91.

FOLLOW-ON (RI-2): the incentive, not the split, is the real fault. All 20 added virtuals landed in 36 days; commit 82026e914 shipped setVaoBake ("default off, no behavior yet") onto the production contract, reverted 5 days later as "measured null" (73c3ec4d9). Nothing here would have stopped that. Need a GATE: nothing lands on Renderer without a real consumer + a certification. And per A8, once retained-surface identity is certified the oracle is DELETED -- a gate that has passed comes down. The exile makes that a two-file deletion.
```

<a id="c29c1332-149"></a>

#### #149 — ENV-1: RETRACTED — env oracle was run outside its contract (N=4); at N=1 it is 380 MATCH / 0 DIFF

status: **completed**

```
RETRACTED 2026-07-14. NOT A BUG. I filed this against my own gate after running it outside its documented contract.

WHAT I GOT WRONG: I ran the harness at envRefreshInterval=4 and read the resulting 40% DIFF as a pre-existing env-cache fault. StarWorldPainter.cpp:273-275 — a comment I wrote — states plainly:

  "(At N>1 a non-zero diff is EXPECTED on skip frames -- it measures inter-refresh sky
   motion, not a correctness fault; run the gate at N=1.)"

The oracle renders a FRESH drawEnv() into envRef and compares it against "main", which on a skip frame holds the CACHED env (up to N-1 frames old). At N>1 the diff therefore measures temporal staleness — the cache doing exactly what the lever exists to do — not a correctness fault. The 1-MATCH-then-3-DIFF pattern at N=4 is the refresh cadence, not a bug.

THE REAL RESULT (harness, N=1, offscreen, 40 frozen frames, 2560x1371):
  envoracle     380 MATCH / 0 DIFF
  spreadoracle  373 MATCH / 0 DIFF
  paralloracle    1 EXACT / 0 DIFF
  frame capture  40 frames, 1 distinct hash

So the env-cache MECHANISM (redirect draws to envCache, manual clear, drawEnv, composite to main) IS certified bit-identical to a direct env render. The retained-surface foundation is sound; #143/#137 are NOT blocked.

This also strengthens the RI-1 certification (bc7732673): all three oracles are 0 DIFF at the valid operating point.

STANDING RULE (now in [[render-harness]]): the env oracle is only meaningful at envRefreshInterval=1. A gate run outside its stated contract produces a confident, wrong number. Read the oracle's own comment before trusting its verdict.

RESIDUAL (minor, not a fault): paralloracle fires only ONCE in a frozen harness — parallax refresh is gated on camera motion, and the camera is frozen. The parallax oracle is therefore barely exercised by this harness; it is not a strong gate for parallax. Worth a camera-pan mode in the harness if parallax work resumes (#137/#138).
```

<a id="c29c1332-150"></a>

#### #150 — RT-0: PROVEN on hardware — D7 confirmed (60,267 × GL_INVALID_OPERATION; world goes black). Explains #204/#285/#510; NOT #104/#498.

status: **completed**

```
PROVEN 2026-07-14, branch repro/d7 @ ef1ed43cf. Artifacts in docs/upstream/d7-repro/.

THE A/B (headless harness, real Intel Arc GPU, offscreen, antiAliasing ON in BOTH legs, one line differs):
                                 upstream's line    our per-FBO opt-in
  GL_INVALID_OPERATION at bind             60267                     0
  failing samplers          lightMap lightState obstacle          none
  meanLuminance (world)                 0.016682              0.257642   (15x darker = BLACK)

MECHANISM, OBSERVED (not inferred):
  glBindTexture(GL_TEXTURE_2D, <GL_TEXTURE_2D_MULTISAMPLE texture>) = GL_INVALID_OPERATION = A NO-OP.
  The texture unit KEEPS ITS PREVIOUS BINDING. The probe shows want != bound every time:
      uniform=lightMap unit=4 want=7 bound=0/11/13  err=0x502
  The sampler then reads whatever was on that unit -- an atlas page. That IS issue #204's title:
  "Shaders jumpscare you with texture atlas when SSAA is enabled".
  INTERNAL CONTROL, same run: uniform=emission (CPU-uploaded, genuinely GL_TEXTURE_2D) binds cleanly
  (want=10 bound=10 err=0x0). ONLY framebuffer-sourced samplers fail. Defect isolated.

IT IS D7, NOT D5: a stale handle would bind SUCCESSFULLY and sample garbage silently (err=0, bound==want).
We observe err=0x502 and bound!=want -- the bind itself is rejected. Competing hypothesis excluded by the
error code.

AA IS MSAA, NOT SSAA: the menu label "SUPER-SAMPLED AA" is cosmetic. antiAliasing -> setMultiSampling(4) ->
glEnable(GL_MULTISAMPLE) + glMinSampleShading(1.0) (MSAA hardware at supersampling quality). Multisample
textures ARE created. There is no render-at-Nx path anywhere in the tree.

SCOPE (state this honestly upstream): upstream ships ONE framebuffer and ZERO frameBufferTextures, so VANILLA
never samples an FBO and never hits this. It breaks every postprocess SHADER MOD the moment AA is on.

MAPPING:
  PR #510  IS this bug. emmyposs diagnosed it FIRST (2026-04-24), never reviewed (created/updated 8s apart),
           now bit-rotted (patches a switchEffectConfig that #542 rewrote). CREDIT THEM. Do not claim discovery.
  #204     EXPLAINED. HIGH confidence, mechanism exact.
  #285     duplicate of #204 (maintainer's own reply: "Turn off Super-Sampled Anti Aliasing").
  #104     NOT D7. Real cause found: setEffectParameter("vertexRounding", m_multiSampling > 0) -- AA turns on
           vertex rounding; adjacent parallax quads compute their shared edge independently and can round
           APART, opening a 1px gap -> "bright lines in the background". SEPARATE FILABLE BUG.
  #498     NOT D7 (a no-op cannot crash). But a separate, nastier bug lives in it: a PERSISTED graphics
           setting that throws makes the game UNLAUNCHABLE -- no try/catch, no safe-mode fallback, user must
           hand-edit starbound.config. SEPARATE FILABLE BUG.

NEXT: unblocks #151 (the AA gate on our env/parallax caches is now provably a dead workaround -- the caches'
FBOs are multisampled=false and safe to sample under AA).
```

<a id="c29c1332-151"></a>

#### #151 — RT-1: env-cache AA gate DELETED (byte-identical under AA); parallax AA gate KEPT (loses per-sample shading)

status: **completed**

```
DONE 2026-07-14 @ ffd176e2a. Half the hypothesis was right, and the control caught the other half.

MEASURED (frozen world, in-process A/B, refresh 1 = direct vs N = cache):
  env cache        AA off: byte-identical   AA on: byte-identical                  -> GATE DELETED
  parallax cache   AA off: byte-identical   AA on: 781760 px (22.3%) maxAbs 0.000977 -> GATE KEPT

WHY THE PARALLAX GATE IS REAL (I nearly deleted it on a plausible argument):
antiAliasing enables glMinSampleShading(1.f). The DIRECT path shades every parallax fragment ONCE PER SAMPLE
into multisampled "main". The CACHE path rasterizes parallax into a SINGLE-SAMPLE surface and composites it as
one flat quad -- so cached parallax never gets per-sample shading. Sub-perceptual (<1/4 of an 8-bit LSB) but it
is precisely the quality the player asked for. The env cache does not care: the sky is a smooth gradient with
no geometric edges to antialias, which is why it is byte-identical either way -- and why AA players have been
paying for a cache they were never allowed to use.

THE GENERAL LAW (belongs in the RetainedRenderTarget / Layer 2 charter):
  A RETAINED SURFACE CANNOT PRESERVE THE MULTISAMPLE SHADING OF THE CONTENT DRAWN INTO IT.
Every future retained layer (world bands, #143's merged compose) inherits this. Content with geometric edges
cannot be cached under MSAA without a quality loss; content that is a smooth gradient can.

INSTRUMENT GAP FOUND: the env/parallax oracles compare against "main", which IS multisample under AA ->
glReadPixels refuses it -> compare() returns not-comparable -> 380 SKIPPED lines that grep as DIFF=0 and READ
AS A PASS. The frame-hash A/B is the only instrument that works under AA (readFrameBuffer already
blit-resolves). FOLLOW-UP: teach RenderOracle::compare the same blit-resolve trick so the oracles work under AA.

CERTIFIED (AA off, N=1): envoracle 380 MATCH/0 DIFF/0 SKIPPED; paralloracle 1/0/0; spreadoracle 373/0/0;
40 frames, 1 hash. core_tests 226/226. game_tests 90/91 (known-red #146).
```

<a id="c29c1332-152"></a>

#### #152 — F1+F2a+F3a DONE: GlFrameBuffer, GlPass, GlTargets extracted and certified

status: **completed**

```
DONE 2026-07-14. Branch upstream/render-target (eb7b148d2, 4 commits from origin/main); merged to dev @ fbbad028f.

WHAT LANDED (all on a branch cut from origin/main, so it is upstreamable as-is):
  specifyStorage()   THE single owner. Derives format, specifies storage, RECORDS textureSize. Nothing else
                     may do any of those three things.
  allocateFace()     Creation only -- mints GL objects, DELEGATES storage.
  resize()           Re-specifies every live face. Idempotent (zero GL calls if size unchanged).
  size()/sizeFor()   The surface reports its own geometry. Nobody derives it by hand.
  makeDoubled()      Mirrors face 0's RECORDED size instead of re-deriving one.
  clearFaces()       The surface clears its own faces.

MEASURED (code only, comments + atlas path excluded):
                                        origin/main   ours
    format ladder                                 3      1
    HDR derivation                                3      1
    the size rule                                 5      1
    functions specifying face storage             3      1
    RAW REACHES AROUND THE RESOLVER               6      0   <-- the seal is a contract now, not a convention

setScreenSize's 30-line resize is ONE line. startFrame's raw faces[0].id/faces[1].id loop is clearFaces().
The ctor no longer allocates a 256x256 placeholder -- the surface is born correct, so there is no window in
which size() lies. Deleted the FALSE CERTIFICATION comment I had left in the header.

BUGS THIS CLOSES AT THE SOURCE (not patched at the symptom):
  - textureSize never recorded -> mod-visible <name>Size uniform read (0,0) forever
  - a doubled surface could be born with two faces of DIFFERENT sizes
  - two of the five copies of the size rule silently dropped overrideSize

CERTIFIED: envoracle 380 MATCH / 0 DIFF / 0 SKIPPED; paralloracle 1/0/0; spreadoracle 371/0/0; 40 frames,
1 distinct hash. core_tests 226/226. game_tests 90/91 (known-red #146).

NEAR-MISS, and the reason [[upstream-branch-hygiene]] now exists: `git add -A` on upstream/msaa-opt-in (cut
from origin/main, whose .gitignore does NOT have our `harness/` entry) swept 231 files / 13,561 insertions
into the commit -- a 38MB universe.chunks, world files, and the DIRECTOR'S PLAYER SAVE -- on a branch bound
for a PUBLIC repo. Caught before push. Branch rebuilt with explicit paths: 2 files, +18/-2, zero
contamination. Harness restored from the bad commit (it was briefly the only copy).

BRANCH TREE NOW:
  origin/main
   |- upstream/msaa-opt-in (2b67cee1f)  2 files +18/-2  PROVEN. Wave 1. PR drafted, NOT pushed.
   \- upstream/fbo-diagnostics -> upstream/render-surface -> upstream/render-target (eb7b148d2)
  dev/upstream-merge (fbbad028f) <- merges all of the above + fork-only work. All certification runs here.

NEXT: F2a (GlPass pure extraction, bit-identical) then F2b (its 3 behaviour changes, separately gated).
```

<a id="c29c1332-153"></a>

#### #153 — GATE-1: SOLVED, but not the way I planned — in-process A/B of two code paths, not a golden hash

status: **completed**

```
DONE 2026-07-14 @ 8e05b0882. The problem is closed; the solution is NOT the one the task proposed.

WHAT I TRIED FIRST (and it failed): make the load deterministic so the frozen-world frame hash becomes a valid
cross-binary golden. Freeze on QUIESCENCE (entity count stable for N frames) instead of a frame count.
  RESULT: the entity COUNT became perfectly reproducible -- two runs of the same binary both quiesced at
  exactly 67 frames with exactly 211 entities. AND THE FRAME HASH STILL DIFFERED (171c97e1.. vs 6458816a..).
  WHY: identical entity COUNT is not identical entity STATE. Animation phase, positions and liquid motion come
  from the server, which is `class UniverseServer : public Thread` -- free-running -- while the client advances
  on a wall-clock dt. No harness change can fix that; it needs a fixed timestep and a synchronously-ticked
  server, which is an ENGINE change.
  KEPT ANYWAY: the quiescence freeze is a real improvement (reproducible world *count*, and a LOUD error when
  the world never settles, because a hash from an unsettled world is a number that looks like a result).

WHAT ACTUALLY WORKS -- and it is stronger than the golden hash would have been:
  IN-PROCESS A/B OF TWO CODE PATHS. Keep the old path alive behind a config key for exactly as long as the
  refactor takes; hold ONE frozen frame; render it down BOTH paths; compare byte-for-byte.
      STAR_RENDERTEST_AB='passRefactor=false|true'
  Same world, same instant, same everything -> a difference is attributable to the CODE and to nothing else.
  It is the tool that proved the env cache byte-identical (envRefreshInterval=1|4), and it works on a refactor.

  CRUCIALLY it hashes THE WHOLE FRAME -- world pass, tiles, entities, interface -- so it closes the exact hole
  this task was about. The oracles only ever covered env/parallax/lighting.

PROVEN ON F2a.2:
    legA passRefactor = false -> hash=1b0f0923f5677022 meanLuminance=0.257526
    legB passRefactor = true  -> hash=1b0f0923f5677022 meanLuminance=0.257526
    ===== A/B MATCH: byte-identical =====

THE SCAFFOLD: setPassRefactor() is a FREE FUNCTION, not a virtual -- the Renderer contract stays at 38
methods. It and switchGlFrameBufferLegacy are DELETED with the last F2a step. A verification scaffold, not a
shipped flag; that is why it does not violate default-on-or-remove.

STANDING PATTERN for every future pure refactor: keep both paths, A/B in-process, prove MATCH, delete the old.
Do NOT reach for a cross-run frame hash -- it cannot work in this engine, and it will hand you a confident
wrong number. Recorded in [[render-harness]].
```

<a id="c29c1332-154"></a>

#### #154 — F3b DONE: GlEffects extracted; load no longer binds by accident (c1d4d0533, c6ffbdc3d)

status: **completed**

```
The second leaf under GlPass. Give an owner to m_effects, loadEffectConfig, setupGlUniforms, and the parameter / scriptable-parameter surface. Once GlTargets (F3a, c05be20bf) and GlEffects both exist, GlPass::bindEffect can finally take GlEffects& and GlTargets& and depend DOWNWARD — GlPass is the TOP of the dependency DAG, so it is built last.

Certify with scripts/render-gate.sh (all three oracles green at shipped defaults) + core_tests + game_tests (ItemComparison is the known red, #146).</description>
<parameter name="activeForm">Extracting GlEffects
```

<a id="c29c1332-155"></a>

#### #155 — RB-1 DONE (4fcf71033): borrowedFrom guard; proven GREEN on hardware (fullbright probe: target stays 448x320, not 1x1)

status: **completed**

```
THE MOST SERIOUS FINDING of the F3b audit. A hole straight through F1's declared sovereignty.

setEffectTextureFromTarget (cpp:~890) stores a RefPtr COPY of a framebuffer's own colour texture into EffectTexture::textureValue. setEffectTexture's else-branch (cpp:~736-742) then, whenever the sampler already holds a texture, does:
    glBindTexture(GL_TEXTURE_2D, ptr->textureValue->textureId);   // the FBO's colour attachment
    ptr->textureValue->textureSize = image.size;                  // rewrites the FBO's size RECORD
    uploadTextureImage(...);                                      // glTexImage2D -- a full storage RE-SPEC

So it re-specifies that framebuffer's colour attachment (size AND internal format) and overwrites the very field GlFrameBuffer::size() reads. F1's header says of specifyStorage: "Nothing else may do any of those three things." Three effect setters do all three, from outside, via an aliased RefPtr. F1 could not see it because the aliasing is created in the EFFECT code.

REACHABLE IN-TREE: world's lightMap sampler is aliased to a lighting FBO face by StarGpuLightmapPass.cpp:244/248; StarWorldPainter.cpp:312 (fullbright) and :375 (GPU-lighting-off fallback) then call setEffectTexture("lightMap", <Image>) on that same EffectTexture in a later frame -- re-specifying lightingGpuUpscaled/lightingGpu to a 1x1 RGB8 or to the CPU lightmap's dims. It "self-heals" on the next setRenderTarget resize ONLY because size() now reports the corrupted number. Repair by accident.

setEffectTextureHalf and setEffectTextureR8 have the identical write-through.

FIX (behaviour change, own gated commit): either EffectTexture knows the texture is target-owned and the upload setters refuse/replace rather than re-spec, or setEffectTextureFromTarget hands over a non-owning view.</description>
<parameter name="activeForm">Fixing the effect-texture write-through into framebuffer storage
```

<a id="c29c1332-156"></a>

#### #156 — RB-2/RB-3 DONE (691392929): compile+link before replace; shader fallback fixed

status: **completed**

```
GlEffects::load() (was loadEffectConfig, cpp:~462-465) does glDeleteProgram + erase UP FRONT, then throws at the link check (~:516-520) on failure.

Deterministic damage on every link failure: the effect is permanently ABSENT from the registry, its old program is deleted, m_pass.effect is stale (the re-bind is never reached), and switchEffectConfig(name) returns false forever after. That return value is IGNORED by StarClientApplication.cpp:512 ("interface") and :532 ("world") -- so those layers silently draw under whatever effect was last bound. A modder with one bad shader gets a silently mis-rendered game, not an error.

FIX (behaviour change, own gated commit): compile and link into a new program FIRST, replace the registry entry only on success. This -- not a container swap -- is the real fix for the stale m_pass.effect.

RELATED (RB-3, upstream): the compile fallback at cpp:~498-499 passes DefaultVertexShader/DefaultFragmentShader (the raw GLSL SOURCE) to compileShader, whose second parameter is a KEY into the `shaders` map. Both lookups miss, both return 0, nothing is attached: "Shader compile error, using default" compiles NO default at all. It also leaks 2 glCreateShader handles (the create precedes the !source early-return). Correct signature: (GLenum type, char const* label, String const& source). CHECK UPSTREAM FIRST -- inherited, not ours. The pixel oracle CANNOT see this (the catch never runs on stock shaders).</description>
<parameter name="activeForm">Fixing the erase-before-link ordering
```

<a id="c29c1332-157"></a>

#### #157 — RB-4 DONE (a1ec80598): passes early-out fixed; upstream draft filed to docs/upstream (NOT published)

status: **completed**

```
A NINTH upstream #542 defect, found during the F3b audit. Ours to file, not ours to fix (present in upstream/main).

StarClientApplication.cpp:568-576 renders post-process layers:
    for (unsigned i = 0; i < layer.passes; i++)
      for (auto& effect : layer.effects) { renderer->switchEffectConfig(effect); renderer->render(quad); }

switchEffectConfig early-outs at `if (m_pass.effect == &effect) return true;` (upstream/main:610-611) -- which returns BEFORE the doubleBuffered `buf->swap()` (upstream:619+), before the target bind, and before the frameBufferTextures rebind.

So for a layer with a SINGLE effect and passes > 1 -- which is the canonical use of `passes`, iterating a feedback/blur shader -- every pass after the first is a no-op ping-pong: the shader samples the same face it is drawing into (undefined per the GL spec: a texture bound as both sampler and draw target). The iteration does not iterate.

Mod-facing only: OpenSB ships postProcessLayers: [] . But `passes` exists precisely to iterate the feedback shaders it breaks, and it is exposed to Lua via setPostProcessLayerPasses (StarClientApplication.cpp:722).

MINIMAL FIX (for the upstream PR): do not early-out for a double-buffered effect --
    if (m_pass.effect == &effect && !effect.doubleBuffered) return true;
preserving the optimisation for the common case while letting the ping-pong actually iterate.

ACTION: draft as docs/upstream/issues/passes.md alongside the 7 other unfiled drafts. DO NOT FILE without the Director's approval.</description>
<parameter name="activeForm">Drafting the upstream passes/double-buffer issue
```

<a id="c29c1332-158"></a>

#### #158 — RB-5a/b/c DONE (91bed0d2a, f46500f90): AA-toggle orphan samplers + last size-rule copy + dup uniform owner

status: **completed**

```
Two smaller findings from the F3b audit.

(a) ORPHANED TARGET TEXTURES. GlFrameBuffer's destructor only .reset()s its RefPtr to the face texture; the glDeleteTextures lives in GlLoneTexture's destructor. So when GlTargets::destroyAll() clears the registry, any EFFECT SAMPLER still holding that face keeps the GL texture ALIVE and orphaned -- and keeps sampling it.

loadConfig -> destroyAll() runs on setMultiSampling / setMainHDR / oracle().setEnabled (StarClientApplication.cpp:503/504/509) -- and loadConfig does NOT reload effects. So an AA or HDR toggle destroys every target while world.lightMap and lightingSpread.lightState still hold their faces; the world then samples a texture belonging to a destroyed framebuffer until the next lighting recompute rebinds it -- which is temporally gated, so possibly several frames.

The generation() counter exists precisely to announce this (StarRenderer.hpp:188-193) and its only consumer in the tree is StarWorldPainter.cpp:165. The effect-texture path never reads it. switchEffectConfig's repair guard (`undefined = !textureValue || textureId == 0`) is FALSE exactly because the orphan is still alive, so that path never repairs it either.

(b) THE LAST COPY OF F1'S SIZE RULE. switchEffectConfig derives `effectScreenSize = m_screenSize / buf->sizeDiv` by hand -- and DROPS overrideSize. F1 collapsed that rule to GlFrameBuffer::sizeFor(); this is the one site it never reached. Latent in-tree (both sized-target effects are always followed by an explicit setRenderTarget(..., size)); live for a mod effect declaring "frameBuffer" on a sized target. Route it through buf->sizeFor()/size().

Also trivial: the dead `screenSize` effectParameter in assets/opensb/rendering/effects/lightingPassthrough.config and lightingUpscale.config resolves a SECOND location for a uniform GlPass owns and pins Vec2F(1,1) into effect.parameters. Inert (setRenderTarget rewrites the real uniform before every draw) but it should go. Asset-only -- deploy via the loose override.</description>
<parameter name="activeForm">Fixing orphaned target textures and the last size-rule copy
```

<a id="c29c1332-159"></a>

#### #159 — RB-6/RB-7 DONE (b74937721, d05f21682): texture internalFormat record + pass notified of target rebuild

status: **completed**

```
Two further render-hardening commits found already landed on dev/upstream-merge (linear, on top of RB-5). Recorded for completeness.

RB-6 (b74937721): GlLoneTexture now records its internalFormat, and the Half/R8 SubImage fast paths are format-aware -- "a texture that describes half its storage is worse than one that describes none". This is what makes the same-size/different-format corruption shape safe at the record level (belt-and-suspenders behind RB-1's borrowedFrom fix).

RB-7 (d05f21682): "the pass was never told the targets were rebuilt" -- the last seat of the RB-5 orphan defect; rebindBorrows told the samplers but not the pass. +14 lines in StarRenderer_opengl.cpp.

STATE: HEAD e30c21c4d, clean tree. Full RB backlog (RB-1..RB-7) + 3 upstream issue drafts + 2 upstream PR drafts (docs/upstream/, NOT published) + 3 architecture-assessment docs all committed. Verified this session: build clean, core_tests 226/226, game_tests 90/91 (ItemComparison known red #146), render-gate all 3 oracles green + 0 GL errors, RB-1 fullbright probe GREEN.</description>
<parameter name="activeForm">Recording RB-6/RB-7 completion
```

<a id="c29c1332-160"></a>

#### #160 — L1 FINISH (§9): steps 1-6 to bring Layer 1 to the Layer-2/3 bar — Director-approved

status: **completed**

```
DONE. §9 step 6 complete: all 7 residuals shipped byte-identical. 1 (createEmptyGlTexture allocator + hasStorage/ownsWritableStorage predicates + gen throw, b65f67a8), 2 (bind key, earlier step 3), 3 (zero friend; false comment fixed, b65f67a8), 4 (both Json config members deleted; frameBuffer/blitFrameBuffer/ordered frameBufferTextures parsed at load, doubleBuffered derived there, switchEffectConfig zero-JSON, GlFrameBuffer hdr cached at ctor; throw-timing moved to load, c6ffbdc3/c6a3d57c), 5 (framebuffer completeness floor, fe7fe81b), 6 (parseEffectParameter single ladder, 81494710), 7 (attribute/uniform cache deleted, 8b8a2499). Capstone 9512783b. Layer 1 nailed: 9/10 checkable conditions literally green; #6 substantively met (RB-6 co-location invariant closes the bug class; effect side keeps 3 distinct-contract upload paths — image/Half/R8 — that an allocator can't absorb). Doc docs/render/layer1-residuals.md marked COMPLETE. Branch dev/upstream-merge unpushed (hold).
```

<a id="c29c1332-161"></a>

#### #161 — Jacobi improvements NOT STARTED — and #168 showed the lighting CPU budget contains no Jacobi at all

status: **pending**

- cited in `docs/superpowers/specs/2026-07-19-compose-merge-and-rs0-finish-design.md`

```
CONFIRMED NOT_STARTED 2026-07-25 by content audit, and RE-GATED on the basis of #168's measurements.

NOTHING LANDED. None of the four candidates is in the tree: no temporal warm-start (iteration 0 is still
seeded from raw emission, not from the previous frame's converged lightmap), no red-black Gauss-Seidel
(the spread is still Jacobi ping-pong), no residual-based early-exit or adaptive N, no multigrid.
spreadIterations default is still 32 (source/game/StarRootLoader.cpp).

=== WHY THE PREMISE NEEDS RE-EXAMINING BEFORE ANY WORK ===
#168 closed the lighting CPU budget and measured it at 00-Ocean-Lab. Post-lever, per recompute:
   gather ~276 (57%) · export ~77 · convert ~66 · begin ~31 · everything else ~15
**None of that is the Jacobi spread.** The Jacobi work is GPU-side (lighting.gpu.spread.gpu_us), and the
CPU cost that feeds it is dominated by the gather and the export/convert path, not by iteration count.
So "optimise Jacobi" is not automatically a win against anything currently measured as expensive.

=== GATE THIS ON #170 (border multiplier) ===
Every O(cells) lighting phase — and the GPU lightmap texture the spread iterates over — scales with the
CALCULATION region, which is the query region padded by borderCells()=48 per side: measured
lighting.calc.cells 35840 vs lighting.cells 8192, exactly 4.375x. #170 proposes shrinking that. Choosing
a Jacobi lever now means aiming at a target whose size is about to change by up to ~2-4x. Do #170 first,
re-measure, then pick.

IF IT IS REVIVED, the ranking should be re-derived from a GPU capture, not from the original task text.
The original candidate order (temporal warm-start > red-black GS > residual early-exit > multigrid) was
written before the telemetry model existed and before the lighting budget was closed; lighting.gpu.
spread.gpu_us is now directly readable per recompute from any profile, so the choice can be evidence-led.

VALIDATION NOTE that still holds: these change output toward the SAME steady state, so they are validated
by a convergence/quality comparison, NOT byte-identity. Reuse the spreadoracle harness pattern.
```

<a id="c29c1332-162"></a>

#### #162 — CPU-1: explore context — what the frame loop times today, thread ownership, frame-skip

status: **completed**

```
Brainstorm step 1 for per-frame CPU metrics alongside GPU in the live render-profile harness. Establish precisely: which main-loop steps are timed vs untimed (StarMainApplication_sdl.cpp:728-784); scope of the existing render.frame.us (in-world portion only) vs the true frame; thread ownership of every existing timer (main / server / lighting) since tick.server.compute.us = 3199us/frame EXCEEDS the whole GPU span (2952us) and summing across threads reproduces the 119%-of-the-whole bug; how frame-skip (updatesBehind loop) affects per-frame accounting; TelemetryScope cost when deep tracing is off.</description>
<parameter name="activeForm">Exploring frame-loop timing context
```

<a id="c29c1332-163"></a>

#### #163 — CPU-2: clarifying questions + 2-3 approaches (brainstorm)

status: **completed**

```
Brainstorm steps 3-4. One question at a time. Key forks identified so far: (a) purpose — bound VERDICT (is the frame CPU- or GPU-limited) vs full ATTRIBUTION (where did every microsecond go); (b) harness-only vs also-live in the Director's sessions (changes gating, HUD, and the meaning of swap.us: GPU backpressure with vsync off, frame pacing with vsync on); (c) whether the server thread's own budget is in scope. Then propose 2-3 approaches with a recommendation.</description>
<parameter name="activeForm">Asking clarifying questions and proposing approaches
```

<a id="c29c1332-164"></a>

#### #164 — CPU-3: present design + get Director approval

status: **completed**

```
Brainstorm steps 5-6: present the design in sections scaled to complexity (architecture, metric set, thread partitioning, data flow into the snapshot, how the windowing tool presents it, testing). Get approval per section. Then write the spec to docs/superpowers/specs/ and commit. Director does not read spec markdown (see memory surface-decisions-not-doc-review) — surface every director-critical decision INLINE; the spec is for the record.
```

<a id="c29c1332-165"></a>

#### #165 — CPU-4: transition to writing-plans (brainstorm terminal)

status: **completed**

```
Brainstorm terminal state. After the design is approved and the spec committed, invoke the writing-plans skill to produce the implementation plan. HARD GATE: no implementation action before design approval.
```

<a id="c29c1332-166"></a>

#### #166 — CPU-5 DONE: unified telemetry model shipped; one verification step deliberately not run (see #172)

status: **completed**

- `84ca718f` spec: cadence is arithmetic, not just a bounds check [#166]
- `1bfa935d` telemetry: write down the thread-affinity invariant the rusage RMW depends on [#166]
- `5e8f66d3` spec: cpu.frame.finish.us also covers the ImGui render [#166]
- `b3c88551` plan: quote CPU BUSY, not total -- the loop is paced [#166]
- `497e398c` telemetry: comment accuracy at three GPU declare sites [#166]
- `313095f1` plan: 'a declare inside a config-gated branch is not a declaration' [#166]
- `5eb17518` plan: the GPU accounting check had the denominator bug it exists to catch [#166]
- `61531413` plan: the consumer must not treat the top bucket as bounded, nor the checksum as exact [#166]
- `39992d4b` plan: fix undefined behaviour in the histogram bucket function [#166]
- `4797f33d` plan: Task 7's conflict oracle was unfalsifiable — remove it [#166]
- `e2966ad2` plan: fold in review findings + telemetry architecture doc [#166]

```
CLOSED 2026-07-25. Plan: docs/superpowers/plans/2026-07-25-unified-telemetry-model.md (its checkboxes were never ticked -- 0 of 55 -- so they are NOT a status signal; this audit is against the tree, not the boxes).

TASK 9 (end-to-end verification) AUDIT:
  Step 1 build + all suites .......... DONE. core_tests 251/251, game_tests 92/92, render_surface_tests 9/9 (re-run 2026-07-25 17:0x).
  Step 2 render gate ................. DONE. GATE: PASS, 3/3 oracles, DIFF=0, SKIPPED=0, GL_INVALID 0. Re-confirmed on every #168 commit.
  Step 3 deep-tracing-off cost ....... NOT RUN -> deferred to #172. The only genuine gap.
  Step 4 spread A/B on both columns .. DONE. harness/profiles/v2-spread32.json, v2-spread16.json, v2-spread32b.json.
  Step 5 config restored ............. DONE + re-verified at close: telemetryDeepTracing=True, lightingGpuSpreadIterations=32.
  Step 6 architecture document ....... DONE. docs/telemetry/architecture.md, now 419+ lines; sections 3 and 7 extended by #168 with the lighting phase table and four new traps.

THE MODEL IS PROVEN IN USE, which is stronger evidence than the plan's own checklist. #168 drove it hard against live data and it did its job: it CLOSED the lighting budget from 47.6% to 99.6%, and its oracle caught six pre-existing defects including two metrics that were overstating a printed figure by 36% in live output. A model that finds real defects in its own first serious consumer is validated.

WHAT WAS LEARNED AT CLOSE (worth keeping): a capture taken with telemetryDeepTracing=false will ALWAYS trip the closure oracle with 'frame/cpu: 100% unattributed', because deep-gating means no TelemetryScope records and therefore no Budget parts exist. That is correct behaviour, not a bug -- but it means scripts/render-profile.sh exits non-zero on a deep-off run (telemetry-window.py exit 3), so any deep-off comparison must read the emitted JSON directly rather than gate on exit status. Whoever picks up #172 needs this or they will misread the run as broken.

Related work shipped under this arc: #167 (GpuTimer::begin takes the descriptor -- deleted the reachability bug class structurally).
```

<a id="c29c1332-167"></a>

#### #167 — CPU-6: bind the GPU descriptor to the recording call — delete the reachability bug class

status: **completed**

- `a94a7a8c` telemetry: say what the contract actually guarantees, and mark the plan superseded [#167]
- cited in `docs/superpowers/plans/2026-07-25-unified-telemetry-model.md`

```
FROM the Task 4 code-quality review (2026-07-25). Task 4 shipped CORRECT but the MECHANISM is fragile, and it produced the same defect three times in one task (environment.compose dead under backdropComposeMerge's default; parallax.gpu_us dead under parallaxOracle; plus the near-miss the census ruled out).

ROOT CAUSE: the GPU idiom makes declaration a free-floating statement placed in per-frame control flow, structurally divorced from recording. Correctness then depends on a dominance proof at every multi-site key, re-derived by hand, forever — and that proof failed twice. Contrast the CPU idiom in the same tree: Telemetry::counter(key, desc) RETURNS the handle, so the descriptor rides on the object you must hold in order to record. That idiom is immune to this bug class BY CONSTRUCTION.

PREFERRED FIX (reviewer's option 1): give GpuTimer::begin a MetricDesc — or a GpuTimerKey value carrying key+desc, constructed once at namespace scope — and have GlGpuTimer forward it to Telemetry::timer(name, desc).record(...) at StarRenderer_opengl.cpp:994. MetricDesc is a 4-enum POD; cost is nil. Makes the GPU path structurally identical to the CPU path, DELETES the bug class, and removes all 15 declare-blocks (~75 of the 107 lines Task 4 added).

FALLBACK (option 2, cheaper): move each declare to namespace scope in the same TU. registry() is a Meyers singleton so static-init is SIOF-safe, and pendingDescs already handles declare-before-first-sample. Reachability becomes unconditional by construction rather than by argument.

REJECTED: a central declaration table — it is unconditionally reachable and its drift risk is caught loudly by the UNDECLARED oracle, but it separates the descriptor from the pass that owns its meaning, and option 1 gets the same immunity without that cost.

SCOPE NOTE: this touches the GpuTimer contract in StarRenderDiagnostics.hpp, so it is a real change to a Renderer contract, not a tidy-up. Deliberately NOT done inline during plan execution — surfaced for the Director. Do it before the idiom spreads to a 16th pass. Guarded by scripts/render-gate.sh like everything else in this arc.</description>
<parameter name="activeForm">Binding the GPU descriptor to the recording call
```

<a id="c29c1332-168"></a>

#### #168 — CPU-7 DONE: lighting CPU budget closed (99.6% GPU-on / 100.0% GPU-off), then cut 16.9% by four levers

status: **completed**

- `2e9c514e` docs(telemetry): calc.cells is not a scene fingerprint -- lights.sources is [#168]
- `f7521455` docs(telemetry): the lighting owner's phase budget + three new traps [#168]
- `4eb4e1c3` lighting: branchless floatToHalf -- kill the range-branch mispredicts [#168]
- `bf9d0fb4` lighting: iterate the spread-input export in destination order [#168]
- `0c8d5e7c` lighting: close the buffer ring -- explicit freshness flag, swap on consume [#168]
- `419b0f63` lighting: swap the published buffers instead of moving them [#168]
- `73beb625` lighting: make the params-cache validity flag atomic [#168]
- `165f07b3` lighting: cache the calculator parameters instead of recomposing them per recompute [#168]
- `83c16487` test: pin the lighting owner's denominator/total contract [#168]
- `25a7605f` telemetry: nine contiguous phases close the lighting CPU budget [#168]
- `065d462b` telemetry: five conditional phases were declared at a cadence they never fire at [#168]
- `53ad8da0` plan: run test binaries from dist/ -- they resolve sbinit.config from cwd [#168]
- `34ffb7bd` telemetry: publish the cell gauges from begin(), not the skipped calculate() [#168]
- `0571b2c9` plan: fix build paths and the pgrep guard before dispatch [#168]
- `9422b768` plan: lighting CPU budget closure, 10 tasks in 2 stages [#168]
- `582991af` spec: lighting CPU budget closure + levers [#168]
- cited in `docs/board.md`
- cited in `docs/superpowers/plans/2026-07-25-lighting-cpu-budget-closure.md`

```
COMPLETE 2026-07-25, ALL SIX SUCCESS CRITERIA MET. Spec: docs/superpowers/specs/2026-07-25-lighting-cpu-budget-closure-design.md. Plan: docs/superpowers/plans/2026-07-25-lighting-cpu-budget-closure.md.

=== STAGE 1: CLOSURE ===
Owner `lighting` went from 47.6% accounted to 99.6%, zero violations, EVERY part at 100% coverage -- coverage-scale-free end to end.
Commits: 34ffb7bd (cell gauges to begin()), 065d462b (five cadence fixes), 25a7605f (nine contiguous phases), 83c16487 (owner-contract test).
Mechanism: nine CONTIGUOUS, EXHAUSTIVE Budget phases. The pre-gate prologue is a FRAME-cadence part, which is what lets a frame-cadence Total close exactly against recompute-cadence parts without a schema change (two Totals per owner are not representable). export/convert/calculate WRAP their `if` rather than sitting inside it.

=== STAGE 2: LEVERS (byte-identical, gate PASS per commit) ===
165f07b3 + 73beb625  L0  params cache (+ atomic validity flag -- it races initWorld on the main thread)
419b0f63 + 0c8d5e7c  L1  buffer ring: explicit freshness flag, swap on consume instead of move
bf9d0fb4             L2  export iterates in destination order (untranspose)
4eb4e1c3             L3a branchless floatToHalf

=== VALIDATED A/B (aba-pre1/post1/pre2/post2, 90s, --warp 00-Ocean-Lab, interleaved, same session) ===
Scene identical: lights/rec 32.0 exactly, calc.cells 35840. Closure 99.5-99.7%, zero violations, every run.
  export   123.9/125.1 -> 71.9/82.3   = -47.4 us  ESTABLISHED
  convert  104.2/106.0 -> 62.9/68.4   = -39.5 us  ESTABLISHED
  params     9.6/8.1   ->  1.2/1.1    =  -7.7 us  ESTABLISHED
  gather   277.8/277.7 -> 264.4/288.5 =  -1.3 us  in noise (untouched -- the control)
  TOTAL    559.7 -> 465.4 us/recompute = -94.3 us, -16.9%
Every phase touched moved; every phase untouched stayed in noise. Pre-run noise 0.1-3.7 us.
NOT separable: export's -47.4 is L1+L2, convert's -39.5 is L1+L3a.

=== SUCCESS CRITERIA: 6 of 6 MET ===
 1 closure >=97% .................. MET (99.5-99.7%)
 2 every part 100% coverage ....... MET
 3 closes under BOTH lightingGpu .. **MET 2026-07-25** (crit3-gpuoff, 60s, --warp 00-Ocean-Lab,
     lightingGpu=false): 100.0% CLOSED, ZERO violations, calc.ran=1737 / calc.skipped=0. The profile
     inverts exactly as the wrap-the-if design predicted: calculate carries 3817.1 us (was 0.1 with GPU
     on) while export -> 0.0 and convert -> 0.2 (were ~77 and ~66). Conditional phases record ~0 rather
     than being coverage-scaled up. Note CPU lighting costs 4177 us/recompute vs 465 with GPU on -- 9x.
 4 one scaling law per phase ...... MET (calc.cells 35840, lights.sources 32.0, both live)
 5 levers byte-identical + A-B-A .. MET
 6 budget re-closes after levers .. MET

=== SIX PRE-EXISTING DEFECTS FIXED ===
 1. lighting.cells set inside the SKIPPED calculate() and measuring the QUERY region -> stale AND wrong by 4.375x; it was the denominator for every per-cell claim.
 2-4. spread/point/post.us declared Recompute but conditional -> one self-healing CPU frame would inflate them ~1100x.
 5. lighting.gpu.cpu_cost.us declared Frame, fires per lightmap update -> scaled UP 1.36x. Printed 458 us/frame against an actual 336. WRONG IN LIVE OUTPUT.
 6. lighting.upload.us same.

=== THREE OF MY OWN ERRORS, CAUGHT BEFORE THEY SHIPPED ===
 - L1 as first specified was INERT: waitForLighting also moved the buffers out, so 99.4% of recomputes still zero-filled ~788KB. Caught by instrumenting, not assuming. The naive follow-up would have BROKEN the freshness protocol -- !empty() WAS the consume-once signal.
 - The "branches stopped vectorisation" rationale for L3a was FALSE. Disassembly shows the loop still does not vectorise; the win is branch-misprediction removal. The comment states the true mechanism.
 - The first A/B was INVALID: the harness player's position persists between runs, so the 'after' captures measured a 30-lights scene against an 83-lights baseline. gather fell 73% on untouched code -- that is the tell.

=== KEY FINDING FOR #161 ===
Post-lever: gather 264-288 (~57%), export ~77, convert ~66, begin ~31. NONE is the Jacobi spread. Gate #161 on #170 (border multiplier), which moves every O(cells) phase at once.

DEPLOYED: c9b2b024 -> /home/apnex/OpenStarbound/dev (the install the Director plays), verified binary + 222 assets match the repo. Backup at dev/starbound.bak-pre168-lighting.
Follow-ups: #169 (F16C, needs sign-off), #170 (border multiplier, gates #161), #171 (producer-side lighting CPU).
Harness: c9b2b024 (--warp pre-flight + ambiguity warning + per-run log archiving + location pinning).
Docs: docs/telemetry/architecture.md sections 3 and 7.
```

<a id="c29c1332-169"></a>

#### #169 — L3b: F16C vcvtps2ph for the fp16 emission convert — needs Director sign-off (output changes)

status: **pending**

```
DEFERRED FROM #168 Stage 2. lighting.cpu.convert.us is 65.7 us/recompute after L3a (branchless scalar). An F16C `vcvtps2ph` rewrite converts 8 floats per instruction and is estimated at ~5 us -- a 10-30x cut on the largest remaining compute-bound lighting phase.

IT IS NOT BYTE-IDENTICAL, which is why it was not shipped with the rest:
  - vcvtps2ph rounds half-to-EVEN; floatToHalf (StarWorldClient.cpp:32) rounds half AWAY FROM ZERO.
  - vcvtps2ph emits proper subnormals; floatToHalf flushes them to signed zero.

MEASURED divergence against a true IEEE half conversion over all 2^32 float bit patterns: 1 in ~16,384 across the non-negative normal-half range this emission grid occupies, and 1 in ~23 across all finite floats (dominated by the subnormal-flush and overflow-clamp regions). So on real lighting data expect ~1-ULP differences on a handful of texels per recompute -- but the render gate's spreadoracle WILL report DIFF, correctly.

Shipping it therefore means replacing byte-identity with a QUALITY argument, which is a different verification contract and needs the Director to agree to it explicitly. Do not slip it in as an optimisation.

ALSO REQUIRED, independent of the rounding question: the tree contains ZERO SIMD and the build has no -march (CMakeLists.txt:295-296 is -O3 -ffast-math only), so this needs __attribute__((target("avx2"))) + __builtin_cpu_supports dispatch + an MSVC path. This codebase has already taken one MSVC portability incident over __builtin_clzll. Budget for the dispatch layer, not just the intrinsic.

Evidence: docs/superpowers/specs/2026-07-25-lighting-cpu-budget-closure-design.md section 8 L3b.
```

<a id="c29c1332-170"></a>

#### #170 — L4 DONE: adaptive calculation border shipped — 4.375x -&gt; 3.000x, -23.6% lighting CPU (03cec1c0)

status: **completed**

- `03cec1c0` lighting: adaptive calculation border -- 4.375x -&gt; 3.000x, -23.6% lighting CPU
- `a9854185` lighting: measure how much of the 48-tile border is actually used (#170 probe)
- cited in `docs/board.md`

```
SHIPPED 2026-07-25, commits a9854185 (probe) + 03cec1c0 (lever). Default ON via
`lightingAdaptiveBorder` kill-switch (StarWorldClient.cpp:2154-2156).

Direction 1 landed: the border is now computed per-recompute and clamped to
[spreadBorderCells(), borderCells()], so it can only ever SHRINK from the old fixed 48. Measured
4.375x -> 3.000x calc/visible cells, -23.6% lighting CPU, and PIXEL-IDENTICAL -- the feared quality
change did not materialise because the trimmed border carried no in-travelling light at the measured
locations.

RESIDUAL, not a blocker: 3.000x is still 3x the visible region. Direction 2 from the original scoping --
export only the sub-region the GPU world shader actually samples, rather than the full calc region -- is
untouched and is the remaining structural cut. It needs the quality comparison, not byte-identity.

#161 (Jacobi lightmap-spread) WAS GATED ON THIS AND IS NOW UNGATED: the region has stopped moving, so a
Jacobi lever is no longer choosing against a moving target.
```

<a id="c29c1332-171"></a>

#### #171 — Producer-side lighting CPU is billed to owner `frame` and cannot be attributed without a telemetry model change

status: **pending**

```
FOUND during #168's closure work. Owner `lighting` now closes at 99.5-99.7%, but that whole is lighting.cpu.total.us, which covers only WorldClient::lightingCalc(). Real lighting CPU runs outside it, on the render thread, and lands in cpu.frame.render.us:

 1. StarWorldClient.cpp:534-541 -- WorldClient::render() walks EVERY entity calling renderLightSources(). This is the entire light-source production pass and it is the direct producer of the light list lightingCalc consumes. Untimed by Telemetry (only a LogMap string at :502).
 2. StarWorldClient.cpp:551 -- m_particles->lightSources() iterates every particle and builds a fresh List allocation per frame (StarParticleManager.cpp:106-113). Untimed.
 3. The same block blocks on m_lightMapPrepMutex, which lightingCalc holds from :1990 through the whole gather. On a busy recompute the main thread's stall is charged to cpu.frame.render.us with no attribution to lighting at all.
 4. StarWorldPainter.cpp:143-150 -- the maxEmission scan iterates the entire emission grid (width*height*3 floats) on the render thread, OUTSIDE processFull's cpuCostScope. Completely untimed, and it is O(calcCells) so it carries the same 4.375x border multiplier as task #170.
 5. StarWorldPainter.cpp:233 adjustLighting -> StarTilePainter.cpp:38-55, a per-render-tile read-modify-write over the CPU lightmap. Untimed.
 6. StarWorldClient.cpp:2211 -- LogMap::set on EVERY lighting-thread wakeup takes a GLOBAL mutex (StarLogging.cpp:100-103) and allocates a String via strf. Outside totalScope. Small but it is lighting cost on a global lock.

WHY IT IS NOT A SIMPLE FIX: attributing these to owner=Lighting would break closure -- they sit outside lighting.cpu.total.us, so they would sum into `parts` without being in the `whole` and trip 'parts exceed the whole'. Doing it properly needs a SECOND Total per owner, which is not representable: StarTelemetry.cpp:445-450 keys the owner table by owner name (last row silently wins) and telemetry-window.py:207/265 unpacks `total` as a scalar and would TypeError on a list.

So this is a telemetry MODEL change (multi-total owners, or a nested-budget concept), not a lighting change. Scope it as such. Until then these costs are real, measurable in aggregate as part of cpu.frame.render.us, and invisible individually.
```

<a id="c29c1332-172"></a>

#### #172 — Telemetry deep-off cost: MEASURED — arming costs +2.16%, within noise; the deep gate works

status: **completed**

```
RUN AND CLOSED 2026-07-25. Four 60s captures, --warp 00-Ocean-Lab, interleaved OFF/ON/OFF/ON, same session, same binary (c9b2b024). Scene fingerprint held: lights/rec 32.0/32.0/32.0/32.1 across all four.

Metric: cpu.process.total_us / cpu.frame.updates -- both plain COUNTERS, not deep-gated, so they survive with tracing off. (cpu.frame.* are TelemetryScope-based and vanish when deep is off; that is why the naive comparison does not work.)

  deep OFF   8565.6, 8347.2  -> mean 8456.4 us/frame   (spread 218.4)
  deep ON    8725.3, 8552.7  -> mean 8639.0 us/frame   (spread 172.6)
  arming cost = +182.6 us/frame = +2.16%
  noise floor (worst within-pair spread) = 218.4 us

VERDICT, STATED HONESTLY: the delta is SMALLER than the noise floor, so it is not distinguishable from zero -- but it is NOT proven free either. Both ON runs sat above their adjacent OFF run (2 of 2 in the same direction), which hints at a small real cost this sample size cannot resolve. Anyone wanting it resolved needs more pairs, not a different method.

WHAT IT DOES ESTABLISH: the deep gate works. TelemetryScope reads Telemetry::deepEnabled() once at construction and stores -1 when off; if that check were leaking into the default path, arming ~50 scopes/frame could not cost only ~2%. #168 added nine more scopes to the lighting hot path and the arming cost is still in the noise.

WHAT IT DOES NOT ANSWER: 'what does the instrumentation cost versus a build with none'. That needs a telemetry-free binary, which does not exist -- the subsystem has been in the tree since fd9e398c (2026-06-08). The question as originally written in the plan (compare against 'the pre-change baseline') was not answerable by the time it was reached.

CONFIRMED BEHAVIOUR worth keeping: a deep-off capture ALWAYS trips the closure oracle with 'frame/cpu: 100% unattributed', because no TelemetryScope records and therefore no Budget parts exist. telemetry-window.py exits 3, so render-profile.sh exits non-zero. That is correct, not a bug. Read the emitted JSON; never gate a deep-off loop on exit status.
```

<a id="c29c1332-173"></a>

#### #173 — RB-FLUSH: setScissorRect flushes per widget (~92/frame) — orphaning killed the stall COST, not the flush COUNT

status: **pending**

```
SPLIT OUT of #125 on 2026-07-25 as the genuine residual. #125's orphaning fix removed the COST of each immediate-VBO flush (GPU frame -74 to -81% at the Director's bases). It did not remove the flushes.

THE REMAINING WASTE: Widget::render -> setupDrawRegion -> setScissorRect -> flushImmediatePrimitives fires
for EVERY widget. The in-game HUD is ~92 widgets across 5 panes (ActionBar 32 children, TeamBar 12,
QuestTracker 7, Chat 9 invisible, StatusPane). Measured pre-orphan: ~21 extra flushes/frame carrying
~12 primitives each -- tiny batches, one draw call apiece, plus all the per-flush CPU (accumulation
buffer copy, buffer bind, vertex attrib setup, draw).

WHY IT IS PLAUSIBLY WORTH DOING ANYWAY: a scissor change is NOT a vertex-format change. The primitives
are homogeneous; only the scissor rect differs. So either
  (a) BATCH PER SCISSOR RECT -- accumulate primitives keyed by scissor, flush once per distinct rect
      rather than once per widget. Most HUD widgets in a pane share a rect or nest, so the distinct-rect
      count should be far below 92.
  (b) FOLD THE SCISSOR INTO THE DRAW -- pass the rect as vertex data or a uniform and clip in the shader,
      removing the state change entirely. Bigger change, needs care with the existing scissor semantics
      (GuiContext.cpp:136-140 sets and clears it around draw regions).

MEASUREMENT IS ALREADY WIRED: render.flush.count and render.flush.primitives exist in
flushImmediatePrimitives (StarRenderer_opengl.cpp) as Cpu/Frame/Call/Detail counters. flushes-per-frame
and primitives-per-flush are directly readable from any profile capture -- a high count with a tiny
primitive count is the signature. Capture at a base WITH the HUD up.

EXPECTED SIZE: unknown and possibly small now that the stalls are gone -- the per-flush cost after
orphaning is CPU-side batching overhead, not a 400us pipeline stall. DO NOT assume the pre-orphan ~8.3ms
figure carries over; that number WAS the stalls. Measure before designing, and be prepared for this to
come back MEASURED NULL like #129 (VAO-format bake), which was cleanly removed rather than shipped.

SEQUENCING: strictly after #137 (P-2) if it touches pass structure, since the interface render path is
inside WorldPainter's orchestration. Independent of the lighting work (#169/#170/#171).
```

<a id="c29c1332-174"></a>

#### #174 — P-0b DONE: motion gate shipped (7ce03361) -- bypass PROVEN engaged; G9 SATISFIED, the split is authorised

status: **completed**

- `7ce03361` harness: the motion gate -- a scripted walk, and a counters oracle that reads it

```
BUILT 2026-07-26, commit 7ce03361. GUARDRAIL G9 IS NOW SATISFIED: the BackdropPass split and L3 boundary
change are authorised.

DECISION THIS TASK DEMANDED, made explicitly: the walk serves the NOFREEZE instrument and a new COUNTERS
oracle, NOT the frozen byte-identity gate. A frozen world does not tick, so the player cannot move in it at
all. STAR_RENDERTEST_WALK therefore implies NOFREEZE itself.

SHIPPED: three frame-counted knobs (WALK right/pause/left/pause, TOGGLE flips a client option and rebuilds
every framebuffer, ZOOM steps zoomLevel), the counters oracle, config capture/restore, and
scripts/render-motion.sh as the front door.

THE COUNTERS ORACLE, which is the deliverable rather than the movement: refreshed + skipped +
bypassed_moving partition the parallax pass's frames, and bypassed_moving > 0 was structurally unreachable
before this existed. Retires #136 item (f).

THE FIRST RUN FAILED AND WAS RIGHT TO -- worth keeping. bypassed_moving=0. The player WAS moving,
deterministically (2146.000 -> 2155.684 -> 2156.775 -> 2147.091, identical every cycle). Cause: I ran
against harness/storage/ (the byte-identity gate's config) where parallaxOracle=true and BackdropPass
deliberately pins the cache path -- "oracle must stay on the cache path to gate it" -- so the bypass can
NEVER engage there. render-motion.sh hardcodes harness/sbinit-perf.config and the reason is written down.

VERIFIED BOTH DIRECTIONS on hardware:
  render-motion.sh        MOTION GATE: PASS, exit 0
     walk legs 23, distinct player positions 14
     refreshed=17 skipped=121 bypassed_moving=213
     gl-state desyncs 0 UNDER MOTION + AA REALLOC (the churn #139's audit was built for)
     GL errors 0; antiAliasing restored to false, not pinned
  STAR_RENDERTEST_WALK=0  MOTION GATE: FAIL, exit 1 (0 distinct positions, no verdict)
Frozen gate unaffected: PASS, 100/20/212 oracles, DIFF=0. NoAssets 6/6.

STILL NOT COVERED, and named rather than implied: this gates COUNTERS, not pixels. There is still no
motion-aware PIXEL oracle, so a change that moves drawing logic on a motion path is certified only for arm
selection and ambient GL state, not for what it draws. That is the residual, and it is the same gap as the
world body's missing pixel oracle in #191.

UNBLOCKS: #191 (WorldPass contract 1), the BackdropPass split, and large parts of #135, #136 and #138.</description>
<parameter name="activeForm">Building the motion-driven harness
```

<a id="c29c1332-175"></a>

#### #175 — SIM-1 DONE: server-tick budget closed 99.76% across 27 phases; compute.entities is the real 65%

status: **completed**

- `90d8d236` docs(board): regenerate -- #175 closed, #176 filed [#175]
- `4eb7b96c` sim: close the server-tick budget -- 27 phases, 99.76%

```
CLOSED 2026-07-25, commit 4eb7b96c (pushed to origin/integration).

Owner `sim` had a denominator and NO TOTAL -- parts with no whole. tick.server.compute.us sat at 98.4% of the four parts that existed, which was the ABSENCE of an attribution, not one.

MEASURED at 00-Ocean-Lab over 4807 ticks: closure 99.76%, 5.4 us/tick unattributed across 27 contiguous Budget parts. Independently recomputed from the raw JSON, not taken from the tool's own summary line.
  compute.entities  1483.4 us/tick  65.2%   <- the real dominant phase
  compute.netsync    220.9           9.7%
  compute.liquid     192.9           8.5%
  compute.wiring     119.2           5.2%
  compute.prologue    50.6           2.2%
  compute.damage      43.5           1.9%
  publish             34.1           1.5%

DIRECTOR DECISIONS (both taken, genuinely confirmed after a spurious auto-answer was caught and discarded):
  1. Name all six lock acquisitions as parts -> busy = total - blocked is recoverable. Measured 0.74 us/tick (0.03%) single-player; the instrument is there for multiplayer.
  2. Land all 27 at once -> avoids the transient Budget->Detail->Budget flip on tick.server.commit.us that the two-tier plan required.

THE TOTAL IS BUSY W.R.T. PACING, NOT BLOCKING. It wraps the run() loop body minus the pacing sleep; the sleep is the last statement and the body has one control-flow path, so it is excluded structurally. But six locks live inside it. An earlier draft asserted "busy by construction" -- wrong, and a critic caught it.

FOLDED IN, pre-existing: Telemetry::markTick moved from inside update() (after the mutex) to the first statement of the run() loop body. Two phases used to close before the denominator incremented, so the consumer's count<=denominator assertion fired on correct data -- and DID, in local captures branchless.json / harnesstest.json. New capture: zero violations.

STRUCTURAL: the 16 compute parts are cadence=Call with FILE-SCOPE handles. WorldServer::update is entered only when dt>0 && !paused, and a block-scope static inside a never-entered function never REGISTERS -- the metric would be absent rather than zero, and absent reads as "no such phase". tick.server.compute.us demoted Budget -> Detail (it is their parent).

ADVERSARIALLY REVIEWED BEFORE IMPLEMENTATION: 3 critics, 2 FATALs, both fixed. Verified: core_tests 251/251, game_tests 92/92, render gate PASS. telemetry_test's OwnersDeclareDenominatorAndTotal updated -- it pinned "sim must not invent a total"; sim now MEASURES one.

SPUN OUT: #176 (publish-phase iterator invalidation, pre-existing UB, deliberately not fixed here).
STILL OPEN elsewhere: SystemWorldServerThread has zero telemetry, so "sim closed at 99.76%" means the WORLD tick, not all server CPU. Do not let that claim drift.
```

<a id="c29c1332-177"></a>

#### #177 — ENV-CHOP DONE: env cache had no motion term; ship flight/warp CONFIRMED SMOOTH in game

status: **completed**

- `1014c3b2` docs(board): regenerate -- #177 closed, confirmed in game [#177]
- `82711412` render: write the three-term retained-cache contract where the next author will read it
- `0a027243` docs(board): regenerate -- #177 deployed to dev, awaiting flight test [#177]
- `00f575ad` render: give the env cache a motion term -- fixes choppy stars during ship flight and warp

```
CLOSED 2026-07-25. Fix 00f575ad, contract 82711412. Deployed to /home/apnex/OpenStarbound/dev (md5:a87ae44c55ec) and CONFIRMED BY THE DIRECTOR IN GAME: "ship flying appears smooth now".

ROOT CAUSE: the env cache's refresh predicate was `envInvalidated || envCadence`, where invalidated() means ONLY "size or pixelRatio changed" (StarRetainedSurface.hpp). No motion term, no content key, no bypass. At the shipped envRefreshInterval of 4 the whole moving backdrop was resampled at 15 Hz on a 60 Hz vsync and held three frames.

NUMBERS at the Director's settings (zoomLevel 3, 1080p): starAndDebrisRatio = lerp(0.0625, 2.0, 3.0) = 2.0625 px/view-unit (Star's lerp takes the interpolant FIRST -- reading it as lerp(a,b,t) gives 5.875 and a 3x-wrong estimate). Starfield moves flyMaxVelocity 5000 x starVelocityFactor 0.2 = 1000 view-units/s => ~34 px/frame vs the 0.75 px threshold = 46x over => refresh every frame in flight, effective interval 1. Before: the sky jumped ~138 px per visible update.

WHY IT HID: StarSky.cpp:199-203 pins starOffset/worldOffset to EXACTLY {} when not flying. The missing term is identically zero in the case everyone tests.

THE FIX: drift threshold (displacement since the fill, NOT a predicted rate -- world-tick data reads a 0 per-frame delta on many render frames, the trap that made parallax N flap) + a content key over NON-POSITIONAL inputs only (flash, sky colours, type, quantized skyAlpha/dayLevel, twinkle frame). envCadence stays unconditional and LAST so the frame counter does not shift.

DERIVED, NOT DECLARED: no `if (flying)` check anywhere. Also covers the speedup/slowdown ramps, the arrival correction, and any future mode that moves the backdrop without moving the camera; scales with zoom and resolution for free.

WIN INTACT, measured: env refresh rate 25.0% of frames before AND after at 00-Ocean-Lab -- exactly the N=4 cadence, unchanged. Gate PASS (envoracle 102/102, paralloracle 20/20, spreadoracle 225/225, 0 DIFF, 0 GL errors). core_tests 251/251, game_tests 92/92.

KNOWN RESIDUAL, accepted: planet-side starRotation drifts ~0.46 px between refreshes at N=4/1080p, under the 0.75 threshold -- but only ~1.6x margin, so on a short-day world or at higher zoom the term will legitimately fire and env will refresh more often than N=4. Design working; small real cost there.

GENERALISED into the shared primitive (82711412): StarRetainedSurface.hpp now states the THREE-TERM CONTRACT -- structural (the class) + motion (caller) + content (caller) -- with both caller-side traps written down. The env cache shipped with only term 1 because nothing said there were three.

TWO CONFIG TRAPS recorded, first one causal: envRefreshInterval's call-site fallback is 1 ("reads as off by default") while StarRootLoader ships 4 -- reasoning from the code you are reading concludes the feature is disabled. Same for parallaxMaxDriftStepPx (fallback 1.5, ships 0.75). envMaxDriftStepPx was therefore declared in StarRootLoader, not left as a fallback. Folded into memory [[config-runtime-pins-defaults]].

MY FIRST DIAGNOSIS WAS WRONG: I blamed the parallax cache's adaptive N. A shipworld has ZERO parallax layers (size-only WorldTemplate ctor => m_layout null => biome() short-circuits => setParallax never runs), so parallaxCacheActive is false in flight and that fix would have been a literal no-op. Our own tree already said so at StarClientApplication.cpp:1471-1473. The four-angle investigation refuted it on independent grounds; the adjudicator also caught that a naive content key would have destroyed the cache's win.
```

<a id="c29c1332-178"></a>

#### #178 — GATE-STALE DONE: render-gate.sh now asserts the build happened, not just the run (2643b1ce)

status: **completed**

- `2643b1ce` render: the gate can no longer certify a binary that was never built

```
FIXED AND PUSHED 2026-07-26, commit 2643b1ce on `integration`.

The gate asserted one thing about freshness -- log newer than binary -- which a FAILED BUILD satisfies
trivially. It now asks both questions, and the source-side check runs BEFORE the game boots:
  1. did the BUILD happen?  the binary is newer than every source under source/ (source/test pruned,
     since that builds the test binaries not the game). dist/ IS the CMake runtime output dir
     (source/CMakeLists.txt:574), so the binary's mtime is its link time, not a copy's.
  2. did the RUN happen?    the pre-existing log-newer-than-binary check.
The verdict line now names the binary and its timestamp. No override -- an escape hatch here is the defect.

VERIFIED BY CONSTRUCTION, both directions (audit closeout hook 3):
  * clean tree     -> "certifying dist/starbound (2026-07-25 22:14:43)", GATE: PASS,
                      envoracle 100/100, paralloracle 20/20, spreadoracle 206/206, 0 GL errors
  * touched source -> "REFUSING TO CERTIFY", names StarWorldPass.cpp, exit 1 in 4ms, game never launched

Memory node [[render-harness]] amended: the standing rule said only "assert log -nt binary" and that half
-assertion is what let this through. It now states both directions with the failure mode written out.

RESIDUAL, deliberately not done: this is an mtime check, not a content check. A build-id embedded by CMake
and asserted by the gate (the audit's alternative for delta[2]) is stronger and needs a rebuild to land;
mtime closes the actual observed hole -- a failed build leaving the binary untouched -- with no C++ change.
```

<a id="c29c1332-179"></a>

#### #179 — DOC-DRIFT DONE: Air-Gap counts generated into the doc and gated by render_docs_fresh (c89be289)

status: **completed**

- `2ef87860` render(tools): measure the two things the published artifacts quote, and claim the Renderer interface
- `e8d6860a` docs(board): file the M7 audit deltas -- 11 new tasks, #174/#136/#170 corrected [#179]
- `7c0e8340` docs(board): regenerate -- #178 and #179 closed, #137 corrected [#179]
- `c89be289` docs(render): generate the Air-Gap counts into the doc, and gate them

```
FIXED AND PUSHED 2026-07-26, commit c89be289 on `integration`.

Four wrong current-state numbers removed from docs/render/architecture-3-target-state.md: "now 296"
(tree 318), "427 -> 119-line render()" (tree 141), BackdropPass "7 singleton reads" and "the worst
offender" (tree 0 since 1e46f71c). Compliance matrix re-scored: BackdropPass contract (2) clean,
contract (1) still unmet -- BackdropInput/LightingInput/WorldInput verified absent from the tree.

THE MECHANISM, because measuring beside the doc had already failed: render-inventory.py grew
--residual / --inject FILE / --check FILE, writing the coupling residual into a marker block inside the
doc, and `render_docs_fresh` (ctest, LABELS NoAssets) fails CI when the block and the tree disagree.
Paths resolve against the repo, not the cwd, since ctest runs from the build dir.

The block carries COUPLING COUNTS ONLY, never line counts -- gating those would redden CI on every render
commit, the zero-tolerance failure mode the render_layering ratchet already decided against. It reports
metered-vs-total, which makes the audit's delta[7] permanently visible: the ratchet meters 3 files and
most of the residual sits in WorldPainter and the painters, which no gate touches.

Every number left in the file is HISTORICAL (876, 112, 427, the 8th read) -- past states no instrument can
measure and no drift can falsify. The rule is written into the doc's own header.

VERIFIED BOTH DIRECTIONS: ctest -L NoAssets 5/5; a copy with the total edited 17 -> 14 makes --check exit 1
with a unified diff and the remedy. All three script gates then re-run from a PRISTINE CLONE of HEAD --
layer1_layering, render_layering, render_docs_fresh all exit 0 -- so the claim is about what shipped, not
about my working tree. (core_tests and render_surface_tests were verified locally; no C++ changed in
either commit, so their result is unaffected. A full pristine BUILD is still the open half of audit G1.)

#137 corrected -- it quoted the same dead count -- and docs/board.md regenerated.
```

<a id="c29c1332-180"></a>

#### #180 — AX-A7 DONE: the clause-2 recovery now recovers, and can be executed (6b6e8b72)

status: **completed**

- `6b6e8b72` render(L3): make the clause-2 recovery actually recover, and make it executable

```
SHIPPED 2026-07-26, commit 6b6e8b72.

THE DEFECT. On a stale deferral flag at entry the detector logged "Recovering by compositing env
directly this frame", bumped render.backdrop.compose_recovered, and reset the flag -- then the merge
decision 170 lines later SET IT AGAIN on the same frame, because backdropComposeMerge ships true. The
deferral repeated, the black frame repeated, and the logged sentence described an action nobody took.
It then went silent after four frames, because the warn budget is a process-lifetime static (#181).

THE FIX, two tokens: `params.composeMerge && !recoverThisFrame`. The recovering frame is forced onto the
standalone branch -- the pre-CM-1 path the comment always claimed, always safe -- costing one merged
compose in a frame that was already broken. The next frame re-arms normally.

MADE EXECUTABLE. STAR_BACKDROP_FORCE_DEFER=1 arms the fault on one frame past warmup. Env-gated like the
existing STAR_RENDERTEST_* knobs, read once, zero cost unset. The reason a broken recovery survived
review is that nothing could run it.

VERIFIED:
  injected  fault armed 1x, recovery logged 1x, GATE: PASS (99/20/201, DIFF=0, 0 GL errors)
  normal    GATE: PASS (89/20/207, DIFF=0, 0 GL errors); ctest -L NoAssets 5/5

WHAT IT DOES NOT PROVE, recorded in the code as well as here: the injection supplies a STALE FLAG, not
the abort that produces one. renderParallax still runs on the injected frame, so the frame is correct
either way and THE GATE CANNOT DISTINGUISH FIXED FROM BROKEN. It proves the branch executes and is
harmless. Reproducing the real black frame needs renderParallax SKIPPED for a frame -- a caller-side
knob this pass cannot provide, not worth inventing for a currently-unreachable fault. The fix's
correctness rests on reading the branch; the contribution is that it is now a branch that runs.

RESIDUAL, carried by #181: the counter is still read by no gate, and the warn budget is still a
process-lifetime static, so a burst of four still buys session-long silence.
```

<a id="c29c1332-181"></a>

#### #181 — AX-A1-COUNTERS DONE: gate reads the contract violation; counters registered at construction (472fd263)

status: **completed**

- `472fd263` render: make the backdrop's contract violation observable -- gate it, register it, un-mute it

```
Two halves of the same Hidden-State finding, both verified in the tree 2026-07-26.

(1) NOBODY READS THE COUNTER. `render.backdrop.compose_recovered` appears in exactly two places, both
inside StarBackdropPass.cpp (:114 registration, :283 a comment). Zero scripts, zero tests, zero docs --
grepped. A contract violation the code deliberately survives is observable only by someone who already
suspects it. render-gate.sh's own comment states the rule this breaks: "a check that reports but does not
gate is not a gate."
  FIX: assert it in render-gate.sh's verdict block (guardrail G6). Any counter that signals a contract
  violation belongs in the verdict, or it is not a signal.

(2) THE WARN BUDGETS ARE PROCESS-LIFETIME. StarBackdropPass.cpp:107 `static int warnBudget = 4`. A burst
of four buys session-long silence -- including across world re-entry, where the operator would most
expect a fresh diagnosis.
  FIX: reset on world entry; invalidateCaches() is the natural hook.

(3) REGISTRATION IS CONDITIONAL. The telemetry counters are function-local statics inside conditional
blocks, so a path never taken yields an ABSENT key in snapshot() -- which a consumer differencing two
snapshots cannot distinguish from ZERO. compose_recovered is the worst case: by construction it did not
exist until the fault first fired.
  FIX: hoist the registrations to unconditional registration at pass construction. Same bug class as the
  block-scope-statics-never-REGISTER trap already recorded in the telemetry work.

Small, unblocked, and (3) is the one that makes the other two trustworthy.
```

<a id="c29c1332-182"></a>

#### #182 — AX-A14 DONE: ContentKey's quantiser was UNDEFINED, not merely untested — fixed + 6 tests (13ed9399)

status: **completed**

- `13ed9399` render(L2): ContentKey's quantiser was not a function of its input -- tests, then the fix

```
SHIPPED 2026-07-26, commit 13ed9399. Filed as "add missing tests"; writing them found a real defect.

THE DEFECT. `mix((uint64_t)(unsigned)floor(scale * v))` -- float->unsigned conversion outside the
destination range is UNDEFINED BEHAVIOUR. Measured under the shipping flags rather than reasoned about:

  -O3 -ffast-math   two keys from DIFFERENT over-range inputs printed the SAME value
                    (140721258484321) and compared UNEQUAL. Address-shaped, not hash-shaped.
  -O0               different values again; NaN landed on the zero bucket where -O3 did not.

A content hash whose comparison disagrees with its own printed value is not a wrong hash, it is not a
hash. Silent by construction: the symptom is a parallax cache that thrashes or spuriously matches and
composites a stale sky -- never a crash.

REACHABILITY, which is what makes it worth fixing rather than noting: the three call sites are skyAlpha,
dayLevel and parallax layer.alpha. layer.alpha comes from MOD-AUTHORABLE parallax JSON. On an install
with hundreds of mods a negative alpha is one authoring mistake away.

THE FIX -- and the FIRST ATTEMPT WAS WRONG IN THE SAME WAY AS THE ORIGINAL. Clamping to 4294967295.0f is
itself out of range: 4294967295 is not representable as a float and rounds UP to 2^32. The standalone
probe caught it. The bound is 4294967040.0f (0xFFFFFF00), the largest float below 2^32.

VERIFIED BEFORE TOUCHING THE HEADER, at both -O0 and -O3 -ffast-math: negative clamps to the zero
bucket, over-range saturates consistently, in-range 0.5 is BIT-IDENTICAL to the pre-fix value
(4953296397560300852). Nothing in [0, 2^32) touches either bound, so no reachable render path changed.

TESTS 9 -> 15: determinism across instances, order-sensitivity, mix(0) advancing the FNV state,
quantisation bucketing, Vec3B/Vec4B non-collision, totality. The totality test FAILED before the fix.

NaN DELIBERATELY NOT ASSERTED. The build is -ffast-math (-ffinite-math-only); clang states using a NaN
at all is undefined and warns on the literal. No leaf function restores semantics the TU was told to
assume away, so asserting it would be green because the compiler chose, not because the code is right --
the oracle-outside-its-contract trap this campaign already paid for once. Recorded in header and test.

Closes guardrail G5's standing counter-example: no L2 member now ships without off-GPU coverage of its
invalidation terms.

VERIFIED: core_tests 15/15; ctest -L NoAssets 5/5; GATE: PASS (envoracle 101/101, paralloracle 20/20,
spreadoracle 220/220, 0 GL errors).
```

<a id="c29c1332-183"></a>

#### #183 — AX-A3-RATCHET DONE: all 17 singleton reads metered, relocation is no longer a route to green (8eddcb37)

status: **completed**

- `8eddcb37` render(gate): meter every file a relocated singleton read can land in

```
SHIPPED 2026-07-26, commit 8eddcb37 on `integration`.

The ratchet metered 3 files holding 2 of 17 reads. Now every file that can RECEIVE a relocated read
carries a ceiling: WorldPainter=9, TilePainter=3, TextPainter=3, EnvironmentPainter=0,
DrawablePainter=0 (the last two are PROHIBITIONS, not ratchets -- they are clean). Metered 2 -> 17.

Guardrail G4 is now mechanical rather than remembered: a read moved out of a pass counts against
whatever catches it, so the metric can only be satisfied by REMOVING coupling, not by moving it.

CROSS-CHECK PERFORMED (audit closeout hook 6, which said confirm rather than assume):
render-inventory.py and layering-lint.sh use different comment-stripping strategies and had never
been compared outside the three metered files. Both report 15 across the five added, + 2 = 17. Exact.

VERIFIED BOTH DIRECTIONS: ctest -L NoAssets 5/5 at the committed ceilings; tightening WorldPainter
9 -> 8 gives LAYERING CEILING EXCEEDED, exit 1, printing all nine offending lines with file:line.

NOTABLE: render_docs_fresh (#179) caught this commit -- metered went 2 -> 17, the generated table
changed, CI went red until the block was regenerated and the diff read. First real catch by that gate,
one day after it landed.

WHAT THIS DOES NOT DO: it removes zero coupling. The residual is still 17. Its value is that the next
pay-down is honest -- and #137's mechanism is precisely more relocation to the composition root, which
would otherwise have driven the passes to 0 while WorldPainter silently climbed past 9.
```

<a id="c29c1332-184"></a>

#### #184 — AX-A3-WORLDPASS DONE (declaration): four consumed inputs documented at the signature (252bed68)

status: **completed**

- `252bed68` render(L2/L3): interface hygiene -- declare what renderWorld consumes, delete what nothing uses

```
SHIPPED 2026-07-26, commit 252bed68 on `integration`. Option (a) -- declare -- is done.

THE AUDIT FOUND ONE CONSUMED FIELD. MEASURING FOUND FOUR:
  CONSUMED (hollowed; outer container intact, elements moved-from)
    entityDrawables      std::move out of ed.layers
    backgroundOverlays } all three via drawDrawableSet, whose entire body is
    foregroundOverlays }   `for (Drawable& d : drawables) drawDrawable(camera, std::move(d));`
    nametags           }
  READ-ONLY
    particles            List<Particle> const*
    overheadBars         iterated by const&

The contract is now written at StarWorldPass.hpp's renderWorld declaration, with the reason: the
rendertest state fingerprint already reads m_renderData AFTER render and survives only because it reads
outer .size(), which the moves leave intact. Extend it to hash nametag or overlay CONTENT -- the obvious
next step -- and it returns a stable, WRONG fingerprint claiming the inputs matched when they were
consumed. A diagnostic that lies is worse than none.

REMAINING, AND DELIBERATELY CARRIED BY STAGE C (#137) RATHER THAN HERE: option (b), splitting the
signature so the sink and the view are separate parameters. That is not a follow-up to the DTO work --
it IS the DTO work, because `WorldInput` cannot be a const& view when four of six members are consumed.
Declaring it first is what makes the DTO designable.

Contrast measured the same way and recorded for stage C: BackdropPass touches only TWO members
(skyRenderData, parallaxLayers) and writes neither, so `BackdropInput` is a two-field const view. The
fat struct has 35 members; the two passes use 2 and 6.
```

<a id="c29c1332-185"></a>

#### #185 — AX-A2-CONFIG DONE: getOrDefault + config_declared gate; newLighting declared, antiAliasing moved down (d46fee26)

status: **completed**

- `d46fee26` config: one declaration per knob -- getOrDefault, and a gate that keeps it

```
VERIFIED 2026-07-26. The asset-side spec is genuinely isomorphic -- opengl.config and effects/*.config are
parsed at runtime and mod-overridable. The C++ side is not: a render knob's value can come from
StarRootLoader defaults, a call-site literal, storage/starbound.config, or nowhere at all, with NO
authority rule between them.

CONFIRMED DIVERGENCES (defaults vs call-site fallback literals):
  * envRefreshInterval    ships 4    against a WorldPainter fallback of 1
  * parallaxMaxDriftStepPx ships 0.75 against a call-site 1.5f  -- KNOWINGLY preserved for byte-identity
  * lightingWorldUpscale  ships 2.0  against 1.0f
  * newLighting           read and written, and EXPOSED AS A GRAPHICS-MENU CHECKBOX
                          (assets/opensb/interface/windowconfig/graphicsmenu.config.patch.lua:62-63)
                          -- declared in no default block anywhere.

THE FALLBACKS ARE REACHABLE, NOT DEAD. Configuration::set with a null value ERASES the key, and the A/B
restore path can pass null with a null captured original. Once erased, the call-site literal becomes
authoritative for the rest of the session. This is the same family as the already-recorded trap that
storage/starbound.config pins any /command-set value and silently overrides shipped defaults -- which
disabled the env cache for hours.

OPERATOR-VISIBLE CONSEQUENCE: /rendercache status can report the parallax cache "off" (reading fallback 1)
while the render path runs adaptive (fallback 0). The console lies to the Director.

FIX:
  1. Declare newLighting in a default block.
  2. Route render config reads through ONE accessor that falls back to Configuration::getDefault(key)
     rather than to a hand-typed call-site literal. That makes the default block the single authority and
     deletes the whole divergence class.
  3. parallaxMaxDriftStepPx's divergence is deliberate (byte-identity); correct it as its own labelled
     change, not folded into this.

Medium. Unblocked. Touches the config path, so byte-identity verification applies.
```

<a id="c29c1332-186"></a>

#### #186 — AX-A3-TIDY DONE: dead includes deleted, compose call site folded, false L2 build claim corrected (252bed68)

status: **completed**

- `252bed68` render(L2/L3): interface hygiene -- declare what renderWorld consumes, delete what nothing uses

```
SHIPPED 2026-07-26, commit 252bed68 on `integration`. All three items, plus two that fell out.

(1) DEAD INCLUDES. StarRoot.hpp / StarConfiguration.hpp deleted from StarBackdropPass.cpp. They
outlived the last Root::singleton() read by a week. render_layering greps for the CALL, so a pass can
be architecturally re-coupled through an include while still measuring 0 -- the compile is the only
thing that can prove the edge is gone. It now has.

(2) DUPLICATED STANDALONE-COMPOSE. Folded into BackdropPass::composeEnvStandalone(Vec2U const&). The
two sites were verbatim copies including a ten-line comment, differing only in which screen-size
variable they read. Byte-identical: both issued the same four calls in the same order.

(3) FALSE BUILD CLAIM IN L2. StarRetainedSurface.hpp claimed accepting Color would "drag StarColor.hpp
across the seam" and cost the core-only link. StarColor.hpp is in source/core/ -- it would have cost
nothing, and the claim was false when written. Corrected to name the REAL fence: nothing here names
StarRenderer.hpp, which is what layer1_layering enforces. Vec4B/Vec3B stands as the narrower judgement.

FELL OUT WHILE DOING IT:
(4) StarRetainedSurface.hpp was ABSENT from star_rendering_HEADERS -- the layer with the strictest
sovereignty claim was not a declared source of the target containing it. Added.
(5) THE GpuLightmapPass -> LightmapPass RENAME IS STRUCK, NOT DEFERRED. `Gpu` is load-bearing: there is
a live CPU lightmap path (renderData.lightMap, gated by lightingGpu; the pass's own header documents
falling back to it). Dropping it would stop the type naming WHICH OF THE TWO it is. The docs were
corrected to the code, not the reverse.

VERIFIED: render-gate.sh GATE: PASS (105/20/211 oracles, DIFF=0, 0 GL errors); ctest -L NoAssets 5/5.
```

<a id="c29c1332-187"></a>

#### #187 — AX-A6 DONE: label rule + preset filter asserted at configure time, both proven to fire (786d4342)

status: **completed**

- `786d4342` ci: the gates could not start on Windows, and nothing was reading CI

```
Every testPreset in source/CMakePresets.json inherits `base`, which filters to LABELS NoAssets, and
.github/workflows/build.yml drives ctest through those presets. So a registered test WITHOUT the label is
SILENTLY SKIPPED by every CI job -- it exists, it passes locally under a bare ctest, and it guards nothing.

That is not hypothetical: it is what left BOTH Layer-1 architecture gates dormant from creation. They were
cited in docs as standing lints while never having executed in CI once.

The rule is currently enforced by a large comment in source/test/CMakeLists.txt asking the next author to
remember. That is prompt-only enforcement of a deterministic rule -- M7's named failure mode, whose
correction is "mechanize or file a primitive follow-up".

FIX: a small ctest that parses the registered test names and asserts every test not on an explicit
assets-required allowlist carries the NoAssets label. The allowlist is the architectural statement (today:
game_tests), so adding to it is a deliberate act rather than an omission.

RELATED OPEN QUESTION worth resolving in the same pass (audit closeout hook 1): does an ADD_TEST whose
COMMAND is a .sh or .py file actually execute on the windows-latest runner? If it does not, layer1_layering,
render_layering and render_docs_fresh are all green-by-absence on one of six jobs and the label wiring
bought nothing there. Check before claiming the gate set is proven.

Small, unblocked.
```

<a id="c29c1332-188"></a>

#### #188 — AX-A4-SPEC DONE: constructor injection retracted in place, per G10 (8781759c)

status: **completed**

- `8781759c` docs: correct the design authority, and give docs/render a front door

```
docs/superpowers/specs/2026-07-19-render-decomposition-design.md §3 prescribes constructor injection for
the Air-Gap config contract, and claims each pass owns its metric handles. Both were overturned by
implementation, on evidence, and CORRECTLY:

  * Constructor injection would FREEZE eight live-tunable knobs mid-session -- /rendercache envrefresh and
    friends -- which are the console levers the campaign uses to A/B itself. The implementation resolves a
    per-frame BackdropParams at the composition root instead (StarBackdropPass.hpp), which is the same
    Air-Gap property without the regression.
  * No pass owns a telemetry handle; all use function-local statics. WorldPass in fact holds the STRONGEST
    design in the tree -- its descriptor travels with begin() -- which is better than the contract as
    written. The contract is wrong, not the code.

THE PROBLEM IS PURELY THE RECORD. The retraction exists only downstream, in the implementation's comments
and in architecture-3. An agent that opens the Director-approved authority document and follows it will
implement constructor injection and silently freeze the console levers -- reintroducing precisely the
regression the implementation refused.

FIX: correction banner on §3 retracting both prescriptions and pointing at the implementation, with the
one-sentence reason for each.

GUARDRAIL G10, which is the generalisation and matters more than this instance: when an implementation
overrides a Director-approved design prescription on evidence, THE AUTHORITY DOCUMENT IS CORRECTED IN THE
SAME CHANGE. An override that lives only downstream is a trap for whoever reads the authority next.

Small. Documentation only, but it is the authority document, so it outranks most of the code items here.
```

<a id="c29c1332-189"></a>

#### #189 — AX-A12 DONE: docs/render/README.md index + both chains cross-link (8781759c)

status: **completed**

- `8781759c` docs: correct the design authority, and give docs/render a front door

```
Six files, ~1384 lines, no entry point, and the architecture-N chain and layer1-architecture.md reference
each other ZERO times (verified by grep). A cold agent's reading order is therefore arbitrary -- and the
arbitrary order routes through whichever doc happens to be stale.

That is not a theoretical navigation complaint: it is the mechanism that converts a stale prescription into
a wrong action. The audit's A4 finding (the design spec still prescribing constructor injection) and this
one compose -- an agent lands on the authority doc first because nothing tells it not to.

FIX: docs/render/README.md, ~30 lines:
  * each file's ROLE -- canonical / point-in-time snapshot / narrative panel
  * each file's FRESHNESS BASIS -- generator-backed (and by which script) vs hand-written, and last verified
  * a reading order for someone arriving cold
  * cross-links: architecture-3 <-> layer1-architecture.md

Also fix while here: layer1-vs-vanilla-assessment.md still says RetainedSurface "is not built". It is built,
tested off-GPU in core_tests, and has two consumers.

EXPLICITLY REJECTED by the audit and NOT part of this: adding a repo-root CLAUDE.md. That was a repo-wide
tooling preference proposed on subsystem evidence with no failure traced to it -- struck as scope creep.
The README survives because a concrete failure path was traced through its absence.

Small, unblocked.
```

<a id="c29c1332-190"></a>

#### #190 — AX-A8-PRISTINE DONE: G1 closed via six-platform CI from actions/checkout; local clone blocked by #196 (704199ef)

status: **completed**

- `704199ef` docs(audit): record the CI arc, and what a pristine checkout actually proved

```
PARTIALLY DONE 2026-07-26. The three script gates -- layer1_layering, render_layering, render_docs_fresh --
were re-run from a pristine `git clone` of HEAD (c89be289) and all exit 0. That half is closed.

THE OPEN HALF: core_tests and render_surface_tests have only ever been verified from a working tree. They
are compiled targets, so proving them from a pristine checkout means clone -> configure -> BUILD -> ctest.
Today the argument that they are fine is an INFERENCE (no C++ changed in the doc/gate commits, so their
behaviour is unchanged) -- sound, but an inference is not the evidence guardrail G1 asks for.

WHY THIS IS A STANDING ITEM AND NOT A ONE-OFF: the claim "CI gate set 4/4" was originally producible ONLY
from a dirty tree, and nothing at the time distinguished "the gates pass" from "the gates pass on this
workstation with two uncommitted files". Origin was red for two commits while every local test passed.

DO: clone HEAD to a scratch dir, configure with the linux-release-clang preset, build the test targets
E-CORE PINNED (taskset -c 6-15 nice -n 19 ... -j 8, Director out of game), run ctest -L NoAssets, paste the
output. Confirm every gate RAN and PASSED -- not skipped, not exit 2.

ALSO CHECK (audit closeout hook 1): whether an ADD_TEST whose COMMAND is a .sh/.py executes at all on the
windows-latest runner. If it does not, all three script gates are green-by-absence on one of six jobs.
Overlaps AX-A6 (#187); do it in whichever lands first.

Costs one full build. Worth batching with the next build window rather than run standalone.
```

<a id="c29c1332-191"></a>

#### #191 — DTO-2 RE-SCOPED: blocker 2 DONE (b2ac6c27); blocker 1 is bigger than filed -- it reaches TileDrawer in the game layer

status: **pending**

- `334bc38d` tools: measure the game&lt;-&gt;render boundary before anyone moves it
- `b2ac6c27` render: EntityDrawables moves out of the header the slice exists to avoid
- cited in `docs/superpowers/specs/2026-08-01-target-state-system-architecture.md`

```
BLOCKER 2 -- DONE 2026-07-26, b2ac6c27. EntityDrawables moved from StarWorldRenderData.hpp to
StarEntityRenderingTypes.hpp, where both its dependencies already live and which WorldRenderData.hpp already
includes, so no include changed and every consumer still sees it.

BLOCKER 1 -- SLICE FULLY CHARACTERISED 2026-07-26. The task's original sizing ("THREE members of thirty-five
... a small change by member count") is wrong in three separate ways, all found by measuring:

(1) THE UNION IS 5 MEMBERS, NOT 3, and it spans two classes in two libraries.
      TileDrawer  (star_game)      reads geometry x4, tileMinPosition x2, tiles x4
      TilePainter (star_rendering) reads geometry x3, lightMap x3, lightMinPosition x2
      union = geometry, tileMinPosition, tiles, lightMap, lightMinPosition  (5 of 35)
    TilePainter : public TileDrawer -- inheritance across the library boundary, not a call -- so the
    inherited forEachRenderTile(WorldRenderData const&) is part of TilePainter's own surface.

(2) TILEDRAWER OWNS A WorldRenderData BY VALUE, and this is the real blocker.
      WorldRenderData m_tempRenderData;  +  Mutex m_tempRenderDataMutex;
      WorldRenderData& renderData();  MutexLocker lockRenderData();
    StarMaterialItem.cpp:349-366 uses it as a SCRATCH BUFFER: locks it, sets geometry to WorldGeometry(3,3),
    resizes/fills a 3x3 tiles array, writes one RenderTile, and calls produceTerrainDrawables to draw a
    placement preview. A reference-slice cannot serve that -- scratch needs real STORAGE for `tiles`.
    So the slice is really an EXTRACTION: a `TileRenderData` value type owning {geometry, tileMinPosition,
    tiles} that WorldRenderData then CONTAINS rather than duplicates, plus a read-only view for the lighting
    members. That is a type-level change to a game-layer struct with 35 members, not a signature rewrite.

(3) IT IS NOT SAFE-BY-CONSTRUCTION, correcting an earlier claim of mine on this task.
    TilePainter's three members have three distinct types, so a mis-wire there IS a compile error. The UNION
    does not have that property: tileMinPosition and lightMinPosition are BOTH Vec2I. Swapping them compiles
    silently and produces wrong tile lighting -- precisely the silent pixel change the missing world-body
    oracle cannot catch (see the verification note below).
    MITIGATION AVAILABLE: source/core/StarStrongTypedef.hpp already provides strong_typedef. Giving the two
    positions distinct types converts the one silent failure mode into a compile error and restores
    safe-by-construction. Do this FIRST if the slice proceeds.

VERIFICATION GAP, measured not just named: the golden full-frame hash CANNOT serve as the world-body oracle.
Same frozen scene, IDENTICAL state fingerprint (epochTime, dayLength, camera, parallaxLayers=1,
entities=213), different hash every run -- stable within a run, different across runs, and NOT ASLR (3 runs
under setarch -R gave 3 distinct hashes). #153 reached the same conclusion by another route. Making the world
body reproducible means pinning entity animation phase at freeze; its own project.

NEXT ACTION: this is a game-layer type extraction, and the Director has deferred the game-layer refactor.
Sequence it behind #199's boundary ratchet showing whether the boundary is actually degrading. If it does
proceed: strong_typedef the two Vec2I positions first, extract TileRenderData second, re-point MaterialItem's
scratch third, and only then slice the signatures.
```

<a id="c29c1332-192"></a>

#### #192 — CI-1 DONE: lint ported to Python, all three gates registered via ${Python3_EXECUTABLE} (786d4342)

status: **completed**

- `45da57fc` ci: two Windows-only failures, both real, neither what the message said
- `786d4342` ci: the gates could not start on Windows, and nothing was reading CI

```
Uncovered by #187. ctest invokes ADD_TEST commands via CreateProcess on Windows, which does not honour a shebang and does not do PATHEXT/file-association lookup. So layer1_layering (.sh), render_layering (.sh) and render_docs_fresh (.py) all report ***Not Run on windows-latest -- registered, NoAssets-labelled, filtered IN by the preset, and unrunnable. ctest scores Not Run as FAILED, so they redden Windows CI unconditionally while guarding nothing there. Fix: port scripts/layering-lint.sh to pure Python (the payload is already Python; only root-location and arg parsing are bash), and register all three as COMMAND ${Python3_EXECUTABLE} <script>, never relying on the shebang. If Python3 is not found at configure, register a loud failing placeholder rather than silently dropping the gates.
```

<a id="c29c1332-193"></a>

#### #193 — CI-2 DONE: STAR_EXT_GUI_LIBS_CORE split + CMake assertion; test no longer links Steam (786d4342)

status: **completed**

- `786d4342` ci: the gates could not start on Windows, and nothing was reading CI

```
dyld: Library not loaded: @loader_path/libsteam_api.dylib -- render_surface_tests links ${STAR_EXT_GUI_LIBS}, which appends ${STEAM_API_LIBRARY} when STAR_ENABLE_STEAM_INTEGRATION is ON (macOS CI has it ON). The dylib is staged next to dist/starbound only, so the test binary cannot launch at all: Subprocess aborted in 0.01s on both macOS jobs. It passes on Windows and Linux by accident of how those link. Beyond the CI red, linking a third-party social SDK into the unit test of a SOVEREIGN Layer-1 module is the opposite of what the module claims to be. Fix: split the SDL3+GL+GLEW core out of STAR_EXT_GUI_LIBS and link the test against that, leaving Steam/Discord for the game target.
```

<a id="c29c1332-194"></a>

#### #194 — CI-3 DONE: absolute 15us bound -&gt; 4x ratio; both ends measured, injection proves it fires (f87a6848)

status: **completed**

- `45da57fc` ci: two Windows-only failures, both real, neither what the message said
- `f87a6848` test: the exception perf guard measured the machine, not the property

```
core_tests fails on windows-latest: EXPECT_LT(usPer, 15.0) got 491.03. The test guards a real fix (StarException must not resolve a DWARF backtrace eagerly in its ctor) but does so with an ABSOLUTE microsecond ceiling measured on this Linux box. On Windows the raw capture path (dbghelp/CaptureStackBackTrace, globally serialised) is far more expensive than Linux's fast unwind, so the number says nothing about whether the regression is present. Same lesson as the render oracles: an absolute threshold is not portable, a DIFFERENTIAL one is. Fix: assert the RATIO -- throw+catch+discard must be several times cheaper than throw+catch+printException(e,true) -- which cancels machine speed. Print all measured numbers either way.
```

<a id="c29c1332-195"></a>

#### #195 — CI-4 DONE: Gates workflow runs the 4 script gates on every push; ceilings read via --from-cmake (1328b3f5)

status: **completed**

- `1328b3f5` ci: run the architecture gates on every push, not just source/ changes

```
.github/workflows/build.yml triggers on push only for paths assets/**, source/**, toolchains/**, triplets/**. A commit touching ONLY scripts/ or docs/ starts no CI at all -- 8781759c (the docs/render index + spec correction) triggered nothing. That is precisely inverted: render_docs_fresh exists to catch drift in docs/render/architecture-3-target-state.md, and layer1_layering/render_layering live in scripts/, so the three gates are blind to commits in exactly the trees they police. Adding docs/** and scripts/** to the existing filter would fire six full multi-platform builds for a typo fix -- too expensive. The right shape is a SEPARATE lightweight workflow (ubuntu, configure-only, no compile) that runs the script gates plus the configure-time label/preset checks on every push regardless of path. Cheap, always runs, and it is the only job that would catch a doc-only or script-only regression.
```

<a id="c29c1332-196"></a>

#### #196 — BUILD-1: a pristine Linux build cannot bootstrap on this workstation -- jemalloc 5.3.1 vs libstdc++ 16

status: **pending**

```
Found by the G1 pristine-checkout proof (#190), 2026-07-26. Cloning HEAD to a scratch dir and configuring fails during vcpkg install: jemalloc 5.3.1's src/jemalloc_cpp.cpp calls std::__throw_bad_alloc(), which libstdc++ 16 (/usr/lib/gcc/x86_64-redhat-linux/16) no longer declares -- "error: no member named '__throw_bad_alloc' in namespace 'std'". The existing build/linux-release-clang/vcpkg_installed predates that toolchain and still works, so the failure is invisible until someone builds from scratch. CI's ubuntu-22.04 jobs are on an older libstdc++ and are unaffected.

Two separable issues:
(1) TOOLCHAIN: jemalloc 5.3.1 does not compile against libstdc++ 16. Needs a newer jemalloc port, a vcpkg overlay patch, or a pinned older toolchain for that port.
(2) REPO, and the more interesting one: source/vcpkg.json lists { "name": "jemalloc", "platform": "linux" } UNCONDITIONALLY, while source/CMakeLists.txt:142 declares option(STAR_USE_JEMALLOC ... OFF). So every Linux build compiles and installs a dependency the default configuration does not link, and when that dependency stops building the whole project stops -- for a library it was not going to use. The fix is to gate the vcpkg dependency behind a manifest feature driven by the CMake option (VCPKG_MANIFEST_FEATURES set before project()).

Risk if left: the working build directory is the only thing keeping this machine able to build. Lose it and the fork cannot be rebuilt here.
```

<a id="c29c1332-197"></a>

#### #197 — FBO-3: effect-parameter state persists per effect and nothing asserts it -- the `world` effect is the exposure

status: **pending**

```
SPLIT OUT of #133 item 1 on 2026-07-26, after the re-audit found the original case solved but the hazard intact.

WHAT IS SOLVED, and should not be re-fixed: the shared passthrough effect. Renderer::composite() sets its
params explicitly on every call (StarRenderer_opengl.cpp:844, "no cross-consumer param bleed"), so
lightingPassthrough is safe across its consumers via passthroughParams(bool). The environmentCompose.config
workaround that #133 cited as evidence no longer exists -- #143 replaced it with backdropCompose, which
declares zero effectParameters.

WHAT REMAINS. EffectParameter::parameterValue (StarGlRenderSurface.hpp:30) is stored per effect and lives
for the effect's lifetime. Nothing resets it at frame start; GlPass::bindEffect replays SCRIPTABLES only.
So an ordinary parameter keeps whatever the last writer set, across frames and across consumers.

THE EXPOSURE IS `world`: bound at TEN sites (StarBackdropPass.cpp:61,87,114,630,653;
StarGpuLightmapPass.cpp:280,284; StarClientApplication.cpp:533 and the WorldPainter path) and declaring
SEVEN parameters (lightMapEnabled, lightMapMultiplier, lightMapOffset, lightMapScale, lightmapBilinear,
lightmapUpscale, vertexRounding). StarWorldPainter.cpp:256-303's fullbright branch sets lightMapEnabled
and lightMapMultiplier and deliberately leaves lightMapScale / lightMapOffset / lightmapBilinear /
lightmapUpscale STALE, relying on the world shader ignoring them when lightMapEnabled=false. That is
correct today and is a live coupling between world.config, world.frag and a C++ branch, asserted by
nothing.

WHY IT IS NOT URGENT: no in-tree divergence has been demonstrated, and applyEffectParameter's dedup means
the cache is only ever the renderer's belief about a uniform IT wrote -- a fresh Effect is re-emplaced on
reload with an empty parameterValue, so the reload-staleness variant is already handled.

THREE COHERENT OPTIONS, decide rather than drift:
 (a) Extend the composite() discipline: give `world` a params struct resolved at its bind sites, so every
     bind states all seven. Symmetric with BackdropParams / LightmapParams and with contract (2).
 (b) Assert instead of restructure: a debug-only check at draw time that every declared parameter of the
     bound effect has been written this frame. Cheap, catches the real failure mode, no behaviour change.
 (c) WONTFIX with the coupling documented at world.config and the fullbright branch, on the grounds that
     one effect with one careful consumer is not worth a mechanism.

Lean: (b) then (a). (b) is the gate this campaign keeps discovering it needed; the audit's whole finding
was that unasserted rules decay. Sequence behind #139(b), the GL-state assertion pass -- same shape, same
seam, and they should be one instrument rather than two.
```

<a id="c29c1332-198"></a>

#### #198 — P-4 phases 2-4: depth-buffer architecture -- ZERO TRACE, survey before designing

status: **pending**

```
SPLIT OUT of #139 on 2026-07-26 when its Phase 1 completed. These phases were never part of Phase 1's
scope and kept #139 open on a different project.

STATUS: ZERO TRACE, unchanged since the 2026-07-25 content audit.
  `git grep GL_DEPTH|DEPTH_TEST|depthBuffer|prepass|depthAttachment -- source/` returns exactly ONE line:
  StarRenderer_opengl.cpp:80 `glDisable(GL_DEPTH_TEST)` -- the finding itself.
  GlSurface has no depth face and no depth flag: its format fields are hdr / alpha / clear / multisample /
  sizeDiv only (StarGlRenderSurface.hpp).
  #114 (R-C opaque tile z-prepass) is closed as explicitly DEFERRED/not-built, so step 2's backdrop cull
  does not exist either.
  No survey or design record anywhere: no markdown mentions "painter's algorithm", "z-prepass" or
  "opaque pass".

NEXT ACTION: do the SURVEY first. Do not start depth-buffer work off the back of #139's original framing,
which bundled it with an assertion pass it has nothing to do with. The question a survey has to answer is
whether a 2D painter's-algorithm renderer with heavy alpha blending can use depth at all without changing
output -- the campaign's own precedent (#129, built then MEASURED NULL then cleanly reverted) says a spike
that answers "do not build this" is a success.

SEQUENCING: behind #174 (motion harness, guardrail G9) like all other render code motion, and the survey
should use the new GL-state audit from #139 -- depth state is exactly the ambient-state class it now
watches.
```

<a id="c29c1332-199"></a>

#### #199 — HEADLESS-1 DEFERRED: sink-gating landed (f7609bb7); a real headless client is its own engineering effort

status: **pending**

- `f7609bb7` game: the client can stop producing the view without stopping the world
- cited in `docs/superpowers/drafts/2026-08-02-tssa-levelling-analysis.md`

```
STATUS 2026-07-26: Director scoped this OUT for now. "Building a full headless playable system is an
engineering effort unto its own right." One piece landed; the rest is not started and is not queued.

WHAT LANDED (f7609bb7) -- and it is narrower than "option (b) shipped" suggests:
  ClientRenderCallback gained `wantView`; the two VIEW sinks (addDrawable, addOverheadBar) discard when
  false. WorldClient gained setHeadless()/headless() and skips the per-entity view assembly wholesale.
  Verified on hardware: the scripted walk still moved the player deterministically (8226.800 -> 8236.483
  -> 8237.573) while captured frames reported entityDrawables = 0.

WHAT IT DOES NOT DO, recorded because the commit title reads stronger than the change:
  * It ran in the FULL client binary (links star_rendering / star_application / star_windowing /
    star_frontend) with a LIVE GL context under SDL_VIDEO_DRIVER=offscreen. The log shows it creating
    lightingGpu, lightingGpuB, envCache and parallaxCache. The renderer was alive throughout.
  * `entities=0` meant zero ENTITY drawables. Tiles, backdrop, parallax and the lightmap all still
    rendered.
  * It used the harness save (harness/storage-perf/player/555c692b...), not the Director's live install,
    and the "movement" was a frame-counted scripted walk, not interactive play.
  * Lighting was deliberately NOT gated -- addLightSource is a view sink, but the lighting thread has its
    own lifecycle and waitForLighting participates in frame timing.

THE VALUE THAT SURVIVES, independent of whether headless is ever finished: the finding that
`entity->render(&callback)` is NOT a view function. It is the entity's per-frame EMIT, and RenderCallback
carries two duties across six sinks -- addDrawable/addOverheadBar/addLightSource are VIEW, while
addParticle is SIM, addAudio is AUDIO and addTilePreview is UI. Skipping the CALL changes the world;
skipping the SINK does not. That is why the seam is at the sink, and it is the honest answer to "why does
the client have no headless expression".

WHAT A REAL HEADLESS CLIENT WOULD ADDITIONALLY NEED (none started):
  1. A starbound_headless target linking star_extern + star_core + star_base + star_game only, the way
     starbound_server already does.
  2. No GL context at all -- today it needs a working GL implementation even offscreen and would not start
     on a GPU-less machine.
  3. The rest of the view gated: tiles (the TileDrawer/TilePainter knot, #191), backdrop, parallax,
     lighting.
  4. A frame loop that does not assume a renderer -- ClientApplication drives everything through
     Application::renderer().

STILL LIVE AND CHEAP: the boundary_ratchet (ceiling 213) keeps the game layer's push-sink surface from
growing while this is parked. That is the part worth keeping warm -- it costs nothing and it means the
decision can be revisited from data rather than from memory.
```

<a id="c29c1332-200"></a>

#### #200 — ARCH-1: whole-system boundary document + arch-graph generator + gate

status: **pending**

- `7d22d2bb` docs(arch): the whole-system boundary map, generated and gated

```
Built scripts/arch-graph.py and docs/architecture/system-boundaries.md: ten diagrams of the whole system's boundaries, nine of them GENERATED from the tree and gated by the new arch_graph_fresh ctest + Gates workflow step.

METHOD DOCUMENTED (the hand-authored 10th diagram): four tests ranked by evidential strength -- (1) Refusal: the build's INCLUDE_DIRECTORIES grant lists are the authority, a #include outside the grant is a compile error, so the cross-directory graph is acyclic BY CONSTRUCTION not by discipline; (2) Severability: 12 live executables in 4 strictly-nested link shells, starbound_server proving game<->presentation; (3) Vocabulary: Root::singleton reach; (4) Duty: the cross-cutting subsystems the first three structurally cannot see.

DIAGRAM TYPE SELECTION: flowchart TD (grant lattice, transitively reduced -> the Hasse diagram); flowchart w/ -.-> --> ==> (granted-vs-spent, three states mapping exactly onto Mermaid's three native arrow weights); sankey-beta (magnitude, caveated as non-conserving); nested subgraph (severability shells); treemap-beta + table (mass); xychart-beta (reach); flowchart+subgraphs (cross-cutting); mindmap (six top-level parts); classDiagram w/ namespace (render layers -- the only place inheritance is the real relationship).

FINDINGS THE INSTRUMENT PRODUCED:
- `application` is NOT a presentation library. Granted core+platform only; a SIBLING of base at tier 2. The lattice has two incomparable T2 branches joining at rendering.
- The zero Root reads in core/base/application are a CONSEQUENCE of the grant list (Root lives in game and they cannot see it), not an achievement. L1 sovereignty is the narrower hand-enforced claim about an internal split.
- 5 granted permissions are spent zero times -> free revocations.
- game is ~44% of the engine in one directory with no sub-CMakeLists, so no internal boundary is compiler-enforceable.
- Exactly 1 of 12 render inheritance edges leaves the subsystem: TilePainter : TileDrawer (#191).
- General rule: to make a boundary real, give it a directory and a grant list -- converts a lint we maintain into a compile error we do not.

VERIFICATION: gate proven to FIRE (tampered Root count -> STALE, exit 1); cwd-independent; all 7 script gates PASS; cmake reconfigure clean with Steam/Discord pinned OFF; arch_graph_fresh registered, NoAssets-labelled, passing through ctest. Memoized reads took the gate 5.56s -> 1.05s with --check still passing, which proves byte-identical output.
```

<a id="c29c1332-201"></a>

#### #201 — CI-5: boundary_fresh was RED on Windows from the day it landed -- native path separator

status: **pending**

- `e7368000` fix(gates): boundary_fresh measured a different tree on Windows than on unix

```
ROOT CAUSE, proven not guessed. scripts/boundary-inventory.py game_files() built keys with str(p.relative_to(REPO)), which stringifies with the NATIVE separator. On windows-latest every key was `source\game\Star*.cpp` while VIEW_BY_DUTY is written with forward slashes, so nothing matched: the view-by-duty total reported 0 instead of 136 and all twenty table rows flipped from "view" to "sim". Simulated locally: 46-line diff between the Linux and Windows reports.

BLAST RADIUS: boundary_fresh failed on the Windows job and passed on the four unix jobs, so Build has been red on integration for three consecutive runs (30194960202, 30195729180, 30198588776) since the gate was registered. It is NOT caused by #200 -- arch_graph_fresh PASSED on Windows (2.84s) in the same run, and the two earlier red runs predate that commit.

FIX: p.relative_to(REPO).as_posix(). board-export.py already used this idiom; boundary-inventory did not. config-lint.py normalised to match, flagged in-comment as cosmetic there (its `rel` only reaches an error message that a passing run never prints, so it could not change a verdict).

VERIFIED: Linux output byte-identical after the fix (the committed doc still matches, no regeneration needed); PureWindowsPath demonstration shows old code misses VIEW_BY_DUTY and new code hits; all 7 script gates pass.

THE LESSON, which is the same one as #192 and the oracle-vocabulary trap: a measurement that depends on which machine ran it is not a measurement. Two of three such defects in this campaign have now been platform-conditional gate output.
```

<a id="c29c1332-202"></a>

#### #202 — RI-2: two L1 files were unclaimed by the layer table -- the #137 failure, second instance

status: **pending**

- `52360eee` fix(render): claim the two L1 files the layer table never looked for

```
FOUND BY A DIRECTOR QUESTION about diagram 5 of the boundary doc ("is the rendering box where the whole L1/L2/L3 decomposition lives?"). Answer: no -- and chasing why exposed that StarTextureAtlas.hpp (418 lines) and StarRenderDiagnostics.hpp (77) are L1 by duty, sitting in source/application, claimed by no layer and therefore invisible to every count render-inventory.py produced. Identical to #137's finding for three helpers in source/rendering.

WHY IT SURVIVED A SECOND TIME: the unassigned-file check that exists precisely to force this question only ever globbed source/rendering -- the directory the FIRST instance was found in. A check scoped to where the last bug was is a check that finds the last bug.

THREE CHANGES + ONE G10 CORRECTION:
1. Claimed both into LAYERS L1. L1 goes 8 files/3796 lines -> 10/4291. Moves NO coupling metric: zero Root reads, zero GL calls, zero telemetry handles, so the gated residual block is byte-unchanged and 17 is still 17. (StarRenderDiagnostics names OpenGlRenderer and Telemetry:: only in comments -- code_only() removes both.)
2. Extended the unassigned check to source/application via APPLICATION_NON_RENDER, an explicit partition of that mixed directory (10 claimed + 15 declared non-render = all 25 files). A new file there now forces the question "is this render?".
3. arch-graph.py namespace labels carry the owning library ("L1_substrate_in_star_application"), and docs/architecture/system-boundaries.md section 7 now states that the treemap shows DIRECTORIES, not layers -- the render decomposition is not contiguous in the tier lattice: L1 is below `game` at T2, L2/L3 above it at T4.
4. G10: docs/render/layer1-architecture.md section 2 updated -- both files also joined the layer1_layering fence, which now holds six files rather than four, so the doc's "four sovereign components plus one shared texture primitive" was understating what it enforces.

VERIFIED BOTH NEW CHECKS FIRE: a probe file in source/application reported as unassigned; an OpenGlRenderer reference appended to StarTextureAtlas.hpp failed layering-lint with the exact file and line. Both probes removed, tree clean. All 7 script gates pass, all 9 NoAssets ctests pass.
```

<a id="c29c1332-203"></a>

#### #203 — ARCH-2: shape + cohesion tests, actions reordered by value, and a recursive-scan defect

status: **pending**

- `07d248f4` docs(arch): write the subsystem-leaving inheritance edge derived-first, so star_game renders outside
- `cfa7603b` docs(arch): section 9 measured the wrong graph -- game's declarations are acyclic
- `38a147d9` docs(arch): the presentation tier is three duties, and two of them blur
- `80084408` docs(arch): full-depth directory tree, and the four directories it revealed are unmeasured
- `c3ef055d` docs(arch): a depth-2 directory tree -- the one view of containment the document lacked
- `7fdae50d` docs(arch): define "grant" -- a coined term used 24 times and never explained
- `98b6692a` docs(arch): cohesion is orthogonal, not downstream -- and name the axis this ladder lacks
- `5e893499` docs(arch): a shape test, a cohesion test, and the recommendation they falsified

```
Director approved items 1-3 from the qualitative self-review of the boundary document.

ITEM 1 -- ACTIONS REORDERED BY VALUE. Was cheapest-first, which put "revoke 5 unused grants" at the top (it forbids things nobody does: no behaviour change, no coupling removed, no work enabled) and buried the only item that matters. Now value-ordered with cost stated separately, and the hygiene item is labelled as hygiene.

ITEM 2 -- THE `game` RECOMMENDATION WAS WRONG, NOT UNDERSTATED. It said "not a refactor -- a CMakeLists.txt per cluster and a grant list each... costs no behavioural change at all". Measured: source/game has 226 of 264 translation units in ONE strongly-connected component (86%). There is no partition. A sub-directory cannot take a grant list while it has mutual includes with its neighbours. Rewritten to say the work is breaking the cycle, that it cannot be done byte-identically, cannot be verified by existing oracles, and that the first deliverable is a feasibility study.

ITEM 3 -- TEST 5 (SHAPE) ADDED, and it is a SECOND-STAGE question, not a fifth rung: tests 1-4 ask whether a boundary exists, shape asks whether it is any good. Metric = for a data-dominant struct crossing a tier, how many members the consumer actually reads. First formulation was FALSIFIED in testing (WorldRenderData is 21/23 against source/rendering -- fine at directory granularity); the defect is per-consumer: WorldPass 6/23, TilePainter 3/23 while the orchestrator reads 20/23. Filters carry the meaning -- classes excluded (low fit there is encapsulation working; without the filter it reported 5 false findings on `Object`), same-tier excluded. The metric also finds WELL-shaped crossings (RenderTile 16/16), so it discriminates rather than only complaining.

TEST 6 (COHESION) ADDED, unplanned, and it is the most valuable output: per-directory largest SCC as a share of the directory. game 86% and windowing 84% are single blobs -- unsplittable. core 3%, rendering 8%, frontend 12% are splittable. THE RENDER DECOMPOSITION SUCCEEDED BECAUSE `rendering` WAS ALREADY 92% ACYCLIC -- feasibility was a property of the ground, not the plan. This rehabilitates the hand-built instruments: for the blob directories they are not a workaround for a cheap mechanism nobody used, they are the ONLY enforcement available.

DEFECT FOUND AND FIXED: arch-graph.py's files_in() walked only the top level, so every count in the published document excluded source/game/{interfaces,items,objects,scripting,terrain} -- 162 files, 19,230 lines, and 119 Root::singleton reads. game corrects from 338 files/96,002 lines/Root×521 to 500/115,233/Root×640. Found only because the cohesion prototype used os.walk and disagreed with the generator (264 units vs 172). Two implementations of one measurement disagreeing is the only reason anyone looked.

ALSO: VENDORED_SUBTREES declared (extern/{curve25519,fmt,lua}, application/discord) -- recursing tripped the ambiguous-basename guard on core.h, correctly. And boundary-inventory.py's VOCAB said WorldRenderData was "the 35-member frame view model"; the tree says 23, and the wrong number had propagated into the generated boundary doc. Number removed -- it is measurable, so the shape block states it.

VERIFIED: both new blocks proven to fire under tamper; all 7 script gates pass; 9/9 NoAssets ctests pass.
```

<a id="c29c1332-204"></a>

#### #204 — TSSA (#204): target-state architecture spec — structure DONE, content is the remaining work

status: **in_progress**

- `42b60f5d` spec: F4 was overclaimed, and the document warned about exactly this
- `298bdd16` spec: eight wrong numbers, every one re-measured before changing
- `1b5f6f00` gates: the excludes facet must agree with the register — and it did not
- `c359fc30` spec: section 14's rules reach the instrument table, including the one with no instrument
- `63248364` spec: section 14 closes its eight audit findings
- `1a1c89d8` spec: section 18's owed list is generated, and the two documents' relationship is finally stated
- `6a53b039` spec: all 41 components derived — 246 of 246 facets
- `5388cbd0` spec: derive the ten libraries, including `game`
- `b2db3904` spec: derive the two foundations and eight contracts
- `7dc30a06` gates: count whether a component was REASONED FOR, not merely declared
- `b0591b19` spec: the last two composition diagrams, and what section 10 actually is
- `299e8b47` spec: celestial was never a 732-line monolith
- `0acb3c7b` spec: sections 8-17 still spoke the retired axiom vocabulary
- `a71484af` spec: N1 gains the clauses it was already being cited for; seven principles become two
- `706e970f` spec: not one of the six axioms survived verification
- `5d6f5b97` spec: section 6 closes six audit findings against itself
- `94a3eec4` spec: section 1 says what OpenStarbound is, at depth
- `e0a6cdec` artifact: render the parts, and stop hand-writing the design state
- `ca7e0011` spec: renumber 74 cross-references the gates could not see
- `a51b3659` spec: restructure to the ratified 18-section TOC
- `364519c3` survey: capture Director intent on the TSSA's purpose
- `2ed13794` spec: the device row has no history, and says so
- `a3eaa447` fix: spec_measures depended on whether the machine had compiled
- `6c30f710` ci: regenerate the taxonomy block, and stop checking a subset of the gates
- `47e33692` spec: rename to "Target State System Architecture" [Director]
- `6539382d` scripts: put the artifact renderer in the repo, where it should have been
- `3751ccaa` spec: split the arrow, so the Law of One means what it says [Director-approved]
- `f88ff14d` spec: the Root count omitted the one directory that matters
- `351e7696` spec: resizeSignal reached nobody, and no gate could say so
- `ad6426e7` spec: the vocabulary move list would not have compiled
- `0fe309ee` spec: generate the honesty ledger, which was the least honest table in it
- `c1e89fdc` spec: the four-line runtime summary drew the edge the graft rule rejected
- `d15a3816` spec: measure the load-bearing numbers instead of remembering them
- `56b9a1ca` spec: D1/D2 state the actual purpose — a full target-state refactor
- `6956aab7` gates: PHASE 0 -- read the prose, which sixteen gates never did
- `01694c2c` spec: ZONEs become four directories, and the design turns out to be perfectly layered
- `bb04e8b5` fix: spec-consistency had an unterminated string literal — committed red
- `91d18ec6` spec: adopt `storage` — and its Lua dependency independently validates `script`
- `fbe4c41b` spec: generate the "what drives what" table — the register never said what starts anything
- `1cf8987d` scripts: one reader for the spec's tables — three parsers gave three different counts
- `50f09efe` spec: the SDL host owns a window, not a graphics API — swapTick moves to `gpu`
- `94c1af57` spec: FREE conflated a paced loop with a busy loop; headlessLoop's pacing is wiring
- `24c79ca8` spec: five clocks in two pairs, and CARDINALITY becomes the fourth runtime axis
- `dc4dc3e7` spec: Sections 1 and 2 reconciled, Section 6 designed — five claims are one experiment
- `4a333db7` spec: the runtime projection catches up, and D7 locks the framing that let it fall behind
- `3ac919ed` spec: adopt `content` — Root becomes private, and the seam already existed
- `4b4982bd` spec: adopt `net` + `script`, and find the thing that actually blocks splitting `game`
- `736030b7` spec: `client` -&gt; `participant`, and the per-composition diagrams gain their clocks
- `d8b8d550` spec: `colocation` — the client stops containing a server
- `cb93b33f` spec: adopt `interaction` + `client_agent` — a client is not one thing
- `a588e8ab` spec: client_sdl_gpu had no edge to `client`, and the gate skipped it silently
- `8f9e1ce3` spec: celestial becomes a CONTRACT, audioTick moves to mixing, palette derived
- `d7708b7a` arch: link-time grant gate, and the audio stack it proved was missing
- `e0b827df` spec: adopt worldgen + world_gen, and correct the utility evidence
- `beb51779` docs(spec): correct the world_sim utility claim -- one of three, and the other two are a third primitive
- `8523181d` docs(spec): four halves, and D9 -- what ticks must not depend on who is watching
- `a6c7dba1` docs(spec): `authority` -&gt; `universe`, because World means one planet here
- `a513ef6f` docs(spec): tier 2 -- an entity no longer knows how it looks; `replica` renamed to `view`
- `618f99e3` docs(spec): split `game` into domain, authority and replica -- my "not separable" was wrong
- `3f07aeec` docs(spec): a method for suspicious links -- and it killed the repair I would have proposed
- `b03248c5` feat(gates): one compile-time diagram per composition, derived from the grant table
- `7155637b` docs(spec): D8 -- a co-located seam is an optimisation, never a cheaper contract
- `a1911e89` docs(spec): the axis is who HOSTS the universe, not single-player vs multiplayer
- `88bc52f2` docs(spec): the client could not reach the server's universeLoop -- multiplayer was undrawn
- `7482dc5f` docs(spec): the netcode return is not a return -- and it is seam 1's precedent
- `41047638` docs(spec): ORDER on runtime edges -- and the edge regex was dropping edges silently
- `9882a77a` feat(gates): dedup-measure -- the north-star claim finally has a number
- `1da322da` docs(spec): the tick is a root -- cadence by reachability, and the dedup measure
- `77669e22` docs(spec): WIRING and SIGNAL kinds; the frame has five phases, not three
- `1a0f118f` build(gates): spec_consistency -- both projections agree, and the graft holds
- `d14f7f88` docs(spec): the runtime taxonomy, the graft rule, and the approved host fix
- `c9c2770f` docs(spec): split run time out of Section 4 into its own Section 5
- `739b4aba` docs(spec): add the execution graph -- the runtime view, clustered by thread
- `14b941a6` docs(spec): seam 1 is bidirectional -- input returns; retract 'nothing returns'
- `2e01529d` docs(spec): separate coherence from anchoring from correctness; retract an overclaim
- `b3647d73` docs(spec): state that the diagram is compile-time only; fix three defects it hid
- `6d181196` docs(spec): rename the gpu contract to Device; state what crosses each seam
- `151b8d56` docs(spec): name the client's loop elements by time domain; fix the inventory's blind spot
- `ebfe4547` docs(spec): container components drop their duty line
- `5538062c` docs(spec): elements nest inside their owning component
- `22c5e6d5` gates(loop-inventory): the loop count becomes a measurement
- `02f2d8d6` docs(spec): add `server`, and stop conflating it with a headless client
- `ac6cafd1` docs(spec): `==&gt;` implies `--&gt;`, and implements is measurable
- `e7f5ab49` docs(spec): the diagram distinguishes implements from consumes
- `8ffffc5e` docs(spec): section 4 becomes target state; the delta moves to section 9
- `cc9a5532` docs(spec): headless client -- define `scene`, and stop contradicting ourselves
- `2f42bca7` docs(spec): headless client -- the vocabulary assessment resolves
- `f053671f` gates(grant-sweep): the grant table gets an instrument
- `55da339e` docs(spec): headless client -- host backends need `platform` to compile
- `a6342adb` docs(spec): headless client -- hosts become symmetric components
- `376eb9c9` docs(spec): headless client -- the target-state loop model
- `d1671a77` docs(spec): headless client -- the *Loop / *Tick rule, and audioTick
- `e1107569` docs(spec): headless client -- scene, ALTITUDE, and nothing approved
- `9823f106` docs(spec): headless client -- host becomes a sovereign component
- `c71a06f3` docs(spec): headless client -- clusters are ZONEs, strictly named
- `cd43f4ba` docs(spec): headless client -- arrows point at dependencies
- `e635efbf` docs(spec): headless client -- ENTRYPOINT is one token, colour is by kind
- `f0cb85d9` docs(spec): headless client -- name, KIND, duty per component
- `4534874d` docs(spec): headless client -- drop the section symbol
- `ab53f943` docs(spec): headless client -- two seams, one component per box
- `998d52fe` docs(spec): headless client -- the naming register, section 4 proposed
- `8fc507c0` docs(spec): sovereign headless client -- WIP design, section 1 approved

```
Branch integration, pushed through ca7e0011. Spec at docs/superpowers/specs/2026-08-01-target-state-system-architecture.md, 4607 lines, 18 sections / 4 parts, 41 components, 17 gates green.

SETTLED: purpose (survey envelope surveys/tssa-purpose-survey.md) — the TSSA is the ratified upper layer of an A8 Sovereign Onion; design specs conform to it, not source; primary consumer is agentic technical designers producing implementation plans; seals without freezing; exhaustive completeness in machine-parseable structure; compression closed permanently. mission-kit/axioms canonical, any-system in force for both architecture and architect. TOC ratified and implemented.

REMAINING: content. Part I is 312 lines of placeholder against section 10's 1691. Section 10 is the DEPTH STANDARD per Director — everything else is under-written, not section 10 over-written.

Sub-tasks: #205 spine, #207 levelling pass, #208 aggregate review.
```

<a id="c29c1332-205"></a>

#### #205 — TSSA-0 DONE: Part I written, 312 -&gt; 735 lines; six axioms verified and five became decisions

status: **completed** · blocks: #206, #207

- `9c8eb88f` draft: N3 is composition, not composable clients
- `f7e61c8e` draft: a server has no participant, and a player is not one either
- `947a34b1` draft: the TSSA frame — axioms, north star, principles

```
All six sections of Part I written. Gates 17/17 throughout. Commits 94a3eec4, 5d6f5b97, 706e970f, a71484af.

THE RESULT THAT MATTERED: ten read-only agents tried to falsify the six claimed domain facts. Four FALSE, two TRUE-only-when-narrowed, none survived as written. The test now stated in section 2: if an implementation can violate a claim and still be Starbound, it is a DECISION, not a fact. Five moved to section 5 as D9-D13 with costs named — D10 costs input latency, D12 makes an adaptive fidelity governor illegal, D9 must REPLACE four uses of client view rectangles.

Section 2 = adopted canonical axioms by applicability tag + four verified domain facts (devices optional, physical time local, steps discrete, content INSTANCES opaque).
Section 3 = N1 gained labelled clauses N1.a-d; a-c had been cited 6x and defined nowhere. N1.d absorbs the chattiness constraint.
Section 4 = seven principles to two (P1 dependencies declared, P2 placement is wiring); five were restatements of A3/N1.a/D9/A1+A2/A8.
Section 6 = six confirmed audit findings closed.

GATE VOCABULARY FIXED TWICE: CITES accepted A1-A6 (retired axioms) and P1-P7 (retired principles). Now A0-A14/F1-F4/N1-N3/P1-P2/D1-D13, proven to fire on injection.

Unpushed at completion: 6 commits.
```

<a id="c29c1332-206"></a>

#### #206 — TSSA-1 DONE: 18-section TOC derived, ratified, implemented, cross-refs renumbered

status: **completed** · blocked by: #205 · blocks: #207

- cited in `docs/superpowers/drafts/2026-08-02-tssa-levelling-analysis.md`
- cited in `docs/superpowers/drafts/tssa-frame.md`

```
Director ratified 2026-08-02. Derived from four constraints rather than chosen: A3 Law of One over sections, A4 Expansionist Bias, A8 Gated Ascension (order is a dependency DAG), and the approved spine. Implemented in a51b3659 (byte-identical move, all 97 retained blocks proven verbatim, 4582 = 4516 + 66 dropped) and ca7e0011 (74 cross-refs renumbered by reading, 4 cross-document refs deliberately untouched, 2 pre-existing "Section 3"-means-Decisions defects surfaced). 17/17 gates green. Old sections 1/2/3 dissolved as ticket-era context-finding.
```

<a id="c29c1332-207"></a>

#### #207 — TSSA-2: review closed; levelling pass — §3 DONE, §§1/2/6 assessed level

status: **in_progress** · blocked by: #206, #205 · blocks: #208

- `337f5494` spec: the CONTRACT sweep -- 25 sites read and decided, ratchet 25 -&gt; 0
- `d2eadae4` spec: the KIND rules said "foundation types" -- two INTERFACEs already broke it
- `9442b154` spec: two rules the gate already contradicted -- and the gate was right
- `248b9a74` docs: file the 44 upheld audit findings; 57 of 103 were refuted [#207]
- `ed5e7125` spec: Section 7 defines all six KINDs -- it defined five, and one was retired
- `cdea71c1` gates: table_census -- because I answered "how many tables?" wrong three times
- `de57756e` spec: F2's evidence clause described a policy, not the code -- and the code is better
- `82955390` gates: N1.d is countable, and the metric was in the document the whole time
- `ef074409` board: export -- Section 1 reuse rule, shared-words gate [#207]
- `41b7adfd` spec: Section 1's rule was about REUSE, and now says so -- and is checked
- `3ee6021d` docs: file the levelling analysis as a WORKLIST, not a findings register
- `170a17cb` gates: BARE_GOAL read the shape of a citation, not its meaning
- `a0e43c2c` board: export -- Section 3 levelled, three new prose-claims verdicts [#207]
- `eab0b4da` gates: a warrant must name a clause, and the check found two I had missed
- `5f2933ad` spec: repoint every N2 and N3 warrant at the clause it actually makes
- `a96a3575` spec: level Section 3 -- N2 and N3 get clauses, because their citations already had them
- `6d5100fb` gates: correct the containment row at its generator, and a false green
- `78a8a4b5` gates: an instrument the document names must be one that runs
- `c16a0f07` board: export — three decisions closed, 45 components, transport added [#207]
- `51415bfe` gates: the UNANSWERED check existed and never ran
- `f5faf4cf` spec: CONTRACT was two kinds, and the split found a missing component
- `5db63949` spec: `platform_null`, and a contract granted 18 times and called by 4
- `b1b34d9a` spec: RenderCallback is the EMIT surface, and the scene is what a painter READS
- `550a3b5e` spec: a rate with a number, a reason and no element
- `b976ae79` spec: Section 11 counted five clocks and then said four
- `8bc6590e` board: export #207 closure state — 38 review findings closed, 3 tensions recorded as owed [#207]
- `12f08312` gates: P2 had no instrument, and Section 4 said it did
- `0c96063d` spec: Section 16 was blocking a register row it had already unblocked
- `b86a1ad0` spec: the last two first-person passages become properties of the model
- `4011f1af` spec: `celestial` is placed by its duty, and two aspirations become pass conditions
- `8e742ae7` spec: migration status leaves Part II, and Section 18 stops being a TODO list
- `176d60be` spec: the document stops narrating its own drafting
- `45af8da3` spec: F2 verified at the width it is cited, and ten dated changelog clauses removed
- `a58ef402` spec: the contract that could not answer, and two the register cannot satisfy
- `3f767902` spec: the payload has a name, and one seam needed two rows
- `f6744d04` spec: a handoff delivers, it does not drive -- and three rosters disagreed
- `6184248e` spec: the Lua surface had four writers and they disagreed four ways
- `7952fb50` spec: two link-count conventions, and a grant the register does not hold
- `669dbc29` gates: a whitelist entry that matches nothing is a defect, not a no-op
- `fdad9c5d` spec: five references that resolved to the wrong thing
- `b8f61e9f` spec: across a seam — failure, back-pressure, lifetime, concurrency, trust
- `b69cd406` spec: Obligations — who this serves, what it must be good at, what was not chosen
- `62fe4dbd` gates: catch a dangling section reference before the restructure creates 72 chances

```
Adversarial review wztsat52f (38 findings) CLOSED. Three Director decisions decided+implemented; the generalised null-object rule split CONTRACT into INTERFACE/VOCABULARY (45 components). Instrument-claim mechanism SHIPPED: prose-claims gained UNREGISTERED_GATE, MISSING_SCRIPT and BARE_GOAL; 15 injections, all proven to fire.

LEVELLING PASS (§10/§16 depth standard = fixed facet shape per unit):
- §1 LEVEL (role/player-participant/content tables, 2-5 facets each, consistent per unit)
- §2 LEVEL (axioms 3 facets, domain facts 3 facets)
- §3 WAS THE OUTLIER, NOW DONE — N1 had 4 clauses x 3 facets and 33/53 clause-qualified citations; N2 and N3 had none and 0/32. N2 gained 4 clauses (sovereign: N2.a one writer per fact, N2.b no component is another's bottleneck; comprehensible: N2.c no unneeded history, N2.d a defect reproduces). N3 gained 3, found by reading its own 26 citations (N3.a two implementations, N3.b nothing linked unasked, N3.c no component holds the list of what composes with it). All 28 warrants repointed; BARE_GOAL gate now keeps it true.
- §6 ACCEPTABLE (budgets 3 facets, owed 2, constraints 2, observability 2) — weakest is the constraints table at 2 facets with no failure-symptom column; candidate for a later pass, not a defect.

Found by the levelling, not by a gate: `platform` was warranted N2 while its own backend `platform_pc` was warranted N3 for the same property (both are N3.b now). D13, the `gpu` rejected facet and the `script` rejected facet also moved between goals.

Artifact republished at eab0b4da (66b9715a). 18/18 gates green.

REMAINING under #207: `celestial`'s vocabulary/lookup split (blocked on dividing StarCelestialDatabase.hpp).
```

<a id="c29c1332-208"></a>

#### #208 — TSSA-3: register fields, aggregate review, re-home the anchoring gates

status: **pending** · blocked by: #207

```
Blocked by #207.

REGISTER FIELDS (decided by the survey, not yet done). Q6 machine-parseable structure means a rule no instrument can read is not a rule. The five audit findings where a normative rule points at a register field that does not exist are resolved by ADDING the fields, not softening the rules:
  - consequence (the fault averted) on the component register — A4 Load-Bearing Context requires Mechanics + Rationale + Consequence; warrant supplies only Rationale
  - bound / overflow-policy / observer on every HANDOFF edge
  - timeout on every cross-machine edge
  - validator on every inbound seam payload
  - the corresponding rows in section 15's instrument table
Then UNWARRANTED becomes a three-part completeness gate.

COMPLETENESS STOPPING RULE (survey flag F5). "Exhaustive completeness" has no terminator, so the "finished" state Q5 asks for is unreachable. Define completeness as a closure property over the registers so it is computable rather than felt.

AGGREGATE REVIEW. Approval is aggregate only.

GATE RE-HOMING. grant_sweep, link_sweep, spec_measures, dedup_measure, host_api_neutral, boundary_ratchet currently anchor on current-state measurements inside a target-state document; they belong to section 17's delta.

NOT IN SCOPE: the design-conformance instrument (survey flag F4). Director scoped it out 2026-08-02 — "we don't have a design spec yet, that tooling is currently out of scope." The obligation is still stated in the document; what must be said out loud is that nothing enforces it yet.
```

### Store `6c8fc9cc-f25d-49cb-9d3e-7a1bcae0c776`

<a id="6c8fc9cc-4"></a>

#### #4 — L1-FIX: the four false comments, the makeDoubled face leak, the GlPass field bag, and the per-draw glTexParameteri hoist

status: **pending**

```
From the hostile 58-agent architecture audit. Full brief: docs/render/architecture-assessment.md §6. Ranked; do B first.

B (DO FIRST -- doc only, ~1hr, highest value per minute). FOUR FALSE COMMENTS IN THE SOURCE:
  * hpp:443 states bindTarget "binds a TARGET and it writes an EFFECT-PROGRAM UNIFORM. That single fact is the
    entire argument for this component." bindTarget issues ZERO glUniform calls -- the write was deleted in
    F3b.1 as provably dead, the .cpp documents the deletion, and the header was never updated. The header
    states the component's reason to exist and it is false.
  * hpp:269, hpp:418, cpp:1034, cpp:2182 all name switchGlFrameBuffer -- a function that does not exist.
  * hpp:303-306 "the enclosing renderer needs the raw faces to allocate, resize and clear them" -- it does
    not; resize/clearFaces/makeDoubled are public.
  * hpp:400 the friend's justification ("the oracle reads pixels back out of a target by name") -- the oracle
    does that entirely through the public find(). A non-sequitur guarding a hole nobody takes.
  The file currently teaches an architecture more confidently than it implements one. Fix that first.

A (a REAL latent bug -- its own commit). makeDoubled() calls allocateFace, which glGenFramebuffers into
  faces[1].id and THEN can throw on glCheckFramebufferStatus. On that throw faces[1] holds a live FBO and
  texture while `doubled` is still false -- and the destructor loops `doubled ? 2u : 1u` and FORGETS IT. That
  is upstream's altId leak, resurrected inside our own resolver, whose destructor comment says "the second
  face cannot be forgotten." `bool doubled` is a biconditional the type does not enforce. Idiomatic fix:
  Maybe<Face> secondFace -- a pattern this file already uses (Maybe<Vec2U> overrideSize). Then existence IS
  the fact.

C (pure-structural, ~30min). renderGlBuffer issues 6 GL calls per effect texture where upstream issues 2 --
  four glTexParameteri we added, sitting INSIDE the vertex-buffer loop although their args depend only on
  (effect, texture), never on vb. Re-issued per buffer, every frame. Hoisting above the vb loop is
  byte-identical in GL state at every glDrawArrays. MEASURE IT; do not claim a win without a number.

D (pure-structural, ~2hr; overlaps residuals item 2 -- do together or do the bind key first). GlPass has ZERO
  `private:` keywords and takes 32 raw field pokes from the renderer, and hand-rolls the inverse of
  bindTarget at two sites that DO NOT AGREE with each other. Give it a private section and a
  GlPass::unbind(Vec2U). Until then it is a namespace with a bind() attached, not a component.

E (upstream). Only postprocess-passes is genuinely liftable (one token). shader-compile-recovery's patch is
  written against our ALREADY-REFACTORED loadEffectConfig -- its upstream form is unwritten work, not a
  cherry-pick. Say so in the PR or write it against upstream first. AND FETCH UPSTREAM BEFORE FILING: our ref
  is the fork point, ~83+ commits stale.</description>
<parameter name="activeForm">Fixing the false comments, the face leak, and the GlPass field bag</parameter>
</invoke>
```

