<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: VANILLA | we offer PR: True -->

# Anti-aliasing also enables vertexRounding, which can round the two sides of a shared background edge one pixel apart (#104)

## Symptom

With **"SUPER-SAMPLED AA"** enabled, thin bright lines flash through background layers. This is #104 ("Super-sampled AA causes bright lines in the background sometimes"), whose reporter noted two details worth taking literally:

> It's not quite easy to quickly reproduce, but when you're mining into a planet you'll certainly see it show for a split second and notice that it will always happen when your camera is at a specific Y coordinate.

Transient, and tied to a *specific* camera coordinate. That is the fingerprint of a rounding knife-edge, and there is one in the AA path. Below is a mechanism in current `origin/main` that produces exactly that signature. I want to be straight about the epistemic status up front: I have verified the code path and reproduced the float32 arithmetic that makes it fire, but I have **not** captured the artifact frame-by-frame in-game and proven this particular seam is the one in #104's screenshot. So: a strongly-supported candidate cause, not a proven one.

This is **vanilla** — no mods, no custom framebuffers needed. It is also **separate from the multisample/framebuffer-sampling issue** diagnosed in draft PR #510 (that one only bites configs that sample a framebuffer texture); please don't let one triage absorb the other.

## The coupling

The AA checkbox does two unrelated things. `StarRenderer_opengl.cpp:636`, on every effect switch:

```cpp
setEffectParameter("vertexRounding", m_multiSampling > 0);
```

So `antiAliasing` (→ `setMultiSampling(4)`, `StarClientApplication.cpp:438`) silently also turns on vertex rounding. There is no way to have one without the other.

`assets/opensb/rendering/effects/world.vert:25-33`:

```glsl
void main() {
  vec2 screenPosition = (vertexTransform * vec3(vertexPosition, 1.0)).xy;

  if (vertexRounding) {
    if (((vertexData >> 3) & 0x1) == 1)
      screenPosition.x = round(screenPosition.x);
    if (((vertexData >> 4) & 0x1) == 1)
      screenPosition.y = round(screenPosition.y);
  }
```

Bits 3/4 are `rX`/`rY` (`StarRenderer_opengl.hpp:130-136`), set per-vertex in `StarRenderer_opengl.cpp:1088-1092`:

```cpp
    // Tell the vertex shader to round to the nearest pixel if the vertices form a straight
    // edge, to ensure sharpness with supersampling. If we rounded *all* vertex positions,
    // it'd cause slight visual issues with sprites rotating around a point.
    glv.pack.vars.rX = min(abs(glv.pos.x() - prev.screenCoordinate.x()), abs(glv.pos.x() - next.screenCoordinate.x())) < 0.001f;
    glv.pack.vars.rY = min(abs(glv.pos.y() - prev.screenCoordinate.y()), abs(glv.pos.y() - next.screenCoordinate.y())) < 0.001f;
```

For quads, `prev`/`next` are the adjacent corners rather than the diagonal (`:1112-1118`), so for an axis-aligned quad every vertex gets `rX = rY = 1` — every parallax/background quad has all four corners snapped to whole pixels.

## Why that opens a seam

Snapping is safe only if two quads that share an edge feed `round()` the *same float*. In the parallax tiler they don't — each tile computes its own anchor, and its far edge comes from a different expression chain. `StarEnvironmentPainter.cpp:318-321`:

```cpp
        float pixelTileLeft = pixelLeft + (x - left) * parallaxPixels[0];
        float pixelTileBottom = pixelBottom + (y - bottom) * parallaxPixels[1];

        Vec2F anchorPoint(pixelTileLeft, pixelTileBottom);
```

and `:340-345`:

```cpp
            RectF drawRect = RectF::withSize(anchorPoint, subImage.size() * camera.pixelRatio());
            primitives.emplace_back(std::in_place_type_t<RenderQuad>(), std::move(texture),
                RenderVertex{drawRect.min(), subImage.min(), drawColor, lightMapMultiplier},
                ...
```

Tile *k*'s far edge is `(pixelBottom + k*H) + H`. Tile *k+1*'s near edge is `pixelBottom + (k+1)*H`. Algebraically identical; in `float`, not bit-identical — they can differ by an ULP. Almost always harmless. But `round()` is a step function, and when the shared edge lands within an ULP of a half-pixel, the two sides step to **different pixels**.

Reproduced in float32 with upstream's exact expressions (1024px layer, `pixelRatio` 3 → `parallaxPixels[1] = 3072`, `pixelBottom = -137.5003`, `k = 1`):

```
 tile k far edge   = 6006.5              -> round() -> 6007
 tile k+1 near edge= 6006.49951171875    -> round() -> 6006
```

An exhaustive float32 sweep of `pixelBottom` over ±8000 with `H = 3072` finds ~192k anchors that split this way, so it is not a hand-picked curiosity — but each disagreement band is only a few ULPs wide, which is exactly why the artifact would appear "for a split second" and "always at a specific Y coordinate" as the camera scrolls the seam through the band, and why it's hard to screenshot on demand.

(`round()`'s direction at exactly `x.5` is implementation-chosen in GLSL, so the precise camera Y at which it fires is hardware-dependent — but the two edges being *different floats* is not.)

Terrain chunks shouldn't be affected: every chunk buffer is rendered through the same `vertexTransform` (`StarTilePainter.cpp:158-161`, in `renderTerrainChunks`), and a shared edge is the same vertex value in both chunks, so it hits `round()` as the same float and both sides snap together. It's the independently-anchored background/parallax quads that can split.

## Repro / how to test the hypothesis

1. Graphics → enable **SUPER-SAMPLED AA**.
2. Mine straight down (slow, continuous camera Y motion) on a planet with a repeating multi-tile parallax layer — per #104, bright horizontal lines flash through the parallax for a frame or two at particular camera Y values.
3. The discriminating test, which I have *not* run and would like to (or you may find it faster): edit `world.vert` to force `vertexRounding` to `false`, leaving MSAA on. If the lines disappear with multisampling still enabled, it's the rounding, not the multisampling. If they survive, this diagnosis is wrong and I'd want to know.

## Suggested fix

The robust fix is to make both sides of a shared edge derive from the *same* float, rather than hoping `round()` agrees on two different ones. Two options, not exclusive:

1. **Make the seam shared by construction, in the tiler.** In `StarEnvironmentPainter`, derive tile *k*'s far edge from tile *k+1*'s anchor (or snap the anchors to whole pixels on the CPU, where `parallaxPixels` is integral for integer `pixelRatio`), so coincident edges are bit-identical before they ever reach `round()`. Note that snapping the *transform* instead would not help here: the parallax quads go through the immediate-primitive stream whose `vertexTransform` is identity, and the two edge floats already differ on the CPU — adding an equal translation to both leaves them differing.
2. **Decouple the two settings.** Whatever the geometry fix, `StarRenderer_opengl.cpp:636` making `vertexRounding` a slave of `m_multiSampling` means users can't keep AA without the rounding. A separate config key (`vertexRounding`, defaulting to today's behaviour) would give #104 reporters a workaround that isn't "turn off AA".

Happy to put up a PR for either shape — say which you'd prefer and we'll match your conventions.
