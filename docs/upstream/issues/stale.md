<!-- STATUS: DRAFT -- NOT FILED. as drafted. -->
<!-- reachability: MODS_ONLY | we offer PR: True -->

# Effect framebuffer-texture handles are not re-resolved after loadConfig rebuilds the framebuffers (AA/HDR toggle leaves the effect sampling the old, orphaned texture)

## Symptom

After the renderer reloads its framebuffer set at runtime (toggling anti-aliasing or HDR in Options does this), an effect that samples a framebuffer through `frameBufferTextures` keeps sampling the **previous, now-orphaned** framebuffer's texture. The bind succeeds — no GL error — so the effect quietly renders a frozen image (whatever was last drawn into the old attachment before the reload) instead of this frame's content, until the renderer is fully reloaded.

To be precise about what this is and isn't: it is **not** a use-after-free. `EffectTexture::textureValue` is a `RefPtr<GlLoneTexture>` (`StarRenderer_opengl.hpp:187`), so the old texture object — and its GL name — stay alive as long as the effect holds the reference. It is a **stale binding**: a live handle to a texture nothing renders into anymore.

This is a *second, independent* way to get garbage out of a framebuffer sampler, distinct from the multisample bind failure (PR #510's diagnosis, which fails the bind with `GL_INVALID_OPERATION`). This one binds cleanly and samples the wrong texture.

## Upstream code

`loadConfig` drops and rebuilds every framebuffer — each new `GlFrameBuffer` allocates a brand-new `GlLoneTexture` — but it does not touch `m_effects`:

```cpp
// source/application/StarRenderer_opengl.cpp:311
void OpenGlRenderer::loadConfig(Json const& config) {
  m_frameBuffers.clear();

  for (auto& pair : config.getObject("frameBuffers", {})) {
    Json config = pair.second;
    config = config.set("multisample", m_multiSampling);
    config = config.set("hdrSetting", m_hdrSetting);
    Logger::info("Creating framebuffer {}", pair.first);
    auto buf = make_ref<GlFrameBuffer>(config);
```

It is reached at runtime from both setting hooks:

```cpp
// source/application/StarRenderer_opengl.cpp:692
void OpenGlRenderer::setMultiSampling(unsigned multiSampling) {
  if (m_multiSampling == multiSampling)
    return;
  ...
  loadConfig(m_config);       // :706
}

void OpenGlRenderer::setMainHDR(bool enabled) {
  if (m_hdrSetting == enabled)
    return;

  m_hdrSetting = enabled;
  loadConfig(m_config);       // :714
}
```

…and both are polled every frame from the client:

```cpp
// source/client/StarClientApplication.cpp:437
  renderer->setMultiSampling(config->get("antiAliasing").optBool().value(false) ? 4 : 0);
  renderer->setMainHDR(config->get("hdr").optBool().value(true));
```

The only place an effect's framebuffer-sourced sampler slot is (re-)resolved is `switchEffectConfig`, and it re-points the slot **only** when the handle is null-or-zero, or when the framebuffer is double-buffered:

```cpp
// source/application/StarRenderer_opengl.cpp:637
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
```

After `loadConfig`, the stale `RefPtr` is neither null nor zero — the effect still owns the old `GlLoneTexture`, so `textureId != 0` — so `undefined` is `false`. For a single-buffered source framebuffer `buf->hasAlt` is also `false`, and the slot is never re-pointed. The subsequent draw binds the old texture and succeeds:

```cpp
// source/application/StarRenderer_opengl.cpp:1263
    for (auto const& p : m_currentEffect->textures) {
      if (p.second.textureValue) {
        glActiveTexture(GL_TEXTURE0 + p.second.textureUnit);
        glBindTexture(GL_TEXTURE_2D, p.second.textureValue->textureId);
```

(A double-buffered source is re-pointed every switch by the `buf->hasAlt` arm, so it recovers by accident, not by design.)

The full-reload path is fine, because `renderReload()` (`StarClientApplication.cpp:508`) calls `loadEffectConfig` too, which rebuilds `effect.textures` with a null `textureValue`. It is specifically the `setMultiSampling` / `setMainHDR` path — `loadConfig` without a matching effect reload — that leaves the effect pointing at the old framebuffer.

## Reachability: MODS ONLY (as shipped)

Worth saying plainly so this triages correctly: **vanilla cannot hit this today.** Upstream ships one framebuffer (`assets/opensb/rendering/opengl.config` → `"main"`) and **zero** `frameBufferTextures` entries anywhere in `assets/` — `world.config` uses `"frameBuffer"` (a render *target*), not a framebuffer *sampler*. So this only bites mods/forks that add a framebuffer and sample it from an effect. It is also the first thing such a mod hits, since AA/HDR are user-facing toggles.

## Repro

1. Add a framebuffer to `/rendering/opengl.config` (single-buffered, no `"double"`), and an effect whose config declares `frameBufferTextures: [{ "framebuffer": "<that fb>", "texture": "<sampler name>" }]` plus a matching `effectTextures` entry.
2. Run, confirm the effect samples correct, live content.
3. In-game, toggle **Anti-Aliasing** (or **HDR**) in Options. This calls `setMultiSampling`/`setMainHDR` → `loadConfig` → every framebuffer is rebuilt.
4. The effect now samples the old texture: its output freezes on the pre-toggle content. `glGetError()` is clean at the bind site; the GL name bound is simply the previous framebuffer's, not the new one's.

A one-line print of `p.second.textureValue->textureId` at `:1266` before and after the toggle confirms it in seconds: the id doesn't change, while the new framebuffer's `texture->textureId` does.

## Suggested fix

Invalidate the effects' framebuffer-sourced sampler slots when the framebuffers they point at are destroyed. The smallest change that matches the existing `undefined` contract is to null them in `loadConfig` before `m_frameBuffers.clear()`, e.g.:

```cpp
void OpenGlRenderer::loadConfig(Json const& config) {
  for (auto& effect : m_effects) {
    if (auto fbts = effect.second.config.optArray("frameBufferTextures")) {
      for (auto const& fbt : *fbts) {
        if (auto ptr = effect.second.textures.ptr(fbt.getString("texture")))
          ptr->textureValue.reset();   // force re-resolve in switchEffectConfig
      }
    }
  }
  m_frameBuffers.clear();
  ...
```

That also makes `undefined` true on the next switch, so the `textureSizeUniform` (`:649`) gets refreshed against the new texture as well — worth noting, because the new framebuffer's size can differ from the old one's when `multisample`/`hdr` changes the format path.

An alternative, if you'd rather not have `loadConfig` reach into effects: re-resolve unconditionally in `switchEffectConfig` (drop the `undefined || buf->hasAlt` guard and always assign `ptr->textureValue = swapped ? buf->altTexture : buf->texture`) — it's a `RefPtr` assignment, and correctness stops depending on whether a stale handle happens to look "defined".

Related, since they all land in this same corner of the renderer: #104, #204, #285, #498, and draft PR #510.

Happy to open a PR for this if you'd like it.
