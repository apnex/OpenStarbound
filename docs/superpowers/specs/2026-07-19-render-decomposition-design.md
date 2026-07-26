# Render Subsystem Decomposition — Target-State Design

**Status:** brainstormed + director-approved **inline** (2026-07-19). Author's implementation reference — the
director does not review spec markdown; the design was approved section-by-section in conversation.
**Supersedes / absorbs:** `2026-07-19-retained-surface-L2-design.md` (the RetainedSurface spec becomes **step 1**
of this sequence). **Refines:** `2026-07-14-render-surface-subsystem-design.md` (the Surface/RetainedSurface/passes
vision) with three things that doc predates — the branch reorg, the *layers-are-an-output* realization, and the
concrete buildability blockers found by the 2026-07-19 read-only render-subsystem map.
**Diagram:** the target state is rendered at the Artifact published 2026-07-19 (layered stack + Air-Gap seam +
extraction rail). **Scope:** the render subsystem only. **Sim-side god-objects are explicitly out of scope**
(their own future campaign — see `[[sim-side-sovereignty-refactor]]`).

---

## 1. The problem

`StarWorldPainter` is a 870-line god-object fusing seven concerns behind one 588-line `render()`: thin
orchestration, the env+parallax backdrop and its two hand-rolled caches, the GPU lightmap dispatch, the world
body (tiles/entities/particles/bars), the two retained-cache primitives, a cross-cutting telemetry smear, and
ad-hoc `Root::singleton()` config reads every frame. The reorg proved the render fork-work partitions into clean
*deltas* (aggregate == trunk), but the branches do **not** compile independently — independent compile is a
stronger property that needs decomposed **code**, not attributed deltas. The 2026-07-19 map pinned exactly why a
render layer can't compile alone:

1. **Fat `WorldRenderData`** (`game/StarWorldRenderData.hpp`) — every pass `#include`s it and it transitively drags
   in the whole game render-data graph, so no pass compiles knowing only its own inputs.
2. **`Root::singleton()` reached inside method bodies** — a hidden global that makes every pass unconstructible /
   untestable in isolation.
3. **String-keyed telemetry smear** — `gpuTimer().begin("free.string")` + `Telemetry::counter("string")` threaded
   through every phase, coupling each render file to the telemetry cluster with zero compile-time ownership.

And the deepest coupling is not a call at all: **passes hand off by leaving GL state mutated** (a target / effect /
sampler left bound as the implicit output). That shared-mutable-GL-state is a latent bug-class that has already
bitten this campaign repeatedly (swap-blindness, the D7 MSAA black-world, the env-cache washout family).

## 2. Target state — six modules

Read top-to-bottom as *depends downward*; the only thing a pass ever touches is the abstract `Renderer`.
Exemplar: `GpuLightmapPass` is **already** sovereign — one entry, `Renderer*`-only, owns its state and its
telemetry, holds no `Root`/`WorldRenderData`. The other concerns are made to match it.

| module | one job (Law of One) | depends on (Air-Gap) | status |
|---|---|---|---|
| **WorldPainter** (orchestrator) | owns the camera; slices `WorldRenderData` → per-pass inputs; injects config; sequences the passes | the passes | to thin |
| **LightmapPass** | the GPU lighting solve (point/spread/upscale) | `Renderer*` + `LightingInput` | sovereign (from `GpuLightmapPass`); tighten only |
| **BackdropPass** | sky + parallax + env background **+ the compose** | `Renderer*` + `BackdropInput` + 2×`RetainedSurface` | to extract |
| **WorldPass** | the world body — tiles/entities/particles/bars, one z-order algorithm | `Renderer*` + `WorldInput` + game DBs | to extract |
| **RetainedSurface** | a retained cache: persistent FBO + refresh gate + invalidation key | `Renderer*` | **step 1** (in design) |
| **Renderer / OpenGlRenderer** (L1 substrate) | the GPU render API + backend (`GlTargets`·`GlPass`·`GlEffects`·`GlFrameBuffer`·`GlLoneTexture`) | — | sovereign, shipped |

**Accepted boundary calls (director, 2026-07-19):** (a) `WorldPass` legitimately depends on game simulation
(`TileDrawer`/`MaterialDatabase`) — it is the one game-coupled pass and is not faked pure (forcing purity =
Ceremony Bloat). (b) `RetainedSurface` is a **standalone** module, not a `BackdropPass` internal — world-bands is a
coming second consumer and `LightmapPass`'s `lightingRef` a latent third (Earned Exposure).

## 3. The Air-Gap contracts — the buildability spine

