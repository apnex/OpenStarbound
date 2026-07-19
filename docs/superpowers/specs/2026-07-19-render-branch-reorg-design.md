# Render-work branch reorganization — design

> **Status: design approved section-by-section (2026-07-19); spec for review before writing the implementation
> plan.** This is a *carefully-reasoned reorg* of the fork's entire divergence from upstream into clean,
> per-purpose branches, so the render layers (and everything else) each live on an appropriate branch, nothing is
> lost, and the upstreamable work is easy to propose. It is the setup for building Layer 2 (`RetainedSurface`) on a
> clean base — but its scope is the whole fork, not just render.

## 1. Goal & requirements

Reorganize a heavily-diverged public fork of OpenStarbound so that:

1. **Branches cleanly represent the work** — the render layers (L1 Surface substrate / L2 RetainedSurface caches /
   L3 sovereign passes) plus all other fork work (CPU/server perf, tooling, docs), each a clean delta off a known
   base.
2. **Keep the upstreaming option open — but stay in our repo.** Layer 1 and the genuine vanilla bug fixes become
   clean branches cut off `upstream/main`, so a future upstream contribution is *possible*. L2/L3 and the perf
   features stay sovereign but are likewise kept clean/PR-able. **We do NOT construct or submit any PRs, and never
   push to upstream — all work stays on `origin` (our repo).** The `up/*` branches are organizational (isolating
   vanilla-reproducible work from sovereign features); they are not PR submissions.
3. **Nothing lost, in aggregate** — the integrated result is provably byte-for-byte identical to today's live
   trunk, and the off-trunk staked experiments are preserved as archives.

## 2. Starting reality (why this is non-trivial)

- The live trunk is **`dev/upstream-merge`** (`1d9d05d3`), a superset of `dev/perf-integration`.
- A player-save purge (`git-filter-repo`) **rewrote the trunk's SHAs**. So `dev/upstream-merge`,
  `dev/perf-integration`, `repro/d7`, `fix/backdrop-cache-bugs` are on a **new** (post-purge) lineage; **all**
  `feat/*`, `pr/*`, `fix/*`, `perf/*`, `build/*`, and `main` are on the **old** (pre-purge) lineage.
- Consequently **`git merge-base(dev/upstream-merge, upstream/main)` is empty** — no shared ancestry with vanilla
  — and cross-lineage commit-range counts are meaningless (every old branch shows ~2000+ "unique" commits, which
  is the lineage split, not real unmerged work). All reasoning is at the **content/diff** level, never by range.

### Inventory (read-only audit, 2026-07-18/19)

**Nothing-lost audit — 38 branches:** 35 are stale pre-purge copies whose content is already in the trunk (safe to
delete after the oracle confirms); **3 hold unique off-trunk work** — `feat/dirty-region-foundation`,
`feat/dirty-region` (staked dirty-region lighting experiments) and `repro/d7` (the only copy of the D7 MSAA
hardware-proof logs). Those 3 are preserved by tagging.

**Fork-authored clusters, by destiny:**

- **Upstream-bound** — Layer-1 render-surface substrate; MSAA opt-in; FBO diagnostics + altId leak;
  setScreenSize/face-storage; shader-recovery (RB-2/3/4, the #542 defects); lua/json list-underflow. *(The HDR
  uninitialized-crash is already fixed upstream — `36a389c6` — skip.)*
- **Render-sovereign, maybe-upstream-later** — L2 retained-caches; L3 GpuLightmapPass; CDL tonemap/promote.
- **Perf-sovereign, orthogonal (~17)** — gpu-render-ladder, temporal-lighting, cpu-lighting-gather,
  texture-upload-stable-grid, drawable-cache, animatedpartset-memo, entity-dormancy, entity-de-RTTI,
  entitymap-spatialhash, collision-broadphase, json, lua-proto-gc, netdelta, server-pertick-reuse, status-churn,
  server-microopts, exception-backtrace.
