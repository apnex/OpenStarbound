<!-- STATUS: DRAFT — NOT FILED. Do not publish without the Director's approval. -->
<!-- Target: OpenStarbound/OpenStarbound issues. Verified against upstream/main 2c7f972b6. -->
<!-- Our fix: 691392929 (RB-2). Pairs with shader-fallback.md — together they are the crash. -->

# A failed effect reload destroys the working effect it was replacing

`loadEffectConfig` deletes the old program and removes the effect from the registry **before** it has
compiled a single line of the new one (`StarRenderer_opengl.cpp:331-334`):

```cpp
if (auto effect = m_effects.ptr(name)) {
  Logger::info("Reloading OpenGL effect {}", name);
  glDeleteProgram(effect->program);
  m_effects.erase(name);
}
```

The link check, ~55 lines later, throws (`:386-389`):

```cpp
glGetProgramiv(program, GL_LINK_STATUS, &status);
if (!status) {
  glGetProgramInfoLog(program, sizeof(logBuffer), NULL, logBuffer);
  glDeleteProgram(program);
  throw RendererException(strf("Failed to link program: {}\n", logBuffer));
}
```

So any failure between those two points leaves the effect **permanently absent**: its program deleted, its
registry entry gone, and no path that restores it. `switchEffectConfig(name)` then returns `false` forever.

That return value is ignored at the two call sites that matter — `StarClientApplication.cpp:460`
(`"world"`) and `:440` / `:485` (`"interface"`) — so those layers go on drawing under whatever effect
happened to be bound last, rather than reporting anything.

`loadEffectConfig` is reachable at runtime, not just at boot: `renderReload()` is registered as a root
reload listener, so it runs on every asset reload.

### Why this matters even though the link "shouldn't" fail

It does fail, routinely, because of the companion bug: the "using default" fallback attaches no shaders, so
a mod with one bad shader reaches `glLinkProgram` with an empty program. Mesa rejects that
(`error: no shaders attached to the program`), the exception propagates, and the game **aborts**.

Fixing either issue alone is an improvement; fixing both turns a crash into a logged warning and a working
game on the default shader.

### Suggested fix

Do not destroy anything until the new program has linked. Compile and link into a local `program`, and only
then replace the registry entry:

```cpp
// ... compile, link, throw on failure -- the registry is still untouched ...

if (auto existing = m_byName.ptr(name)) {
  Logger::info("Reloading OpenGL effect {}", name);
  glDeleteProgram(existing->program);
  m_byName.erase(name);
}
auto& effect = m_byName.emplace(name, Effect()).first->second;
effect.program = program;
```

A failed reload then costs an error in the log and nothing else: the old effect keeps rendering.
