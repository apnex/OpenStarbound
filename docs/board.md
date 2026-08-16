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

**210 tasks** across 2 store(s): 2 in_progress, 35 pending, 173 completed

- `c29c1332-648a-42c6-87f0-1a6f14884fb0` — 209 tasks, ids 64–273
- `6c8fc9cc-f25d-49cb-9d3e-7a1bcae0c776` — 1 tasks, ids 4–4

---

## Next

Ranked work, in order. **The rank lives on the task** (`metadata.rank`), not in this file and
not in the page — one source, three renderings, so a reprioritisation cannot leave any of them
disagreeing. Set with `TaskUpdate(metadata={"rank": N})`; clear with `{"rank": null}`.

| # | rank | id | task | startable |
|---:|---:|---|---|---|
| 1 | 1 | `#273` | AXIS-SUBSYSTEM: twelve in-process keys reach every leg and none reach the joined axis, so no matrix run can a… | ready |
| 2 | 2 | `#269` | LIGHTING-WHOLE: "lighting CPU" is not any owner's total — three keys under two owners each hold part of it, a… | ready |
| 3 | 3 | `#270` | LIGHTING-REDERIVE: #168's closure and #170/#217's border percentages were measured against a denominator now… | blocked by #269 |
| 4 | 4 | `#272` | DRIVE-RESIDUAL: a quarter of cpu_cost is in none of the seven parts, and switchEffectConfig submits inside an… | ready |
| 5 | 5 | `#237` | MEASURE-CLIENT: a fifth entrypoint — real client, real GPU, no window, owns its fixture and its storage | ready |
| 6 | 6 | `#266` | DESC-RATCHET-WIDTH: the ratchet counts MetricDesc SITES, so a descriptor-free handle is still uncounted — and… | ready |
| 7 | 7 | `#252` | TEL-EXTRACT: everything that WRANGLES, PROJECTS or CONSUMES metrics moves to metrics/ — ~390 lines, of which… | ready |
| 8 | 8 | `#250` | GM-3 STREAM: the single sovereign API surface — one reader interface, one sample type, a LIVE transport that… | ready |
| 9 | 9 | `#251` | GM-4 DISJOINT: GL_GPU_DISJOINT_EXT has never been checked, and this GPU clocks 933-2350MHz — every historical… | ready |
| 10 | 10 | `#261` | HANDOVER-STALE: #204's resume document is 167 commits behind and every §2 figure reads wrong | ready |
| 11 | 11 | `#208` | TSSA-3: register fields, aggregate review, re-home the anchoring gates | ready |
| 12 | 12 | `#260` | TSSA-PLATFORM-DISSOLVE: a ratified five-stage thread, stages 3/4/5 not started, on no board task | blocked by #261 |
| 13 | 13 | `#204` | TSSA (#204): target-state architecture spec — structure DONE, content is the remaining work | ready |
| 14 | 14 | `#268` | LEVER-FLOOR: 1/2/3/4/5 DONE — only "raise n" remains, and it costs ~1.7h not the 8 min I first wrote | blocked by #273 |
| 15 | 15 | `#262` | BOARD-TAIL: apply the 2026-08-08 audit's pointer corrections to 21 dormant tasks | ready |

> **6 completed task(s) still carry a rank** (#171, #245, #263, #264, #265, #271). A rank is a claim about
> what comes next, so a finished item holding one is stale — clear it with `{"rank": null}`.

---

## Integrity

A self-check, so the drift this file exists to prevent is *visible* rather than something
someone has to go and discover. It is the same discipline as the render oracles: a check that
reports but does not surface is not a check.

**Commit ids cited in task text:** 227, of which **41 resolve to nothing** in either repository.

**Descriptions normalised on export: 27.** The task harness has, on these, appended its
own closing markup (`</description>`, `</parameter>`, `<parameter name="activeForm">…`) into
the stored description. It is stripped at render time rather than by rewriting the store —
non-destructive, self-healing, and the store belongs to the harness. Counted here rather than
laundered silently: a normalisation nobody can see is one nobody can notice is wrong.

That is expected and mostly harmless: TWO history rewrites destroyed these ids while preserving every byte of content — the 2026-07-19 whole-fork reorg, and an earlier one around 2026-07-18 that rebuilt the 2026-07-14 stretch of `dev/upstream-merge`. What matters is not that an id is dead but whether anyone can still say what it *was*. `docs/board-anchors.json` answers that, id by id:

- **22** — re-anchored to a live commit
- **9** — a deployed-binary MD5, never a commit
- **3** — an A/B render frame hash, never a commit
- **3** — dead, with no live equivalent that could be defended
- **4** — NOT YET INVESTIGATED

**Unexplained ids: 4.**  ← investigate these; they are citations nobody can resolve.

| Task | Unexplained ids |
|-----:|:----------------|
| [#199](#c29c1332-199) | `555c692b` |
| [#207](#c29c1332-207) | `66b9715a` |
| [#214](#c29c1332-214) | `471488eed310861f` |
| [#254](#c29c1332-254) | `14904c73` |

**A caveat the anchors carry, and the reason they are not just a lookup table:** 22 of the re-anchored commits are *not ancestors of* `integration`. They survive only on `dev/upstream-merge` / `reorg/tooling`. On `integration` the whole Layer-1 arc is one squashed commit, `083c6340`. So citing the fine-grained commit alone is misleading in a second way, and each anchor records the HEAD carrier as well.

Mappings resting on message-matching rather than a direct id link were sent to an adversarial auditor instructed to refute them: **7 audited, 3 overturned** to `unresolvable`. A wrong anchor is worse than an absent one — it is authoritative-looking and points at the wrong commit, which is the exact failure this file exists to remove.

**Completed tasks citing no commit and no doc:** 85 of 173.

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
| [#70](#c29c1332-70) | `c29c1332` | done | UniqueEffect Lua-context churn optimization (~21% of exploring WST) | — | `board-audit-2026-08-08.md` `board.md` |
| [#71](#c29c1332-71) | `c29c1332` | done | L1: reserve() callback maps before registerCallback storm | — | — |
| [#72](#c29c1332-72) | `c29c1332` | done | L3: move-not-copy in addCallbacks | — | — |
| [#73](#c29c1332-73) | `c29c1332` | done | StatusEffectChurnBench micro-bench (L1/L3 clean A/B) | — | — |
| [#74](#c29c1332-74) | `c29c1332` | done | L2: Proto-cache engine + T1/T4 tests (core_tests) | — | — |
| [#75](#c29c1332-75) | `c29c1332` | open | L2 dense-workload magnitude + verify toggle fix live (opportunistic) | — | `board-audit-2026-08-08.md` `matrix-prereq-ledger.md` |
| [#76](#c29c1332-76) | `c29c1332` | done | Build-window: L2 default-on + C++ fallback consistency (flags KEPT) | — | — |
| [#77](#c29c1332-77) | `c29c1332` | done | Move 1: Entity de-RTTI via entityCast virtual-accessor downcast | — | — |
| [#78](#c29c1332-78) | `c29c1332` | done | Follow-up: extend entityCast to residual per-candidate cast sites | — | — |
| [#79](#c29c1332-79) | `c29c1332` | done | Collision/movement cluster investigation (~12% WST, Move-0 result) | — | — |
| [#80](#c29c1332-80) | `c29c1332` | done | Build collision levers (gated: user OUT of game) | — | — |
| [#81](#c29c1332-81) | `c29c1332` | done | L2 collision arena (~2.55%) — needs movement verification harness first | — | — |
| [#82](#c29c1332-82) | `c29c1332` | open | L2 collision arena — optional live A/B confirmation + future terrain test-harness | — | — |
| [#83](#c29c1332-83) | `c29c1332` | done | L-WIND-A: gate Plant wind computation to slave/render branch (~1.85% dead store) | — | — |
| [#84](#c29c1332-84) | `c29c1332` | open | L-WIND-A: server profile now EXISTS (#175); measurement attempted and REFUSED by the new fingerprint — needs an in-proc… | `91cd1ef4` `35808327` | — |
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
| [#130](#c29c1332-130) | `c29c1332` | **active** | Base In A Box — Reforged: sovereign mod fork (scan/print/dup) | — | `matrix-prereq-ledger.md` |
| [#131](#c29c1332-131) | `c29c1332` | done | GL_INVALID_VALUE ROOT-CAUSED AND FIXED: inactive vertex attribute location -1 fed to a GLuint index | `e02d4484` `ba0d22ef` `84203421` `32b8f849` `7b15c880` `1df96d68` | — |
| [#132](#c29c1332-132) | `c29c1332` | done | Idle-GPU floor investigation: profile static-scene per-pass GPU cost (base/ship) → floor-reduction levers&lt;/subject&g… | — | `board.md` |
| [#133](#c29c1332-133) | `c29c1332` | done | FBO-2 RE-AUDIT DONE: items 2+3 closed (3 fixed in a4106470); item 1 survives as a named hazard -&gt; #197 | `a4106470` | `board.md` `2026-07-14-render-surface-subsystem-design.md` |
| [#134](#c29c1332-134) | `c29c1332` | done | Design the "perfect" env-cache / retained-surface implementation (brainstorm → spec → plan)&lt;/subject&gt; &lt;paramet… | — | — |
| [#135](#c29c1332-135) | `c29c1332` | open | SP-2c: UN-HOLD — P-3 has not landed, so nothing is obsoleted; still NOT_STARTED | — | — |
| [#136](#c29c1332-136) | `c29c1332` | open | P-1: RE-SCOPED to perceptual items only — (b) and (f) are substrate-decided (audit delta[12]) | `d8f36de7` `404781e0` `0c27d76b` `a8196e52` `6e1e691f` `4e95c50c` | `matrix-prereq-ledger.md` |
| [#137](#c29c1332-137) | `c29c1332` | done | P-2 DONE: Air-Gap seam closed for BackdropPass, half-closed for WorldPass; residual metered (see #191) | `a0f0089b` `b2cabd8d` `e6edbe11` `d4b47d7e` `37306207` `c2208b71` `da0125b2` `1e46f71c` `af9d54a9` `aba06048` | — |
| [#138](#c29c1332-138) | `c29c1332` | open | P-3 NOT STARTED — and two feasibility spikes must run BEFORE any parallax shader work is authorised | — | — |
| [#139](#c29c1332-139) | `c29c1332` | done | P-4 PHASE 1 DONE: GL-state assertion pass shipped + gate-read (f02a69f5); phases 2-4 (depth) split to #198 | `f02a69f5` | — |
| [#140](#c29c1332-140) | `c29c1332` | done | P-0 DONE: headless render harness — built, and exercised hard all through #166/#168 | — | — |
| [#141](#c29c1332-141) | `c29c1332` | done | P-5: THE TRUNK — half the GPU frame is unattributed; instrument it before choosing any more levers | — | `matrix-prereq-ledger.md` `2026-07-25-unified-telemetry-model-design.md` |
| [#142](#c29c1332-142) | `c29c1332` | done | FBO-1: FBO subsystem hardening — honour explicit size, gate oracle surfaces, diagnosable failures | — | — |
| [#143](#c29c1332-143) | `c29c1332` | done | CM-1: merge the env + parallax composes into one full-screen pass (MEASURED: ~2ms/frame) | — | — |
| [#144](#c29c1332-144) | `c29c1332` | open | UM-1: upstream merge landed — remaining audit findings (27 confirmed) | — | `matrix-prereq-ledger.md` |
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
| [#168](#c29c1332-168) | `c29c1332` | done | CPU-7 DONE: lighting CPU budget closed (99.6% GPU-on / 100.0% GPU-off), then cut 16.9% by four levers | `164163cc` `2e9c514e` `f7521455` `4eb4e1c3` `bf9d0fb4` `0c8d5e7c` `419b0f63` `73beb625` `165f07b3` `83c16487` `25a7605f` `065d462b` `53ad8da0` `34ffb7bd` `0571b2c9` `9422b768` `582991af` | `board.md` `2026-07-25-lighting-cpu-budget-closure.md` |
| [#169](#c29c1332-169) | `c29c1332` | open | L3b: F16C vcvtps2ph for the fp16 emission convert — needs Director sign-off (output changes) | — | — |
| [#170](#c29c1332-170) | `c29c1332` | done | L4 DONE: adaptive border shipped (03cec1c0). Its -23.6% is SUPERSEDED, not wrong — re-measured 2026-08-04 at -10/-11% | `03cec1c0` `a9854185` | — |
| [#171](#c29c1332-171) | `c29c1332` | done | LIGHTING-PRODUCE CLOSED: the denominator under every lighting percentage was missing a third of itself — and it is 1.5%… | `7bede779` `164163cc` `a1296a7d` `b1d8c3a0` | — |
| [#172](#c29c1332-172) | `c29c1332` | done | Telemetry deep-off cost: MEASURED — arming costs +2.16%, within noise; the deep gate works | — | — |
| [#173](#c29c1332-173) | `c29c1332` | done | RB-FLUSH: CLOSED — a CPU lever on a loop that sleeps 72-84% of every frame, at both measured scenes | `7bede779` `164163cc` | — |
| [#174](#c29c1332-174) | `c29c1332` | done | P-0b DONE: motion gate shipped (7ce03361) -- bypass PROVEN engaged; G9 SATISFIED, the split is authorised | `7ce03361` | — |
| [#175](#c29c1332-175) | `c29c1332` | done | SIM-1 DONE: server-tick budget closed 99.76% across 27 phases; compute.entities is the real 65% | `4eb7b96c` | — |
| [#177](#c29c1332-177) | `c29c1332` | done | ENV-CHOP DONE: env cache had no motion term; ship flight/warp CONFIRMED SMOOTH in game | `82711412` `00f575ad` | — |
| [#178](#c29c1332-178) | `c29c1332` | done | GATE-STALE DONE: render-gate.sh now asserts the build happened, not just the run (2643b1ce) | `2643b1ce` | — |
| [#179](#c29c1332-179) | `c29c1332` | done | DOC-DRIFT DONE: Air-Gap counts generated into the doc and gated by render_docs_fresh (c89be289) | `2ef87860` `c89be289` | — |
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
| [#191](#c29c1332-191) | `c29c1332` | open | DTO-2 RE-SCOPED: blocker 2 DONE (b2ac6c27); blocker 1 is bigger than filed -- it reaches TileDrawer in the game layer | `334bc38d` `b2ac6c27` | `matrix-prereq-ledger.md` `2026-08-01-target-state-system-architecture.md` |
| [#192](#c29c1332-192) | `c29c1332` | done | CI-1 DONE: lint ported to Python, all three gates registered via ${Python3_EXECUTABLE} (786d4342) | `45da57fc` `786d4342` | — |
| [#193](#c29c1332-193) | `c29c1332` | done | CI-2 DONE: STAR_EXT_GUI_LIBS_CORE split + CMake assertion; test no longer links Steam (786d4342) | `786d4342` | — |
| [#194](#c29c1332-194) | `c29c1332` | done | CI-3 DONE: absolute 15us bound -&gt; 4x ratio; both ends measured, injection proves it fires (f87a6848) | `45da57fc` `f87a6848` | — |
| [#195](#c29c1332-195) | `c29c1332` | done | CI-4 DONE: Gates workflow runs the 4 script gates on every push; ceilings read via --from-cmake (1328b3f5) | `1328b3f5` | — |
| [#196](#c29c1332-196) | `c29c1332` | done | BUILD-1 CLOSED: overlay vendored + registered declaratively; pristine clone bootstraps (18a8e5c0, 1830ef31) | `f0b9fb1f` `1830ef31` `18a8e5c0` | — |
| [#197](#c29c1332-197) | `c29c1332` | open | FBO-3: effect-parameter state persists per effect and nothing asserts it -- the `world` effect is the exposure | — | `matrix-prereq-ledger.md` |
| [#198](#c29c1332-198) | `c29c1332` | open | P-4 phases 2-4: depth-buffer architecture -- ZERO TRACE, survey before designing | — | — |
| [#199](#c29c1332-199) | `c29c1332` | open | HEADLESS-1 DEFERRED: sink-gating landed (f7609bb7); a real headless client is its own engineering effort | `f7609bb7` | `2026-08-02-tssa-levelling-analysis.md` |
| [#200](#c29c1332-200) | `c29c1332` | done | ARCH-1 DONE: boundary doc + arch-graph generator + arch_graph_fresh gate all in the tree and green | `7d22d2bb` | — |
| [#201](#c29c1332-201) | `c29c1332` | done | CI-5 DONE: the native path separator is gone — as_posix() at boundary-inventory.py:116, with the warrant at :109 | `e7368000` | — |
| [#202](#c29c1332-202) | `c29c1332` | done | RI-2 DONE: both L1 files claimed, the application partition exists, the fence holds six, gated residual 17 of 17 | `52360eee` | — |
| [#203](#c29c1332-203) | `c29c1332` | open | ARCH-2: shape + cohesion tests, actions reordered by value, and a recursive-scan defect | `07d248f4` `cfa7603b` `38a147d9` `80084408` `c3ef055d` `7fdae50d` `98b6692a` `5e893499` | — |
| [#204](#c29c1332-204) | `c29c1332` | **active** | TSSA (#204): target-state architecture spec — structure DONE, content is the remaining work | `3eed5ab8` `d4d07d9a` `84fe2324` `c8bf863b` `c363e052` `fa6f4d8f` `a0bbb0c1` `1899aa71` `02de4584` `e386d983` `46894dae` `466c19cc` `82170188` `0481bb82` `566fce3d` `21ba119a` `5d276333` `42b60f5d` `298bdd16` `1b5f6f00` `c359fc30` `63248364` `1a1c89d8` `6a53b039` `5388cbd0` `b2db3904` `7dc30a06` `b0591b19` `299e8b47` `0acb3c7b` `a71484af` `706e970f` `5d6f5b97` `94a3eec4` `e0a6cdec` `ca7e0011` `a51b3659` `364519c3` `2ed13794` `a3eaa447` `6c30f710` `47e33692` `6539382d` `3751ccaa` `f88ff14d` `351e7696` `ad6426e7` `0fe309ee` `c1e89fdc` `d15a3816` `56b9a1ca` `6956aab7` `01694c2c` `bb04e8b5` `91d18ec6` `fbe4c41b` `1cf8987d` `50f09efe` `94c1af57` `24c79ca8` `dc4dc3e7` `4a333db7` `3ac919ed` `4b4982bd` `736030b7` `d8b8d550` `cb93b33f` `a588e8ab` `8f9e1ce3` `d7708b7a` `e0b827df` `beb51779` `8523181d` `a6c7dba1` `a513ef6f` `618f99e3` `3f07aeec` `b03248c5` `7155637b` `a1911e89` `88bc52f2` `7482dc5f` `41047638` `9882a77a` `1da322da` `77669e22` `1a0f118f` `d14f7f88` `c9c2770f` `739b4aba` `14b941a6` `2e01529d` `b3647d73` `6d181196` `151b8d56` `ebfe4547` `5538062c` `22c5e6d5` `02f2d8d6` `ac6cafd1` `e7f5ab49` `8ffffc5e` `cc9a5532` `2f42bca7` `f053671f` `55da339e` `a6342adb` `376eb9c9` `d1671a77` `e1107569` `9823f106` `c71a06f3` `cd43f4ba` `e635efbf` `f0cb85d9` `4534874d` `ab53f943` `998d52fe` `8fc507c0` | — |
| [#205](#c29c1332-205) | `c29c1332` | done | TSSA-0 DONE: Part I written, 312 -&gt; 735 lines; six axioms verified and five became decisions | `9c8eb88f` `f7e61c8e` `947a34b1` | — |
| [#206](#c29c1332-206) | `c29c1332` | done | TSSA-1 DONE: 18-section TOC derived, ratified, implemented, cross-refs renumbered | — | `2026-08-02-tssa-levelling-analysis.md` `tssa-frame.md` |
| [#207](#c29c1332-207) | `c29c1332` | done | TSSA-2 DONE: the levelling pass closed; its one "remaining" item shipped at 706c7f4a and its blocker was never real | `60a66f02` `706c7f4a` `890218ab` `2920def1` `a64b21c7` `337f5494` `d2eadae4` `9442b154` `248b9a74` `ed5e7125` `cdea71c1` `de57756e` `82955390` `41b7adfd` `3ee6021d` `170a17cb` `eab0b4da` `5f2933ad` `a96a3575` `6d5100fb` `78a8a4b5` `51415bfe` `f5faf4cf` `5db63949` `b1b34d9a` `550a3b5e` `b976ae79` `12f08312` `0c96063d` `b86a1ad0` `4011f1af` `8e742ae7` `176d60be` `45af8da3` `a58ef402` `3f767902` `f6744d04` `6184248e` `7952fb50` `669dbc29` `fdad9c5d` `b8f61e9f` `b69cd406` `62fe4dbd` | — |
| [#208](#c29c1332-208) | `c29c1332` | open | TSSA-3: register fields, aggregate review, re-home the anchoring gates | — | — |
| [#209](#c29c1332-209) | `c29c1332` | done | TSSA-4: give the derivable numbers an owner — 2 live defects found, ZONE tally + closure numerators ungated | — | — |
| [#210](#c29c1332-210) | `c29c1332` | done | RACE-1 DONE (66ec860b): sector unload now holds m_lightMapPrepMutex; TSan verification DEFERRED | `66ec860b` | — |
| [#211](#c29c1332-211) | `c29c1332` | done | GL-GUARD-1 CLOSED: guard b1e66be4 + ARB arm now dispatches to glMinSampleShadingARB (b18797e4) | `b18797e4` `b1e66be4` | — |
| [#212](#c29c1332-212) | `c29c1332` | done | TEST-CI-1 DONE (f5088933): 20 lighting assertions now run in CI; configure-time guard proven to fire | `f5088933` | — |
| [#213](#c29c1332-213) | `c29c1332` | done | LIGHT-DEDUP-1 CLOSED (7879e0a2): 140 lines -&gt; 70, byte-identical at instruction level; beam divergence declared and… | `0fed2045` `c8bf863b` `7879e0a2` | — |
| [#214](#c29c1332-214) | `c29c1332` | done | GATHER-1 DONE (9429b14d): one gatherColumns + two sinks, byte-identical; E01/E04 unblocked. Gate's own A/B fixed (cb17d… | `cb17d323` | — |
| [#215](#c29c1332-215) | `c29c1332` | done | ORACLE-MOVE-1 CLOSED (c1a4433d): oracles split to their own TU; kernel object proven unchanged; task's split corrected | `84fe2324` `c1a4433d` | — |
| [#216](#c29c1332-216) | `c29c1332` | done | ORACLE-TRUTH-1 CLOSED (7dbf8c98): 5 closed-form assertions + an experiment proving differential tests are blind to a sh… | `84fe2324` `7dbf8c98` | — |
| [#217](#c29c1332-217) | `c29c1332` | done | BORDER-1 DONE (817e54e8) + MEASURED at the scene that exercises it: lever -27.8% region / -10.1% lighting CPU; fix cost… | `817e54e8` | — |
| [#218](#c29c1332-218) | `c29c1332` | done | COMMENT-1 DONE (9e56c204): rule written + 6 wrong comments swept + all 9 line-refs converted + comment_claims gate at z… | `bc0305df` `9e56c204` | — |
| [#219](#c29c1332-219) | `c29c1332` | done | LEDGER-B DONE: pr570-ledger reports 155 rows / 0 open; D29 declined by the Director 2026-08-05 (c363e052) | `c363e052` `9ccb57a8` `e93cefd5` `3b0214db` `e3682234` `0a8fff9d` | — |
| [#220](#c29c1332-220) | `c29c1332` | done | EXTERN-1 CLOSED: all 5 ledger rows done -- deletions 4e4ced84, E10 provenance 4960f7e5, E06 guard a69c8888 | `8862dca7` `6e0f6117` `a69c8888` `4960f7e5` `4e4ced84` | `board-audit-2026-08-08.md` |
| [#221](#c29c1332-221) | `c29c1332` | done | GATHER-2 CLOSED: all four done as hardening -- E02 fc81ae17, E01 13cd3784, E03 c572e875, E04 a9ca6e26 | `a9ca6e26` `c572e875` `13cd3784` `fc81ae17` | — |
| [#222](#c29c1332-222) | `c29c1332` | done | BORDER-2 CLOSED: the 13.2% was the HARNESS, not the border. True cost = 0.168% of pixels at one fp16 LSB | `c9f7f524` `6e66f36e` | — |
| [#223](#c29c1332-223) | `c29c1332` | done | GATE-TOLERANCE-1 DONE: paralloracle was never failing -- the gate read a BOUNDED-diff oracle as a zero-diff one | `f048adc4` `0c6184bf` | — |
| [#224](#c29c1332-224) | `c29c1332` | done | SPREAD-CAP-1 CLOSED: cap 48 covers 100% of measured content (10 locations); margin is 1.7% but a breach is now loud, no… | `f95abac2` `9f2aba93` `8b05254b` `bf6fa6bf` `5f77683b` | — |
| [#225](#c29c1332-225) | `c29c1332` | done | EPOCH-1 FIXED (8f322517): hit rate 17.9% -&gt; 80.7%. My "not viable" close was WRONG -- see the correction | `8f322517` `5af987a2` `e572e1ab` | — |
| [#226](#c29c1332-226) | `c29c1332` | done | EPOCH-2: H1 undergroundLevel + H2 loadDefaultSector FIXED (8f322517); H3 asset-reload flags left open, severity unverif… | `8f322517` | `matrix-prereq-ledger.md` |
| [#227](#c29c1332-227) | `c29c1332` | done | ATTRIB-1 CLOSED (122ca40a): all 7 artefacts attributed; imgui grant found upstream; the field is now gated on correspon… | `aa484259` `122ca40a` | — |
| [#228](#c29c1332-228) | `c29c1332` | done | BEAM-EPS-1 CLOSED (0fed2045): unified on 0; bit-identical for all real content, colored path provably untouched | `0fed2045` | — |
| [#229](#c29c1332-229) | `c29c1332` | done | GPUTEL-B1: explore context for the sovereign GPU telemetry design | `8a081d61` `dc722dee` `934afba9` `c3a92060` `8d5734f6` `fecf37f6` `320b67bc` `c3181e45` `e6bde922` `8ed374c4` `fb9c13bc` `db4e35d2` `60997cb8` `11ebce0c` `8261ce4e` `dcb7daf6` `9b3c9428` | — |
| [#230](#c29c1332-230) | `c29c1332` | done | GPUTEL-B2: clarifying questions, one at a time | — | — |
| [#231](#c29c1332-231) | `c29c1332` | done | GPUTEL-B3: propose 2-3 approaches with trade-offs | — | — |
| [#232](#c29c1332-232) | `c29c1332` | done | GPUTEL-B4: present design, get approval per section | — | — |
| [#233](#c29c1332-233) | `c29c1332` | done | GPUTEL-B5: write + self-review + Director-review the spec, then writing-plans | — | — |
| [#234](#c29c1332-234) | `c29c1332` | done | GM-1b: EngineBusyReader must poll, not bracket — the PMU publishes lazily | `9505dd92` | — |
| [#235](#c29c1332-235) | `c29c1332` | done | BUSY-VS-WALL DONE: the series is retained, CPU busy is attributed per owner, the clock is declared and ratcheted, and b… | `164163cc` `7fa1d2ed` `eec79ca1` `e368f046` `0eef793b` `11a28ba0` `3a8c530e` `72ad0000` `50d57866` | — |
| [#236](#c29c1332-236) | `c29c1332` | done | GPU-CLOSE-1 CLOSED by [#235] e368f046: the re-declaration that would revive the inert oracle is now refused mechanically | `e8814a61` `49249551` `420f1759` `040a5033` `feb8886f` `9db54200` `0290f6ef` | — |
| [#237](#c29c1332-237) | `c29c1332` | open | MEASURE-CLIENT: a fifth entrypoint — real client, real GPU, no window, owns its fixture and its storage | `20027817` | — |
| [#238](#c29c1332-238) | `c29c1332` | done | LEDGER-SWEEP: clusters A+B+C — 13 open rows closed before the matrix run | `a8df237e` `b879c332` `92b8a3eb` `a66a7378` `246d75df` | — |
| [#239](#c29c1332-239) | `c29c1332` | done | MATRIX-RUN: the 8-lever matrix, 28 legs, run id matrix-20260807-071440 | `297090b0` `f6cb3258` | — |
| [#240](#c29c1332-240) | `c29c1332` | done | R13-FIX: hoist the four lazy tick.server.lock.* registrations — closes the ledger's only BLOCK | `5605be17` `495682df` | — |
| [#241](#c29c1332-241) | `c29c1332` | done | R14-FIX: register both compose keys eagerly — the never-RAN arm still reads ABSENT | `cd0b5175` `775176ff` | — |
| [#242](#c29c1332-242) | `c29c1332` | done | MATRIX-RERUN: bank Desert Town quotable, then a second scene — with a PMU sample alongside | `24bd4a51` | — |
| [#243](#c29c1332-243) | `c29c1332` | done | REG-RATCHET: pin the count of lazily-registered telemetry handles so there is no eighth instance | `d3650330` | — |
| [#244](#c29c1332-244) | `c29c1332` | done | POST-MATRIX ORDER: SUPERSEDED — the board is the order now, and five of the nine have since resolved | `bb29075c` `ce8a089d` `35bbb97c` `495682df` | — |
| [#245](#c29c1332-245) | `c29c1332` | done | DESC-CONVERGE CLOSED: unit and boundedness are read, the ratchet counts only what it names, and two of my own claims we… | `da28b864` `a01c4a9c` `21adb941` `bc0305df` `7f222773` `fe20f87d` `bea29db1` `fa04b907` `aa484259` | — |
| [#246](#c29c1332-246) | `c29c1332` | done | WORLD-PASS: CLOSED — the bracket is 2.5x the entire GPU's busy time, so it is a span, not a cost | `7bede779` `164163cc` | — |
| [#247](#c29c1332-247) | `c29c1332` | done | R15+R16-FIX: the .nested counters and owner gl's TOTAL are the two registrations R14 did not reach | `cb59fc50` | — |
| [#248](#c29c1332-248) | `c29c1332` | done | PMU-ATTRIB: per-lever GPU cost needs continuous sampling, not spot samples against a moving baseline | `dd30bfa5` | — |
| [#249](#c29c1332-249) | `c29c1332` | done | RE-MEASURE: every GPU busy number from 2026-08-07 was mis-aligned; re-run both scenes with the fixed stamps | `ad4e2f54` `caea59f5` | — |
| [#250](#c29c1332-250) | `c29c1332` | open | GM-3 STREAM: the single sovereign API surface — one reader interface, one sample type, a LIVE transport that is not fil… | — | `board-audit-2026-08-08.md` `board.md` |
| [#251](#c29c1332-251) | `c29c1332` | open | GM-4 DISJOINT: GL_GPU_DISJOINT_EXT has never been checked, and this GPU clocks 933-2350MHz — every historical GL_TIME_E… | — | — |
| [#252](#c29c1332-252) | `c29c1332` | open | TEL-EXTRACT: everything that WRANGLES, PROJECTS or CONSUMES metrics moves to metrics/ — ~390 lines, of which 246 are pr… | — | — |
| [#253](#c29c1332-253) | `c29c1332` | done | OBS-JOIN DONE: all four cells of the busy model, on one epoch axis, per leg — verified live | `a4fc43b3` `5fcc401c` `57d00526` `be2ceeee` `f5029bf3` `ce6c88a1` `7fa1d2ed` `c1dfce7e` | — |
| [#254](#c29c1332-254) | `c29c1332` | done | OBS-PLOT DONE: the busy model renders, artifact 14904c73, and the plot cannot draw across a hole (46a4c792) | `991b7940` `46a4c792` | — |
| [#255](#c29c1332-255) | `c29c1332` | done | MATRIX-CPU DONE: 27 legs, all four cells, and the caches trade CPU for GPU at identical frame rate (a4fc43b3) | `35e049a1` `a4fc43b3` | — |
| [#256](#c29c1332-256) | `c29c1332` | done | TSSA-OBS DONE: `telemetry` is a component, and teaching the sweep to SEE it found three defects (c19cb959) | `574879f3` `c19cb959` `94f44d59` | — |
| [#257](#c29c1332-257) | `c29c1332` | done | DEDUP-RATCHET DONE: gated on FILES not symbols, and the gate set can now see it — 56 green (8ef7e573) | `8ef7e573` | — |
| [#258](#c29c1332-258) | `c29c1332` | done | GATE-SKIP-2 DONE: pr570_ledger exits 77, and run-gates --selftest now reads the rule out of gates.yml (7e9815a4) | `8ef7e573` `7e9815a4` | — |
| [#259](#c29c1332-259) | `c29c1332` | done | OBS-PMU-WIRE DONE: all three sources on one axis per leg — and the two GPU instruments now cross-check every interval (… | `5fcc401c` | — |
| [#260](#c29c1332-260) | `c29c1332` | open | TSSA-PLATFORM-DISSOLVE: a ratified five-stage thread, stages 3/4/5 not started, on no board task | — | — |
| [#261](#c29c1332-261) | `c29c1332` | open | HANDOVER-STALE: #204's resume document is 167 commits behind and every §2 figure reads wrong | — | — |
| [#262](#c29c1332-262) | `c29c1332` | open | BOARD-TAIL: apply the 2026-08-08 audit's pointer corrections to 21 dormant tasks | — | — |
| [#263](#c29c1332-263) | `c29c1332` | done | BOARD-CITE CLOSED: it was 47 phantom rows on 14 tasks, not two, and the board had no fixed point | `5d231c48` | — |
| [#264](#c29c1332-264) | `c29c1332` | done | LEVER-CPU-WEIGHTED CLOSED: the fix is correct, was implemented and measured, and changes nothing — the real constraint… | — | — |
| [#265](#c29c1332-265) | `c29c1332` | done | SIM-ON-RENDER CLOSED as a NULL RESULT: the three sim effects were noise, and the omnibus now refuses them mechanically | `33d04e0e` `45a09f6d` `a686d95b` | — |
| [#266](#c29c1332-266) | `c29c1332` | open | DESC-RATCHET-WIDTH: the ratchet counts MetricDesc SITES, so a descriptor-free handle is still uncounted — and a one-arg… | — | — |
| [#267](#c29c1332-267) | `c29c1332` | open | A1-HANDLES: `Telemetry` is the ambient global A1 forbids — and the axiom derivation says the row is FIVE duties, not one | — | — |
| [#268](#c29c1332-268) | `c29c1332` | open | LEVER-FLOOR: 1/2/3/4/5 DONE — only "raise n" remains, and it costs ~1.7h not the 8 min I first wrote | `33d04e0e` `45a09f6d` `35e049a1` | — |
| [#269](#c29c1332-269) | `c29c1332` | open | LIGHTING-WHOLE: "lighting CPU" is not any owner's total — three keys under two owners each hold part of it, and nothing… | `7bede779` | — |
| [#270](#c29c1332-270) | `c29c1332` | open | LIGHTING-REDERIVE: #168's closure and #170/#217's border percentages were measured against a denominator now known to b… | — | — |
| [#271](#c29c1332-271) | `c29c1332` | done | GPU-DRIVE-DECOMPOSE CLOSED: the wait hypothesis is REFUTED at 0.03% — it is CPU work, and `point` is 38% of it | `7bede779` `164163cc` | — |
| [#272](#c29c1332-272) | `c29c1332` | open | DRIVE-RESIDUAL: a quarter of cpu_cost is in none of the seven parts, and switchEffectConfig submits inside another 38% | `7bede779` | — |
| [#273](#c29c1332-273) | `c29c1332` | open | AXIS-SUBSYSTEM: twelve in-process keys reach every leg and none reach the joined axis, so no matrix run can attribute t… | — | — |
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

- cited in `docs/board-audit-2026-08-08.md`
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

- cited in `docs/board-audit-2026-08-08.md`
- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`

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

- `91cd1ef4` SCENE-FINGERPRINT (O01/#84): the matrix had none, and its own artefacts show the drift #84 was refused for
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
60 MATCH / 0 DIFF, parallax oracle 60 EXACT / 0 DIFF).
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

- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`

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

Related: #144 carries the same defect class for frameBufferTextures across loadConfig.
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
DONE: brainstorm→spec→plan complete. Root-caused the probe failure (surface-lifecycle, not compose; ship-white = transparent sky + blind alpha=1 replace over non-black envCache; red/black = pre-existing AA×lighting bug). Spec committed kubebound 2f47a6e (specs/2026-07-12-env-cache-design.md); plan committed 3f63085 (plans/2026-07-12-env-cache-plan.md). Design = oracle-first (automated pixel-memcmp as debugging instrument + deploy gate), fix to zero-diff, N-sweep measure, AA-off target + direct fallback. Phase 2 = #133 hardening + parallax/world extensibility. IMPLEMENTATION PENDING = execute the plan (feat/env-cache @ 46b8606 is the WIP start point). Handover: kubebound/2026-07-12-HANDOVER.md.
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

- `d8f36de7` O13: the harness measured wherever the player happened to be, and quiescence fired on the world it was leaving
- `404781e0` ORACLE-136d: the golden reproduces to ONE LSB with no world body and 19% of pixels with one -- #191 was right
- `0c27d76b` ORACLE-136c: two per-run terms pinned out of the sky, and the golden-hash blocker is NOT what #191 named
- `a8196e52` ORACLE-136b: the blocker's own evidence does not survive, and two clauses of the trade were false
- `6e1e691f` ORACLE-136a: the staleness bound was one operand nothing tested, and a refuted claim about the instrument
- `4e95c50c` telemetry: the two compose arms are mutually exclusive -- Cadence::Call, not Frame [#136]
- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`

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
task closes on what it actually delivered rather than staying open on a different project.
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

- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`
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

CAVEAT: measured at the SHIP. A surface world (42 parallax layers, dense terrain) will rank differently -- parallax and world both rise. Re-run the same measurement at the Director's BASE before finalizing the lever order.
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

REMAINING: user confirms the crash is gone in live play (it was intermittent, so absence over time is the only proof).
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

- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`

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
scripts/deploy-install.sh <install> --verify as the single sanctioned repo->install path.
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
DONE 2026-07-20 (abf6e2eb). All 3 game_tests failures fixed; scripts/game-tests.sh -> 91/91 PASSED (22s). Two root causes: (1) ItemComparison — pre-existing UPSTREAM failure (perfectlygenericitem ObjectItem injects scriptStorage:{} via retainObjectParametersInItem:true; raw JsonObject== exactMatch rejects it vs pre-construction descriptors). Fixed with a clean() helper in item_test.cpp that strips scriptStorage. (2) ConstructItems + RootTest.All — game_tests fails on any error-level log; ~40 broken third-party workshop mods in sbinit.config. Fixed via scripts/game-tests.sh running against a clean asset set (base + opensb) using harness/sbinit-test.config (local, gitignored like sbinit.config). Render gate keeps the full mod set. Test-only changes; starbound binary + dev deploy unaffected.
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
Surface layer should make unrepresentable.
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

Certify with scripts/render-gate.sh (all three oracles green at shipped defaults) + core_tests + game_tests (ItemComparison is the known red, #146).
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

FIX (behaviour change, own gated commit): either EffectTexture knows the texture is target-owned and the upload setters refuse/replace rather than re-spec, or setEffectTextureFromTarget hands over a non-owning view.
```

<a id="c29c1332-156"></a>

#### #156 — RB-2/RB-3 DONE (691392929): compile+link before replace; shader fallback fixed

status: **completed**

```
GlEffects::load() (was loadEffectConfig, cpp:~462-465) does glDeleteProgram + erase UP FRONT, then throws at the link check (~:516-520) on failure.

Deterministic damage on every link failure: the effect is permanently ABSENT from the registry, its old program is deleted, m_pass.effect is stale (the re-bind is never reached), and switchEffectConfig(name) returns false forever after. That return value is IGNORED by StarClientApplication.cpp:512 ("interface") and :532 ("world") -- so those layers silently draw under whatever effect was last bound. A modder with one bad shader gets a silently mis-rendered game, not an error.

FIX (behaviour change, own gated commit): compile and link into a new program FIRST, replace the registry entry only on success. This -- not a container swap -- is the real fix for the stale m_pass.effect.

RELATED (RB-3, upstream): the compile fallback at cpp:~498-499 passes DefaultVertexShader/DefaultFragmentShader (the raw GLSL SOURCE) to compileShader, whose second parameter is a KEY into the `shaders` map. Both lookups miss, both return 0, nothing is attached: "Shader compile error, using default" compiles NO default at all. It also leaks 2 glCreateShader handles (the create precedes the !source early-return). Correct signature: (GLenum type, char const* label, String const& source). CHECK UPSTREAM FIRST -- inherited, not ours. The pixel oracle CANNOT see this (the catch never runs on stock shaders).
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

ACTION: draft as docs/upstream/issues/passes.md alongside the 7 other unfiled drafts. DO NOT FILE without the Director's approval.
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

Also trivial: the dead `screenSize` effectParameter in assets/opensb/rendering/effects/lightingPassthrough.config and lightingUpscale.config resolves a SECOND location for a uniform GlPass owns and pins Vec2F(1,1) into effect.parameters. Inert (setRenderTarget rewrites the real uniform before every draw) but it should go. Asset-only -- deploy via the loose override.
```

<a id="c29c1332-159"></a>

#### #159 — RB-6/RB-7 DONE (b74937721, d05f21682): texture internalFormat record + pass notified of target rebuild

status: **completed**

```
Two further render-hardening commits found already landed on dev/upstream-merge (linear, on top of RB-5). Recorded for completeness.

RB-6 (b74937721): GlLoneTexture now records its internalFormat, and the Half/R8 SubImage fast paths are format-aware -- "a texture that describes half its storage is worse than one that describes none". This is what makes the same-size/different-format corruption shape safe at the record level (belt-and-suspenders behind RB-1's borrowedFrom fix).

RB-7 (d05f21682): "the pass was never told the targets were rebuilt" -- the last seat of the RB-5 orphan defect; rebindBorrows told the samplers but not the pass. +14 lines in StarRenderer_opengl.cpp.

STATE: HEAD e30c21c4d, clean tree. Full RB backlog (RB-1..RB-7) + 3 upstream issue drafts + 2 upstream PR drafts (docs/upstream/, NOT published) + 3 architecture-assessment docs all committed. Verified this session: build clean, core_tests 226/226, game_tests 90/91 (ItemComparison known red #146), render-gate all 3 oracles green + 0 GL errors, RB-1 fullbright probe GREEN.
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
Jacobi improvements NOT STARTED. NOW UNGATED (#170 shipped, the calc region stopped moving).

RE-RANKED 2026-08-04 on evidence from the OpenStarbound PR 570 analysis. The task's own instruction
was "if it is revived, the ranking should be re-derived from a GPU capture, not from the original
task text." This is the first new evidence since that was written.

WHAT SHIPPED, TO BE CLEAR: the GPU spread pass IS Jacobi ping-pong, running today at
spreadIterations resolved by GpuLightmapPass::spreadIterationsFor() = min(cap, max(8, ceil(
maxEmission * spreadMaxAir))). #168 found no Jacobi in the CPU budget because it is not on the CPU.
This task is about IMPROVING the shipped Jacobi, not introducing it.

THE NEW EVIDENCE. PR 570's author ran an out-of-tree GPU spike (their deleted TODO.md, commit
a6ff9509, patch 0008 lines 200-210, in Russian) and abandoned the GPU path:
    "relax ~1 ms/pass x 48 = ~51 ms/iteration against 9-13 ms CPU-parallel Dial (4.1x slower)
     -- bottleneck is UMA bandwidth (~13 GB/s): 48 x 13 MB = 625 MB per iteration, CPU Dial
     lives in L3 cache. GPU path is closed for this hardware; a win is only possible on a
     discrete GPU with fast VRAM and/or with a small number of passes."
Their configuration: per-light 2D compute dispatches, atomicMax accumulation, fixed K=48, full
resolution, Intel ADL-N (Atom-class, ~13 GB/s).

DOES NOT TRANSFER TO US, and the reasons are our design: we solve ONCE over the whole field (not per
light), at TILE resolution with a separate upscale pass, in RGBA16F with the obstacle packed into
alpha (J-2), with adaptive K floored at 8. Measured on the Director's Arc Pro 130T/140T:
lighting.gpu.spread.gpu_us = 744, lighting.gpu.point.gpu_us = 987.

WHAT IT DOES CHANGE -- THE GOVERNING PRODUCT IS passes x working-set x (1/bandwidth). That re-ranks
the four original candidates, and independently confirms the original top two:

  1. TEMPORAL WARM-START   attacks PASSES directly. Seed iteration 0 from the previous frame's
                           converged lightmap instead of raw emission. Biggest win on the static
                           scenes #132 is about.
  2. RESIDUAL EARLY-EXIT / ADAPTIVE N   attacks PASSES directly. Stop at convergence instead of
                           running the resolved K.
  3. red-black Gauss-Seidel   changes convergence RATE, not traffic per pass. Demoted.
  4. multigrid             large change, traffic win unclear. Demoted.

THE MEASUREMENT TO DO FIRST, and it is cheap because both counters already exist:
    lighting.gpu.spread.passes   (K actually run)   StarGpuLightmapPass.cpp:43
    lighting.gpu.spread.gpu_us   (what it cost)     StarGpuLightmapPass.cpp:96/:183
Sweep K on the headless harness, record the pair, derive us-per-pass and thus our effective
bytes/second. That converts a single-machine measurement into a MODEL, and the model answers the
question we currently cannot: at what K does our GPU path cross our own CPU calculate() (measured
3,817 us/recompute by #168) on a given bandwidth? K is scene-driven and FU ships very bright lights,
so the crossover is a real question rather than an imported one.

DO NOT BUILD an automatic CPU/GPU fallback policy off this. That would be a policy for hardware we
have never run, on a threshold nobody has measured -- see #129, a lever measured null and removed.
Measure first; the policy is trivial once the number exists and may prove unnecessary.

SCALE CHECK so this is not overweighted: #132 measured env ~1614 + parallax ~1939 + world ~1839 per
FRAME against lighting's ~1731 per RECOMPUTE (gated by the temporal gate's 33 ms floor). Lighting is
not what saturates the GPU; the scene passes are.

VALIDATION NOTE that still holds: these change output toward the SAME steady state, so they are
validated by a convergence/quality comparison, NOT byte-identity. Reuse the spreadoracle harness.

STILL TRUE FROM THE 2026-07-25 AUDIT: none of the four candidates is in the tree. No temporal
warm-start (iteration 0 is still seeded from raw emission), no red-black GS, no residual early-exit
or adaptive N, no multigrid.
```

<a id="c29c1332-162"></a>

#### #162 — CPU-1: explore context — what the frame loop times today, thread ownership, frame-skip

status: **completed**

```
Brainstorm step 1 for per-frame CPU metrics alongside GPU in the live render-profile harness. Establish precisely: which main-loop steps are timed vs untimed (StarMainApplication_sdl.cpp:728-784); scope of the existing render.frame.us (in-world portion only) vs the true frame; thread ownership of every existing timer (main / server / lighting) since tick.server.compute.us = 3199us/frame EXCEEDS the whole GPU span (2952us) and summing across threads reproduces the 119%-of-the-whole bug; how frame-skip (updatesBehind loop) affects per-frame accounting; TelemetryScope cost when deep tracing is off.
```

<a id="c29c1332-163"></a>

#### #163 — CPU-2: clarifying questions + 2-3 approaches (brainstorm)

status: **completed**

```
Brainstorm steps 3-4. One question at a time. Key forks identified so far: (a) purpose — bound VERDICT (is the frame CPU- or GPU-limited) vs full ATTRIBUTION (where did every microsecond go); (b) harness-only vs also-live in the Director's sessions (changes gating, HUD, and the meaning of swap.us: GPU backpressure with vsync off, frame pacing with vsync on); (c) whether the server thread's own budget is in scope. Then propose 2-3 approaches with a recommendation.
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

SCOPE NOTE: this touches the GpuTimer contract in StarRenderDiagnostics.hpp, so it is a real change to a Renderer contract, not a tidy-up. Deliberately NOT done inline during plan execution — surfaced for the Director. Do it before the idiom spreads to a 16th pass. Guarded by scripts/render-gate.sh like everything else in this arc.
```

<a id="c29c1332-168"></a>

#### #168 — CPU-7 DONE: lighting CPU budget closed (99.6% GPU-on / 100.0% GPU-off), then cut 16.9% by four levers

status: **completed**

- `164163cc` [#271] the 152-159 us/frame lump gets seven parts, and one of them is the wait
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

#### #170 — L4 DONE: adaptive border shipped (03cec1c0). Its -23.6% is SUPERSEDED, not wrong — re-measured 2026-08-04 at -10/-11%

status: **completed**

- `03cec1c0` lighting: adaptive calculation border -- 4.375x -&gt; 3.000x, -23.6% lighting CPU
- `a9854185` lighting: measure how much of the 48-tile border is actually used (#170 probe)

```
SHIPPED 03cec1c0 (2026-07-25). Calculation region = query region padded by borderCells()=48 (128x64 -> 224x160). Now computed per recompute from the lights present and clamped into [spreadBorderCells()=32, borderCells()=48]. Kill-switch lightingAdaptiveBorder, default ON. RETAINED ON -- Director's standing rationale 2026-08-04: correct + beats vanilla = stays on.

ORIGINAL (from the commit): interleaved OFF/ON/OFF/ON at 00-Ocean-Lab, 45s each, lights.sources identical across arms.
  calc cells         35840 -> 24576   -31.4%
  lighting.cpu.total 508.2 -> 388.3 us/rec   -23.6%
  breakdown: gather 303.5->236.9, export 78.1->52.1, convert 66.3->46.0, begin 43.2->35.9

RE-MEASURED 2026-08-04 (live-profile harness, after #217 corrected the estimator):
  explore           35840 -> 24576  -31.4% region;  153.5 -> 136.3 us/rec  -11.2%
  04-Ocean Factory  35840 -> 25872  -27.8% region;   86.5 ->  77.8 us/rec  -10.1%
  04-Ocean Factory, CPU SOLVE FORCED ON (lightingGpu=false):  953.8 -> 879.9  -7.7%

THE REGION REDUCTION REPRODUCES EXACTLY. -31.4% at explore, identical cells 35840 -> 24576. The lever does what it says.

THE CPU PERCENTAGE DOES NOT, AND THE CAUSE IS NOT ESTABLISHED. A first explanation -- "the O(cells) CPU work moved to the GPU since July, so the same cells now carry less cost" -- was WRITTEN INTO THIS TASK AND IS REFUTED. Two independent disproofs:
  1. The commit's own breakdown lists gather/export/convert/begin and NO `calculate` line, so the CPU solve was ALREADY skipped in July under the GPU latch. Nothing moved; it had already moved.
  2. Forcing the CPU solve back ON today (lightingGpu=false, verified by lighting.cpu.calc.ran = 1121 vs 1127 and calc.skipped = 0) made the saving SMALLER, 7.7%, not larger. calculate.us is 2245.6 vs 2394.4 -- only -6.2% against a -27.8% region, because calculatePointLighting is LIGHT-proportional (per light, a bounded box around it) rather than AREA-proportional. Adding it dilutes the percentage.

WHAT IS ACTUALLY KNOWN. The absolute static-border cost at explore today is 153.5 us/rec against 508.2 at Ocean Lab in July -- but those are DIFFERENT SCENES and not comparable. Ocean Lab today reads 282.9 us/rec (adaptive, the shipped default); its static-border figure was never taken today, so the one apples-to-apples comparison available has not been run.

A LIKELY BUT UNMEASURED EXPLANATION: if a -31.4% cell reduction yielded -23.6% in July and yields -11.2% now, a larger share of lighting CPU is now NON-area-proportional. #168's four levers targeted the gather specifically (B1/B2 column work, A1/A2 caching), which is the largest area-proportional term -- cutting it would leave proportionally more fixed cost and shrink the lever's percentage without weakening the lever. PLAUSIBLE, NOT VERIFIED. Settling it needs one A/B at 00-Ocean-Lab, adaptive on/off, which is ~4 minutes of harness time.

DO NOT QUOTE -23.6% BARE. It is "at 00-Ocean-Lab on 2026-07-25, when the static-border lighting CPU there was 508 us/rec". The live figure is 10-11% at explore and Ocean Factory. See #217 for the bookmark-representativeness table (explore has border.needed 4 and cannot exercise this lever at all).
```

<a id="c29c1332-171"></a>

#### #171 — LIGHTING-PRODUCE CLOSED: the denominator under every lighting percentage was missing a third of itself — and it is 1.5% of the frame

status: **completed** · metadata: `{"rank": 2}`

- `7bede779` [#271] the wait hypothesis is refuted at 0.03% -- it is CPU work, and the per-light section is 38% of it
- `164163cc` [#271] the 152-159 us/frame lump gets seven parts, and one of them is the wait
- `a1296a7d` [#171] the first reading of the instrument: the lighting denominator has been missing a third of itself
- `b1d8c3a0` [#171] five producer-side lighting costs get names; every lighting percentage has been excluding them

```
Producer-side lighting CPU was billed to owner `frame` with no name of its own.

=== CLOSED 2026-08-15. INSTRUMENT BUILT (b1d8c3a0) AND READ (a1296a7d) ===

THE ANSWER. Two 90-second legs at Desert Town, offscreen on the real GPU, sim running, deep tracing
on. Producer-side lighting CPU is 47.3-57.5% of `lighting.cpu.total.us` — so that key, the
denominator under every "lighting CPU" percentage this project has published, captures only
**63.5-67.9%** of the CPU that is actually lighting work. About a third has never been counted.

THE HONEST SECOND HALF, which must travel with the first: it is **~1.5% of the render frame**
(35.6-39.8 us/frame against cpu.frame.render.us of 2356-2469). It materially changes the DENOMINATOR
of lighting percentages; it does not reveal a large new slice of frame time.

81% OF IT IS ONE CALL SITE — the forAllEntities / renderLightSources walk, 28.8-32.4 us/frame.
prep 1.2-1.5, spread_scan 5.5-5.9, adjust 0.07-0.08. One place to point a lever, if it ever earns one.

FIVE KEYS SHIPPED (the survey said six; lightingMain's LogMap is documented intent, not a defect):
  lighting.produce.entities.us / .prep.us / .particles.us / .adjust.us / lighting.gpu.spread_scan.us
All owner Frame (the render thread genuinely pays), Detail (slices of cpu.frame.render.us, never
summed), file-scope registered, unit and clock declared.

THREE DEFECTS THE PROJECT'S OWN MACHINERY CAUGHT IN MY WORK:
  * reg_ratchet went red at 92/87 — all five began as function-local statics. Ceiling NOT raised;
    all five moved to file scope. Back to 87, none added.
  * Constructor members (the #181 shape) were ALSO wrong: GpuLightmapPass is built only when
    lightingGpu is on and a WorldPainter exists only while a world is loaded, so a member is ABSENT
    for exactly the captures that matter. The eager block in StarGpuLightmapPass.cpp documents this;
    I had read past it.
  * READING the instrument found a defect IN the instrument: adjust was declared Cadence::Frame but
    fires only inside `if (lightMapUpdated)` — 347 of 900 frames on the smoke leg. Declared Frame the
    consumer would have scaled it 2.59x and invented cost for 553 frames. Now Call; the 90s legs
    confirm n=1742 of 4499, matching lighting.gpu.cpu_cost.us exactly.

METHOD LIMIT, STATED: two legs give a RANGE, not a confidence interval. n=2 supports no significance
claim. Spreads are real (entities 11.0%, prep 18.8%, particles 26.7%) and the producer/consumer ratio
swings 47.3-57.5% between legs, so the headline is an interval by construction.

Evidence: docs/evidence/lighting-produce-first-reading.md (tracked half; harness/profiles gitignored,
same split as matrix-20260808-140430).

=== WHAT THIS OPENED — NOW ON THEIR OWN ROWS, NOT IN THIS CLOSED RECORD ===

This section originally CARRIED the three open questions in prose. That was a defect: a live question
inside a completed task never reaches the ready queue, which is the one thing the durable index
exists to prevent. They now have rows, and this task cites them rather than restating them:

  [#269] LIGHTING-WHOLE — "lighting CPU" is not any owner's total. Three keys under two owners each
         hold part of it and nothing declares the union; `lighting.gpu.cpu_cost.us` (152-159 us/frame)
         is larger than the other two combined. Needs a Director decision, then `.whole` made live.
  [#270] LIGHTING-REDERIVE — #168's closure and #170/#217's border percentages are denominated on the
         short key. Blocked by #269, because re-deriving against a denominator about to change is
         paying twice.

The third item — the model change for a lighting-owned whole — is not separate: it is [#269]'s
item 2, and splitting it from the decision it implements would be two rows for one fact.
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

#### #173 — RB-FLUSH: CLOSED — a CPU lever on a loop that sleeps 72-84% of every frame, at both measured scenes

status: **completed** · blocked by: #242

- `7bede779` [#271] the wait hypothesis is refuted at 0.03% -- it is CPU work, and the per-light section is 38% of it
- `164163cc` [#271] the 152-159 us/frame lump gets seven parts, and one of them is the wait

```
CLOSED 2026-08-07, NOT DONE, and closed the way this task asked to be: "EXPECTED SIZE: unknown and
possibly small now that the stalls are gone... be prepared for this to come back MEASURED NULL like
#129 (VAO-format bake), which was cleanly removed rather than shipped."

THE PREMISE IS PER-FLUSH CPU OVERHEAD -- accumulation-buffer copy, buffer bind, vertex attrib setup,
one draw call apiece -- after #125's orphaning removed the pipeline STALLS. That premise needs CPU
deadline pressure to matter, and there is none at either measured scene:

    Desert Town   cpu.frame.idle.us 11,635 of a 16,195 us frame  -> the loop SLEEPS 72% of each frame
    Ark Ruins     cpu.frame.idle.us ~13,600 of ~16,180 us        -> ~84%
    every leg at both scenes: 61.7 fps, within ~13 us of each other

cpu.frame.idle.us measures sleeping directly, so this closure does NOT depend on the per-phase CPU
attribution that is still unbuilt. Shaving CPU work off a loop that spends most of its time waiting
for the next frame buys nothing a player can see.

WHAT WOULD RE-OPEN IT, so the close is falsifiable: a scene where cpu.frame.idle.us approaches zero --
dense combat, a large base with the full HUD up, or a slower machine -- with render.flush.count still
near 92/frame. Both counters (render.flush.count, render.flush.primitives) are already wired and ride
in every profile, so re-opening costs one capture rather than an investigation.

NOTE THE AXIS. This is a CPU lever and the campaign currently has a rigorous GPU table and no CPU
equivalent. The closure above rests on IDLE TIME, measured directly. It deliberately does NOT rest on
the per-lever CPU deltas, which are noise-dominated today -- baseline spread 488 us on a 4,190 us
signal, and one lever's removal apparently making the CPU FASTER. Those are the CPU-column work, and
they are not used here.
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

UNBLOCKS: #191 (WorldPass contract 1), the BackdropPass split, and large parts of #135, #136 and #138.
```

<a id="c29c1332-175"></a>

#### #175 — SIM-1 DONE: server-tick budget closed 99.76% across 27 phases; compute.entities is the real 65%

status: **completed**

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

- `82711412` render: write the three-term retained-cache contract where the next author will read it
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
- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`
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

#### #196 — BUILD-1 CLOSED: overlay vendored + registered declaratively; pristine clone bootstraps (18a8e5c0, 1830ef31)

status: **completed**

- `f0b9fb1f` ledger: D55/E07/E08/E09 done -- the overlay is in-tree, registered, and verified from a clone
- `1830ef31` build: make mimalloc a vcpkg manifest feature instead of an unconditional dependency
- `18a8e5c0` build: vendor the vcpkg overlay ports and register them declaratively

```
All four ledger rows closed: D55 (root cause), E07 (declarative registration), E08 (retirement conditions), E09 (mimalloc as manifest feature).

ROOT CAUSE CONFIRMED BEFORE FIXING, exactly as filed: the jemalloc + libsystemd overlay ports lived at /root/vcpkg-overlay-ports, outside the repo, reachable only via a hand-passed -DVCPKG_OVERLAY_PORTS recorded in one CMakeCache. Repo-wide grep matched only prose. The patches were not MISSING from a pristine clone, they were UNREACHABLE from one.

E07 (18a8e5c0): 12 files vendored to source/vcpkg-overlay/ports/ + "overlay-ports" in source/vcpkg-configuration.json. This matches the shape the presets ALREADY used for overlay TRIPLETS (VCPKG_OVERLAY_TRIPLETS) -- ports were the omission, not the pattern.

VERIFIED FROM A PRISTINE CLONE, NOT THE WORKING TREE. vcpkg resolved jemalloc@5.3.1#1 and libsystemd@260.2 out of source/vcpkg-overlay/ports with nothing on the command line; the registry carries #0 and 260.1, so the versions identify the source unambiguously. The first pristine configure restored jemalloc from the binary cache in 4.7ms -- that proves RESOLUTION but NOT a from-scratch bootstrap, which is the claim the ticket made. Held the cache entry out, re-ran: "Building jemalloc@5.3.1#1" from source, 0 errors, under the libstdc++ 16 that motivated the patch. Then CUT OVER: cleared the stale cache flag in the working build and deleted /root/vcpkg-overlay-ports from the machine; the working build re-configures green with the directory gone.

E08 (18a8e5c0, source/vcpkg-overlay/README.md): each port names its retirement condition AND, separately, whether anything DETECTS it -- the asymmetry is the substance. jemalloc SELF-RETIRES, ENFORCED: its entire delta is one vcpkg_replace_string of std::__throw_bad_alloc(), and that helper hashes the file either side of the substitution and raises Z_VCPKG_BACKCOMPAT_MESSAGE_LEVEL when nothing changed -- which scripts/ports.cmake sets to FATAL_ERROR. Upstream fixing the call STOPS the build and names the port. libsystemd has NO such mechanism (version pin + -Dwerror=false both build silently when redundant); recorded as manual, trigger = baseline bump, rather than implying parity.

E09 (1830ef31): mimalloc is now a manifest feature selected from STAR_USE_MIMALLOC. Worse than "off by default" -- NO preset sets it (Windows rpmalloc, Linux presets jemalloc, macOS jemalloc off), so it was built on every platform for every developer and linked by no shipped configuration. VCPKG_MANIFEST_FEATURES must be appended before project() (the toolchain reads it there), which is before option() declares the variable, so the cache value is the only readable source and an unset one correctly selects nothing. Proven BOTH directions in the clone: default green with mimalloc absent; -DSTAR_USE_MIMALLOC=ON installs 3.3.2 and the REQUIRED find_package resolves -- the arm that catches B17's configure break.

libsystemd confirmed LIVE, not dead weight: sdl3[dbus] -> dbus -> libsystemd, all three in the installed tree.

run-gates 27/27 green. Bookkeeping f0b9fb1f.
```

<a id="c29c1332-197"></a>

#### #197 — FBO-3: effect-parameter state persists per effect and nothing asserts it -- the `world` effect is the exposure

status: **pending**

- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`

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

#### #200 — ARCH-1 DONE: boundary doc + arch-graph generator + arch_graph_fresh gate all in the tree and green

status: **completed**

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

#### #201 — CI-5 DONE: the native path separator is gone — as_posix() at boundary-inventory.py:116, with the warrant at :109

status: **completed**

- `e7368000` fix(gates): boundary_fresh measured a different tree on Windows than on unix

```
ROOT CAUSE, proven not guessed. scripts/boundary-inventory.py game_files() built keys with str(p.relative_to(REPO)), which stringifies with the NATIVE separator. On windows-latest every key was `source\game\Star*.cpp` while VIEW_BY_DUTY is written with forward slashes, so nothing matched: the view-by-duty total reported 0 instead of 136 and all twenty table rows flipped from "view" to "sim". Simulated locally: 46-line diff between the Linux and Windows reports.

BLAST RADIUS: boundary_fresh failed on the Windows job and passed on the four unix jobs, so Build has been red on integration for three consecutive runs (30194960202, 30195729180, 30198588776) since the gate was registered. It is NOT caused by #200 -- arch_graph_fresh PASSED on Windows (2.84s) in the same run, and the two earlier red runs predate that commit.

FIX: p.relative_to(REPO).as_posix(). board-export.py already used this idiom; boundary-inventory did not. config-lint.py normalised to match, flagged in-comment as cosmetic there (its `rel` only reaches an error message that a passing run never prints, so it could not change a verdict).

VERIFIED: Linux output byte-identical after the fix (the committed doc still matches, no regeneration needed); PureWindowsPath demonstration shows old code misses VIEW_BY_DUTY and new code hits; all 7 script gates pass.

THE LESSON, which is the same one as #192 and the oracle-vocabulary trap: a measurement that depends on which machine ran it is not a measurement. Two of three such defects in this campaign have now been platform-conditional gate output.
```

<a id="c29c1332-202"></a>

#### #202 — RI-2 DONE: both L1 files claimed, the application partition exists, the fence holds six, gated residual 17 of 17

status: **completed**

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

status: **in_progress** · metadata: `{"rank": 13}`

- `3eed5ab8` [#204] D14: A1 and A2 bind, and `telemetry` is what A1 forbids
- `d4d07d9a` [#204] the section-7 compile diagram was missing 24 edges its own grant table requires
- `84fe2324` ledger: C08 and C16 done -- the PR-570 backlog is now fully settled
- `c8bf863b` ledger: C11/D31 done at 7879e0a2; the beam divergence they exposed is now #228
- `c363e052` ledger: D29 declined by Director decision; B08 closed with it
- `fa6f4d8f` ledger: ten rows were done in the tree and still recorded as accepted
- `a0bbb0c1` ledger: D11 done at a9ca6e26; D29's measurement trigger has fired and it now needs a decision [#204]
- `1899aa71` ledger: C05 done at 9429b14d; E01-E04 repointed to #221 -- closing #214 had orphaned two accepted rows [#204]
- `02de4584` fix(spec-artifact): stamp the page's provenance, not HEAD
- `e386d983` ledger: A05/E05 done at 66ec860b -- the sector-unload UAF is closed [#204]
- `46894dae` board: #212 closed at f5088933; ledger row C17 -&gt; done [#204]
- `466c19cc` ledger: surface the challenge corrections, and file the three adoptable items [#204]
- `82170188` ledger: status per row, in a decisions file the generator reads -- and the generator now exists [#204]
- `0481bb82` docs: PR 570 findings ledger -- 155 rows, 33 open decisions, generated not transcribed [#204]
- `566fce3d` docs: a durable handover for #204, and the board export that makes its ids resolve [#204]
- `21ba119a` spec: `statistics` is not a platform service -- stage 2 of dissolving `platform` [#204]
- `5d276333` spec: the numerator nobody read -- four stale figures, two of them whole tables [#204]
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

#### #207 — TSSA-2 DONE: the levelling pass closed; its one "remaining" item shipped at 706c7f4a and its blocker was never real

status: **completed** · blocked by: #206, #205 · blocks: #208

- `60a66f02` spec: P2P networking is a TRANSPORT -- stage 1 of dissolving `platform`
- `706c7f4a` spec: split `celestial` -- 45 -&gt; 48 components, and the split was already in the tree
- `890218ab` spec: symmetry decides the kind -- the rule three pairs already followed
- `2920def1` gates: A3's Law of One, finally read -- and I nearly cheated it
- `a64b21c7` spec: a target state has no date -- 7 dates, 6 removed, 1 declared
- `337f5494` spec: the CONTRACT sweep -- 25 sites read and decided, ratchet 25 -&gt; 0
- `d2eadae4` spec: the KIND rules said "foundation types" -- two INTERFACEs already broke it
- `9442b154` spec: two rules the gate already contradicted -- and the gate was right
- `248b9a74` docs: file the 44 upheld audit findings; 57 of 103 were refuted [#207]
- `ed5e7125` spec: Section 7 defines all six KINDs -- it defined five, and one was retired
- `cdea71c1` gates: table_census -- because I answered "how many tables?" wrong three times
- `de57756e` spec: F2's evidence clause described a policy, not the code -- and the code is better
- `82955390` gates: N1.d is countable, and the metric was in the document the whole time
- `41b7adfd` spec: Section 1's rule was about REUSE, and now says so -- and is checked
- `3ee6021d` docs: file the levelling analysis as a WORKLIST, not a findings register
- `170a17cb` gates: BARE_GOAL read the shape of a citation, not its meaning
- `eab0b4da` gates: a warrant must name a clause, and the check found two I had missed
- `5f2933ad` spec: repoint every N2 and N3 warrant at the clause it actually makes
- `a96a3575` spec: level Section 3 -- N2 and N3 get clauses, because their citations already had them
- `6d5100fb` gates: correct the containment row at its generator, and a false green
- `78a8a4b5` gates: an instrument the document names must be one that runs
- `51415bfe` gates: the UNANSWERED check existed and never ran
- `f5faf4cf` spec: CONTRACT was two kinds, and the split found a missing component
- `5db63949` spec: `platform_null`, and a contract granted 18 times and called by 4
- `b1b34d9a` spec: RenderCallback is the EMIT surface, and the scene is what a painter READS
- `550a3b5e` spec: a rate with a number, a reason and no element
- `b976ae79` spec: Section 11 counted five clocks and then said four
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

status: **pending** · blocked by: #207 · metadata: `{"rank": 11}`

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

<a id="c29c1332-209"></a>

#### #209 — TSSA-4: give the derivable numbers an owner — 2 live defects found, ZONE tally + closure numerators ungated

status: **completed**

```
DONE at 5d276333, pushed to origin/integration, 21/21 gates green.

FOUR stale figures found, not the two originally filed — all green through 21/21 gates because
COUNT_DRIFT reads the DENOMINATOR of an `N of M` and never the numerator:
  1. §7 ZONE tally: machine 14, domain 14 — column summing to 45 beside a 49-row register
  2. §12 TWICE: two independent tables at `32 of 41` / `26 of 41` / `19 of 41` — the 41-component
     era, two register generations stale, one with the correct figures in the prose beneath it
  3. `world_sim` "linking 12 of 49", contradicting the generated diagram above it saying 13;
     the missing member is `platform`
  4. the subtraction claim: "minus `windowing`, `frontend`, `transcript` ... three grants" — the
     real grant difference is FIVE (omitted `colocation` + `starmap_authority`, the two carrying
     the argument), and five grants remove NINE components
  plus "15 references" -> 29 across 18 lines (the figure gates.yml documents as the undercount)

FOUR instruments so they cannot go stale again: tree-map#zones (generated), CLOSURE_DRIFT (3 tiers),
MODAL_ZONE, SUBTRACTION. ZONE_FACES declared once in spec-model (§7's table and composition-graphs'
diagram titles had drifted apart on 3 of 4). STALE_ZONE no longer reads HTML comments.

Stages 2-5 of the `platform` decomposition no longer require hand-editing any closure figure —
the gate names the exact number.
```

<a id="c29c1332-210"></a>

#### #210 — RACE-1 DONE (66ec860b): sector unload now holds m_lightMapPrepMutex; TSan verification DEFERRED

status: **completed**

- `66ec860b` fix(lighting): hold m_lightMapPrepMutex across sector unload

```
Use-after-free: WorldClient::update unloaded sectors with no lock while the lighting thread gathered through them. SectorArray2D::evalColumnsPrivPar hands workers a raw Array* into sector storage; unloadSector -> takeSector frees it immediately; SectorArray2D has no internal synchronisation.

FIX (66ec860b): the unload loop takes m_lightMapPrepMutex, which lightingCalc already holds across the whole gather -- no new lock. Cost is one gather of main-thread stall, 93-276us (#168), against a 16.6ms frame.

ROOT CAUSE of reachability: the gather's comment claimed the calc region is "strictly inside the loaded-sector region". False. With windowMonitoringBorder=32: loaded = window +64..+95; calc min = -49 (safe by 15); calc max = window +1 +bucketSlack(<=31) +border(<=48) = +80, exceeding +64 by up to 16. The +31 is #127's grid-size bucket -- it rounds the light-window SIZE up to a multiple of 32 anchored at the min corner, growing the region on the MAX SIDE ONLY, while neededSectors stays derived from the UNBUCKETED window. #127 was measured and right for its purpose (texture-upload churn 7.09% -> 2.37%); the widening went unnoticed because nothing measured it. The rare conjunction required (size just past a bucket boundary, border near 48, unfavourable alignment) explains why the crash never reproduced on demand.

INSTRUMENT: the false comment is replaced by the arithmetic plus counter lighting.gather.calc_outside_loaded, gated on Telemetry::enabled() so the per-gather sector scan is one branch when telemetry is off.

PROVEN: correct by construction (reader holds the mutex across the entire gather); 346 tests pass (277 core_tests + 69 game_tests); 22/22 gates green.
NOT PROVEN: the race was never observed firing and the fix was never observed eliminating it. No TSan preset exists and jemalloc conflicts with TSan.

OPEN FOLLOW-UPS:
1. TSan verification -- needs a TSan preset with jemalloc disabled. Not built.
2. Read lighting.gather.calc_outside_loaded from a real play session. Zero => alignment has been covering us, and widening neededSectors is unnecessary. Non-zero => derive the sector padding from the calc region rather than letting it coincide.
3. The mutex name now under-describes its duty. Any future cross-thread reader of m_tileArray must take it and nothing enforces that; the real fix is for SectorArray2D to own its lifetime contract (core container, shared with the server) -- candidate for the sim-side sovereignty refactor.
```

<a id="c29c1332-211"></a>

#### #211 — GL-GUARD-1 CLOSED: guard b1e66be4 + ARB arm now dispatches to glMinSampleShadingARB (b18797e4)

status: **completed**

- `b18797e4` fix(gl): the sample-shading guard admitted ARB but called the core 4.0 entry point
- `b1e66be4` fix(gl): guard the GL 4.0 sample-shading calls; enforce the A/B config rule

```
ORIGINAL SCOPE IS DONE. b1e66be4 added the GL 4.0 capability guard and kept the feature (ledger row A07 now done); #151 measured per-sample shading at 22.3% of pixels, so we deliberately guard rather than take PR-570's deletion.

REMAINING SCOPE -- A REAL DEFECT INSIDE THE GUARD, found by the 2026-08-04 ledger audit, not by any gate.

source/application/StarRenderer_opengl.cpp:962 admits either capability:
    bool sampleShading = GLEW_VERSION_4_0 || GLEW_ARB_sample_shading;
then lines 967 and 971 unconditionally call glMinSampleShading(...).

GLEW maps that name to __glewMinSampleShading, which is the CORE 4.0 entry point. Confirmed in
GLEW's own source, not merely its headers:
    _glewInit_GL_VERSION_4_0        loads glMinSampleShading    (__glewMinSampleShading)
    _glewInit_GL_ARB_sample_shading loads glMinSampleShadingARB (__glewMinSampleShadingARB)
They are SEPARATE pointers loaded by SEPARATE initialisers (glew.h also carries a third, ...OES).

So on a driver exposing ARB_sample_shading WITHOUT GL 4.0, sampleShading is true and
__glewMinSampleShading is NULL -> null function-pointer call. The guard converts a benign GL error
into a crash on exactly the driver class it was written to protect. The primary case the row named
(3.x with no sample-shading capability at all) IS correctly guarded; only the ARB arm is wrong.

FIX, either is defensible -- Director's call:
  (a) dispatch the ARB arm to glMinSampleShadingARB, keeping ARB-only drivers working; or
  (b) drop `|| GLEW_ARB_sample_shading` and require core 4.0, which is simpler and matches what the
      code actually calls today.
(b) is the smaller change and loses per-sample shading on ARB-only hardware; (a) preserves it.
GL_SAMPLE_SHADING as an enum is fine either way -- the enum value is shared; it is only the function
pointer that is split.

NOT REPRODUCIBLE ON THIS WORKSTATION: the Arc Pro reports GL 4.6, so this hardware always takes the
VERSION_4_0 arm and cannot exercise the bug. Verification must be by construction (read the
dispatch), or by forcing the ARB arm behind a debug override -- do not claim it fixed off a local
run that never entered the branch.
```

<a id="c29c1332-212"></a>

#### #212 — TEST-CI-1 DONE (f5088933): 20 lighting assertions now run in CI; configure-time guard proven to fire

status: **completed**

- `f5088933` test: the lighting tests now run in CI -- 20 assertions that guarded nothing for their whole life [#212]

```
FOUND 2026-08-04 comparing against OpenStarbound PR 570. A defect in our own verification, not a
decision: we built the tests and locked them in a target that never executes.

MEASURED 2026-08-04, so this is no longer an argument. Run from dist/, E-core pinned, headless, no
display, no interaction:

    ./game_tests --gtest_filter='LightingTelemetry.*:LightingSpread.*:LightingPoint.*:
                                 TemporalLightingGate.*:CellularLightingTonemap.*'
    => 20 tests from 5 suites ran. 3 ms total. 20 PASSED. exit 0.

  per-filter wall clock, same binary:
    LightingPoint.*   5 tests   0.00 s     <-- asset-free
    RootTest.*        1 test    2.82 s     <-- actually loads assets
    ItemTest.*        3 tests   1.51 s     <-- actually loads assets

  Root construction is LAZY. Sitting in game_tests costs the lighting tests NOTHING; the seconds
  belong to the tests that genuinely boot a universe. So the exclusion is by TARGET MEMBERSHIP, not
  by cost and not by dependency. That is the whole finding, now with a number on it.

THE STATE
  source/test/CMakeLists.txt:78-82 puts all five lighting test files in game_tests:
      lighting_telemetry_test.cpp   lighting_spread_test.cpp   lighting_point_test.cpp
      temporal_light_gate_test.cpp  cellular_lighting_test.cpp
  source/test/CMakeLists.txt:495:
      SET (star_tests_needing_assets game_tests)  # game_tests boots a universe from assets/ -- cannot run in CI

  => 20 lighting assertions run on zero platforms, on every push, forever.

THEY DO NOT NEED ASSETS. Verified per file: zero Root::singleton, zero assets(), config built from
inline Json::parseJson. The only thing tying them to game_tests is game_tests_main.cpp:41, which
AddGlobalTestEnvironment's a Root for the WHOLE binary.

THE CONTRAST THAT FOUND IT. PR 570 labels core_tests "NoAssets" (their CMakeLists:94), filters on
that label in their base test preset, and build.yml drives ctest through it on Linux, Linux-ARM,
macOS, macOS-ARM and Windows. Their 9 lighting tests run on five platforms every push. Their remedy
is also demonstrated: compile ../base/StarCellularLighting.cpp directly into core_tests.

FOR SCALE: core_tests, which CI DOES run, is 257 tests in 7.01 s. Adding 20 lighting tests that cost
0.00 s is free.

WORK
  1. move the asset-free lighting tests into core_tests (or a new asset-free target), compiling the
     base-layer sources they need
  2. ASSERT THEY EXECUTE -- a count, not an assumption. A test that silently does not run is the
     exact failure this task exists about, and it would be absurd to fix it by hope.
  3. check whether other game_tests members are equally asset-free hostages (drawable_cache_test and
     animated_part_set_test are candidates) and move what qualifies
  4. leave genuinely asset-dependent tests where they are; the star_tests_needing_assets guard at
     :495 is correct and must keep working

BLOCKS: #213, #214, #215, #216 all land in lighting code. Until this is done their only defence is
"Claude ran them by hand", which is the shape of claim this project exists to delete.

RELATED: #194, #195 built the gate-running machinery; this is the same class one level down --
machinery that exists and is not pointed at the thing it was built for.
```

<a id="c29c1332-213"></a>

#### #213 — LIGHT-DEDUP-1 CLOSED (7879e0a2): 140 lines -&gt; 70, byte-identical at instruction level; beam divergence declared and split to #228

status: **completed**

- `0fed2045` fix(lighting): unify the beam guard on 0 -- monochrome and colored disagreed
- `c8bf863b` ledger: C11/D31 done at 7879e0a2; the beam divergence they exposed is now #228
- `7879e0a2` refactor(lighting): one calculatePointLighting body, two instantiations -- byte-identical

```
FOUND 2026-08-04 via the OpenStarbound PR 570 comparison. Small, real, and NOT purely mechanical --
one of the two differences is a behavioural divergence that needs a decision.

THE DUPLICATION
  source/base/StarCellularLightArray.cpp:12-78   ScalarLightTraits specialization
  source/base/StarCellularLightArray.cpp:85-151  ColoredLightTraits specialization

VERIFIED BY NORMALISED DIFF (substitute Scalar/ColoredLightTraits -> LightTraits, strip trailing
whitespace, then diff). The two bodies differ in exactly TWO lines:

  1. SEMANTIC -- the beam epsilon:
         scalar :   if (light.beam > 0.0001f) {
         colored:   if (light.beam > 0.0f) {
     These diverge for beam values in (0, 0.0001]. Unifying means CHOOSING one, or keeping the
     epsilon as a traits constant. Do not silently pick; whichever way it goes it is an output
     change for some inputs, and the scalar path is player-visible (see RISK below).

  2. COSMETIC -- one line differs by a single leading space.

Everything else is already generic over the traits: maxIntensity, subtract, max, operator+ and
operator* are all traits operations. So the merge is a plain LightTraits:: substitution with ZERO
new hooks and no `if constexpr`.

DO NOT IMPORT PR 570'S MECHANISM. Their unification carries traits hooks -- spreadDrops,
subtractChannels writing into float contrib[ComponentCount], channels() handing out a float const*
-- and every one of those exists ONLY to service their channel-major SoA storage (their
StarCellularLightArray.hpp:453-457, sink is float* dst[ComponentCount] into m_pointChannels). We are
not taking the SoA, which removes the entire reason the hooks exist. Taking the abstraction while
rejecting its justification is the trade to avoid.

RISK IS HIGHER THAN "IT IS ONLY THE CPU FALLBACK" -- that framing is FALSE and it is the reason this
task carries more care than its size suggests:
  CellularLightIntensityCalculator holds a ScalarCellularLightArray (StarCellularLighting.hpp:237)
  and its calculate() (StarCellularLighting.cpp:395) runs the scalar calculatePointLighting
  UNCONDITIONALLY. That path is:
    - WorldServer::lightLevel()        StarWorldServer.cpp:2645
    - WorldClient::lightLevel()        StarWorldClient.cpp:2789
    - the Lua callback world.lightLevel  StarWorldLuaBindings.cpp:636   <-- MOD-FACING
    - read by StarMainInterface.cpp:574
  lightingGpu / skipCpuCalc (StarWorldClient.cpp:2401) does NOT bypass it. So this is live on the
  dedicated server and in every mod that queries light.

WHAT THIS DOES AND DOES NOT BUY. It takes the point-light model from 8 transcriptions to 7. The
other six are NOT duplication and must not be merged:
    .hpp:541            lineAttenuation -- the shared Wu primitive the kernel calls
    .cpp:170            verbatim mirror, deliberately INDEPENDENT so it can falsify production
    .cpp:310            obstacleRaycastDDA -- the GLSL-portable form; merging defeats the de-risk
    .cpp:379            pointLightingReference -- the oracle; shared math cannot detect a regression
    lightingPoint.frag:34, :86   the GPU shader -- a language boundary, not duplication

VERIFICATION: byte-identity for every beam value outside (0, 0.0001], by construction. For the
epsilon decision itself, state the chosen behaviour explicitly in the commit. NOTE that our lighting
tests currently run in ZERO CI jobs (#212) -- so the oracles that would defend this change do not
execute. Prefer landing #212 first, or run the lighting tests by hand and say so.
```

<a id="c29c1332-214"></a>

#### #214 — GATHER-1 DONE (9429b14d): one gatherColumns + two sinks, byte-identical; E01/E04 unblocked. Gate's own A/B fixed (cb17d323)

status: **completed**

- `cb17d323` fix(render-gate): the in-process A/B was invisible and ungated

```
From PR-570 ledger row C05 -- one of the few axes where their design beat ours on simplicity.

THE FORK WAS THE SINK, NOT THE REGION. C05 called it region-parameterisation; the region was the least of it. lightingTileGather and gatherStableColumns were near-duplicate 45-line bodies whose per-tile emission arithmetic, B1 column staging and B2 material-run memo were identical text in two places. The only real difference: one folds environmentLight in and calls setCellColumn per column; the other records skyExposed and writes GatherCell per tile.

gatherColumns(region, sink) now holds the computation once. gatherStableColumns: 45 lines -> 12.

BYTE-IDENTICAL, by add ORDER. The direct path folded environmentLight LAST (after fg + liquid + bg); the sink folds it in the same position, and applyStableToCells already reconstructed it identically as stableLight + environmentLight. Float addition is not associative, so the header carries the one rule that matters: do not move the env fold into gatherColumns.

VERIFIED THREE WAYS:
  textual  the per-tile arithmetic diffs line-for-line against HEAD; the single change is
           `light += environmentLight` -> `skyExposed = true`, add relocated to the sink
  runtime  render-gate in-process A/B, lightingGatherCache true|false: both legs hash
           471488eed310861f. Oracles 112/26/202, 0 DIFF, 0 GL errors, 0 state desyncs
  tests    282 core_tests + 69 game_tests; 24/24 gates green

NOT PROVEN: the A/B is a MUTUAL check between the two sinks. Break both identically and it passes. Old-vs-new rests on the textual diff.

NO PERFORMANCE CHANGE claimed or intended. Value is: ~33 lines deleted; ledger rows E01 and E04 unblocked (both were deferred specifically on this landing); E02/E03 now land here; and one divergence hazard closed -- two copies of identical float arithmetic kept in lockstep by discipline alone, which is exactly the still-open #213 (two calculatePointLighting specializations two lines apart, one of them semantic).

FOUND WHILE VERIFYING, fixed in cb17d323: the render gate's in-process A/B was invisible AND ungated. The guard grepped "RENDERTEST_AB" (never written to the log) and the body grepped "renderTest" while the log writes "rendertest" -- both scored zero, so the section vanished; and it was an echo that never touched `pass`, so a DIFFering A/B certified as GATE: PASS. Third instance of the vocabulary trap in a file already carrying two warnings about it. Every A/B ever run through that gate was unjudged. Now asserts both legs exist, fails on DIFFER, fails on a missing leg -- all three proven by injection.

PROCESS NOTE: the first render-gate commit claimed "24/24 gates green" before I read the exit status. The suite was red (arch_graph_fresh, from this task's own uncommitted line-count changes). Amended to state the truth rather than leaving a false claim in the history.
```

<a id="c29c1332-215"></a>

#### #215 — ORACLE-MOVE-1 CLOSED (c1a4433d): oracles split to their own TU; kernel object proven unchanged; task's split corrected

status: **completed**

- `84fe2324` ledger: C08 and C16 done -- the PR-570 backlog is now fully settled
- `c1a4433d` refactor(lighting): split the differential oracles out of the kernel TU

```
Oracles moved to source/base/StarCellularLightingOracle.{hpp,cpp}. Kernel keeps only the kernel.

THE TASK'S PROPOSED SPLIT WAS WRONG IN ONE RESPECT AND IS CORRECTED. It listed SpreadParameters and PointParameters as things to move. They must NOT move: CellularLightingCalculator DECLARES both as its accessor return types, and PointParameters is a member of LightmapParams on the shipping GPU path (StarGpuLightmapPass). Moving them would have made production depend on an oracle header -- the exact inversion this refactor exists to remove. They stay in the kernel; the dependency now runs oracle -> kernel and never the other way.

WHAT MOVED: ObstacleRaycast, spreadJacobiReference + pointLightingReference declarations and bodies, and the anonymous-namespace obstacle helpers. 55 header lines + 379 cpp lines.

PURE RELOCATION, PROVEN NOT ASSERTED: kernel object disassembled before and after and compared address-independently -- ScalarLightTraits 369 -> 369, ColoredLightTraits 450 -> 450, zero instruction differences. Symbol partition is clean: the kernel object now exports only lineAttenuation + calculatePointLighting; the oracle object exports exactly the two references.

CONSUMERS: StarWorldPainter.cpp now includes the oracle header explicitly, so the parity-localizer dependency is DECLARED rather than arriving incidentally through the kernel header. Two test files updated. Two comments in StarCellularLighting.hpp that named the old home were fixed -- a reference outliving its referent is the drift class this project has hit repeatedly.

A STALE PRECONDITION IN THIS TASK, corrected: it said the lighting tests "run in ZERO CI jobs (#212)". #212 is DONE -- core_tests is ctest-registered with the NoAssets label and the preset filters to exactly that, so they DO run in CI. Verified before relying on it.

HONEST ACCOUNTING, as the task demanded: buys no runtime performance, does not shrink the binary. The code still ships; it just stops being in the way of reading the kernel.

core_tests 289/289 at the time, game_tests 72/72, run-gates 27/27.
```

<a id="c29c1332-216"></a>

#### #216 — ORACLE-TRUTH-1 CLOSED (7dbf8c98): 5 closed-form assertions + an experiment proving differential tests are blind to a shared error

status: **completed**

- `84fe2324` ledger: C08 and C16 done -- the PR-570 backlog is now fully settled
- `7dbf8c98` test(lighting): closed-form assertions, and an experiment showing what they catch

```
Added source/test/lighting_closedform_test.cpp -- five assertions whose expected values are derived IN THE TEST from the attenuation rule and never from any implementation:

  AirAttenuationIsLinearInDistance      d=3,6,9 -> 1 - d/12 (three points, so a slope change fails too)
  DiagonalDistanceIsEuclidean           d=sqrt(18); Manhattan says 6 and Chebyshev says 3, so the
                                        diagonal is the only case that discriminates the three metrics
  LightOnACellCentreIsUndimmed          production's distance==0 branch, which skips attenuation
  NoLightBeyondPointMaxAir              d=12 and d=15 dark, PLUS d=11 lit as a control so it cannot
                                        pass by everything being dark
  BrightnessLimitScalesTheSumToTheLimit proportional scale to 1.4, not a per-channel clamp

THE ROW'S CENTRAL CLAIM IS NOW DEMONSTRATED, NOT ARGUED. Two injections of the same 2%
air-attenuation error:
  * PRODUCTION ONLY          -> 3 closed-form tests RED. They bind to production output.
  * PRODUCTION + THE ORACLE  -> DIFFERENTIAL suite 8/8 GREEN, closed-form still 3 RED.
The second run is the row's surviving claim made concrete: a differential oracle written from
production pins DRIFT, not TRUTH, and with the same misunderstanding on both sides it is blind. Only
an assertion about the MODEL survives. Both injections reverted via git checkout and verified
byte-identical to committed.

THE TWO THAT DID NOT FIRE ARE CORRECT, and recorded so nobody reads it as weak coverage:
LightOnACellCentre takes the zero-distance branch (attenuation never computed) and BrightnessLimit
reads a cell whose sum still exceeds the limit after a 2% shift. They fire on what they are about.

COMPLEMENT, NOT REPLACEMENT: the differential tests catch drift across the CPU/GPU/reference
triangle, which closed-form cannot; these catch a shared misunderstanding, which differential cannot.
Two failure modes, two instruments -- exactly as the row framed it.

The row's own caveat is preserved: "zero absolute assertions" would have overstated the gap. Two
loose bounds already existed in lighting_point_test; what was missing was a MODEL-derived value.

core_tests 289 -> 294 all green, game_tests 72/72, run-gates 27/27. They run in CI (#212).
```

<a id="c29c1332-217"></a>

#### #217 — BORDER-1 DONE (817e54e8) + MEASURED at the scene that exercises it: lever -27.8% region / -10.1% lighting CPU; fix costs +5.3% region

status: **completed**

- `817e54e8` fix(lighting): adaptive border under-sized coloured lights, dropping them

```
FIX: 817e54e8. #170's border estimator measured point-light reach from the channel MEAN while the engine uses the channel MAX, so saturated coloured lights 32-48 cells outside the query rect were DROPPED, not dimmed. Fixed with color.max(), a half-open +1, and a geometry wrap. Five tests in core_tests, each proven to fire by injection.

MEASURED 2026-08-04 on the headless live-profile harness (real Intel Arc GPU, offscreen EGL, sim running, unattended). All legs adjacent, same bookmark, config pin echoed per leg, acceptance test = calc.cells must differ.

THE SCENE MATTERS, AND `explore` IS THE WRONG ONE. A 30s probe per location reading lighting.border.needed (computed unconditionally, so it is a valid search key regardless of the lever):
  explore              border  4   maxInt 0.96   12,660 point of 35,865 sources
  00-Ocean-Lab         border 31   maxInt 1.00   19,392 point of 19,392
  01-Lava Refinery     border 31   maxInt 1.00   41,520 point of 58,062
  03-Surface Outpost   border 28   maxInt 1.00   21,411 point of 21,909
  04-Ocean Factory     border 34   maxInt 1.00   16,950 point of 16,950   <- clears the 32 floor
Director's intuition, correct: the machine bases run 7-8x explore's border requirement.

A/B AT 04-OCEAN FACTORY (border 34, the only location where the border is genuinely adaptive):
  lighting.calc.cells        25872 vs 35840   -27.8%
  lighting.cpu.total.us       77.8 vs  86.5   -10.1%
  lighting.gpu.spread.gpu_us 574.3 vs 655.5   -12.4%
  lighting.cpu.export.us      50.9 vs  60.9   -16.6%
A/B at explore (border pinned at the 32 floor): -31.4% region, -11.2% lighting CPU.
The lever saves LESS in harder scenes because they need a wider border. Degrades gracefully.

THE GIVE-BACK, DERIVED. The cell counts resolve exactly to a 128x64 light window: b=48 -> 224x160 = 35840; b=32 -> 192x128 = 24576; b=34 -> 196x132 = 25872, no residue. With maxIntensity 1.00 present, the old mean-based estimator under-credited saturated lights ~3x and would have fallen below the 32 floor, clamping to 24576. The corrected estimator returns 34 -> 25872. So the correctness fix costs +5.3% lighting region (1296 cells) at this scene, against a lever still worth 27.8% region and 10.1% CPU. ASSUMPTION STATED: that the old estimator landed below 32 here. Not executed on a pre-#217 binary; derived from the saturation ratio and the observed floor behaviour.

STRUCTURAL FINDINGS BEYOND THE BORDER (both recorded in the render-harness memory):
- The machine bases are almost ENTIRELY point lights (Ocean Lab, Ocean Factory: sources == point lights exactly). explore is the opposite, mostly spread. A point-light lever is a BASE lever, not an exploring one.
- LIGHT COUNT DOES NOT PREDICT LIGHTING CPU. Lava Refinery: 41,520 lights at 240.7us. Ocean Factory: 16,950 at 73.8us. Ocean Lab: 19,392 at 282.9us. Region size and recompute rate dominate. Direct warning for ledger row D29 -- do not size a point-light cap from a count.

STILL OUTSTANDING:
- Defect 3 (unwrapped positions) is fixed but untested; world geometry is unreachable from a core_tests unit test. Needs a seam scene.
- No pixel confirmation that previously-dropped lights are back. scripts/render-gate.sh can do this as a byte-identical A/B, unattended. Not run.
- The give-back is derived, not executed. A pre-#217 binary at 04-Ocean Factory would measure it directly.
```

<a id="c29c1332-218"></a>

#### #218 — COMMENT-1 DONE (9e56c204): rule written + 6 wrong comments swept + all 9 line-refs converted + comment_claims gate at zero

status: **completed**

- `bc0305df` [#245] the client reads three keys it does not own, instead of re-declaring them
- `9e56c204` docs(comments): the comment standard, the sweep, and the one gate it earns

```
DONE at 9e56c204. docs/comment-standard.md carries the rule; scripts/comment-claims.py is the gate; 24/24 gates green (was 22).

THE RULE: a comment may not assert what no instrument can catch going false, and may not restate what the code already says. Rule at the site, derivation in the commit. Symbols not line numbers. Measurements carry their task id.

SCOPE (Director-approved, set by measurement): applies to code we author or touch, enforced at review. NOT a tree-wide cleanup -- narration is ~7% of the corpus and ~95% of that is upstream; our own 5,549 comment lines yielded four hits, three legitimate.

SWEPT (6): StarWorldClient.hpp (guard-condition vs thread-ownership warrant), StarSectorArray2D.hpp (evalColumnsParallel given its own contract incl. concurrency + lifetime obligations), StarTelemetry.hpp x2 (stale design citations), StarRenderer_opengl.cpp (stale hazard citation), StarWorldServerThread.cpp (self-citation replaced by the compiler-enforced fact).

SURVEY CORRECTION: the finding on StarWorldServerThread claimed update() is public and a second caller was "one edit away". It is private and always was. The finding stood; that supporting fact did not. Recorded because the survey's own accuracy is subject to the same rule.

GATE: bans file:line citations. Ban not checker -- population was 9, all ours, 55% rotted, and two written on one day were both wrong in ten. Landed at ZERO (all 9 converted, including the 3 that still resolved). Proven to fire by injection; selftest asserts 11 catch / 8 no-catch.

NOT BUILT, with reasons recorded in the doc: task-id gate (5 namespaces share `#N`), dangling-symbol gate (~11 deliberate post-mortem refs would false-positive, 2:1 over 6 real), config/metric keys (0 dangling, config_declared covers half), measurement claims (not mechanisable). Comment-to-code ratio ratchet REJECTED -- satisfied by deleting a true comment.

FOLLOW-UPS NOT DONE:
1. docs/**/*.md was never scanned for line refs into source. The survey flagged this as probably a LARGER population than the source tree (9 sites), and docs rot with no compiler at all. The gate currently scans source only.
2. Non-C++ comments unscanned: assets/**/*.lua, *.frag/*.vert shaders, CMakeLists.txt, scripts/*.py. Shaders are cited BY C++ comments and may cite back.
3. The measurement-claim writing rule (carry the task id) is stated in the doc but 102 existing claims on 76 lines remain undated and unsourced. No sweep done.
4. Symbol checking in the survey was whole-word presence, NOT scope-aware -- "0 dangling qualified symbols" means "0 names absent from the tree", not "0 names in the wrong class". A scope-aware pass would likely find more.
```

<a id="c29c1332-219"></a>

#### #219 — LEDGER-B DONE: pr570-ledger reports 155 rows / 0 open; D29 declined by the Director 2026-08-05 (c363e052)

status: **completed**

- `c363e052` ledger: D29 declined by Director decision; B08 closed with it
- `9ccb57a8` ledger: sweep the 10 A-rows closed on one unchallenged verdict
- `e93cefd5` ledger(html): double the content width -- the page is scanned as a table, not read as prose [#219]
- `3b0214db` feat(ledger): render the artifact from the generator, not by hand
- `e3682234` ledger: all 35 B-rows settled -- 50 open -&gt; 15; B21 done at 0a8fff9d [#219]
- `0a8fff9d` refactor(lighting): delete the dead LightTraits::multiply members

```
The PR-570 ledger's B section (B01-B35) was the tail of the claim list an earlier run CAPPED: it enumerated 47 claims, checked 12, and logged the 35 it dropped. Those 35 carried the verdict "not examined". All 35 are now settled.

OUTCOME: 13 NOT_REAL, 7 REAL_BUT_NOT_OURS, 7 COVERED by decisions already taken, 8 flagged as things we carry -- of which SIX were REFUTED on adversarial verification. Two survived.

SURVIVOR 1 -- rm-lighttraits-spread-multiply. DONE. `ScalarLightTraits::multiply` and `ColoredLightTraits::multiply` were dead in our tree: 2 declarations + 2 inline definitions in source/base/StarCellularLightArray.hpp, zero call sites repo-wide (verified independently by Claude -- every other `multiply` hit is Color::multiply, ItemDescriptor::multiply, BlendMode::PremultiplyInto, or a Lua/directive string). Deleted. The `spread` half of their removal is REJECTED: `spread` is live here with 12 call sites in calculateLightSpread plus the Jacobi oracle mirror, and they could only delete it because the same patch rewrote the spread sweep to raw channel floats.

SURVIVOR 2 -- point-light-cap-and-cull. NOT filed separately: it is a duplicate of open row D29, and B08/B24/B25 are ONE change enumerated three times. Folded into D29 for the Director's decision. Two flaws recorded so we do not imitate them if D29 is ever built: their cull tests the RAW world position with no geometry wrap (the same failure class #217 fixed in our border estimator), and their cap ranks by raw brightness so a bright off-screen light outranks a dim one at the player's feet, while the partition reorders the light list and changes the lightmap bitwise even below the cap. Our exposure is per-light draw calls, not wrong pixels -- both executors already skip off-grid lights.

THE SIX REFUTATIONS ARE THE VALUE HERE. Each was flagged as a live defect and killed by reading our own source:
- rm-aos-cell-storage: filed as a removal but concedes it is a layout rewrite; our AoS Cell store is the live backing store of the whole lighting system. Its cost model also counts snapshotSpreadInput as a production reader -- it is TEST-ONLY (four callers, all in source/test), overstating the cost 2x. One production reader exists: exportSpreadInputs, under `if (lightingGpu)`.
- rm-lightingtilegather-noarg: lightingTileGather() is not dead, it is the live OFF branch of the lightingGatherCache kill-switch, reachable via `/lighting gathercache off` and deliberately retained as the A/B baseline. Deleting it removes the lever that de-risks A1/A2. The duplication is real and is #214's job.
- dead-dependency-cleanup: covered by D59/D60/D61/D64; and mimalloc is NOT dead here -- deleting it reproduces the configure break their own PR scored against itself.
- rm-offscreen-light-sources: their 49-tile cull radius exceeds the 32-tile windowMonitoringBorder band bounding our client entity map, so it would drop nothing; the far-off-screen light scenario is unreachable because the server destroys client slaves outside window.padded(32).
- ci-vcpkg-binary-cache: covered by D65, already closed different-tradeoff.
- rm-commented-vcpkg-cache-ci: their patch is a REPLACEMENT not a removal, and the same hunk as B11; our commented lines are a dated decision record, not dead code.

ENUMERATION QUALITY, worth carrying forward: their 47-claim list contains at least four duplicate pairs counted twice (B11/B27 one hunk; B08/B24/B25 one change), and a large share of the "fix" claims repair defects introduced earlier in the same PR. Treat their headline claim counts as inflated.

ALL 35 decisions recorded in docs/superpowers/drafts/osb-pr570-decisions.json with per-row reasons.
```

<a id="c29c1332-220"></a>

#### #220 — EXTERN-1 CLOSED: all 5 ledger rows done -- deletions 4e4ced84, E10 provenance 4960f7e5, E06 guard a69c8888

status: **completed**

- `8862dca7` gate: the extern provenance doc had no freshness arm, and its own doc denied it
- `6e0f6117` extern: delete the two dead xxHash dispatch files the provenance table found [#220]
- `a69c8888` E06: guard the multisample-to-sampler bind on the C++ path, where the exposure actually is [#220]
- `4960f7e5` E10: derived, cross-checked provenance for source/extern, and it found two dead files [#220]
- `4e4ced84` ledger: decide the last 15 rows; delete two dead vendored artefacts
- cited in `docs/board-audit-2026-08-08.md`

```
Five PR-570 ledger rows (D34, D59, D60, E10, E06), all discharged.

PART 1 -- DELETIONS (D34/D59/D60), done at 4e4ced84 and VERIFIED at close rather than taken on trust:
tinyformat.h and lib/linux/libcrypto.a are absent, zero tfm:: uses remain, and no dangling reference
survives in source/extern/CMakeLists.txt or doc/OPENSOURCE.md. Both companion edits were mandatory and
present -- a header named in star_extern_HEADERS but absent from disk is a CONFIGURE HARD ERROR, which
their own PR proved (patch 0001 breaks configure, 0002 repairs it). The lib/ SEARCH PATH is deliberately
untouched: B16 proposed removing it and that is NOT_REAL, it is load-bearing for FindSteamApi/FindDiscordApi.

PART 2 -- E10 PROVENANCE, done at 4960f7e5. A declared register (docs/architecture/extern-provenance.json)
plus scripts/extern-provenance.py with --check / --inject / --selftest, registered as extern_provenance and
extern_provenance_fires. Generated rather than written because PR 570's hand-written extern/README.md
contradicted their own vcpkg.json on the day it landed (B12): upstream/licence/why are declared, everything
derivable is derived AND cross-checked, so drift goes red. --selftest drives nine arms both directions.

  THE REACH TAXONOMY IS THE REAL OUTPUT, learned by nearly getting it wrong three times:
    malloc.c            in no CMake list, #included by rpmalloc.c        -> indirect
    xxh3.h              in no CMake list, #included by StarXXHash.hpp    -> include_path, LIVE
    xxh_x86dispatch.*   in no list, included by nothing repo-wide        -> unreachable, DEAD
  "not in CMakeLists" is not "not compiled", and "not compiled" is not "dead". My first pass read all three
  as dead on a grep scoped to source/extern; the repo-wide check is what saved xxh3.h from deletion.

  TWO FINDINGS RECORDED, NOT ACTED ON -- both need a Director decision:
    (a) xxh_x86dispatch.c/.h, 35.8 KB, genuinely dead, same class as tinyformat.h. In NO CMake list, so
        deletion needs no companion edit. The register says "unreachable" and the gate keeps saying so.
    (b) SIX of eight vendored artefacts have NO attribution in doc/OPENSOURCE.md -- fmt, fast_float,
        curve25519, rpmalloc, imgui_lua_bindings, xxhash-x86dispatch. imgui_lua_bindings carries no licence
        header at all in the vendored copy. A licensing gap, not a documentation one. The generated table
        prints **none** in the `attributed` column wherever it is missing.

PART 3 -- E06 GUARD, done at a69c8888. The recommendation as written (warn at loadConfig when a
"multisampled" framebuffer is named by an effect's frameBufferTextures) would have caught NOTHING -- that
set is empty here. Guarded the C++ path instead: setEffectTextureFromTarget, which composite() delegates to,
so one guard covers both call shapes. VERIFIED that delegation rather than assuming it.
Reports rather than refuses, deliberately -- refusing would leave the sampler on whatever it held and trade
a loud failure for a quiet wrong image. render-gate.sh reads it.
PROVEN TO FIRE: forcing every target to 4x multisample produced 11494 detections and a red gate, naming
setEffectTextureFromTarget('inputTexture', 'envCache'). Reverted: 0 detections, green.

Certified across the three commits: render gate PASS, core_tests 289/289, run-gates 27/27 (25 before, plus
extern_provenance and extern_provenance_fires).
```

<a id="c29c1332-221"></a>

#### #221 — GATHER-2 CLOSED: all four done as hardening -- E02 fc81ae17, E01 13cd3784, E03 c572e875, E04 a9ca6e26

status: **completed**

- `a9ca6e26` E04: the gather oracle -- rebuild the stable grid and check the cache against it, on the real world [#221]
- `c572e875` E03: clear only the vacated L, and record why the other half of the clear must stay [#221]
- `13cd3784` E01: extract the A2 scroll geometry and give it the test it never had [#221]
- `fc81ae17` E02: meter which path the gather cache takes, and find it loses 76-86% of its hits to the tile epoch [#221]

```
Reframed on Director instruction as HARDENING, not performance, reordered E02 -> E01 -> E03 -> E04, and completed 2026-08-04. No performance improvement was made or claimed. Every item's verification was proven able to fail.

E02 -- OBSERVABILITY (fc81ae17). Six counters partitioning the cached path by construction, plus
lighting.gather.margin_cells. Partition cross-checked against lighting.temporal.recomputed (1968 and 2231,
exact) on two live profiles. IT FALSIFIED THE PREMISE A1/A2 WERE BUILT ON: scrolling does not dominate --
5.5% of recomputes while walking, 0% standing still -- and the cache loses 76-86% of its hits to tile-epoch
churn. That became #225.

E01 -- TESTABILITY (13cd3784). The A2 scroll geometry extracted to source/base/StarGridScroll.hpp as a pure
function; production calls it. In base, not game, because core_tests links star_base and not star_game --
in game it would have joined the tests that run on zero platforms (#212). Six tests. Three injections:
mirrored overlap bounds -> 4 of 5 red; marginX one column short -> rebuild + coverage red only; margin
extending into the overlap -> MarginsNeverTouchTheOverlap red ALONE, which is why that assertion is not
redundant (the rebuild test is blind to it because both sources agree in the test but need not in
production). MatchesTheInlineArithmeticItReplaced exists solely because the gate cannot certify a
scroll-path change; its comment says to retire it once the extraction is no longer the change under review.

E03 -- DEDUPE (c572e875). Clear only the vacated L. The overlap clear was dead work -- 163 margin cells of
a 35,840-cell grid, so assign() cleared ~200x what needed it. THE MARGIN CLEAR IS NOT DEAD and checking
that was the point: lightingStableGather's own comment says tileEvalColumnsParallel CLAMPS AWAY cells in
unloaded sectors, so a margin cell whose sector is absent is never written and must already read as zero.
The tempting simplification -- the margins get gathered anyway, drop their clear too -- would have shipped
stale lighting at sector boundaries. assign -> resize is safe only because of E01's covering property.

E04 -- CORRECTNESS (a9ca6e26). An in-engine oracle: rebuild the grid from scratch every recompute and
compare, tagged with the branch that produced it. Observe-only, so arming it changes cost and nothing else.
Verified on the path the gate cannot reach -- a walking profile gave 125 scroll comparisons, 0 diffs -- and
the injection (skip the marginY gather) reported diff=145 maxAbs=0.450980 first=(2048,116) path=scroll,
plus a propagated DIFF on path=hit.

KNOWN LIMIT CARRIED FORWARD: under the render gate the oracle only reaches the HIT path, because a frozen
camera never scrolls. Scroll coverage needs a deliberate walking profile with lightingGatherOracle armed.
If the scroll path regresses, nothing catches it until someone runs that walk. The oracle's arming lives in
harness/storage/starbound.config, which is gitignored -- same as the other three oracles.

THE PERF CASE, MEASURED AND SET ASIDE, unchanged by any of this: with GPU lighting on, lighting CPU is
2.3-8.2% of active frame CPU and the whole gather 0.6-3.2%, in a frame that idles 60-77%. A walking
profile with the oracle ARMED (so with gather work roughly doubled) still reported "CPU busy: 6006us/frame
of a 16213us pace (37% utilised), VERDICT: HEADROOM (63% idle), 62 fps mean".

RESIDUAL NOT CLOSED HERE: p99 20398us against a 16213us pace in that armed run -- about 1% of frames
overrun the pacer. Not compared against an unarmed run, so whether it is the oracle's cost or a
pre-existing tail is UNKNOWN. Worth a look if frame consistency is ever the question.

Next in this area: #225 (epoch churn, now confirmed by two independent instruments) and #224 (spread
iteration cap).
```

<a id="c29c1332-222"></a>

#### #222 — BORDER-2 CLOSED: the 13.2% was the HARNESS, not the border. True cost = 0.168% of pixels at one fp16 LSB

status: **completed**

- `c9f7f524` arch-graph: regenerate system-boundaries blocks after the harness change [#222]
- `6e66f36e` harness: the A/B had no null control, and in daylight it measured its own sun rays [#222]

```
FILED 2026-08-04 claiming the adaptive border cost 13.2% of pixels via a spread-boundary artefact. THAT CLAIM IS REFUTED. The instrument was measuring itself.

THE DEFECT. renderTestCapture froze the SIM and left the RENDER clock running. WorldPainter::update kept advancing EnvironmentPainter's ray timer, which sets per-ray alpha, so a "frozen" scene repainted its sun rays every frame. The A/B renders its legs seconds apart, so the rays had moved between them. Diagnosed by dumping the diff as a PNG: the differing pixels were radial streaks converging on the sun, and nothing else -- the sky around them was bit-identical, which no lighting-region artefact can produce.

THE NULL CONTROL, which this instrument had never had. Same scene, lightingAdaptiveBorder held at true in BOTH legs:
    A/B DIFF: 260606 px (7.4252%) maxAbs=0.055176
The floor was larger than two of the three readings taken as the lever's cost (13.2%, 1.72%, 3.63% across three runs of the same scene -- that 8x spread was itself the tell, and I filed the first sample as a property before taking the second).

AFTER THE FIX (6e66f36e), same scene, same lever:
    A/B NULL OK: leg A reproduced byte-identically
    A/B DIFF: 5894 px (0.1679%) maxAbs=0.000488
0.000488 = 1/2048 = one fp16 lightmap LSB = an eighth of a single 8-bit display level. The differing pixels sit in the lit underground region (grid rows 4-8, left of centre), which is where a lighting-border effect belongs -- not in the sky.

WHAT IS NOW TRUE:
  - the adaptive border is NOT byte-identical at a scene that exercises it, so #170's pixel-identity claim does not hold universally
  - the difference is sub-LSB and cannot be displayed, let alone seen
  - the point-light half is confirmed correct: identical point counts in both legs, and #217's channel-max fix stands
  - there is NO evidence the spread floor of 32 is wrong. The entire case for re-deriving it was the 13.2%

C (measure the real spread requirement, derive the floor) IS NO LONGER WARRANTED as a correctness fix, and should not be run on the strength of this ticket. If it is ever wanted it is now a byte-identity question at the fp16 quantisation floor, not a visual-cost question.

SUPERSEDES the spread-boundary hypothesis in this ticket's original body in full. Method lesson recorded: a differential instrument that has never been run against itself is not known to measure anything.
```

<a id="c29c1332-223"></a>

#### #223 — GATE-TOLERANCE-1 DONE: paralloracle was never failing -- the gate read a BOUNDED-diff oracle as a zero-diff one

status: **completed**

- `f048adc4` gate: register render_gate_verdicts -- the render gate's own tolerance arms are now in the standing set [#223]
- `0c6184bf` gate: paralloracle is a BOUNDED-diff oracle and the gate read it as a zero-diff one [#223]

```
FILED as "the parallel lighting oracle diverges". BOTH HALVES OF THAT WERE WRONG, and the Director's remark that Lava Refinery is deep underground is what forced the check.

IT IS NOT A LIGHTING ORACLE. paralloracle is the PARALLAX cache oracle (source/rendering/StarBackdropPass.cpp): it renders the parallax to a reference target and pixel-compares it against the composited main. Nothing to do with parallel-vs-serial lighting. I inferred "parallel" from the name and never read the call site.

IT WAS NOT FAILING. The call site declares a bounded tolerance in as many words:

  // Bounded-diff gate (NOT a 0-diff gate): the premultiplied cache double-rounds partial-alpha texels, so a
  // small count on semi-transparent fringes with maxAbs ~<=1 LSB is EXPECTED + sub-perceptual. A large maxAbs
  // would flag a real blend/compose bug rather than the rounding.

and emits "(<=~1 LSB expected: premult double-rounding)" on every diff line. Observed maxAbs was 0.00061-0.00073 in every run -- under a FIFTH of the 8-bit LSB (1/255 = 0.00392). The gate scored any `diff=` line as red:

    bad=$(grep -cE "\[$o\] (DIFF|diff=)" "$LOG")
    if [ "$bad" -ne 0 ] || [ "$ok" -eq 0 ]; then FAIL

so the oracle's declared contract existed only as prose and the gate asserted something stricter than the code it watches. Fifth instance in that file of the gate and the thing it reads disagreeing about semantics.

WHY IT HID: the harness runs at one default location and was almost always run there. Warping to the Director's real bases is what exposed it.

WHAT ACTUALLY VARIES BY SCENE is the FREQUENCY of a diff, not its size:
    default location   26/26 EXACT,  25/25 EXACT
    01-Lava Refinery   31/34, 21/24 EXACT
    04-Ocean Factory    0/25,  0/29,  2/32,  2/20 EXACT
When a frame DOES diff, the count is ~735,000-752,000 px (about 21% of the frame) at BOTH bases, and maxAbs is 0.00061-0.00073 at both. So breadth and magnitude are scene-independent; only how often a refresh lands on a differing frame moves. An earlier version of this note said the gradient "tracks parallax density" -- that was inference past the data and is retracted; only the frequency ordering is measured.

FIXED. render-gate.sh now judges each oracle against a per-oracle tolerance (paralloracle 1/255, the other two zero), reports the worst maxAbs seen, and fails on: never ran, only SKIPPED, any diff on a zero-tolerance oracle, a diff with no maxAbs to judge it by, or maxAbs over tolerance. `scripts/render-gate.sh --selftest` drives all eight arms with synthetic logs, both directions -- 3 must-not-fire, 5 must-fire -- so the new tolerance is watched to fire rather than trusted to. Both bases now certify green end to end.

NOT CLOSED BY THIS, and worth someone's attention later: the call-site comment anticipates "a small count on semi-transparent fringes", and the measured count is 21% of the frame. maxAbs is decisively inside the rounding band and maxAbs is the discriminator the code itself nominates, so this is not evidence of a blend bug -- but the comment's model of the effect does not match its size, and one of the two is out of date.
```

<a id="c29c1332-224"></a>

#### #224 — SPREAD-CAP-1 CLOSED: cap 48 covers 100% of measured content (10 locations); margin is 1.7% but a breach is now loud, not silent

status: **completed**

- `f95abac2` lighting: detect a spread-cap breach instead of assuming one cannot happen
- `9f2aba93` fix(lighting): raise the GPU spread cap to 48 -- the pass was denied a third of the reach it asked for
- `8b05254b` arch-graph: regenerate after the spread telemetry change [#224]
- `bf6fa6bf` telemetry: the spread gauges must be RUNNING MAXIMA, not last-value
- `5f77683b` telemetry: record the spread iteration REQUEST, not only the granted count

```
CLOSED. Shipped, verified on hardware, and the residual is bounded and instrumented. Director's call was to stop here if a cap is simple and correct for now; the sweep supports that.

COMMITS: 5f77683b gauges | bf6fa6bf running maxima | 9f2aba93 cap 32->48 + declarations reconciled | f95abac2 breach detector

THE EMISSION DISTRIBUTION, ten locations, unattended live harness (running maxima):
    0.806  ->26  Desert Town
    0.850  ->28  precursor-surface, 00-Ocean-Lab, Peacekeeper Station
    0.894  ->29  03-Surface Outpost
    0.918  ->30  Miniknog, Outpost - Main Teleporter
    1.475  ->48  01-Lava Refinery, 04-Ocean Factory, crucible
BIMODAL AND QUANTISED. A cluster at 0.81-0.92 and exactly 1.475 at three unrelated interiors, with
nothing between and nothing above. Exact values repeating across unrelated locations shows these come
from a small set of shared sources rather than scene-dependent accumulation -- which is why the
original "identify the 1.475 emitter" framing had no answer: no single emitter can produce it.
MaterialDatabase::radiantLight SUMS material + mod, and the gather adds liquid + background +
environment on top; the brightest single radiantLight channel in vanilla AND all 43 installed mods is
exactly 255 (= 1.0), so 1.475 is necessarily a sum.

THE MARGIN, stated precisely because it is thin: ceil(E * 32) <= 48 holds for emission up to exactly
1.500. Observed maximum is 1.475. That is 1.7% of headroom, and three locations sit on the boundary.
48 is not comfortably above the ceiling -- it lands almost exactly on it.

WHY THAT IS ACCEPTABLE ANYWAY: the failure mode is no longer silent. f95abac2 logs the emission, the
iterations needed, the shortfall in steps, and the value that would solve it -- proven to fire (cap
forced to 32 at a scene requesting 48) and proven silent at the shipped cap. A thin margin you can
see is a different thing from a thin margin you cannot.

WHY NOT THE DERIVED CAP: it would bound what is COMBINABLE rather than what is BUILT. Worst-case
stacking in installed content is ~4-5 emission -> ~160 iterations -> roughly 3x today's spread cost,
permanently, to cover a tile nobody has placed. Poor trade against content that measurably tops out
at 1.475. The derived cap remains the correct end-state ONLY if emission ever gains a real declared
bound -- and note brightnessLimit (1.4) cannot serve as one, since 1.475 already exceeds it.

IF THIS IS EVER REOPENED, the three paths and what each needs are written up in the session record:
  A  clamp emission at gather -> E_max by fiat, cap = ceil(E_max * spreadMaxAir), can never bind.
     Needs a chosen clamp value, evidence it is invisible (the gauge now provides it), and an A/B --
     it IS an output change.
  B  derive from the databases at load. Needs enumeration + re-derivation on asset reload, and yields
     the expensive ~160.
  C  delete the cap, bound cost instead. Needs the FREQUENCY distribution, not just the peak.
The measurement that discriminates all three is the emission distribution, which now exists.

NOT MEASURED, and the honest gap: the environmentLight term's maximum across biomes and times of day.
The surface samples (0.806-0.918) are one time of day on the worlds sampled. A brighter biome could
push a sky-exposed cell higher -- the detector would catch it.
```

<a id="c29c1332-225"></a>

#### #225 — EPOCH-1 FIXED (8f322517): hit rate 17.9% -&gt; 80.7%. My "not viable" close was WRONG -- see the correction

status: **completed**

- `8f322517` EPOCH: bump on sector load and world parameters, and stop bumping on liquid that cannot light [#226][#225]
- `5af987a2` #225 step 2: classify epoch bumps by location, and close the question -- no invalidation policy fixes this [#225]
- `e572e1ab` #225 step 1: split the epoch bumps, and find that value-gating cannot fix the churn [#225]

```
FIXED 2026-08-04 at 8f322517. This ticket was closed earlier the same day as "measured, not viable". THAT CLOSE WAS WRONG and the correction is the most useful thing in this record.

THE FIX: the liquid epoch bump is now conditional on liquidsDatabase->radiantLight() actually changing.

    MEASURED, 60s walking at 01-Lava Refinery
                            before     after
        gather.hit             402      1463
        gather.scroll          119       332
        gather.full.epoch     1727        17
    Hit rate 17.9% -> 80.7%. Epoch-driven misses fell 99%.

WHY I GOT IT WRONG, because the error is reusable. I reasoned about the TOTAL bump count: 8558 of 73070 is
11.7%, leaving ~21 bumps per recompute, therefore the cache still misses. That assumes bumps arrive EVENLY.
They do not. The liquid stream is ~143/s -- one every 7ms -- so it lands in essentially every recompute
interval. The 67584 netTile bumps arrive in tileArrayUpdate BATCHES, clustering into few intervals and
leaving the rest clean. What governs hit rate is THE FRACTION OF INTERVALS CONTAINING ZERO BUMPS, not the
total count, so removing an evenly-spread stream is worth far more than its share of the total.

I wrote exactly that caveat into commit 5af987a2 -- "arrivals are bursty... NOT MEASURED... could convert
to anywhere from zero upward" -- and then let the headline conclusion stand anyway. The caveat was the
answer. The lesson is not "measure more", it is: when your own stated caveat would overturn your
conclusion, it is not a caveat, it is the open question.

The Director's challenge ("so did you implement them?") is what forced the check.

ORDERING WAS LOAD-BEARING. The liquid gate is safe only because #226 H2 (loadDefaultSector bumps on its
own) landed in the same change. Those suppressed bumps were incidentally covering that gap; removing
accidental cover before closing the gap is how a value gate ships stale lighting.

NO STALENESS, PROVEN: the gather oracle (#221 E04) rebuilt the stable grid from scratch every recompute
across the verification run -- 2904 comparisons, all EXACT. Plus render gate PASS (gatheroracle 215/215)
and run-gates 25/25. The hardening cluster taken on non-performance grounds is what made this both
possible and verifiable.

WHAT REMAINS UNFIXED AND IS FINE: the netTile site still bumps unconditionally, 67584 times a minute, with
a measured ZERO no-op rate -- every one genuinely changes a light-relevant field, so there is nothing to
gate. 26.5% of them are for tiles outside the calculation region, which a regional epoch could drop, but
with full.epoch already at 17 there is nothing left to win. NOT WORTH DOING.

STILL NOT MEASURED: whether the hit-rate gain converts to frame time. The gather was 3.20% of active frame
CPU at this base and the client idles 60-77%, so expect little to nothing observable. The value here is a
cache that now does what it was built to do.
```

<a id="c29c1332-226"></a>

#### #226 — EPOCH-2: H1 undergroundLevel + H2 loadDefaultSector FIXED (8f322517); H3 asset-reload flags left open, severity unverified

status: **completed**

- `8f322517` EPOCH: bump on sector load and world parameters, and stop bumping on liquid that cannot light [#226][#225]
- cited in `docs/superpowers/drafts/matrix-prereq-ledger.md`

```
Found by the read-only trace run for #225, which was asked the adversarial question: is any input the lighting gather reads written by a path that does not bump m_lightingTileEpoch at all? Three answers. Two are now fixed.

--- H1 FIXED (8f322517). undergroundLevel.
gatherColumns reads m_worldTemplate->undergroundLevel() every gather and it is the SOLE non-tile term of
skyExposed: `if (tile.backgroundLightTransparent && pos[1] + y > undergroundLevel) skyExposed = true;`.
WorldParametersUpdatePacket called setWorldParameters with no bump and no m_gatherValid reset, so a
world-parameter change could stale the skyExposed bit of the ENTIRE cached grid, and no per-tile
invalidation could ever catch it because the input is not a tile field. Now bumps the epoch -- chosen over
resetting m_gatherValid because the packet path is the client thread while the cache state belongs to the
lighting thread, and the epoch is the atomic already built to cross that boundary.

--- H2 FIXED (8f322517). loadDefaultSector.
It bumped nothing, and tileEvalColumnsParallel passes evalEmpty=false, so absent sectors are never visited
and their grid cells keep whatever they already held. A newly loaded sector therefore moves cells from
"skipped, stale value retained" to "gathered" -- a real change even when every tile in it matches. The
readNetTile loop that follows happened to bump per tile and cover this, but that was an accident of never
gating. Now bumps once per batch, unconditionally.
THIS WAS THE BLOCKER for #225's liquid gate, and landing it in the same change is what made that gate safe.

--- H3 STILL OPEN, severity unverified. The cached transparency flags.
backgroundLightTransparent and foregroundLightTransparent are derived state recomputed ONLY in readNetTile.
If the material database's render profiles change under a running client (asset reload), both the flags and
the radiantLight results go stale with no tile write anywhere -- even a full re-gather reads the stale
cached flags. A reload tracker exists near the lighting-parameter cache but NEITHER the agent NOR I verified
whether it invalidates m_gatherEpoch / m_gatherValid.
NOT PURSUED because mid-session asset reload may not be a real scenario for the Director. If it is, the
check is small: does m_lightingParamsReloadTracker's pullTriggered path also drop the gather cache?

ALSO RECORDED: unloadSector bumps no epoch either. Pre-existing, documented in the code, tracked as the E04
residual. #225's liquid gate removes churn that was incidentally papering over it, so it is now marginally
more exposed -- but the gather oracle (#221 E04) is the instrument that would catch it, and 2904 comparisons
across a walking run came back EXACT.

SAFETY MULTIPLIER, still true and worth carrying: m_lightingTileEpoch is ALSO the change detector for the
temporal lighting gate (StarTemporalLightingGate compares tileEpoch != prev.tileEpoch -- equality, not
monotonic motion, so a conditional bump is compatible). But any future narrowing of a bump condition
suppresses temporal recomputes as well as gather rebuilds.
```

<a id="c29c1332-227"></a>

#### #227 — ATTRIB-1 CLOSED (122ca40a): all 7 artefacts attributed; imgui grant found upstream; the field is now gated on correspondence

status: **completed**

- `aa484259` evidence: hash the one matrix run that can still be re-windowed
- `122ca40a` attrib: attribute all seven vendored artefacts, and gate the field that was never checked

```
Both halves closed.

CONTENT: five entries added to doc/OPENSOURCE.md (curve25519, fast_float, fmt, rpmalloc, imgui_lua_bindings); register `attributed` now set for all seven. Licence text taken from the VENDORED copies -- what we actually redistribute -- not from upstream repos.

imgui_lua_bindings was the filed worst case and checking upstream CHANGED the answer rather than confirming it. There is no LICENCE file and no header, but the upstream README carries an express permission grant in informal terms, explicitly covering inclusion in an open source project. Quoted verbatim in the entry, because a paraphrase of a grant is not the grant. We are squarely inside it: open-source fork, not sold, authorship not claimed. Register licence corrected from "UNSTATED-IN-VENDORED-COPY" -- true of the file, false of the project. Not a removal decision after all.

Its terms impose exactly one obligation that silence would have breached: "don't ... claim that the source code was made by you". A vendored copy with no header and no attribution entry is the closest thing to an implicit authorship claim, so the entry is the fix. The README also says "(let me know please!)" -- a request, not a condition; contacting the author is outward-facing and is the Director's call, NOT actioned.

fast_float is tri-licensed (Apache-2.0 OR MIT OR BSL-1.0); we take MIT and the entry says which, rather than leaving a reader to guess.

MECHANISM: `attributed` was rendered in the generated table and required by nothing -- that is how five went missing behind a green gate. It now checks CORRESPONDENCE rather than declaration: the named file must exist AND actually mention the artefact's upstream URL. Matched on upstream, not our register name, because the name is ours and can drift while the URL is what a reader follows. Four selftest arms including the load-bearing one -- a register naming an attribution file that never mentions the artefact.

A ratchet turned out to be the wrong instrument: with all seven attributed we are at the floor, so a hard requirement is satisfiable today and strictly stronger.

extern-provenance selftest 18/18; run-gates 27/27.
```

<a id="c29c1332-228"></a>

#### #228 — BEAM-EPS-1 CLOSED (0fed2045): unified on 0; bit-identical for all real content, colored path provably untouched

status: **completed**

- `0fed2045` fix(lighting): unify the beam guard on 0 -- monochrome and colored disagreed

```
Director approved unify CONDITIONAL on the perceptible change being negligible. Verified before shipping, and the answer came out stronger than the condition: for all content that exists, it is bit-identical, not merely imperceptible.

WHY THE AFFECTED WINDOW (0, 1e-4] IS EMPTY -- measured, not reasoned:
  * pointBeam is authored per object, default exactly 0.0f, so a light is either 0 (both old guards
    skip) or a configured beam (both apply). Only a value strictly inside the window differs.
  * vanilla packed.pak: 48 pointBeam values, MINIMUM 0.1
  * 43 installed Workshop mods: 91 pointBeam values, MINIMUM 0.1
    -- three orders of magnitude above the old 1e-4 epsilon.
  * Not interpolated: NetworkedAnimator reads it once via getFloat and passes it through, so nothing
    transits the window transiently during a fade either. This was the one mechanism that could have
    populated an empty window, and it does not exist.
  * Worst case even for a hypothetical asset inside the window: attenuation shifts by <= 2e-4, and
    ScalarLightTraits::subtract is linear (max(c - drop, 0)), so ~1/20th of an 8-bit level.

HONEST LIMIT: the Workshop mod index holds metadata only (title/description/tags), not file contents,
so "all mods ever published" is NOT covered -- only vanilla plus the 43 mods installed here. The
bound above is what covers the rest.

INSTRUCTION-LEVEL PROOF, and it DISCRIMINATES rather than merely agreeing:
  ColoredLightTraits 450 -> 450, UNCHANGED. The default path is bit-identical.
  ScalarLightTraits  370 -> 369, one instruction FEWER -- the 1e-4 literal no longer needs loading.
Any change in the colored stream would have meant the unification was wrong; there is none.

The traits constant that 7879e0a2 introduced to carry the divergence is deleted with it: a constant
identical for both instantiations is indirection with nothing behind it.

core_tests 289/289, game_tests 72/72, run-gates 27/27.

NOT DONE, deliberately: no in-game visual confirmation. The change cannot alter any pixel for content
on this machine, so there is nothing for a play session to observe -- asking for one would imply a
check that could not fail.
```

<a id="c29c1332-229"></a>

#### #229 — GPUTEL-B1: explore context for the sovereign GPU telemetry design

status: **completed**

- `8a081d61` GM-2a (T2): the whole a part closes against is keyed by (owner, DOMAIN)
- `dc722dee` GATE-SKIP-1: a gate that did not run may no longer read as green
- `934afba9` GM-1 Task 6c: CORRECTION -- EngineBusyReader's "PROVEN" claim was measured badly
- `c3a92060` GM-1 Tasks 8+9: mutual validation, plus the docs and the gate
- `8d5734f6` GM-1 Task 7: the metrics CLI -- measures a process from outside it
- `fecf37f6` GM-1 Task 6b: refuse a PMU event whose unit is not nanoseconds
- `320b67bc` GM-1 Task 6: EngineBusyReader -- the second, independent path to one quantity
- `c3181e45` GM-1 Tasks 4+5: unavailable is never zero; a reset counter is discarded
- `e6bde922` GM-1 Task 3: ClientBusyReader, with the 4x dedup bug pinned as a test
- `8ed374c4` GM-1 Task 2: MetricSample -- a reading carries its meaning or does not exist
- `fb9c13bc` GM-1 Task 1: register `metrics` as a component -- duty, grant, derivation
- `db4e35d2` GM-1: implementation plan -- 9 tasks, TDD, from the approved design
- `60997cb8` GM-0: ship row finalised at N=3 -- 14.83% busy, 2403us/frame, +/-0.7%
- `11ebce0c` GM-0: sovereign metrics design -- GM-1 specified, GM-2..GM-5 scoped
- `8261ce4e` GPUTIMER-3: CORRECTION -- the 96.8% GPU-busy claim in 9b3c9428 is WRONG
- `dcb7daf6` GPUTIMER-2: gate the straddled bracket, at authoring time
- `9b3c9428` GPUTIMER-1: the per-pass GPU timers under-reported the frame by 3.4x

```
DONE. Established this session: (a) per-pass GL_TIME_ELAPSED measures elapsed GPU-timeline span INCLUDING idle -- parts sum to ~100% of the 16.2ms frame period while true GPU busy is 24.1%; (b) the GL timers cost +11.4pp busy = +47% relative, so timers-on is not the shipped config; (c) TickRateApproacher(60.0f) is hardcoded at StarMainApplication_sdl.cpp:1386 -- uncapping would speed the SIM, not just the pacing; (d) fdinfo drm-engine-render deduped by drm-client-id is exact and free but whole-process only; (e) hardware exposes i915 PMU (rcs0-busy, actual-frequency-gt0/gt1, rc6-residency, per-engine sema/wait), GL_INTEL_performance_query (real per-region HW counters), and GL_EXT_disjoint_timer_query (GL_GPU_DISJOINT_EXT -- NEVER CHECKED by our code, and the GPU clocks 933-2350MHz).
```

<a id="c29c1332-230"></a>

#### #230 — GPUTEL-B2: clarifying questions, one at a time

status: **completed**

```
Understand purpose/constraints/success criteria for the sovereign GPU telemetry system. Load-bearing unknowns: (1) what questions must it answer -- absolute per-pass attribution, lever A/B deltas, regression gating, or shipped-config health; (2) whether an invasive high-fidelity MEASUREMENT MODE is acceptable (instrument may cost, runs only in the harness) versus an always-on near-zero-cost path -- the current system attempts both and fails at both; (3) whether changing the frame pacing to saturate the GPU is on the table, given it changes the sim workload.
```

<a id="c29c1332-231"></a>

#### #231 — GPUTEL-B3: propose 2-3 approaches with trade-offs

status: **completed**

```
Candidate axes established by B1: (A) kernel-side truth -- i915 PMU rcs0-busy + fdinfo per-client, exact and near-free but whole-process, no per-pass split; (B) GL_INTEL_performance_query hardware counters per region -- genuine per-pass work attribution independent of idle, but Mesa/Intel-specific and heavyweight; (C) saturation mode -- decouple render pacing from sim tick so elapsed==busy, making the existing timers valid, at the cost of a real change to the frame loop; (D) differential-only methodology -- rank levers purely by their delta on kernel-side busy, no budget attribution at all. Include the disjoint-query fix in whichever lands.
```

<a id="c29c1332-232"></a>

#### #232 — GPUTEL-B4: present design, get approval per section

status: **completed**

```
Cover architecture, components, data flow, error handling, testing. HARD GATE: no implementation until the Director approves. Must state, for every metric the system emits, WHAT PHYSICAL QUANTITY IT MEASURES and under what condition that is valid -- the failure being repaired is a metric whose name claimed one thing and whose value meant another. Must also carry a self-validation story: an instrument that cannot be checked against an independent one is how both the 4.04x step and the 96.8% busy figure survived.
```

<a id="c29c1332-233"></a>

#### #233 — GPUTEL-B5: write + self-review + Director-review the spec, then writing-plans

status: **completed**

```
Write to docs/superpowers/specs/2026-08-05-sovereign-gpu-telemetry-design.md and commit. Self-review for placeholders, internal contradictions, scope, ambiguity. Then Director review. Terminal state is invoking superpowers:writing-plans -- no other implementation skill. Note the Director does not read spec markdown (memory: surface-decisions-not-doc-review), so surface every director-critical decision INLINE in chat and follow Claude's lean elsewhere.
```

<a id="c29c1332-234"></a>

#### #234 — GM-1b: EngineBusyReader must poll, not bracket — the PMU publishes lazily

status: **completed**

- `9505dd92` GM-1b (#234): EngineBusyReader polls; a single read can no longer escape

```
FILED WITH EVIDENCE, not a suspicion. source/metrics/StarEngineBusyReader.{hpp,cpp} has two defects characterised during GM-1 Task 8 (see commit 934afba9 for the correction, c3a92060 for the measurement):

(1) CATCH-UP. open() returns its first sample immediately. The first read after perf_event_open carries unbounded historic busy time -- 2.74 SECONDS arrived in one 0.5s interval during characterisation.
(2) STALENESS. The i915 PMU publishes lazily. Under steady 35% load sampled at 0.5s, 4 of 40 intervals advanced by exactly ZERO ns and the next by double. Staleness scales with read interval (10% zero-advance at 50ms, 6% at 200ms, never >2 deep). A window ending inside a stale interval under-reports by whatever is unpublished. 2 of 20 runs disagreed grossly: one read 0.000000 against 29.6% fdinfo, another exactly half.

THE FIX IS AN API CHANGE, NOT A PATCH: drain a warm-up (~0.30s), then POLL through the window (~0.02s) and accumulate -- the shape scripts/pmu-render-busy.py uses, which has 40/40 clean runs behind it. open()/sample() becomes something like busyOver(seconds). Deliberately NOT half-fixed: draining the warm-up alone removes the catch-up, leaves the staleness, and LOOKS fixed, which is worse than a defect that is written down.

No consumer today, so nothing currently reports a wrong number. The bound to re-measure after the fix: the Python reader's spread was min 0.0020pp / median 0.0757pp / max 0.2863pp over forty concurrent 8s windows at 20.9-25.5% load.
```

<a id="c29c1332-235"></a>

#### #235 — BUSY-VS-WALL DONE: the series is retained, CPU busy is attributed per owner, the clock is declared and ratcheted, and both unfalsifiable wh…

_Stored subject exceeds the heading; reproduced verbatim:_

```
BUSY-VS-WALL DONE: the series is retained, CPU busy is attributed per owner, the clock is declared and ratcheted, and both unfalsifiable wholes are gone
```

status: **completed** · blocks: #252, #250

- `164163cc` [#271] the 152-159 us/frame lump gets seven parts, and one of them is the wait
- `7fa1d2ed` OBS-JOIN: the sovereign readers get a harness consumer, and it re-points itself at each leg's client [#253]
- `eec79ca1` C07 + CLOCK-RATCHET: the frame budget closed against a period that contains a sleep, so it could not fail [#235]
- `e368f046` GPU-CLOCK: a timeline span is not work, so it may not be a budget or a whole -- and now it cannot become one [#235]
- `0eef793b` THREAD-BUSY: CPU busy attributed to the declared owners, and the process keeps time no live thread can account for [#235]
- `11a28ba0` BUSY-FIELD: the shared reading's map was named for GPU engines, and the third reader does not key by engine [#235]
- `3a8c530e` SERIES: the snapshots were deleted, the intervals were counted and discarded, and the only timestamp was a filename [#235]
- `72ad0000` GM-2c: every lever in the matrix can now prove it engaged
- `50d57866` GM-2b: the lever matrix asserts the experiment happened; descriptor design revised

```
SCOPE WIDENED 2026-08-07 (Director). Was "every CPU phase timer measures WALL time, not CPU work" --
kept at #235 because StarMetricDesc.hpp cites this number by name in the MetricClock comment.

THE DEFECT, RESTATED. Every duration in this system is a WALL span between two points, on both sides:
TelemetryScope records a monotonic delta (busy + blocked + preempted, undifferentiated), and
GL_TIME_ELAPSED / GL_TIMESTAMP record elapsed GPU TIMELINE. No phase can answer "was this work, or
waiting?" -- and the two have OPPOSITE fixes.

PROVEN 2026-08-07, no longer argued: render.frame.gpu_span_us reports ~16,200us -- the frame period --
at 0.22%, 9.97%, 23.39% and 32.93% real engine busy alike. A metric identical across a 1.4x change in
the quantity it names (both ends correctly aligned) is measuring elapsed timeline, not work.

=== THE SUBSTRATE EXISTS TODAY, MEASURED NOT PROPOSED (2026-08-07) ===

Both BUSY quantities are available OUT OF PROCESS, per-owner, as cumulative counters on a wall clock.
Sampled live against a running matrix leg:

  /proc/<pid>/fdinfo/<drm fd>   drm-engine-{render,copy,compute,video,video-enhance}: ns, PER CLIENT
      measured 10.95% for the game's own pid while the system-wide i915 PMU read 14.06% --
      so ~3.1pp of what we have been quoting was NEVER THE GAME. This kills the wobbling floor
      (0.213% one hour, 5.1% the next, 2.34% a third time): the floor for a per-client counter is
      ZERO BY CONSTRUCTION. Also yields drm-total/drm-resident -- VRAM, free, same read.
  /proc/<pid>/task/<tid>/stat   utime+stime: CPU BUSY ns, PER THREAD
      measured 27.22% of one core across 36 threads.

AND THE THREAD NAMES ALREADY CARRY THE OWNER TAXONOMY. Measured, 3s window:
      starbound          24.0% of a core   -> MetricOwner::Frame
      WorldServerThre     6.0%             -> MetricOwner::Sim
      WorldClient::li     1.0%             -> MetricOwner::Lighting
      starboun:gdrv0      0.3%             -> MetricOwner::Gl (driver thread)
      SDLAudioP15, UniverseServer/Connect, SystemWorldServ -> Unknown/other
So CPU busy is attributable to the DECLARED owner vocabulary with a name->owner map and no engine
change at all. That is the missing half of the model, and it was never an engine problem.

=== WHAT THIS MAKES #235 ===

NOT "add busy-vs-wall to the telemetry model". MetricClock already has exactly the right enumerators
-- Wall, ThreadCpu, ProcessCpu, GpuEngine, GpuTimeline -- and ZERO production sites populate it. The
work is:
 1. Make `clock` mandatory and gated; refuse to compare or sum across clocks.
 2. Make clock=GpuTimeline with role=Budget|Total a CONFIGURE-TIME ERROR. render.frame.gpu_span_us
    carries exactly that combination today and it is wrong in kind: a timeline span cannot close a
    budget, ever.
 3. Add the two sovereign busy readers above beside the PMU one, joined by the window stamps #248
    built. The in-process telemetry keeps doing what ONLY it can do -- which pass, which entity,
    which branch -- and the sovereign observer supplies busy. Reconcile per owner.
 4. RETAIN THE TIME SERIES. telemetry-window differences files[lo] and files[hi] of ~15+ snapshots
    per leg; the snapshots are cumulative, so differencing CONSECUTIVE pairs already yields a
    per-interval series for EVERY declared metric, including the histogram buckets (p99 over time).
    The data is emitted and thrown away at the last step. That is the whole distance between what we
    have and a plottable stream.

DIRECTOR'S NORTH STAR, stated 2026-08-07: "both GPU and CPU metrics, correctly attributed, as a
stream of time series data that can be directly plotted and compared against every single lever."
Dimensions: wall time; subsystem (cpu / gpu engine class); owner; lever configuration; scene; and the
metric's own descriptor. All five sources above share one wall-clock axis and are all
cumulative-differenceable, so the join is the one #248 already ships.

RELATED: #248 built the alignment and the floor discipline. #249 is the re-measurement. The sham-leg
proposal (a leg configured identically to baseline, run under a lever's name) gives a universal noise
floor for EVERY stream at once and should land with this.
```

<a id="c29c1332-236"></a>

#### #236 — GPU-CLOSE-1 CLOSED by [#235] e368f046: the re-declaration that would revive the inert oracle is now refused mechanically

status: **completed**

- `e8814a61` PREREQ-4: a lifetime total wearing a window's label, and 42 unrecorded content inputs
- `49249551` PREREQ-3: the harness measured a window nobody chose, on a clock that was not the cadence's
- `420f1759` PREREQ-2: eight counters could not report zero -- including the one guarding every GPU number
- `040a5033` PREREQ-1: three matrix blockers closed -- a governor, a stale binary, and a scaled-up phantom
- `feb8886f` LEDGER-MATRIX: 49 matrix prerequisites, durable, and each row checked against the TREE
- `9db54200` GM-2d (#236): the GPU parts were never a partition -- root cause, not a tolerance
- `0290f6ef` GPU-CLOSE-1a (#236): the closure check gets a MEASURED bound, watched from both ends

```
RESHAPED AGAIN 2026-08-07. The earlier reshaping stands and is not retracted: the excess is
BIDIRECTIONAL across 9 legs (+0.61% to -3.29%), which refutes both original hypotheses by arithmetic,
and the real defect is a ZERO-TOLERANCE comparison between two independently-sampled quantities that
agree only to about +/-3%. That analysis is still correct.

WHAT CHANGED, AND IT IS NOT A FIX. The oracle CANNOT FIRE ANY MORE. D01 demoted the 13 GPU pass
timers from MetricRole::Budget to Detail, so a real leg now contains:

    gpu-domain role=detail   25
    gpu-domain role=total     1
    gpu-domain role=budget under owner gl: 0

telemetry-window's closure runs under `if parts:`; with zero budget parts it takes the RANK-ONLY
branch and computes no closure at all. Verified against a real leg of matrix-20260807-151445:
violations = []. So the check this task exists to repair has silently stopped existing.

THE HAZARD THAT KEEPS THIS OPEN. An inert check is not a safe check -- it is a landmine with the pin
back in. Re-declare ANY gpu-domain metric as Budget under owner gl, which the descriptor convergence
(#245) touches 139 sites and could easily do, and the zero-tolerance oracle springs back with the
same coin-flip failure that killed an entire matrix pass. Nothing announces that.

AND THE PREMISE IS NOW WORSE THAN "NOISY". Measured 2026-08-07: render.frame.gpu_span_us -- the
`whole` in that comparison -- reports ~16,200 us at 0.22%, 11.39%, 23.39% and 32.93% real engine
busy. It is the frame period. The parts are timeline spans too. So the closure was never comparing
work against work; it was comparing one span against a sum of spans, and the +/-3% agreement was
agreement about bracket durations. A tolerance would have made a meaningless check quiet rather than
correct.

WHAT IS ACTUALLY LEFT, and it is small:
 1. Make the inertness LOUD rather than silent: if any gpu-domain Budget part appears under an owner
    whose declared total has clock=GpuTimeline, that is a configure-time or gate-time error, not a
    runtime coin flip. This is the same assertion #235 needs for role=Budget|Total + GpuTimeline.
 2. If a gl/gpu closure is ever wanted again, build it on PMU busy or per-client drm-engine
    accounting -- quantities that ARE work -- never on *.gpu_us brackets.
 3. The tolerance lesson stands for whenever it returns, and the shape is already written down twice:
    #223 (a BOUNDED-diff oracle read as a zero-diff one) and #194 (both ends measured, injection
    proves it fires).

DEPENDENCY: item 1 belongs with #235 and should land there rather than here. This task stays open
only to own the hazard until it does. It is NOT in progress and nothing should be built against it.
```

<a id="c29c1332-237"></a>

#### #237 — MEASURE-CLIENT: a fifth entrypoint — real client, real GPU, no window, owns its fixture and its storage

status: **pending** · metadata: `{"rank": 5}`

- `20027817` RESIDENCY-RATCHET (#237): a parked decision with no instrument gets made by accretion

```
PROPOSED 2026-08-06 by the Director, from evidence produced closing #136/O13. NOT STARTED, not queued.

WHAT IT IS. A fifth ENTRYPOINT in the TSSA's existing composition scheme (participant + presentation
backend + driver, chosen independently). The spec already states the relation outright: "swap `rendering`
for `transcript` and the same chain ends in `recordTick`". So this is not a new KIND of thing.

    client_opengl     frameLoop     rendering  -> GPU     window, GUI, human
    client_headless   headlessLoop  transcript -> file    none
    PROPOSED          headlessLoop  rendering  -> GPU     none

It is a REAL CLIENT and behaves as a real player: real WorldClient, real player entity, logs in, warps,
walks, streams worlds. "Headless" here means no window, no GUI, no human input -- NOT no rendering.

IT IS NOT #199, and conflating them would be the mistake. #199 is presentation-NULL: "no GL context at
all", linking star_extern + star_core + star_base + star_game only, the way starbound_server does, for
throughput and agents. This one KEEPS the renderer, because golden frames and GL_TIME_ELAPSED are the
entire point -- a transcript backend is useless to a render-measurement instrument. The link delta is
therefore precise and checkable: drop star_windowing and star_frontend, KEEP star_rendering and
star_application. #199 stays deferred on its own terms; the boundary_ratchet keeps it warm.

WHY -- three defects, all one cause, all measured 2026-08-06. The harness is ~8% resident in stock
StarClientApplication (177 of 2233 lines in the .cpp, 36 of 296 in the .hpp) and therefore INHERITS state
it never asked for:
  * THE SCENE SELECTED ITSELF. No notion of a target scene; it inherits the player save's persisted
    position. Every early determinism run landed at 03-Surface Outpost, which fails both preconditions
    (never settles, dayLevel=0 so the terms under test draw nothing). Nobody chose it.
  * QUIESCENCE FIRED ON THE DEPARTURE WORLD. The settle criterion is written against the client's
    world-load lifecycle and knows nothing about a pending warp; the ship holds still, so the counter
    reached 90 mid-transition. Warp issued t+0.1s, load "ended" 2.8s later still aboard the ship.
  * IT REWRITES ITS OWN INVENTORY. Runs go through the real save path; the bookmark list went 27 -> 23
    across one evening, and an instance world's population went 47 -> 1.
Fixes for all three shipped (d8f36de7) as ASSERTIONS. That is the tell: I had to DETECT being in the
wrong world because nothing prevented it. A peer module makes all three impossible by construction.

WHAT IT OWNS THAT TODAY'S HARNESS CANNOT:
  1. ITS FIXTURE -- which world, which spawn, which motion, when to capture: declared, not inherited.
  2. ITS STORAGE -- read-only or restored per run, so a run cannot mutate the inventory it measures.
  3. ITS LOOP -- no title-screen state machine, no GUI, no input path to inherit ordering from.

SECOND COST, independent of correctness: StarClientApplication is STOCK upstream (origin/main has its own
commits on it, e.g. 4427a49b). Every upstream merge negotiates around our scaffolding, and the fork is
already ~83 commits behind.

INSTRUMENT ALREADY EXISTS. scripts/grant-sweep.py checks the TSSA grant table against the tree and reports
UNVERIFIABLE for target components with no files yet -- rather than passing over what it cannot see. A new
entrypoint starts as UNVERIFIABLE rows and earns each grant back, so the module boundary is enforced from
day one instead of asserted.

SEQUENCING. Behind the TSSA (#204/#207) -- that is where the grant table and the entrypoint set live, and
this adds a row to both. Cross-refs: #199 (the presentation-null sibling), #200/#203 (boundary work),
O13 in docs/superpowers/drafts/matrix-prereq-ledger.md (the three defects, with measurements).
```

<a id="c29c1332-238"></a>

#### #238 — LEDGER-SWEEP: clusters A+B+C — 13 open rows closed before the matrix run

status: **completed**

- `a8df237e` D05-PROBE: publishes DO outrun consumptions, and Recompute is refuted by measurement
- `b879c332` DESC-LINT: the paragraph asserting a check was in place was the only thing holding the rule up
- `92b8a3eb` CLUSTER C (D03-D09): a descriptor asserting one quantity while the code produces another
- `a66a7378` CLUSTER B (H06): the arrival gate was vacuous on every frame before the warp fired
- `246d75df` CLUSTER A (R07-R11): a counter registered inside the branch it measures cannot report zero

```
DONE 2026-08-06. All 13 rows closed across three commits, each with signatures null-controlled against HEAD.

246d75df CLUSTER A (R07-R11) -- registration hoisted out of the branch it measures.
a66a7378 CLUSTER B (H06) -- the arrival gate was vacuous before the warp fired; warp now re-enters the load phase; render-profile.sh reads the log ordering (new gate render_profile_verdicts).
92b8a3eb CLUSTER C (D03-D09) -- seven descriptor corrections, measured against a 60s live capture.

Ledger: 53 rows, 20 open (was 33), 0 BLOCK, 33 verified (was 20). Artifact refreshed at
https://claude.ai/code/artifact/a26d0a94-2aba-4769-9779-eb3251b2ccc6

FOUND WHILE VERIFYING, still open and NOT in the approved scope:
1. scripts/metric-desc-lint.py does not exist, though StarMetricDesc.hpp says it "requires DESIGNATED
   INITIALIZERS at any site that sets .measures/.validWhen". No source site uses either field, and no
   site uses designated initializers. A claim with no instrument, plus two never-used descriptor fields.
2. client-residency reads 184/185 harness lines in StarClientApplication.cpp -- one line of headroom.
   The next harness edit turns it red. Raising it is a decision (defer #237 again), not a formality.
3. D05's cadence question is not settled by data: 1035/1035 was measured at 72% headroom, the regime
   where publish/consume coalescing cannot occur. Call is safe in both regimes; the loaded regime is
   untested. The matrix's own heavy legs are the natural place to check it.
```

<a id="c29c1332-239"></a>

#### #239 — MATRIX-RUN: the 8-lever matrix, 28 legs, run id matrix-20260807-071440

status: **completed**

- `297090b0` R14: the matrix reproduced R07's own impact statement on a row I had closed
- `f6cb3258` R13: the matrix produced 28 legs and not one quotable cost, for one lazy registration

```
COMPLETE 2026-08-07 08:02. Run matrix-20260807-071454, 28 legs in 48 min (~105s/leg, not the
estimated 98 min). Data: harness/matrix/matrix-20260807-071454/ (GITIGNORED -- findings below are
the copy that travels).

VERDICT: 24 lever legs OK, 0 VOID, 0 failed. Cost attribution NOT performed, by design.

WHAT THE RUN PROVED ABOUT ITSELF
 * Scene fingerprint (O01), first real use: baselines 560/556/559 entities, every one of 24 lever
   legs within 1.44% (most under 0.72%) against a 5% bound. Every leg measured the same scene.
 * Witnesses: all 24 read MOVED. drawableCache 201750->0 / 202455->0 / 208824->0; vboOrphan
   152259->0 / 153728->0 / 153609->0; envRefresh 3374/3375/3375->0; parallaxRefresh
   2998/2999/3000->0; temporalDecouple 2689/2686/2655->0; gatherCache 1561/1634/1692->0;
   backdropComposeMerge inverts 0->4499/4500/4498. WEAKEST WITNESS: scriptProtoCacheEnabled at
   231 / 45 / 238 -- a 5x swing between passes. Worth a look before anyone leans on that lever.
 * Frame rate identical on every leg: 16191-16211us, 0.12% spread, 62 fps, ~72% idle throughout.
   The loop is PACED, not saturated. No lever moved it.

FOUR READ-OUTS
 1. D05 coalescing -- CONFIRMED, MY PREDICTION WRONG. 3 of 27 legs coalesced, at 0.99903 /
    0.99942 / 0.99956. I predicted ~0.997 on the three lightingTemporalDecouple legs; two of
    those were EXACTLY 1.00000 despite 4500 publishes each, and two of the three hits were
    lightingGatherCache legs instead. So coalescing is real, sporadic, an order of magnitude
    smaller than the 0.27% probe, and NOT reliably produced by raising the publish rate. The
    probe also had antiAliasing on, which the matrix legs did not -- a plausible untested
    contributor. Cadence::Call remains correct and is arguably better justified: under Recompute
    3 of 27 legs would be scaled up for reasons unrelated to GPU work, which reads as noise
    rather than as bias and is therefore harder to catch.
 2. D04 nesting suppression -- MEASURED NULL. All 12 .nested counters zero across all 27 legs, on
    top of 0 in two standalone probes. The guard suppresses nothing at this scene. The
    Frame->Call cadence fix stands on its own argument (count is neither frames nor blits), not
    on suppression.
 3. R07 part-set stability -- FAILED, see R14. Three distinct timer key sets across 27 legs; the
    absent key is always a mutually-exclusive compose arm, and it lands on backdropComposeMerge
    and parallaxRefreshInterval specifically. R07's fix covers ran-but-unresolved, not never-ran.
 4. Settle-vs-cap -- 3 of 28 legs capped. Established as world-settling noise, NOT lever-driven
    (same lever capped in one pass and settled in another; fps identical across all legs). My
    two mid-run mechanisms were both wrong; reading 1 was right.

LEDGER CONSEQUENCES, both committed
 * R13 (f6cb3258, evidence corrected in 297090b0): tick.server.lock.sync.us registers on first
   acquisition; costsQuotable "no" on 28 of 28 legs. BLOCKS_MATRIX.
 * R14 (297090b0): R07 half-closed. Filed because the matrix reproduced R07's own impact
   statement on a row I had already marked done -- the signature was true and answered the
   smaller half.

NEXT RUN NEEDS, in order: fix R13 (one hoist, plus the three latent siblings), then R14, then
re-run and assert zero legs flagged. The cost-analysis half still waits on the schema 3->4
descriptor convergence regardless.
```

<a id="c29c1332-240"></a>

#### #240 — R13-FIX: hoist the four lazy tick.server.lock.* registrations — closes the ledger's only BLOCK

status: **completed** · blocks: #242

- `5605be17` R13 CLOSED: the four lock timers register at static init, not at first acquisition
- `495682df` BOARD-HTML: a reading surface for the board, from the same generator — not a second authority

```
RANK 1 of the post-matrix order (2026-08-07). ~15 minutes.

tick.server.lock.sync.us is a function-local static in WorldServerThread::sync(). sync() is the
PERIODIC disk sync, so its first acquisition usually falls INSIDE the measurement window;
telemetry-window.py then refuses to difference a lifetime total against an absent entry and
lever-matrix.sh flags that leg's costs not-quotable. Measured: 28 of 28 legs of
matrix-20260807-071454, costsQuotable "no" on every one including the warm-up.

FIX: hoist to namespace scope in StarWorldServerThread.cpp -- and do ALL FOUR lock timers
(.sync.us, .us, .message.us, .queue.us), not just the one that bites. The other three share the
lazy shape and are saved only by being acquired every tick; fixing one leaves the pattern in the
file for the next rare-path timer to inherit.

Ledger row R13 (BLOCKS_MATRIX). Signature is an ordering predicate already checked in both
directions. Closing this takes the ledger to 0 BLOCK and makes every subsequent matrix run
quotable. Everything numeric downstream depends on it.

Gate: scripts/ci/run-gates.sh + core_tests + game_tests. Commit ends [#240].
```

<a id="c29c1332-241"></a>

#### #241 — R14-FIX: register both compose keys eagerly — the never-RAN arm still reads ABSENT

status: **completed**

- `cd0b5175` LEDGER: file R15 and R16 -- the two registrations R14 did not reach
- `775176ff` R14-FIX: a pass that never runs now reads ZERO, not ABSENT -- and the leg file says so

```
RANK 2 of the post-matrix order (2026-08-07). ~45 minutes.

R07's fix registers a GPU pass key at GlGpuTimer::begin, which covers a pass that RUNS but whose
queries never resolve. It cannot cover a pass that NEVER RUNS, because begin() is never called for
it -- and StarBackdropPass picks exactly one of two mutually-exclusive compose arms.

MEASURED in matrix-20260807-071454: three distinct registered-timer key sets across 27 legs.
21 legs have parallax.compose and lack environment.compose; the three backdropComposeMerge-OFF
legs have both; the three parallaxRefreshInterval-OFF legs have environment.compose and lack
parallax.compose. Owner `gl` closes over a different part set on different legs of the very
levers being compared -- R07's own impact statement, live on a row marked done.

FIX, and it is cheaper than R07 assumed. Both keys AND both descriptors already live in
StarBackdropPass.cpp. Lift each key+desc into a named constant used by BOTH an eager
construction-time registration and the begin() call. No second source of truth, no global table,
no vocabulary-drift hazard -- which was the exact objection that made R07 stop short, and it
dissolves at file scope.

Bounded today because both keys are MetricRole::Detail and do not enter the gl closure sum; the
exposure is the printed table and any leg-to-leg part-set diff. It is lever-CORRELATED, landing
on two of eight levers rather than scattered.

Ledger row R14 (DEGRADES_MATRIX), signature is absent-sense and checked. Commit ends [#241].
```

<a id="c29c1332-242"></a>

#### #242 — MATRIX-RERUN: bank Desert Town quotable, then a second scene — with a PMU sample alongside

status: **completed** · blocked by: #240 · blocks: #173, #246

- `24bd4a51` BOARD: the board cannot cite the commit that creates it -- name the fixed point

```
DONE 2026-08-07. Two runs: matrix-20260807-112809 (Desert Town, 47m46s) and
matrix-20260807-122912 (The Ark Ruins, ~48m). PMU sampled alongside both.

LEG A -- ALL THREE ASSERTIONS PASS, and they pass at BOTH scenes:
                              before (071454)   Desert Town   Ark Ruins
  legs not-quotable            27 of 27          0 of 27       0 of 27
  distinct *.gpu_us key sets   3 (sizes 11-12)   1 (size 13)   1 (size 13)
  legs carrying `zeroed`       0 of 27           27 of 27      27 of 27
#240 and #241 are now verified against the artifact that exposed them, at two scenes.
Nothing else moved: baseline cpu.frame.total 16203->16206, idle 11600->11509, gpu_span 16200->16207.
Ark Ruins: 21 OK, 3 VOID, 0 failed. All 3 VOIDs are parallaxRefreshInterval (below).

THE HEADLINE: SCENE DEPENDENCE IS ~50x, AND EVERY NUMBER THIS CAMPAIGN HOLDS CAME FROM THE CHEAP ONE.
  i915 rcs0-busy, calibrated (idle floor 0.213%, glxgears uncapped 88.67%, 416x working range):
    Desert Town baseline      0.22%   -- INSIDE the idle floor
    Ark Ruins baseline        9.97%   (12.30 / 8.16 / 9.46 across r1/r2/r3)
    Desert Town, ALL caches off  32.93%
  render.pass.parallax.gpu_us is 5174us at Desert Town and EXACTLY 0 at Ark Ruins -- the parallax pass
  does not run there at all, which is why all three parallaxRefreshInterval legs VOIDed on a witness
  that reads 0 in both arms. The runner caught a lever that would otherwise have produced a
  plausible delta from two identical arms.

GPU_SPAN_US IS BLIND, PROVEN THREE WAYS. render.frame.gpu_span_us reports ~16,200us/frame -- the frame
period -- in every condition measured:
    Desert Town caches on   0.22% busy -> 16,207us
    Desert Town caches off 32.93% busy -> 16,193us
    Ark Ruins   caches on   9.97% busy -> 16,176us
  A metric identical across a 150x change in the quantity it names is measuring elapsed GPU TIMELINE,
  not work. Every per-pass *.gpu_us number this campaign has quoted is a span. This is #235's thesis,
  measured rather than argued.

WHAT I COULD NOT PRODUCE, STATED AS A RESULT: PER-LEVER GPU COST. I computed a delta table
(off-renderVboOrphan +4.24pp, off-lightingTemporalDecouple +3.41pp, ...) and it DOES NOT SURVIVE ITS
OWN NOISE CONTROL. The baseline's own repeat-to-repeat spread is 4.13pp (12.30 / 8.16 / 9.46), which
exceeds most of the deltas. Only off-lightingTemporalDecouple (13.09/13.55/13.50, spread 0.46, above
baseline's MAX) is clearly separated. The sim is live and non-deterministic by design, and 6-8 samples
of 10s across a 90s leg cannot resolve a few pp against that. Attribution needs continuous sampling
aligned to leg boundaries -- see the follow-up task.

WHAT THESE RUNS DECIDE:
  * #246 WORLD-PASS -- justification stays RETRACTED, now by positive proof. render.pass.world.gpu_us
    is ~9700us (Desert Town) / ~8900us (Ark Ruins), but total GPU busy at Ark Ruins is 9.97% of a
    16.2ms frame ~= 1.6ms. The "biggest bracket" is ~5.6x the entire GPU's busy time. It is a span.
  * #173 RB-FLUSH -- CPU lever. CPU is 71% idle at Desert Town and 84% idle at Ark Ruins, and the GPU
    is at the idle floor at one of them. CLOSE, do not do.
  * #237 gets its acceptance oracle: two scenes, two banked runs, all legs quotable.
  * #245 cost attribution must read PMU busy, NOT the GL spans.
```

<a id="c29c1332-243"></a>

#### #243 — REG-RATCHET: pin the count of lazily-registered telemetry handles so there is no eighth instance

status: **completed** · blocks: #245

- `d3650330` REG-RATCHET: pin the lazy-registration count at 95, so there is no tenth instance

```
DONE 2026-08-07 (d3650330), in scripts/metric-desc-lint.py as the task specified -- same file, same
selftest harness, its own gate name and verdict line.

THE RATCHET: 95 function-local `static auto x = Telemetry::(counter|gauge|timer|rate)(` sites, pinned.
Registered as gates reg_ratchet + reg_ratchet_fires.

THE RULE IT ENCODES, and it is a MEASURED convention rather than an assumption:
    static auto x = Telemetry::...(...)   in a function   -> LAZY, counted
    auto x       = Telemetry::...(...)    namespace scope  -> EAGER, ignored
All 95 static-auto sites are indented (none at column 0), and every eager registration shipped by
#240 and #241 is written WITHOUT `static`. So `static` on a Telemetry registration MEANS lazy. That
convention is now written down at the regex, including the instruction to drop `static` when writing
an eager namespace-scope registration -- otherwise a correct fix would read as a regression here.

THREE REFUSALS, each from a defect this repo has actually paid for:
  * ZERO MATCHES FAILS. A naive `total > ceiling` reports the tree's best-ever result the moment the
    pattern stops matching. Caught three times now; twice today in gates written the same afternoon.
  * SLACK IS REPORTED, with the number to lower the ceiling to. A ceiling above the real count is
    exactly the room the next lazy registration slides into unnoticed.
  * IT PRINTS ITS DENOMINATOR -- the task's own instruction, "a ratchet whose count nobody sees is a
    ratchet nobody notices is wrong".

NOT COVERED, NAMED IN THE DOCSTRING RATHER THAN LEFT SILENT: a function-local NON-static registration
(`auto c = Telemetry::counter(key, desc)` inside a function body) registers on first call exactly like
the static form. Exactly one exists -- cpu.process.total_us in StarTelemetryReporter::writeSnapshot,
low risk because that function runs on a fixed interval and so always registers early. Separating it
from the eager namespace-scope form needs brace-depth parsing, and an indentation heuristic is
precisely what gputimer-brackets refuses ("indentation is a style, braces are the language"). If a
second appears, the docstring paragraph is where the note is.

PROVEN, not asserted: 6/6 selftest arms (counts lazy, ignores eager, ignores comments, fires on
growth, passes exactly at the ceiling, refuses to pass on zero matches), plus an injection against the
REAL tree -- one added lazy registration takes the count to 96 and turns the gate red. 47 gates green.

SEQUENCING SATISFIED: this landed BEFORE #245's descriptor convergence, which touches all 152
MetricDesc sites. A large mechanical pass is exactly when the pattern comes back, and the lock is now
on before the stable is opened. #247 will REMOVE instances, so expect the ceiling to need lowering --
the check will say so by name.
```

<a id="c29c1332-244"></a>

#### #244 — POST-MATRIX ORDER: SUPERSEDED — the board is the order now, and five of the nine have since resolved

status: **completed**

- `bb29075c` BOARD: Next shows POSITION, which always starts at 1
- `ce8a089d` BOARD: drop the rank contiguity check — it fired on progress
- `35bbb97c` BOARD-NEXT: a ranked Next list whose source is the task itself
- `495682df` BOARD-HTML: a reading surface for the board, from the same generator — not a second authority

```
CLOSED 2026-08-07 as superseded. This was the ranked-nine index written the morning the "GPU-bound"
reading was retracted. Keeping it would leave a second, stale copy of the order beside the board,
which renders the live ranks from metadata.rank -- exactly the two-sources-of-truth shape this
campaign keeps paying for.

WHAT HAPPENED TO THE NINE, so the history resolves without re-reading this file:
  1  #240 R13-FIX                  DONE. Closed the ledger's only BLOCK.
  2  #241 R14-FIX                  DONE. All 13 pass keys eager; gpu_pass_keys gate added.
  3  #235 BUSY-VS-WALL             OPEN, and much cheaper than it looked -- the substrate turned out
                                   to exist in /proc (per-client GPU, per-thread CPU).
  4  #242 MATRIX RE-RUN            DONE, then re-done as #249 after #248 proved the alignment wrong.
  5  #243 REG-RATCHET              OPEN, unchanged, still precedes #245.
  6  --   descriptor convergence   = #245, OPEN, still coupled to #235.
  7  #237 MEASURE-CLIENT           OPEN. Its acceptance oracle now exists: two banked scenes.
  8  --   WORLD PASS               = #246, CLOSED. Engine busy came back at 23.70%/11.39%, which is
                                   the condition that task set for closing itself.
  9  #173 RB-FLUSH                 CLOSED. CPU lever, loop sleeps 72-84% of every frame.

THE RANKING PREMISE HELD. This file argued #235 belonged at 3 "despite being the most work" because
until work and waiting are separable, nothing below it can be honestly ranked -- and items 8 and 9
had just lost their evidence to that confusion. Both are now closed on measurements that only became
possible once an instrument measured WORK rather than span. The argument was right.

Two items were added after it and are not in the nine: #248 (PMU attribution machinery, DONE) and
#249 (the re-measurement it forced, DONE). Live order lives on the board and in the artifact.
```

<a id="c29c1332-245"></a>

#### #245 — DESC-CONVERGE CLOSED: unit and boundedness are read, the ratchet counts only what it names, and two of my own claims were wrong

status: **completed** · metadata: `{"rank": 1}`

- `da28b864` [#245] I called the capture mode "the real blocker" without checking the corpus; it is a hazard, and now it is recorded
- `a01c4a9c` [#245] clock_ratchet counted three things it shouldn't, and one it couldn't move
- `21adb941` [#245] the runner claimed a migration that had already landed, and wrote the claim into evidence
- `bc0305df` [#245] the client reads three keys it does not own, instead of re-declaring them
- `7f222773` [#245] descConflict finally has a reader outside a test
- `fe20f87d` [#245] a gauge now says whether it is a level or a peak, and the consumer believes it
- `bea29db1` [#245] the closure refuses to add quantities in different units
- `fa04b907` [#245] the before-picture: the consumer's answers are now a thing a gate can check
- `aa484259` evidence: hash the one matrix run that can still be re-windowed

```
Spec: docs/superpowers/specs/2026-08-06-metric-descriptor-convergence-design.md

lever-matrix.sh emitted raw legs and explicitly did NOT attribute cost because "the vocabulary is
being converged". This was that convergence — and the closing pass established that the quoted
reason had not been true for over a week.

=== WHAT SHIPPED (8 commits) ===

aa484259  EVIDENCE PRESERVED. harness/ is gitignored with ZERO tracked files, and NINE of ten banked
matrix runs kept no .snapshots — they banked the consumer's OUTPUT and discarded its INPUT and can
never be re-windowed. Only matrix-20260808-140430 kept its 486 snapshot files, and it is the basis
of the 27-leg result. Read-only copy at /root/backups/openstarbound-evidence/ +
docs/evidence/matrix-20260808-140430.manifest.md, verifying 599/599 files, 0 mismatches. The
manifest is deliberately the only TRACKED half — the corpus carries a player save.

fa04b907  THE BEFORE-PICTURE, BUILT FIRST. No gate could judge a consumer change: every other gate
reads the SOURCE, this one reads the ANSWERS. banked_corpus_reread re-windows all 27 legs and diffs
window/metrics/owners/zeroed/violations/label plus meta. Exclusion set is FOUR keys, a closed
literal, with an arm proving a look-alike key still fires. Exits 77 in CI (untracked corpus).

bea29db1  THE LIVE DEFECT: THE CLOSURE ADDED QUANTITIES IN DIFFERENT UNITS. Schema 4 shipped `unit`
at 58 sites and TRANSPORTED it; nothing read it. A correctly-declared nanosecond metric would have
been summed as microseconds, 1000x, silently, version check passing. unit_basis() now runs BEFORE
the sum. Mixed units REFUSE the closure; no conversion is guessed. Weakness declared: undeclared is
homogeneous with itself, because refusing 109 of 146 banked metrics would refuse the evidence.

fe20f87d  A GAUGE SAYS WHETHER IT IS A LEVEL OR A PEAK. Four of eight gauges never fall — verified
at the WRITE SITE (`s = max(s, x)`), not inferred from the name. The consumer windowed every gauge
as a level, so a run-long peak arrived in `value` beside a counter's honest window delta. All eight
declared; a peak emits runLongMax + peakNotWindowed. boundedness_declared is A RULE, NOT A RATCHET.

bc0305df  The cross-TU re-declaration removed; the one-argument overload asserts no descriptor, so
there is nothing left to drift. descConflict/typeConflict gained a real consumer reader + test —
they had been a claim with no instrument in the very field this task was populating.

21adb941  THE RUNNER CLAIMED A MIGRATION THAT HAD ALREADY LANDED, AND WROTE THE CLAIM INTO EVIDENCE.
Three sites said "being converged (schema 3 -> 4)"; the bump landed at 9db54200 and the file was
edited two days later, so the stale claim rode into banked manifests — one site is a string written
into every manifest.json. New gate `convergence_claims` is anchored on the LIVE SCHEMA read from
telemetry-window.py, not on the string "schema 3 -> 4", which would pass the moment someone wrote
"4 -> 5". Rule: a claim that something is in flight must name its destination, and that destination
must not already have been reached. Null control against the ORIGINAL file from git: 4 sites fire at
schema 4, ZERO at schema 3 — it was correct when written, which is why reading it never caught it.

a01c4a9c  THE RATCHET COUNTED THREE THINGS IT SHOULDN'T. clock_ratchet rglobbed source/test/, where
one fixture site was undeclared against a slack of exactly one — deleting a test file was a legal
way to buy headroom for a new production violation. It also counted the two `MetricDesc{}`
sentinels, which are the deliberate ABSENCE of a descriptor. 108/165 was three parts wrong; repaired
corpus is 105/161, ceiling 109 -> 105, slack 1 -> 0.
  THE PLAN SAID ADD A `unit_ratchet`; I MEASURED FIRST AND IT WOULD HAVE BEEN A DUPLICATE. The two
  sets are IDENTICAL — all 105 sites lacking a clock also lack a unit, because the population is
  exactly the sites still using the positional form. One ceiling now counts either facet.
  Renamed clock_ratchet -> desc_facet_ratchet, and the one live prose reference was remapped by
  reading it.

da28b864  CORRECTION TO MY OWN 21adb941. Fixing the stale claim, I replaced it with a different
wrong one: that attribution is blocked because the five Budget parts record only under
telemetryDeepTracing (default false). The mechanism is real — TelemetryScope's constructor is
`deepEnabled() ? now : -1` — but I reasoned from the DEFAULT to the CORPUS instead of reading the
corpus. All 54 banked legs carry every Budget part at count=4498, the same count as the whole,
because harness/storage-perf/starbound.config pins the flag TRUE. ATTRIBUTION IS NOT BLOCKED.
  The real defect was one level up: nothing recorded which capture mode a leg ran in, and the pin
  lives in a gitignored file — ABSENT-vs-ZERO promoted from a single key to a whole capture mode.
  The manifest now records `captureDeepTracing`, read from the config the run actually used, and
  records None rather than a guess when it cannot be read.

=== SCOPE THAT WAS DELIBERATELY NOT DONE (survey wf_f9fcd43d-f37, two adversarial FATALs adopted) ===
  * ALL ELEVEN PROPOSED `whole` PAIRINGS ARE A CATEGORY ERROR — every one sat on a MetricRole::Detail,
    defined as "nested inside a Budget part; NEVER SUMMED". Deferred to #171.
  * THE ~110-SITE PROSE SWEEP IS OUT. Two proposed validWhen strings are FALSE at HEAD, and 27 of the
    rest restate MetricClock::GpuTimeline, already declared AND already enforced by gpu_timeline_role.
    Populating sites with restatement is worse than leaving them honest and empty.
  Four fields remain at zero production sites: source, measures, validWhen, whole.

VERIFIED THROUGHOUT: banked_corpus_reread 27/27 byte-identical after EVERY change — nothing above
re-interpreted a single banked number. 64 gates green; metrics_mutual SKIP (needs a live GPU client,
correctly not counted as a pass). Gate count 59 -> 66.
```

<a id="c29c1332-246"></a>

#### #246 — WORLD-PASS: CLOSED — the bracket is 2.5x the entire GPU's busy time, so it is a span, not a cost

status: **completed** · blocked by: #242

- `7bede779` [#271] the wait hypothesis is refuted at 0.03% -- it is CPU work, and the per-light section is 38% of it
- `164163cc` [#271] the 152-159 us/frame lump gets seven parts, and one of them is the wait

```
CLOSED 2026-08-07, NOT DONE, on the exact criterion this task set for itself: "DO NOT START until
#242's PMU sample says the GPU is genuinely busy. If engine busy comes back at ~25%, there is no GPU
bottleneck at this scene and this task should be CLOSED, not done."

Engine busy came back at 23.70% at Desert Town and 11.39% at Ark Ruins, both measured with a
calibrated out-of-process instrument (idle floor 2.34%, glxgears uncapped 88.67%) and exact window
alignment. That is the stated condition, met.

THE ARITHMETIC THAT SETTLES IT. At Desert Town the whole GPU is busy 23.70% of a 16.2 ms frame,
i.e. ~3.84 ms of actual engine time per frame. render.pass.world.gpu_us reports 9,578 us. The
"biggest bracket" is ~2.5x the entire GPU's busy time for the whole frame -- it cannot be a cost.
It is how long the bracket was OPEN.
(An earlier note here said ~5.6x; that used the mis-aligned 0.22% baseline and is corrected.)

AND THE SECOND CHECK THIS TASK ASKED FOR IS ANSWERED. It required explaining the gap between #132's
session average of 1,839 us/frame for the world pass and the matrix's 9,440. The explanation is the
same one: both are timeline spans, and a span's size tracks how long the frame was, not how much work
happened inside it. render.frame.gpu_span_us reports ~16,200 us -- the frame period -- at 0.22%,
11.39%, 23.39% and 32.93% engine busy alike. Nothing about the 5x gap needs a scene or a regression
to explain it.

WHAT WOULD RE-OPEN IT: a scene where per-client engine busy is high enough that the GPU is the
binding constraint. Neither measured scene is close -- 61.7 fps on every leg, frame paced, CPU 72-84%
idle. If such a scene is found, the target must be chosen from PMU busy or per-client drm-engine
accounting, NEVER from a *.gpu_us bracket, which is the mistake this task was filed on.
```

<a id="c29c1332-247"></a>

#### #247 — R15+R16-FIX: the .nested counters and owner gl's TOTAL are the two registrations R14 did not reach

status: **completed**

- `cb59fc50` R15+R16: the rejection counters and owner gl's TOTAL -- the two registrations R14 did not reach

```
DONE 2026-08-07 (cb59fc50). Both rows closed; ledger 22 open -> 20.

R15 -- THE 13 .nested REJECTION COUNTERS. Each records samples the nesting guard rejected, and each
carried R14's defect one level down: registered inside begin(), so for a pass whose arm never runs "no
samples were rejected" and "nothing was watching" were the same reading -- verbatim the sentence
render.gputimer.dropped's own comment uses to justify ITS eager registration, three lines away. All 13
now register eagerly, PAIRED with their timers so they cannot drift apart by eye.

Second, independent defect on the same line: `name + ".nested"` heap-allocated a String per pass per
frame on the render path. StarMetricDesc.hpp forbids exactly that a few lines from its own definition.
begin() now resolves the handle ONCE per key behind a flag on Ring. The TIMER is deliberately still
re-resolved every begin -- two call sites may name one key and descConflict is raised only by a call
that PASSES a desc; the nested counter has one descriptor everywhere, so it has nothing to detect.

R16 -- OWNER gl's DECLARED TOTAL, render.frame.gpu_span_us, was reachable only four conditionals deep.
Worse in kind than the R14 keys: those were Detail, this is the DENOMINATOR. When absent,
telemetry-window took its "declared total is absent" branch and printed NOTHING, because D01 demoted
the pass timers to Detail so `if parts:` was false -- the whole gl/gpu block vanished, indistinguishable
from an owner with no metrics. Counted before fixing: present in 27 of 27 legs, so latent not live.

BOTH HALVES OF R16. no_whole_report is extracted and speaks even with zero parts, and is a VIOLATION
only when parts exist: with parts a real budget went unclosed; with none nothing was mis-closed, but
the instrument still failed to produce its denominator and that is a fact about the run.

THE GATE DERIVES THE PAIR. gpu_pass_keys appends ".nested" to each begun key rather than reading a
second list -- there is nothing for a future key to be forgotten from, which is what R14's fix note
meant by "do not simply duplicate the list". Now 13 begun / 13 registered / 13 rejection counters, all
three sets agreeing both ways, with two new selftest arms (missing .nested; orphan .nested) proven to
fire against the REAL tree by injection, not only fixtures.

TWO SIGNATURES RE-AUTHORED, recorded because rewriting a signature to match what you did is how a
ledger stops corresponding to anything:
  * R15's original pattern STILL MATCHES after the fix -- begin() still resolves the handle, now once
    behind a guard. The defect was never that text. Signature now asserts the substantive state.
  * R16's failed only because the renderer's anonymous namespace sits OUTSIDE `namespace Star`.
    Widened to (?:Star::)?; a signature that reads red over a namespace prefix is about spelling.

Ratchet unchanged at 95 -- neither fix adds or removes a function-local static, so no ceiling move was
needed. 47 gates green, core_tests 312/312, game_tests 72/72, telemetry-window selftest 13/13.
```

<a id="c29c1332-248"></a>

#### #248 — PMU-ATTRIB: per-lever GPU cost needs continuous sampling, not spot samples against a moving baseline

status: **completed**

- `dd30bfa5` PMU-ATTRIB: the second instrument is now always on, and the floor is a mechanism

```
DONE 2026-08-07 (dd30bfa5). Mechanism built AND proven end-to-end on a real run
(matrix-20260807-145441, --repeats 1). On its first live run it falsified the numbers that motivated
building it, which is the tool working.

WHAT SHIPPED, four parts:
 1. telemetry-window stamps meta.windowStartEpoch/EndEpoch from the mtimes of the two snapshots the
    window is ACTUALLY differenced over. Not the env sidecar -- that brackets the wider measurement
    phase. Stamped at window time because render-profile purges the snapshot dir at the next leg.
 2. scripts/pmu-engine-sample.py -- continuous rcs0 with a slow ccs0/bcs0/vecs0 control sweep.
    UNAVAILABLE is written, never 0.000000.
 3. scripts/pmu-join.py -- prints a RESOLUTION FLOOR, reports anything inside it as UNRESOLVED.
    Floor = max(null-control delta, baseline spread), measured IN THAT RUN.
 4. lever-table.json declares gpuNullControl on scriptProtoCacheEnabled; pmu-join READS it.
 lever-matrix starts the sampler itself and joins at the end, non-fatal by construction.

PROVEN: 751 usable samples vs 47 across the whole previous 48-minute run; 9 of 9 legs stamped and
resolved; null control read +0.15pp; both selftests registered as gates; 45 gates green.

AND IT IMMEDIATELY RETRACTED ITS OWN PREMISE. Same scene, same binary, same warp:
    Desert Town baseline, OLD method (leg-JSON mtime -90s, 4-6 spot samples):   0.22 %
    Desert Town baseline, EXACT stamps (50 samples inside the real window):    23.39 %
    idle floor measured immediately after, game not running:                    2.34 %
The leg JSON is written AFTER the client is killed and the log archived, so [mtime-90, mtime] is
shifted late and straddles the inter-leg gap where the client is DEAD and the GPU is at idle. With
4-6 samples per leg at 53s cadence against ~103s legs, the mean was decided by sampling PHASE.

CONSEQUENTLY RETRACTED (see the follow-up task):
  * "Desert Town runs at the idle floor / the campaign has taken essentially all the GPU load."
  * "~50x scene difference between Desert Town and Ark Ruins" -- BOTH figures used the bad alignment.

SURVIVES, and is now stronger for having a correctly-aligned pair:
  * gpu_span_us is BLIND. 23.39% busy -> 16,208us; 32.93% busy -> 16,193us. Both correctly aligned
    (the all-caches-off probe sampled concurrently over a single profile). A 1.4x change in real GPU
    work moves the reported "GPU span" by 0.1%.
  * The instrument itself: idle 2.34%, glxgears uncapped 88.67%.
  * #246 -- render.pass.world.gpu_us is 9,578us while the WHOLE GPU is busy 23.39% of a 16.2ms frame
    ~= 3.8ms. The bracket is still ~2.5x the entire GPU's busy time (was quoted as 5.6x on the bad
    baseline). It is a span. CLOSE stands.
  * #173 -- CPU 72% idle at this baseline. CLOSE stands.

NOT TRUSTWORTHY YET, and stated rather than buried: this verification ran --repeats 1, so the
baseline spread is 0.00pp by construction and the floor collapses to the null control alone (0.15pp).
The 3-repeat run showed the baseline moving 4.13pp between repeats. Every delta in the verification
table is provisional until a 3-repeat run with the fixed alignment.
```

<a id="c29c1332-249"></a>

#### #249 — RE-MEASURE: every GPU busy number from 2026-08-07 was mis-aligned; re-run both scenes with the fixed stamps

status: **completed**

- `ad4e2f54` PMU-JOIN: a delta smaller than its own scatter is not a measurement, and one selftest arm could not fail
- `caea59f5` PMU-JOIN: a leg the runner VOIDed must not reach the table

```
DONE 2026-08-07. Both scenes re-measured with the exact window stamps.
  Desert Town  matrix-20260807-151445   23 OK, 1 VOID, 0 failed, 2150 samples, 27/27 stamped
  Ark Ruins    matrix-20260807-160513   21 OK, 3 VOID, 0 failed, 2158 samples, 27/27 stamped
All three assertions pass at BOTH scenes: every leg stamped; floor built from both terms; and the
null control lands INSIDE the floor (+0.04pp / -0.06pp against floors of 0.45 / 0.43).

THE RETRACTION HELD, AND THE DIRECTION WAS ALSO WRONG. Mis-aligned data said Desert Town 0.22% and
Ark Ruins 9.97% -- a ~50x gap with Ark the heavier scene. Correctly aligned:
    Desert Town baseline  23.70 %   (repeats 23.93 / 23.48 / 23.68, spread 0.45)
    Ark Ruins   baseline  11.39 %   (repeats 11.64 / 11.32 / 11.21, spread 0.43)
A real ~2x scene difference, with DESERT TOWN THE HEAVIER ONE -- the opposite of what was reported.
Baseline spread fell from 4.13pp to ~0.44pp: what was called run-to-run variance was sampling phase.

PER-LEVER GPU COST, first quotable table in the campaign, both scenes:

  lever                        Desert Town        Ark Ruins
  parallaxRefreshInterval      +16.61 pp          VOID (no parallax pass at this scene, all 3 repeats)
  envRefreshInterval            +5.42 pp          +1.42 pp
  renderVboOrphan               +3.16 pp          +3.85 pp
  lightingTemporalDecouple      +2.48 pp          +2.07 pp
  backdropComposeMerge          +1.68 pp          unresolved (-0.17, inside floor)
  renderDrawableCache          unresolved         unresolved (own spread 2.27 > delta 0.59)
  lightingGatherCache          unresolved         unresolved (own spread 3.17 > delta 0.83)
  scriptProtoCacheEnabled      +0.04 (control)    -0.06 (control)

WHAT GENERALISES, which is what the second scene was for:
  * renderVboOrphan is the ONLY lever that resolves at BOTH scenes and is top-3 at both (#3 Desert,
    #1 Ark). The most robust lever we own -- and it is #125, the VBO orphaning work.
  * lightingTemporalDecouple and envRefreshInterval also resolve at both, in the same direction.
  * parallaxRefreshInterval is the largest single lever anywhere (+16.61pp) and is INAPPLICABLE at
    the other scene. Most scene-dependent thing in the table by a wide margin.
  * renderDrawableCache and lightingGatherCache resolve at NEITHER scene on the GPU. Both are
    plausibly CPU levers; that is a statement about the GPU axis only.

AND NOTHING MOVES THE FRAME. Every leg at both scenes runs 61.7 fps within ~13us. Going from 23.70%
to 40.31% GPU busy -- a 70% increase in real GPU work -- moves the frame by 0.06%. The frame is paced
and the CPU sits 71-74% idle, so the levers buy POWER, THERMAL HEADROOM AND MARGIN, not frames. Any
future lever must be justified on that basis at these scenes.

TWO TOOL DEFECTS FOUND BY THE RUNS AND FIXED (caea59f5, ad4e2f54): pmu-join was quoting deltas from
legs the runner had VOIDed; and its floor was blind to a lever's own scatter, which let two
outlier-driven Ark Ruins results read as RESOLVED. A selftest arm that could not fail was also found
and repaired.

NOT ADDITIVE, stated so nobody sums the column: the seven Desert Town deltas total +29.22pp, while
turning those seven off together measured +9.2pp over baseline. The levers overlap heavily; each
number is a MARGINAL cost given every other lever is on.
```

<a id="c29c1332-250"></a>

#### #250 — GM-3 STREAM: the single sovereign API surface — one reader interface, one sample type, a LIVE transport that is not files

status: **pending** · blocked by: #235, #253 · metadata: `{"rank": 8}`

- cited in `docs/board-audit-2026-08-08.md`
- cited in `docs/board.md`

```
DIRECTOR, 2026-08-07: "I want the option for full streaming while I play - we can evaluate any performance impact later, but let's not restrict our design because of it." And: "as much of our sovereign observability and metrics system is in fact sovereign, under the metrics/ directory."

NAMED BY THE ROADMAP, NEVER FILED. docs/superpowers/specs/2026-08-05-sovereign-metrics-design.md §5 lists GM-3 as "exporters: OpenMetrics + IPC socket -> external tooling consumes without linking us", and §4.6 puts it explicitly out of scope for GM-1. GM-1 shipped; nothing carried GM-3 onto the board. This is that row.

RUNNABLE NOW. Both blockers closed: #235 (ThreadBusyReader shipped 0eef793b) and #253 (the join, 2026-08-08). Nothing in this task's scope has shipped -- no reader base class, no library-side sample vocabulary, no socket, no OpenMetrics -- verified by the 2026-08-08 board audit.

=== WHAT THE STREAMING DIRECTIVE DECIDES, ARCHITECTURALLY ===

Full streaming rules the FILE PATH out of the live axis. Measured 2026-08-07:
  * TelemetryReporter::writeSnapshot serialises ~70KB of JSON for 150 keys (was 143 when this was
    filed) and is called SYNCHRONOUSLY on the client main thread (StarClientApplication.cpp:527-534,
    the telemetryReportInterval path).
  * There is NO retention policy anywhere in TelemetryReporter or ClientApplication. Re-verified at
    HEAD 2026-08-08: dev/storage/telemetry still holds 54,364 files / 250MB, with nothing ever
    deleting them.
A file per snapshot was only ever tenable at a ~5s cadence. So the live path is a DIFFERENT
path from the archive path: a lock-free ring the sovereign side drains out-of-process. Files
remain for the harness and the archive, where cost does not matter. This is not a perf
compromise -- it is what "streaming" means, and it makes the retention problem stop scaling
with the sampling rate.

=== SCOPE ===

 1. HOIST THE CONVERGENCE POINT INTO THE LIBRARY. samplesFor() -- the one function that turns
    a reader result into MetricSamples -- lives in metrics_main.cpp:74, i.e. in the EXECUTABLE, and
    its own comment at :80 says "When this moves into the library (task #250)". A second consumer
    must reimplement it or shell out and parse. THE PREDICTED COST HAS NOW BEEN PAID: [#253]'s
    scripts/metrics-sample.py shells out to dist/metrics and parses its TSV, which is precisely the
    "reimplement or shell out" this item was filed to prevent. Pure relocation into star_metrics.
    This demotes the CLI from OWNER of the sample vocabulary to its first consumer.
 2. ONE READER INTERFACE. main calls TWO readers by hand -- ClientBusyReader and ThreadBusyReader --
    and both return BusyReading. (CORRECTED 2026-08-08: this used to say they return DIFFERENT types,
    BusyReading vs EngineBusyWindow. That was never true. `git log -S EngineBusyReader --
    source/metrics/metrics_main.cpp` is EMPTY: main has never called EngineBusyReader, which is
    exercised only by source/test/metrics_test.cpp:180-233. The type difference is real but lives
    between the LIBRARY's readers, not at the call site.) A unified surface must absorb it:
        enumerate(pid, window) -> List<MetricSample>
    with ClientBusyReader / EngineBusyReader / ThreadBusyReader / SnapshotReader behind it.
    MetricDesc's MetricSource enum -- InProcess, ProcFs, SysFs, PerfEvent, GlQuery -- is ALREADY the
    taxonomy for this.
 3. THE SnapshotReader ARM IS THE POINT. It makes the game's own 150 in-process keys just
    another source behind the same surface. Two stores, one API. NEVER a merged store: the
    convergence spec §1 already refuted merging (Telemetry is a process-global singleton with
    no subject parameter; MetricSample holds five Strings and throws, so it cannot go on a
    per-call bracket). This is a PRESENTER over two stores.
 4. TRANSPORT. Socket + OpenMetrics. Shaped as REPEATED WINDOWS ON A COMMON CLOCK from day
    one -- a single-shot API retrofitted to stream is the expensive version.
 5. BOTH CLOCKS ON THE SAMPLE. HALF-BUILT SINCE THIS WAS FILED, and this item shrinks accordingly.
    The SNAPSHOT meta already carries both: StarTelemetryReporter.cpp:82-83 writes tMonotonicNs and
    tEpochNs (schema 4, shipped 3a8c530e under [#235]), and the live artefact confirms it. What
    remains is carrying both on the SAMPLE. Carry monotonic for interval arithmetic (immune to NTP
    steps) and epoch for the join and the x-axis. Picking one loses something.
 6. RESAMPLING RULE, and it is load-bearing: re-difference RAW CUMULATIVE COUNTERS onto a
    common grid, NEVER interpolate a rate. Interpolating a rate invents work the hardware
    never did -- the same rule busyDelta already encodes for counters that move backwards.
    MetricBoundedness decides whether a point is allowed to exist: a level and a rate are the
    same line on a plot, and differencing a HighWaterMark (lighting.gpu.spread
    .max_emission_x1000) yields a smooth, plausible, meaningless series.
    [#253] has since IMPLEMENTED this rule in Python (scripts/obs-join.py rediff()), together with
    two properties this item does not yet name and #256's T10/T11 now do: a stated METHOD per source
    (a level, a difference, or a mean of differences) and a denominator that is the span actually
    MEASURED rather than the interval requested. Both belong on the sample type.

=== THE UNOWNED HALF, STATED SO IT IS A DECISION AND NOT AN OMISSION ===

The CONSUMER half of this system is Python in scripts/ with no component at all. RE-MEASURED
2026-08-08: the five originally-named files are now 2,260 lines, not 1,932 -- telemetry-window.py
813 (was 661), gputimer-brackets.py 401, pmu-join.py 372, metric-desc-lint.py 489 (was 313),
pmu-engine-sample.py 185. [#253] then added two more consumers to the same unowned surface,
obs-join.py 514 and metrics-sample.py 317, for a total of 3,091. The gap grew 60% while this row sat
pending. By line count it remains the largest sovereignty gap in the observability system, larger
than the StarClient ride-in. Whether it becomes C++ under metrics/ or stays Python with a DECLARED
boundary is a TSSA question; #200 (ARCH-1) is now DONE, so the boundary machinery to answer it
exists. #256's T7 records what the consumer must carry, derived from the join rather than guessed.

CROSS-REF: #245 is GM-2. #237 would be a consumer. #252 moves the projection code. #254 is the first
consumer of the sample properties this task defines -- built first ON PURPOSE, per #256 T7: designing
the interface before a consumer exists fits the diagram rather than the data.
```

<a id="c29c1332-251"></a>

#### #251 — GM-4 DISJOINT: GL_GPU_DISJOINT_EXT has never been checked, and this GPU clocks 933-2350MHz — every historical GL_TIME_ELAPSED sample may ha…

_Stored subject exceeds the heading; reproduced verbatim:_

```
GM-4 DISJOINT: GL_GPU_DISJOINT_EXT has never been checked, and this GPU clocks 933-2350MHz — every historical GL_TIME_ELAPSED sample may have been silently invalid
```

status: **pending** · metadata: `{"rank": 9}`

```
NAMED BY THE ROADMAP, NEVER FILED. docs/superpowers/specs/2026-08-05-sovereign-metrics-design.md §5 records it verbatim under GM-4:

  "GM-4 carries a known unfixed hazard: GL_EXT_disjoint_timer_query exposes GL_GPU_DISJOINT_EXT,
   which reports when a timer result was invalidated by frequency change or preemption. Our code
   has never checked it, and this GPU clocks between 933MHz and 2350MHz. Every historical
   GL_TIME_ELAPSED sample may have been silently disjoint."

WHY THIS IS THE SAME CLASS AS THE DEFECTS ALREADY CLOSED, not a new kind of thing. The driver
publishes a flag saying "the number I just gave you is not valid". Nothing in the tree reads it.
That is exactly:
  * C05 -- the engine emits descConflict/typeConflict on every metric and NOTHING reads them.
  * R14/R15/R16 -- a handle registered on a path not always taken, so ABSENT and ZERO are the
    same reading.
  * "A claim with no instrument" -- "checked by X" is itself a claim.
An instrument that discards the driver's own validity flag is reporting confidence it was
explicitly told not to have.

AND THE EXPOSURE IS MEASURED, not hypothetical. The env sidecar records gpuClockMhzStart /
gpuClockMhzEnd on every leg precisely because clock drift was suspected; a real leg of
matrix-20260807-160513 reads 883MHz at both ends. The clock MOVES during the runs we quote from.

SCOPE:
 1. Query GL_GPU_DISJOINT_EXT around the timer ring in OpenGlRenderer and DISCARD -- not
    silently, but into a declared counter -- any sample taken across a disjoint interval.
    render.gputimer.dropped already exists as the precedent for a rejection counter that is
    registered EAGERLY so "none rejected" and "nothing watching" cannot be confused (#247).
 2. Decide whether the extension is present at all on this driver, and say so honestly if not:
    an UNAVAILABLE reading is visible, a silent zero is not (BusyReading::unavailable is the
    established shape).
 3. Re-read the retained GPU numbers in light of whatever the disjoint rate turns out to be.
    If it is non-zero, the per-pass gpu_us table needs a stated uncertainty, the way #235's
    findings forced the PMU numbers to be re-measured.

RELATED: #236 (the gl closure is inert), #235 (gpu_span_us is a timeline, not work). Both
concern the same instrument family. This one is about whether the individual samples were ever
valid, which is upstream of what they mean.
```

<a id="c29c1332-252"></a>

#### #252 — TEL-EXTRACT: everything that WRANGLES, PROJECTS or CONSUMES metrics moves to metrics/ — ~390 lines, of which 246 are projection hiding insi…

_Stored subject exceeds the heading; reproduced verbatim:_

```
TEL-EXTRACT: everything that WRANGLES, PROJECTS or CONSUMES metrics moves to metrics/ — ~390 lines, of which 246 are projection hiding inside the core store
```

status: **pending** · blocked by: #235, #255 · metadata: `{"rank": 7}`

```
DIRECTOR, 2026-08-07, two rulings:
  (a) "I agree that instrumental stays - we should aim to keep this as thin a 'shim' as possible,
      but with greatest 'useful metric atomic coverage' in source."
  (b) "All code that wrangles, projects or consumes metrics should probably move to the metrics
      folder for now, and in future we can fold some of the python into the sovereign modules and
      interfaces yet to be built."

Ruling (b) WIDENS THIS TASK from the ~60-line client ride-in it was filed as. RE-MEASURED at HEAD
2026-08-08 by the board audit, and the original accounting had a real error in it:

  StarTelemetry.cpp                       597 lines  (was 587 when filed)
    lines 342-493   naming tables + spec tables       ~152   PROJECTION
    lines 495-568   Telemetry::snapshot()              ~74   PROJECTION
    lines 570-595   Telemetry::reset()                 ~26   STORE -- STAYS
    remainder       lock-free registry, atomics,      ~345   STORE
                    histogram bucketing, applyDesc
  StarTelemetryReporter.{cpp,hpp}         120 lines          PROJECTION (writes the file)
  StarClientApplication + CommandProcessor ~60 lines          MECHANISM (5 sites)
                                          ------
                                          ~406 lines to move, of which ~226 is projection

THE ORIGINAL FIGURE COUNTED reset() AS PART OF snapshot() -- it said "485-587 Telemetry::snapshot()
~103". snapshot() ends at 568; 570-595 is reset(), which is store code and does not move. Corrected
above. ~38% of what is filed as "the core store" is projection: the naming tables rendering the
descriptor vocabulary to strings, the owner denominator/total table, and the JSON serialiser. None of
that is the store. All of it is interpretation.

NOTHING HAS MOVED YET, verified 2026-08-08: all nine xName tables, both spec tables and
Telemetry::snapshot() are still in source/core/StarTelemetry.cpp.

=== THE SEAM ===

snapshot() reads registry().nodes directly with relaxed atomic loads. Moving it means Telemetry
exposes a READ-ONLY VISITOR over its nodes and the projection owns the rendering. Narrow seam, and it
keeps the rule that survives from StarMetricDesc.hpp: the in-process store is the subject reporting on
itself; the outsider that must never be a peer of its subject owns everything that interprets.

=== WHY THIS IS NOT TIDYING: THE VOCABULARY IS ALREADY SPELLED TWICE ===

  unitName()   in core emits "ns", "us", "ratio", "count", "hz"
  MetricSample in metrics_main.cpp writes the LITERALS "ns" and "ratio"
They agree today BY COINCIDENCE. Nothing enforces it, and there is no test that would notice.

And `source` ALREADY DIVERGES:
  sourceName()          emits "procfs", "perf_event", "in_process", "sysfs", "gl_query"
  MetricSample.source   carries "fdinfo:drm-engine", "i915-pmu:rcs0-busy", "procfs:task-stat"
StarMetricDesc.hpp records the mapping in a COMMENT -- "the existing free-text values map onto
these -- 'fdinfo:*' to ProcFs, 'i915-pmu:*' to PerfEvent" -- and nothing executes it. That is a
rule held up by prose, which is the exact shape of the defect the designated-initializer lint was
written to end. One table in one place fixes it by construction.

=== RULING (a): THE SHIM'S THICKNESS IS A COVERAGE LIMITER ===

Today a call site is `static auto x = Telemetry::timer("key", MetricDesc{...})` plus a
TelemetryScope. MetricDesc now has eleven fields, and #245 populates the remaining five across 165
production sites. Left alone, the shim gets THICKER at exactly the moment the Director asked for it to
be thinner -- and a wide brace-init per site is a disincentive to declare a metric at all, which caps
"useful metric atomic coverage" by friction rather than by judgement.

Resolution, and it belongs in #245 rather than here: NAMED DESCRIPTOR FACTORIES at the call site.
The descriptor stays BOUND to the recording call (#167 -- separating them is the reachability bug
class), it just stops being eighteen fields of text.

=== WHAT STILL DOES NOT MOVE ===

RE-MEASURED 2026-08-08, because the previous figure did not reproduce. There are 256 references to
Telemetry::/TelemetryScope/TelemetryTimer/Counter/Gauge outside core/StarTelemetry* and test --
game 133, rendering 46, application 44, base 17, client 13, frontend 3. This task and #256 both used
to record "410 sites, game 183, rendering 105, application 69, base 29", and no grep reproduces it:
the broadest possible count, case-insensitive "telemetry" anywhere including comments, is 352.
Corrected rather than carried, since it is the number that says how much does NOT move.

Nearly all of those 256 are instrumentation AT the point of measurement. A TelemetryScope in
WorldClient::lightingCalc belongs in WorldClient. R07-R16, the nine registration defects closed
2026-08-05..07, were every one of them about keeping declaration at the call site. These stay,
and the goal for them is THINNER, not elsewhere.

=== THE STORE'S DESTINATION IS NOW DECIDED, AND IT IS NOT `core` ===

Director, 2026-08-08, settling #256 T9. The register gained a 54th component, `telemetry` -- LIBRARY,
MACHINE, granted `core` alone -- owning **the handle a subject records through**. `MetricDesc` and its
enums stay in `core`, because a vocabulary two components share belongs where both are granted.

So this task's earlier line "Telemetry's lock-free registry and MetricDesc stay in core" is HALF
SUPERSEDED: the vocabulary stays, the registry becomes its own component. That is a further move this
task does not perform and nobody has yet scheduled -- recorded in the `telemetry` derivation's own
`owes` facet rather than left implicit.

AND THE PROJECTION CANNOT SIMPLY GO TO `metrics` EITHER, for the reason that decided T9: `metrics`
names nothing it measures, and anything named BY the subjects would invert that. The projection moving
to `metrics` is legal only because `metrics` would then name `telemetry` -- an instrument naming an
instrument, never a subject. Stated here because it is the constraint that makes this seam the right
one rather than a convenience.

=== FUTURE, PER RULING (b) ===

RE-MEASURED 2026-08-08: the Python in scripts/ is 2,260 lines across the five originally-named files,
not 1,932 -- telemetry-window 813 (was 661), gputimer-brackets 401, pmu-join 372, metric-desc-lint 489
(was 313), pmu-engine-sample 185. [#253] added two more consumers to the same unowned surface,
obs-join 514 and metrics-sample 317; [#254] and [#255] added obs-plot and lever-cpu. These are
consumers and fold into the sovereign modules "in future ... interfaces yet to be built". NOT this
task. #250 builds the interface they would fold into.

CARRIES THE RETENTION POLICY: dev/storage/telemetry still holds 54,364 files / 250MB with nothing that
deletes them, re-verified 2026-08-08, and [#253] now writes a sovereign TSV per leg on top. Retention
belongs to whoever owns cadence.

CROSS-REF: #250 (GM-3 transport), #245 (the factories), #204/#208 (TSSA), #256 T7/T9.
```

<a id="c29c1332-253"></a>

#### #253 — OBS-JOIN DONE: all four cells of the busy model, on one epoch axis, per leg — verified live

status: **completed** · blocks: #254, #255, #250

- `a4fc43b3` MATRIX-CPU: the acceptance test ran, and the CPU half answered [#255]
- `5fcc401c` OBS-PMU-WIRE: the joiner had a PMU column and nothing ever filled it [#259]
- `57d00526` OBS-JOIN: register both selftests as gates, and refresh the arch graph [#253]
- `be2ceeee` FOLD: the attribution residual was clamped, so it could never show it was noise [#253]
- `f5029bf3` OBS-JOIN: the harness runs the sovereign sampler per leg and joins before the leg is over [#253]
- `ce6c88a1` OBS-JOIN: three artefacts, one epoch axis, one file per leg [#253]
- `7fa1d2ed` OBS-JOIN: the sovereign readers get a harness consumer, and it re-points itself at each leg's client [#253]
- `c1dfce7e` OBS-JOIN: the sovereign readers can be sampled as a series, because a rate cannot be re-windowed [#253]

```
THE NORTH STAR'S CENTRAL CLAIM, AND IT NOW EXISTS. Director, 2026-08-07: "both GPU and CPU metrics,
correctly attributed, as a stream of time series data that can be directly plotted and compared against
every single lever." Three artefacts held three quarters of that and had never met in one file.

SHIPPED, four commits:

  c1dfce7e  `metrics --pid N --raw [--every S]` -- the sovereign readers emit CUMULATIVE counters as a
            TSV stream instead of a delta over a window the tool chose. NOT a loop around the windowed
            mode: that would produce a stream of RATES, and a rate cannot be re-windowed -- the mean of
            ninety one-second busy ratios is not the busy fraction of ninety seconds. It would also have
            HOLES between windows, and a hole in a busy series looks like idle time.
  7fa1d2ed  scripts/metrics-sample.py -- supervises the reader against whichever client is alive,
            re-discovering across the ~28 client restarts of a matrix run. Reads no /proc itself: every
            counter comes from dist/metrics, so the fdinfo dedup, comm-parsing and lseek/EINVAL rules
            keep ONE implementation.
  ce6c88a1  scripts/obs-join.py -- one <label>.joined.json per leg: in-process intervals, both sovereign
            readers, and the PMU on a single epoch axis.
  f5029bf3  render-profile.sh runs the sampler per leg and joins before the leg is over; lever-matrix.sh
            carries both artefacts into the run directory.

VERIFIED LIVE, 60s at Desert Town, sim running, 11 of 11 intervals joined:

    cpu.frame.work.wall_fraction   0.2756     in-process, the engine's own view of its main thread
    cpu.owner.frame.busy_cores     0.2606     sovereign, the SAME thread, a different mechanism
    cpu.owner.sim.busy_cores       0.1617
    cpu.owner.unknown.busy_cores   0.0576
    cpu.owner.gl.busy_cores        0.0097
    cpu.owner.lighting.busy_cores  0.0058
    cpu.process.busy_cores         0.4965
    gpu.engine.render.busy_ratio   0.2374     sovereign, per client, on the same axis
    gpu.engine.{compute,copy,video,video-enhance}.busy_ratio  0.0000

TWO CHECKS PASSED THAT NOTHING COULD RUN BEFORE. The engine's own wall fraction EXCEEDS the sovereign
CPU busy of the same thread -- the relation that must hold, since one counts wall time not spent
sleeping and the other counts CPU actually consumed, so the 0.015 gap is the main thread blocked or
descheduled. And the owners track cpu.process.busy_cores to within a few clock ticks in all 11
intervals: the whole shrinks to its parts.

DESIGN DECISIONS THE ARTEFACT ENCODES
  * TWO SOURCES, TWO METHODS, LABELLED. Sovereign counters are RE-DIFFERENCED (exact, re-windowable);
    the PMU is a MEAN OF RATES (neither). Both carry a `method` field, because a reader who cannot tell
    them apart will quote them as the same kind of number.
  * THE DENOMINATOR IS THE SPAN COVERED, not the nominal interval. Measured 92.8% at a 0.5s cadence;
    the naive form would have read 7.2% low on every leg, silently, worst at the edges.
  * THREE WAYS A COUNTER STOPS BEING DIFFERENCEABLE, all reported: pid changed, counter fell (an
    owner's live-thread sum drops when a thread exits), fewer than two samples. Each is a different
    fact and none is reported as 0.
  * THE TWO GPU VOCABULARIES ARE NOT MERGED. PMU says rcs0, the DRM reader says render. Putting one
    under the other's name would ASSERT the agreement metrics-mutual-check exists to TEST.

A DEFECT THE INSTRUMENT FOUND, folded as be2ceeee. cpu.unattributed.busy_ns was the residual between
the process whole and the owner parts, described as exited-thread time and CLAMPED AT ZERO -- and had
never read anything but 0.0. Eleven live intervals gave -2, +5, 0, -2, +6, -1, 0, +2, -5, 0, +2 ticks:
every value a whole 10ms tick, both signs, no thread having exited. It is per-thread tick quantisation,
and the clamp is what made that undiscoverable. Renamed cpu.attribution.residual_ns, unclamped, both
terms named, and put on the shared axis beside the parts it is the residual of.

VERIFICATION. metrics-sample 7 arms, obs-join 11 arms, EVERY arm injection-proven to fail. The injection
pass found the respawn defect rather than certifying the code: a fake reader that printed and exited was
relaunched 500 times in 0.3 seconds. Both selftests registered as gates (57d00526). run-gates.sh: 55
green, metrics_mutual SKIPPED as usual with no GPU client.

FEEDS [#254] (the plot has its file), [#255] (the matrix now fills all four cells), [#250] (what the
sample type must carry is known rather than guessed), [#256] (three TSSA deltas: T10 method, T11
measured denominator, T12 signed residual).
```

<a id="c29c1332-254"></a>

#### #254 — OBS-PLOT DONE: the busy model renders, artifact 14904c73, and the plot cannot draw across a hole (46a4c792)

status: **completed** · blocked by: #253 · blocks: #255

- `991b7940` OBS-PLOT: the matrix draws its own busy model at the end, so 28 legs read against each other [#254]
- `46a4c792` OBS-PLOT: the last clause of the north star — CPU and GPU on one axis, and a plot that cannot lie [#254]

```
UNBLOCKED 2026-08-08: [#253] closed and produced exactly the input this was waiting for. The JOINED
artefact now exists -- harness/profiles/obs-join-verify.joined.json, 11 intervals, both sovereign
readers (cpu.owner.*.busy_cores, gpu.engine.*.busy_ratio) and the in-process series on one epoch axis
-- and render-profile.sh:422 produces one per leg. Nothing still draws it.

"Directly plotted" was in the Director's statement of the goal and is the clause that makes the rest
legible -- a per-interval stream nobody looks at is a file, not an instrument.

THIS IS A RENDERER OVER AN EXISTING FILE, NOT A PIPELINE. Four of the design rules below are already
implemented in scripts/obs-join.py and must NOT be re-derived: re-differencing (rediff(), :112), the
covered-span denominator (coveredS, :24-28), the one-core-per-owner normalisation (DERIVED, :63-65),
and the counter-vs-gauge distinction (COUNTER_SUFFIX, :57). The joined artefact carries a per-interval
`axis` dict holding exactly the dimensionless series a plot wants. Read that; do not recompute it.

WHAT IS NOT IN THE FILE YET: the PMU. obs-join.py takes --pmu and render-profile.sh never passes it, so
every per-leg artefact records `pmu.available: false`. That is [#259], and it should land first or
alongside -- a plot that silently omits device-wide GPU busy while showing per-client GPU busy is
exactly the "most convincing wrong picture" this task's own last line warns about.

THE DESIGN IS ALREADY SETTLED, from the exchange on 2026-08-07:
  * SHARED X AXIS = the instant. Both series are cumulative counters differenced onto a common grid,
    which is the join, not the arithmetic.
  * SHARED Y AXIS IS FINE for CPU and GPU together: both are dimensionless utilisation ratios. The
    denominator objection only ever applied to SUMMING them.
  * THE CPU DENOMINATOR IS ONE CORE, per owner -- because GPU engine busy is also "one engine", so
    both curves answer the same question about different hardware. Normalising to all 16 cores is
    technically correct system utilisation and squashes every pattern against the floor.
  * RESAMPLE BY RE-DIFFERENCING RAW CUMULATIVE COUNTERS, never by interpolating a rate. Interpolating
    invents work the hardware never did -- the rule busyDelta already encodes for backwards counters.
  * MetricBoundedness decides whether a point may exist at all: a level and a rate are the same line,
    and differencing a HighWaterMark yields a smooth, plausible, meaningless series.
  * The two sources may keep their NATIVE rates -- in-process ~4.85s, sovereign 0.5s as shipped --
    plotted on one axis rather than the fast one downsampled to the slow. The out-of-process reader can
    sample fast precisely because it does not perturb, which is the module boundary paying for itself.
  * PLOT THE RESIDUAL TOO. cpu.attribution.residual_cores is on the axis and is SIGNED. A whole that
    cannot be seen failing to match its parts is a whole nobody can falsify, and on the measured leg it
    swings -0.0125 to +0.0150 -- which is what makes it visibly noise rather than a leak.

WHAT IT SHOULD MAKE VISIBLE, now measured twice. The regime finding: the frame is PINNED while neither
resource is saturated. Desert Town, 60s, [#253]: ~62 fps mean, main thread 0.2606 cores, sim 0.1617,
process 0.4965, GPU render engine 0.2374 -- and CPU work swinging 3,505 -> 11,722 us/frame p50 to p99
inside one leg. That is the finding the campaign has been inferring from separately-measured numbers in
separate documents; on one axis it stops needing to be argued.

THERE IS ALREADY A CHART IDIOM IN THE REPO: scripts/arch-graph.py emits mermaid xychart-beta (:965,
:1118). Following it costs nothing and keeps one charting vocabulary.

Published as an artefact so the Director can read it without reading a log.
```

<a id="c29c1332-255"></a>

#### #255 — MATRIX-CPU DONE: 27 legs, all four cells, and the caches trade CPU for GPU at identical frame rate (a4fc43b3)

status: **completed** · blocked by: #253, #254, #259 · blocks: #252

- `35e049a1` [#268] the correction argument was below the noise floor: the MDE decides, and a screen is not a finding
- `a4fc43b3` MATRIX-CPU: the acceptance test ran, and the CPU half answered [#255]

```
NOT A LEVER HUNT. Director, 2026-08-07: "does our new sovereign observability system give us
measurement and insights into the wall, busy, work of CPU and GPU and with attribution that we've
never previously had?" This run answers that question; any lever finding is a by-product, and the
Director has ruled that no new render/engine optimisation is chased off the back of it yet.

WHY A MATRIX AND NOT ANOTHER SINGLE LEG. Everything built in [#235] and [#253] has been validated
against ONE leg. Every defect this campaign has found surfaced the moment something ran at scale -- the
mis-aligned PMU across 27 legs, the 4x fdinfo error, the 504-tick thread-sum gap. 28 legs across
warps, load phases and lever toggles is a far harder test of the series retention, the stamps, the
join and the CPU attribution than anything constructible by hand.

WHAT IT PRODUCES THAT HAS NEVER EXISTED
  * per-lever CPU WORK, against a whole that can now shrink -- impossible before C07, because the
    denominator was a pacing period holding 11,298us of sleep and the sleep was one of its parts
  * per-lever CPU BUSY per owner, from outside the process
  * per-lever GPU busy (re-confirms the existing table through the same instrument)
  * a per-interval series per leg, on one axis with the above
  * the first test AT SCALE of the attribution residual. REWRITTEN 2026-08-08: this bullet used to read
    "the first real test of cpu.unattributed.busy_ns, which has only ever read 0.0". Both halves are
    now dead. The key is cpu.attribution.residual_ns (renamed and UNCLAMPED at be2ceeee, [#253]), and
    it has already read non-zero on both signs -- 11 live intervals gave -2,+5,0,-2,+6,-1,0,+2,-5,0,+2
    clock ticks, which [#253] established is per-thread tick quantisation, not exited-thread time. So
    the question the matrix answers is sharper than the old one: across 28 legs that each cross a LOAD
    PHASE -- the one condition under which threads really do exit and take their cost out of the live
    sum -- does an exited-thread term separate from the quantisation floor, or does the residual stay
    inside +/- a few ticks throughout? A residual that never leaves the tick band would mean the
    exited-thread term is real but negligible at these scenes, which is itself a result.

BLOCKED BY #254 (the plot) and #259 (the PMU wiring). [#253] is DONE, and its per-leg wiring is in the
tree and gate-checked: render-profile.sh:184 starts the sovereign sampler at 0.5s per leg, :422 joins
before the leg ends, lever-matrix.sh:592-593 carries <label>.joined.json and <label>.sovereign.tsv into
the run directory. Running before the plot gives numbers nobody can read against each other; running
before #259 banks 28 legs whose PMU column is empty, which is the two-artefacts-that-never-meet shape
[#253] existed to end, surviving one level up.

RUN IT BEFORE THE REFACTOR, NOT AFTER. #252 and #250 move and re-home this machinery. A completed
matrix is the regression baseline those changes get compared against -- the same discipline as every
byte-identical extraction on this fork. Refactoring first means having nothing to compare to.

GL_TIME_ELAPSED VALIDITY (#251) IS NO LONGER A SEQUENCING BLOCKER, and the reason is a [#253] result.
The banked per-leg profiles contain per-pass gpu_us, which are GL_TIME_ELAPSED and therefore carry
whatever #251 turns out to find. That used to argue for running #251 first. It no longer does: the
joined artefact now carries SOVEREIGN GPU busy (fdinfo, per client) on the same epoch axis as those
per-pass timers, so a disjoint problem would show up as a divergence between two independent
instruments IN THE BANKED DATA, after the fact. Bank the matrix; let the cross-check decide whether
#251 is urgent. Measuring beats sequencing defensively.

EXPECTATION, SET DELIBERATELY: the most likely headline is that no lever moves CPU meaningfully at
these scenes either, given the frame is pinned at ~62 fps with ~28% of one core on the main thread and
~24% GPU engine busy (measured at Desert Town, [#253]). A MEASURED null on CPU levers is a result this
campaign has never been able to produce, and it is what would justify moving the target rather than
the levers.

Cost: ~47 minutes unattended for one scene at 3 repeats, per matrix-20260807-160513. The 28-leg shape
checks out against scripts/lever-table.json: 8 levers -> 3 repeats x (baseline + 8 off) + warmup = 28.
```

<a id="c29c1332-256"></a>

#### #256 — TSSA-OBS DONE: `telemetry` is a component, and teaching the sweep to SEE it found three defects (c19cb959)

status: **completed**

- `574879f3` [#256] the Reporter was in the file map and out of the contents cell, both green
- `c19cb959` [#256] telemetry becomes a component, and the sweep that reads it finds three defects
- `94f44d59` TSSA-OBS pass 1: the gate that could not see the component, and three positions the campaign paid for [#256]

```
DIRECTOR, 2026-08-07: the TSSA is a WIP and it is ours to shape toward our own north star. Metrics and
observability are now deeper-explored than any other area of the fork, so this campaign FEEDS the
document rather than only conforming to it. Findings are appended as each board item completes, while
the reasoning is still attached to the thing that produced it.

=== SETTLED IN THE FIRST PASS (94f44d59) ===

T2 DONE. The `metrics` register row named MetricSample, ClientBusyReader, EngineBusyReader. A new
    bidirectional check (scripts/register-artefacts.py) found FOUR unnamed types, not the one manual
    reading caught: BusyReading, EngineBusyWindow, EngineOpenResult, ThreadBusyReader -- two of which
    predated this campaign entirely. Row rewritten; check green at 7 named / 7 declared / 0 unnamed.
    The instrument matters more than the row: nothing read the register's artefact names, which is why
    it went stale, and the BACKWARD direction (declared but unnamed) is the one that caught it.

T3/T4/T6 DONE. Three rows added to the observability requires/forbids table, each with its warrant
    written beside it because each was bought with a shipped defect: a metric must state its clock; a
    whole must shrink when its parts do; an attribution must state its granularity.

GATE-BLINDNESS DONE, and it was the sharpest find of that pass. grant-sweep.py's PASSTHROUGH list
    omitted `metrics`, so the ONE instrument connecting the grant table to the tree reported the
    component UNVERIFIABLE while eight of its files sat in the tree. See T14 below: this is now known
    to be one of FOUR instances of the same shape -- FIVE as of the close below.

=== FROM [#253], THE JOIN -- THREE DELTAS, ALL OF THE THIRD KIND ===

None is the tree violating a stated position, and none is the document lagging the tree. All three are
things the campaign LEARNED that the document does not say, which is the kind nothing goes red for.

T10. A METRIC MUST STATE ITS METHOD, NOT ONLY ITS CLOCK. T3 bought "a metric that states which clock
    it takes". The join needed a SECOND declaration the vocabulary has nowhere for: how the number was
    DERIVED. The sovereign counters are re-differenced -- exact, and re-windowable over any interval a
    consumer later chooses. The PMU figure is a mean of sub-window rates and is neither. Same unit,
    same clock, same owner, same domain; one survives re-windowing and the other does not, and nothing
    in the descriptor distinguishes them.

T11. A RATE'S DENOMINATOR MUST BE MEASURED, NOT DECLARED. T4 bought "a whole that shrinks when its
    parts do". The join found the dual: the denominator is the span ACTUALLY COVERED, never the
    nominal interval. At a 0.5s cadence the real coverage was 92.8%, so the naive form reads 7.2% low,
    silently, and worst at the edges where a reader is most likely to be drawing a conclusion.

T12. A DISCREPANCY MUST KEEP ITS SIGN. A REFINEMENT of T4 rather than a new subject.
    cpu.unattributed.busy_ns was clamped at zero and had therefore never read anything but 0.0 in its
    life -- a residual that can only look like a leak cannot report the OTHER failure, which is
    double-counting. Unclamped and renamed cpu.attribution.residual_ns, it reads +-a few 10ms ticks in
    both directions, which is per-thread tick quantisation and not a leak at all. A clamped residual
    is an instrument that has been told which answer to give.

T13. A RATCHET MUST COUNT A UNIT ONLY THE THING IT NAMES CAN MOVE. dedup_measure ratcheted the number
    of SYMBOLS in the shared closure, and adding one log line moved it 18 with no new coupling at all
    -- it was measuring the compiler's template instantiation, not the architecture. Re-based on
    FILES, which only a genuinely new participant can change. Closed by [#257].

T14. AN INSTRUMENT NEEDS A VERDICT FOR "DID NOT RUN", OR IT CANNOT BE DEPLOYED WHERE IT MATTERS. The
    fourth instance of the gate-blindness shape above, and the one that generalises it: a gate whose
    absent verdict is spelled the same as its passing verdict is worse than no gate, because it
    reports green from a machine that never had the hardware. Exit 77 = SKIP throughout, and
    run-gates now REFUSES to call a skipped gate green. Closed by [#258].

=== TWO OPEN CONFLICTS, PUT TO THE DIRECTOR 2026-08-08, DECIDED, AND NOW CLOSED ===

T8. THE FORBID COLUMN FORBIDS WHAT WE BUILT. The observability table pairs "the instrument's grants a
    subset of its subject's" with a forbid on "an instrument that must be LINKED INTO its subject to
    see it". Every TelemetryTimer in the fork is exactly that. Resolved below.

T9. THE OWNERSHIP RULE HAS NO LAWFUL HOME FOR HALF THE SYSTEM. The table requires "every declared
    metric has an owner", and forbids the ambient global -- while Telemetry IS an ambient global that
    every component reaches for by name. Resolved below.

=== NOT YET SETTLED, CARRIED FORWARD ===

T5. The sovereign boundary buys sampling freedom, not only correctness -- the out-of-process reader can
    poll fast BECAUSE it does not perturb. The perturbation difference is still unmeasured, and writing
    it now would be a claim with no instrument. [#253] and [#259] narrow the experiment: TWO samplers
    now run against a live client for whole legs (procfs at 2 Hz, i915 PMU continuously), so the
    experiment is a leg with and without them, and the harness to run it exists.

T7. The consumer half -- Python that belongs to none of the 54 components -- waits on [#252]. The
    precondition is MET: the join has been performed AND rendered, so what the consumer must carry is
    known rather than guessed. It is FIVE things, none of them a property of a file format:
      (a) the counter-vs-gauge distinction (`_total` marks what may be differenced);
      (b) THREE distinct unavailabilities -- pid changed, counter fell, fewer than two samples --
          because each means something different about what happened;
      (c) the covered-span denominator (T11);
      (d) the method declaration (T10);
      (e) SEQUENCE POSITION, added by [#254]. A plot must BREAK its line where a series has no value;
          drawing across a hole invents a measurement and looks exactly like a real one. To break it,
          the renderer must know two surviving points are not ADJACENT, and a timestamp cannot tell it
          that -- intervals vary in length, so "further apart than usual" is a guess. The interval
          INDEX travels with every point and the pen lifts where it jumps.
    All five are properties of a SAMPLE TYPE, which is what [#250] is for.

T1. The document's side of the ambient-global violation (the `metrics` owes facet) waits until [#252]
    has a shape.

=== CLOSED 2026-08-08 by c19cb959: T8 AND T9 RESOLVED, AND THE SWEEP THAT READS THEM ===

T8 RESOLVED as the Director directed: the forbid retargeted from "an instrument that must be LINKED
    INTO its subject to see it" to "an instrument that can only READ its subject from inside it". As
    first written it outlawed the row above it -- a TelemetryTimer inside `rendering` IS linked into
    its subject by construction, and recording from inside is the design. Being able to READ only from
    inside is the defect. The requires half was always right and is untouched.

T9 RESOLVED as (c)+(d), both halves: the register SPLIT so `telemetry` is its own row (KIND LIBRARY,
    ZONE MACHINE, duty "the handle a subject records through"), AND the ambient-global forbid
    retargeted at LAZY REGISTRATION. Law of One refused the first duty I wrote ("the declared
    instrument: a handle, and the registry that holds it") -- the registry moved to contents.

THE DECISIVE WORK WAS MECHANICAL, AND IT IS THE GATE-BLINDNESS SHAPE FOR THE FIFTH TIME. `grant-sweep`
    mapped files by DIRECTORY, so the four StarTelemetry* files in `source/core/` were invisible and
    the new component read UNVERIFIABLE -- "not in the tree and not declared future". This is the
    first instance that adding a directory would NOT have fixed. CARVED_OUT is now keyed by directory
    with `core` as its second key, and the moment the sweep could SEE the component it reported four
    MISSING grants:

      * `gpu` is a CONTRACT and included ALL of StarTelemetry.hpp to name ONE type, `MetricDesc`,
        which lives in `core`. Narrowed to StarMetricDesc.hpp. Found as CONTRACT_GRANT -- an INTERFACE
        naming a LIBRARY -- by prose_claims, not by anyone reading the file.
      * `windowing`'s 57 telemetry crossings ALL ran through that single over-broad include. None
        survive it. Grant retracted, not granted.
      * `metrics` may not name `telemetry`, the exclusion the register calls load-bearing. It was
        enforced by NOTHING: while those files mapped to `core`, which `metrics` IS granted, the
        sweep could not have seen the violation.
      * `base`, `host_sdl` and `server` are legitimate recorders and now carry the grant.

T15. AN UNGATED FIGURE BESIDE A GATED ONE IS THE ONE THAT ROTS. Seventeen prose figures were wrong and
    only six were gated. Every entrypoint closure moved by one; R4's "27 of the 41 components are not
    yet their own directory" stood while the register grew to 54 and the set to 38, in a sentence that
    NAMES `grant_sweep` as its source; BACKEND was described as "no modal zone at all: six MACHINE to
    six DEVICE" against a measured 9/6/2; world_sim's closure list NAMED THIRTEEN while claiming
    fifteen. Two verdicts added -- check_backend_spread (reads all three counts AND re-derives which
    KIND has the weakest modal share, so naming the wrong KIND cannot pass with three right numbers)
    and check_unverifiable (reads grant_sweep.unverifiable_components(), newly exposed for it). Four
    injection drives, all firing; 23 total.

T16. A RATCHET RE-BASELINED BY AN ATTRIBUTION CHANGE MUST SHOW ITS NULL CONTROL. dedup_ratchet 73 ->
    74. One file left `core` (in ALLOWED_SHARED) for `telemetry` (not). Control taken off the report
    itself: telemetry contributes exactly 1 file, 74 - 1 = 73, the same objects, the previous number.
    `telemetry` deliberately NOT added to ALLOWED_SHARED -- that holds the number at 73 by EXEMPTING
    the component instead of counting it, and a component whose own `owes` facet says it still has to
    leave `core` should stay visible to the ratchet.

T17. AN INSTRUMENT NARROWER THAN THE FALSIFIER IT ANSWERS MUST SAY SO. The `falsified` facet claimed
    "three ways, and two are already instrumented" while naming ONE. Each now names its own, including
    `clock_ratchet`, which counts undeclared CLOCKS only -- a descriptor-free counter or gauge is
    uncounted. Filed as #266.

VERIFIED: 58 gates green; metrics_mutual SKIPPED (needs a live GPU client, not this machine); build
clean, 0 errors.

NOT A REVIEW TASK, AND IT STAYS THAT WAY. T8/T9 are closed but T1/T5/T7 are not, and the arc is not
over. Re-open with one entry per completed item as [#250]/[#252] land.
```

<a id="c29c1332-257"></a>

#### #257 — DEDUP-RATCHET DONE: gated on FILES not symbols, and the gate set can now see it — 56 green (8ef7e573)

status: **completed**

- `8ef7e573` DEDUP-RATCHET: the ratchet was counting a quantity that moves when you add a log line [#257]

```
FOUND BY THE #253 BOARD AUDIT, 2026-08-08. Two defects, and the second is the one that matters.

DEFECT 1 -- THE RATCHET IS BREACHED.
    python3 scripts/dedup-measure.py --check   ->  exit 1
    "LEAK = 614 symbols outside {base, core, host, platform, presentation, scene}"
    "FAIL -- leak 614 exceeds ceiling 596"
CEILING = 596 is declared at scripts/dedup-measure.py:79. The measurement is NOT stale: the script's own
staleness guard (:194-199) did not fire, and only one build tree holds objects
(build/linux-release-clang, 584 .o, mtime 2026-08-08 11:53, i.e. this session's build).

DEFECT 2 -- THE RUNNER CANNOT SEE IT, AND THAT IS THE REAL FINDING.
    grep -c "dedup-measure" .github/workflows/gates.yml  ->  0
scripts/ci/run-gates.sh derives its entire set by parsing gates.yml. So dedup_measure is in NO gate
run, and "55 green" is a tally taken over a set that excludes a red ratchet. The standing rule on this
fork is "scripts/ci/run-gates.sh green IS the gate"; a ratchet that requires a BUILD falls through that
rule silently, because the runner was designed around checks that need only the source tree.

That is the same defect class as GATE-SKIP-1 (dc722dee) and as grant-sweep's PASSTHROUGH blindness
(#256): AN INSTRUMENT THAT CANNOT SEE A COMPONENT IS INDISTINGUISHABLE FROM ONE THAT AGREES WITH IT.
Third instance.

WHAT THIS TASK OWES
  1. Decide the breach: pay the 18 symbols down, or explain what grew and re-record the ceiling WITH A
     REASON. A ceiling raised without a warrant is not a ratchet.
  2. Establish WHEN it crossed 596. Requires rebuilds at older commits, which is expensive under the
     E-core-pinned build rule -- so bound it by bisecting on the commits that touched the six zones
     rather than by rebuilding everything.
  3. Decide how a BUILD-REQUIRING gate joins the run-gates set. Options: a second declared list in
     gates.yml gated on the build job; a `--with-build` flag on run-gates.sh that runs them and reports
     them separately; or an explicit NOT-COVERED section in run-gates.sh's own output so the hole is
     printed rather than absent. The third is the cheapest and matches the exit-77 precedent: an absent
     verdict must be spelled differently from a passing one.

NOT to be confused with metrics_mutual, which is honestly reported as SKIP by run-gates.sh. This one is
not reported at all.
```

<a id="c29c1332-258"></a>

#### #258 — GATE-SKIP-2 DONE: pr570_ledger exits 77, and run-gates --selftest now reads the rule out of gates.yml (7e9815a4)

status: **completed**

- `8ef7e573` DEDUP-RATCHET: the ratchet was counting a quantity that moves when you add a log line [#257]
- `7e9815a4` GATE-SKIP-2: pr570_ledger exits 77, and the rule is now read out of the file rather than remembered [#258]

```
FOUND BY THE #253 BOARD AUDIT, 2026-08-08. A one-line fix, filed because the sweep that fixed its twin
walked past it.

.github/workflows/gates.yml:314-320
    - name: pr570_ledger -- the findings ledger matches its inputs and its decisions
      run: |
        if [ -d /root/analysis/osb-pr570/raw ]; then
          python3 scripts/pr570-ledger.py --check
        else
          echo "pr570-ledger: SKIPPED -- raw outputs not present on this machine"
        fi

The else-branch echoes and EXITS 0. On any machine without /root/analysis/osb-pr570/raw -- which is
every CI runner and every fresh clone -- run-gates.sh reads that 0 as OK and counts a check that
compared nothing toward the green tally.

THIS IS THE DEFECT dc722dee ALREADY FIXED, FOR metrics_mutual, IN THIS FILE. Compare gates.yml:406-411:
        else
          # 77, not a fall-through. This else-branch used to end here and exit 0, so run-gates.sh
          # printed OK and counted a comparison that never happened toward the green tally.
          exit 77
        fi
dc722dee's own message says "THE HOLE WAS IN THREE PLACES". It was in four. The pr570_ledger step was
already in the file when that commit landed.

THE FIX: `exit 77` in the else branch. One line.

THE FIX THAT MATTERS MORE: a check that every guarded step in gates.yml ends its did-not-run path in 77
rather than 0 -- because this is now the second time the same shape has been found by reading rather
than by an instrument, and reading is not a mechanism. Cheapest form: a run-gates.sh selftest arm that
greps gates.yml for `else` branches inside `run: |` blocks and asserts each reaches an `exit 77`. That
arm must itself be proven to fire.

NOTE this does NOT change the current local tally: /root/analysis/osb-pr570/raw IS present on this
host, so pr570_ledger really ran and really passed (155 rows, 0 open) in the 55-green run at 849f97d2.
The defect is about every machine where it does not.
```

<a id="c29c1332-259"></a>

#### #259 — OBS-PMU-WIRE DONE: all three sources on one axis per leg — and the two GPU instruments now cross-check every interval (5fcc401c)

status: **completed** · blocks: #255

- `5fcc401c` OBS-PMU-WIRE: the joiner had a PMU column and nothing ever filled it [#259]

```
A GAP IN #253's OWN WORK, found by the audit's adversarial pass and confirmed by hand.

scripts/obs-join.py takes --pmu and joins the i915 engine series onto the same epoch axis as everything
else. scripts/render-profile.sh:422 never passes it:

    scripts/obs-join.py "harness/profiles/$LABEL.series.json" --sovereign "$SOVEREIGN_SERIES" || true

So every per-leg artefact records, on every interval:
    "pmu": {"available": false, "reason": "no PMU series supplied"}
Confirmed against the live capture: harness/profiles/obs-join-verify.joined.json has sources.pmu = null.

WHY IT HAPPENED, AND WHY IT IS NOT SIMPLY A MISSING FLAG. The two samplers live at different levels ON
PURPOSE. The sovereign readers are procfs reads with no warmup, so render-profile.sh starts one PER LEG.
The i915 PMU pays a warmup to open its counter and is therefore started ONCE PER RUN by
lever-matrix.sh:523-526, and joined to legs afterwards by pmu-join.py using the window stamps. A
standalone render-profile.sh run genuinely has no PMU series to pass -- there is no sampler above it.

So the fix is at the matrix level, not in render-profile.sh: after the run, re-join each leg with
--pmu "$PMU_SERIES". obs-join.py already resolves each interval against the PMU rows by epoch, so this
is a second invocation, not new arithmetic.

CORRECTION OWED TO #253's OWN RECORD. Its closing text says "ONE joined artefact per leg: in-process
intervals, both sovereign readers, and the PMU on a single epoch axis." The first two are true and
verified; the third describes the SCRIPT's capability, not the artefact the harness produces. The
commit ce6c88a1 has the same overstatement. Both should be read as: the joiner supports three sources,
the harness currently fills two.

CONSEQUENCE FOR #255. A matrix run today would bank per-leg artefacts whose PMU column is empty, and
the GPU engine busy would live only in the run-level pmu-join.py report -- i.e. exactly the two-artefacts
-that-never-meet shape #253 existed to end, surviving one level up. Worth closing BEFORE the matrix, and
it is small.

BLOCKS #255 in the sense that running the matrix first means re-joining afterwards from retained files
(possible -- the raw TSVs are retained per leg and at run level -- but it means the banked artefact is
not the one anybody reads).
```

<a id="c29c1332-260"></a>

#### #260 — TSSA-PLATFORM-DISSOLVE: a ratified five-stage thread, stages 3/4/5 not started, on no board task

status: **pending** · blocked by: #261 · metadata: `{"rank": 12}`

```
FOUND BY THE #253 BOARD AUDIT's completeness critic, 2026-08-08, and verified: the word `ugc` appears
ZERO times in the entire 454KB board.

docs/superpowers/HANDOVER.md §3 carries a five-stage plan for dissolving the `platform` component --
Director-ratified, with stages 1 and 2 shipped (60a66f02 "P2P networking is a TRANSPORT -- stage 1 of
dissolving `platform`"). Stages 3, 4 and 5 are marked "not started":

    stage 3   ugc
    stage 4   desktop, plus the platform_pc -> vendor rename
    stage 5   delete platform / platform_null

None of them exists as a board task. #204's own block lists its sub-tasks exhaustively -- "#205 spine,
#207 levelling pass, #208 aggregate review" -- and there is no fourth. #208 scopes register fields, the
completeness stopping rule, the aggregate review and gate re-homing; not the dissolution.

WHY THIS IS THE SHARP KIND OF GAP. The board exists so that a `[#NNN]` in a commit resolves and so that
nothing ratified is carried only in someone's head. A half-finished, Director-ratified architecture
thread whose ONLY pointer is a point-in-time handover note is the failure the board was built to
prevent -- and that note is now 167 commits stale (see the sibling task on HANDOVER.md).

WHAT TO FILE. Either one task or three, each pointing at HANDOVER §3.2's eight-item checklist as the
template, and each ranked relative to #208. Three is probably right: the stages have different sizes and
stage 4 carries a rename that touches the register, the grant table and every gate that names
platform_pc.

DO NOT START WITHOUT reading HANDOVER §3 first -- and refresh that document before trusting it, because
every figure in its §2 state table currently reads wrong at HEAD.
```

<a id="c29c1332-261"></a>

#### #261 — HANDOVER-STALE: #204's resume document is 167 commits behind and every §2 figure reads wrong

status: **pending** · blocks: #260 · metadata: `{"rank": 10}`

```
FOUND BY THE #253 BOARD AUDIT, 2026-08-08.

docs/superpowers/HANDOVER.md is the document the #204 memory explicitly says to READ FIRST -- it carries
the live status of the TSSA and names the commit it describes so staleness is detectable. It names
21ba119a. `git rev-list --count 21ba119a..HEAD` = 167.

Every headline figure in its §2 state table has rotted:

    §2 says                      HEAD says
    gates      21 of 21 green    56 declared steps; run-gates.sh: 55 green, 1 SKIPPED (metrics_mutual)
    components 52                53          (scripts/spec-model.py)
    grant rows 50                51          (scripts/spec-model.py)
    elements   24                24          (unchanged)
    open owes  14 of 52          15 of 53    (generated ledger, TSSA line 6440)

and §6's named component list omits `metrics` entirely -- the component this whole campaign has been
building.

WHY IT MATTERS MORE THAN A NORMAL DOC DRIFT. This file is the designated entry point for a fresh
session on #204. A resume document whose numbers are wrong does not merely mislead; it teaches the
reader that the numbers are decorative, which is how the register came to name two readers when the
tree had three (#256 T2).

IT IS ALSO THE ONLY WRITTEN HOME of the ratified platform-dissolution plan (see the sibling task), so
it cannot simply be deleted.

THE FIX IS A CHOICE, and it is the same one the board itself already made:
  (a) GENERATE the §2 figures, as docs/board.md and system-boundaries.md already do, and gate them --
      then staleness is impossible rather than merely detectable; or
  (b) stamp it SUPERSEDED and move the live status into the board, leaving HANDOVER as narrative only.
(a) is more work and is the answer consistent with every other generated-number decision on this fork
(#179, #209, arch_graph_fresh). A figure a human retypes is a figure that will be wrong.
```

<a id="c29c1332-262"></a>

#### #262 — BOARD-TAIL: apply the 2026-08-08 audit's pointer corrections to 21 dormant tasks

status: **pending** · metadata: `{"rank": 15}`

```
RECORDED, NOT YET APPLIED -- said plainly so the gap is a decision rather than a discovery.

The 2026-08-08 board audit (15 read-only agents + adversarial verification, full record at
docs/board-audit-2026-08-08.md) checked all 37 open/active tasks against the tree. The LIVE
observability cluster was corrected in place the same day: #250, #252, #245, #254, #255, and five
tasks were closed (#200, #201, #202, #207, #219). Five new defects were filed (#257-#261).

WHAT REMAINS UNAPPLIED is the pointer rot in the DORMANT clusters -- render, sim-levers, and the
older arch tasks. Every one of these was verified as STILL GENUINELY OPEN; what has rotted is the
file:line references a reader would follow. Leaving them is safe for now precisely because the
Director has ruled that no new render/engine optimisation is chased yet, so nobody is following
these pointers this week. It is NOT safe indefinitely: a task whose pointers are dead reads as a
task nobody can start.

TASKS WITH CONFIRMED ROTTED POINTERS (corrections in the audit doc, one section each):
  #135  parallax refresh state    hpp:99-119  -> hpp:191-221 ; cpp:266-320 -> cpp:483-534
        (cpp:266-320 is now the ENV cache's drift/motion term from #177 -- a DIFFERENT gate)
  #136  frameBufferGeneration is in StarRenderer.hpp:194, not StarGlRenderSurface.hpp
  #138  both sequencing prerequisites SHIPPED; only the two feasibility spikes remain
  #161  both counter line references wrong -- the ones the task calls "cheap because both
        counters already exist"
  #169  floatToHalf is StarWorldClient.cpp:65 not :32; "CMakeLists.txt:295-296" names a file that
        does not exist (there is no repo-root CMakeLists.txt)
  #191  WorldRenderData has 23 members, not 35 -- the slice is 5 of 23, and the "35 members"
        framing overstates the target by 52%
  #197  EIGHT switchEffectConfig("world") sites, not ten; none of the eight line numbers is right;
        WorldPainter is not one of them. The CENTRAL claim (nothing asserts effect-parameter state)
        is CONFIRMED at HEAD.
  #198  "ZERO TRACE" verifies literally; the one line is StarRenderer_opengl.cpp:106, not :80
  #171  every line reference dead; renumbered in the audit doc. The OBJECTION is unchanged (all six
        costs are (Frame, Cpu), one pair, one whole) but its stated MECHANICS are stale: the owner
        table is now keyed by (owner, domain), and MetricDesc.whole exists and is INERT.
  #66   the cited integration SHA is on no branch; the "superseded by #70" redirect points at a
        completed task
  #90   SKIP-0 flag site drifted 122 lines; the closing sentence repeats a claim #84 retracted
  #124  LEVER HUNT -- no code, but the design spec AND implementation plan both exist on disk and
        the task cites neither, so it reads as not-yet-brainstormed when only the build remains
  #75   NOT partially done -- fully open on both items; the toggle fix's same-session path has
        never been exercised, and the ProtoCacheToggle tests cannot run in CI
  #104  decision note lives at /root/kubebound/2026-06-26-fu-lua-offload-decision.md; /tmp/fulua/ is
        gone. The DEFER holds and the reconsider-gate is NOT open.
  #144  THREE items shipped (not four): viewport, shader-fallback dead code, process-trap script.
        setEffectTextureHalf is PARTIAL. Several paths moved to StarGlRenderSurface.cpp.
  #4    THE SECOND STORE's only task. THREE of four items shipped under [#160]; only the per-draw
        glTexParameteri hoist remains. Every hpp:/cpp: reference is dead -- the code moved to
        source/application/StarGlRenderSurface.{hpp,cpp}.
  #130  live repo, dormant a month; the work after STAGE 0 was an undocumented by-reference
        blueprint arc, not the hardening backlog the board says comes next
  #203  every deliverable shipped, but do NOT close: it left a residual defect --
        system-boundaries.md:1069-1070 still states a position §14 item 4 retracts
  #204  every headline figure rotted, including "Part I is 312 lines of placeholder" which #205
        superseded
  #208  four of five register-field bullets genuinely open; the fifth (section 15's instrument
        table rows) landed at c359fc30. Blocker #207 is now CLOSED.
  #237  correctly filed and correctly worded -- the ONLY defensible edit is to drop or re-derive
        "the fork is already ~83 commits behind"
  #251  "933-2350MHz" should read "800-2350MHz" (sysfs gt_RPn/gt_RP0 on card1). The central claim
        verifies literally: `git log --all -S DISJOINT -- source/` is EMPTY, so absent means
        never-written, not renamed.

DO NOT APPLY BLIND. Each correction in the audit doc survived an adversarial refutation pass, but
twelve of the audit's original thirty-two findings were REFUTED and several of those refutations
themselves carried corrections. Read the refutation column before editing a task, not just the
finding.
```

<a id="c29c1332-263"></a>

#### #263 — BOARD-CITE CLOSED: it was 47 phantom rows on 14 tasks, not two, and the board had no fixed point

status: **completed** · metadata: `{"rank": 14}`

- `5d231c48` [#263] a board-only commit can no longer be cited as evidence, whatever its message says

```
A board-only commit that merely discusses a task was being cited as EVIDENCE for it.

=== CLOSED 2026-08-15 at 5d231c48 ===

THE CONVENTION ALREADY EXISTED and was already written down at board-export.py:73-76: a commit that
only regenerates the board carries NO stamp, and refers to tasks in prose instead. Nothing enforced
it. I violated it myself while closing #245, and the very next regeneration surfaced it — the board
grew an evidence row citing a commit whose entire content was the previous regeneration.

IT IS A LOOP, and that is what forced a structural fix rather than another correction. A stamped
board commit produces a new evidence row, which dirties the board, which needs another commit, which
produces another row. The board could never reach a fixed point.

THE SCALE WAS 23x WHAT THIS TASK RECORDED. This task said "twice in two commits". Measured across
all branches: 47 phantom rows on 14 tasks — 11% of the board's 440 evidence rows. #204 alone
credited THIRTY commits containing no work. #207 six, #177/#179/#242/#244 two each.

THE FIX: commit_index() skips any commit touching nothing but BOARD_ARTEFACTS
({docs/board.md, docs/board.html}) — a closed literal rather than a docs/ prefix, because a real
finding written into docs/ must stay citable. The citation is now impossible regardless of what the
message says.

CAUGHT BY THE NULL CONTROL, NOT BY REVIEW: the first version put the record separator at the END of
the git format string, but --name-only prints paths AFTER the format, so they fell into the next
record and corrupted its sha. Diffing the evidence index before-vs-after reported 437 of 440 rows
changed — nothing like "drop the board-only ones". Separator moved to the record start; corrected
control reports 47 removed, 0 added, every one verified to touch only docs/board.md.

Idempotence proven: two consecutive regenerations produce an identical md5.
```

<a id="c29c1332-264"></a>

#### #264 — LEVER-CPU-WEIGHTED CLOSED: the fix is correct, was implemented and measured, and changes nothing — the real constraint is elsewhere

status: **completed** · metadata: `{"rank": 4}`

```
"The per-lever estimator is an unweighted mean of interval rates, and the whole model says not to be."

=== CLOSED 2026-08-15. MEASURED RATHER THAN ARGUED ===

THE MECHANICAL CLAIM IS TRUE. `leg_means()` in scripts/lever-cpu.py returns `st.mean` over each
leg's per-interval axis values and `verdicts_for()` takes `st.mean` across repeats — an unweighted
mean of rates, not total/total. The intervals are genuinely unequal in the weight that matters:
`durationS` is near-constant (CV 0.0010) but the actual rate denominator `coveredS` produced by
`rediff()` spans 3.981–4.511 s (CV 0.0514, max/min 1.133). So the defect is real in principle.

IT IS IMMATERIAL IN FACT. The prescribed fix (sum busyNs / sum coveredS) was implemented over
matrix-20260808-140430 and compared: it reproduces every published floor exactly, then yields
ZERO verdict flips across all 48 lever-by-key cells. Largest movement on any published number is
cpu.process / off-parallaxRefreshInterval, -0.06665 -> -0.06807 (2.1%). The three larger relative
shifts all land on deltas already deep inside the floor — numbers never quoted.

THE BOARD'S NAMED PAYOFF FAILS OUTRIGHT. `cpu.frame.work.wall_fraction` is built by
`in_process_axis()` as `(total/1e6)/duration_s`, so its denominator is `durationS`, NOT `coveredS`,
and the prescribed fix does not apply to it. Computing its true total/total moves every lever by
<=0.00004 against a floor of 0.0793; all eight stay unresolved. The closing suggestion that the
weighted estimator is "the first thing to try" for owner `frame` is therefore refuted — the weighted
floor for frame is 0.0840 vs 0.0841 unweighted.

WHAT THE REAL CONSTRAINT IS, and this task misdiagnosed it: for all four keys the floor is set by
the BASELINE'S ACROSS-REPEAT SPREAD, never by the null control. wall_fraction baseline repeats are
0.2416/0.2914/0.3208 — a spread 27.9% of the baseline value; cpu.owner.frame is 31.6%. Weighting
intervals WITHIN a leg cannot reduce spread ACROSS legs.

NOT IMPLEMENTED, DELIBERATELY: shipping a rewrite that provably changes no verdict would add risk to
the estimator every future run depends on, for no gain. The measurement is the deliverable.

The two REAL defects this task surfaced en route are carried to the new estimator task, not dropped:
lever-cpu borrows a control declared `gpuNullControl` as its CPU floor (in this run one lever moved
cpu.process by 63% of that key's floor), and lever-matrix.sh never rotates the baseline out of
slot 1. See also [#265], which found the same estimator promoting noise to RESOLVED.
```

<a id="c29c1332-265"></a>

#### #265 — SIM-ON-RENDER CLOSED as a NULL RESULT: the three sim effects were noise, and the omnibus now refuses them mechanically

status: **completed** · metadata: `{"rank": 5}`

- `33d04e0e` [#268] the baseline now brackets each pass at both ends -- and the run's real cost is 20x what I wrote
- `45a09f6d` [#268] the CPU floor was built from a GPU control, a 3-point range, and a number that lived only in stdout
- `a686d95b` [#265] four published lever effects were noise; the omnibus test now refuses them mechanically

```
Owner `sim` moved on three render-side levers and nothing explained it.

=== CLOSED 2026-08-15 at a686d95b. IT IS NOISE, AND FOUR NUMBERS ARE WITHDRAWN ===

THE ARITHMETIC WAS NOT STALE — the three deltas reproduce exactly from the banked run
(off-lightingGatherCache -0.0489, off-envRefreshInterval -0.0445, off-parallaxRefreshInterval
-0.0436). What was false is "nothing explains it". The run's own data explains it as drift.

RE-DERIVED INDEPENDENTLY from the raw joined legs before anything was changed, because withdrawing
published numbers on a subagent's arithmetic would be the same defect one level up:
  * one-way ANOVA on cpu.owner.sim.busy_cores: F(8,18) = 2.18, p = 0.081 — NO between-lever structure
  * the same computation on the same 27 legs: cpu.process F = 3.77, p = 0.0092; GPU F = 1741
    — the instrument resolves real lever effects, it just does not resolve them on sim
  * ALL EIGHT levers move sim negative, mean -0.0295, including the DECLARED NULL CONTROL (-0.0168)
    and off-renderVboOrphan (-0.0174), which cannot touch the world server
  * after removing that common offset the three "resolved" levers sit at 1.14 / 0.88 / 0.83 SE
    against a pooled within-group sd of 0.0208 — indistinguishable from the control at 0.75 SE
  * a targeted Bonferroni test agrees: best corrected p on sim is 0.080

MIS-ATTRIBUTION IS DISCONFIRMED, so this is drift and not a tagging bug: the in-process timer
`tick.server.total.us`, bracketed structurally around the WorldServerThread loop body, tracks the
/proc-derived sim busy within ~4% across all nine groups. The one real render->sim coupling path in
the tree (WorldServerThread's adaptive-fidelity governor) is pinned off for this run by
`pinned: {"serverFidelity": "high"}`.

THE FIX IS MECHANICAL, NOT A BOARD NOTE. verdicts_for() now applies Fisher's protection: a key whose
between-lever ANOVA is not significant resolves NOTHING, whatever the individual deltas look like.
That withdraws FOUR numbers — the three sim effects plus cpu.owner.gl / off-parallaxRefreshInterval
(-0.0020, p = 0.168), which a Bonferroni test also rejects at 0.385. The per-lever floor is KEPT as
a second, independent hurdle. Distributions are inline (no scipy anywhere in this repo), checked
against published table values.

SURFACED, NOT DECIDED: on cpu.process the two methods disagree — omnibus p = 0.0092 passes it, but
Bonferroni puts off-envRefreshInterval at 0.198 and off-parallaxRefreshInterval at 0.055. The
corrected p now prints beside every verdict; whether to gate on it is the Director's call.

#255's headline survives — it rests on cpu.process (F = 3.77) and the GPU series (F = 1741).

RESIDUE MOVED TO A NEW TASK, not lost: declare a CPU null control instead of borrowing
`gpuNullControl`; rotate the baseline out of slot 1; persist the floor into the run manifest.
```

<a id="c29c1332-266"></a>

#### #266 — DESC-RATCHET-WIDTH: the ratchet counts MetricDesc SITES, so a descriptor-free handle is still uncounted — and a one-arg READ can create one

status: **pending** · metadata: `{"rank": 6}`

```
Found while giving `telemetry`'s `falsified` facet its instruments ([#256], c19cb959).

THE FALSIFIER: "if a handle can be created without a descriptor, a key can exist with no stated
quantity." LIVE — source/core/StarTelemetry.hpp still declares the descriptor-free overloads
(counter/gauge/timer/rate by key alone) beside the declaring ones.

=== STEP 1 (MEASURE FIRST) IS DONE, 2026-08-15. THE ANSWER IS NOT "0 OR N" ===

Grepping `Telemetry::{counter,gauge,timer,rate}(` for one-argument forms returns 12 production hits,
and reading them rather than counting them:
  * 4 are inside COMMENTS (StarRenderDiagnostics.hpp, StarRenderer_opengl.cpp x2, StarWorldPass.cpp)
  * 1 is a false positive — StarRenderer_opengl.cpp `ring.timer = Telemetry::timer(name, desc)` DOES
    pass a descriptor
  * 2 are the DEFINITIONS of the overloads themselves (StarTelemetry.cpp:270, :273)
  * 5 are genuine, all in StarClientApplication.cpp (:674, :675, :679, :680, :1810)
So descriptor-free REGISTRATIONS in production: effectively ZERO. All five genuine uses are READS of
a key another TU owns and declares — and one of them (:1810) was introduced DELIBERATELY at bc0305df
precisely because the one-argument overload asserts no descriptor, which is what makes it unable to
drift from the owner's declaration. Deleting the overloads outright would remove the SAFE form.

=== THE REAL DEFECT THE MEASUREMENT FOUND, AND IT IS AN ORDERING HAZARD ===
`Telemetry::counter(String const& key)` is `registry().getOrCreate(key, MetricType::Counter)`. It is
getOrCreate, not get. So a one-argument READ CREATES the node when the key is absent — a
descriptor-free key, produced by a reader, on a path where the owner has not registered yet. That is
ABSENT-vs-ZERO with the polarity reversed: instead of a key missing because its owner never ran, a
key PRESENT and undescribed because a reader ran first. Whether any of the five can actually lose
that race is UNVERIFIED and is the next step — do not assume it from the code shape alone.

=== WHAT TO DO, REVISED ===
  1. DONE — see above.
  2. Establish whether any of the five reads can execute before the owning registration. If none can,
     the hazard is theoretical and the right move is a comment plus a test pinning the order.
  3. If any can: the fix is a READ-ONLY accessor that does NOT create (returning an absent handle),
     not a wider ratchet. Structural impossibility over vigilance, as with R14/R15/R16.
  4. The ratchet itself is now `desc_facet_ratchet` (renamed from clock_ratchet at a01c4a9c; it
     counts sites under-declaring clock OR unit, ceiling 105, slack 0, source/test/ and empty
     `MetricDesc{}` sentinels excluded). It counts MetricDesc SITES, so a descriptor-free handle is
     still outside its scope — the narrowness this task names is unchanged by the rename.

RELATED: #245 is CLOSED and did not subsume this; it converged the descriptor, not the API shape.
```

<a id="c29c1332-267"></a>

#### #267 — A1-HANDLES: `Telemetry` is the ambient global A1 forbids — and the axiom derivation says the row is FIVE duties, not one

status: **pending**

```
RATIFIED BY THE DIRECTOR as D14 (commit 3eed5ab8): axioms A1 (Sovereign State Transparency) and A2 (Isomorphic Specification) bind. This task is the price of A1.

THE VIOLATION. `Telemetry` is a process-global registry every component writes into by string key, reached for by NAME rather than handed over at composition. A1 forbids "a process-global that some component mutates on another's behalf"; TSSA §14 convicts `ServerGlobalTimestep` in exactly those words. The defence "each subject records only about ITSELF" is refuted by our own R07-R16: nine occasions in two days where one component's registration habit changed what a READER saw. A shared registry is a shared fact.

MEASURED SCOPE: 256 references across six components (game 133, rendering 46, application 44, base 17, client 13, frontend 3).

=== THE DERIVED TARGET SHAPE (14-agent axiom derivation, wf_11f6ea68-b79) ===
Four lenses derived independently (A3 Law of One / A1 state / A2+A4 declaration / A8+P1+P2 placement), four candidate shapes were proposed, each was attacked from two angles, and one synthesis survived. NOT YET DIRECTOR-APPROVED -- this is the design input, not a mandate.

THE HEADLINE: the register is right about the DIRECTION of every arrow and wrong about the NUMBER of rows. A3 counted honestly finds FIVE concerns inside the `telemetry` row: the write cell, the vocabulary's wire spelling, the owner/budget model, the JSON projection, and artifact emission. 54 -> 58 components, two rows amended, NONE retired.

  `metric`  VOCABULARY/MACHINE -- "what one number is". MetricDesc + its eight enums AND their wire
            spellings, MetricSample, BusyReading, MetricSnapshot, the histogram layout, the owner
            tables. Named by telemetry, metrics, report, series and the nine subjects.
  `report`  LIBRARY/MACHINE -- "turns a measurement value into a run artifact". Today's
            TelemetryReporter, taking a snapshot BY VALUE. NAMES NEITHER `telemetry` NOR `metrics`.
  `series`  LIBRARY/MACHINE -- "turns measurement values into a series". Where the campaign's five
            hard-won rules land in C++ where grant_sweep can read them: re-differencing, the covered
            span as denominator, the signed residual, boundedness-respecting refusal, no
            interpolation across a hole.
  `measure` ENTRYPOINT/COMPOSITION -- metrics_main.cpp. ADD_EXECUTABLE is a placement decision and P2
            gives placement to compositions alone.

WHY `report` IS THE KEY MOVE, and why the Director's #252 ruling was right in direction and unexecutable in destination: the projection CANNOT go to `metrics` (reading a registry means naming `telemetry`, which is `metrics`'s ratified falsifier, now grant_sweep-enforced) and CANNOT stay in `telemetry` (it names six subject keys as string literals, which that row's excludes facet forbids). The destination must be a component that names NOTHING in the subject -- it takes a declared value and hands back an artifact. #252 is therefore SUBSUMED, and the contradiction put to the Director dissolves without amending either falsifier.

THE ONE THING NEEDING DIRECTOR SIGHT: the derivation amends the KIND TABLE, which governs all 54 rows rather than these four. The VOCABULARY rule reads "no implementations and no ==> edge"; it would become "no BEHAVIOUR and no ==> edge", so a total function from a declared value to its spelling is allowed. Evidence: StarThreadBusyReader.cpp:15-26 HAND-COPIES the owner names with a comment saying so, because the spelling is TU-private elsewhere. Precedent: the document already records correcting "may name only FOUNDATION" as "not a tightening, it was false". If that amendment is refused, `metric` is not a VOCABULARY and the vocabulary stays in `core`.

WHAT IT HONESTLY DOES NOT DO: A1 IS NOT SATISFIED by this shape. `Telemetry::` stays a static accessor on a process global at ~155 sites. The shape NARROWS it -- the duty gains "its OWN", and setEnabled/setDeepEnabled/reset leave the public surface so `frontend` stops mutating nine components' recording state -- but the ambient global survives and this task stays open. Anything claiming otherwise is relocating the defect.

SEQUENCE (9 steps, each landable alone; full text in the workflow output):
  1. Fix the two LIVE correspondence defects first -- (a) DONE at 574879f3, the Reporter was in the
     file map and out of the contents cell, both green; (b) `metrics` is KIND LIBRARY while
     source/metrics/CMakeLists.txt:32 is ADD_EXECUTABLE, and its row omits the `extern` it takes.
  2. Split `measure` out as the eighth ENTRYPOINT.
  3. Physically move StarTelemetry.{hpp,cpp} to source/telemetry/ -- 29 include lines. This is what
     turns P1 from a convention into a compile error for this component.
  4. Write `key_owner_sweep` REPORT-ONLY before anything structural depends on it. THIS IS THE A1
     INSTRUMENT and it does not exist -- build it before the sweep.
  5. Create `metric`; amend the VOCABULARY rule; land `vocabulary_pure`.
  6. `Json snapshot()` -> `MetricSnapshot snapshot()`, making ABSENT-vs-ZERO a property of the VALUE
     rather than a lint. SCHEMA BREAK: sequence after #242/#249 bank, not during.
  7. Create `report`; thread the writer and the enable/deep control down from entrypoints. The
     `/telemetry` verbs survive.
  8. Create `series`; descend the five rules out of Python behind a DIFFERENTIAL harness. Retire a
     Python script only when the harness agrees on real data.
  9. Close #251 before any further GPU conclusion.

STRAINS THE DERIVATION ADMITS: A8 is strained (reg_ratchet 87 / clock_ratchet 109, both slack zero) and A4 is strained on the 3,351-line Python tier, whose rules no register row admits to. Both are declared, not hidden.
```

<a id="c29c1332-268"></a>

#### #268 — LEVER-FLOOR: 1/2/3/4/5 DONE — only "raise n" remains, and it costs ~1.7h not the 8 min I first wrote

status: **pending** · blocked by: #273 · metadata: `{"rank": 14}`

- `33d04e0e` [#268] the baseline now brackets each pass at both ends -- and the run's real cost is 20x what I wrote
- `45a09f6d` [#268] the CPU floor was built from a GPU control, a 3-point range, and a number that lived only in stdout
- `35e049a1` [#268] the correction argument was below the noise floor: the MDE decides, and a screen is not a finding

```
Carries the residue of [#264] and [#265], both of which independently hit the per-lever floor
methodology in scripts/lever-cpu.py — which governs EVERY future matrix run.

=== DONE ===

5. THE MDE RULE AND THE THIRD VERDICT (35e049a1). At n=3 the MDE at 80% power is 31% of baseline on
   cpu.owner.sim and 16% on cpu.process, while the contested deltas were 0.0666 and 0.0534 — at or
   below the MDE at every correction including none. So a delta under the MDE is never quotable as a
   cost, and verdicts are CONFIRMED / CANDIDATE / NULL. Family, alpha and power DECLARED in
   lever-table.json before the run; a missing block refuses rather than defaulting.

1. NO CPU NULL CONTROL EXISTS, AND NONE CAN (45a09f6d). lever-cpu imported pmu-join's, which reads
   `gpuNullControl` — null for the GPU, worth -0.0299 cores on cpu.process, 63% of that key's floor.
   Every lever in this table IS a CPU code-path change, so no symmetric control is possible. Now
   reads `cpuNullControl`; no lever sets it; the table states why none can. Deliberately NOT done:
   picking the smallest measured delta, which is the circularity a control exists to prevent.

2. THE FLOOR IS max(baseline spread, pooled-sd LSD) (45a09f6d). The 3-point range is KEPT — it is a
   poor scale estimate but the only term that sees drift ACROSS legs. LSD added from the pooled
   within-group sd. Strictly below the MDE, so significance bar and detectability bar, not the same
   test twice.

4. THE THRESHOLDS TRAVEL WITH THE RUN (45a09f6d). lever-cpu-thresholds.json, beside the run.

3. THE BASELINE BRACKETS EACH PASS AT BOTH ENDS (33d04e0e). CORRECTION TO THIS TASK'S OWN PREMISE:
   the LEVERS already rotate (ORDER spreads them by (i + r - 1) % N_LEVERS, with a comment saying
   why); only the BASELINE was pinned to slot 0. Within-pass drift therefore landed as a systematic
   baseline-vs-levers offset — [#265]'s signature exactly. A SECOND baseline now runs at the end of
   each pass rather than rotating the first into the ring, because the head baseline is load-bearing
   before any lever runs (BASE_SCENE gates the pass, BASE_VIOLATED decides quotability,
   assert_witnesses_exist reads it). Label `-r${r}b-baseline` was CHECKED against lever-cpu's parser:
   it arrives as an extra baseline REPEAT, doubling the n of the term the floor binds on, with no
   analyser change. Tail failure is recorded and the pass continues.

NULL CONTROL FOR 1/2/4: 2 of 64 lever-by-key cells moved (cpu.process env and parallax, CANDIDATE ->
unresolved; parallax a NEAR MISS at 0.0666 vs a 0.0675 floor). Both CONFIRMED effects UNCHANGED,
which is the test that the floor improved rather than merely grew. Item 3 cannot be validated against
banked data — it changes the harness.

=== STILL OPEN: ITEM 6, AND IT IS NOW A DIRECTOR CHOICE ===

6. RAISE n. THE COST FIGURE I ORIGINALLY WROTE HERE WAS WRONG BY ~20x. "n=10 gives 15.8% (~8 min of
   legs)" counted LEGS-TIME ONLY (10 x 9 x 5s) and ignored that every leg boots a client, loads a
   world and settles it. The banked 27-leg run spans 43.7 min of wall clock: **1.7 min/leg**. With the
   tail baseline that is 10 legs per pass:

     n=3  (today)   30 legs   ~51 min    MDE ~31% of baseline on owner sim
     n=6            60 legs   ~1.7 h     MDE ~20%
     n=10          100 legs   ~2.8 h     MDE ~15.8%
     n=15          150 legs   ~4.3 h     MDE ~12.8%

   The curve flattens because the baseline's own across-repeat spread is 28-32% — item 3 attacks that
   directly, so the next run's MDE may beat these figures at the same n. THAT IS A REASON TO RUN AT
   MODERATE n FIRST and re-measure the spread, rather than buying 4 hours against an estimate made
   before the drift fix existed.

   RECOMMENDATION: n=6 (~1.7 h). It doubles today's precision, gives 12 baseline samples against 6
   per lever, and measures whether item 3 shrank the baseline spread — which decides whether n=10 or
   n=15 is worth buying at all.
```

<a id="c29c1332-269"></a>

#### #269 — LIGHTING-WHOLE: "lighting CPU" is not any owner's total — three keys under two owners each hold part of it, and nothing declares the union

status: **pending** · blocks: #270 · metadata: `{"rank": 2}`

- `7bede779` [#271] the wait hypothesis is refuted at 0.03% -- it is CPU work, and the per-light section is 38% of it

```
Orphaned out of [#171]'s closure notes, where it would never have been seen again — a live open question
recorded inside a COMPLETED task's description does not appear in the ready queue. That is the durable
index failing in the way it exists to prevent, and it was my error in closing #171.

=== THE PROBLEM, MEASURED ===

Three keys hold parts of what anyone would call "lighting CPU", under TWO different owners, and no
declared quantity is their union. Per-frame figures from the two 90s Desert Town legs of 2026-08-15
(docs/evidence/lighting-produce-first-reading.md):

  lighting.cpu.total.us       69-75 us/frame   owner LIGHTING, role Total   the async calc thread
  producer side (5 keys)      36-40 us/frame   owner FRAME,   role Detail   [#171], render thread
  lighting.gpu.cpu_cost.us   152-159 us/frame  owner FRAME,   role Detail   driving the GPU pass

`lighting.gpu.cpu_cost.us` is LARGER THAN THE OTHER TWO COMBINED. It is CPU time the process spends
only because lighting exists, and no statement this project publishes says whether it counts.

=== WHY THIS IS NOT MERELY A DEFINITION ===

`lighting.cpu.total.us` is declared MetricRole::Total — "IS the owner's whole". It is the whole of
owner `lighting`, and that is correct and will stay correct: the other two are paid for by the render
thread and billing them to owner `lighting` would move cost in the books without moving it in the
machine (the reasoning [#171] settled). So the union is NOT any owner's total, and cannot be made one
without lying about which thread pays.

That is exactly what MetricDesc's `whole` field is for, and `whole` is INERT end-to-end: it exists,
it serialises via Telemetry::snapshot, and NOTHING reads it — scripts/telemetry-window.py's closure
reads only `spec["totals"]` keyed by (owner, domain), and the sole setter in the tree is the
test.desc.full fixture in source/test/telemetry_test.cpp. So the mechanism that would express "these
three close against one named quantity" is shipped and unused.

=== WHAT THIS TASK OWES ===

1. A DIRECTOR DECISION, and it is genuinely a judgement rather than a measurement: is the CPU cost of
   DRIVING the GPU lighting pass part of "lighting CPU"? Arguments both ways are real. For: it is
   time spent solely because lighting exists, and excluding it lets a lever that moves work from the
   lighting thread onto the render thread read as a win. Against: it is render-thread orchestration,
   and folding it in makes "lighting CPU" a number no single subsystem owns or can be held to.
2. IF INCLUDED: populate `.whole` at the production sites, teach telemetry-window.py to group Budget
   parts by their declared whole rather than by (owner, domain) alone, and resolve the MetricRole
   problem — a metric that is simultaneously a Budget part of the frame and a Total for its own
   sub-parts. Today a second (Frame, Cpu) Total silently OVERWRITES cpu.frame.work.us in
   Telemetry::snapshot's totals emitter, so this cannot be done by declaration alone.
3. EITHER WAY: one sentence, in the telemetry architecture doc, saying what "lighting CPU" is the
   total OF. Its absence is why three keys could each be partially quoted as it.

=== CAVEAT ON THE NUMBERS ABOVE ===
cpu_cost's own scope has history: [#171] found it excludes spreadIterationsFor's scan (now
lighting.gpu.spread_scan.us, 5.5-5.9 us/frame), and its registration comment records a prior cadence
defect that reported 458 us/frame against an actual 336. Treat its magnitude as approximately right
and its boundary as not yet audited.

Blocks nothing; blocked by nothing. Pure analysis + a decision — NO GPU required.
```

<a id="c29c1332-270"></a>

#### #270 — LIGHTING-REDERIVE: #168's closure and #170/#217's border percentages were measured against a denominator now known to be ~a third short

status: **pending** · blocked by: #269 · metadata: `{"rank": 3}`

```
Orphaned out of [#171]'s closure notes — recorded inside a COMPLETED task, therefore invisible to the
ready queue. Same error as [#269]; both are being given real rows rather than left as prose in a
closed record.

=== WHAT IS AT STAKE ===

[#171] measured that `lighting.cpu.total.us` captures only 63.5-67.9% of the CPU that is lighting
work — the producer side (36-40 us/frame) was never in it. Several published percentages are
denominated on that key:

  [#168]  "lighting CPU budget closed 99.6% GPU-on / 100.0% GPU-off", then "cut 16.9% by four levers"
  [#170]  adaptive border, re-measured 2026-08-04 at -10/-11%
  [#217]  border lever -27.8% region / -10.1% lighting CPU; the fix costs +5.3% region

THE CLOSURE FIGURE IS THE INTERESTING ONE. "99.6% closed" means the parts of lightingCalc summed to
99.6% of lightingCalc — which is TRUE and is not what a reader takes from it. It says nothing about
whether lightingCalc is all of lighting, and it is not. The closure was never wrong; its SCOPE was
never stated.

The percentage CUTS are a different case and may well survive: a lever that cuts X% of the gather is
still cutting that work whatever the denominator is called. What changes is the claim's reach — "cut
lighting CPU by 16.9%" becomes "cut the consumer half by 16.9%", which is roughly 11% of the whole if
the producer side is unaffected by those levers. That inference is UNVERIFIED and is the work here.

=== WHAT THIS TASK OWES ===

1. For each of the three, determine whether the lever plausibly touches the producer side at all.
   The four #168 levers act inside lightingCalc; the border levers act on the calc region. If none
   touch production, the cuts stand and only their DENOMINATOR needs restating — a doc correction,
   not a re-measurement.
2. Where restating is enough, restate it in place with the scope named, per the standing rule that a
   corrected claim is rewritten rather than annotated.
3. Where it is NOT enough, re-measure on that lever's OWN scene. #170/#217 were measured at the scene
   that exercises the border, not at Desert Town; a number re-derived at the wrong scene is a new
   error, not a fix.

=== SEQUENCING ===
Do NOT start this before [#269] settles what "lighting CPU" is the total of. Re-deriving percentages
against a denominator that is itself about to change would be paying twice, and the second answer
would supersede the first — the shape [#170]'s own "-23.6% is SUPERSEDED, not wrong" already records.

Items 1 and 2 need no GPU. Item 3, if reached, needs a live client at the border scene.
```

<a id="c29c1332-271"></a>

#### #271 — GPU-DRIVE-DECOMPOSE CLOSED: the wait hypothesis is REFUTED at 0.03% — it is CPU work, and `point` is 38% of it

status: **completed** · metadata: `{"rank": 1}`

- `7bede779` [#271] the wait hypothesis is refuted at 0.03% -- it is CPU work, and the per-light section is 38% of it
- `164163cc` [#271] the 152-159 us/frame lump gets seven parts, and one of them is the wait

```
`lighting.gpu.cpu_cost.us` was 152-159 us/frame in one TelemetryScope — larger than the lighting
consumer and the whole [#171] producer side combined, ~6.5% of cpu.frame.render.us, with no parts.

=== INSTRUMENT BUILT (164163cc) AND READ. THE ANSWER IS: COST, NOT WAIT. ===

`lighting.gpu.drive.flush.us` is **0.027% / 0.026%** of cpu_cost — 192 and 179 us across 3472 and
3454 samples, about **0.05 us per flush**. The two explicit `m_renderer->flush()` calls do not block.

THAT WAS THE WHOLE REASON THIS RANKED FIRST. [#246] found a GPU bracket reading 2.5x the device's
entire busy time was a span not a cost; [#173] closed a proposed lever on a loop sleeping 72-84% of
every frame. Neither shape applies here — so unlike those two, **a lever on this work is possible.**

THE SPLIT (two 90s legs, Desert Town, same protocol as [#171]; shares replicate within 0.05pp):
  point     38.4-39.6%   the per-light quad section, ~61 us/frame   <== the target
  upload    17.0%        emission upload + uploadObstacle
  spread    12.9%        the Jacobi dispatch loop
  repack     5.3%        the O(texels) RGBA16F pack loop
  compose    1.4%
  upscale    0.17%
  flush      0.03%
  RESIDUAL  23.6-24.8%   in none of the seven parts

Evidence: docs/evidence/gpu-drive-decomposition.md (tracked half; harness/profiles gitignored).

=== THE CLOSURE DOES NOT CLOSE, AND THAT IS CARRIED FORWARD, NOT ROUNDED AWAY ===
This task's own acceptance text said "a closure that does not close is the finding, not a rounding
note". A quarter of cpu_cost is unattributed, and `switchEffectConfig` is the named suspect —
`flushImmediatePrimitives()` is its FIRST statement, unconditional, so every effect switch is a
submit point. At least four occur per call; ONE sits inside `point` (the limit this task declared and
could not cut from the pass side) and at least TWO sit in the residual. That is 62% of cpu_cost whose
internal split is unknown.
  -> [#272] DRIVE-RESIDUAL owns it. Separating the submit from the switch is a RENDERER change, not a
     telemetry one, which is exactly the boundary this task said it could not cross.

=== WHAT THIS DOES NOT ESTABLISH ===
That `flushImmediatePrimitives` does not block. 0.05-us explicit flushes are strong evidence the
pipeline is not stalling this thread, but the submits inside switchEffectConfig were never timed, and
strong evidence is not measurement.

=== SEQUENCING NOTE FOR [#269] ===
The Director's inclusion decision now has its data: cpu_cost is a COST, not a wait. Including
152-159 us/frame of real CPU work in "lighting CPU" is a different proposition from including a wait,
and the wait reading is off the table.
```

<a id="c29c1332-272"></a>

#### #272 — DRIVE-RESIDUAL: a quarter of cpu_cost is in none of the seven parts, and switchEffectConfig submits inside another 38%

status: **pending** · metadata: `{"rank": 4}`

- `7bede779` [#271] the wait hypothesis is refuted at 0.03% -- it is CPU work, and the per-light section is 38% of it

```
Opened by [#271]'s reading. Its own acceptance text said "a closure that does not close is the
finding, not a rounding note" — this is that finding.

=== WHAT [#271] ESTABLISHED, SO THIS DOES NOT RE-LITIGATE IT ===
The wait hypothesis is REFUTED: `lighting.gpu.drive.flush.us` is 0.027%/0.026% of cpu_cost, ~0.05 us
per flush across 3472/3454 samples. cpu_cost is CPU WORK. `point` is the dominant named part at
38.4-39.6%, then `upload` 17.0% and `spread` 12.9%.
Evidence: docs/evidence/gpu-drive-decomposition.md

=== WHAT IS OPEN ===

1. THE RESIDUAL IS 23.6-24.8% OF cpu_cost and is in none of the seven parts. It is the code between
   and around the brackets: the initial switchEffectConfig("lightingSpread"), the full-quad buffer
   build, FOUR gpuTimer().begin/end pairs (each issuing GL queries), the setEffectParameter and
   setBlendMode calls, and the trailing setRenderTarget({}) + switchEffectConfig("world") +
   setEffectTextureFromTarget("lightMap", ...).

2. switchEffectConfig IS A SUBMIT POINT, AND THIS IS NOT A GUESS. `flushImmediatePrimitives()` is its
   FIRST statement, unconditional, before it even looks up the effect (StarRenderer_opengl.cpp,
   OpenGlRenderer::switchEffectConfig). There are at least four per call — lightingSpread,
   lightingPoint, lightingUpscale, world — of which ONE sits inside `point` (the limit [#271]
   declared and could not cut) and at least TWO sit in the residual.

   So `point`'s 38% and the residual's 24% BOTH contain effect-switch submits: 62% of cpu_cost whose
   internal split is unknown. Attributing it means bracketing switchEffectConfig itself, which is a
   RENDERER change rather than a telemetry one — the separation [#271] said it could not make from
   the pass side.

3. A CHEAPER FIRST STEP THAN CHANGING THE RENDERER: bracket the trailing block and the
   gpuTimer begin/end pairs from the PASS side. That cannot separate the submit from the switch, but
   it bounds how much of the residual is the trailing block versus the query issuance, and may show
   the residual is mostly one of them.

=== WHAT NOT TO DO ===
Do not widen `lighting.gpu.cpu_cost.us` to "fix" the residual by definition. It is a published number
([#168]: 336 us/frame) and [#171] and [#271] both declined to widen it for the same reason: silently
changing what a measured key spans is a re-measurement wearing a refactor's clothes.

Do not assume flushImmediatePrimitives blocks. [#271]'s 0.05-us explicit flushes are strong evidence
the pipeline is not stalling this thread, but the submits inside switchEffectConfig were never timed,
and "strong evidence" is not the same as measured.

NO GPU NEEDED to write the instrumentation; the reading needs a live client and a quiet machine.
```

<a id="c29c1332-273"></a>

#### #273 — AXIS-SUBSYSTEM: twelve in-process keys reach every leg and none reach the joined axis, so no matrix run can attribute them per lever

status: **pending** · blocks: #268 · metadata: `{"rank": 1}`

```
Found while the Director asked what would augment a 1.7-hour matrix re-run. The answer is this, and
it was not on the board.

=== THE FACT ===

scripts/lever-cpu.py analyses `iv["axis"]`, and obs-join builds that axis from exactly three sources:
sovereign /proc-derived keys matching AXIS_SUFFIXES (".busy_ratio", ".busy_cores", ".residual_cores"),
the i915 PMU engines, and ONE derived in-process value — `cpu.frame.work.wall_fraction`, built by
in_process_axis() from cpu.frame.work.us alone.

Verified on a leg captured AFTER [#171] and [#271] shipped (harness/profiles/drive271-a): the axis
carries 14 keys, and NONE of these is among them —

  lighting.produce.{entities,prep,particles,adjust}.us          [#171], 5 keys with spread_scan
  lighting.gpu.drive.{repack,upload,spread,point,compose,upscale,flush}.us   [#271], 7 keys
  lighting.gpu.cpu_cost.us, lighting.cpu.total.us               the two wholes they decompose

All are present in every leg's `.json`. All are invisible to every per-lever verdict.

=== WHY IT MULTIPLIES THE RUN ===

Today a matrix run says "off-parallaxRefreshInterval moves cpu.process by 12.2%". With the subsystem
keys on the axis it says WHICH OF TWELVE NAMED COSTS MOVED — i.e. where in the code the lever acts.
That converts the run from validating levers already shipped into attributing lever effects to named
call sites, which is the difference between retrospective confirmation and forward guidance.

Concretely it would answer, for free, in the same 1.7 hours:
  * Does ANY lever move `lighting.gpu.drive.point.us`? That is 38% of a 152-159 us/frame cost that
    [#271] proved is real CPU work — the best lever target currently known, and nothing has tested
    whether the existing eight touch it.
  * Do the border/cache levers move the PRODUCER side ([#171]'s five keys)? That is exactly what
    [#270] needs to know before deciding whether its percentages can be restated or must be re-measured.
  * Does the drive cost move with the levers at all, or is it a fixed floor? Decides whether [#272]'s
    residual hunt is chasing something a lever already controls.

=== THE DESIGN POINT, WHICH IS NOT MECHANICAL ===

The axis is documented as "the small dimensionless set a plot puts on one Y". These keys are
microsecond TOTALS over an interval, so they cannot go on raw — they need normalising the way
cpu.frame.work.wall_fraction already is. Two defensible choices:
  (a) wall fraction: total / 1e6 / durationS -- same shape as the existing in-process axis entry,
      directly comparable to the owner busy_cores beside it.
  (b) per-frame microseconds: total / frame count -- the unit every reading in
      docs/evidence/{lighting-produce-first-reading,gpu-drive-decomposition}.md is quoted in.
(a) keeps the axis dimensionless and one-Y-plottable; (b) matches how the numbers are already
discussed. Pick ONE and say why in the code, because a mixed axis is how a sum stops meaning anything.

NOTE the cadence trap: several of these are MetricCadence::Call (spread_scan, adjust, and all seven
drive keys), firing on lightmap-publish frames only — 1736 of 4499. Dividing by FRAMES would understate
them by 2.6x. Divide by the key's own count, or by durationS, never by a frame count they do not share.

=== SCOPE ===
Touches scripts/obs-join.py (axis construction) and scripts/lever-cpu.py (the analysed key list).
NO GPU required. Must be validated against the banked run + the four legs captured today, showing the
new axis entries appear and the existing 14 are byte-identical — the same null-control discipline
[#268] required.

DO THIS BEFORE any matrix re-run. Every key on the axis when the run starts gets eight lever deltas
for free; every key added afterwards costs another 1.7 hours to attribute.
```

### Store `6c8fc9cc-f25d-49cb-9d3e-7a1bcae0c776`

<a id="6c8fc9cc-4"></a>

#### #4 — L1-FIX: the four false comments, the makeDoubled face leak, the GlPass field bag, and the per-draw glTexParameteri hoist

status: **pending**

- `6c75f4aa` board: evidence rendered for every store, and #245's progress written back

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
  is the fork point, ~83+ commits stale.
```

