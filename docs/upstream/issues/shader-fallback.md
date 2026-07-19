<!-- STATUS: DRAFT — NOT FILED. Do not publish without the Director's approval. -->
<!-- Target: OpenStarbound/OpenStarbound issues. Verified against upstream/main 2c7f972b6. -->
<!-- Our fix: 691392929 (RB-3). Pairs with shader-erase-before-link.md — together they are the crash. -->

# The "using default" shader fallback compiles no default

`loadEffectConfig`'s compile lambda takes its second parameter as a **key into the `shaders` map**
(`StarRenderer_opengl.cpp:340-342`):

```cpp
auto compileShader = [&](GLenum type, String const& name) -> GLuint {
  GLuint shader = glCreateShader(type);
  auto* source = shaders.ptr(name);          // <-- `name` is a KEY
  if (!source)
    return 0;
```

The recovery path passes it the **shader source** instead (`:364-368`):

```cpp
catch (RendererException const& e) {
  Logger::error("Shader compile error, using default: {}", e.what());
  if (vertexShader) glDeleteShader(vertexShader);
  if (fragmentShader) glDeleteShader(fragmentShader);
  vertexShader = compileShader(GL_VERTEX_SHADER, DefaultVertexShader);      // <-- the raw GLSL
  fragmentShader = compileShader(GL_FRAGMENT_SHADER, DefaultFragmentShader);
}
```

`DefaultVertexShader` is a `char const*` holding a `#version 150` shader body. `shaders` is keyed
`"vertex"` / `"fragment"`. So `shaders.ptr(<the whole shader body>)` misses, the lambda returns `0`, and
**nothing is attached to the program**. The log says the default was used. It was not.

Whether the shaderless program then links or throws is left to the driver. On Mesa it throws, and because
the old effect has already been destroyed (see the companion issue) that throw is fatal.

Minor, same function: `glCreateShader` runs *before* the `if (!source) return 0;`, so the handle leaks on
every miss.

### Repro

Append one line of non-GLSL to any effect's fragment shader and boot:

```
[Error] Shader compile error, using default: (RendererException) Failed to compile fragment shader:
        0:31(1): error: illegal use of reserved word `this'
[Error] Fatal Exception caught: (RendererException) Failed to link program:
        error: no shaders attached to the program
Aborted (core dumped)
```

`no shaders attached to the program` is the proof: the fallback compiled nothing.

### Suggested fix

The parameter is doing three jobs at once — map key, shader source, and error label. Split it, and hoist the
lookup to the call site:

```cpp
auto compileStage = [&](GLenum type, char const* label, String const& source) -> GLuint {
  if (source.empty())
    return 0;                       // this stage was not supplied; an effect may provide only one
  GLuint shader = glCreateShader(type);
  ...
  if (!status) {
    glGetShaderInfoLog(shader, sizeof(logBuffer), NULL, logBuffer);
    glDeleteShader(shader);         // no leak
    throw RendererException(strf("Failed to compile {} shader of effect '{}': {}\n", label, name, logBuffer));
  }
  return shader;
};

auto sourceOf = [&](char const* key) -> String {
  auto* s = shaders.ptr(key);
  return s ? *s : String();
};

vertexShader   = compileStage(GL_VERTEX_SHADER,   "vertex",   sourceOf("vertex"));
fragmentShader = compileStage(GL_FRAGMENT_SHADER, "fragment", sourceOf("fragment"));
// and in the catch:
vertexShader   = compileStage(GL_VERTEX_SHADER,   "default vertex",   DefaultVertexShader);
fragmentShader = compileStage(GL_FRAGMENT_SHADER, "default fragment", DefaultFragmentShader);
```

With this, the same broken shader boots to a working game on the default program instead of aborting.
