# The walking fixture measures — and is a better basis than standing for the key that matters

Four legs, `STAR_RENDERTEST_WALK=45`, 90s each, `04-Ocean-Factory`, warm-up discarded (n=3). Run
2026-08-22 against the same scene whose STANDING floors were reproduced exactly earlier that day
(4 of 6 to three decimals, `4fcba71b`), so the two fixtures are directly comparable.

## The player really walks, and the walk is tiny

`renderTestDriveMotion` issues `m_player->moveRight()` / `moveLeft()` — the same calls the input
binding handlers make — so collision, the movement controller and the camera all see an ordinary
walk. The per-leg position lines confirm it moved rather than issuing moves the world ignored:

    frame=0    leg=0 (right)  player=(8126.000, 4020.500)
    frame=45   leg=1 (pause)  player=(8135.684, 4017.500)
    frame=90   leg=2 (left)   player=(8137.677, 4016.500)
    frame=135  leg=3 (pause)  player=(8127.993, 4020.500)

**117 samples over 5220 frames span 11.85 tiles**, first 8126.00, last 8127.07. The oscillation
caveat is not theoretical: the fixture never leaves a twelve-tile box, and ends where it started.

## Walking costs materially more — and is STEADIER where it counts

| key | stand mean | cv% | walk mean | cv% | walk/stand |
|---|---:|---:|---:|---:|---:|
| `cpu.frame.render.us` | 2202.8 | 8.95 | 2711.0 | **1.59** | 1.23× |
| `cpu.frame.work.us` | 4059.6 | 5.16 | 4902.9 | **2.56** | 1.21× |
| `lighting.cpu.union.us` | 307.0 | 2.75 | 561.8 | 7.27 | 1.83× |
| `lighting.cpu.total.us` | 76.0 | 13.95 | 175.1 | 5.44 | 2.30× |
| `lighting.produce.entities.us` | 32.7 | 7.37 | 41.0 | 19.60 | 1.26× |
| `lighting.gpu.cpu_cost.us` | 188.6 | 3.74 | 330.4 | 10.72 | 1.75× |

Two results, and the second is the one that matters for [#286].

**Motion is expensive and every standing fixture we own is blind to it.** Walking adds 23% to
render CPU and 21% to total frame CPU; it nearly doubles lighting CPU (union +83%, total +130%).
Whatever we have concluded about lighting cost, we concluded it about a player standing still.

**A walking leg is MORE differenceable than a standing one on `cpu.frame.render.us`** — CV 1.59%
against 8.95%. That is the exact parent [#286] wants to decompose, and the moving fixture gives a
bar roughly five times tighter. The frame-level keys tighten (render, work); the lighting keys
loosen (union, entities, cpu_cost).

A PLAUSIBLE READING, NOT AN ESTABLISHED ONE: standing, the frame cost is dominated by whatever
incidental variation the caches produce, and is lumpy; walking imposes a sustained periodic workload
that averages out across a 90s window. Lighting goes the other way because recomputes are triggered
BY movement, so their count depends on where the window boundaries fall in the walk cycle. That
story fits all six rows and has not been tested against anything.

## What this does NOT establish

**n=3.** A CV from three legs is a rough figure, and the standing floors it is compared against are
also n=3. Neither is precise; the 5.6× ratio on `render.us` is far larger than that imprecision,
which is why it is quotable at all.

**The number is plausibly a FLOOR on rebuild cost, not a representative one.** Eleven point eight
five tiles of oscillation re-treads chunks that are warm after the first pass. Sustained travel
would miss continuously. If the decomposition later reads "buffer upload is small", that is evidence
about this fixture before it is evidence about the engine.

**No interaction.** No placing, so `StarMaterialItem`'s placement preview — which drives the scratch
`WorldRenderData` that [#191] calls its real blocker — is never exercised.

**Whether the walk stays on loaded terrain is NOT answered.** Twelve tiles makes it very likely, but
"likely" is not measured, and a leg that walked off the loaded region would measure chunk LOADING
rather than chunk REBUILD.

## THE FLOOR IS NOW RECORDED, BECAUSE THE PREREQUISITE WAS MET

This section previously said the floor was deliberately absent: **a walking leg and a standing leg
were indistinguishable in the artefact**, both recording `meta.scene = "04-Ocean-Factory"` and
neither recording the fixture, so a walking CV under that scene key would have handed a moving bar
to standing comparisons — [#285]'s basis mismatch, manufactured rather than found.

The prerequisite it named has been met. `render-profile.sh` passes `--walk` to the analyser, the
analyser writes `meta.walk`, `leg-diff.py` derives `fixture` from it, refuses to compare across
fixtures, and looks floors up under `fixtures.<name>.<scene>` with **no fallback** to the still
entry. The walk floor for 04-Ocean-Factory is recorded in `scene-floor.json` and re-derived by
`scripts/ci/floor-reproduces.py` on every gate run.

**The four legs behind these figures were re-run, not relabelled.** The original bank predated the
wiring and self-reported `still`. Backfilling `meta.walk` by hand would have made my say-so the
evidence rather than the artefact, so the legs were measured again with the profiler writing the
field. Each of the four now asserts `walk=45` itself.

### Walking is the STEADIER fixture here, which was not the expected direction

| key | still CV | walk CV |
|---|---|---|
| `lighting.cpu.total.us` | 13.952% | **6.372%** |
| `cpu.frame.render.us` | 8.947% | **5.736%** |
| `lighting.gpu.drive.point.us` | 3.912% | **2.881%** |
| `lighting.gpu.cpu_cost.us` | 3.735% | **3.530%** |
| `lighting.cpu.union.us` | 2.751% | 4.745% |
| `lighting.produce.entities.us` | 7.368% | 10.157% |

Only the two keys whose *content* genuinely moves get noisier. So the moving fixture is not a
noisier instrument than the standing one — at this scene it is a better one. That matters for
[#274]: the border lever is unresolvable standing, because its effect is smaller than the still
fixture's own 13.95% noise on the key it moves. At 6.37% it may be resolvable here unpaired.

### And two n=3 banks of the same thing do not agree on the noise

The discarded pre-wiring bank was the same binary, scene, fixture and asset chain. Its CVs disagree
with the recorded ones by up to 3.8× — `produce.entities` 19.60 vs 10.16, `drive.point` 11.03 vs
2.88, `cpu_cost` 10.72 vs 3.53. **Three legs do not pin a CV.** A floor derived from three is a
usable refusal threshold, which is all `leg-diff` asks of it, and is not a precise figure — here or
in the `scenes` table, which is derived the same way. This is [#268] arriving from a second
direction: a between-bank component exists that a within-bank spread cannot see.
