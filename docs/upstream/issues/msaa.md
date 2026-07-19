<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: MODS_ONLY | we offer PR: True -->

# Framebuffers are allocated as GL_TEXTURE_2D_MULTISAMPLE when antiAliasing is on, but effect textures are bound as GL_TEXTURE_2D (GL_INVALID_OPERATION; the unit keeps its previous binding)

### Prior art

@emmyposs already diagnosed this in **draft PR #510** (2026-04-24), which hasn't had a review yet. This issue isn't a new diagnosis — it's a confirmation of theirs, with a hardware repro and GL error counts attached, in the hope it helps the PR get triaged. Credit for finding it goes to them.

Very likely the same root cause as **#204** ("shaders jumpscare you with the texture atlas when SSAA is enabled") and **#285** (whose current workaround is "turn off Super-Sampled Anti Aliasing").

### Symptom

With `antiAliasing` on, any effect that samples a framebuffer (via `frameBufferTextures`) samples **the texture atlas** instead — or renders black, if the unit happened to hold nothing. The sampler silently reads whatever `GL_TEXTURE_2D` was last bound to that texture unit.

### Why

`setMultiSampling` → `loadConfig` stamps the setting onto *every* framebuffer, overwriting the framebuffer's own key, so nothing can opt out — `source/application/StarRenderer_opengl.cpp:311-316`:

```cpp
void OpenGlRenderer::loadConfig(Json const& config) {
  m_frameBuffers.clear();

  for (auto& pair : config.getObject("frameBuffers", {})) {
    Json config = pair.second;
    config = config.set("multisample", m_multiSampling);
```

That makes the framebuffer's texture a multisample texture — `:171-172`:

```cpp
  multisample = GLEW_VERSION_4_0 ? config.getUInt("multisample", 0) : 0;
  GLenum target = multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
```

But the effect-texture bind site binds unconditionally to `GL_TEXTURE_2D` — `:1263-1268`:

```cpp
    for (auto const& p : m_currentEffect->textures) {
      if (p.second.textureValue) {
        glActiveTexture(GL_TEXTURE0 + p.second.textureUnit);
        glBindTexture(GL_TEXTURE_2D, p.second.textureValue->textureId);
      }
    }
```

Binding a name whose target is already `GL_TEXTURE_2D_MULTISAMPLE` to `GL_TEXTURE_2D` is `GL_INVALID_OPERATION`. Per spec the call is a **no-op**: the unit *retains its previous* `GL_TEXTURE_2D` binding — in practice an atlas page, which is exactly what #204 shows on screen. The shader's `sampler2D` then samples it happily, with no error surfaced.

(`switchEffectConfig` is what routes a framebuffer's texture into `effect.textures[...].textureValue` — `:638-657` — and that's what later reaches the bind above. It's the same `m_currentEffect->textures` map that CPU-uploaded textures from `setEffectTexture` (`:582`) live in, which gives a nice in-frame control — see below.)

### Repro / measurements

Since vanilla has no `frameBufferTextures` (see reachability below), the repro needs a config that routes a framebuffer into a `sampler2D` — i.e. what a mod does. With that in place and `antiAliasing` on, one gameplay session, instrumented with a per-bind probe (`want` = the texture we asked to bind, `bound` = what the unit actually holds afterwards):

```
uniform=emission   unit=2  want=10  bound=10  err=0x0     <- CPU-uploaded texture (setEffectTexture): binds fine
uniform=lightMap   unit=4  want=7   bound=0   err=0x502   <- framebuffer-sourced texture: INVALID_OPERATION
uniform=lightMap   unit=4  want=7   bound=11  err=0x502   <- the bind is a NO-OP; unit KEEPS its old binding
uniform=lightMap   unit=4  want=7   bound=13  err=0x502
```

The CPU-uploaded sampler in the **same frame**, through the **same** bind loop, binds cleanly — only the framebuffer-sourced sampler fails. `bound=0` is the black case; `bound=11`/`13` are live atlas pages, which is the #204 case.

Session totals, with the only difference between the two legs being whether multisample is forced onto the *sampled* framebuffer:

| | as upstream is today | with per-framebuffer opt-in |
|---|---|---|
| `GL_INVALID_OPERATION` at bind | 60,267 | 0 |
| failing samplers | all framebuffer-sourced | none |
| mean world luminance | 0.016682 | 0.257642 |

(Intel Arc, Mesa 25.x.)

### Reachability — stock OpenStarbound is fine, and I want to be plain about that

Vanilla ships exactly one framebuffer and zero `frameBufferTextures`: `assets/opensb/rendering/opengl.config` declares only `"main"`, and `assets/opensb/rendering/effects/interface.config` has `"blitFrameBuffer" : "main"` — so `main` is *resolved* to the default framebuffer with `glBlitFramebuffer` (`:1339-1347`) and never sampled. Blitting a multisample FBO is the one correct thing to do with one, so **an unmodded client never hits this**. It bites the moment a mod (or a future engine feature) samples a framebuffer, which is the situation in #204 and #285.

For triage: this predates PR #542 — both the bind site and the forced-multisample stamp go back to 8a8a05015 (2024-04-08). #542 is just the first feature to stand on them.

### Suggested fix

Make multisampling a **per-framebuffer opt-in** instead of a global stamp: only a framebuffer that is MSAA-resolved to the screen via `glBlitFramebuffer` (i.e. `main`) may be `GL_TEXTURE_2D_MULTISAMPLE`; anything that is *sampled* must stay single-sample. Concretely, in `loadConfig`, honour the framebuffer's own key rather than overwriting it — e.g.

```cpp
config = config.set("multisample", config.getBool("allowMultisample", false) ? m_multiSampling : 0);
```

— and set `"allowMultisample" : true` on `"main"` in `opengl.config`. That's the right-hand column in the table above: error count goes to zero with AA still on and MSAA still doing its job on the screen target. It is substantially what #510 proposes.

Happy to open a PR if #510 isn't the preferred route — but emmyposs got here first and it should get the first look.
