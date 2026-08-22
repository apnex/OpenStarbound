# #274 BORDER-REDERIVE — the lever is real, and it is the OFF arm that moved

Pre-registration: `docs/evidence/border-rederive-preregistration.md`, committed as `85741414`
**before** the run. Direction, magnitude, both denominators and the decision rule were fixed in
advance; what follows applies that rule unchanged, including where it did not go my way.

## Result

`scripts/render-ab.sh 90 border --warp 04-Ocean-Factory --repeats 5 --warmup 1`, arms interleaved
a,b,a,b, one warm-up discarded, pair tags written into the artefacts so `leg-diff.py` reads the
pairing rather than inferring it. Legs banked at `harness/profiles/border-p{0..4}{a,b}.*`.

| key | paired mean | sd | 95% CI | verdict |
|---|---|---|---|---|
| `lighting.cpu.total.us` | **+48.19%** when OFF | 26.74 | **[+15.0, +81.4]** | **CONFIRMED** |
| `lighting.gpu.cpu_cost.us` | +9.10% | 7.58 | [−0.30, +18.50] | UNRESOLVED |
| `lighting.cpu.union.us` | — | — | — | ABSENT |

Per-pair differences on `lighting.cpu.total.us`, all five positive:

    pair 0   on  83.4   off 144.0   +72.61%
    pair 1   on  77.3   off 121.9   +57.63%
    pair 2   on  83.5   off 123.4   +47.73%
    pair 3   on  67.7   off 108.2   +59.93%
    pair 4   on  91.5   off  94.3    +3.07%

**CONFIRMED per the pre-registered rule:** direction is a saving, unanimous across all five pairs,
and the paired interval excludes zero.

## The real finding: the ON arm reproduces, the OFF arm does not

[#217] measured **the same key at the same scene** on 2026-08-04 — `lighting.cpu.total.us`,
77.8 on vs 86.5 off, quoted as −10.1%. So the denominators are directly comparable and [#171]'s
denominator correction does not apply to this comparison. That was worth checking before quoting a
ratio, and it comes out clean.

|  | #217 (2026-08-04, ONE pair) | #274 (today, five pairs) |
|---|---|---|
| **ON** arm | 77.8 | mean 80.7, range [67.7, 91.5] |
| **OFF** arm | 86.5 | mean 118.4, range [94.3, 144.0] |

**#217's ON sits inside today's ON range. Its OFF sits below today's entire OFF range, by +36.8%.**

The shipped default is stable across eighteen days. What moved is the *counterfactual* — what it
costs to run without the border. So the headline is not "the lever got three times better"; it is
**the lever's value is measured against a baseline that has itself become more expensive**, and the
saving is therefore larger now than when it was first quoted.

DO NOT restate this as "[#217] was wrong by 3x". Two reasons. Its ON arm reproduces exactly, which is
the half a wrong measurement would have got wrong. And its A/B was a **single adjacent pair** — n=1,
no spread at all — so its 86.5 has unknown uncertainty and may simply be a low sample; today's
lowest OFF leg is 94.3, which is not far off.

## What did not survive, and is reported as such

- **`lighting.gpu.cpu_cost.us` is UNRESOLVED.** Mean +9.10%, but one pair goes the other way (−1.88%)
  and the interval straddles zero. The pre-registration says an unresolved key is reported unresolved
  and is not backfilled with the prior figure. It is not evidence the lever is free on the GPU-driver
  cost; it is evidence this run could not separate it.
- **`lighting.cpu.union.us` was ABSENT from every leg.** I pre-registered reporting against both
  denominators and could deliver only one. The union was still summed by hand into a markdown table
  rather than computed by the analyser — precisely [#269]'s outstanding item. Recorded as a
  commitment not met, rather than dropped.

  **DELIVERED LATER THE SAME DAY, and it changes the headline.** [#269] made the union
  analyser-computed, [#281] made it diffable, and [#283] found the reason it still read absent — a
  consumer not reading the leg's `zeroed` list, not a missing member. Its floor was then derived from
  the same banked legs behind every other figure at this scene. On the identical five pairs:

  | denominator | paired difference | spread |
  |---|---:|---:|
  | `lighting.cpu.total.us` — about a **quarter** of lighting CPU | +46.69% | 69.53pp |
  | `lighting.cpu.union.us` — **all** of it | **+20.45%** | 26.31pp |

  Same legs, same saving in microseconds, two denominators. **The border lever is worth ~20% of
  lighting's real CPU cost**, and every larger figure quoted for it — this document's +48.19%
  included — is denominated against a part. That is [#171]'s defect arriving at the measurement site,
  which is exactly why the pre-registration demanded both. The union is also the steadier basis: its
  spread is a third of the part's, because summing seven members averages away variance one member
  carries alone.

  The +46.69% here and the +48.19% above are the same comparison under two estimators — leg-diff
  takes a ratio of means, the table above is a mean of per-pair ratios. Neither is wrong; they are
  not interchangeable and are not quoted as one number.

## Pair 4 is an outlier, and chasing it cannot change the verdict

+3.07% against +47…+73% for the other four; it is what drives sd 26.74 and the wide interval.
Including it makes the result MORE conservative — removing it would tighten the CI and enlarge the
effect. So it is not a threat to the verdict and was deliberately not pursued as the close-out.
It is worth understanding before the magnitude is quoted precisely, and its OFF leg (94.3) is also
the one nearest [#217]'s 86.5, which may be the same phenomenon seen twice.

## Method note: the paired design was necessary

`04-Ocean-Factory`'s unpaired floor on this key is **13.95%** (measured today, 1 warm-up + 3 legs).
The effect [#217] reported is 10.1% — *smaller than the scene's own unpaired noise* — so no repeat
count would have let an unpaired A/B adjudicate it. The effect turned out large enough (~48%) that
unpaired might have caught it by luck, but that could not be known in advance, and
`lighting.gpu.cpu_cost.us` at +9.10% is exactly the case where unpaired fails and paired still
cannot resolve. [#279] built `render-ab.sh` for this.

## Open

1. **Why did the OFF arm get more expensive since 2026-08-04?** The highest-value follow-up. A
   `git log` over the lighting path between 817e54e8 and HEAD, or a pre-#217 binary at this scene,
   would settle whether this is a real regression in the no-border path or sampling.
2. Pair 4's behaviour.
3. `lighting.cpu.union.us` needs [#269] before the second denominator can ever be reported.
4. [#217]'s own three outstanding items are untouched by this: the unwrapped-position defect is fixed
   but untested, there is no pixel confirmation that dropped lights returned, and the +5.3% give-back
   remains derived rather than executed.