- **Tooling/docs, never upstream** — telemetry substrate; render harness+oracle; docs + upstream drafts;
  build-toolchain (Fedora-44/Clang vcpkg fixes).
- **Archive (unique, off-trunk)** — dirty-region-foundation, dirty-region, d7-repro.

## 3. Target topology

Everything off `upstream/main` (`2c7f972b`):

```
upstream/main (bare)
├─ up/render-surface  up/msaa-opt-in  up/fbo-diagnostics  up/setscreensize-facestorage
│      up/shader-recovery  up/lua-json-underflow          ← clean upstream-based branches (kept PR-able; NOT submitted)
│
├─ render/layer1  →  render/layer2-retained-surface  →  render/layer3-passes   ← the render STACK, off bare upstream
│
├─ tooling/telemetry                     ← the ONE thin fork base
│    └─ perf/<cluster>  (tapped)         ← the ~17 perf clusters stack here (untapped ones may be off bare upstream)
│         └─ (render-adjacent perf: gpu-render-ladder, temporal-lighting, cpu-lighting-gather,
│             texture-upload)  stack on render/layer3
│
├─ tooling/render-harness-oracle    docs/*
├─ tooling/build-toolchain               ← BUILD OVERLAY, merged only at integration
│
├─ archive/dirty-region-foundation  archive/dirty-region  archive/d7-repro   ← preserved, never built, never deleted
│
└─ integration = merge(all sovereign) + tooling/build-toolchain   ← the LIVE BUILD, byte-identical to today's trunk
```

## 4. Base-dependency handling (destiny-driven, one thin fork base)

Toolchain and telemetry are *different kinds* of dependency and get different treatment:

- **`build-toolchain` is a build-config OVERLAY, not a base.** It changes vcpkg/CMake, not engine semantics. It
  lives only on `integration`. Every feature/layer/PR branch stays off **bare `upstream/main`**; we build/verify
  them by merging onto `integration` (or a throwaway toolchain merge). This keeps the PRs genuinely clean.
- **`tooling/telemetry` IS a source dependency** — a perf branch's measurement taps call the telemetry API, so
  they need it to compile. It is the one justified thin fork base: tapped perf branches stack on it. The render
  layers and bug-fix PRs do not.
- **Do NOT pre-peel the telemetry taps.** Telemetry is fork-only and never upstreamed, so a future upstream PR of
  a perf cluster must strip its taps — but that is *maybe-later* work. Peeling all clusters now, to serve PRs we
  may never file, is the YAGNI trap. Keep taps with their branch; peel a cluster's taps **only when it is actually
  being upstreamed.**

## 5. Reconstruction method

**Attribution from history.** There is no ancestry to upstream, but the trunk has a complete, internally-coherent
commit history: every fork change was made by *some* trunk commit. So we do not guess hunk→cluster — we read it:
new files self-attribute; every shared-file hunk attributes to the trunk commit that introduced it (`git log -p`
/ `blame`), which maps to a cluster via the inventory. Output: a reviewed **`commit→cluster` manifest**, with a
zero-orphan-commit completeness check.

**The tree-diff oracle (correctness backbone).** After reconstruction,
`integration = merge(all sovereign branches) + toolchain`. The acceptance test is:

> **`git diff integration dev/upstream-merge` over fork paths = EMPTY.**

An empty diff proves `integration` is byte-for-byte identical to today's trunk — complete and unchanged, by
construction. A non-empty diff points at the exact mis-attributed or dropped hunk. Then `integration` builds +
passes the render gate + `core_tests`/`game_tests` for functional confirmation. **The cutover is gated on this
oracle**, so there is no window in which the aggregate is at risk: either the reconstruction provably equals the
trunk and we adopt it, or it does not and we keep `dev/upstream-merge` as-is.

