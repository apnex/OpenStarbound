# The game ↔ render boundary

> **Where this sits:** start from the [📇 index](README.md). This document is about the seam *below* the
> render subsystem — what `star_game` carries that only a renderer wants. The layers *above* that seam are
> [`architecture-3-target-state.md`](architecture-3-target-state.md) (L1/L2/L3) and
> [`layer1-architecture.md`](layer1-architecture.md).

## The headline, and it is not what it looks like

**A headless Starbound already exists and ships.** `starbound_server` links
`star_extern + star_core + star_base + star_game` — no `star_rendering`, no `star_application`, no
`star_windowing`, no `star_frontend` — and CI assembles it (`scripts/ci/linux/assemble.sh`).

**Zero files in `source/game` reference `Renderer`, `OpenGl`, `GL_` or `glew`.** The graphics API does not
exist below the render layer. `RenderCallback` (`StarEntityRendering.hpp`) is an abstract sink the *game*
layer defines and the *render* layer implements, and `star_rendering` includes `${STAR_GAME_INCLUDES}` and
never the reverse. The dependency arrow already points the right way.

So the thing this document measures is **not** "can we go headless". It is: **the client has no headless
expression.** `WorldClient` unconditionally produces `WorldRenderData`; every `Entity` carries
`render()` / `renderLightSources()` / `destroy(RenderCallback*)` vtable slots a dedicated server never
calls; and view code such as `TileDrawer` lives in the sim library. None of that stops a headless *server*.
All of it stops a headless *client*.

## Why a number and not an argument

