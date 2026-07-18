# Render architecture — honest assessment, HEAD vs upstream

**Written 2026-07-14 at `e472e594b`. Nothing in here has been implemented — this is the brief for a future
session.** Companion to [`layer1-residuals.md`](layer1-residuals.md), which it **corrects in two places**
(see §5).

## How this was produced, and why you should trust it over the commit messages

58 agents, 53 assessment claims, each individually refuted by an independent skeptic. **25 died.** The
remaining 28 are below.

The exercise was deliberately hostile: the person writing the report had done the work and had an obvious
incentive to tell a flattering story. **It caught him doing exactly that, five times.** Those corrections are
§4 and they matter more than the praise.

**CAVEAT ON EVERY "UPSTREAM STILL HAS THIS" CLAIM.** Our `upstream/main` ref is `2c7f972b6` — the point we
forked. Nobody has fetched. Real upstream is ~83+ commits ahead (see memory `openstarbound-upstream-drift`).
**Fetch before filing anything.** Every upstream claim here is verified against the fork point, not against
real HEAD.

---

## 1. The one structural difference — and it is visible in the type

**upstream:**

```cpp
struct GlFrameBuffer : RefCounter {
  GLuint id = 0;            RefPtr<GlLoneTexture> texture;      // face 0
  bool hasAlt = false;
  GLuint altId = 0;         RefPtr<GlLoneTexture> altTexture;   // face 1
  void makeAlt(Vec2U const& screenSize = Vec2U(256, 256));
  void swap();
};   // no `private:` anywhere. every field public.
```

Three parallel facts about a second face. The consequences are facts about the **shape**, not lapses of care:

- **The destructor is `glDeleteFramebuffers(1, &id); texture.reset();`** — `altId` and `altTexture` are never
  freed. Every doubled surface leaks both, on every `loadConfig` (i.e. every AA or HDR toggle).
- `makeAlt` is a second allocator that duplicates the constructor, **and its author knew**:
  > `// Bott: ...this is a lot of repeated code. unfortunately it's also rather difficult to make it not repeated.`

  It was not difficult. It was **impossible**, while the second face had its own names.
- `makeAlt`'s `screenSize` defaults to **256×256** and is called with no argument — so every `"double":true`
  surface upstream is born 256×256 while face 0 was sized separately.
- `swap()` physically exchanges the two, so every consumer re-derives "the other one" by hand:
  `swapped ? buf->altTexture : buf->texture`, `(useAlt && hasAlt) ? altId : id`. **Forget `&& hasAlt` and you
  get `altId == 0` — which is FBO 0, the window.**

**ours:**

```cpp
struct GlFrameBuffer : RefCounter {
  struct Face { GLuint id = 0; RefPtr<GlLoneTexture> texture; };
  Face& writeFace(); Face& readFace();       // the only resolver
  Vec2U size() const; Vec2U sizeFor(Vec2U const&) const;
  void makeDoubled(); void swap(); void resize(Vec2U const&); void clearFaces();
private:
  friend class OpenGlRenderer;
  void specifyStorage(Face&, Vec2U const&, char const*);   // the only storage writer
  void allocateFace(Face&, Vec2U const&, char const*);     // the only allocator
  Face faces[2]; unsigned write = 0; bool doubled = false;
};
```

**The destructor loops. There is no twin to forget, because there is no twin.** Verified by grep: every
`faces[` access in the tree is inside GlFrameBuffer's own methods. Zero external reaches.

| rule | upstream | ours |
|---|---|---|
| hdr/alpha/multisample → internalFormat ladder | **3 verbatim copies** | **1** (`specifyStorage`) |
| the size rule | **4 copies, in 3 mutually inconsistent forms** | **1** (`sizeFor`) |
| "which face do I mean" | hand-resolved at every consumer | **1** (`writeFace`/`readFace`) |
| per-face allocator | 2 (ctor + `makeAlt`) | **1** (`allocateFace`) |
| the storage descriptor | **does not exist** — `textureSize` set to `{0,0}` and never written again, so the mod-facing size uniform reads zero forever | recorded by the act that specifies it |

