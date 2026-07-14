# Unified Render-Surface Subsystem — Design

**Status:** approved (director, 2026-07-14). Layer 1 in build.
**Supersedes:** task #133 ("first-class retained cache surface"), #137 (sovereignty extraction), and the
ad-hoc half of #143 (compose merge).

---

## The problem, in one line

Three separate mechanisms exist for "a framebuffer with more than one face or more than one life", and
none of them knows about the others:

| mechanism | owner | what it does |
|---|---|---|
| upstream #542 `hasAlt`/`swap()` | `OpenGlRenderer` | declarative double-buffer for mod feedback shaders |
| `lightingGpu` ⇄ `lightingGpuB` | `StarGpuLightmapPass` | hand-rolled N-iteration Jacobi ping-pong |
| `envCache` / `parallaxCache` | `StarWorldPainter` | retained surfaces + refresh gates, hand-rolled twice |

The costs of that are not hypothetical. They are, today:

- **Five of upstream's six #542 defects** are the same missing abstraction wearing different clothes
  (below). Patching them one by one is the **Surface Patching** fault (A8, Law of Fallback).
- **Swap-blindness.** `setRenderTarget`, `setEffectTextureFromTarget`, `readFrameBuffer` and
  `compareFrameBuffers` all reach for `buf->texture` / `buf->id` — *the current write face* — with no notion
  of `swap()`. A mod declaring `"double": true` on `main` (the obvious target for a post-process feedback
  shader) silently corrupts our compose. No error. This is the **Air-Gap** violation: consumers reach into
  another unit's internals instead of asking through a contract.
- **`WorldPainter::render()` is 655 lines** against vanilla's 217, and 85 of those are env/parallax cache
  policy hand-rolled twice. **God-Object Accretion** (A3).
- **Filtering is a property of the texture object**, so one framebuffer cannot be `nearest` for the Jacobi
  spread and `linear` for the bicubic upscale. Worse, `setEffectTextureFromTarget` binds the framebuffer's
  texture *directly and silently ignores the effect's declared filtering* — `lightingSpread.config` says
  `"nearest"` and gets `"linear"`. Discovered while shipping J-2 (5d7a09d7); it did not bite only because
  the spread's sample coordinates happen to land on exact texel centres.

---

## Architecture — three layers

### Layer 1 — `Surface` (mechanism). Upstream-track.

Lives in `OpenGlRenderer`. Built **on `origin/main`**, upstream-shaped, then merged into the fork and
offered as a PR. Not because upstream must accept it, but because `StarRenderer_opengl.cpp` is the worst
collision file in the tree (683 lines ours vs 179 theirs) and building our most invasive refactor there
*without* upstreaming it guarantees every future upstream renderer change conflicts with us, forever.

A **Surface** owns **1–2 faces**. A **face** is a texture plus a lazily-created framebuffer view onto it.
Faces are the unit of ping-pong; the framebuffer is an implementation detail, not the primary object.

```
Surface
  name                          -- so a failure can say which surface broke
  faces[1..2]                   -- texture + FBO view; 2 iff double-buffered
  format (hdr, alpha, samples)
  size    (fixed | screen-derived)

  allocate()                    -- THE one allocation path
  bind(Write)                   -- THE one bind path: sets the viewport, records textureSize
  face(Write) / face(Read)      -- THE swap-aware resolver; every accessor routes through it
  swap()                        -- serves #542 AND the Jacobi ping-pong
  generation                    -- invalidates cached RefPtrs on realloc
```

**Texture-centric, not FBO-centric.** A face owns a *texture*; the framebuffer is a cached view. This is
near-free today and means a GL 4.3 compute path binds a face as an *image unit* with no redesign. One real
constraint it surfaces: image load/store does not support `RGB16F`, so anything we may ever compute on must
be RGBA. Our lightmap targets already are.

**Faces = 1 or 2, not N.** Everything we have fits in two. Generalising further is **Ceremony Bloat** (A3).

**Filtering is per-binding, not per-texture.** The Surface exposes filtering at the point of *use*, so the
same surface can be sampled `nearest` by the spread and `linear` by the upscale. This makes the bug J-2
uncovered unrepresentable rather than merely fixed.