**Dependency-ordered diff-partition replay.** The base is `2ea33530` — established to be **tree-identical to
`upstream/main`** (upstream rewrote its own SHAs; the trees are equal) **and an ancestor of the trunk**. Rather than
replay the messy authored commits (reverts, "oops", the fc3d55e/2ea33530 base mismatch), we **partition the net fork
diff** (`2ea33530 → trunk`, ~199 files) by cluster and apply each cluster's slice onto its base, committing it as a
few clean logical commits. Owned whole-files are taken verbatim from the trunk; the ~7 shared files
(`StarRenderer_opengl.cpp`, `StarWorldPainter.cpp`, config/command files) accumulate their attributed hunks up the
dependency stack, so by the top of a stack the shared file equals the trunk's version. This yields clean,
upstream-applyable per-cluster deltas; the authored history is discarded here and preserved only in the frozen
legacy trunk. The replay is a **repeatable script** that runs the oracle after each stage.

**The real labor** is the conflict resolution in the interleaved render files (`StarRenderer_opengl.cpp`,
`StarWorldPainter::render()`), where L1/L2/L3 hunks are physically interspersed. The oracle makes it *safe* (a
mistake cannot slip through); it is simply the part that takes time.

## 6. Aggregate-preservation guarantee

The end state maintains all existing work in aggregate:

- **The live build** (`integration`) is proven byte-for-byte identical to today's `dev/upstream-merge` tree by the
  oracle — every line of every shipped feature.
- **The 35 stale branches** are already in the trunk; deleted only after the oracle confirms; nothing lost.
- **Reverted work** (e.g. the L2 VAO-bake) is correctly *absent* — the tree reflects the revert.
- **The 3 archives** hold off-trunk work (not in the aggregate today either); preserved as `archive/*` tags.

The only thing that changes is the **git structure** (clean branches, new SHAs, upstream base) — the goal — and
even the old history survives: `dev/upstream-merge` is kept frozen as the legacy trunk.

## 7. Phasing

| Phase | Work | Game-safe? | Gate |
|---|---|---|---|
| **0** | `commit→cluster` manifest + reconstruction/oracle scripts; cut `tooling/build-toolchain` + `tooling/telemetry` off upstream | git-only, safe in-game | manifest complete (zero orphans) |
| **1** | Upstream-bound PRs off bare upstream (mostly exist) | git-only | each a clean bare-upstream delta |
| **2** | Render stack `layer1`→`layer2`→`layer3` (the hard core: interleaved render files) | git-only | partial oracle: render-file tree == trunk |
| **3** | ~13 independent perf branches | git-only | content matches trunk hunks |
| **4** | Render-adjacent perf (gpu-render-ladder, temporal-lighting, cpu-lighting-gather, texture-upload) on `layer3` | git-only | — |
| **5** | `integration` = merge(all) + toolchain; **THE oracle** (empty tree-diff) + build + render gate + tests | **needs out-of-game** | oracle empty + byte-identical + gate PASS |
| **6** | Cutover: `integration` forward, `dev/upstream-merge` frozen legacy; tag 3 archives; delete 35 stale (post-oracle); decide public-branch fate | git-only | — |

Phases 0–4 are git/attribution/branch construction and can proceed while the game runs (git does not touch the
running deployed build; if needed, replay runs in a worktree). Only Phase 5's build + render gate must wait for an
out-of-game window. Every phase leaves a resumable, oracle-checkable state.

## 8. Constraints

- **Build only when the user is OUT of game** (`pgrep` check first); E-core-pinned build; the render gate boots
  offscreen on the real GPU — Phase 5 only.
- **No `git add -A`; no `filter-repo`-class rewrites.** The reorg creates fresh clean branches; it never rewrites
  the pushed public history (which stays frozen as legacy). Pre-flight scan before any push (no
  save/world/harness/pak paths).
- **We work only in our repo (`origin`). Do NOT construct PR descriptions, open PRs, or push to `upstream`.** The
  `up/*` branches keep future upstreaming *possible* but are organizational only; the existing `docs/upstream/*`
  drafts stay unfiled and no new ones are written.
- **Commit/push only when asked.**
