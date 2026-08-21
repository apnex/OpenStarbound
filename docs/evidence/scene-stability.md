# Which scene is a stable fixture — four candidates, twelve legs (#278)

12 legs: 4 scenes × 3 passes, **interleaved by pass** (all four scenes in pass 1, then pass 2, then
pass 3) so thermal drift lands on every scene equally instead of being confounded with the scene.
90s each, `scripts/render-profile.sh`, offscreen on the real GPU, sim running, deep tracing on. All
on asset chain `6f763c36bbb38e08`, recorded in every leg per [#277]. Raw legs:
`harness/profiles/stab-*.json`.

The decision rule and the expectation below were **written to the board before the data existed**, so
neither could be fitted to the result. Both are quoted here unchanged.

## The rule, as pre-registered

> PRIMARY: coefficient of variation of `lighting.produce.entities.us` per frame across a scene's 3
> legs. CHOOSE: lowest primary CV, PROVIDED the scene's render cost is non-trivial — a scene that is
> cheap on every key tests nothing. EXPECT: **Desert Town has the highest CV.** If it does NOT, the
> NPC explanation is wrong and that is the finding.

## The result: the expectation is REFUTED

| scene | entities µs/f | CV | render µs/f | CV | cpu_cost µs/f | CV |
|---|---:|---:|---:|---:|---:|---:|
| **Desert Town** | 30.4 | **6.58%** | 2426.3 | **6.55%** | 164.5 | **5.84%** |
| 00-Ocean-Lab | 34.4 | 17.47% | 2350.8 | 11.70% | 266.0 | 7.74% |
| 01-Lava Refinery | 42.6 | 24.03% | 2444.8 | 14.22% | 248.1 | 12.42% |
| 03-Surface Outpost | 53.3 | 26.78% | 3175.3 | 22.30% | 462.5 | 46.45% |

**Desert Town is the most stable of the four, on every metric**, and it is the only scene stable on
all three. It passes the non-triviality guard: 2426 µs/frame of render is squarely among the others,
so its stability is not bought by measuring nothing.

The hypothesis under test was that Desert Town — a village with wandering NPCs — would be the worst
fixture, and that the static machine bases would be better. [#217]'s survey supported it
(`00-Ocean-Lab: 19,392 point of 19,392 sources`; every source a fixed point light). It is not what
the legs say. Switching to a machine base would have made the primary metric 3–4× **more** variable.

## Three of four scenes have a first-visit transient, and it does not explain them

Per leg, in pass order:

```
03-Surface-Outpost  cpu_cost  611.4 -> 559.9 -> 216.3     ~3x, monotonic
00-Ocean-Lab        cpu_cost  289.7 -> 253.0 -> 255.2     p1 high, then settles
01-Lava-Refinery    cpu_cost  254.4 -> 275.3 -> 214.6
Desert-Town         cpu_cost  153.5 -> 171.3 -> 168.8     no transient
```

Passes were interleaved, so a machine-wide drift would move all four together; Desert Town rose then
flattened, so the decline is scene-specific. Re-ranking with pass 1 discarded does **not** rescue the
bases — two get worse:

| scene | entities (p2,p3) | render | cpu_cost |
|---|---:|---:|---:|
| Desert Town | 7.80% | 6.95% | 1.47% |
| 00-Ocean-Lab | 15.53% | 5.25% | **0.90%** |
| 01-Lava Refinery | 48.27% | 28.54% | 24.77% |
| 03-Surface Outpost | 13.39% | 24.33% | 88.55% |

(Spread at n=2 — `|diff|/mean`, not a CV. It ranks; it does not measure.) Ocean-Lab becomes excellent
on `cpu_cost` and `render` once its transient is dropped, and stays poor on `entities`.

## What was actually wrong, and it was not the scene

The investigation began because two legs differed 13.6% on `lighting.produce.entities.us`. **At a CV
of 6.58%, two draws differing by 13.6% is about 1.5σ — unremarkable.** There was no anomaly to
explain; a theory was built on a single pair and three more legs dissolved it.

The real finding is worse than the one being chased and applies everywhere:

**The BEST available scene still carries ~6.6% leg-to-leg CV**, which exceeds most lever effects this
project has confirmed. The defect is not scene choice, it is UNPAIRED CROSS-RUN COMPARISON. [#276]'s
A/B was internally paired (`on1−off1`, `on2−off2`) and its paired differences agreed to **0.2%** —
against within-arm drift of 4.4–4.8%. The same afternoon, an unpaired cross-run comparison produced
"content addition made the frame 18.9% faster", which is not a result, it is the noise floor.

## What this does NOT establish

That these CVs are accurate. n=3 gives a rough estimate and the p2/p3 column is n=2. This probe
**ranks** four scenes; it does not certify any of them, and the chosen scene still needs its own
repeat count established before anything is published against it. What the ranking supports is
directional and consistent across both views: Desert Town < Ocean-Lab < {Lava Refinery, Surface
Outpost}.

Nor does it establish why the bases are noisier. They are Director-built FU installations with active
machinery, and "every source is a point light" ([#217]) was read as "static" — a point light can
belong to a machine that animates, spawns and despawns. That inference was mine and it was wrong; the
mechanism is unmeasured.
