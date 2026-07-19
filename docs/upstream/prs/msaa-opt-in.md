<!-- STATUS: DRAFT — NOT PUSHED, NOT OPENED. Branch: upstream/msaa-opt-in (8ab2359e0, from origin/main 2c7f972b6). -->
<!-- Target: OpenStarbound/OpenStarbound  <-  apnex/OpenStarbound:upstream/msaa-opt-in -->

# Make multisampling a per-framebuffer opt-in

### Credit first

@emmyposs already found this and fixed it in **#510** (April), which hasn't had a review yet. I hit the
same thing independently in a fork, and this PR is the same change with a hardware repro attached — my
hope is that it helps #510 get triaged rather than replacing it. If you'd rather merge theirs, please do;
I'm happy to close this and go comment on it instead.

Probably the cause of **#204** ("Shaders jumpscare you with texture atlas when SSAA is enabled") and
**#285** (whose current workaround is "Turn off Super-Sampled Anti Aliasing").

### The problem

With `antiAliasing` on, any effect that **samples** a framebuffer reads the wrong texture — usually a page
of the sprite atlas, which is exactly what #204 shows on screen.

`loadConfig` stamps the global setting onto every framebuffer, overwriting the framebuffer's own key, so
nothing can opt out — `StarRenderer_opengl.cpp:316`:

```cpp
config = config.set("multisample", m_multiSampling);
```

That makes the framebuffer's texture `GL_TEXTURE_2D_MULTISAMPLE` (`:171-172`). But the effect-texture bind
site binds unconditionally to `GL_TEXTURE_2D` (`:1263-1267`):

```cpp
glActiveTexture(GL_TEXTURE0 + p.second.textureUnit);
glBindTexture(GL_TEXTURE_2D, p.second.textureValue->textureId);
```

Binding a name whose target is already `GL_TEXTURE_2D_MULTISAMPLE` to `GL_TEXTURE_2D` is
`GL_INVALID_OPERATION`. Per spec the call is a **no-op** — so the texture unit silently **keeps its
previous `GL_TEXTURE_2D` binding**, and the shader's `sampler2D` samples that instead. Nothing surfaces
the error.

### Measured

Instrumented the bind site with a per-bind probe (`want` = the texture we asked to bind, `bound` = what the
unit actually holds afterwards), `antiAliasing` on:

```
uniform=emission   unit=2  want=10  bound=10  err=0x0     <- a CPU-uploaded texture: binds fine
uniform=lightMap   unit=4  want=7   bound=0   err=0x502   <- a framebuffer texture: INVALID_OPERATION
uniform=lightMap   unit=4  want=7   bound=11  err=0x502   <- the bind is a no-op; the unit keeps its old binding
uniform=lightMap   unit=4  want=7   bound=13  err=0x502
```

A CPU-uploaded sampler in the **same frame** binds cleanly. Only framebuffer-sourced samplers fail.

One session, `antiAliasing` on in both legs, the only difference being this patch:

| | before | after |
|---|---|---|
| `GL_INVALID_OPERATION` at the bind | 60,267 | **0** |
| failing samplers | all framebuffer-sourced | **none** |
| mean world luminance | 0.016682 | **0.257642** |

(Intel Arc / Mesa. The luminance row is the world going black and coming back.)

### The change

Only a framebuffer that is **MSAA-resolved to the screen** may be multisampled. `main` is — it's blitted to
the default framebuffer with `glBlitFramebuffer`, which is a legal MSAA resolve, and it is never sampled.
Anything a shader *samples* must stay single-sample.

```cpp
config = config.set("multisample", config.getBool("allowMultisample", false) ? m_multiSampling : 0);
```

…and `"allowMultisample": true` on `main` in `opengl.config`. Two files, +18/−2. MSAA still does its job on
the screen target; AA stays on; the error count goes to zero.

### Scope — vanilla is fine, and I want to be plain about that

Stock OpenStarbound ships exactly one framebuffer and zero `frameBufferTextures`, so **an unmodded client
never hits this**. It bites the moment something samples a framebuffer: postprocess shader mods, and the
double-buffered feedback effects that `"double"` was added for in #542.

For triage: this **predates #542**. The forced-multisample stamp goes back to 8a8a05015 (2024-04-08); #542
is just the first feature to stand on it.

### One thing worth knowing

`"allowMultisample"` is a new key, so a mod that today declares a framebuffer it samples gets fixed with no
change on its side (it simply doesn't opt in). A mod that *wants* a multisampled target it only blits can
add the key. I don't think anything relies on the current behaviour, since the current behaviour is that
sampling is broken — but it is a behaviour change and you should be the judge of it.
