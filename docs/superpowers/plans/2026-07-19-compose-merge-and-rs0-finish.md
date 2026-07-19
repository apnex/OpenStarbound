# Compose-Merge + RS-0 Finish (CM-1 + R2 + R4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the env+parallax compose-merge (CM-1); unify the lightmap Jacobi ping-pong onto `swap()` (R2); rename `GlFrameBuffer` → `GlSurface` (R4) — each byte-identical-or-bounded and oracle-gated.

**Architecture:** All work is on branch `render/decomposition`. CM-1 adds a two-input `backdropCompose` effect and, on the both-caches-active path, defers the env compose so `renderParallax` issues a single merged full-screen pass into `main`. R2 makes `lightingGpu` a doubled `GlSurface` and rewrites the spread loop from two named FBOs to `swap()`. R4 is a mechanical rename done last so it sweeps the new code.

**Tech Stack:** C++17, OpenGL 3.2 (GLSL `#version 150`), the fork's `Renderer`/`OpenGlRenderer`, effect `.config`/`.frag`/`.vert` assets, `scripts/render-gate.sh` (offscreen GPU oracles), CMake+Ninja (Clang, `VCPKG_ROOT=/root/vcpkg`).

**Build command (all tasks):** `VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target starbound -j 8` — the client target is `starbound`, output goes directly to `dist/starbound` (which `render-gate.sh` boots). The harness reads assets straight from the repo (`harness/sbinit.config`), so new `.config`/`.frag` files are live in the gate with no deploy. (The live game would need `scripts/deploy-install.sh`; we gate via the harness.) R4 also has a `render_surface_tests` target worth running.

**Domain note on "tests":** This is a byte-identical GPU refactor. The test of record is the **in-frame render-gate oracle** (`scripts/render-gate.sh`: env `MATCH/0`, parallax `EXACT`/bounded, spread `MATCH/0`, `GL_INVALID=0`), not xUnit. Where a pure-C++ unit is added (none here — RetainedSurface already has its suite), TDD applies; otherwise each task's "test" step is the oracle run. Every build: user **out of game** (`pgrep starbound` first), E-core-pinned `taskset -c 6-15 nice -n 19`, `VCPKG_ROOT=/root/vcpkg` inline.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `assets/opensb/rendering/effects/backdropCompose.config` (new) | Two-input compose effect declaration | A1 |
| `assets/opensb/rendering/effects/backdropCompose.frag` (new) | `out = P.rgb + E.rgb·(1−P.a)` | A1 |
| `assets/opensb/rendering/effects/backdropCompose.vert` (new) | Screen-space quad → `fragTexCoord` (copy of `lightingPassthrough.vert`) | A1 |
| `source/game/StarRootLoader.cpp:107` | `backdropComposeMerge` config default (ON) | A2 |
| `source/rendering/StarBackdropPass.hpp` / `.cpp` | Full-screen quad buffer + `mergedCompose()`; defer env compose; issue merged compose | A3, A4 |
| `source/frontend/StarClientCommandProcessor.cpp` | `/rendercache` status line reports the new flag | A2 |
| `source/rendering/StarGpuLightmapPass.cpp` | Spread loop → `swap()`; compose reads final face | B2, B3 |
| `assets/opensb/rendering/opengl.config` | `lightingGpu` `"double": true`; drop `lightingGpuB` | B1 |
| `source/application/StarGlRenderSurface.hpp` / `.cpp` + all references | Rename `GlFrameBuffer` → `GlSurface` | C1 |

---

## PHASE A — CM-1: the compose-merge

### Task A0: Ablation baseline (measure before building)

**Files:** none (measurement only).

- [ ] **Step 1: Confirm the tree builds and the gate is green at baseline**

Run:
```bash
pgrep -a starbound || echo "clear to build"
cd /root/frackin/OpenStarbound
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
taskset -c 6-15 bash scripts/render-gate.sh
```
Expected: build OK; gate prints env/parallax/spread pass counts with `DIFF=0` and `GL_INVALID=0`, exit 0.

