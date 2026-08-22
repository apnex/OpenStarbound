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

## THE FLOOR IS NOT RECORDED, AND THAT IS DELIBERATE

`scene-floor.json` has no entry for these numbers. **A walking leg and a standing leg are
indistinguishable in the artefact**: both record `meta.scene = "04-Ocean-Factory"` and neither
records the fixture. So writing a walking CV under that scene key would hand a moving bar to
standing comparisons and a standing bar to moving ones — which is [#285]'s basis mismatch,
manufactured rather than found.

The prerequisite is that the leg records its fixture: `meta.walk` written by `render-profile.sh`,
and floors keyed on scene AND fixture. Until then these figures live here, cited, and are not
loaded by any consumer.
