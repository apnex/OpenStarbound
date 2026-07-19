<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: MODS_ONLY | we offer PR: True -->

# Framebuffer alt created on-demand by switchEffectConfig is never cleared, so the frame it is created in renders on top of undefined GPU memory

# Symptom

When an effect declares itself double-buffered but its framebuffer wasn't declared `"double": true`, the renderer notices mid-frame and creates the alt for you. There's a warning in the log when it happens:

```
Warn: Effect <name> should be double buffered, but framebuffer <name> doesn't have an alt! Making one!
```

The alt it creates is never cleared. For the rest of that frame the effect draws on top of undefined GPU memory, and any texel the effect doesn't fully overwrite is whatever was in that VRAM. On the next frame `startFrame()` picks the alt up and clears it — so for a normal `clear: true` framebuffer it self-corrects after one frame, and for a `"clear": false` one (`clear = config.getBool("clear",true);`, `source/application/StarRenderer_opengl.cpp:169`) it never does. Either way it recurs every time the framebuffers are rebuilt: `loadConfig` does `m_frameBuffers.clear()` and constructs fresh `GlFrameBuffer`s, which only get an alt if they carry `"double": true` — so any graphics-settings change or renderer config reload puts it back.

This is unrelated to the AA/multisample cluster (#104, #204, #285, #498, draft PR #510) — it reproduces with anti-aliasing off.

The mid-frame allocation hitch is the cosmetic half of this. The undefined first frame is the real one.

# The code

`makeAlt` allocates the texture with no initial data — `glTexImage2D(..., NULL)` / `glTexImage2DMultisample`, whose contents are undefined — attaches it, and returns (`source/application/StarRenderer_opengl.cpp:260-262`, `:282-295`):

```cpp
    glTexImage2D(
      GL_TEXTURE_2D, 0, internalFormat, size[0], size[1], 0, format, type, NULL);
  }
```
```cpp
  altId = 0;
  glGenFramebuffers(1, &altId);
  if (!altId)
    throw RendererException("Failed to create OpenGL framebuffer");

  glBindFramebuffer(GL_FRAMEBUFFER, altId);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, altTexture->glTextureId(), 0);

  auto framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE)
    throw RendererException("OpenGL framebuffer is not complete!");
  
  hasAlt = true;
}
```

No `glClear`. That's fine for the call site in `loadConfig` (`:320-322`), because that runs outside a frame and `startFrame()` clears both halves before anything reads them (`:803-812`):

```cpp
  for (auto& frameBuffer : m_frameBuffers) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer.second->id);
    if (frameBuffer.second->clear)
      glClear(GL_COLOR_BUFFER_BIT);
    
    if (frameBuffer.second->hasAlt) {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer.second->altId);
      if (frameBuffer.second->clear)
        glClear(GL_COLOR_BUFFER_BIT);
    }
```

It is not fine for the other call site, which runs *inside* the frame, after `startFrame()` has already been and gone (`:619-626`):

```cpp
    if (effect.doubleBuffered) {
      if (!buf->hasAlt) {
        Logger::warn("Effect {} should be double buffered, but framebuffer {} doesn't have an alt! Making one!", name, *outFrameBufferId);
        buf->makeAlt(m_screenSize);
      }
      buf->swap();
    }
    switchGlFrameBuffer(buf);
```

`swap()` (`:297-304`) then makes the brand-new, never-cleared texture the **draw target**:

```cpp
  std::swap(id,altId);
  std::swap(texture,altTexture);
  justSwapped = true;
```

so `id` is now the fresh FBO and `buf->texture` is the fresh undefined texture. (The texture the effect *samples* is `buf->altTexture` — the old, properly-cleared one — so the self-read itself is well-defined; it's the target that isn't.) The undefined content escapes through everything downstream that consumes that framebuffer in the same frame: the composite/blit to screen, another effect sampling it via `frameBufferTextures` (`:637-655`; the non-swapped path takes `buf->texture` at `:647`), and every texel the effect's own draws don't cover — which for a self-reading accumulation effect is most of them.

# Reachability — mod-only

Worth saying plainly: **vanilla can't hit this.** Upstream ships one framebuffer and no effect that qualifies as double-buffered:

`assets/opensb/rendering/opengl.config`
```json
{
  "frameBuffers" : {
    "main" : {
      "hdr":"FromSetting"
    }
  }
}
```
`assets/opensb/rendering/effects/world.config` sets `"frameBuffer": "main"` with no `blitFrameBuffer` and no `frameBufferTextures`; `assets/opensb/rendering/effects/interface.config` sets `"blitFrameBuffer": "main"` but no `frameBuffer`, so the `optString("frameBuffer")` gate at `:488` never opens. `effect.doubleBuffered` (`:488-503`) stays false for both, `makeAlt` is only ever reached from `loadConfig`, and there it's harmless. This only bites mods that add a self-reading effect — which is exactly the audience the on-demand path exists for.

# Repro

1. Patch `/rendering/opengl.config` to add a framebuffer *without* `"double": true`:
   ```json
   { "frameBuffers": { "accum": {} } }
   ```
2. Add `/rendering/effects/accum.config` that writes to it and reads it back — the `frameBufferTextures` self-reference is what flips `doubleBuffered` on at `:494-503`:
   ```json
   { "frameBuffer" : "accum",
     "frameBufferTextures" : [ { "framebuffer": "accum", "texture": "previous" } ],
     "effectShaders" : { "vertex": "accum.vert", "fragment": "accum.frag" } }
   ```
3. Register it by patching `/client.config:postProcessLayers` with a layer whose `effects` include `"accum"` (that's what drives `loadEffectConfig`, `source/client/StarClientApplication.cpp:559-576`).
4. Have the shader write only part of the target (any partial-coverage or alpha-blended draw).
5. The `should be double buffered, but framebuffer ... doesn't have an alt! Making one!` warning appears once, and that frame's uncovered texels come from undefined VRAM. Change any graphics setting to force `loadConfig` and it happens again.

An easy way to make it visible rather than driver-dependent: `glClearColor` to magenta and clear the alt in `makeAlt` — the frame that was garbage becomes uniformly magenta, which is the tell that nothing was writing those texels in the first place. (On many drivers freshly-allocated VRAM happens to come back zeroed, which is why this can hide.)

# Suggested fix

Clear the alt where it's created, so it's defined no matter which call site got there first. `makeAlt` already leaves `altId` bound to `GL_FRAMEBUFFER` at that point (`:287`), so it's a couple of lines before `hasAlt = true;` — with the same scissor guard `startFrame()` uses at `:800-801`, since `switchEffectConfig` can run with `GL_SCISSOR_TEST` enabled and a scissored `glClear` would leave the rest of the surface undefined anyway:

```cpp
  GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
  if (scissorWasEnabled)
    glDisable(GL_SCISSOR_TEST);
  glClear(GL_COLOR_BUFFER_BIT);
  if (scissorWasEnabled)
    glEnable(GL_SCISSOR_TEST);

  hasAlt = true;
```

Unconditionally, not gated on `clear` — a framebuffer that opts out of per-frame clearing wants to *accumulate*, which still needs a defined starting state, and as noted above it's exactly the case `startFrame()` will never rescue.

Optionally, and orthogonally: the mid-frame allocation could be avoided entirely by having `loadConfig` create the alt for any framebuffer that some effect names as double-buffered, instead of relying on the `"double"` key being kept in sync by hand. Then the on-demand path stays as a correct-but-unhit fallback rather than the normal way mods end up here. Happy to keep that out of scope if you'd rather.

Happy to open a PR with the clear (and the `loadConfig` pre-create, if you want it) if that's useful.