#### What Layer 1 makes impossible (rather than fixes)

| upstream #542 defect | root cause | how Surface removes it |
|---|---|---|
| `altId`/`altTexture` leaked on every `loadConfig` | no single **lifecycle** owner | faces are owned by the Surface; one destructor |
| `makeAlt` duplicates the whole ctor | no single **allocation** path | `allocate()` is the only allocator |
| `textureSize` never recorded → mod-visible `textureSize` uniform reads `(0,0)` | no allocation owner to record it | `allocate()` records it |
| `switchEffectConfig` never sets the viewport | no single **bind** path | `bind()` sets it |
| effect texture `RefPtr` goes stale across `loadConfig` | no **generation/ownership** model | `generation` invalidates |
| lazy `makeAlt` allocates a framebuffer **mid-frame** | no lifecycle owner | faces allocated at config time |

(The sixth — MSAA forced onto every framebuffer, so a `double` surface sampled as `sampler2D` reads **black**
under AA — is genuinely independent and is *already fixed in our fork*. It is a prerequisite for upstream's
own #542 to work at all, and is worth its own PR.)

**The mod contract is preserved.** `"double": true` is only a pre-allocation *hint*; the real contract is
the effect-config shape (`frameBuffer == blitFrameBuffer`, or `frameBuffer` appearing in
`frameBufferTextures`) plus the swap-on-entry semantics. Inference and lazy allocation keep working. An
explicit `"doubleBuffered": true` on the effect is **added** for new mods — the inference is fragile, since
it triggers on mere name equality — but never *required*.

### Layer 2 — `RetainedSurface` (policy). Sovereign.

A Surface, plus: persistent content, a **refresh gate** (fixed-N / adaptive / dirty-key), and an
**invalidation key** (size, pixelRatio, generation, content hash). Backend-agnostic; sits above the
`Renderer` interface, not inside the GL backend.

`envCache` and `parallaxCache` become **two instances of one thing** instead of two hand-built copies.

This layer stays sovereign because upstream has **zero consumers** for any of it — no caches, no refresh
gates, no oracles. Pushing it would ship dead code into their tree: the **Speculative Surface** fault (A3),
which is precisely what we spent this morning deleting from our own config (`devOnly`).

### Layer 3 — sovereign passes.

`BackdropPass` joins the already-sovereign `LightmapPass` (`StarGpuLightmapPass.cpp`, 169 lines — proof the
pattern works). BackdropPass owns the sky, the parallax layers, their two RetainedSurfaces, the refresh
gates, adaptive-N, the moving-camera bypass, the oracles — **and the compose**.

`WorldPainter::render()` returns to being a thin orchestrator: **655 → ~250 lines** (vanilla: 217).

#### The test of whether the decomposition is real

**The compose merge stops being a feature.** Once *one component* owns both `envCache` and `parallaxCache`,
composing them in a single full-screen pass is the *obvious implementation* — not a bolt-on someone has to
remember. The measured ~2ms/frame (env compose 977µs + parallax compose 990µs, together ≈ the entire world
pass at 2099µs) **falls out of the architecture**.

Likewise the swap hazard: it is not "fixed", it becomes **unrepresentable**.

That is **Composable by Default** (A3): *"a new capability is assembled by composing existing units, never
by modifying them."*

---

## What this is NOT

- **Not a replacement for the Jacobi ping-pong.** Upstream's #542 **cannot** express it: `switchEffectConfig`
  early-returns when the effect is already current, so N consecutive draws yield **one** swap, not N. #542 is
  single-pass feedback; our Jacobi is an N-iteration solve. Ours is strictly more expressive and stays. What
  Layer 1 gives it is a *named* primitive (`surface.swap()` / `surface.face(Read)`) in place of two string
  constants and a `% 2` — **one swap mechanism, two consumers**, declarative for mods and imperative for the
  solver.
- **Not a render graph.** Passes describing their own inputs/outputs with a scheduler is a *program*, not a
  subsystem, and would mean designing the compose merge before measuring it.

---

## Axiom mapping

| axiom | principle | how this design satisfies it |
|---|---|---|
| **A3** Sovereign Composition | **Law of One** | Surface = faces+lifecycle. RetainedSurface = retention policy. BackdropPass = the backdrop. No "and". |
| | **Air-Gap** | `face(Write\|Read)` is the declared adapter. Nothing reaches for `buf->texture` again — that reach *is* today's swap-blindness bug. |
| | **Earned Exposure** | Layer 1 is promoted to a stable upstream surface because a real external consumer (upstream itself, and every mod) needs it. Layer 2 is **not** promoted: no consumer outside its origin. |
| | **Composable by Default** | The compose merge arrives by composition, not by modifying anything. |
| | **Logic Density** | 2 faces, not N. No render graph. `WorldPainter::render()` 655 → ~250. |
| | *fault: God-Object Accretion* | The 655-line `render()` is the fault; Layer 3 is the remedy. |
| | *fault: Speculative Surface* | Layer 2 stays sovereign. `devOnly` surfaces are not allocated unless armed. |
| **A8** Gated Recursive Integrity | **Gated Ascension** | Layer N+1 is not begun until Layer N is oracle-certified. Phase 0 (gates) preceded everything *because* `game_tests` hung and could not certify anything. |
| | **Binary Certification** | Every migration step is pass/fail on an oracle: env (60 MATCH/0 DIFF), parallax (60 EXACT/0 DIFF), spread (119 MATCH/0 DIFF). No partial credit. |
| | **Law of Fallback** | The six #542 defects are audited *down* to the missing Surface, not patched at the symptom. Same discipline that found `m_hdrSetting` after the VRAM theory failed. |
| | *fault: Surface Patching* | Fixing the six defects individually **is** that fault. This design refuses it. |
| **A4** Zero-Loss Knowledge | | Every retraction is recorded in the commit that supersedes it (the 74–81% VBO win; the ~47% J-2 tap prediction that measured 17.6%). |

---

## Verification

Every step is gated on an **in-frame** oracle — in-frame because the harness's cross-run frame hash is
worthless here (the unpaused load phase lets the sim diverge; entity counts differ run to run).

| layer | gate |
|---|---|
| Layer 1 | env oracle + parallax oracle + spread oracle all still MATCH; `game_tests` 90/91; `core_tests` 226/226 |
| Layer 2 | migrating each cache is bit-identical → its own oracle stays MATCH |
| Layer 3 | same oracles; plus the compose merge is measured by **ablation**, not by per-pass GL timers (which serialise the pipeline and are **not additive** — they rank passes, they do not budget them) |

**What no gate catches:** the oracles compare *our* renderer against *itself*. They cannot tell us whether
upstream's rendering changed the picture. That is what the merge's frozen-camera oracle run answered
(bit-identical), and it is why a *golden-image* comparison against the pre-merge build is not available —
the load-phase divergence poisons it.

---

## Sequencing

- **Phase 0 — gates. DONE.** `game_tests` hung forever → 90/91 in 23s (`649e2965`), which exposed two real
  engine bugs it had been hiding. `scripts/deploy-install.sh --verify` (`e97f14c4`), which immediately found
  `dev-feat` running without a shipped Lua GC lever. J-1 (`6d2856c2`) + J-2 (`5d7a09d7`), −17.6% spread,
  bit-identical, plus the lighting oracle as reusable capital.
- **Phase 1 — Layer 1.** On `origin/main` → merge to fork → offer PR.
- **Phase 2 — Layers 2+3 together.** One migration; splitting them means migrating the caches twice. The
  compose merge (~2ms/frame) falls out.
- **Phase 3 — evidence-gated.** Compute-shader Jacobi (33 global round-trips → ~4–8 via workgroup shared
  memory; needs a GL 4.3 capability gate against upstream's 3.2 floor). And #139, the painter's-algorithm
  program — which the measurements now argue must be bought as **architecture/debt, not performance**: the
  entire live render path is ~2.8ms, and Phase 2 takes ~0.8ms of it.
