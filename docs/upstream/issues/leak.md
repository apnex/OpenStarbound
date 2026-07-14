<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: MODS_ONLY | we offer PR: True -->

# ~GlFrameBuffer deletes `id` but not `altId`, so every AA/HDR toggle orphans a double-buffered framebuffer (and its texture's storage)

### Symptom

With a mod that uses a double-buffered framebuffer, VRAM grows by roughly one full-screen texture every time the player toggles anti-aliasing or HDR in the video settings. It never comes back. Flip AA on and off a few times in the settings menu and usage climbs monotonically.

This is **mod-only** — nothing upstream reaches the double-buffered path:

```json
// assets/opensb/rendering/opengl.config
{
  "frameBuffers" : {
    "main" : {
      "hdr":"FromSetting"
    }
  }
}
```

`"main"` has no `"double"` key, so `loadConfig` never calls `makeAlt` (`:320-322`). The one shipped effect that targets a framebuffer, `assets/opensb/rendering/effects/world.config:2` (`"frameBuffer" : "main"`), sets no `blitFrameBuffer` and no `frameBufferTextures`, so `Effect::doubleBuffered` (`source/application/StarRenderer_opengl.cpp:488-505`) stays false and the lazy path at `:619-623` never fires either. Filing it because mods do use the feature, and because the fix is two lines.

### The code

`makeAlt()` creates a second FBO and a second texture (`source/application/StarRenderer_opengl.cpp:282-288`):

```cpp
  altId = 0;
  glGenFramebuffers(1, &altId);
  if (!altId)
    throw RendererException("Failed to create OpenGL framebuffer");

  glBindFramebuffer(GL_FRAMEBUFFER, altId);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, altTexture->glTextureId(), 0);
```

The destructor only knows about one of them (`source/application/StarRenderer_opengl.cpp:306-309`):

```cpp
OpenGlRenderer::GlFrameBuffer::~GlFrameBuffer() {
  glDeleteFramebuffers(1, &id);
  texture.reset();
}
```

`altId` is never passed to `glDeleteFramebuffers`, so the FBO name leaks.

The part that makes it a *storage* leak rather than just a name leak: `altTexture` is a `RefPtr<GlLoneTexture>` member, so its destructor does run and does call `glDeleteTextures` (`:968-971`) — but that texture is still attached to the orphaned `altId` as `COLOR_ATTACHMENT0` (`:288`). Per GL 4.5 §5.1.2, deleting a texture detaches it only from framebuffer objects **bound to the current context**; the orphaned FBO isn't bound, so it keeps its attachment and the texture object stays alive with its storage. So the leaked bytes are the alt texture's, held hostage by the leaked FBO.

(`swap()` at `:301` swaps `id`/`altId`, so which of the two survives is whichever was last swapped in — the leaked amount is the same either way.)

### Repro

Two ways for a mod to get an alt framebuffer; both leak.

**A. Declare it up front.**
```json
// rendering/opengl.config patch
"frameBuffers": { "myBuf": { "double": true } }
```
`loadConfig` calls `makeAlt()` directly (`:320-322`). No effect needs to reference it.

**B. Don't declare it at all.** Ship an effect whose `frameBuffer` equals its `blitFrameBuffer`, or whose `frameBufferTextures` references its own output framebuffer — either makes `doubleBuffered` true (`:488-505`), and the render path then creates the alt lazily (`:619-623`):

```cpp
    if (effect.doubleBuffered) {
      if (!buf->hasAlt) {
        Logger::warn("Effect {} should be double buffered, but framebuffer {} doesn't have an alt! Making one!", name, *outFrameBufferId);
        buf->makeAlt(m_screenSize);
      }
```

Then, either way:

1. Launch, get in-world, watch process VRAM (`intel_gpu_top`, `nvidia-smi`, RenderDoc — whatever's handy).
2. Open video settings and toggle anti-aliasing (or HDR) on/off repeatedly.

Each toggle calls `setMultiSampling` / `setMainHDR`, both of which end in `loadConfig(m_config)` (`:706`, `:714`), and `loadConfig` starts with `m_frameBuffers.clear()` (`:312`) — so every toggle destroys and rebuilds every framebuffer, leaking one alt FBO plus one alt texture's storage each time. In case B the alt is then re-created on the very next frame by `:622`, so the cycle repeats cleanly per toggle.

The leaked texture is full screen size, because `setScreenSize` reallocates the alt at screen resolution (`:778-780`, `:791-793`). Per toggle, per doubled framebuffer, at 2560×1440:

| config | leaked per toggle |
|---|---|
| RGBA8 | ~14 MB |
| HDR (RGBA16F) | ~29 MB |
| 4× multisample (RGBA8; `alpha` is forced true at `:177`) | ~59 MB |

A player fiddling with the video menu can shed hundreds of MB in a minute.

### Suggested fix

```cpp
OpenGlRenderer::GlFrameBuffer::~GlFrameBuffer() {
  glDeleteFramebuffers(1, &id);
  texture.reset();
  if (hasAlt) {
    glDeleteFramebuffers(1, &altId);
    altTexture.reset();
  }
}
```

Deleting the FBO first drops the attachment, so the subsequent texture delete actually frees the storage.

Worth noting *why* this was easy to miss, since it points at the more durable fix: `makeAlt()` (`:231-295`) is a near-verbatim copy of the constructor body (`:160-229`) — the comment on `:232` says as much (*"...this is a lot of repeated code. unfortunately it's also rather difficult to make it not repeated."*). The constructor's paired cleanup lives in the destructor; the copy in `makeAlt` had no paired cleanup to copy. Factoring the shared "allocate texture + FBO at target/format" body into one helper that both call would make the destructor obviously symmetric. That's a refactor though — the leak fix above stands on its own.

Happy to open a PR with the destructor fix (and optionally the `makeAlt`/ctor dedup as a separate commit) if that's useful.