**This is the product.** The `#542` bug class is not fixed — it is largely **inexpressible by array shape**,
and it needs no `private:` to work.

---

## 2. What was bought — the defect ledger

**Eight defects. Five live, two latent, one restored capability. Three are upstream's.**

| | | whose |
|---|---|---|
| **MSAA forced onto every framebuffer** | The strongest one. `config.set("multisample", …)` on *every* FBO, unconditionally. | **UPSTREAM** (PR #561 filed) |
| **RB-2/3** the "using default" shader fallback compiles no default; the old program is destroyed before the new one links. **Together: a hard crash from one bad shader in a mod.** | **UPSTREAM** |
| **RB-4** post-process `passes > 1` never ping-pongs — the early-out returns before the swap. Exposed through a shipped UI slider that did nothing. | **UPSTREAM** |
| **RB-6** the storage descriptor had one writer and four mutators → half-float RGBA written into 3-channel storage, forever. **It was silently eating a shipped, measured perf win** (J-2's obstacle-flag-in-alpha, 17→9 taps). | ours |
| **RB-5** an AA toggle left four samplers reading destroyed render targets | ours |
| **RB-1** an upload setter re-specified the storage of a target it had merely borrowed | **ours, self-inflicted — see §4** |
| **RB-7** the pass kept an orphaned target alive across a rebuild | ours, latent |
| **the `textureFiltering` no-op** — a mod-facing key that did nothing for framebuffer-sourced textures | ours |

**Correct the MSAA mechanism when you describe it.** It is *not* "a multisample texture reads as zero." The
object is `GL_TEXTURE_2D_MULTISAMPLE`; binding it to `GL_TEXTURE_2D` is `GL_INVALID_OPERATION` and the bind is
a **no-op** — the texture unit keeps its *previous* binding. Our hardware probe: `uniform=lightMap unit=4
want=7 bound=13 err=0x502`. The shader samples a stale unrelated texture. Nondeterministic, order-dependent,
**harder** to diagnose than a clean zero.

Upstream is dormant on it only **by accident of its asset set** (one framebuffer, zero `frameBufferTextures`
consumers) — not by design. The first sampled FBO they add, they self-inflict. **That is the argument for
#561, not "mod authors."**

---

## 3. What it cost

**A permanently unmergeable file.** 1,583 insertions vs upstream in `StarRenderer_opengl.{cpp,hpp}`. The
struct they still ship does not exist in our tree. Any future upstream change to `GlFrameBuffer`,
`loadEffectConfig`, `switchEffectConfig` or `renderGlBuffer` **will not merge — it must be re-implemented by
hand against a different type.** This is permanent and is paid every upstream cycle. It is the single largest
cost of the work and no amount of green oracles changes it.

**Calibrate it, though:** upstream has touched this file **19 times in its entire history, 4 times in the last
year**. The cost is real but low-frequency. It is not catastrophic. Do not use it as an argument against
finishing the work.

**The code cost is nothing. The prose cost is real.** Refactor-era growth is **+149 code lines across eight
files** (`.cpp`: +42). Features — GPU lighting, the caches, the oracles, telemetry — are **82%** of this
subsystem's growth. But the refactor added **+358 comment lines, and four of them are false** (§5).

**A hot-path regression, real and unmeasured.** `renderGlBuffer`'s effect-texture loop issues **6 GL calls per
texture** where upstream issues 2 — four `glTexParameteri` we added. They sit **inside** the vertex-buffer
loop although their arguments depend only on `(effect, texture)`, never on `vb`. Byte-identical calls,
re-issued once per vertex buffer, every frame. Hoisting them above the `vb` loop is byte-identical in GL state
at every `glDrawArrays`. Bounded blast radius (`interface.config` declares no effect textures, so only the
world pass pays) — but nobody measured it.

---

## 4. Corrections to the record — do not inherit these overclaims

**1. "Four sovereign components." Three are. GlPass is not.**
It has **zero `private:` keywords**. The renderer makes **32 raw field pokes** into it (`m_pass.effect` ×14,
`m_pass.target` ×6, `m_pass.screenSizeUniform` ×5, the flattened attributes…). It hand-rolls the *inverse* of
`bindTarget` at two sites **that do not agree with each other**. The missing member is `GlPass::unbind(Vec2U)`.
Until it exists, GlPass is a namespace with a `bind()` attached.

**2. "Sealed / unrepresentable." Overclaimed.**
The `friend` readmits every nested sibling — which is precisely the population where all the defects lived.
And there is a live `#542`-class latent bug **inside the resolver's own implementation** (§6, item A). Honest
score: **divergent facts 3 → 2, not 0.** The accidental path is closed and the duplication is gone. That is
enough. Say that instead.

**3. RB-1 was OURS, self-inflicted. It is not an upstream find.**
`setEffectTextureFromTarget` **does not exist upstream**. Our GPU lightmap was the first thing to point an
upload sampler at a framebuffer face. The hazard was latent there; **we made it live, then found it.** It has
been presented as a discovery. It is not. Disclose this.

**4. We re-introduced the exact duplication we are being congratulated for killing.**
`setEffectTextureHalf` and `setEffectTextureR8` are **29 character-identical lines**, ours, bypassing the
shared `createGlTexture` their own sibling setter calls. Upstream had 4 format ladders file-wide; HEAD has
4–5. **The framebuffer ladder collapsed 3→1 — a real win — but the file-wide count did not fall.**

**5. "The `Renderer` interface is untouched." False.**
`+80/-0` vs upstream, and the refactor era itself changed it (`+7/-32` — exiling the instruments). Say **"not
split; subtractive only."**

**6. "Nothing is unblocked" is the correct answer to "what does this unblock?"**
- **LightmapPass predates the refactor by a month.** It was listed as output. It is **inherited**.
- **The compute path was never blocked by the god-class.** Upstream's `GlFrameBuffer` already owned a texture
  and attached it with `glFramebufferTexture2D`. Zero `glGenRenderbuffers` in either tree. **Strike that
  claim.** (And the design's own stated constraint — *"anything we may compute on must be RGBA"* — is **not
  enforced**: `specifyStorage` still derives `GL_RGB16F` when `hdr && !alpha`, and `envCache` is exactly that
  surface.)
- **The compose merge was already expressible** — `composite()` and `setEffectTextureFromTarget` both predate
  the refactor. **And do not print the "~2ms/frame" figure**: it is the sum of two per-pass GL timers, while
  the same spec states those timers "serialise the pipeline and are NOT ADDITIVE". It is a *cost* of two
  composes, not a *saving*; merging removes at most one. What the refactor genuinely buys the merge is
  **landmine removal** — RB-5 and RB-6 would each have silently corrupted a two-borrow compose. That is
  defensible. "Unblocks" is not.

---

## 5. Corrections to `layer1-residuals.md`

That document is otherwise sound. **Two things in it are wrong:**

**Its "nailed" checklist item 6 says the single-writer storage descriptor is *"already true as of RB-6"*. It
is not.** RB-6 established *"whoever specifies storage records what it specified"* — a different claim from
*"there is one writer."* `->textureSize =` still resolves to **four** functions on the effect side
(`setEffectTexture`, `setEffectTextureHalf`, `setEffectTextureR8`, `createGlTexture`). Residuals item 1a
(`createEmptyLoneTexture`) is what makes it true. **Remove the parenthetical.**

**Its checklist fails 9 of 10 at HEAD** — friend=3 (target 0), `glViewport`=4 (target 2), `justSwapped` alive,
`Json config` still a member of both, `glGenTextures`=6 (target 3), the borrow predicate written 3×, three
type ladders. **None of these is a bug.** It is naming-and-structure debt, and the checklist is doing its job
by failing. But do not describe the subsystem as "sealed" while your own checklist says 9/10 unreached.

---

## 6. New work items this assessment generated

Ranked. **None is a live corruption. Item A is a real latent bug; the rest is honesty and hygiene.**

### A. `makeDoubled()` can leak a face — upstream's `altId` bug, inside our own resolver
**BEHAVIOUR-ADJACENT. Its own commit. This is the one that matters.**

```cpp
void GlFrameBuffer::makeDoubled() {
  if (doubled) return;
  allocateFace(faces[1], size(), "second face");   // writes faces[1].id, THEN can throw
  doubled = true;                                  // only reached on success
}
```

`allocateFace` calls `glGenFramebuffers` into `faces[1].id` and *then* throws on
`glCheckFramebufferStatus`. On that throw, `faces[1]` holds a live FBO and a live texture while `doubled` is
still `false` — and the destructor loops `doubled ? 2u : 1u` and **forgets it.** The destructor's comment says
"the second face cannot be forgotten." It can.

`bool doubled` is declared as a biconditional (*"faces[1] exists iff doubled"*) **that the type does not
enforce.** The idiomatic fix is `Maybe<Face> secondFace` — a pattern **this very file already uses**
(`Maybe<Vec2U> overrideSize`). Then existence *is* the fact, and there is no second bool to disagree with
reality. It was available and was not taken.

### B. Fix the four false comments — before anything else is written
**DONE (§9 step 1).** All four corrected: the `switchGlFrameBuffer` ghost (4 sites → 0), the `bindTarget`
"entire argument for this component" claim (F3b.1 deleted that write — comment now states so and the intro
premise is corrected to ground GlPass on the current-state pair + per-draw locations), the GlFrameBuffer
"renderer needs the raw faces" claim (verified nothing outside touches the privates; friend labelled
vestigial-until-§8.ii), and the GlTargets friend non-sequitur (same). Comment-only; binary byte-identical.

**DOC-ONLY. One hour. Highest value per minute on this list.**

- **`hpp:443`** — states `bindTarget` *"binds a TARGET and it writes an EFFECT-PROGRAM UNIFORM. That single
  fact is **the entire argument for this component**."* **`bindTarget` issues zero `glUniform` calls.** The
  write was deleted in F3b.1 as provably dead at both call sites — and the `.cpp` says so, eight commits
  later, while the header still teaches the retracted claim. **The header states the component's reason to
  exist and it is false.**
- **`hpp:269`, `hpp:418`, `cpp:1034`, `cpp:2182`** — all name `switchGlFrameBuffer`, **a function that does
  not exist.**
- **`hpp:303-306`** — *"the enclosing renderer needs the raw faces to allocate, resize and clear them."* It
  does not. `resize()`, `clearFaces()` and `makeDoubled()` are public and do all of it.
- **`hpp:400`** — the friend's justification, *"the oracle reads pixels back out of a target by name."* The
  oracle does exactly that — **entirely through the public `find()`**. It is a non-sequitur guarding a hole
  nobody takes.

A subsystem that ships ~590 comment lines and lies in four of them is **worse to read** than one that ships 22
and lies in none. **The file currently teaches an architecture more confidently than it implements one.** That
is the single most important sentence in this document.

### C. Hoist the per-draw `glTexParameteri` block out of the vertex-buffer loop
**PURE-STRUCTURAL, byte-identical in GL state at every draw. ~30 min.** See §3. Also deletes upstream's
pre-existing redundant binds. **Measure it; do not claim a win without a number.**

### D. Give `GlPass` a `private:` and an `unbind()`
**DONE (§9 step 3, folded with the bind key).**
**PURE-STRUCTURAL. ~2 hours.** 32 raw field pokes, and two hand-rolled inverse-of-bindTarget sites that
disagree. Fold both into `GlPass::unbind(Vec2U)`. Then GlPass is a component rather than a field bag. **Note:
this partly overlaps residuals item 2 (the bind key) — do them together or do the bind key first.**

### E. The two remaining upstream PRs have never been built against upstream
Both fixes exist as hardware-tested commits in our tree, but only `postprocess-passes` is genuinely liftable
(one token). **`shader-compile-recovery`'s patch is written against our already-refactored `loadEffectConfig`
— its upstream form is unwritten work, not a cherry-pick.** Say so in the PR, or write it against upstream
first. And **fetch upstream before filing anything.**

---

## 7. The verdict

**Worth it — on the defect ledger and the shape, and on nothing else that was originally claimed.**

Eight defects, five live, one silently eating a shipped perf win, three genuinely upstream's. A class of bug —
*"two facts about one thing disagreed"* — is now largely inexpressible in this subsystem. That is real, and
**+149 code lines is nothing** to have paid for it.

Against that: a permanently unmergeable file (paid every upstream cycle, but only ~4 times a year), four false
comments, an unmeasured hot-path regression, and two new duplicated allocators written by the same hands in
the same file that was being celebrated for killing duplication.

**GOOD at:** framebuffer surfaces. Adding a face, resizing, swapping, or changing the format ladder is a
one-site edit with a compiler-visible owner.

**BAD at:** everything *adjacent* to a framebuffer. The effect-texture upload path is duplicated and holds the
framebuffer invariant up with three hand-copied guard predicates. GlPass is a public field bag. And the
documentation outruns the code.

**Whoever picks this up: do item B first.** It costs an hour and it is the difference between documentation
and marketing.

---

## 8. The ceiling — what would make Layer 1 EXCELLENT, not merely better than vanilla

**"Better than vanilla" is a low bar and we have already cleared it.** Vanilla leaks a framebuffer and a
texture on every AA toggle, writes its format ladder out three times, and its own author gave up on the
duplication in a comment. Say it plainly and stop congratulating ourselves for it.

Finishing §6 and `layer1-residuals.md` makes Layer 1 **defensibly** better rather than **arguably** better —
two to three days, mostly mechanical, no design left. Worth having. **But it is hygiene, not architecture.**

**The step-change is three moves, and only one of them is on any existing list.**

### i. Kill the last divergent fact — `Maybe<Face>`
**DONE (§9 step 2).** `Face faces[2]` + `bool doubled` + `unsigned write` → `Face front` + `Maybe<Face> back`
+ `bool writeToBack`. Existence IS doubledness (`doubled()` == `back.isValid()`); there is no bool left to
disagree with whether a second texture is allocated, and `makeDoubled` builds into a local and moves it in, so
there is no instant where `doubled()` is true but the storage is half-formed. `front`/`back`/`writeToBack` map
exactly onto the old `faces[0]`/`faces[1]`/`write` — single-face path gate-verified byte-identical (3 oracles,
0 GL errors); doubled path is dead in-tree, verified by the exact mapping and now compiler-enforced.

**~20 lines. The single highest-leverage change in the subsystem.**

`bool doubled` is a biconditional (*"faces[1] exists iff doubled"*) **that the type does not enforce** — which
is precisely how upstream's `altId` bug walked back in through our own front door (§6 item A). Replace it with
`Maybe<Face> secondFace` and **existence IS the fact**. There is no second bool left to disagree with reality,
and the "one writer, many mutators" family — which produced *four* of this campaign's bugs — loses its last
foothold in `GlFrameBuffer`.

This is the difference between *"we fixed the bug"* and *"the bug is not expressible."* The whole refactor is
sold on the second. Right now it delivers the second for the **faces** and the first for **whether there are
two of them**.

### ii. Make the seal structural, not a doorbell
**DONE (§9 step 5).** 5a deleted the three vestigial friends (compiler-enforced seal). 5b physically extracted the substrate to its own translation unit in two tiers: Tier 1 = GlTexture+GlLoneTexture (StarGlTexturePrimitives), Tier 2 = the four components + Effect types (StarGlRenderSurface), depending on Tier 1. The god-header dropped 635->312 lines. The components are now a namespace-scope module with a real OUTSIDE -- sealed against every consumer, constructible in a test, composable by Layers 2/3. Pure relocation, byte-identical; certified env/parallax/spread green, core 226/226.

**The four components in their own translation unit. No `friend`.**

Today the seal is `friend class OpenGlRenderer`, and per `[class.access.nest]` that readmits **every nested
sibling** — which is exactly the population where all seven defects lived. `GlFrameBuffer` is a private nested
type: *nothing outside this TU can even name it*, so **there is no outside**. "Sovereign" is currently a
convention honoured by every call site and **guaranteed against none**.

Move them out and the compiler enforces what the comments claim. This is the item that converts the word
"sealed" from marketing into a fact, and it is the reason §4 has to exist at all.

### iii. Fix the neighbourhood, not the house
**DONE (§9 step 4).** Three hand-copied fresh-allocation predicates -> one `EffectTexture::ownsWritableStorage()`. Two 29-line character-identical allocators -> one `createEmptyGlTexture()`. The borrow set atomically through `adopt()`/`share()`/`release()` so texture and borrow-status cannot diverge — no setter hand-pokes `borrowedFrom` any more. Byte-identical; certified env 83/83, parallax 20/20, spread 216/216, 0 GL errors, RB-1 probe clean.

**The effect-texture boundary — and it is on no list.**

**Three of our four self-inflicted bugs (RB-1, RB-5, RB-6) lived in the effect-texture path, not in
`GlFrameBuffer`.** The surface is good. **The boundary is bad**: three hand-copied guard predicates holding the
framebuffer's invariant up from outside, two 29-line character-identical allocators, and a sampler that can
*borrow* a target's storage with nothing but a `String` and a convention standing between it and corruption.

That is where the next class of bugs already is. It is the least glamorous item here and the one most likely
to bite.

---

## 9. THE DECISION — Layer 1 gets finished to the same standard as 2 and 3

**Director's ruling, 2026-07-14.** The assessment's author recommended taking only item B and `Maybe<Face>`,
banking the rest opportunistically, and moving to Layer 2 on the grounds that Layer 1 "doesn't need to be
excellent, it needs to be not the problem."

**That lean was overruled, and correctly.**

> *"We are already committed to Layer 2 and Layer 3 being excellent. Layer 1 might as well be too."*

The argument is **Foundation-of-Sand (A8)**, and it is the right one. Layers 2 and 3 are not neighbours of
Layer 1 — they are **built on top of it**. `RetainedSurface` *is* a Surface plus policy. `BackdropPass` *owns*
two of them. A merely-adequate substrate under two excellent layers is not a pragmatic trade; it is the
foundation fault the axioms name, and every shortcut left in Layer 1 is one that Layers 2 and 3 will be
obliged to route around — and then to *document* routing around, which is how the "correct by convention"
comments in §4 got written in the first place.

**So: finish it.** §6, then `layer1-residuals.md`, then the three ceiling moves above. The order that
maximises value per day:

1. **§6 B** — the four false comments (1 hour). Nothing else should be written on top of a file that teaches
   things that are not true.
2. **§8 i** — `Maybe<Face>` (20 lines). It closes a real latent bug *and* it is the last divergent fact.
3. **residuals item 2** — the `(target, face, size)` bind key, folded with **§6 D** (GlPass gets a `private:`
   and an `unbind()`). These are the same commit; doing them apart is doing them twice.
4. **§8 iii** — the effect-texture boundary. One seal predicate, one allocator, and the borrow made
   structural rather than stringly-typed.
5. **§8 ii** — the components move to their own TU, and the `friend`s go. Do this **last**: it is the move
   that makes every preceding one compiler-enforced, and doing it first would just mean fighting the seal
   while still changing what is behind it.
6. The remaining residuals (the `Json config` members, the inert sampling config, the type ladders) —
   opportunistically, whenever a commit already has the file open.

**The bar is not "better than vanilla." It is: every architectural comment in the file is true of the code,
and the bug classes we closed are closed by the compiler rather than by our care.**
