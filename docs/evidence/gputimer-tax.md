# What the GPU-timer brackets cost `lighting.gpu.cpu_cost.us` (#276)

Four interleaved 90-second legs at Desert Town, `scripts/render-profile.sh`, offscreen on the real
GPU with the sim running, deep tracing on. `on` and `off` differ **only** by the environment variable
`STAR_NO_PERPASS_GPU_TIMERS=1`, which `StarRenderer_opengl.cpp:1138-1149` uses to suppress the
per-pass `GL_TIME_ELAPSED` brackets. Raw legs: `harness/profiles/gputax-{on1,off1,on2,off2}.json`.

Run order was **on1, off1, on2, off2** — interleaved, not blocked, for the reason §3 makes concrete.

## 1. The null control, checked before anything else

With the variable set, `begin()` returns at :1136 before it reaches `m_flushPending()`. So the
brackets must still RUN the same number of times and cost almost nothing. Both halves hold:

| leg | `drive.gputimer.us` | n | `drive.point.gputimer.us` | n |
|---|---:|---:|---:|---:|
| on1 | 162,265 | 10,314 | 137,983 | 3,438 |
| off1 | **2,235** | 10,488 | **691** | 3,496 |
| on2 | 156,736 | 10,350 | 138,715 | 3,450 |
| off2 | **2,176** | 10,416 | **650** | 3,472 |

Counts are unchanged across arms; only cost collapses. The residual **0.21 µs per call** on the OFF
legs is the early-return path itself, and it is what normal play pays — `cpu_cost` is a
`TelemetryScope` and records only under deep tracing, so a live session never reaches the rest.

## 2. THE TAX IS 10.2%, AND THE 37% FIGURE WAS ATTRIBUTION, NOT CAUSATION

| key | ON µs | OFF µs | delta | ratio |
|---|---:|---:|---:|---:|
| `lighting.gpu.cpu_cost.us` | 793,052 | 712,416 | **−80,636** | 0.90× |
| `lighting.gpu.drive.point.us` | 307,348 | 297,046 | −10,302 | 0.97× |
| **`lighting.gpu.drive.upload.us`** | 131,167 | **201,192** | **+70,025** | **1.53×** |
| `lighting.gpu.drive.spread.us` | 104,882 | 103,648 | −1,234 | 0.99× |
| `lighting.gpu.drive.repack.us` | 40,120 | 40,631 | +511 | 1.01× |

Suppressing the brackets frees ~292,000 µs of bracket cost and `cpu_cost` falls only 80,636 µs,
because **`upload` grows by 70,025 µs**. The work does not vanish; it relocates.
`GlGpuTimer::begin()`/`end()` each call `m_flushPending()` (:1190, :1225) to submit pending
primitives so the query measures only the work that follows — so that submit was being BILLED to the
timer bracket. Remove the bracket and the driver performs the same submit moments later, inside
`upload`.

**The brackets are attributed 37% of `cpu_cost` and cause 10.2% of it.** The first draft of #276 led
with the 37%. That was wrong in the specific way the task's own text warned against two paragraphs
earlier — "removing the forced flushes changes WHEN the surrounding work happens, so the real figure
must be measured". It was measured, and it is a third of the claim.

## 3. WHY INTERLEAVED, AND WHAT IT CAUGHT

`cpu_cost` drifts DOWNWARD across the session, within both arms:

```
ON   810,838  ->  775,267     (-4.4%)
OFF  730,113  ->  694,719     (-4.8%)
```

That drift is larger than half the effect being measured. But the PAIRED differences are

```
on1 - off1 = 80,725
on2 - off2 = 80,548        agreeing to 0.2%
```

Pairing removes the drift completely. A blocked two-leg A/B (all ON, then all OFF, or the reverse)
would have reported anywhere from ~5% to ~15% depending on which pair it happened to draw, and
nothing in its output would have said so.

## 4. Consequences for numbers already published

`cpu_cost` is measurable only under deep tracing, which is also the only condition that pays the tax.
Every published value of it is therefore an instrumented-condition number, and the correction factor
is **0.898**.

| quantity | as published | corrected |
|---|---:|---:|
| `lighting.cpu.total.us` share of `lighting.cpu.union.us` | 25.8–28.6% | **27.4–30.4%** |
| `lighting.gpu.cpu_cost.us` share of the union | 57.9–59.4% | **55.2–56.8%** |

A 2–3 point move. [#270]'s conclusion **survives**: driving the GPU pass is still the majority of
lighting CPU, and `lighting.cpu.total.us` still captures well under a third of it.

`drive.point`'s per-CALL cost falls 178.5 → 170.6 µs (−4.4%) with the brackets off, so a small part
of what [#271] and [#272] attributed to `point` was the flush at its `end()` boundary. The direction
was predicted; the magnitude is minor and does not disturb `point`'s standing as the largest part.

## What this does NOT establish

That the 0.898 factor transfers to other scenes, or to the legs it was applied to. It was measured
today at Desert Town; [#171]'s legs are from 2026-08-15 at Desert Town on a **different content
chain** — three Workshop mods in `harness/sbinit-perf.config` were updated by Steam at 08:34 on
2026-08-22, mid-experiment. Ratios travel better than absolutes, which is why the correction is
expressed as one, but the chain difference is real and unrecorded in those legs. That gap is [#277].
