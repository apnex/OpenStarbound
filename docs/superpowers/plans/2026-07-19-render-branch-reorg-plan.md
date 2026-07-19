# Render-work branch reorganization — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Reconstruct the fork's entire divergence from upstream into clean, per-purpose branches (render layers +
all other work), off a clean upstream base, with the integrated result **provably byte-identical** to today's
`dev/upstream-merge` — nothing lost, only the branch topology changed. Work stays in our repo; no PRs.

**Architecture:** A `file/hunk→cluster` manifest partitions the **net fork diff** (`2ea33530 → trunk`) by cluster;
a dependency-ordered, oracle-gated **diff-partition replay** applies each cluster's slice onto its clean base and
commits it as a small set of logical commits (the messy authored history — reverts, "oops", the fc3d55e base
mismatch — is discarded here and preserved only in the frozen legacy trunk). Assemble
`integration = merge(all sovereign) + toolchain`; the acceptance gate is
`git diff integration dev/upstream-merge == empty`. Cutover is gated on that oracle, so the aggregate is never at
risk.

**Tech stack:** git (worktrees, `apply`/checkout-by-path, per-hunk staging), bash + python for the manifest/oracle
scripts, the existing `scripts/render-gate.sh` + `ctest` for functional verification.
**Base commit: `2ea33530`** — established (Task 0.1) to be **tree-identical to `upstream/main`** (the `git diff`
is empty; upstream rewrote its own SHAs, so `2ea33530` and `2c7f972b` are the same snapshot) **and an ancestor of
the trunk**. So the clean branches root at the exact upstream tree, and the reconstruction has real shared
ancestry. Design: `docs/superpowers/specs/2026-07-19-render-branch-reorg-design.md`.

**Standing constraints:** Build/render-gate ONLY when the user is OUT of game (`pgrep` first) — Phase 5 only. No
`git add -A`, no `filter-repo` rewrites. No PRs, no push to `upstream`, `origin`-only. Commit/push only when asked.

---

## Phase 0 — Reconstruction engine & bases (git-only; safe while the game runs)

### Task 0.1: Verify the reconstruction base

**Files:** none (investigation).

- [ ] **Step 1: Confirm `2ea33530` is the right base — tree-identical to `upstream/main` AND an ancestor of the
  trunk.** (DONE 2026-07-19.)

```bash
cd /root/frackin/OpenStarbound
git diff --stat 2ea33530 upstream/main            # expect EMPTY -> same tree as current upstream
git merge-base --is-ancestor 2ea33530 dev/upstream-merge && echo "ancestor of trunk"   # expect true
git diff --shortstat 2ea33530 dev/upstream-merge  # the net fork payload (~199 files, +18142/-1320)
```

Result: `2ea33530` tree == `upstream/main` (empty diff), and it IS an ancestor of the trunk. It is NOT an ancestor
of `upstream/main` — irrelevant, because its *tree* matches (upstream rewrote its own history). `$BASE=2ea33530`.
A later, optional, trivial rebase can root the branches at `upstream/main` (2c7f972b) since the trees are equal.

### Task 0.2: Build the `file/hunk→cluster` manifest

**Files:** Create `scripts/reorg/manifest.tsv` (owned whole-files), `scripts/reorg/shared-hunks.tsv` (shared-file
hunk attributions), `scripts/reorg/select-hunks.sh` (the hunk filter).

- [ ] **Step 1: List the net fork diff files.** `git diff --name-only 2ea33530 dev/upstream-merge` → the 199
  changed paths. This is the complete reconstruction payload.
- [ ] **Step 2: Auto-attribute the WHOLE-FILE-owned paths.** Every path touched by exactly one cluster (all new
  files + single-cluster edits) maps by path, using the inventory's file→cluster map (design §2). Output
  `manifest.tsv`: `path  cluster`.
- [ ] **Step 3: Attribute the SHARED files' hunks.** The ~7 files touched by multiple clusters —
  `StarRenderer_opengl.{cpp,hpp}`, `StarRenderer.hpp`, `source/rendering/StarWorldPainter.{cpp,hpp}`,
  `StarRootLoader.cpp`, `StarClientCommandProcessor.cpp`, `StarClientApplication.cpp`, `opengl.config`,
  `StarCellularLighting*` — get per-hunk attribution in `shared-hunks.tsv` (hunk header/line-range → cluster),
  from the layer-boundary audit (design §2). `select-hunks.sh <patch> <cluster>` emits only that cluster's hunks.
- [ ] **Step 4: Completeness check.** Every one of the 199 files is either owned-by-one-cluster (in `manifest.tsv`)
  or in the shared set with all its hunks attributed (`shared-hunks.tsv`). Zero unattributed files or hunks — this
  is the completeness invariant the Phase-5 oracle ultimately proves.

