# #274 BORDER-REDERIVE — pre-registered, before the run

Written **before** `render-ab.sh` was launched, so nothing below can be fitted to the result. The
rule this follows is the one that made [#278] trustworthy: state the expectation and the decision
rule first, then run, then quote both regardless of which way it went.

## What is being re-derived

[#217] shipped the adaptive lighting border (`817e54e8`) and measured it at the one scene that
exercises it: **−27.8% of the dirty region, −10.1% of lighting CPU**, with the correctness fix
costing +5.3% of region. The −10.1% is among the campaign's most-quoted numbers.

[#270] split this out as [#274] because that measurement predates the discipline the campaign has
since adopted: paired arms ([#279]), a declared scene floor ([#278]), and a stated denominator
([#171] — the lighting whole was a third short at the time).

## The measurement problem, settled BEFORE the run

`04-Ocean-Factory`'s floor was measured today, 1 warm-up + 3 legs:

| key | unpaired CV |
|---|---|
| `lighting.cpu.total.us` | **13.95%** |
| `lighting.gpu.cpu_cost.us` | 3.61% |
| `lighting.gpu.drive.point.us` | 3.81% |
| `cpu.frame.render.us` | 8.95% |

**The effect (−10.1%) is smaller than the unpaired floor (13.95%) on its own key.** An unpaired
comparison cannot resolve it: the two-sided bar is `1.96 × cv × sqrt(1/na + 1/nb)`, which at n=4 per
arm is still ~19%. This is not a reason to doubt [#217]; it is a reason the unpaired method cannot
adjudicate it either way.

So this runs **paired** — `render-ab.sh`, arms interleaved a,b,a,b, one warm-up discarded, pair tags
written into the artefacts so `leg-diff.py` reads the pairing rather than inferring it. Pairing
removes the between-leg component that this floor is made of; [#276] is the existence proof, where
drift within each arm ran 4.4–4.8% while the paired differences agreed to 0.2%.

## Pre-registered expectation

1. **Direction:** turning `lightingAdaptiveBorder` OFF should make lighting CPU *worse* (the lever is
   a saving). A result in the other direction refutes [#217] outright and must be reported as such.
2. **Magnitude:** ~10% of `lighting.cpu.total.us`, from [#217]. I do **not** predict the number will
   reproduce exactly — [#170]'s −23.6% became −10/−11% when re-measured, so this family of figures
   has already moved once under better method.
3. **Denominator:** reported against **both** `lighting.cpu.total.us` (comparable to [#217]) **and**
   `lighting.cpu.union.us` (the whole [#269] declares). Two denominators because [#171] showed the
   original one was a third short, and a percentage without its denominator named is the defect this
   campaign keeps finding.

## Decision rule, fixed in advance

- **CONFIRMED** if the paired difference clears the paired bar and the direction is a saving. Quote
  it with both denominators.
- **UNRESOLVED** if it does not clear the bar. Then say so plainly — *this run could not separate the
  lever from its noise* — and do **not** fall back to quoting [#217]'s figure as though re-derived.
  An unresolved lever is not a lever that costs nothing.
- **REFUTED** if the direction reverses and clears the bar.

## What must not happen

- No quoting the −10.1% as "confirmed" on the strength of a run that merely failed to contradict it.
- No comparing this scene's delta to another scene's — that is the cross-scene error withdrawn today.
- No result at all if the arms do not both engage: `render-ab.sh` aborts on a bad spec token, and the
  witness must move.