- [ ] **Step 2: Ablation A/B for the honest per-pass cost**

The pass-mask harness lives at `source/rendering/StarWorldPainter.cpp` (`STAR_RENDERTEST_PASS_MASK`, bit0 env, bit1 parallax). Capture the compose cost by ablating env then parallax vs neither, in the frozen world:
```bash
taskset -c 6-15 STAR_RENDERTEST_PASS_MASK=3 bash scripts/render-gate.sh   # both on  (baseline)
taskset -c 6-15 STAR_RENDERTEST_PASS_MASK=0 bash scripts/render-gate.sh   # both off (floor)
```
Record the frame-time delta from telemetry in the gate log (`render.pass.environment.compose.gpu_us`, `render.pass.parallax.compose.gpu_us`, and the aggregate). **This delta — not the ~2 ms GL-timer sum — is CM-1's saving budget.** Write the numbers into the commit message for A5.

- [ ] **Step 3: Record the baseline number**

No code. Note the measured saving in the task tracker so A5's ablation can be compared against it. If the measured saving is < ~0.3 ms, CM-1 still proceeds (architectural cleanliness, per director "perfection"), but the commit message states the honest figure.

---

### Task A1: `backdropCompose` effect assets

**Files:**
- Create: `assets/opensb/rendering/effects/backdropCompose.config`
- Create: `assets/opensb/rendering/effects/backdropCompose.frag`
- Create: `assets/opensb/rendering/effects/backdropCompose.vert`

- [ ] **Step 1: Write the vertex shader (identical to `lightingPassthrough.vert`)**

`assets/opensb/rendering/effects/backdropCompose.vert`:
```glsl
#version 150

// Full-screen compose: screen-space quad (vertexPosition in pixels) mapped to clip space,
// with a [0,1] sample coordinate derived from clip position (target-size independent).
uniform vec2 screenSize;

in vec2 vertexPosition;
in vec2 vertexTextureCoordinate;
in vec4 vertexColor;
in int vertexData;

out vec2 fragTexCoord;

void main() {
  vec2 clip = vertexPosition / screenSize * 2.0 - 1.0;
  fragTexCoord = clip * 0.5 + 0.5;
  gl_Position = vec4(clip, 0.0, 1.0);
}
```

- [ ] **Step 2: Write the fragment shader (the merged blend)**

`assets/opensb/rendering/effects/backdropCompose.frag`:
```glsl
#version 150

// CM-1: one full-screen pass replacing two sequential composites into "main".
// E = envCache  (opaque backdrop; today: preserveAlpha=false -> alpha forced 1, opaque replace)
// P = parallaxCache (premultiplied coverage; today: preserveAlpha=true -> premultiplied-over)
// Sequential result was:  main = E ; then main = P.rgb + main*(1 - P.a)
// which is algebraically the single expression below. Blend is OFF for this pass (we write the
// full result); env covers every pixel so no clear is needed.
uniform sampler2D envTexture;
uniform sampler2D parallaxTexture;

in vec2 fragTexCoord;

out vec4 fragColor;

void main() {
  vec4 E = texture(envTexture, fragTexCoord);
  vec4 P = texture(parallaxTexture, fragTexCoord);
  fragColor = vec4(P.rgb + E.rgb * (1.0 - P.a), 1.0);
}
```

- [ ] **Step 3: Write the effect config (two input textures, no params)**

`assets/opensb/rendering/effects/backdropCompose.config`:
```json
{
  "includeVBTextures" : false,

  "effectTextures" : {
    "envTexture" : {
      "textureUniform" : "envTexture",
      "textureAddressing" : "clamp",
      "textureFiltering" : "nearest"
    },
    "parallaxTexture" : {
      "textureUniform" : "parallaxTexture",
      "textureAddressing" : "clamp",
      "textureFiltering" : "nearest"
    }
  },

  "effectShaders" : {
    "vertex" : "backdropCompose.vert",
    "fragment" : "backdropCompose.frag"
  }
}
```

