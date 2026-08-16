# Producer-side lighting CPU — first reading (#171)

Two 90-second legs, Desert Town, `scripts/render-profile.sh`, offscreen on the real GPU with the sim
running. Deep tracing on (harness/storage-perf pins it). Binary at the #171 instrumentation commit
plus the adjust-cadence fix. Raw legs: `harness/profiles/produce171-{a,b}.json` (harness/ is
gitignored; this table is the tracked half, as with matrix-20260808-140430).

TWO LEGS GIVE A RANGE, NOT A CONFIDENCE INTERVAL. n=2 supports no significance claim; every figure
below is quoted as the interval the two legs spanned.

| key | A total us | B total us | A n | B n | A us/frame | B us/frame |
|---|---:|---:|---:|---:|---:|---:|
| `lighting.produce.entities.us` | 145,633 | 129,577 | 4,499 | 4,500 | 32.37 | 28.79 |
| `lighting.produce.prep.us` | 6,535 | 5,308 | 4,499 | 4,500 | 1.45 | 1.18 |
| `lighting.produce.adjust.us` | 326 | 346 | 1,742 | 1,737 | 0.07 | 0.08 |
| `lighting.gpu.spread_scan.us` | 26,494 | 24,760 | 1,742 | 1,737 | 5.89 | 5.50 |
| `lighting.produce.particles.us` | 2,913 | 2,134 | 4,499 | 4,500 | 0.65 | 0.47 |
| `lighting.cpu.total.us` | 311,270 | 338,090 | 4,499 | 4,500 | 69.19 | 75.13 |
| `lighting.gpu.cpu_cost.us` | 716,670 | 684,120 | 1,742 | 1,737 | 159.30 | 152.03 |
| `cpu.frame.render.us` | 11,109,626 | 10,602,396 | 4,499 | 4,500 | 2469.35 | 2356.09 |

`lighting.produce.particles.us` NESTS inside `prep` and is excluded from every sum below — that is
what MetricRole::Detail permits and why it is declared Detail.

## The result

| quantity | leg A | leg B |
|---|---:|---:|
| producer side (4 keys) | 178,988 us | 159,991 us |
| `lighting.cpu.total.us` (the consumer — the denominator every lighting % uses) | 311,270 us | 338,090 us |
| producer as % of that denominator | 57.5% | 47.3% |
| **fraction the denominator actually captures** | **63.5%** | **67.9%** |
| producer as % of `cpu.frame.render.us` | 1.61% | 1.51% |

**`lighting.cpu.total.us` captures roughly two thirds (63.5–67.9%) of the CPU that is lighting work.**
About a third has never been counted by any lighting percentage this project has published.

**But it is ~1.5% of the render frame.** The producer side is 35.6–39.8 us/frame against a
cpu.frame.render.us of 2356–2469 us/frame. It materially changes the DENOMINATOR of lighting
percentages; it does not reveal a large new slice of frame time.

**81% of it is one call site**: the `forAllEntities` / `renderLightSources` walk, 28.8–32.4 us/frame.
`prep` (1.2–1.5), `spread_scan` (5.5–5.9), `adjust` (0.07–0.08) are the rest.

## Two things this reading does NOT establish

1. `lighting.gpu.cpu_cost.us` is 152–159 us/frame — LARGER than the consumer and the producer
   combined. It is owner `frame`, Detail, and was already named, so it is outside #171's scope
   (which was about UNNAMED cost). But any future statement of the form 'lighting costs X' has to
   say whether it includes the CPU cost of driving the GPU pass. Today none of them do.
2. Whether #168's 16.9% cut and #170/#217's border percentages move. Those were measured against
   the old denominator on their own scenes; re-deriving them is a separate re-measurement.
