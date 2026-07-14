# Layer 1 — remaining residuals

**As of `d05f21682`.** All line numbers are against that commit and will drift; re-verify before cutting.

> **READ [`architecture-assessment.md`](architecture-assessment.md) FIRST.** A hostile 58-agent audit ran
> after this document was written. It **corrected two things below** (marked inline), and it added one real
> latent bug this document missed — `makeDoubled()` can leak a face, which is upstream's `altId` bug
> resurrected inside our own resolver. It also found four **false comments** in the source, including one that
> states GlPass's entire reason for existing. Fix those before writing any more.
>
> **AND READ ITS §8–§9.** Director's ruling: Layer 1 is finished to the **same standard as Layers 2 and 3**,
> not merely to "no longer the problem." Layers 2 and 3 are *built on* this one — a merely-adequate substrate
> under two excellent layers is the **Foundation-of-Sand** fault, and every shortcut left here is one they will
> be obliged to route around. The assessment's §9 gives the ordering; **this document is steps 3 and 6 of it,
> not the whole plan.**
>
> **The bar is not "better than vanilla."** It is: *every architectural comment in the file is true of the
> code, and the bug classes we closed are closed by the compiler rather than by our care.*

Layer 1 is the render-surface substrate inside the GL backend: **`GlFrameBuffer`** (one surface, its 1–2
faces, its storage), **`GlTargets`** (which surfaces exist, and the generation), **`GlPass`** (the bind — the
coupled `(effect, target)` pair and the flattened locations the draw path reads), **`GlEffects`** (the
compiled programs and the scriptable surface).

**No known LIVE corruptions remain in Layer 1** — the last four (RB-1, RB-5, RB-6, RB-7) are closed, each
proven on hardware before and after.

**CORRECTED:** this originally said *"no known bugs left."* The later audit found **one latent bug** this
document missed — `makeDoubled()` writes a live FBO into `faces[1]` and can then throw, leaving `doubled ==
false` so the destructor forgets the face. See `architecture-assessment.md` §6 item A. Everything else below
is structure.

This list was produced by a 120-agent audit in which every proposal faced three independent skeptics.
**25 of 38 proposals were killed** as wrong or as ceremony. What survived is below; what died is at the end,
recorded so nobody re-proposes it.

---

## The pattern behind the four bugs we fixed

RB-1, RB-5, RB-6 and RB-7 were **the same defect in four seats**: a record with one writer and several
mutators.

| | the record | who wrote it | who else changed the thing it described |
|---|---|---|---|
| RB-1 | a target's storage | `specifyStorage` | three effect upload setters, through an aliased `RefPtr` |
| RB-5 | "which target am I sampling" | `setEffectTextureFromTarget` | `loadConfig`, by destroying every target |
| RB-6 | `uploadChannels` | `setEffectTextureHalf` | the other three storage-spec paths |
| RB-7 | `m_pass.target` | `bindTarget` | `loadConfig`, by destroying every target |

**The rule the code now enforces: a descriptor must be written by the act it describes.** Every item below is
in service of keeping it that way.

---

## 1. One seal predicate, one effect-side allocator

**PURE-STRUCTURAL. ~2 hours.**

**The seal predicate is written out three times, verbatim** — `:879`, `:1097`, `:1152`:

```cpp
if (!ptr->textureValue || ptr->textureValue->textureId == 0 || ptr->borrowed()) {
```

