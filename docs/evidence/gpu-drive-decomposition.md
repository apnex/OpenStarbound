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

# What #272 instruments — NOT YET MEASURED

Six keys added to `StarGpuLightmapPass.cpp` to close the residual above and split `point`. **No leg
has been run against them.** Everything in this section is a question, not a reading; nothing here
may be cited as evidence until the numbers exist.

The count of switches in the residual is now exact, where §3 above could only say "at least two":
**three** — `lightingSpread` at entry, `lightingUpscale` in the upscale guard, `world` in the tail.
The fourth, `lightingPoint`, is the one inside `point`.

| key | sites | count/call | answers |
|---|---|---:|---|
| `drive.switch.us` | the 3 residual `switchEffectConfig` calls | 2 or 3 | how much of the residual is effect-switch submit |
| `drive.quad.us` | the persistent full-quad buffer | 1 | whether L3's persistence is holding |
| `drive.gputimer.us` | the non-`point` `gpuTimer` begin/end calls | 4 or 6 | what our own instrument costs inside the span it decomposes |
| `drive.bind.us` | tail `setRenderTarget({})` + `setEffectTextureFromTarget` | 2 | what handing the result back costs |
| `drive.point.switch.us` | `switchEffectConfig("lightingPoint")` | 0 or 1 | **how much of `point`'s 38% is submit, not per-light work** |
| `drive.point.gputimer.us` | the 2 `gpuTimer` calls inside `point` | 0 or 2 | the same instrument question, inside `point` |

## The summation rule, which the names carry

`drive.point.*` are **subsets** of `drive.point.us` — probes inside an existing part, never added to
it. Every other `drive.*` key is **disjoint** from the seven parts and from each other. So the
closure to check is

```
cpu_cost - SUM(seven parts + switch + quad + gputimer + bind)
```

and if that lands near zero, the residual is explained. `point.switch` and `point.gputimer` are read
against `point`, not against `cpu_cost`.

## Two corrections this makes to the text above

1. §3 said separating the switch from `point` "needs a renderer change, not a telemetry one". **Too
   strong.** Timing the switch CALL is telemetry, and now happens. What needs a renderer change is
   separating the flush *inside* the switch from the switch itself: `flushImmediatePrimitives()` is
   `switchEffectConfig`'s first statement and there is no seam between them.
2. Two call sites were restructured so the switch could be bracketed — the `lightingSpread` test and
   the `worldUpscale >= 1.5f && switchEffectConfig(...)` short-circuit. Both preserve the original
   condition exactly; the short-circuit in particular is load-bearing, since binding the upscale
   effect on frames that never use it would be a behaviour change, not a measurement one.