- [ ] **Step 4: Register the effect in the renderer's effect list**

Effects are enumerated in `assets/opensb/rendering/opengl.config` (the same file that declares `lightingPassthrough`, `lightingUpscale`, framebuffers). Add `backdropCompose.config` to the effect-config list exactly as `lightingPassthrough.config` is listed. Verify by grepping:
```bash
grep -n 'lightingPassthrough\|effectConfigs\|backdropCompose' assets/opensb/rendering/opengl.config
```
Expected: `backdropCompose` now appears in the same array/object as `lightingPassthrough`.

- [ ] **Step 5: Build (asset-only + a client relink) and confirm the effect loads**

Run:
```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
taskset -c 6-15 bash scripts/render-gate.sh
```
Expected: build OK; gate green (the new effect is loaded but not yet used — no behaviour change). A shader-compile error in `backdropCompose.frag` would surface as a load-time log error; confirm none.

- [ ] **Step 6: Commit**
```bash
git add assets/opensb/rendering/effects/backdropCompose.config assets/opensb/rendering/effects/backdropCompose.frag assets/opensb/rendering/effects/backdropCompose.vert assets/opensb/rendering/opengl.config
git commit -m "feat(render): add backdropCompose effect (two-input env+parallax merge) [CM-1]"
```

---

### Task A2: `backdropComposeMerge` config flag (default ON)

**Files:**
- Modify: `source/game/StarRootLoader.cpp:107` (render-cache config defaults block)
- Modify: `source/frontend/StarClientCommandProcessor.cpp:661` (`/rendercache` status line)

- [ ] **Step 1: Add the default (ON) beside the other render-cache flags**

In `source/game/StarRootLoader.cpp`, the render-cache defaults block currently starts:
```cpp
      "envRefreshInterval" : 4,
      "envOracle" : false,
      "lightingSpreadOracle" : false,
      "parallaxRefreshInterval" : 0,
      "parallaxMaxDriftStepPx" : 0.75,
      "parallaxOracle" : false,
```
Add `"backdropComposeMerge" : true,` immediately after `"parallaxOracle" : false,` so the merge ships default-ON (per the complete-mechanism rule).

- [ ] **Step 2: Report it in `/rendercache` status (so the toggle is observable)**

In `source/frontend/StarClientCommandProcessor.cpp` the status `strf(...)` (around :661) lists `envRefreshInterval={} envOracle={} parallaxRefreshInterval={} parallaxOracle={}`. Extend the format string with ` backdropComposeMerge={}` and add the matching argument `cfg->get("backdropComposeMerge", true).toBool()` in the arg list.

- [ ] **Step 3: Build + confirm the default reads true**
```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
```
Expected: compiles. (Behavioural confirmation happens in A4/A5; the flag is not yet consumed.)

- [ ] **Step 4: Commit**
```bash
git add source/game/StarRootLoader.cpp source/frontend/StarClientCommandProcessor.cpp
git commit -m "feat(render): backdropComposeMerge config flag, default ON [CM-1]"
```

---

### Task A3: BackdropPass merged-compose primitive

**Files:**
- Modify: `source/rendering/StarBackdropPass.hpp` (members + method decl)
- Modify: `source/rendering/StarBackdropPass.cpp` (quad buffer init + `mergedCompose()`)

- [ ] **Step 1: Add the full-screen quad buffer + flag + method declaration**

In `StarBackdropPass.hpp`, add to the private members (near `m_renderer`):
```cpp
  RenderBufferPtr m_fullQuadBuffer;   // CM-1: screen-space quad for the merged backdrop compose
  Vec2U m_fullQuadSize = {0, 0};      // rebuild the quad when the screen size changes
  bool m_composeMerge = true;         // backdropComposeMerge config flag (read in renderEnvironment)
```
and declare, in the private methods:
```cpp
  // CM-1: one full-screen pass sampling both caches into "main" (env opaque base, parallax premultiplied-over).
  void mergedCompose(Vec2U const& size);
```

