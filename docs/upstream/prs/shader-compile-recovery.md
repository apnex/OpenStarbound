<!-- STATUS: DRAFT — NOT PUSHED, NOT OPENED. Do not publish without the Director's approval. -->
<!-- Target: OpenStarbound/OpenStarbound  <-  apnex/OpenStarbound:pr/shader-compile-recovery -->
<!-- Our commits: 691392929 (both fixes). Verified against upstream/main 2c7f972b6. -->
<!-- Closes the two issue drafts: shader-fallback.md, shader-erase-before-link.md -->

# Fix: one bad shader in a mod takes the game down, and the "default" fallback compiles no default

Closes #____ (fallback compiles nothing), closes #____ (failed reload destroys the working effect).

### TL;DR

Append one line of non-GLSL to any effect's `.frag` and boot:

```
[Error] Shader compile error, using default: (RendererException) Failed to compile fragment shader:
        0:31(1): error: illegal use of reserved word `this'
[Error] Fatal Exception caught: (RendererException) Failed to link program:
        error: no shaders attached to the program
Aborted (core dumped)
```

Two independent bugs in `loadEffectConfig` combine into that. Separately each is survivable; together they
are a crash, and the log claims a recovery that never happened.

---

### 1. The fallback passed the shader source where a map key was expected

The compile lambda's second parameter is a **key into `shaders`** (`StarRenderer_opengl.cpp:340-342`):

```cpp
auto compileShader = [&](GLenum type, String const& name) -> GLuint {
  GLuint shader = glCreateShader(type);
  auto* source = shaders.ptr(name);      // <-- KEY
  if (!source)
    return 0;
```

The recovery path passes it `DefaultVertexShader` — a `char const*` holding a `#version 150` shader body
(`:364-368`). `shaders` is keyed `"vertex"` / `"fragment"`, so the lookup misses, `0` comes back, and
**nothing is attached to the program**. `"Shader compile error, using default"` announces a fallback that
did not occur.

`no shaders attached to the program` in the crash log above is the proof.

The parameter was doing three jobs — key, source, and error label. This PR splits it into a label and a
source, and hoists the lookup to the call site, so the fallback compiles the actual defaults. It also
deletes the shader handle on the throw path, which previously leaked.

### 2. The old program was destroyed before the new one was built

`loadEffectConfig` opens by deleting the old program and erasing the registry entry (`:331-334`) — ~55 lines
before the link check that throws (`:386-389`). Any failure in between leaves the effect **permanently
absent**, and `switchEffectConfig(name)` returns `false` from then on. That return is ignored for both
`"world"` (`StarClientApplication.cpp:460`) and `"interface"` (`:440`, `:485`), so those layers would go on
drawing under whatever effect was bound last.

This PR compiles and links first, and replaces the registry entry only on success. A failed reload now costs
an error in the log and nothing else: the old effect keeps rendering.

`loadEffectConfig` is reachable at runtime, not only at boot — `renderReload()` is a registered root reload
listener, so this fires on any asset reload, not just startup.

---

### After

Same broken shader, same boot:

```
[Error] Shader compile error in effect 'interface', falling back to the built-in default:
        (RendererException) Failed to compile fragment shader of effect 'interface':
        0:31(1): error: illegal use of reserved word `this'
```

exit 0. The default program compiles and links, the game runs, and the message names the effect and the
stage that failed.

### Testing

- Verified on Mesa / Intel Arc, GL 4.6 core, both before and after.
- The engine's own test suites pass unchanged (`core_tests` 226/226).
- The ordinary (non-failing) load path is byte-identical: three GPU pixel oracles comparing cached against
  direct render paths report 0 diff over the run, and the GL error count is 0.

### Notes for review

The two fixes are separable and I'm happy to split them into two PRs if you'd prefer — I've kept them
together because either one alone still leaves the crash reachable by the other, and reviewing them as one
story is much easier than as two.