And a **fourth** site (`:961`, `switchEffectConfig`'s `frameBufferTextures` block) is the same shape with
`|| borrowed()` **deliberately absent** — correct there, because that site is *establishing* a borrow rather
than refusing to write through one. Nothing says so. Two named predicates make the difference legible:

```cpp
bool hasStorage() const;    // a texture exists and has been specified
bool ownsStorage() const;   // ...and it is ours to write into
```

**The effect-side allocator is written out twice.** `:1098-1112` (`setEffectTextureHalf`) and `:1153-1167`
(`setEffectTextureR8`) diff to **zero differing lines** — 15 lines each — and both are `createGlTexture`
(`:2130`) minus the upload. Collapse to one `createEmptyLoneTexture(size, addressing, filtering)`.

**TRAP — keep the `fresh` bool.** R8's guard is `!fresh && textureSize == size && internalFormat == GL_R8`. A
shared allocator records `internalFormat`, so dropping `fresh` would send a brand-new texture down the
`glTexSubImage2D` branch into never-specified storage.

**NOT allocators — do not fold these in.** `GlFrameBuffer::allocateFace` (multisample-aware, sets sampling
params *after* storage) and `GlTextureAtlasSet::createAtlasTexture`. Different contracts.

**Only non-identity:** the shared allocator gives Half/R8 the `textureId == 0` throw they currently lack.
Error path only. State it in the commit; do not smuggle it.

---

## 2. The bind key — and the screen as a representable target

**BEHAVIOUR CHANGE. Its own gate. ~1 day. This is the only real day of work on the list.**

Key the pass bind on **`(target identity, write-face index, size)`**.

Today `GlPass::bindTarget` early-outs on target *identity alone*. Consequences, all of them live:

- `setRenderTarget` must hand-write its own `glViewport` afterwards to cover for it, because re-targeting the
  same surface at a new size would otherwise keep a stale viewport.
- `justSwapped` exists purely to defeat the same early-out after a `swap()` — the face changed but the target
  didn't, so identity alone cannot see it. **The face index must be in the key**, or a `(target, size)` key
  does not retire `justSwapped`.
- `bindTarget` **cannot represent the screen**: it dereferences `newTarget` unconditionally, so
  `bindTarget(nullptr)` is a segfault. Two sites therefore hand-roll the screen bind
  (`switchEffectConfig`'s no-framebuffer branch, and `setRenderTarget(nullopt)`).

**Do NOT key on a raw `GLuint` FBO name.** GL names are recyclable: `destroyAll` deletes every FBO,
`loadConfig` rebuilds them, and the driver may hand back the same name — a cached raw name would elide the
bind and draw the pass to the *screen*. A `RefPtr` key cannot be recycled.

**What dies:** `justSwapped` · the `vp == 0` fallback in `bindTarget` · `setRenderTarget`'s cover `glViewport`
· both hand-rolled screen binds. **`glViewport` call sites: 4 → 2** (`GlPass::bindTarget`, `setScreenSize`).

**What stays — do not move it.** The three `screenSize` uniform writes. `bindTarget` runs *before*
`glUseProgram`, so a uniform written there lands on the **outgoing** program. This is already documented at
the bottom of `bindTarget`; read it before touching anything. And do **not** route `setScreenSize` through
`bindTarget` — it has five callers, one of which is `loadConfig`, immediately after `destroyAll`.

**The behaviour delta is the fix, not a side effect:** `switchEffectConfig`'s no-framebuffer branch sets no
viewport today. It gains one. It is correct-by-ordering right now only because every consumer of a sized
target restores via `setRenderTarget({})` first. Correct-by-convention is exactly what Local Reasoning
forbids.

**Also in this commit:** the `GlPass` header comment still justifies the component on *"it writes an
EFFECT-PROGRAM UNIFORM… the two halves cannot be split."* **That uniform write is gone** (deleted in F3b.1 —
it was dead at both call sites). Rewrite the comment, or the component's stated reason for existing is a lie.

---

## 3. Retire all three `friend` declarations — the seal is one bool wide

**PURE-STRUCTURAL. ~1 hour. Bundle with anything; do not schedule alone.**

- **`GlTargets`' friend (`hpp:400`) — DEAD.** `m_byId` / `m_generation` are touched only inside GlTargets'
  own methods. Its comment (*"the oracle reads pixels back out of a target by name"*) justifies a hole the
  oracle **does not take** — it uses the public `find()` like everyone else.
- **`GlEffects`' friend (`hpp:494`) — DEAD.** `m_byName` is touched only inside GlEffects' own methods.
- **`GlFrameBuffer`'s friend (`hpp:308`) — LIVE, for exactly one bool at two sites:** `buf->doubled` at
  `:938` and `:964`. Nothing else — not `faces[]`, not `write`, not `specifyStorage`, not `allocateFace`.
  - `:938` → make `makeDoubled()` return `bool` (true iff it allocated). **Flag:** the warn then fires *after*
    the allocation instead of before. Log order only; no GL call moves.
  - `:964` → a public `bool isDoubled() const`.

Then **zero `friend` in the file**, and `faces[]` is unreachable by the *compiler* rather than by convention.

**Two comments become false and must be fixed in the same commit:** the `GlFrameBuffer` private-section
comment (*"the renderer… needs the raw faces to allocate, resize and clear them"* — it does not; `resize`,
`clearFaces` and `makeDoubled` are public and do all of it) and `GlTargets`' friend comment above.

**Do not plant a wrong C++ rule in the commit message.** Friendship *is* available to nested classes. These
grants are dead because **nobody takes them**, not because nesting cannot reach them.

---

## 4. Delete both retained `Json config` members

**PURE-STRUCTURAL, except one throw-timing note. ~half day.**

**`Effect::config`** is retained solely so the **switch** path can re-hash load-time constants out of JSON on
every effect change: `optString("frameBuffer")` (`:919`), `optArray("frameBufferTextures")` (`:955`),
`optString("blitFrameBuffer")` (`:979`). `doubleBuffered` already proves the pattern — derived once at load,
stored in a field. The job was one-quarter done.

Parse in `GlEffects::load`, beside `includeVBTextures`: `frameBuffer`, `blitFrameBuffer`, an **ordered**
`List<pair<String, String>>` of (textureUniform, framebuffer), and `doubleBuffered` — which now falls out of
the same single pass, deleting the 19-line re-scan at `:768-786`. **`switchEffectConfig` ends with zero JSON
in it.**

**Same fault, same commit: `GlFrameBuffer::config`.** The constructor unpacks six keys and leaves three
behind — `getBool("hdrSetting")` at `:261`, **inside `specifyStorage`, so it re-parses JSON on every resize,
per face**, and `getString("textureAddressing"/"textureFiltering")` at `:318-319`. `hdrSetting` is not even
author-written: `loadConfig` *injects* it from `m_hdrSetting`, a value the renderer already owns. It
round-trips through a `JsonObject` so the framebuffer can re-hash it back out.

**TRAPS.** Store **names, never resolved `RefPtr`s** (`m_targets.get()` throws; `devOnly` targets may not
exist; `loadConfig` rebuilds everything). Keep an **ordered `List`, not a map** — the `undefined ||
buf->doubled` guard makes duplicate entries order-sensitive. Commit parsed fields only after the whole parse
succeeds: `fbt.getString("texture")`'s throw moves from switch time to **load** time. That is an improvement
(fail at load, not mid-frame) — **declare it, don't smuggle it.**

---

## 5. A framebuffer's sampling config is inert — and the object has been lying about it

**PURE-STRUCTURAL. ~1 hour.**

`renderGlBuffer` overwrites MIN/MAG/WRAP on every effect texture **on every draw**, from the *sampling
effect's* declaration (`:2099-2114`) — and a face texture reaches the GPU **only** through an `EffectTexture`
sampler. So `allocateFace`'s config read (`:318-319`) and the four `"textureFiltering":"linear"` keys in
`assets/opensb/rendering/opengl.config` **do nothing**. The file already argues this at `:2099`; the old home
was left standing next to the new one.

**The second witness:** `allocateFace` then hardcodes `textureFiltering = Nearest`, `textureAddressing =
Clamp` on the object. So whenever the config said `"linear"`, the GL state was LINEAR while the object's own
`filtering()` self-report said Nearest. **The C++ record has contradicted its GL state all along.**

**Fix = single-owner collapse, not a deletion.** Set the `glTexParameteri` floor from the same Nearest/Clamp
constants the object records; delete the config reads and the four asset keys; rewrite the ownership comment
(a face carries a **completeness floor**; the **binding** owns sampling).

**Do NOT delete the parameter block.** GL's default `MIN_FILTER` is `GL_NEAREST_MIPMAP_LINEAR` — a mip-less
face would be born **incomplete**.

---

## 6. One parameter-type ladder, not three

**PURE-STRUCTURAL. ~2 hours.**

Three six-way ladders over the same type names sit in one block (`:684-733`): type → `typeIndexOf`, the
scriptable default, and the non-scriptable default. Collapse to one `parseEffectParameter(type, def)`; derive
`parameterType` from the parsed value.

**Sell it on this, not on the 28 lines:** `parameterType` and the parsed default **must** agree — two separate
sites throw on mismatch — and today that invariant is held by two independently-maintained if-chains and
enforced nowhere. The **scriptable** default path is checked by *nothing*: a ladder skew passes load silently
and then rejects every subsequent script write. Deriving one from the other makes it structural.

**TWO TRAPS.**
- The unmatched branch must **throw**. A default-constructed `RenderEffectParameter` is `float 0.0f`, not
  empty — returning one would upload a bogus `glUniform1f`.
- **Preserve the insert order** in the non-scriptable branch. It inserts with `parameterValue` *unset* and
  *then* calls `setEffectParameter`. A merged helper that inserts a fully-populated parameter first trips
  `applyEffectParameter`'s dedup guard and **silently elides every non-scriptable default upload** — the exact
  failure the file already records a warning about.

---

## 7. Delete `Effect::attributes` / `Effect::uniforms`

**PURE-STRUCTURAL. ~2 hours.**

Two `StringMap`s memoizing `glGet*Location`, plus two getters, whose **only** caller is `GlPass::bindEffect`
(`:2144-2163`) — which immediately flattens them into fixed fields. A cache serving one function that already
caches. Resolve the fixed locations once, at load, into `GLint` fields on `Effect` (this also retires the
`GLuint`-vs-`GLint -1` type dishonesty in the getters' signatures).

`GlPass::textureSizeUniforms` **stays flattened** — it is read per-draw — but becomes a fixed array, killing
the per-bind `clear()`/`append()`.

**NO PERF CLAIM IN THE COMMIT MESSAGE.** `bindEffect` fires on effect *change* only, and the hot repeated bind
(`lightingSpread`) declares `includeVBTextures: false` and constructs zero strings. This is a deletion under
Earned Exposure. **It will not move a profile.**

**Separately gated if taken at all:** hoisting the VB sampler `glUniform1i` loop to load time. It changes the
GL call stream and changes who wins when a mod names an effectTexture `texture0..3`. Its own oracle run, or
skip it.

---

# NOT WORTH DOING — killed, with reasons

Recorded so they are not re-proposed. **25 of 38 audit proposals died here.**

**Renaming `GlFrameBuffer` → `Surface`.** *I proposed this and it is refuted.* Zero bugs made impossible. The
design's "texture-centric, not FBO-centric" claim is carried by the **resolver** (`writeFace()`/`readFace()`),
not by the name. Closed.

**Merging `parameters` and `scriptables`.** They are a **trust boundary**: `scriptables` is the
author-declared allowlist enforced against mod Lua in `StarRenderingLuaBindings`, and `parameterValue` means
*GPU-state shadow* in one and *CPU desired-value* in the other. Merging would let Lua poison the shadow and
pin an engine uniform to a stale value. **Add a comment saying so; do not merge.**

**A shared dedup/type-check core between `applyEffectParameter` and `GlEffects::setScriptable`.** Same shape,
different contract — one elides a flush *and* a `glUniform` against a bound program; the other elides an
assignment against no program at all. Merging re-couples GlEffects to the renderer's upload path. Ceremony.

**Standalone deletion of the `vp == 0` fallback, the `screenSize` effect parameter, or the
`OpenGlRenderer::bindTarget` forwarder.** All three are riders on item 2, which rewrites that function.
Deleting them now and re-adding them two commits later is churn. (And the "latent `overrideSize`
wrong-viewport bug" story told about the `vp == 0` branch is **false** — it cannot produce a value different
from the one it replaces.)

**Drive-bys — take only if the file is already open, never schedule:** `composite()` relocation,
`rendererId()` deletion, `frameBufferGeneration()`'s unreachable default body, `EffectParameterHandle`
hardening, the `hasFrameBuffer` doc.

---

# The boundary question: is the `Renderer`-interface split part of Layer 1?

**No. It is a separate axis — and the verdict on that axis is DON'T SPLIT. The question is closed.**

1. **Decisive:** decomposing the implementation into `GlFrameBuffer` / `GlTargets` / `GlPass` / `GlEffects`
   required **zero changes to `StarRenderer.hpp`**. Contract and mechanism are orthogonal. Layer 1 is the
   mechanism axis, by demonstration.
2. **One implementer.** `OpenGlRenderer` is the only `public Renderer` in the tree. No mocks, no second
   backend.
3. **`Renderer` is not a working backend boundary today anyway.** The frame lifecycle
   (`setScreenSize`/`startFrame`/`finishFrame`) is **not virtual and not on the interface**, so `SdlPlatform`
   holds the **concrete** `OpenGlRendererPtr`. What `Renderer` actually is — and does well — is a **compile
   firewall**: zero GL headers reach `rendering/`, `frontend/`, `windowing/`, `game/`. **One header does that.
   Five would not do it one byte better.**
4. **No consumer needs all the virtuals, but every virtual has a real caller** — there is **zero Speculative
   Surface** in the interface to delete. The only clean cleavage planes are single-consumer *and*
   single-implementor, which is exactly the condition under which a narrower contract prevents no bug, removes
   no duplication, and unblocks nothing.

Splitting it would hand a future backend **five bases to implement instead of one — harder, not easier.**

**The live risk is the interface GROWING, not its width. Police that instead.**

---

# "Layer 1 is nailed" — the checkable condition

Not a taste judgement. Ten greps and three oracles.

1. `grep -c "friend" StarRenderer_opengl.hpp` → **0**
2. `grep -c "glViewport" StarRenderer_opengl.cpp` → **2** (`GlPass::bindTarget`, `setScreenSize`).
   `justSwapped` → **0 hits**
3. Exactly **one** writer of `GL_DRAW_FRAMEBUFFER` on the draw path (`GlPass::bindTarget`). `clearFaces`, the
   blit and the oracle are the only exceptions, and each **restores what it found** — no consumer hand-rolls
   "what should be bound"
4. **Zero `Json` in `switchEffectConfig`.** `Effect::config` and `GlFrameBuffer::config` do not exist as
   members
5. **One allocator per side.** `glGenTextures` appears in exactly three places: `GlFrameBuffer::allocateFace`,
   `createEmptyLoneTexture`, `createAtlasTexture`
6. **One writer of the storage descriptor.** `->textureSize =` and `->internalFormat =` each resolve to a
   single function per side.
   **CORRECTED** — this originally read *"already true as of RB-6"*. **It is not.** RB-6 established a
   different (and weaker) invariant: *whoever specifies storage records what it specified*. There are still
   **four** writers on the effect side (`setEffectTexture`, `setEffectTextureHalf`, `setEffectTextureR8`,
   `createGlTexture`). **Item 1 above (`createEmptyLoneTexture`) is what makes this true.** Do not tick it
   until then.
7. **The seal predicate appears once.** Zero inline copies of `textureId == 0 || … borrowed()`
8. **The size rule appears once.** No `screenSize /` arithmetic outside `sizeFor()`
9. **The type-name ladder appears once**
10. Every component's one-line description contains **no "and"**, and every architectural comment in the file
    is true of the code as it stands

Plus: all three GPU pixel oracles MATCH — with **item 2 carrying its own separate oracle run on its own
commit**. It is the only remaining item that can move a pixel, and it cannot ride on anything else.
