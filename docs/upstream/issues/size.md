<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: MODS_ONLY | we offer PR: True -->

# Framebuffer textures never record their allocated size, so a shader's `textureSizeUniform` for a `frameBufferTextures` entry always reads (0, 0)

## Symptom

An effect that samples a framebuffer via `frameBufferTextures` gets `(0, 0)` in the matching `textureSizeUniform`. The texture itself binds and samples fine (with anti-aliasing off — with it on, framebuffer-sourced samplers hit a separate problem), so only its declared size is wrong. The shader can't compute a texel size, and any `1.0 / size` neighbourhood sample — blur, edge detect, dilate, any texel-snapped read — divides by zero.

The same `textureSizeUniform` mechanism works correctly for a CPU-uploaded effect texture, which is a useful internal control: this is specific to framebuffer-backed textures.

Vanilla ships zero `frameBufferTextures` entries (`assets/opensb/rendering/effects/world.config` only uses `effectTextures` + `setEffectTexture`), so **this is mod-facing only** — it doesn't affect a stock install. Filing it because it's a small, contained defect in a feature that's otherwise fully wired up.

## The code

The `<name>Size` uniform is fed from `GlLoneTexture::glTextureSize()`, which returns the `textureSize` member verbatim (`source/application/StarRenderer_opengl.cpp:989-991`):

```cpp
Vec2U OpenGlRenderer::GlLoneTexture::glTextureSize() const {
  return textureSize;
}
```

For a framebuffer's texture, `textureSize` is initialised to `{0, 0}` and never assigned again. Constructor, `:160-164`:

```cpp
OpenGlRenderer::GlFrameBuffer::GlFrameBuffer(Json const& fbConfig) : config(fbConfig) {
  texture = make_ref<GlLoneTexture>();
  texture->textureFiltering = TextureFiltering::Nearest;
  texture->textureAddressing = TextureAddressing::Clamp;
  texture->textureSize = {0, 0};
```

`makeAlt`, `:233-236`, does the same:

```cpp
  altTexture = make_ref<GlLoneTexture>();
  altTexture->textureFiltering = TextureFiltering::Nearest;
  altTexture->textureAddressing = TextureAddressing::Clamp;
  altTexture->textureSize = {0, 0};
```

The storage *is* allocated — in the ctor, in `makeAlt`, and again on every resize in `setScreenSize` (`:762-790`) — but the record is never updated alongside it:

```cpp
      glBindTexture(GL_TEXTURE_2D, frameBuffer.second->texture->glTextureId());
      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_screenSize[0] / sizeDiv, m_screenSize[1] / sizeDiv, 0, format, type, NULL);
```

So when `switchEffectConfig` hands the framebuffer texture to the effect and pushes the size uniform (`:637-657`), it pushes zeroes:

```cpp
  if (auto fbts = effect.config.optArray("frameBufferTextures")) {
    for (auto const& fbt : *fbts) {
      if (auto frameBufferId = fbt.optString("framebuffer")) {
        auto textureUniform = fbt.getString("texture");
        auto ptr = m_currentEffect->textures.ptr(textureUniform);
        if (ptr) {
          auto undefined = !ptr->textureValue || ptr->textureValue->textureId == 0;
          auto swapped = effect.doubleBuffered && (*frameBufferId).equals(*outFrameBufferId);
          auto buf = getGlFrameBuffer(*frameBufferId);
          if (undefined || buf->hasAlt) {
            auto texture = swapped ? buf->altTexture : buf->texture;
            ptr->textureValue = texture;
            if (ptr->textureSizeUniform != -1 && undefined) {
              auto textureSize = ptr->textureValue->glTextureSize();
              glUniform2f(ptr->textureSizeUniform, textureSize[0], textureSize[1]);
            }
          }
        }
      }
    }
  }
```

The control, right next door — `setEffectTexture` (`:589-600`) records the size at upload and re-pushes the uniform every call, which is why `lightMapSize` in the stock `world` effect is correct:

```cpp
  if (!ptr->textureValue || ptr->textureValue->textureId == 0) {
    ptr->textureValue = createGlTexture(image, ptr->textureAddressing, ptr->textureFiltering);
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->textureValue->textureId);
    ptr->textureValue->textureSize = image.size;
    uploadTextureImage(image.format, image.size, image.data);
  }

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, textureSize[0], textureSize[1]);
  }
```

There's a second-order issue in the same block: the size push is gated on `&& undefined` (`:649`), i.e. it only ever happens on the *first* switch to that effect. Even with `textureSize` correctly recorded, a window resize would reallocate the framebuffer at a new size and the uniform would keep the old value.

## Repro (~2 min, no code changes)

A post-process effect that samples the existing `main` framebuffer — no new framebuffer needed.

1. Register the effect as a post-process layer, otherwise `renderReload` never loads it (`source/client/StarClientApplication.cpp:512-563`):

```json
// client.config.patch
[
  { "op" : "add", "path" : "/postProcessLayers/-", "value" : { "effects" : [ "sizeprobe" ] } }
]
```

2. `rendering/effects/sizeprobe.config`:

```json
{
  "frameBufferTextures" : [ { "framebuffer" : "main", "texture" : "mainTex" } ],
  "effectTextures" : {
    "mainTex" : {
      "textureUniform" : "mainTex",
      "textureSizeUniform" : "mainTexSize"
    }
  },
  "effectShaders" : {
    "vertex" : "sizeprobe.vert",
    "fragment" : "sizeprobe.frag"
  }
}
```

3. In `sizeprobe.frag`, output the uniform directly:

```glsl
uniform sampler2D mainTex;
uniform vec2 mainTexSize;
// ...
gl_FragColor = vec4(mainTexSize / vec2(2048.0), 0.0, 1.0);
```

4. The screen goes pure black. Expected: a red/green tint proportional to the framebuffer's resolution. The same shader reading `lightMapSize` (a CPU-uploaded texture) in the `world` effect shows the tint, so the plumbing between `textureSizeUniform` and the shader is fine.

## Is there another way for a mod to get the size?

Only partially, and I'd rather be straight about it than overstate the impact:

- `setupGlUniforms` (`:1301`, `:1309`) gives the shader a `screenSize` uniform — but it's the size of the effect's *own* render target (`effectScreenSize = m_screenSize / (buf->sizeDiv)` at `:618`), not the size of the framebuffer being sampled. It coincidentally equals the sampled framebuffer's size in the common case (`sizeDiv: 1`, no `size` override), so a mod can lean on `screenSize` there. The moment the sampled framebuffer has `sizeDiv > 1` or an explicit `"size"`, there is no correct value available.
- `renderer.setEffectParameter` from Lua can push a `vec2` in, but the mod has to know the size out-of-band and re-push it on every resize; nothing in `StarRenderingLuaBindings.cpp` exposes a framebuffer's dimensions to Lua.

So `frameBufferTextures` + `textureSizeUniform` works today only for the `sizeDiv: 1` case, and only if the shader ignores its own `textureSizeUniform` and uses `screenSize` instead — which rather defeats having the uniform.

## Suggested fix

Record the size where the storage is actually allocated — the ctor, `makeAlt`, and both branches of `setScreenSize` — e.g. `texture->textureSize = size;` next to each `glTexImage2D` / `glTexImage2DMultisample`. Then drop the `&& undefined` gate at `:649` (or re-push when the recorded size differs) so the uniform survives a resize and a double-buffered swap.

Happy to open a PR for this if you'd like it.