> ### ⚠ CORRECTION, 2026-07-26 — read this before implementing anything below
>
> This section is the **authority document** for the Air-Gap contracts, and it is now partly wrong. It is
> corrected here rather than rewritten, so the record of what was approved survives beside what shipped.
> Guardrail **G10** (`docs/render/axiom-alignment-audit.md`): when an implementation overrides an approved
> prescription on evidence, the authority document is corrected in the same change. That did not happen at
> the time — this is the repair, a week late, and the delay is the finding.
>
> **RETRACTED — "resolved once and handed to each pass … makes passes constructible in isolation."**
> Resolving config at *construction* would ship a regression. All eight backdrop knobs are live-tunable
> mid-session (`/rendercache envrefresh`, `/rendercache parallaxrefresh`, the antiAliasing client option
> polled every frame); freezing them at construction silently breaks the console levers this campaign uses
> to A/B its own work. **What shipped is a per-frame params struct resolved at the boundary** —
> `BackdropParams`, then `LightmapParams` — which has the same Air-Gap property, the pass being a pure
> function of its parameters, without the regression. See `StarBackdropPass.hpp`.
>
> **WRONG WHEN WRITTEN — "per-pass telemetry handles … (LightmapPass already does — the model)."**
> LightmapPass did not. It used function-local statics, as did every other pass; there was no model to
> copy. The contract was also scored backwards in review: `WorldPass` was marked deficient while holding
> the *strongest* design in the tree — its descriptor travels with `begin()`, making an undeclared metric
> unrepresentable, which is better than handle-ownership as written here. As of `472fd263` `BackdropPass`
> genuinely owns its six counters, registered at construction — not to satisfy this sentence, but because
> conditional lazy registration made ABSENT indistinguishable from ZERO.
>
> **AFFIRMED — "const-ref views / lightweight slices, never per-frame copies."** This was right, and the
> implementation honoured it: `BackdropPass::Input` is two const references, `WorldPass::Input` six
> references passed by value. No per-frame copy was added anywhere.
>
> **STILL UNBUILT:** `LightingInput` as a named type (LightmapPass satisfies it in substance, with sliced
> `ImageView`/`List` parameters), and `WorldPass` shedding `StarWorldRenderData.hpp` — blocked on
> `TilePainter` and on `EntityDrawables` being defined *inside* that header (#191).

The three blockers become interfaces, and the deepest one becomes an explicit value:

- **Per-pass input DTOs** — `LightingInput` / `BackdropInput` / `WorldInput`, sliced from `WorldRenderData` by the
  orchestrator. **These MUST be const-ref views / lightweight slices, never per-frame copies** (a copy is the one
  way this decomposition could quietly add cost — §6).
- **Injected config + assets** — resolved once and handed to each pass, replacing `Root::singleton()` reads in
  method bodies. Makes passes constructible + unit-testable in isolation, and drops per-frame global lookups.
- **Per-pass telemetry handles** — each pass owns its metric handles (LightmapPass already does — the model),
  ending the string-keyed smear and the typo-misroute.
- **Explicit pass outputs** — e.g. `LightmapResult` (bound lightMap + border + active) returned as a value instead
  of "leave the GL state mutated." Passes hand off through a contract, not a global side-effect; the
  shared-mutable-GL-state bug-class becomes **unrepresentable** ("close by construction, not by care").

Once each pass talks only to `Renderer*` + its DTO, a render layer compiles knowing only its own inputs — which
is the whole point.

## 4. The develop-on-trunk model (why layers are an output)

The clean layer branches cannot be *developed on*, because a buildable `layer2` doesn't exist until the code is
decomposed — and the decomposition IS this work. So: **develop the whole sequence on this buildable branch
(`render/decomposition`, off the go-forward trunk), byte-identical and oracle-gated. The clean, independently
buildable, upstreamable layer branches are then REGENERATED from the decomposed trunk** (via the reorg engine,
with telemetry as the render base and the passes genuinely split) — an output, not a precondition. This resolves
the chicken-and-egg the narrow L2 attempt hit.

## 5. Extraction sequence (each byte-identical · oracle-gated — A8 Gated Ascension)

Each step lands green on its own oracle before the next begins. Each is its own plan + build cycle.

| # | extraction | gate |
|---|---|---|
| 0 | delete `m_worldScreenRect` (dead field, referenced nowhere) | no-op |
| 1 | **RetainedSurface** — env + parallax become two instances (still in WorldPainter) | env / parallax oracle MATCH |
| 2 | **LightmapPass tighten** — pull caller config/input-prep in; emit explicit `LightmapResult` | spread MATCH |
| 3 | **BackdropPass** — sky/parallax/env + the two surfaces + arbiter + compose | env / parallax MATCH |
| 4 | **WorldPass** — the world body extracted as one unit | full-frame render gate |
| 5 | **WorldPainter → thin orchestrator** — DTO-slicing + config injection land as each pass leaves | buildable layers regenerate |

The ~2 ms compose-merge and the per-pass "static scene sips power" floor levers are **enabled by** step 3+ but are
NOT part of this decomposition (byte-identity forbids behavior change); they are separate, ablation-measured
follow-ons (§6).

## 6. What the decomposition buys (honest ledger)

**Banked directly (byte-identical, true on landing):**
- **Comprehensibility / logic density** — the primary win; 870-line god-function → thin orchestrator + focused
  passes each holdable in context (`render()` ~655 → ~250).
- **Robustness** — the shared-mutable-GL-state bug-class is retired by explicit outputs (unrepresentable, not
  fixed). A durable correctness dividend, the "over and above clean interfaces" benefit.
- **Testability** — injected config + DTOs make every pass constructible against a mock `Renderer` → off-GPU unit
  tests (the safety net the L1 extraction never got).
- **Dedup** — real but **narrow**: `RetainedSurface` (two caches = one policy written twice → one class) + minor
  telemetry-handle consolidation. The pass split is *separation*, not dedup — no broad LOC collapse.
- **Tiny incidental perf nudge** — config resolved once (injected) vs `Root::singleton()` every frame per pass.

**Enabled, NOT banked (build + ablation-measure before counting):**
- **Performance** — the decomposition changes zero pixels / zero frame-time (the byte-identity gate). It makes the
  perf levers *composition* instead of god-surgery: the compose-merge falls out of BackdropPass owning both
  surfaces; per-pass static-skip becomes "gate a sovereign pass"; compute-Jacobi / painter's-algorithm land inside
  clean passes. Substrate for the floor-reduction arc — earns none of it until each lever is built + measured.

**The one cost to watch:** DTOs as **copies** would add per-frame allocation — keep them const-ref views.

## 7. Verification

Every step: its own in-frame oracle stays green (env `MATCH/0`, parallax bounded-`MATCH/≤1LSB`, spread `MATCH/0`) +
`game_tests` 90/91 (#146 pre-existing) + `core_tests` + `scripts/render-gate.sh` (env/parallax/spread DIFF=0,
GL_INVALID=0). Assert each oracle *ran* (nonzero MATCH), per the vocabulary-trap lesson. New per-pass **off-GPU
unit tests** ship with each extraction (mock `Renderer` + injected config + DTO fixtures) — the durable asset.
Builds are E-core-pinned and only when the user is OUT of game.

## 8. Axiom scoring (A3 · A8 · A4)

- **Law of One** — gives the module set (the "and" test on the god-object); each module's one-liner has no "and".
- **Air-Gap** — the DTO / inject / per-pass-telemetry / explicit-output fixes *are* compile-independence; the
  buildability blockers were Air-Gap violations. The load-bearing axiom.
- **Composable by Default** — the acceptance test: compose-merge, world-bands, compute-Jacobi all assemble by
  composition without reopening a module. Checkable on paper before building.
- **Earned Exposure** — `Renderer` (L1) is the public/upstream surface; the passes stay internal/sovereign;
  `RetainedSurface` is exposed because a second consumer is coming.
- **Logic Density + faults** — no render graph ("a program, not a subsystem"); `WorldPass` stays one indivisible
  pass. Refuse God-Object Accretion (the thing we're dismantling), Speculative Surface (no generic `Pass` base),
  Ceremony Bloat (no `RetainedSurface` modes neither instance needs).
- **A8 Gated Ascension + Binary Certification** — one extraction at a time, each pass/fail on its oracle.
- **"Close by construction, not by care"** — explicit outputs make the GL-state bug-class unrepresentable, not
  remembered.
- **A4 Zero-Loss** — every rejected approach recorded: render graph (Logic Density); per-layer interface split
  (found non-buildable, this session); develop-on-clean-layer2 (impossible pre-decomposition).

## 9. Out of scope

L3's compose-merge as a *feature*; world-bands (a future third `RetainedSurface` instance); `BackdropPass`'s
single-pass-compiled-parallax alternative (#138 — may later obsolete the parallax cache, which is why the parallax
path stays clean not gold-plated); the painter's-algorithm program (#139); compute-Jacobi (Phase 3, GL 4.3-gated);
and the entire **sim-side** decomposition.
