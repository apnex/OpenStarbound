# Compose-Merge + RS-0 Finish — Design

**Status:** approved (director, 2026-07-19 — "Proceed. Include R2 and R4, we want perfection").
**Parent:** [`2026-07-14-render-surface-subsystem-design.md`](2026-07-14-render-surface-subsystem-design.md) (RS-0).
**Relationship:** RS-0 is, after the render decomposition + the RB/L1-hardening campaign, **substantially
built** — the `Surface` substance (`GlFrameBuffer`+`Face`+`EffectTexture`: faces, `swap()`, `makeDoubled()`,
size-oracle, borrow model), the bind-lifecycle bug class (RB-1…RB-7), **and the per-binding filtering fix**
(`StarRenderer_opengl.cpp:1573-1598` — "filtering belongs to the BINDING") are all in-tree, alongside
`RetainedSurface` + `BackdropPass` + a ~115-line `WorldPainter::render()`. This spec closes the **remaining
residual of value** and lands the compose-merge that RS-0 said would "fall out" once one component owned both
caches — which `BackdropPass` now does.

## The one-line goal

Land the env+parallax **compose-merge** (CM-1); unify the lightmap **Jacobi ping-pong onto `swap()`** (R2);
rename `GlFrameBuffer` → **`GlSurface`** to make the abstraction first-class (R4). Each step byte-identical
where a bit-exact result is achievable, bounded-diff where the status quo already is, and **oracle-gated**.

## Out of scope

- **R3 — `RetainedSurface` *holds* a `Surface` object** (instead of referencing a framebuffer by string
  name). A cleanliness down-payment; **not required** by CM-1/R2/R4. Deferred, available on request.
- **R5 — upstream PR of Layer 1.** RS-0's Phase-1 "build on `origin/main`, offer a PR" is undercut: git shows
  **no common ancestor** between HEAD and `origin/main` (a residue of the save-purge history rewrite). A
  cherry-pickable PR is impractical as specified; re-decide separately, do not execute here.

---

## Component 1 — CM-1: the compose-merge

### Today (two full-screen composes into `main`)

Only on the retained-cache path. `BackdropPass::renderEnvironment` composites `envCache → main`
(`lightingPassthrough`, `preserveAlpha=false`, implicit `Alpha` blend → opaque replace onto cleared `main`);
after the lightmap phase, `renderParallax` composites `parallaxCache → main` (`preserveAlpha=true`,
`PremultipliedOver`). Nothing draws into `main` between them — the lightmap "between" is a texture bind, and
`lightingPassthrough` never samples the lightmap.

### Target (one full-screen compose)

A new fragment effect **`backdropCompose`** samples *both* cache textures and writes `main` once:

```
out.rgb = P.rgb + E.rgb * (1.0 - P.a);   // E = envCache (opaque base), P = parallaxCache (premultiplied)
out.a   = 1.0;
```

This is algebraically the sequential result: `main = env` (opaque), then `main = P.rgb + main·(1−P.a)`. Env
covers every pixel, so no clear is required; blend is off (the shader computes the full result).

**Restructure (both-caches-active path only):** `renderEnvironment` *fills* `envCache` but **defers** its
compose; `renderParallax` fills `parallaxCache` then issues the single `backdropCompose`. Safe because nothing
reads `main` between the original two composes.

**The four states** (unchanged except the first):

| env cache | parallax cache | behaviour |
|---|---|---|
| active | active | **merged** — one `backdropCompose` in `renderParallax` |
| active | inactive | env compose (today), parallax direct draw |
| inactive | active | env direct draw, parallax compose (today) |
| inactive | inactive | both direct draws (today) |

Both caches are simultaneously active exactly in the camera-parked, AA-off, static scene — the idle-base/ship
floor target, and the only place the merge applies.

### Correctness & gates

- **env** `MATCH/0` — the env contribution stays byte-identical (opaque replace, computed identically).
- **parallax** bounded `≤1 LSB` — the in-shader premultiplied-over may differ from GL fixed-function blend by
  ≤1 LSB on semi-transparent fringes; the parallax cache is **already** ≤1 LSB today, so this inherits the
  status-quo contract rather than introducing a new tolerance.
- **spread** `MATCH/0`, `GL_INVALID=0` — no collateral.
- **Ablation A/B** (`STAR_RENDERTEST_PASS_MASK`) reports the real saving. The ~2 ms figure is the non-additive
  sum of two serialising GL timers (`architecture-assessment.md:176`) and is **not** the acceptance number.
- **Adversarial verify** across the full input space (the harness exercises one frozen world).
- Shipped behind a **default-ON** config flag (`backdropComposeMerge`), per the complete-mechanism rule.

