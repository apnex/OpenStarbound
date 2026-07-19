# Render Architecture — Vanilla Baseline

The pristine upstream OpenStarbound render pipeline at commit `2ea33530` ("binds: merge duplicate category ids") — the state **before the entire kubebound campaign**.

**Panel 1 of 3:** 🕰️ vanilla (here) → [🧱 accreted monolith](architecture-2-accreted-monolith.md) → [🏗️ decomposed target](architecture-3-target-state.md).

> ### The decomposition re-extracts *our own* accretion — it does not untangle upstream.
> Pristine `WorldPainter` was **already a slim orchestrator**: a **112-line** `render()`, a **324-line** file, four sub-painters, and **zero** retained caches. The campaign's perf machinery — env + parallax retained caches, the GPU-lightmap dispatch, and the config-read bloat, ~550 added lines — is what fused it into today's **876-line** god-object. Steps 3+ pull that machinery back out into sovereign passes, restoring the orchestrator's original slimness **while keeping every measured perf win.**

---

## The vanilla frame — everything drawn directly, every frame

```mermaid
flowchart TD
  classDef step fill:#3a4a5c,color:#ffffff,stroke:#1f2a36,stroke-width:1px
  classDef light fill:#6b5b2e,color:#ffffff,stroke:#3d3418,stroke-width:1px

  A["setup — camera screen size + pixelRatio<br/>TilePainter.setup (build/cache tile chunks)"]:::step
  B["EnvironmentPainter — sky &amp; celestial, direct:<br/>stars → debris → back-orbiters → horizon → sky → front-orbiters"]:::step
  C["renderer.flush()"]:::step
  D["Lighting — NO draw, effect state only:<br/>CPU cellular lightMap uploaded via setEffectTexture('lightMap')<br/>+ lightMapEnabled / lightMapMultiplier / lightMapScale / lightMapOffset"]:::light
  E["EnvironmentPainter.renderParallaxLayers — direct"]:::step
  F["World layers, entities interleaved by RenderLayer:<br/>bg overlays · bg tiles · platforms · back particles · liquid ·<br/>mid particles · fg tiles · fg overlays · front particles · nametags + bars"]:::step
  G["dim overlay (if dimLevel ≠ 0)"]:::step
  H["cleanup — text / drawable / environment / tile painters"]:::step

  A --> B --> C --> D --> E --> F --> G --> H
```

The lightmap is **CPU-computed on the game thread** (cellular lighting), handed to the frame as `WorldRenderData.lightMap`, and consumed implicitly by the `world` shader. There is **no GPU lighting pass** and **no `lightMapBorder`**.

## The vanilla structure — a slim orchestrator on a monolithic renderer

```mermaid
flowchart TB
  classDef orch fill:#1f5c4d,color:#ffffff,stroke:#0d3327,stroke-width:1px
  classDef painter fill:#3a4a5c,color:#ffffff,stroke:#1f2a36,stroke-width:1px
  classDef iface fill:#4a3d6b,color:#ffffff,stroke:#271f3d,stroke-width:1px
  classDef mono fill:#7a3b3b,color:#ffffff,stroke:#3d1d1d,stroke-width:1px

  WP["WorldPainter — slim orchestrator<br/>112-line render() · 324-line .cpp<br/>camera math · layer interleave · particles/bars · lighting params"]:::orch

  subgraph PAINTERS["4 sub-painters — all share m_renderer + one WorldRenderData&amp;"]
    EP["EnvironmentPainter<br/>sky · stars · orbiters · parallax"]:::painter
    TP["TilePainter<br/>bg/mid/liquid/fg terrain<br/>(geometry/vertex cache, not image cache)"]:::painter
    DP["DrawablePainter"]:::painter
    TXP["TextPainter"]:::painter
  end

  R{{"abstract Renderer interface (pure virtual)"}}:::iface
  GL["OpenGlRenderer — ONE monolithic 1379-line .cpp<br/>GlFrameBuffer / GlTexture / Effect … = PRIVATE nested structs<br/>FrameBufferCount = 1 · flat immediate-primitive API + switchEffectConfig"]:::mono

  WP --> EP & TP & DP & TXP
  EP --> R
  TP --> R
  DP --> R
  TXP --> R
  R -. "one concrete impl" .-> GL
```

The only renderer abstraction upstream ships is the `Renderer` interface + its single `OpenGlRenderer`. The API `WorldPainter` uses is essentially **flat immediate-mode** (`immediatePrimitives` / `render(RenderPrimitive)` / `flush`) plus a **global named-effect shader swap** (`switchEffectConfig("world")`). There is **no** `setRenderTarget`, `composite`, `clearRenderTarget`, or `setEffectTextureFromTarget` — those, and the sovereign L1 GL classes, are all campaign additions.

---

## What the campaign added, and what the decomposition does with it

| Concern | Vanilla `2ea33530` | Campaign added (our fork) | Decomposition target |
|---|---|---|---|
| **Orchestrator** | slim `WorldPainter` — 112-line `render()`, 324-line file | bloated to **876 lines** (cache bookkeeping + GPU dispatch + config reads inline) | thin orchestrator again, 5 sovereign modules beneath |
| **Lighting** | CPU cellular lightmap → `lightMap` shader texture | `GpuLightmapPass` (GPU spread + point + cap), `lightMapBorder` | ✅ `LightmapPass` — explicit `LightmapResult` |
| **Sky / env** | drawn direct every frame | `envCache` retained FBO (N-cadence refresh) | `BackdropPass` owns a `RetainedSurface` |
| **Parallax** | drawn direct every frame | `parallaxCache` retained FBO (park + content key) | `BackdropPass` owns a `RetainedSurface` |
| **Renderer substrate** | GL helpers = private nested structs in 1 monolith; `FrameBufferCount=1` | sovereign L1 (`GlTargets`/`GlPass`/`GlEffects`/`GlRenderSurface`), multi-FBO | ✅ L1 (done) |
| **Render-target / compose API** | none — flat primitives + `switchEffectConfig` (shader swap) | `setRenderTarget` · `composite` · `clearRenderTarget` · `setEffectTextureFromTarget` · `gpuTimer` | renderer API, retained |
| **Sub-step hand-off** | shared renderer state + one `WorldRenderData&` + `flush()` | + mutated GL state between the new caches/passes | explicit per-pass DTOs + outputs (Air-Gap) |

## Presence / absence at `2ea33530`

| Concept | State |
|---|---|
| Abstract `Renderer` interface + single `OpenGlRenderer` | **EXISTS** (upstream) |
| Sovereign L1 GL* substrate classes | **ABSENT** — private nested structs in one 1379-line .cpp |
| `GpuLightmapPass` / GPU lighting compute | **ABSENT** — CPU lightmap upload only |
| `lightMapBorder` | **ABSENT** |
| env cache · parallax cache · `RetainedSurface` | **ABSENT** — everything drawn directly each frame |
| `setRenderTarget` · `composite` · `clearRenderTarget` · `setEffectTextureFromTarget` | **ABSENT** |
| per-pass input DTOs · explicit pass output structs | **ABSENT** — one frame-wide `WorldRenderData&`, hand-off via shared state |
| named effect configs / `switchEffectConfig` (shader swap) | **EXISTS** (not render targets) |

*Source: read-only archaeology of the blob tree at `2ea33530`; structural claims (112-line `render()`, abstract-interface presence, absence of the render-target API) spot-verified against the commit.*
