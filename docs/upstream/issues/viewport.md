<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: MODS_ONLY | we offer PR: True -->

# Framebuffers with sizeDiv (or an explicit size) are rendered with the window-sized viewport, so their contents are magnified and clipped

## Symptom

A framebuffer declared with `"sizeDiv": 2` (or an explicit `"size"`) has an attachment smaller than the window, but draws into it still go through the window-sized viewport. The result is that the effect's output is magnified by `sizeDiv` and cropped to the lower-left corner of the attachment — only about `1/sizeDiv²` of the intended image survives, and the rest is clipped away. `blitFrameBuffer` on such a buffer is affected too.

**Reachability: mod-only.** Upstream's `assets/opensb/rendering/opengl.config` ships exactly one framebuffer, `main`, with no `sizeDiv` and no `size`, so nothing in-tree hits this. It only bites mods that try to use the half-res / fixed-size framebuffer feature that the config format already advertises. Flagging it because the feature reads as supported and silently isn't.

**This predates #542.** `git show c88faca84^:source/application/StarRenderer_opengl.cpp` shows the same shape (`sizeDiv` at :179, `effectScreenSize` at :514-518, `switchGlFrameBuffer` at :1222 with no viewport call, the lone `glViewport` at :649). Double buffering did not introduce it; it just happens to be the first feature standing next to it.

## The code

`glViewport` is called in exactly one place in the entire renderer — on window resize:

```cpp
// source/application/StarRenderer_opengl.cpp:762-765
void OpenGlRenderer::setScreenSize(Vec2U screenSize) {
  m_screenSize = screenSize;
  glViewport(0, 0, m_screenSize[0], m_screenSize[1]);
  glUniform2f(m_screenSizeUniform, m_screenSize[0], m_screenSize[1]);
```

Binding a framebuffer as the draw target does not touch it:

```cpp
// source/application/StarRenderer_opengl.cpp:1349-1356
void OpenGlRenderer::switchGlFrameBuffer(RefPtr<GlFrameBuffer> const& frameBuffer) {
  if (m_currentFrameBuffer == frameBuffer && !frameBuffer->justSwapped)
    return;

  frameBuffer->justSwapped = false;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->id);
  m_currentFrameBuffer = frameBuffer;
}
```

Meanwhile the *shader* is already told the target is smaller — `switchEffectConfig` divides the `screenSize` uniform by `sizeDiv`:

```cpp
// source/application/StarRenderer_opengl.cpp:613-633 (elided)
  auto effectScreenSize = m_screenSize;

  auto outFrameBufferId = effect.config.optString("frameBuffer");
  if (outFrameBufferId) {
    auto buf = getGlFrameBuffer(*outFrameBufferId);
    effectScreenSize = m_screenSize / (buf->sizeDiv);
    ...
    switchGlFrameBuffer(buf);
  } else {
    m_currentFrameBuffer.reset();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  }

  glUseProgram(m_program = effect.program);
  setupGlUniforms(effect, effectScreenSize);
```

So the shader-side half of the feature is implemented and the viewport-side half isn't. For an effect on the world vertex path, `assets/opensb/rendering/effects/world.vert:49` maps pixels to clip space with that uniform:

```glsl
gl_Position = vec4(screenPosition / screenSize * 2.0 - 1.0, 0.0, 1.0);
```

With `sizeDiv = 2` on a `W×H` window: `screenPosition` in `[0, W/2]` fills NDC `[-1, 1]`, NDC `[-1, 1]` fills the *viewport*, and the viewport is still `W×H` — but the attachment is only `W/2 × H/2` (allocated as such at :777/:790). Everything past `W/2` is off the attachment and is dropped. Net: 2× magnification plus a crop to the bottom-left quarter.

A full-screen post effect lands in the same place by the other half of the bug: `assets/opensb/rendering/effects/basic.vert:6` is `gl_Position = vec4(vertexPosition, 0.0, 1.0);` and never reads `screenSize`, so its quad covers NDC exactly — and the oversized viewport alone magnifies it and pushes three quarters of it off the smaller attachment. Either vertex path, same symptom.

Two smaller things in the same family, from the same missing notion of "the target's real size":

- **`overrideSize` is not in the uniform math.** Line 618 divides by `sizeDiv` only, so a framebuffer declared with an explicit `"size"` (ctor :181-184) gets the *window's* `screenSize` uniform as well as the window's viewport.
- **The blit rects are window-sized too**, so a `sizeDiv`/`size` framebuffer can't be blitted back correctly either — the read rect runs off the source attachment:

```cpp
// source/application/StarRenderer_opengl.cpp:1339-1347
void OpenGlRenderer::blitGlFrameBuffer(RefPtr<GlFrameBuffer> const& frameBuffer, bool const& useAlt) {
  auto& size = m_screenSize;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (useAlt && frameBuffer->hasAlt) ? frameBuffer->altId : frameBuffer->id);
  glBlitFramebuffer(
    0, 0, size[0], size[1],
    0, 0, size[0], size[1],
    GL_COLOR_BUFFER_BIT, GL_NEAREST
  );
}
```

## Repro (60 seconds)

In a mod's `rendering/opengl.config`:

```json
{
  "frameBuffers" : {
    "main" : { "hdr" : "FromSetting" },
    "half" : { "sizeDiv" : 2 }
  }
}
```

Point any effect at `"frameBuffer": "half"`, then blit or sample it back. Expect a half-res copy of the scene; observe the bottom-left quarter, 2× zoomed. A `glGetIntegerv(GL_VIEWPORT)` right after `switchGlFrameBuffer` reads back the full window size regardless of which framebuffer is bound.

## Suggested fix

`makeAlt` already computes the number every one of these sites needs:

```cpp
// source/application/StarRenderer_opengl.cpp:247
  Vec2U size = overrideSize ? *overrideSize : (screenSize / sizeDiv);
```

Lifting that into one accessor on `GlFrameBuffer` and using it everywhere the code currently open-codes window-size math would cover all three:

1. set the viewport when the draw target changes — `glViewport(0, 0, size[0], size[1])` in `switchGlFrameBuffer`, and `glViewport(0, 0, m_screenSize[0], m_screenSize[1])` in the `else` branch of `switchEffectConfig` (:627-630) that returns to the default framebuffer. Note the early-out at :1350-1351 relies on `m_currentFrameBuffer` matching GL's actual state, and `startFrame` (:817) / `finishFrame` (:844) both bind framebuffer 0 without clearing it — harmless in-tree, since `interface.config` has no `frameBuffer` and so nulls it via the else branch every frame, but any viewport tracking added here probably wants to invalidate in those two spots too rather than inherit the assumption;
2. use the same accessor for `effectScreenSize` at :618 so `overrideSize` buffers get the right `screenSize` uniform;
3. derive the `glBlitFramebuffer` source rect from the source framebuffer's size (dest stays `m_screenSize`), which also makes a `sizeDiv` buffer upscale correctly on the way back out.

That keeps the current behaviour byte-for-byte for the shipped `main` framebuffer (`sizeDiv == 1`, no `overrideSize` → `size() == m_screenSize`), which is the only framebuffer `world.config` and `interface.config` touch.

Happy to open a PR for this if it'd be useful — just say the word.
