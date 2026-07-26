# Render Layer 1 — Architecture

> **Naming (R4, 2026-07-20):** the render-surface type was renamed `GlFrameBuffer` → `GlSurface` (it owns 1–2
> faces; it is a surface, not a framebuffer). This doc uses the current name `GlSurface`; historical/upstream
> docs retain `GlFrameBuffer`.

> **Where this sits:** start from the [📇 index](README.md). This document is canonical for the L1 substrate —
> everything *behind* the abstract `Renderer` seam. The L2 primitive and the L3 passes that sit *on* that seam
> are described by [`architecture-3-target-state.md`](architecture-3-target-state.md), whose Air-Gap table is
> generated from the tree and CI-gated. The two chains were written six weeks apart and referenced each other
> zero times until 2026-07-26; where they disagree, the instruments win — see [the audit](axiom-alignment-audit.md).

> **This is the canonical definition of render Layer 1.** Any document that refers to "render Layer 1" —
> [`architecture-assessment.md`](architecture-assessment.md), the unified render-surface subsystem spec
> ([`../superpowers/specs/2026-07-14-render-surface-subsystem-design.md`](../superpowers/specs/2026-07-14-render-surface-subsystem-design.md)),
> or any campaign note — points here for its purpose, scope, intent, and acceptance bar. This document
> supersedes the former `layer1-residuals.md` worklist: all seven residuals are shipped, and that build history
> is preserved in [Appendix B](#appendix-b--how-it-was-built-the-seven-structural-decisions).

> **STATUS: COMPLETE — to the Layer-2/3 bar on both its clauses, after a completeness audit.** A first "done"
> verdict overclaimed; a read-only adversarial re-assessment caught false comments and two still-open bug seats
> (an RB-7 frame-boundary staleness and an RB-1 sibling-alias, both aimed at the Layer-2 retained surfaces).
> Those were fixed and re-certified, the comments corrected, and the grep-rules converted to type-rules where
> byte-identity allowed. Now: the four render-boundary corruptions (RB-1/5/6/7) are closed by construction —
> save one *irreducible* by-care residue that A3's Air-Gap forbids folding away (§4.2 item 3); the ten checkable
> conditions ([§7](#7-the-acceptance-bar--the-checkable-conditions)) hold under their corrected, module-scoped
> definitions; and the substrate is certified byte-identical on hardware (render gate env/parallax/spread
> DIFF=0, GL_INVALID=0; `core_tests` 226/226; `game_tests` 90/91, the lone failure pre-existing — #146). Shipped
> to `dev/upstream-merge`. The honest verdict and remaining caveats are [§8](#8-honest-status--the-remaining-caveats).

---

## 1. Purpose — why Layer 1 exists

Render Layer 1 is the **render-surface substrate** inside the OpenGL backend: the mechanism that owns GPU
render surfaces (framebuffers), the registry of which surfaces exist, the bind that couples an effect program
to a target, and the compiled effect programs themselves. Before the decomposition it was undifferentiated mass
inside the `OpenGlRenderer` god-object.

It exists to serve two goals — one corrective, one enabling.

**(a) Make a class of bug inexpressible.** The god-object hosted a whole family of corruption with one shape:
*two CPU-side facts about one GPU thing disagreed, because one fact had a single writer and several silent
mutators.* Four live instances were found and fixed on hardware (RB-1/5/6/7 — see [§3](#3-intent--the-axioms-and-the-one-rule)).
The point of the decomposition is not to fix those four bugs — it is to make the *shape* structurally
impossible: give every fact exactly one sovereign owner, written by the act that changes the thing the fact
describes, so no second mutator exists to leave the record stale.

> **Attribution note** (per the [2026-07-18 comparative assessment](layer1-vs-vanilla-assessment.md)): this
> bug-shape closure is delivered by the **sealing** — private descriptor fields, the `m_owned` bit, the
> self-recording chokepoints — which is *separable* from the four-component **extraction**. A nested-but-sealed
> monolith closes the same shape; the extraction itself buys module sovereignty (goal (b)), not the bug closure.
> Throughout this document "decomposition" means the **whole arc — sealing *and* extraction** — not the file
> split alone. See the assessment §2 for the honest vs-upstream / vs-monolith split.

**(b) Turn the substrate into composable primitives.** The floor-reduction feature arc — retained caches
(environment / parallax / world), refresh gates, sovereign render passes — is being built *on top of* this
substrate. If the substrate stays a god-object, each of those features is hand-rolled inside it, re-deriving the
same size/format/bind logic and re-opening the same bug class. Made sovereign, the same features become
**composition over primitives you can trust.** This is the **Foundation-of-Sand** principle: Layers 2 and 3 are
not neighbours of Layer 1 — they are *built on it*, so a merely-adequate Layer 1 is a fault every layer above is
obliged to route around.

**What Layer 1 does NOT change: any rendered pixel.** The entire value is structural — a decomposition
certified byte-for-byte identical. "Better output than vanilla" was never the goal. "The same output, from code
in which the bug class cannot recur" is the whole goal.

## 2. Scope

### In Layer 1

Four sovereign components, one shared texture primitive, and two abstract seams the GL backend implements.
Each is the **single owner of one fact**, and each one-line responsibility contains no "and":

| Component | The one fact it owns | Lives in |
|---|---|---|
| **`GlSurface`** | one render surface — its one or two faces, and their storage | `StarGlRenderSurface` |
| **`GlTargets`** | which surfaces exist, and the generation counter | `StarGlRenderSurface` |
| **`GlPass`** | the bind — the coupled `(effect, target)` pair, and the flattened locations the draw path reads | `StarGlRenderSurface` |
| **`GlEffects`** | the compiled programs and the scriptable-parameter surface | `StarGlRenderSurface` |
| **`GlLoneTexture`** | the storage descriptor (`textureSize` + `internalFormat`) of a standalone texture | `StarGlTexturePrimitives` |
| **`TextureAtlasSet`** | where a texture lives inside an atlas, independent of any graphics API | `StarTextureAtlas` |
| **`GpuTimer` / `RenderOracle`** | the GPU-side measurement seam the backend implements | `StarRenderDiagnostics` |

**The last two rows joined on 2026-07-26**, and the reason is worth recording because it is the second
instance of one failure. Both were in the build, claimed by no layer, and therefore invisible to every
count `render-inventory.py` produced — the identical situation #137 found for three helpers in
`source/rendering`. They survived a second time because the instrument's unassigned-file check only ever
globbed `source/rendering`, the directory the *first* instance was found in. It now covers
`source/application` too, via an explicit partition of that mixed directory into render and not-render,
so a third instance is reported rather than absorbed.

Claiming them moves L1 from 3796 to 4291 lines and moves **no** coupling metric at all: both carry zero
`Root::singleton()` reads, zero GL calls and zero telemetry handles, so the Air-Gap residual is still 17.
`StarRenderDiagnostics` does name `OpenGlRenderer` and `Telemetry::`, but only in comments explaining
what it deliberately does *not* depend on — which is why both instruments strip before they grep. Both
files are now inside the `layer1_layering` fence, so it holds six files rather than four.

### Explicitly NOT Layer 1

Each of these is a deliberate scope ruling, not an omission:

- **The `Renderer` interface split.** A separate axis — contract vs mechanism. Verdict: **do not split.** The
  full analysis is [§6](#6-scope-boundary--the-renderer-interface-is-not-part-of-layer-1).
- **Retained-cache policy** — validity, refresh gates, integer scroll-shift, the dirty oracle. That is
  **Layer 2** (`RetainedSurface`): a Surface *plus policy*.
- **Render passes** — backdrop, lightmap, world, compose. That is **Layer 3**: sovereign passes that *own* a
  sequence of Layer-1 operations.
- **The texture atlas** (`GlTextureAtlasSet` / grouped textures). A different allocation contract; it holds no
  lone-texture descriptor and never feeds an effect sampler's same-size/same-format fast path. Deliberately not
  folded into the lone-texture primitives.
- **The render oracle** (`GlRenderOracle`). Diagnostic scaffolding for byte-identity certification, not a
  production component. Its transient multisample-resolve scratch texture is likewise carved out of every
  substrate invariant. Expected lifetime: deleted.

## 3. Intent — the axioms and the one rule

Layer 1 answers to two governing axioms.

**A3 — Sovereign Composition.** Four sub-principles, each with a concrete Layer-1 meaning:

- **Law of One** — one owner per fact. `specifyStorage` is the only writer of a face's storage descriptor;
  `GlPass::bindTarget` the only writer of the draw framebuffer on the draw path; `sizeFor()` the only place the
  screen→surface size rule is written.
- **Air-Gap** — nothing reaches past a component's interface into its internals. There are **zero `friend`**
  declarations: `GlSurface::faces`, `GlTargets::m_byId`, `GlEffects::m_byName`, and `GlPass::boundViewport`
  are unreachable by the *compiler*, not by convention.
- **Earned Exposure** — a component exposes only what a consumer needs. The `Renderer` interface hands its
  twelve consumers three texture methods and no way to name a framebuffer face.
- **Local Reasoning** — each component is correct without knowing the whole renderer. "Correct-by-ordering" and
  "correct-by-convention" are precisely the properties the design refuses to rely on.

**A8 — Close by construction, not by care.** A bug class is closed only when the bad state is *unrepresentable*
(the compiler rejects it) or *structurally unreachable* (an invariant makes it so) — never when it merely
requires a discipline someone must remember to keep.

### The one rule

> **A descriptor must be written by the act it describes.**

This is the direct answer to the defect the layer exists to kill. RB-1, RB-5, RB-6 and RB-7 were the **same
defect in four seats** — a record with one writer and several mutators:

| | the record | who wrote it | who else changed the thing it described |
|---|---|---|---|
| RB-1 | a target's storage | `specifyStorage` | three effect upload setters, through an aliased `RefPtr` |
| RB-5 | "which target am I sampling" | `setEffectTextureFromTarget` | `loadConfig`, by destroying every target |
| RB-6 | the texture's storage format | `setEffectTextureHalf` | the other three storage-spec paths |
| RB-7 | `m_pass.target` | `bindTarget` | `loadConfig`, by destroying every target |

Every structural decision in Layer 1 exists to keep that fourth column empty.

## 4. Why it is built the way it is

The specific shape — four components, this set of invariants, a byte-identity contract verified by an oracle —
is not incidental. Each choice discharges one part of the purpose.

### 4.1 Why these four seams

The decomposition follows the facts, not the file. Each component is drawn around exactly one thing that must
have a single owner:

- **`GlSurface`** owns *storage*. A surface is one or two faces; `specifyStorage` is the only writer of a
  face's descriptor, and `sizeFor()` the only writer of its size rule. Adding a face, resizing, swapping, or
  changing the format ladder is a one-site edit with a compiler-visible owner.
- **`GlTargets`** owns *identity and lifetime* — which surfaces exist and the generation number that lets
  observers notice a rebuild. Surfaces are reached by name through `find()`; nothing external holds the set open.
- **`GlPass`** owns *the bind* — the coupled `(effect, target)` decision and the flattened attribute/uniform
  locations the hot draw path reads. It is the sole writer of `GL_DRAW_FRAMEBUFFER` on the draw path.
- **`GlEffects`** owns *the compiled programs* and the author-declared scriptable surface.

### 4.2 The load-bearing invariants, and how each is held **by construction**

This is the A8 ledger — the reason the bug class cannot recur, stated per invariant:

1. **The storage descriptor is written only by the act that specifies it — and cannot be poked.** Storage
   specification and descriptor recording are one indivisible operation, and no setter writes its own
   same-size/same-format guard: two self-recording chokepoints do it (`uploadLoneStorage` owns *the* guard for
   the numeric paths; `uploadTextureImage` records via a `GlLoneTexture*` out-param), and `createEmptyGlTexture`
   is the allocator carve-out (`internalFormat` stays the `0` sentinel no guard can match). The descriptor
   fields (`textureSize`, `internalFormat`) are **private**, changed only through `recordStorage()` /
   `setAllocatedSize()`, so a raw field poke can no longer half-record them. This closes **RB-6**. *Honest
   limit:* "recorded beside the spec" is still co-location by convention — `glTex*Image2D` are free globals a
   future path could call directly — the byte-identity-bounded residual named in §7 condition #6 / Appendix C.
2. **A borrow is set as one atomic act, on private state.** The borrow trio (the texture, whether we adopted
   it `m_owned`, and which named target to re-point to) is **private**, changed only by `adopt` / `share` /
   `release`, so no call site can poke one field and desync it. `ownsWritableStorage()` trusts `m_owned` (set
   only by `adopt`), not the target name — because `adopt(tex)` and `share(tex, "")` both leave the name empty
   yet mean opposite things (ours vs owned-elsewhere), and deriving writability from the name alone let
   `setEffectTextureAlias` of a non-borrowed source re-specify the source's live texture through the alias.
   This closes **RB-1** and its sibling-alias seat, by the compiler.
3. **A rebuild invalidates every view of what it rebuilt — some by construction, some irreducibly by care.**
   `GlTargets` carries a generation counter; retained surfaces and samplers re-point on a bump. The pass bind
   cache is dropped wherever GL_DRAW changes outside the pass: `loadConfig` (before `destroyAll`) and — the fix
   the audit forced — `startFrame` (which clears faces and raw-binds 0 at the frame boundary, so a frame ending
   with a non-screen target still live cannot leave the next frame's first bind trusting a stale cache; the
   exact hazard aimed at the Layer-2 retained surfaces). **Honest residue:** the two cross-component teardown
   notifications — `rebindBorrows` (RB-5) and the pass's `invalidate()` (RB-7) — are hand-ordered companion
   calls to `destroyAll` in `loadConfig`. A3's Air-Gap *forbids* folding them into `GlTargets::destroyAll`
   (targets may not reach into effects or the pass), so this residue is by care, and it is **irreducible under
   sovereignty** — a consequence of the seal, not a shortcut left in it.
4. **Existence *is* doubledness, and a face owns its FBO.** A `GlSurface` is `Face front; Maybe<Face> back;`
   — it has a second face iff `back` is engaged, with no separate `bool doubled` to disagree (this closed the
   `altId` bug). And a `Face` now RAIIs its framebuffer object (destructor deletes it, move hands it off, copy
   deleted), so `makeDoubled()` — which builds the second face into a LOCAL — leaks nothing even if
   `allocateFace` throws after `glGenFramebuffers`: the local reclaims the FBO on unwind. Both by construction.
5. **The bind key is `(target, write-face index, size)`.** The pass rebinds exactly when what it is bound to
   changes; a `swap()` moves the face index, which changes the key and forces the rebind *by itself* — there is
   no `justSwapped` bool, no cover `glViewport`. The screen is a representable (null) target, so no site
   hand-rolls a screen bind.
6. **A framebuffer face carries only a completeness floor.** Sampling (MIN/MAG/WRAP) is owned by the *binding*
   and re-specified per draw; the face records nothing about it and no longer contradicts its own GL state.
7. **Load-time constants are derived once, at load, into fields.** `frameBuffer` / `blitFrameBuffer` /
   `frameBufferTextures` / `doubleBuffered` / the HDR bit are parsed once in `GlEffects::load` and
   `GlSurface`'s constructor. `switchEffectConfig` re-hashes zero JSON; `specifyStorage` re-parses nothing
   per resize.
8. **Fixed locations are resolved once, at load.** Attribute and uniform locations become `GLint` fields on
   `Effect`; `GlPass::bindEffect` is field copies, not per-bind string lookups.

### 4.3 Why byte-identity is the correctness contract

Because the entire value is structural, the correctness contract is *the output must not change*. A
decomposition that altered a pixel would have traded a real bug class for a real regression. So every commit is
certified **byte-identical**.

### 4.4 Why the verification is an in-process oracle, not a golden hash

A cross-run golden hash is unusable here: the unpaused load phase lets the simulation diverge (entity counts
differ run to run), so two runs of "the same" scene are not comparable. The **render gate** instead boots
offscreen on the real GPU, freezes the world, and runs an **in-process A/B of two code paths within one frame**
— the env / parallax / spread oracles compare cached-vs-freshly-drawn (or path-A-vs-path-B) pixels in the same
frame, immune to sim divergence. `DIFF=0` across all three, with zero `GL_INVALID`, is the gate. Item that can
move a pixel carries its own oracle run on its own commit.

## 5. The layer scheme, and the two senses of "Layer 1"

Layer 1 is the bottom of a three-layer render-surface architecture:

- **Layer 1 — mechanism.** The surface substrate defined by this document. Owns storage, identity, the bind,
  the programs. Knows nothing about *why* a surface is refreshed.
- **Layer 2 — policy** (`RetainedSurface`). A Surface *plus* a validity/refresh gate and optional scroll-shift.
  The environment and parallax caches are instances.
- **Layer 3 — sovereign passes.** Backdrop, lightmap, world: each *owns* a sequence of Layer-1/2 operations
  behind a stable seam that alternative pass architectures can be swapped in behind.

**A naming caution.** "Layer 1" is used in two related but non-identical senses, and this document reconciles
them:

1. **The extraction Layer 1 (this document, current code):** the *four* components `GlSurface` /
   `GlTargets` / `GlPass` / `GlEffects`. This is what exists and is certified today.
2. **The design Layer 1 = `Surface` (forward design):** in the unified render-surface subsystem spec, "Layer 1"
   is a single `Surface` abstraction that generalises `GlSurface` and subsumes upstream #542 declarative
   double-buffering. That spec is a *not-yet-approved forward design*; its `Surface` is the intended
   *evolution* of extraction-Layer-1's `GlSurface`, not a synonym for the current four-component substrate.

When a document says "Layer 1" without qualification, it means sense 1 (this substrate). The forward design
owns the word only inside its own spec.

## 6. Scope boundary — the `Renderer` interface is not part of Layer 1

**A separate axis, and the verdict on that axis is DON'T SPLIT. The question is closed.**

1. **Decisive:** decomposing the implementation into `GlSurface` / `GlTargets` / `GlPass` / `GlEffects`
   required **zero changes to `StarRenderer.hpp`**. Contract and mechanism are orthogonal. Layer 1 is the
   mechanism axis, by demonstration.
2. **One implementer.** `OpenGlRenderer` is the only `public Renderer` in the tree. No mocks, no second backend.
3. **`Renderer` is not a working backend boundary today anyway.** The frame lifecycle
   (`setScreenSize`/`startFrame`/`finishFrame`) is not virtual and not on the interface, so `SdlPlatform` holds
   the concrete `OpenGlRendererPtr`. What `Renderer` actually is — and does well — is a **compile firewall**:
   zero GL headers reach `rendering/`, `frontend/`, `windowing/`, `game/`. One header does that; five would not
   do it one byte better.
4. **No consumer needs all the virtuals, but every virtual has a real caller** — there is zero Speculative
   Surface in the interface to delete. The only clean cleavage planes are single-consumer *and*
   single-implementor, which is exactly the condition under which a narrower contract prevents no bug, removes no
   duplication, and unblocks nothing.

Splitting it would hand a future backend five bases to implement instead of one — harder, not easier. **The
live risk is the interface GROWING, not its width. Police that instead.**

## 7. The acceptance bar & the checkable conditions

The bar is not "better than vanilla." It is the standard set for Layers 2 and 3:

> *Every architectural comment in the file is true of the code, and the bug classes we closed are closed by the
> compiler rather than by our care.*

"Nailed" is not a taste judgement. It is a set of mechanical checks — worded against the **module**, not one
translation unit (the components were extracted into `StarGlRenderSurface.*`, so a grep pinned to
`StarRenderer_opengl.*` measures the wrong file; the first cut of this list made exactly that error):

1. **No `friend` anywhere in the module.** All four components are top-level classes with private members; no
   `friend` *declaration* exists in `StarGlRenderSurface.hpp` or `StarRenderer_opengl.hpp` (only prose).
2. **`glViewport` appears only in `GlPass::bindTarget` (the draw path) and `OpenGlRenderer::setScreenSize`.**
   `justSwapped` does not exist.
3. **`GlPass::bindTarget` is the ONLY writer of `GL_DRAW_FRAMEBUFFER` on the draw path, and its cache is
   honest.** The non-draw writers — `clearFaces`, `startFrame`/`finishFrame`'s screen bind, the blit, the
   oracle — either restore what they found (blit, oracle) or are followed by `m_pass.invalidate()` that drops
   the cache (`clearFaces` runs inside `startFrame`, which now invalidates). So no draw trusts a cache GL has
   moved out from under. *(This was a fake-green before the frame-boundary fix: it counted the write-side while
   the RB-7 invariant lives on the read-side early-out.)*
4. **Zero `Json` in `switchEffectConfig`.** `Effect::config` and `GlSurface::config` do not exist as members.
5. **One allocator per side.** `glGenTextures` on a managed texture appears in exactly three functions:
   `GlSurface::allocateFace` (`StarGlRenderSurface.cpp`), `createEmptyGlTexture` and
   `GlTextureAtlasSet::createAtlasTexture` (`StarRenderer_opengl.cpp`). (The oracle's transient resolve scratch
   is diagnostic, outside the set.)
6. **The storage-descriptor chokepoint.** *(Re-derived — see [Appendix C](#appendix-c--condition-6-why-it-was-re-derived).)*
   Every `glTexSubImage2D` on a `GlLoneTexture` is inside `uploadLoneStorage`; every storage-defining
   `glTexImage2D` on one is inside `uploadLoneStorage`, `uploadTextureImage`, or `specifyStorage`; the descriptor
   fields are **private**, written only by `recordStorage` / `setAllocatedSize`. Spec, record, guard: one act.
7. **The seal predicate appears once.** Zero inline copies of the "may I write?" test; `hasStorage()` and
   `ownsWritableStorage()` are the two named forms, over **private** borrow state.
8. **The size rule appears once.** No `screenSize /` arithmetic outside `sizeFor()`.
9. **The effect-parameter type-name ladder appears once** (`parseEffectParameter`).
10. **Every architectural comment is true of the code** (verified after the false-comment cleanup). Each
    component's responsibility is singular, or its "and" is a *defended coupling* stated as such: `GlPass` binds
    one effect **and** its coupled target because GL sets the current program and draw framebuffer together;
    `GlEffects` owns the compiled programs **and** the scriptable shadow that rides on them, one registry.

Plus: all three GPU pixel oracles MATCH (`DIFF=0`); every commit that could move a pixel carried its own oracle
run. All checks hold as of the tier-3 hardening (git log `L1 harden`).

## 8. Honest status & the remaining caveats

Layer 1 is finished to the Layer-2/3 bar on both clauses the Director named — **now**, after a completeness
audit corrected an earlier overclaim. A first "10/10, done" verdict was wrong on both clauses, and a read-only
adversarial re-assessment caught it: several architectural comments were false, and two flagship bug seats — an
RB-7 frame-boundary cache staleness and an RB-1 sibling-alias — were still open and pointed straight at the
Layer-2 retained surfaces. Both were fixed and re-certified byte-identical; the false comments were corrected;
and the grep-rules were converted to type-rules where byte-identity allowed (private borrow trio, private
storage descriptor, RAII face FBO). The honest state:

- **Clause (a) — every comment true: met.** The false-comment cluster (a `friend` cross-reference to friends
  that no longer exist, a `[class.access.nest]` justification for classes that are no longer nested, a
  `bindEffect` precondition it establishes itself, stale `faces[]` nomenclature) is fixed. The two "and"
  one-liners are defended couplings, stated (condition #10).
- **Clause (b) — closed by the compiler, not our care: met, with one irreducible residue.** RB-1 (borrow
  atomicity + sibling alias), RB-6 (descriptor raw-poke), the `makeDoubled` FBO leak, the RB-7 frame-boundary
  staleness, and the bind key are structural. The **irreducible residue** is RB-5/RB-7's cross-component
  teardown notifications (`rebindBorrows` / `invalidate`): A3's Air-Gap forbids folding them into
  `GlTargets::destroyAll`, so they stay hand-ordered companion calls in `loadConfig` — by care, but *forced by
  sovereignty*, and documented as such rather than marked "closed" (§4.2 item 3).

The remaining caveats, stated rather than buried:

- **The descriptor chokepoint is compiler-sealed against a field poke, but co-location is still by convention.**
  `glTex*Image2D` are free globals a future path could call directly (and then call `recordStorage`, or not).
  Private fields close the raw-poke seat; "recorded beside the spec" is the byte-identity-bounded residual
  (Appendix C). Making it truly unrepresentable needs a `GlLoneTexture`-owned upload method, which the shared
  atlas caller and the `glPixelStorei` alignment co-location forbid at DIFF=0. Stopped at the strongest form.
- **`GlPass` caches a raw non-owning `Effect*` into `GlEffects`' `m_byName` storage** — kept valid by the
  co-located rebind-after-`load()` in `loadEffectConfig`. This is by-care (the header warns "the reference dies
  at the next `load()`"), byte-identity-bounded, **reducible** (making it a RefPtr borrow closes it by
  construction — backlog item 4), and pre-existing in the monolith (it was `m_currentEffect`). Not one of the
  sovereignty-*irreducible* residues; a code-quality choice, disclosed here for completeness.
- **Correctness evidence is the render-gate oracle plus a first set of standalone unit tests.**
  `render_surface_tests` (9 cases, `source/test/render_surface_test.cpp`, the `render_surface_tests` ctest) now
  construct `EffectTexture` and `GlLoneTexture` in isolation — with no GL context — and assert the RB-1
  borrow-trio (`adopt`/`share`/`release`, including the empty-name sibling-alias seal) and the RB-6
  storage-descriptor invariants directly. These are the first tests to exercise a Layer-1 component standalone,
  and they are only possible *because* the module is sovereign. Still NOT unit-tested: the GL-calling components
  (`GlSurface` / `GlTargets` / `GlPass` / `GlEffects`), which allocate via direct GL calls needing a context
  the headless harness lacks; those remain covered by the render gate alone.
- **Byte-identity is certified for one config of one frozen world** (AA off, HDR on). The render oracle cannot
  compare a multisample target by construction, so the AA path — RB-5's home — is verified by reasoning, not by
  the gate. The AA/HDR/double-buffer permutation matrix is asserted, not exercised.
- **One permanently-divergent file from upstream**, paid at each upstream merge (~4×/year), consciously.

## 9. Ongoing perfection

Layer 1 is complete, not frozen. Both faces of it are held to the same bar:

- **The substrate (code):** any future change re-runs the render gate + `core_tests` + `game_tests` and must
  stay byte-identical (or carry its own oracle if it can move a pixel). The checkable conditions in §7 are a
  standing lint, not a one-time gate.
- **The definition (this document):** it is the canonical reference. When the substrate changes, this document
  changes with it — a comment or invariant that stops being true of the code is the same defect here as in the
  source.

---

## Appendix A — Rejected designs (do not re-propose)

Recorded so they are not re-proposed. Produced by a 120-agent audit in which every proposal faced three
independent skeptics; **25 of 38 proposals died here.**

- **Renaming `GlSurface` → `Surface`.** Zero bugs made impossible. The "texture-centric, not FBO-centric"
  claim is carried by the resolver (`writeFace()` / `readFace()`), not by the name. *(The forward design revives
  `Surface` as a genuinely new abstraction — that is a different proposal; see [§5](#5-the-layer-scheme-and-the-two-senses-of-layer-1).)*
- **Merging `parameters` and `scriptables`.** They are a **trust boundary**: `scriptables` is the
  author-declared allowlist enforced against mod Lua; `parameterValue` means *GPU-state shadow* in one and *CPU
  desired-value* in the other. Merging would let Lua poison the shadow and pin an engine uniform to a stale
  value.
- **A shared dedup/type-check core between `applyEffectParameter` and `GlEffects::setScriptable`.** Same shape,
  different contract — one elides a flush and a `glUniform` against a bound program; the other elides an
  assignment against no program at all. Merging re-couples `GlEffects` to the renderer's upload path. Ceremony.
- **Standalone deletion of the `vp == 0` fallback, the `screenSize` effect parameter, or the `bindTarget`
  forwarder.** All were riders on the bind-key rewrite; deleting and re-adding is churn. (The "latent
  `overrideSize` wrong-viewport bug" once told about the `vp == 0` branch is **false** — it could not produce a
  value different from the one it replaced.)

## Appendix B — How it was built (the seven structural decisions)

The substrate was extracted and hardened over a sequence of byte-identical commits. Each closed one structural
gap; together they took Layer 1 from "no longer the problem" to the Layer-2/3 bar. Referenced by symbol, since
line numbers drift.

1. **One seal predicate, one effect-side allocator.** Three verbatim copies of the "may I write here?" test
   became `hasStorage()` / `ownsWritableStorage()`; the twice-written empty-texture allocator became
   `createEmptyGlTexture`, which also gained the `textureId == 0` throw its inline copies lacked.
2. **The `(target, face, size)` bind key.** Retired `justSwapped`, the `vp == 0` fallback, and
   `setRenderTarget`'s cover `glViewport`; the screen became a representable null target.
3. **Zero `friend`.** All three grants were dead (nobody took them) and were removed; `faces[]` / `m_byId` /
   `m_byName` became compiler-unreachable.
4. **Both retained `Json config` members deleted.** `Effect`'s and `GlSurface`'s config Json were parsed
   once into fields; `switchEffectConfig` went zero-JSON; a malformed-config throw moved from mid-frame to load.
5. **The framebuffer face is a completeness floor, not a sampling config.** The inert `textureFiltering` config
   reads and the four asset keys were deleted; the face stopped contradicting its own GL sampling state.
6. **One effect-parameter type ladder** (`parseEffectParameter`), deriving `parameterType` from the parsed
   value so the type and its default cannot skew.
7. **`Effect::attributes` / `uniforms` deleted.** The location cache that served one already-caching function
   became fixed `GLint` fields resolved once at load.

Two more structural moves rode alongside: **`Maybe<Face>`** (existence *is* doubledness — §4.2 item 4), and the
**storage-descriptor chokepoint** (Appendix C), which re-derived condition #6.

## Appendix C — Condition #6: why it was re-derived

The original condition #6 ("`->textureSize =` and `->internalFormat =` each resolve to a single function per
side") was the wrong invariant, in two ways, and a 9-agent read-only re-evaluation (source enumeration +
RB-6/RB-7 intent + adversarial critique) established the real one:

- It was **unreachable** on the effect side — the upload paths carry irreducibly different formats (arbitrary
  `PixelFormat` image, half-float, R8), so "one function" would relocate the format switch, not remove a writer.
- It **measured the wrong half.** RB-6 detonates on the *read* side — a `glTexSubImage2D` fast-path guard
  trusting a stale descriptor — which a write-side single-writer never touches. A future setter copy-pasted with
  a size-only guard (verbatim R8's original bug) would pass "one writer" and still reopen RB-6.

The real invariant is co-location plus a single guard: *no path specifies a `GlLoneTexture`'s storage without
recording its whole descriptor, and no setter writes its own SubImage guard — the guard, the spec, and the
record are one indivisible act.* It is realised byte-identically by the two chokepoints in §4.2 item 1. The
honest limit (free-global bypass) is in §8.

**The lesson, kept:** a checkable condition is itself a claim that can be wrong. #6 was green-able while the
property it named was violable. When a condition can be satisfied without the guarantee it stands for, fix the
condition, not the checkbox.
