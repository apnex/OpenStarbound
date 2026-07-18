# Render Layer 1 — Architecture

> **This is the canonical definition of render Layer 1.** Any document that refers to "render Layer 1" —
> [`architecture-assessment.md`](architecture-assessment.md), the unified render-surface subsystem spec
> ([`../superpowers/specs/2026-07-14-render-surface-subsystem-design.md`](../superpowers/specs/2026-07-14-render-surface-subsystem-design.md)),
> or any campaign note — points here for its purpose, scope, intent, and acceptance bar. This document
> supersedes the former `layer1-residuals.md` worklist: all seven residuals are shipped, and that build history
> is preserved in [Appendix B](#appendix-b--how-it-was-built-the-seven-structural-decisions).

> **STATUS: COMPLETE.** The four render-boundary corruptions (RB-1/5/6/7) are closed; all ten checkable
> conditions ([§7](#7-the-acceptance-bar--the-checkable-conditions)) are green; the whole substrate is certified
> byte-identical on hardware (render gate env/parallax/spread DIFF=0, GL_INVALID=0; `core_tests` 226/226;
> `game_tests` 90/91, the lone failure pre-existing and unrelated — `ItemTest.ItemComparison`, #146). Shipped to
> `dev/upstream-merge`. The honest completeness verdict, including its remaining caveats, is
> [§8](#8-honest-status--the-remaining-caveats).

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

Four sovereign components plus one shared texture primitive. Each is the **single owner of one fact**, and each
one-line responsibility contains no "and":

| Component | The one fact it owns | Lives in |
|---|---|---|
| **`GlFrameBuffer`** | one render surface — its one or two faces, and their storage | `StarGlRenderSurface` |
| **`GlTargets`** | which surfaces exist, and the generation counter | `StarGlRenderSurface` |
| **`GlPass`** | the bind — the coupled `(effect, target)` pair, and the flattened locations the draw path reads | `StarGlRenderSurface` |
| **`GlEffects`** | the compiled programs and the scriptable-parameter surface | `StarGlRenderSurface` |
| **`GlLoneTexture`** | the storage descriptor (`textureSize` + `internalFormat`) of a standalone texture | `StarGlTexturePrimitives` |

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
  declarations: `GlFrameBuffer::faces`, `GlTargets::m_byId`, `GlEffects::m_byName`, and `GlPass::boundViewport`
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

- **`GlFrameBuffer`** owns *storage*. A surface is one or two faces; `specifyStorage` is the only writer of a
  face's descriptor, and `sizeFor()` the only writer of its size rule. Adding a face, resizing, swapping, or
  changing the format ladder is a one-site edit with a compiler-visible owner.
- **`GlTargets`** owns *identity and lifetime* — which surfaces exist and the generation number that lets
  observers notice a rebuild. Surfaces are reached by name through `find()`; nothing external holds the set open.
- **`GlPass`** owns *the bind* — the coupled `(effect, target)` decision and the flattened attribute/uniform
  locations the hot draw path reads. It is the sole writer of `GL_DRAW_FRAMEBUFFER` on the draw path.
- **`GlEffects`** owns *the compiled programs* and the author-declared scriptable surface.

### 4.2 The load-bearing invariants, and how each is held **by construction**

This is the A8 ledger — the reason the bug class cannot recur, stated per invariant:

1. **The storage descriptor is written only by the act that specifies it.** Storage specification and
   descriptor recording are one indivisible operation, and no setter writes its own same-size/same-format
   guard. Two self-recording chokepoints do it: `uploadLoneStorage` (owns *the* guard for the numeric upload
   paths; `setEffectTextureHalf`/`R8` route through it) and `uploadTextureImage` (records the descriptor itself
   via a `GlLoneTexture*` out-param). `createEmptyGlTexture` is the documented allocator carve-out — it writes
   only `textureSize`; `internalFormat` stays the `0` sentinel that no guard can match, so a half-recorded
   texture cannot reach a fast path. This is what closed **RB-6** for good.
2. **A borrow is set as one act.** `adopt` / `share` / `release` move a texture and its borrow-status together;
   `ownsWritableStorage()` refuses to re-specify storage the sampler is only borrowing. An effect upload setter
   therefore cannot write through an aliased framebuffer face. This closed **RB-1**.
3. **A rebuild invalidates every view of what it rebuilt.** `GlTargets` carries a generation counter; retained
   surfaces and samplers re-point on a bump, and the pass is invalidated before `destroyAll`. No holder keeps a
   destroyed FBO alive to be sampled or re-bound. This closed **RB-5** and **RB-7**.
4. **Existence *is* doubledness.** A `GlFrameBuffer` is `Face front; Maybe<Face> back;`. It has a second face
   iff `back` is engaged — there is no separate `bool doubled` that could disagree. `makeDoubled()` builds the
   second face in a local and `emplace`s `back` only once it is fully allocated, so a throw leaks nothing and
   there is no instant at which `doubled()` is true over half-formed storage. This closes, by construction, the
   `altId` face-leak that vanilla and an earlier draft both carried.
5. **The bind key is `(target, write-face index, size)`.** The pass rebinds exactly when what it is bound to
   changes; a `swap()` moves the face index, which changes the key and forces the rebind *by itself* — there is
   no `justSwapped` bool, no cover `glViewport`. The screen is a representable (null) target, so no site
   hand-rolls a screen bind.
6. **A framebuffer face carries only a completeness floor.** Sampling (MIN/MAG/WRAP) is owned by the *binding*
   and re-specified per draw; the face records nothing about it and no longer contradicts its own GL state.
7. **Load-time constants are derived once, at load, into fields.** `frameBuffer` / `blitFrameBuffer` /
   `frameBufferTextures` / `doubleBuffered` / the HDR bit are parsed once in `GlEffects::load` and
   `GlFrameBuffer`'s constructor. `switchEffectConfig` re-hashes zero JSON; `specifyStorage` re-parses nothing
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

1. **The extraction Layer 1 (this document, current code):** the *four* components `GlFrameBuffer` /
   `GlTargets` / `GlPass` / `GlEffects`. This is what exists and is certified today.
2. **The design Layer 1 = `Surface` (forward design):** in the unified render-surface subsystem spec, "Layer 1"
   is a single `Surface` abstraction that generalises `GlFrameBuffer` and subsumes upstream #542 declarative
   double-buffering. That spec is a *not-yet-approved forward design*; its `Surface` is the intended
   *evolution* of extraction-Layer-1's `GlFrameBuffer`, not a synonym for the current four-component substrate.

When a document says "Layer 1" without qualification, it means sense 1 (this substrate). The forward design
owns the word only inside its own spec.

## 6. Scope boundary — the `Renderer` interface is not part of Layer 1

**A separate axis, and the verdict on that axis is DON'T SPLIT. The question is closed.**

1. **Decisive:** decomposing the implementation into `GlFrameBuffer` / `GlTargets` / `GlPass` / `GlEffects`
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

"Nailed" is not a taste judgement. It is **ten greps and three oracles**, each mechanically checkable:

1. `grep -c "friend" StarRenderer_opengl.hpp` → **0**.
2. `grep -c "glViewport" StarRenderer_opengl.cpp` → **2** (`setScreenSize` + the forwarder); the draw-path
   `glViewport` lives only in `GlPass::bindTarget`. `justSwapped` → **0**.
3. Exactly **one** writer of `GL_DRAW_FRAMEBUFFER` on the draw path (`GlPass::bindTarget`). `clearFaces`, the
   blit and the oracle are the only exceptions, and each **restores what it found**.
4. **Zero `Json` in `switchEffectConfig`.** `Effect::config` and `GlFrameBuffer::config` do not exist as members.
5. **One allocator per side.** `glGenTextures` on a managed texture appears in exactly three places:
   `GlFrameBuffer::allocateFace`, `createEmptyGlTexture`, `GlTextureAtlasSet::createAtlasTexture`. (The oracle's
   transient resolve scratch is diagnostic, outside the set.)
6. **The storage-descriptor chokepoint.** *(Re-derived — the original "one writer per side" grep was the wrong
   invariant; see [Appendix C](#appendix-c--condition-6-why-it-was-re-derived).)* Every `glTexSubImage2D` on a
   `GlLoneTexture` is inside `uploadLoneStorage`; every storage-defining `glTexImage2D` on one is inside
   `uploadLoneStorage`, `uploadTextureImage`, or `specifyStorage`; `->internalFormat =` resolves to exactly
   those three; no setter writes its own SubImage guard. So spec, record, and the sub-vs-re-spec decision are
   one indivisible act.
7. **The seal predicate appears once.** Zero inline copies of `textureId == 0 || … borrowed()`; `hasStorage()`
   and `ownsWritableStorage()` are the two named forms.
8. **The size rule appears once.** No `screenSize /` arithmetic outside `sizeFor()`.
9. **The effect-parameter type-name ladder appears once** (`parseEffectParameter`).
10. Every component's one-line description contains **no "and"**, and every architectural comment in the file is
    true of the code as it stands.

Plus: all three GPU pixel oracles MATCH (`DIFF=0`), with the bind-key change (the one item that could move a
pixel) carrying its own oracle run on its own commit.

**All ten are green** as of the chokepoint commit (git log `condition #6 re-derived`).

## 8. Honest status & the remaining caveats

Layer 1 is, honestly, **finished to the Layer-2/3 bar** on the two things that bar actually names: the bug
class is closed by construction (§4.2), and the architectural comments are true of the code. The four live
corruptions are gone, proven on hardware before and after; the `makeDoubled` latent leak is closed by the
`Maybe<Face>` representation; the substrate has a real "outside" — it is constructible and composable by Layers
2 and 3.

The caveats, stated rather than buried:

- **The chokepoint is "one grep rule," not compiler-impossible.** `glTexImage2D` / `glTexSubImage2D` are free
  globals and `GlLoneTexture`'s descriptor fields are public, so a *future* direct bypass is representable. The
  guarantee is that every spec routed through the sanctioned path self-records and self-guards; a bypass is a
  single greppable violation, not a per-site audit. Making it truly unrepresentable would require private
  descriptor fields and a `GlLoneTexture`-owned upload method — which the shared atlas caller and the
  `glPixelStorei` alignment co-location forbid at byte-identity. Deliberately stopped at the strongest DIFF=0
  form.
- **Correctness evidence is the render-gate oracle, not standalone unit tests.** Sovereignty makes the
  components *constructible* in isolation, but no unit test yet exercises `GlFrameBuffer` / `GlTargets` /
  `GlPass` / `GlEffects` directly; the byte-identity gate is the proof. That is a genuine gap against "testable
  in isolation," and the cheapest future strengthening.
- **One permanently-divergent file from upstream.** The substrate cannot merge cleanly with upstream's
  monolithic renderer; the cost is paid at each upstream merge (~4×/year), consciously.

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

- **Renaming `GlFrameBuffer` → `Surface`.** Zero bugs made impossible. The "texture-centric, not FBO-centric"
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
4. **Both retained `Json config` members deleted.** `Effect`'s and `GlFrameBuffer`'s config Json were parsed
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
