<!-- STATUS: DRAFT -- NOT FILED. CORRECTED by maintainer-review. -->
<!-- reachability: VANILLA | we offer PR: True -->

# A graphics setting that throws at apply-time is unrecoverable: the value is already on disk, so the game aborts on every subsequent launch

## Symptom

Graphics settings are persisted to `storage/starbound.config` and re-applied on the first rendered frame of every launch, before the title screen. If applying one throws a `RendererException`, the process aborts — and because the value is already on disk, it aborts again on the next launch, and the next. There is no way back to the options menu to undo it; recovery requires hand-editing `storage/starbound.config`.

This is the *recovery* half of #498 ("crash with SSAA + shaders"), and it seems worth fixing independently of whatever triggers the throw there. We're not claiming to know what throws on those users' machines, and we haven't reproduced a specific throw ourselves — only that when something does throw at the apply site, there is nothing to catch it and nothing to fall back to. (Unrelated to the multisample bind issue diagnosed in draft PR #510 — different mechanism, filed separately so they can be triaged apart.)

## The chain, in upstream source

**1. The setting is written to the live config the moment the box is ticked** — the checkbox callback bypasses the Apply button entirely:

`source/frontend/StarGraphicsMenu.cpp:84-89`
```cpp
  reader.registerCallback("antiAliasingCheckbox", [=](Widget*) {
    bool checked = fetchChild<ButtonWidget>("antiAliasingCheckbox")->isChecked();
    m_localChanges.set("antiAliasing", checked);
    Root::singleton().configuration()->set("antiAliasing", checked);
    syncGui();
  });
```

**2. Root's maintenance thread flushes the live config to disk on a 5-second timer:**

`source/game/StarRoot.cpp:59`, `:150-155`
```cpp
  unsigned const RootMaintenanceSleep = 5000;
...
        {
          MutexLocker locker(m_configurationMutex);
          writeConfig();
        }

        m_maintenanceStopCondition.wait(m_maintenanceStopMutex, RootMaintenanceSleep);
```
`Root::writeConfig()` (`:740-750`) overwrites `storage/starbound.config` — the runtime config file registered at `StarClientApplication.cpp:163`. (`~Root()` also calls `writeConfig()`, but that's irrelevant here: `std::abort()` runs no destructors, so the 5-second flush is the only path that persists a value from a session that later dies.)

**3. It is re-applied every frame, from `render()`, with no guard:**

`source/client/StarClientApplication.cpp:438-439`
```cpp
  renderer->setMultiSampling(config->get("antiAliasing").optBool().value(false) ? 4 : 0);
  renderer->setMainHDR(config->get("hdr").optBool().value(true));
```
This sits above the `m_state` switch in `render()`, so on a fresh launch with `antiAliasing: true` on disk it runs on frame 1 — during Splash/Mods, before the title screen exists.

**4. The setter tears down and rebuilds every framebuffer, and that path throws:**

`source/application/StarRenderer_opengl.cpp:692-707`
```cpp
void OpenGlRenderer::setMultiSampling(unsigned multiSampling) {
  if (m_multiSampling == multiSampling)
    return;

  m_multiSampling = multiSampling;
  if (m_multiSampling) {
    glEnable(GL_MULTISAMPLE);
    ...
  }
  loadConfig(m_config);
}
```
`loadConfig` (`:311-328`) clears `m_frameBuffers` and reconstructs each one; `GlFrameBuffer::GlFrameBuffer` and `makeAlt()` throw `RendererException` at `:167`, `:221`, `:228`, `:239`, `:285`, `:292` (`"Could not generate OpenGL texture for framebuffer"`, `"Failed to create OpenGL framebuffer"`, `"OpenGL framebuffer is not complete!"`). `setMainHDR` (`:709-715`) has the identical shape.

**5. The only handler above it aborts the process:**

`source/application/StarMainApplication_sdl.cpp:758-760`, `:786-788`
```cpp
        m_renderer->startFrame();
        m_application->render();
        m_renderer->finishFrame();
...
    } catch (std::exception const& e) {
      Logger::error("Application: exception thrown!");
      fatalException(e, true);
    }
```
and `fatalException` (`source/core/StarException_unix.cpp:165-177`, and the Windows twin) ends in `std::abort();`.

There is no `try`/`catch` between `render()` and that abort, and no safe-mode / last-known-good path on the config side.

## How a machine ends up in the loop

The precondition is that `antiAliasing: true` is *already on disk* when a launch begins. Worth being precise about this, because ticking the box in a session where the apply throws immediately does **not** get you there: the throw fires on the next frame (~16 ms), `abort()` skips `~Root()`, and the 5-second flush never runs — nothing is written, and the next launch is clean.

The realistic routes in are the ones where the setting was persisted while it still worked:

- AA was enabled and worked (the flush at step 2 ran); a shader mod adding framebuffers, a driver update, a resolution/fullscreen change, or VRAM pressure later makes the same rebuild fail. Framebuffer allocation failure is inherently environment-dependent, which is exactly why it can persist first and fail later.
- The config was hand-written, copied between machines, or shipped in a modpack.

## Repro

1. Put `"antiAliasing" : true` in `storage/starbound.config`.
2. If applying it throws on your driver + effect-config + resolution combination, the game aborts on the first rendered frame.
3. Relaunch. It aborts again, at the same point, before the title screen — so the options menu is unreachable.
4. Recovery requires editing `storage/starbound.config` by hand.

Step 2 is the environment-dependent part. Steps 3-4 follow unconditionally from the code above once step 2 happens for *any* reason. Users in #285 and #498 are being told to turn SSAA off — which is the one thing they can no longer reach the menu to do.

## Suggested fix

Guard the apply site and revert the offending key to its default rather than dying:

- Wrap the `setMultiSampling` / `setMainHDR` calls in `ClientApplication::render()` in a `try`/`catch (RendererException const&)`.
- On throw: `Logger::error(...)`, then `configuration->set(key, configuration->getDefault(key))` (`Configuration::getDefault` already exists, `source/base/StarConfiguration.cpp:34-37`) and re-invoke the setter with the default value. Re-running the setter matters: `loadConfig` does `m_frameBuffers.clear()` *before* it can throw, so simply swallowing the exception would leave the framebuffer map partially populated.
- Optionally surface a one-line notice on the title screen so the user learns the setting was reverted.

That keeps the game launchable no matter what a driver does with a given framebuffer/sample-count combination, and it makes #498-class reports recoverable without a text editor.

Happy to open a PR for this if you'd like it.