---

## Component 2 — R2: unify the Jacobi ping-pong onto `swap()`

### Today

`StarGpuLightmapPass::runSpread` ping-pongs **two named framebuffers** via parity:

```
char const* targets[2] = {"lightingGpu", "lightingGpuB"};
for (i in 0..spreadIterations) {
  target = targets[i % 2];
  setRenderTarget(target, size);
  (i==0) ? setEffectTextureAlias("lightState","emission") : setEffectTextureFromTarget("lightState", last);
  renderBuffer(fullQuad);  last = target;
}
```

The `swap()`/doubled-`Surface` machinery exists but this consumer does not ride it — it is the second, still
hand-rolled swap the spec wanted unified.

### Target

Make `lightingGpu` a **doubled Surface** (two faces). Each iteration writes the write-face and reads
`lightState` from the read-face, then `swap()`:

```
for (i in 0..spreadIterations) {
  setRenderTarget("lightingGpu", size);                 // write-face
  (i==0) ? setEffectTextureAlias("lightState","emission")
         : setEffectTextureFromTarget("lightState","lightingGpu");   // resolver → read-face (swap-aware)
  renderBuffer(fullQuad);
  swap("lightingGpu");
}
```

`lightingGpuB` is removed; the compose/upscale read the final face of `lightingGpu`. **One swap mechanism, two
consumers** (this + the #542 double-buffered-effect path).

### Correctness & gates

- **spread** `MATCH/0` — bit-identical: the read/write sequence per iteration must reproduce the `%2` named
  ping-pong exactly (iteration *i* reads iteration *i−1*'s output).
- `GL_INVALID=0`; the memory footprint is unchanged (two faces vs two framebuffers).
- **Adversarial verify** of the swap ordering (the off-by-one hazard: which face is read at `i` and after the
  final `swap()` at compose time).

---

## Component 3 — R4: rename `GlFrameBuffer` → `GlSurface`

The surface substance is complete; only the *name* is not first-class — `GlFrameBuffer` is a misnomer (the
struct *owns* 1-2 faces, it is not a framebuffer). Rename the struct `GlFrameBuffer` → `GlSurface` (in
`StarGlRenderSurface.{hpp,cpp}`) and align the public method vocabulary to the RS-0 design where it maps
cleanly — `allocate()` (THE allocation path), `bind(Write)` (THE bind path), `face(Write|Read)` (THE
resolver), `swap()`. No behaviour change.

**Target is `GlSurface`, not bare `Surface`** (director, 2026-07-19): keeps the `Gl`-prefixed family
consistency (`GlTargets`/`GlPass`/`GlEffects`/`GlTexture`/`GlLoneTexture`), stays honest that the struct is
GL-backend-internal (lives inside `OpenGlRenderer`), avoids the `SDL_Surface` (CPU pixel buffer) collision,
and matches the `StarGlRenderSurface` file name. Bare `Surface` is reserved for the parked upstream-track
backend-agnostic Layer 1 (R5).

**Done last**, so the rename sweeps the new CM-1/R2 code in the same pass.

### Correctness & gates

- Pure rename: **compile clean** + **all oracles unchanged** (env `MATCH/0`, parallax `≤1 LSB`, spread
  `MATCH/0`), `GL_INVALID=0`. No new logic to adversarially verify beyond "the diff is a rename."

---

## Sequencing

1. **CM-1** — ablation baseline → `backdropCompose` effect (+ CPU mirror if the effect path needs one) →
   defer env compose + both-active gating → oracle-gate + adversarial verify → default-ON flag.
2. **R2** — make `lightingGpu` doubled → rewrite `runSpread` to `swap()` → update compose/upscale to the final
   face → spread `MATCH/0` + adversarial verify.
3. **R4** — rename `GlFrameBuffer`→`GlSurface` + API vocabulary → compile + all oracles unchanged.

Each lands as its own oracle-certified commit on `render/decomposition`. Builds are E-core-pinned
(`taskset -c 6-15 nice -n 19`), `VCPKG_ROOT=/root/vcpkg` inline, **user out of game** (`pgrep` first).

## Verification

The gate harness `scripts/render-gate.sh` runs the three in-frame oracles (env / parallax / spread) offscreen
on the real GPU and fails on any DIFF, any unarmed oracle, or any GL error. Every step must pass it. CM-1 and
R2 additionally get read-only adversarial 4-lens verification across the full input space, since the harness
exercises one frozen world. Assert each oracle **ran** and matches what it writes (the vocabulary trap:
env says `MATCH`, parallax says `EXACT`).
