<!-- STATUS: DRAFT — NOT FILED. Do not publish without the Director's approval. -->
<!-- Target: OpenStarbound/OpenStarbound issues. Verified against upstream/main 2c7f972b6. -->
<!-- Our fix: a1ec80598 (RB-4). A #542 double-buffering defect. -->

# `passes` does nothing for a double-buffered effect — the early-out returns before the swap

A post-process layer's `passes` is meant to run its effects repeatedly. For a **double-buffered** effect —
one that samples the framebuffer it writes, which is exactly what #542's `"double"` framebuffers exist for —
every pass after the first is a no-op.

`switchEffectConfig` opens with an "already bound, nothing to do" early-out
(`StarRenderer_opengl.cpp:610-611`):

```cpp
Effect& effect = find->second;
if (m_currentEffect == &effect)
  return true;
```

It returns **before** the swap (`:619-625`):

```cpp
if (effect.doubleBuffered) {
  if (!buf->hasAlt) { ... buf->makeAlt(m_screenSize); }
  buf->swap();            // <-- never reached on a repeat bind
}
switchGlFrameBuffer(buf); // <-- nor this, nor the frameBufferTextures rebind below
```

And the render loop binds the same effect once per pass (`StarClientApplication.cpp:476-480`):

```cpp
for (unsigned i = 0; i < layer.passes; i++) {
  for (auto& effect : layer.effects) {
    renderer->switchEffectConfig(effect);
    renderer->render(quad);
  }
}
```

So for a layer with a single effect and `passes: 2`, the first pass swaps and draws; the second early-outs,
does **not** swap, and draws into the very face it just wrote — while sampling that same face. (A texture
bound simultaneously as a sampler source and as the draw target is undefined per the GL spec.) The
iteration does not iterate.

This is mod-facing rather than vanilla-facing: no shipped effect is double-buffered and no shipped
framebuffer declares `"double"`, which is presumably why it has gone unnoticed. `passes` is also exposed to
Lua via `setPostProcessLayerPasses` (`StarClientApplication.cpp:582`).

### Repro

A minimal mod: a framebuffer with `"double": true`; an effect whose `frameBuffer` and whose
`frameBufferTextures` entry name that same framebuffer (which is how the loader derives `doubleBuffered`,
`:485-501`); an accumulating fragment shader; and a layer with `"passes": 2`. Instrumenting `buf->swap()` and
the early-out:

```
BEFORE   306 swap()  and  306 early-out   across 306 frames   -- one of each, every frame
AFTER    612 swap()  and    0 early-out                       -- exactly double; both passes ping-pong
```

### Suggested fix

A double-buffered effect has per-invocation work; being already bound does not excuse it. One clause:

```cpp
if (m_currentEffect == &effect && !effect.doubleBuffered)
  return true;
```

The optimisation is kept for the ordinary case, and the ping-pong actually iterates.