- [ ] **Step 2: Implement `mergedCompose()` in `StarBackdropPass.cpp`**

Mirror the `GpuLightmapPass` full-quad pattern (own buffer, `renderBuffer`, `flush`). Add:
```cpp
void BackdropPass::mergedCompose(Vec2U const& size) {
  if (m_fullQuadSize != size) {
    List<RenderPrimitive> prims;
    prims.append(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f));
    if (!m_fullQuadBuffer)
      m_fullQuadBuffer = m_renderer->createRenderBuffer();
    m_fullQuadBuffer->set(prims);
    m_fullQuadSize = size;
  }
  m_renderer->switchEffectConfig("backdropCompose");
  m_renderer->setEffectTextureFromTarget("envTexture", m_envCache.name());
  m_renderer->setEffectTextureFromTarget("parallaxTexture", m_parallaxCache.name());
  m_renderer->setRenderTarget("main", size);
  m_renderer->renderBuffer(m_fullQuadBuffer);
  m_renderer->flush();
  m_renderer->switchEffectConfig("world");
}
```
(Verify `createRenderBuffer`/`renderBuffer`/`switchEffectConfig`/`setEffectTextureFromTarget`/`setRenderTarget`/`flush` signatures against `source/application/StarRenderer.hpp`; they are the same ones `GpuLightmapPass` and the existing composes call.)

- [ ] **Step 3: Build (unused method) to confirm it compiles**
```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
```
Expected: compiles; no behaviour change yet (method uncalled).

- [ ] **Step 4: Commit**
```bash
git add source/rendering/StarBackdropPass.hpp source/rendering/StarBackdropPass.cpp
git commit -m "feat(render): BackdropPass::mergedCompose full-screen quad primitive [CM-1]"
```

---

### Task A4: Wire the merge (defer env compose when both caches active)

**Files:**
- Modify: `source/rendering/StarBackdropPass.cpp` (`renderEnvironment`, `renderParallax`)

**Design of the four states** (only the both-active path changes):

| env cache | parallax cache | env compose | parallax compose |
|---|---|---|---|
| active | active | **deferred** (skip) | replaced by `mergedCompose()` |
| active | inactive | today's env compose | today's direct draw |
| inactive | active | today's direct draw | today's parallax compose |
| inactive | inactive | today's direct draw | today's direct draw |

The env-cache **fill** (draw into `envCache`) is unchanged and always happens in `renderEnvironment`; only the env→main **compose** is deferred. `renderParallax` already runs after the lightmap phase and already knows whether its own cache is active; it also needs to know whether env's cache is active and whether merge is enabled.

- [ ] **Step 1: Read the flag once per frame and expose env-cache-active to renderParallax**

In `renderEnvironment`, near the top (where `m_envRefreshedThisFrame = false;` is set), read the config flag and record whether the env cache is active this frame. Add a member `bool m_envCacheActiveThisFrame = false;` to `StarBackdropPass.hpp`, and in `renderEnvironment` set:
```cpp
  m_composeMerge = Root::singleton().configuration()->get("backdropComposeMerge", true).toBool();
  // envCacheActive is the existing condition that decides fill-then-compose vs direct draw:
  bool envCacheActive = /* existing condition, e.g. (envRefreshInterval > 1 || envOracle) */;
  m_envCacheActiveThisFrame = envCacheActive;
```
(Use the EXACT existing `envCacheActive` expression already computed in `renderEnvironment` — do not duplicate the logic; hoist the existing local into the member.)

- [ ] **Step 2: Defer the env compose on the both-active path**