The L3 decomposition already paid for this lesson (#183). A ratchet that metered three of seventeen coupling
sites was satisfiable by **relocation**, and had already been satisfied that way: eight `Root::singleton()`
reads moved out of a pass into an unmetered orchestrator, and the gate scored a perfect result for a net
change of zero. Ceilings have to exist *before* the code moves, or "better" is unfalsifiable.

This is that instrument, for a boundary nobody had counted. It counts **vocabulary in code** (comments and
string literals stripped), not includes — an include is one line whatever it drags in, while the vocabulary
is what actually has to be paid down.

## The split that decides the remedy

A single total would be the wrong headline, because the surface is two different things:

- **Appearance** — `Drawable`. An entity *saying what it looks like*. It carries no GL, no renderer and no
  frame state; it is data. A game object describing its own appearance is a legitimate game-layer duty, and
  shrinking this number is not obviously progress — it might just be moving character appearance out of the
  character. `StarHumanoid.cpp` alone holds 72 of these and is exactly that case.
- **Push sink + frame model** — everything else. Interfaces that exist so a renderer can pull work out per
  frame. **These are the ones a headless client has to be able to not have.**

Counted together they produce one big number nobody can act on. Counted apart they produce two numbers with
different remedies.

<!-- BEGIN GENERATED: scripts/boundary-inventory.py --inject -->
**The game layer's view surface, measured from the tree.** Regenerate with `scripts/boundary-inventory.py --inject docs/render/game-render-boundary.md`; `boundary_fresh` fails CI if this block and the tree disagree.

| metric | value |
|:-------|------:|
| `star_game` files | 500 |
| files free of view vocabulary | 384 |
| files naming a view concept | 116 |
| appearance vocabulary (`Drawable`) | 495 |
| **PUSH-SINK + FRAME-MODEL vocabulary** | **213** across 51 files |
| of the above, in files that are view-by-duty | 136 |

Per term:

| term | refs | files | what it is |
|:-----|-----:|------:|:-----------|
| `Drawable` | 495 | 96 | the view data model -- what to draw, produced by game code |
| `RenderCallback` | 101 | 38 | the abstract sink entities push drawables into |
| `WorldRenderData` | 18 | 6 | the frame view model the render passes consume |
| `EntityDrawables` | 3 | 3 | per-entity drawable bundle |
| `EntityRenderLayer` | 61 | 25 | draw ordering -- a render concern expressed in game types |
| `EntityHighlightEffect` | 7 | 3 | a visual effect described by the sim |
| `renderLightSources` | 23 | 19 | entity API that exists only to feed the renderer |

The twenty heaviest files:

| file | refs | terms | duty |
|:-----|-----:|:------|:-----|
| `source/game/StarHumanoid.cpp` | 72 | Drawable×72 | sim |
| `source/game/StarDrawable.cpp` | 35 | Drawable×35 | view |
| `source/game/StarEntityRenderingTypes.hpp` | 31 | EntityRenderLayer×27, EntityHighlightEffect×2, Drawable×1, EntityDrawables×1 | view |
| `source/game/StarDrawable.hpp` | 30 | Drawable×30 | view |
| `source/game/StarNetworkedAnimator.cpp` | 23 | Drawable×23 | sim |
| `source/game/StarItem.cpp` | 18 | Drawable×18 | sim |
| `source/game/StarObject.cpp` | 17 | Drawable×9, RenderCallback×6, EntityRenderLayer×1, renderLightSources×1 | sim |
| `source/game/items/StarTools.cpp` | 17 | Drawable×17 | sim |
| `source/game/items/StarActiveItem.cpp` | 14 | Drawable×13, EntityRenderLayer×1 | sim |
| `source/game/StarNetworkedAnimator.hpp` | 13 | Drawable×13 | sim |
| `source/game/StarPlayer.cpp` | 13 | EntityHighlightEffect×4, Drawable×3, RenderCallback×3, renderLightSources×2, EntityRenderLayer×1 | sim |
| `source/game/StarWorldClient.cpp` | 13 | Drawable×5, renderLightSources×4, WorldRenderData×2, EntityDrawables×1, EntityRenderLayer×1 | sim |
| `source/game/interfaces/StarBeamItem.cpp` | 13 | Drawable×13 | sim |
| `source/game/scripting/StarLuaAnimationComponent.hpp` | 12 | Drawable×7, EntityRenderLayer×5 | sim |
| `source/game/scripting/StarLuaGameConverters.cpp` | 12 | Drawable×12 | sim |
| `source/game/StarObject.hpp` | 11 | RenderCallback×6, Drawable×3, EntityRenderLayer×1, renderLightSources×1 | sim |
| `source/game/StarEntityRendering.cpp` | 10 | RenderCallback×8, Drawable×1, EntityRenderLayer×1 | view |
| `source/game/items/StarMaterialItem.cpp` | 10 | Drawable×7, RenderCallback×1, WorldRenderData×1, EntityRenderLayer×1 | sim |
| `source/game/items/StarTools.hpp` | 10 | Drawable×10 | sim |
| `source/game/StarObjectDatabase.cpp` | 9 | Drawable×9 | sim |
<!-- END GENERATED -->

## Reading the result

The push-sink figure is the one to watch, and a large part of it sits in files that are **view by duty** —
already correctly identified as view code, merely living in the sim library. Those want *moving*, not
shrinking. The remainder is genuine sim code that names a render concept, and that is the part a headless
client would have to pay down.

`TileDrawer` is the worked example, and it is why #191 stalled: it is game-layer by location and render-layer
by duty, `TilePainter` (render) *inherits* from it, and `TilePainter` cannot be sliced without it.

## The rule

**The push-sink number may not grow.** The ceiling lives in the `boundary_ratchet` registration in
`source/test/CMakeLists.txt` and is read from there by the gate rather than restated here. Lower it as the
pay-down lands; raising it is allowed but must be argued in the commit message.

It is a **total**, not per-file, and that is deliberate — the opposite call from the L3 Air-Gap ratchet, for
a reason specific to this metric. #183's finding was that a per-file ratchet over a *subset* is satisfiable
by **relocation**: closing a seam moves coupling to the composition root, so metering only the passes scored
a perfect result for a net change of zero. That failure mode cannot occur here. Moving a reference from one
`star_game` file to another leaves the total identical; the only ways down are to remove the reference or to
move the code *out* of `star_game` — which is precisely the goal. The gameable proxy and the real target
coincide, so the simplest metric is also the honest one.

No current-state number in this document is hand-typed — see the [index](README.md) for that rule and why it
exists.
