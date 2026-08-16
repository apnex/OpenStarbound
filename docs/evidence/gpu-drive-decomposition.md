# What lighting.gpu.cpu_cost.us is made of — first reading (#271)

Two 90-second legs, Desert Town, `scripts/render-profile.sh`, offscreen on the real GPU with the sim
running, deep tracing on. Same protocol and scene as [#171]'s reading, so the two are comparable.
Raw legs: `harness/profiles/drive271-{a,b}.json` (harness/ gitignored; this table is the tracked half).

Two legs give a RANGE, not a confidence interval. The shares below replicate within 0.05pp, which
is a statement about this scene's stability, not a significance claim.

| part | A us | B us | A % of cpu_cost | B % | A n | B n |
|---|---:|---:|---:|---:|---:|---:|
| `repack` | 38,465 | 37,204 | 5.37% | 5.32% | 1,736 | 1,727 |
| `upload` | 121,782 | 119,196 | 17.00% | 17.05% | 1,736 | 1,727 |
| `spread` | 92,207 | 90,019 | 12.87% | 12.87% | 1,736 | 1,727 |
| `point` | 275,249 | 276,697 | 38.41% | 39.57% | 1,736 | 1,727 |
| `compose` | 10,086 | 9,813 | 1.41% | 1.40% | 1,736 | 1,727 |
| `upscale` | 1,250 | 1,149 | 0.17% | 0.16% | 1,736 | 1,727 |
| `flush` | 192 | 179 | 0.03% | 0.03% | 3,472 | 3,454 |

| | A | B |
|---|---:|---:|
| `cpu_cost` total | 716,564 us | 699,184 us |
| SUM(parts) | 539,231 us | 534,257 us |
| **residual (unattributed)** | **177,333 us (24.7%)** | **164,927 us (23.6%)** |

## 1. The wait hypothesis is REFUTED

`drive.flush` is **0.027% / 0.026%** of cpu_cost — 192 and 179 us across 3472 and 3454 samples, i.e.
about 0.05 us per flush. The two explicit `m_renderer->flush()` calls do not block. cpu_cost is CPU
WORK, not the render thread waiting on GL.

This was the leading hypothesis and the reason the task was ranked first: [#246] found a GPU bracket
reading 2.5x the device's whole busy time was a span, and [#173] closed a lever on a loop sleeping
72-84% of every frame. Neither shape applies here. **A lever on this work is possible.**

## 2. `point` is the target: 38.4-39.6%

The per-light quad section is the dominant named part at 275k us — roughly 61 us/frame. `upload`
(17.0%) and `spread` (12.9%) follow. `compose`, `upscale` and `flush` together are under 1.6%.

## 3. THE CLOSURE DOES NOT CLOSE — 23.6-24.8% is unattributed, and that is a finding

Roughly a quarter of cpu_cost is in none of the seven parts. It is the code between and around the
brackets: the initial `switchEffectConfig("lightingSpread")`, the full-quad buffer build, four
`gpuTimer().begin/end` pairs (each issuing GL queries), the setEffectParameter/setBlendMode calls,
and the trailing `setRenderTarget({})` + `switchEffectConfig("world")` +
`setEffectTextureFromTarget("lightMap", ...)`.

**`switchEffectConfig` is the prime suspect, and it is not a guess**: `flushImmediatePrimitives()` is
its FIRST statement, unconditional, before it even looks up the effect. So every effect switch is a
submit point. There are at least four per call — lightingSpread, lightingPoint, lightingUpscale,
world — of which one sits inside `point` (the declared limit of this decomposition) and at least two
sit in the residual.

So `point`'s 38% and the residual's 24% BOTH contain effect-switch submits, and separating them is
the next question. That is 62% of cpu_cost whose internal split is still unknown.

## What this does not establish

That `flushImmediatePrimitives` does not block. The explicit flushes costing ~0.05 us each is strong
evidence the pipeline is not stalling this thread, but it is evidence, not proof, and the submits
inside switchEffectConfig were never separately timed.