At the env `composite(...)` site in `renderEnvironment` (the `m_renderer->composite("lightingPassthrough", "main", ...)` call, bracketed by the `render.pass.environment.compose.gpu_us` timer), guard it so it is **skipped** when the merge will happen. The merge happens iff `m_composeMerge && envCacheActive && parallaxWillCache`. Because `renderEnvironment` runs before `renderParallax`, compute `parallaxWillCache` from the same predicate `renderParallax` uses to decide its cache path (park + AA-off + N>1 + layers-present). Extract that predicate into a `bool BackdropPass::parallaxCacheActive(WorldCamera const&, WorldRenderData&) const` helper (Step 3) and call it here:
```cpp
  bool willMerge = m_composeMerge && envCacheActive && parallaxCacheActive(camera, renderData);
  if (!willMerge) {
    // ... existing env compose (composite into "main") unchanged ...
  }
  // else: env cache is filled but not composed; renderParallax issues the merged compose.
```

- [ ] **Step 3: Extract `parallaxCacheActive()` so both entry points agree**

The predicate that `renderParallax` currently evaluates inline to choose cache-vs-direct must become a `const` helper so `renderEnvironment` can ask the same question without side effects. Add to `StarBackdropPass.hpp`:
```cpp
  // True iff the parallax cache path will run this frame (layers present, AA off, N>1/oracle, camera parked).
  bool parallaxCacheActive(WorldCamera const& camera, WorldRenderData& renderData) const;
```
and move the existing inline predicate from `renderParallax` into it (pure read; no counter mutation — the still-frame counters stay updated in `renderParallax`). `renderParallax` then calls `parallaxCacheActive(...)` where it used the inline condition.

- [ ] **Step 4: Issue the merged compose in renderParallax**

In `renderParallax`, on the cache-active path, after the parallax cache is **filled** (the `drawParallax` into `parallaxCache` with `PremultiplyInto`), replace the parallax `composite("lightingPassthrough", "main", ...)` call with:
```cpp
  bool willMerge = m_composeMerge && m_envCacheActiveThisFrame;   // parallax cache is active here by construction
  if (willMerge) {
    // CM-1: single pass — env (opaque base) + parallax (premultiplied-over) into "main".
    ZoneScopedN("render.pass.parallax.compose.gpu_us");   // keep the existing timer bracket macro
    mergedCompose(parallaxScreenSize);
  } else {
    // ... existing parallax compose (setBlendMode PremultipliedOver; composite; restore Alpha) unchanged ...
  }
```
(Keep the exact GPU-timer bracket the existing compose uses, so `render.pass.parallax.compose.gpu_us` still records the merged pass.)

- [ ] **Step 5: Build**
```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
```
Expected: compiles.