**Cluster → base map** (used by the procedure). Perf is grouped into **5 subsystem branches** (director decision
2026-07-19) to avoid 4-way hunk-splitting of shared game files; the grouping makes `StarWorldServer` and
`StarWorldClient` single-group-owned, leaving only `StarObject`/`StarEntity`/`StarPlant` as light 2-way cross-group
splits.

- `up/*` (msaa, fbo-diagnostics, setscreensize-facestorage, shader-recovery, lua-json-underflow) → `$BASE`
- `render/layer1` → `$BASE`; `render/layer2-retained-surface` → `render/layer1`; `render/layer3-passes` →
  `render/layer2-retained-surface` (CDL folds into L3; the render-adjacent perf — gpu-render-ladder,
  temporal-lighting, cpu-lighting-gather, texture-upload — also stacks on `render/layer3-passes` **or** joins the
  perf group that owns its files, decided per-file in the manifest)
- `tooling/telemetry`, `tooling/render-harness-oracle`, `tooling/build-toolchain`, `docs/*` → `$BASE`
- **perf/server-tick** (WorldServer + WorldServerThread + collision-broadphase + dormancy + pertick-reuse +
  server-microopts + Periodic + CellularLiquid) → `tooling/telemetry`
- **perf/entity-dispatch** (Entity + interfaces/* accessor overrides (de-RTTI) + EntityMap + SpatialHash2D) → `$BASE`
- **perf/animation-drawable** (NetworkedAnimator + Object drawable-cache + AnimatedPartSet) → `tooling/telemetry`
- **perf/world-client-lighting** (WorldClient: temporal-gate + lighting-gather + texture-upload-stable-grid +
  TemporalLightingGate) → `tooling/telemetry`
- **perf/core** (Json + Lua + LuaRoot/Components + NetElement* + exception-backtrace + StatusController) → `$BASE`

### Task 0.3: Write the tree-diff oracle

**Files:** Create `scripts/reorg/oracle.sh`.

- [ ] **Step 1: The oracle = tree-identity of a candidate branch vs the trunk.**

```bash
#!/bin/bash
# scripts/reorg/oracle.sh <candidate-ref>  -- PASS iff the candidate's tracked tree == dev/upstream-merge's.
set -u; cd "$(git rev-parse --show-toplevel)"
CAND="${1:?candidate ref}"
DIFF=$(git diff --stat "$CAND" dev/upstream-merge -- . ':(exclude)scripts/reorg/')
if [ -z "$DIFF" ]; then echo "ORACLE PASS: $CAND is tree-identical to dev/upstream-merge"; exit 0
else echo "ORACLE FAIL: residual diff (mis-attributed/dropped hunks):"; echo "$DIFF"; exit 1; fi
```

- [ ] **Step 2: Verify the oracle self-tests: `oracle.sh dev/upstream-merge` PASSES** (a branch is identical to
  itself).

### Task 0.4: Cut the base branches off `$BASE`

**Files:** none (git branches).

- [ ] **Step 1: Create the fork-only bases and the toolchain overlay branch.**

```bash
git branch tooling/build-toolchain "$BASE"
git branch tooling/telemetry "$BASE"
# (populated by their clusters in Phase 3-tooling via the replay procedure)
```

- [ ] **Step 2: Commit the reorg tooling** (`scripts/reorg/*`, `manifest.tsv`) on a scratch `reorg/tooling` branch
  so the engine is versioned but not mixed into a cluster. (Commit only when the user asks.)

---

## The per-cluster diff-partition procedure (referenced by Phases 1–4)

For each cluster `C` with base `B`, its **owned whole-files**, and its **shared-file hunk set** from the manifest:

```bash
git worktree add ../reorg-wt "$B"; cd ../reorg-wt   # isolate from the running game's working tree
git switch -c "$C"
# 1) OWNED whole files (new files + files only this cluster touches): take the trunk's version verbatim
git checkout dev/upstream-merge -- <C's owned files>
# 2) SHARED files (StarRenderer_opengl.cpp, StarWorldPainter.cpp, StarRootLoader.cpp, StarClientCommandProcessor.cpp,
#    opengl.config, StarRenderer.hpp, StarClientApplication.cpp, StarCellularLighting*): apply ONLY C's hunks onto
#    B's version of the file. Because clusters STACK in dependency order, each layer/branch adds its hunks on top of
#    what its base already has; by the top of a stack the shared file == the trunk's version (all hunks present).
git diff "$B" dev/upstream-merge -- <shared file> > /tmp/full.patch
scripts/reorg/select-hunks.sh /tmp/full.patch "$C" | git apply --3way   # select-hunks filters to C's attributed hunks
git add -A <C's paths>; git commit -m "reorg($C): <one-line purpose>"
# 3) partial verify: C's OWNED paths now match the trunk exactly
git diff HEAD dev/upstream-merge -- <C's owned files>   # expect EMPTY
```

Rules: apply clusters in **dependency order** (a base's shared-file hunks are present before a dependent adds its
own). Owned files are exact (`checkout` from trunk); shared files accumulate their attributed hunks up the stack.
The full oracle (Phase 5) — `integration == trunk` — is the final proof that the partition was complete and
disjoint. The messy authored history is intentionally NOT carried; it lives in the frozen legacy trunk.

---

## Phase 1 — Upstream-based clean branches (git-only)

### Task 1.1: Reconstruct the five `up/*` branches off `$BASE`

**Files:** git branches `up/msaa-opt-in`, `up/fbo-diagnostics`, `up/setscreensize-facestorage`,
`up/shader-recovery`, `up/lua-json-underflow`.

- [ ] **Step 1: Apply the replay procedure** to each, base `$BASE`. These are small and mostly disjoint; several
  already exist as `pr/*`/`fix/*` on the old lineage — use those as the content reference, but re-express onto
  `$BASE` via the manifest commits (not by merging the old lineage).
- [ ] **Step 2: Per-branch sanity** — each is a small, self-contained delta (`git diff $BASE up/<x> --stat` is only
  that fix's files). No build here (Phase 5). No PR is created.

---

## Phase 2 — The render stack (the hard core; git-only)

### Task 2.1: `render/layer1` off `$BASE`

**Files:** branch `render/layer1`.

- [ ] **Step 1: Replay the L1 substrate cluster** (StarGlRenderSurface.*, StarGlTexturePrimitives.*, the
  StarRenderer_opengl.* extraction + §9 hardening, StarRenderDiagnostics.hpp, render_surface_test.cpp,
  layer1-layering-lint.sh, CMakeLists). Per design, **the whole `OpenGlRenderer` backend ships in L1** — the L2/L3
  method bodies interleaved in `StarRenderer_opengl.cpp` come along; L2/L3 branches consume, never re-touch it.
- [ ] **Step 2: Partial oracle** — `git diff render/layer1 dev/upstream-merge -- source/application/` should show
  only the *L2/L3-specific interface additions in `StarRenderer.hpp`* still pending (they arrive with L2/L3), and
  nothing else. Record the residual for cross-check in 2.2/2.3.

### Task 2.2: `render/layer2-retained-surface` off `render/layer1`

**Files:** branch `render/layer2-retained-surface`.

- [ ] **Step 1: Replay the L2 cluster** — the env/parallax cache policy in `StarWorldPainter.cpp::render()` +
  `.hpp` members, the L2 Renderer-interface hunks (composite/setRenderTarget/clearRenderTarget/hasFrameBuffer/
  frameBufferGeneration + BlendMode Premultiply\*), the L2 config/command/asset hunks (opengl.config env/parallax
  FBOs, StarRootLoader env/parallax keys, StarClientCommandProcessor render/rendercache handlers). **The conflict
  labor is here:** L2 hunks are interleaved with L3 in `StarWorldPainter::render()` and the shared config files.
- [ ] **Step 2: Partial oracle** — `git diff render/layer2-retained-surface dev/upstream-merge -- source/rendering/StarWorldPainter.cpp`
  should show only the L3 GPU-lightmap-dispatch block still pending.

### Task 2.3: `render/layer3-passes` off `render/layer2-retained-surface`

**Files:** branch `render/layer3-passes`.

- [ ] **Step 1: Replay the L3 clusters** — GpuLightmapPass.* + CDL + the L3 Renderer-interface hunks
  (getEffectParameterHandle, setEffectTexture{Alias,Half,R8}, BlendMode Additive/Max), L3 assets
  (lighting{Point,Spread,Upscale}.*, world.frag/config uniforms), the base lighting mirrors
  (StarCellularLight\*), L3 config/command hunks.
- [ ] **Step 2: Partial oracle** — `git diff render/layer3-passes dev/upstream-merge -- source/rendering/ source/application/`
  should be **empty** (the render stack now fully reconstructs the render files). This is the biggest checkpoint.

---

## Phase 3 — Independent perf branches (git-only)

### Task 3.1: The tooling bases

**Files:** branches `tooling/telemetry`, `tooling/render-harness-oracle`, `tooling/build-toolchain`, `docs/*`.

- [ ] **Step 1: Replay** each tooling cluster onto `$BASE` via the procedure (telemetry first — it is the perf base).

### Task 3.2: The ~13 independent perf clusters

**Files:** `perf/entity-dormancy`, `perf/entity-de-rtti`, `perf/entitymap-spatialhash`, `perf/collision-broadphase`,
`perf/netdelta-dirty-version`, `perf/json-query-fastpath`, `perf/lua-proto-gc`, `perf/server-pertick-reuse`,
`perf/status-uniqueeffect-churn`, `perf/server-tick-microopts`, `perf/exception-lazy-backtrace`,
`perf/animatedpartset-memo`, `perf/render-drawable-cache`.

- [ ] **Step 1: Replay** each onto `tooling/telemetry` (tapped) or `$BASE` (untapped) per the manifest. These
  touch disjoint game/core/base files — mostly clean cherry-picks, minimal conflicts.

---

## Phase 4 — Render-adjacent perf (git-only)

### Task 4.1: The four render-adjacent perf clusters on `render/layer3-passes`

**Files:** `perf/gpu-render-ladder`, `perf/temporal-lighting`, `perf/cpu-lighting-gather`,
`perf/texture-upload-stable-grid`.

- [ ] **Step 1: Replay** each onto `render/layer3-passes` (they touch render/lighting files L3 owns). Resolve
  conflicts by keeping the cluster's hunks.

---

## Phase 5 — Integration + THE oracle (REQUIRES USER OUT OF GAME)

### Task 5.1: Assemble `integration`

**Files:** branch `integration`.

- [ ] **Step 1: Build `integration` by merging every sovereign branch + the toolchain overlay** in dependency
  order (render stack, then perf, then render-adjacent perf, then tooling/telemetry/harness, then
  build-toolchain). Octopus or sequential merges; resolve any merge conflicts by preferring the branch that owns
  the file (should be none if attribution was disjoint).

### Task 5.2: The acceptance oracle

- [ ] **Step 1: Run the tree-diff oracle.**

```bash
bash scripts/reorg/oracle.sh integration
```

Expected: **`ORACLE PASS`** (empty diff). If FAIL, the printed residual pinpoints the mis-attributed/dropped
hunk — fix the owning cluster branch, re-merge, re-run. Do NOT proceed until PASS.

### Task 5.3: Functional confirmation (out-of-game)

- [ ] **Step 1: Confirm the user is out of game** (`pgrep -af starbound | grep -v claude`), then build `integration`
  E-core-pinned: `VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang -j 10`.
- [ ] **Step 2: Run the gates:** `bash scripts/render-gate.sh` (env/parallax/spread DIFF=0, GL_INVALID=0) +
  `ctest -R 'core_tests|layer1_layering|render_surface_tests'`. Expected: all green (game_tests unchanged, #146
  pre-existing).

Expected: oracle PASS + gate PASS + tests green = `integration` is byte-identical AND functional.

---

## Phase 6 — Cutover & cleanup (git-only; do only after Phase 5 green)

### Task 6.1: Preserve the archives

- [ ] **Step 1:** `git tag archive/dirty-region-foundation feat/dirty-region-foundation` (and `dirty-region`,
  `repro/d7`). Confirm the tags resolve before any branch deletion.

### Task 6.2: Adopt `integration`, freeze legacy

- [ ] **Step 1:** Keep `dev/upstream-merge` as the frozen legacy trunk (do not delete; it is the pushed public
  branch). `integration` becomes the go-forward live build; deploy from it going forward.

### Task 6.3: Delete the 35 stale branches (only after the oracle confirmed their content is in `integration`)

- [ ] **Step 1:** For each of the 35 SAFE branches from the nothing-lost audit, `git branch -D <name>` — but ONLY
  after re-confirming its feature signature is present in `integration` (spot-check, not blind).

### Task 6.4: Decide the public-branch fate

- [ ] **Step 1: Surface to the user** (do not act unprompted): keep `origin/dev/upstream-merge` frozen as legacy,
  and whether/when to push the new clean branches to `origin`. No push without explicit approval; pre-flight scan
  first.

---

## Self-review

- **Spec coverage:** topology (Phases 1–4 + tooling), base-deps (Task 0.4 bases, 5.1 toolchain overlay), method
  (0.2 manifest, 0.3 oracle, replay procedure), phasing (Phases 0–6), aggregate-guarantee (5.2 oracle),
  constraints (headers + Phase 5 gating) — all covered.
- **Placeholder scan:** no TBD/TODO; the one irreducibly-manual step (attribution in 0.2, conflict resolution in
  2.1–2.3/4.1) is described procedurally, not hand-waved, and gated by the oracle.
- **Consistency:** base `$BASE` = `2ea33530` used uniformly; the oracle (`== dev/upstream-merge`) is coherent with
  basing off the trunk's own upstream snapshot; cluster names match the design manifest.
- **Ambiguity:** the oracle-base subtlety (why `2ea33530` not `2c7f972b`) is stated explicitly; the optional
  rebase onto current upstream is a deliberate later step, out of this plan's scope.
