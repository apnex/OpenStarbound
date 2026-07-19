<!-- STATUS: DRAFT — NOT PUSHED, NOT OPENED. Do not publish without the Director's approval. -->
<!-- Target: OpenStarbound/OpenStarbound  <-  apnex/OpenStarbound:pr/postprocess-passes -->
<!-- Our commit: a1ec80598. Verified against upstream/main 2c7f972b6. -->
<!-- Closes the issue draft: postprocess-passes.md -->

# Fix: `passes` is a no-op for double-buffered effects — the early-out returns before the swap

Closes #____.

### The problem

A post-process layer's `passes` runs its effects repeatedly. For a **double-buffered** effect — one that
samples the framebuffer it writes, which is what the `"double"` framebuffers from #542 exist to support —
every pass after the first does nothing.

`switchEffectConfig` early-outs when the effect is already bound (`StarRenderer_opengl.cpp:610-611`):

```cpp
Effect& effect = find->second;
if (m_currentEffect == &effect)
  return true;
```

That returns **before** the swap, the target rebind, and the `frameBufferTextures` rebind (`:619-625`):

```cpp
if (effect.doubleBuffered) {
  if (!buf->hasAlt) { ... }
  buf->swap();            // never reached on a repeat bind
}
switchGlFrameBuffer(buf);
```

and the render loop binds the same effect once per pass (`StarClientApplication.cpp:476-480`):

```cpp
for (unsigned i = 0; i < layer.passes; i++)
  for (auto& effect : layer.effects) {
    renderer->switchEffectConfig(effect);
    renderer->render(quad);
  }
```

So a layer with one effect and `passes: 2` swaps on the first pass and early-outs on the second — which then
draws into the very face it just wrote while sampling that same face. (Binding a texture as both sampler
source and draw target is undefined per the GL spec.) The iteration does not iterate, which is a shame,
because iterating a feedback shader is the entire reason `passes` exists.

Mod-facing rather than vanilla-facing — no shipped effect is double-buffered and no shipped framebuffer
declares `"double"`, which is presumably why nobody has hit it. `passes` is also reachable from Lua via
`setPostProcessLayerPasses` (`StarClientApplication.cpp:582`).

### The fix

A double-buffered effect has per-invocation work, and being already bound does not excuse it:

```cpp
if (m_currentEffect == &effect && !effect.doubleBuffered)
  return true;
```

The early-out is kept for the ordinary case; the ping-pong now actually iterates.

### Testing

Built a minimal mod to exercise the path, since nothing in-tree does: a framebuffer with `"double": true`;
an effect whose `frameBuffer` and whose `frameBufferTextures` entry name that same framebuffer (which is how
`doubleBuffered` is derived, `:485-501`); an accumulating fragment shader; a layer with `"passes": 2`.

Instrumenting `buf->swap()` and the early-out, on Mesa / Intel Arc:

```
BEFORE   306 swap()  and  306 early-out   across 306 frames   -- one of each, every frame
AFTER    612 swap()  and    0 early-out                       -- exactly double; both passes ping-pong
```

The ordinary (non-double-buffered) path is unchanged: three GPU pixel oracles report 0 diff, GL errors 0,
`core_tests` 226/226.