- [ ] **Step 6: Do NOT commit yet — proceed to A5 (verification gates this task's commit).**

---

### Task A5: Verify CM-1 (oracles + ablation + adversarial) and commit

**Files:** none (verification), then commit A3/A4 wiring.

- [ ] **Step 1: Render-gate — env, parallax, spread must hold**

Run:
```bash
taskset -c 6-15 bash scripts/render-gate.sh
```
Expected: `[envoracle] MATCH (0 diff)` (env stays byte-identical — the env contribution is unchanged, just relocated); `[paralloracle]` within its bounded contract (`EXACT` or maxAbs ≤ ~1 LSB — the merged premultiplied-over may differ from GL fixed-function blend by ≤1 LSB, the same tolerance the parallax cache already has); `[spreadoracle] MATCH (0 diff)`; `GL_INVALID=0`; exit 0.

> If `paralloracle` shows a large `maxAbs`, STOP — the blend algebra is wrong (check the `P.rgb + E.rgb·(1−P.a)` sign/premultiplication and that blend is OFF for the merged pass). Do not proceed on a red gate.

- [ ] **Step 2: Ablation A/B — the honest saving**

Repeat A0 Step 2 with the merge active vs the flag off:
```bash
taskset -c 6-15 STAR_RENDERTEST_AB='backdropComposeMerge=true|false' bash scripts/render-gate.sh
```
Record the frame-time / compose-µs delta. This is the number that goes in the commit message.

- [ ] **Step 3: Adversarial verification (read-only, full input space)**

The gate exercises one frozen world. Dispatch a read-only 4-lens adversarial review (Explore agents, no Edit/Write) comparing the parent commit vs HEAD across: (1) the four cache-state combinations and their transitions; (2) the deferred-env-compose ordering (does anything read `main` between fill and merged compose across all paths?); (3) resize / pixelRatio / FBO-generation-bump mid-session; (4) the blend algebra vs the two-step sequential composite. Fold any confirmed issue as a labeled follow-up commit; a false alarm is recorded and dismissed.

- [ ] **Step 4: Commit the wiring**
```bash
git add source/rendering/StarBackdropPass.hpp source/rendering/StarBackdropPass.cpp
git commit -m "feat(render): merge env+parallax composes into one pass [CM-1]

Both-caches-active path defers the env compose and issues a single
backdropCompose (env opaque base + parallax premultiplied-over) into main.
Gate: env MATCH/0, parallax <=1 LSB, spread MATCH/0, GL_INVALID=0.
Ablation A/B saving: <fill in measured number>."
```

---

## PHASE B — R2: unify the Jacobi ping-pong onto `swap()`

### Task B1: make `lightingGpu` a doubled surface

**Files:**
- Modify: `assets/opensb/rendering/opengl.config` (framebuffer declarations)

- [ ] **Step 1: Declare `lightingGpu` double-buffered; remove `lightingGpuB`**

In `assets/opensb/rendering/opengl.config`, find the framebuffer declarations for `lightingGpu` and `lightingGpuB`. Add `"double" : true` to the `lightingGpu` block (this drives `GlTargets::add` → `makeDoubled()`), and delete the separate `lightingGpuB` framebuffer declaration (its second face now lives on `lightingGpu`). Verify:
```bash
grep -n 'lightingGpu' assets/opensb/rendering/opengl.config
```
Expected: `lightingGpu` has `"double" : true`; no `lightingGpuB`.

- [ ] **Step 2: Build (config not yet consumed by new code path — old code still names lightingGpuB, will fail to find it)**

Do NOT build/gate here in isolation — the C++ in `StarGpuLightmapPass.cpp` still references `"lightingGpuB"`. B1 and B2 land together. Proceed directly to B2.

---

### Task B2: rewrite the spread loop to `swap()`

**Files:**
- Modify: `source/rendering/StarGpuLightmapPass.cpp` (`runSpread`, the `targets[2]` array, the compose target selection)

Current `runSpread` (verbatim, `StarGpuLightmapPass.cpp:95-112`):
```cpp
  auto runSpread = [&](bool fromAlpha) -> char const* {
    m_renderer->setEffectParameter("obstacleInAlpha", fromAlpha);
    char const* last = nullptr;
    for (unsigned i = 0; i < spreadIterations; ++i) {
      char const* target = targets[i % 2];
      m_renderer->setRenderTarget(String(target), size);
      if (i == 0)
        m_renderer->setEffectTextureAlias("lightState", "emission");
      else
        m_renderer->setEffectTextureFromTarget("lightState", last);
      m_renderer->renderBuffer(m_fullQuadBuffer);
      last = target;
    }
    spreadPasses.inc(spreadIterations);
    return last;
  };
```

- [ ] **Step 1: Replace the two-named-FBO ping-pong with `swap()` on one doubled surface**

Rewrite the lambda so every iteration writes `lightingGpu`'s write-face, reads `lightState` from its read-face, then swaps. The read after iteration 0 is `setEffectTextureFromTarget("lightState", "lightingGpu")` — the resolver returns the read-face (swap-aware):
```cpp
  auto runSpread = [&](bool fromAlpha) -> bool {
    m_renderer->setEffectParameter("obstacleInAlpha", fromAlpha);
    for (unsigned i = 0; i < spreadIterations; ++i) {
      m_renderer->setRenderTarget("lightingGpu", size);        // write-face
      if (i == 0)
        m_renderer->setEffectTextureAlias("lightState", "emission");
      else
        m_renderer->setEffectTextureFromTarget("lightState", "lightingGpu");   // read-face (swap-aware)
      m_renderer->renderBuffer(m_fullQuadBuffer);
      m_renderer->swap("lightingGpu");                          // flip: this frame's write becomes next read
    }
    spreadPasses.inc(spreadIterations);
    return true;   // result now lives in lightingGpu's read-face (post-final-swap)
  };
```

> **Off-by-one hazard (the adversarial focus):** after the loop's final `swap()`, the last-written face is the **read**-face. So the compose/upscale must read `lightingGpu`'s read-face, i.e. `setEffectTextureFromTarget(..., "lightingGpu")` returns exactly that. Confirm the pre-existing behaviour (compose read `targets[spreadIterations % 2]` = the last-written buffer) maps to "read-face after the final swap." If it does not, drop the final iteration's `swap()` (swap only between iterations, `i < spreadIterations - 1`) so the last write stays the write-face and matches the old compose target. **The spread oracle decides which is correct — it must be MATCH/0.**

- [ ] **Step 2: Delete the `targets[2]` array and fix the compose target selection**

Remove `char const* targets[2] = {"lightingGpu", "lightingGpuB"};` (`:36`). The compose (`:218-220`) currently does:
```cpp
    char const* composeTarget = targets[spreadIterations % 2];   // != lastTarget
    ...
    m_renderer->composite("lightingPassthrough", composeTarget, size, "inputTexture", lastTarget, {...});
```
Replace with a compose that reads `lightingGpu`'s final face into the point/compose target the pass already uses. Because there is now one surface, the compose reads `"lightingGpu"` (resolver → the face holding the final spread result) and writes the existing compose destination. Update the oracle-park path (`runSpread(false)` → `composite(... "lightingRef" ...)`) identically — it parks a reference copy and is unaffected by the surface unification beyond the source name.

- [ ] **Step 3: Update the bicubic upscale source, if it named `lightingGpuB`**
```bash
grep -n 'lightingGpuB\|lightingGpu' source/rendering/StarGpuLightmapPass.cpp assets/opensb/rendering/effects/lightingUpscale.config
```
Expected after edits: **no** remaining reference to `lightingGpuB` anywhere. Any upscale/compose config that named `lightingGpuB` now names `lightingGpu`.

- [ ] **Step 4: Build**
```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
```
Expected: compiles; no reference to `lightingGpuB` remains (link/asset-load clean).

- [ ] **Step 5: Verify — spread oracle must be MATCH/0, then commit**

Run:
```bash
taskset -c 6-15 bash scripts/render-gate.sh
```
Expected: `[spreadoracle] MATCH (0 diff)` (the swap ping-pong reproduces the named-FBO ping-pong bit-for-bit); env/parallax unchanged; `GL_INVALID=0`.

Then a read-only adversarial pass focused on the swap ordering (which face is read at iteration `i`; which face the compose reads after the final swap; the oracle-reference park). Only after spread `MATCH/0` and the adversarial pass:
```bash
git add source/rendering/StarGpuLightmapPass.cpp assets/opensb/rendering/opengl.config assets/opensb/rendering/effects/lightingUpscale.config
git commit -m "refactor(render): Jacobi spread ping-pongs one doubled surface via swap() [R2]

lightingGpu is now double-buffered; runSpread writes the write-face,
reads the read-face, swaps -- one swap mechanism shared with the #542
double-buffered-effect path. lightingGpuB removed. Gate: spread MATCH/0."
```

---

## PHASE C — R4: rename `GlFrameBuffer` → `GlSurface`

### Task C1: mechanical rename + API vocabulary

**Files:**
- Modify: `source/application/StarGlRenderSurface.hpp`, `source/application/StarGlRenderSurface.cpp`, and every referencing file (chiefly `source/application/StarRenderer_opengl.cpp`).

- [ ] **Step 1: Enumerate every reference**
```bash
grep -rln '\bGlFrameBuffer\b' source/ | sort
```
Record the file list.

- [ ] **Step 2: Rename the type (whole-word, all files)**

Rename `GlFrameBuffer` → `GlSurface` everywhere. Because it is a whole-word type rename with no name collisions (verified: no existing `GlSurface`/`Surface` symbol), a scoped substitution is safe, but do it via the editor per-file (not a blind global sed) so each hunk is reviewable — the codebase convention is Edit-tool changes, not shell munging. Keep the `Face` struct name and the file name `StarGlRenderSurface.{hpp,cpp}` (already surface-named).

- [ ] **Step 3: Align the method vocabulary where it maps cleanly**

Only if a method's current name is *less* clear than the RS-0 canonical and the rename is mechanical: ensure `allocate()` / `bind(...)` / `face(Write|Read)` / `swap()` read as the canonical paths. If the current names already match (they largely do — `swap()`, `makeDoubled()`, `writeFace()`/`readFace()`), leave them. **No behaviour change; no signature change beyond the type name.** Do not invent new methods.

- [ ] **Step 4: Build (pure rename → must compile with zero warnings)**
```bash
VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build --target star_client -j 8
```
Expected: compiles clean; the diff is a rename only.

- [ ] **Step 5: Verify — all oracles unchanged (rename cannot alter pixels)**
```bash
taskset -c 6-15 bash scripts/render-gate.sh
```
Expected: env `MATCH/0`, parallax bounded, spread `MATCH/0`, `GL_INVALID=0` — identical to before C1.

- [ ] **Step 6: Commit**
```bash
git add source/application/StarGlRenderSurface.hpp source/application/StarGlRenderSurface.cpp source/application/StarRenderer_opengl.cpp
# plus any other files the grep in Step 1 listed
git commit -m "refactor(render): rename GlFrameBuffer -> GlSurface [R4]

The struct owns 1-2 faces; it is a surface, not a framebuffer. GlSurface
keeps the Gl- family (GlTargets/GlPass/GlEffects/GlTexture/GlLoneTexture),
stays backend-honest, and avoids the SDL_Surface (CPU image) collision.
Pure rename: all oracles unchanged."
```

---

## Self-Review

**1. Spec coverage:**
- CM-1 → A1 (effect), A2 (flag), A3 (primitive), A4 (wiring), A5 (gates + ablation). ✓
- R2 → B1 (doubled config), B2 (swap rewrite), (B2 Step 3 upscale). ✓
- R4 → C1. ✓
- Ablation-first (spec §CM-1 gates) → A0. ✓
- Out-of-scope R3/R5 → not planned (correct). ✓

**2. Placeholder scan:** The only intentional fill-in is the measured ablation number in the A5 commit message (a value produced at runtime, not a design gap). The `parallaxCacheActive()` predicate and the existing `envCacheActive` expression are described as "the exact existing condition" rather than re-derived — the implementer hoists the live expression, which is correct for a byte-identical refactor (re-deriving risks divergence). No `TODO`/`TBD`.

**3. Type consistency:** `mergedCompose(Vec2U const&)`, `parallaxCacheActive(WorldCamera const&, WorldRenderData&) const`, members `m_fullQuadBuffer`/`m_fullQuadSize`/`m_composeMerge`/`m_envCacheActiveThisFrame`, effect name `backdropCompose`, samplers `envTexture`/`parallaxTexture`, config key `backdropComposeMerge`, type `GlSurface` — all used consistently across tasks.

**Known execution-time confirmations (not gaps):** the exact `opengl.config` effect-list/framebuffer-decl syntax (A1 S4, B1 S1) and the exact `envCacheActive` expression (A4 S1) are confirmed against the live file when the task runs — the plan says which file/line and what to look for. This is deliberate for a byte-identical refactor.
