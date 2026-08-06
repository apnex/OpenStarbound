#include "StarRenderer_opengl.hpp"
#include "StarJsonExtra.hpp"
#include "StarCasting.hpp"
#include "StarLogging.hpp"

#include <atomic>
#include "StarTelemetry.hpp"

#include <cstring>  // memcmp (RenderOracle::compare bit-identity)

namespace Star {

// A/B switch for the VBO-orphaning fix (task #141). Default = orphaning ON. STAR_NO_VBO_ORPHAN=1 restores the
// old bare-glBufferSubData behaviour, so the win is MEASURED rather than asserted. File-scope, not a member:
// GlRenderBuffer is a nested struct and cannot reach OpenGlRenderer's instance state.
// Orphan the immediate VBO before re-writing it. A bare glBufferSubData re-writes a buffer the GPU may STILL
// BE READING from the previous flush, forcing an IMPLICIT SYNCHRONISATION -- a full pipeline stall. Measured at
// the Director's three bases: the frame drops from ~11.5-12.9ms to ~2.4-3.2ms. A 74-81% cut, one line.
//
// BYTE-IDENTICAL BY CONSTRUCTION: glBufferData(cap, NULL) marks the old contents dead, then glBufferSubData
// writes exactly the same bytes as before into [0, size). The draw reads only [0, vertexCount) which lies
// inside that range, so the region left undefined by the orphan is never sampled.
//
// File-scope, not a member: GlRenderBuffer is a nested struct and cannot reach OpenGlRenderer's instance state.
// Driven from a config key so the in-process A/B gate can PROVE the byte-identity rather than assert it.
static bool NoVboOrphan = [](){
  char const* e = getenv("STAR_NO_VBO_ORPHAN");
  return e && *e && *e != '0';
}();

void OpenGlRenderer::setVboOrphan(bool enabled) {
  NoVboOrphan = !enabled;
}





// KHR_debug attribution probe (#131). glGetError tells you an error HAPPENED somewhere since the last drain;
// it never tells you WHICH call raised it, and the flag saturates, so a bisect by drain placement can only ever
// narrow to a code region. A SYNCHRONOUS debug callback fires inside the offending driver call, so the C++ stack
// at that moment names the call site exactly. Off by default (it forces synchronous driver behaviour and costs
// frame time); STAR_GL_DEBUG=1 arms it.
static bool GlDebugRequested = [](){
  char const* e = getenv("STAR_GL_DEBUG");
  return e && *e && *e != '0';
}();

static void GLAPIENTRY GlMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei,
    const GLchar* message, const void*) {
  if (type != GL_DEBUG_TYPE_ERROR)
    return;
  // Rate-limited: a per-frame error would otherwise write a stack trace every frame for the whole run.
  static int budget = 8;
  if (budget <= 0)
    return;
  --budget;
  Logger::error("GL DEBUG ERROR source={:#x} id={} severity={:#x}: {}", source, id, severity, message);
  printStack("  at");
}



// ---------------------------------------------------------------------------------------------------------
// GlTargets -- owns which render targets exist.









OpenGlRenderer::OpenGlRenderer()
  // The timer's whole dependency on the renderer, made explicit and one-directional.
  : m_gpuTimer([this]() { flushImmediatePrimitives(); }), m_oracle(*this) {
  auto glewResult = glewInit();
  if (glewResult != GLEW_OK && glewResult != GLEW_ERROR_NO_GLX_DISPLAY)
    throw RendererException::format("Could not initialize GLEW: {}", (char*)glewGetErrorString(glewResult));

  if (!GLEW_VERSION_3_2)
    throw RendererException("OpenGL 3.2 not available!");

  Logger::info("OpenGL version: '{}' vendor: '{}' renderer: '{}' shader: '{}'",
      (const char*)glGetString(GL_VERSION),
      (const char*)glGetString(GL_VENDOR),
      (const char*)glGetString(GL_RENDERER),
      (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));

  // AT CONSTRUCTION, so the metric exists from frame zero whatever the frames do. Registering it inside the
  // audit -- a block that only does anything when something is already wrong -- would make ABSENT and ZERO
  // the same reading for anyone differencing two snapshots. That is the defect #181 closed for the
  // backdrop's contract-violation counter, and it is not worth learning twice.
  m_glStateMismatches = Telemetry::counter("render.glstate.mismatches",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});

  glClearColor(0.0, 0.0, 0.0, 1.0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  if (GLEW_VERSION_4_3 && GlDebugRequested) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);   // fire INSIDE the offending call, so the stack below is the truth
    glDebugMessageCallback(GlMessageCallback, this);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    Logger::info("GL debug output ENABLED (STAR_GL_DEBUG)");
  }

  m_whiteTexture = createGlTexture(Image::filled({1, 1}, Vec4B(255, 255, 255, 255), PixelFormat::RGBA32),
      TextureAddressing::Clamp,
      TextureFiltering::Nearest);
  m_immediateRenderBuffer = createGlRenderBuffer();

  loadEffectConfig("internal", JsonObject(), {{"vertex", DefaultVertexShader}, {"fragment", DefaultFragmentShader}});

  m_limitTextureGroupSize = false;
  m_useMultiTexturing = true;
  m_multiSampling = false;

  logGlErrorSummary("OpenGL errors during renderer initialization");
}

OpenGlRenderer::~OpenGlRenderer() {
  // Drained SEPARATELY so a teardown error names which half raised it. One summary after both could only
  // ever say "shutdown", which is what made the long-standing GL_INVALID_VALUE unattributable.
  //
  // Note the two halves differ in what they can even raise: of every GL call reachable from destroyAll,
  // glDeleteProgram is the ONLY one that can produce GL_INVALID_VALUE -- glDeleteTextures,
  // glDeleteFramebuffers and glDeleteBuffers all silently ignore names that are not live objects, and
  // glDeleteProgram(0) is ignored too. So an error attributed to the effects half points at a program id
  // that was never a program; one attributed to targets points somewhere unexpected entirely.
  m_effects.destroyAll();
  logGlErrorSummary("OpenGL errors destroying effects");

  m_targets.destroyAll();
  logGlErrorSummary("OpenGL errors destroying targets");
}

String OpenGlRenderer::rendererId() const {
  return "OpenGL20";
}

Vec2U OpenGlRenderer::screenSize() const {
  return m_screenSize;
}











void OpenGlRenderer::loadConfig(Json const& config) {
  // Every framebuffer below is destroyed and re-created with UNDEFINED content. Retained (clear:false)
  // surfaces have no other way to learn this -- their refresh keys (size/camera/counter) are unchanged
  // across the realloc -- so bump the generation and let them invalidate. Both setMainHDR and
  // setMultiSampling land here, and ClientApplication polls both client options every frame.
  // THE PASS LETS GO FIRST. m_pass.target is a RefPtr, so a pass still holding one of these across the
  // rebuild would keep its GlSurface -- and therefore its FBO -- alive after the registry has forgotten
  // it. The surface is not freed, it is ORPHANED, and the pass goes on believing a dead surface is bound. Its
  // identity-keyed bind cache would then match that corpse and skip a real rebind.
  //
  // This is the RB-5 defect in the last seat that had it. rebindBorrows() told the SAMPLERS the targets were
  // rebuilt, and the generation counter tells the RETAINED SURFACES -- and nobody told the PASS.
  //
  // LATENT, not live, and it is worth being exact about why: ClientApplication polls the AA and HDR options
  // (its setMultiSampling / setMainHDR pair) immediately after the previous frame ended with
  // switchEffectConfig("interface"), and "interface" declares no frameBuffer -- so the pass is on the screen
  // holding nothing at the one moment this runs. Correctness by frame ordering. Nothing states that ordering,
  // nothing enforces it, and it is invisible to whoever next moves the poll or gives "interface" a target.
  //
  // invalidate(), not just target.reset(): the (target, face, size) bind key means the screen early-out now
  // trusts boundViewport too, and the target rebuild below leaves GL_DRAW on the LAST-allocated FBO
  // (allocateFace binds it) while boundViewport stays == screenSize from that interface unbind. A bare
  // target.reset() would leave the cache reading (screen, screenSize) -- matching the screen key -- so the
  // next screen bind would early-out over that corpse binding and draw into a live target FBO. Dropping the
  // whole cache to its {0,0} sentinel forces the next bind to emit real GL. This is the RB-5 defect closed for
  // the bind cache the same way target.reset() closed it for the RefPtr. Off every oracle path (loadConfig
  // never runs mid-recompute), so byte-identical on all certified paths.
  m_pass.invalidate();
  m_targets.destroyAll();

  for (auto& pair : config.getObject("frameBuffers", {})) {
    Json config = pair.second;
    // Multisampling is OPT-IN per framebuffer, and only the target that is MSAA-RESOLVED to the screen
    // (glBlitFramebuffer, i.e. "main") may opt in. Any framebuffer that is SAMPLED AS A TEXTURE must stay
    // single-sample -- and the reason is NOT that a multisample texture reads as zero. Binding a
    // GL_TEXTURE_2D_MULTISAMPLE object to the GL_TEXTURE_2D target is GL_INVALID_OPERATION and the bind is
    // a NO-OP: the texture unit keeps whatever it held before, so the shader samples a stale unrelated
    // texture. Order-dependent, not deterministic zeros. Proven on hardware at #150 --
    // `uniform=lightMap unit=4 want=7 bound=13 err=0x502`. Design guards that test the BIND, not the pixels.
    //
    // This was forced onto EVERY framebuffer unconditionally, which meant that with antiAliasing on, the
    // GPU lightmap targets (lightingGpu / lightingGpuB / lightingGpuUpscaled) became multisample textures --
    // so the world shader's lightMap sampler took whatever was stale on its unit and THE WORLD WENT BLACK.
    // Only the sky and
    // parallax survived, because they are not lightmapped. Vanilla never hit this: it had only "main", which
    // is resolved to the screen and never sampled. The bug arrived with the GPU-lighting feature, which was
    // the first thing to sample an FBO as a texture -- and it is also why the env/parallax caches were gated
    // on !antiAliasing, a workaround for the symptom rather than a fix for the cause.
    config = config.set("multisample", config.getBool("multisampled", false) ? m_multiSampling : 0);
    config = config.set("hdrSetting", m_hdrSetting);
    // A "devOnly" surface exists solely to serve a validation oracle, which is off in normal play. These are
    // screen-sized HDR surfaces (~22MB each at 1440p), so allocating them unconditionally spent real VRAM on
    // something nothing ever read. Create them only while an oracle is actually armed.
    if (config.getBool("devOnly", false) && !m_oracle.enabled())
      continue;

    Logger::info("Creating framebuffer {}", pair.first);
    m_targets.add(pair.first, config, m_screenSize);
  }

  // THE TARGETS ARE NEW; THE SAMPLERS STILL POINT AT THE OLD ONES.
  //
  // destroyAll() ran above, but ~GlSurface only RELEASES its RefPtr to the face texture -- the
  // glDeleteTextures lives in ~GlLoneTexture and does not run while an effect sampler still holds a reference.
  // So the texture is not freed, it is ORPHANED: a live GL texture belonging to a framebuffer that no longer
  // exists, still bound to a sampler, still being read every frame.
  //
  // This is reachable from the options menu. ClientApplication polls antiAliasing and HDR every frame and
  // calls setMultiSampling / setMainHDR on change, both of which land here -- and loadConfig does NOT reload
  // effects, so nothing else was ever going to re-point them. The world's `lightMap` would go on sampling a
  // destroyed lighting target until the next lighting recompute happened to rebind it, which is temporally
  // gated and can be several frames away.
  //
  // The generation counter announces the rebuild to RETAINED SURFACES; this announces it to SAMPLERS.
  m_effects.rebindBorrows(m_targets);
  setScreenSize(m_screenSize);
  m_config = config;
}

OpenGlRenderer::GlRenderOracle::GlRenderOracle(OpenGlRenderer& renderer) : m_renderer(renderer) {}

bool OpenGlRenderer::GlRenderOracle::enabled() const {
  return m_enabled;
}

void OpenGlRenderer::GlRenderOracle::setEnabled(bool enabled) {
  if (m_enabled == enabled)
    return;

  m_enabled = enabled;
  m_renderer.loadConfig(m_renderer.m_config);
}

GpuTimer& OpenGlRenderer::gpuTimer() {
  return m_gpuTimer;
}

RenderOracle& OpenGlRenderer::oracle() {
  return m_oracle;
}

// ---------------------------------------------------------------------------------------------------------
// GlEffects -- owns the compiled GPU programs.








void OpenGlRenderer::loadEffectConfig(String const& name, Json const& effectConfig, StringMap<String> const& shaders) {
  // THE REGISTRY COMPILES IT; THE PASS BINDS IT. Two lines, and they used to be a hundred and eighty apart:
  // the bind (`m_pass.effect = &effect` plus the uniform flatten) sat buried in the middle of the load, an
  // unannounced side effect that nothing named.
  //
  // The bind is not optional and cannot be moved later. The parameter and texture loops below issue
  // glUniform1i / glUniform* against the program they are describing, and glUniform writes into whatever
  // program is CURRENTLY BOUND -- so the program must be current before the first of them runs.
  //
  // It is also what makes the renderer's pass valid AT ALL: the constructor's loadEffectConfig("internal") is
  // the only thing that ever moves m_pass.effect off its nullptr, and every effect entry point dereferences
  // it without a null check. A load() that did not bind would leave the renderer booted with a null pass and
  // the first draw would take it.
  Effect& effect = m_effects.load(name, effectConfig, shaders);
  m_pass.bindEffect(effect, m_screenSize);
  GLuint program = effect.program;

  for (auto const& p : effectConfig.getObject("effectParameters", {})) {
    EffectParameter effectParameter;

    effectParameter.parameterUniform = glGetUniformLocation(program, p.second.getString("uniform").utf8Ptr());
    if (effectParameter.parameterUniform == -1) {
      Logger::warn("OpenGL20 effect parameter '{}' in effect '{}' has no associated uniform, skipping", p.first, name);
    } else {
      String type = p.second.getString("type");
      Json def = p.second.get("default", {});
      // ONE type-name ladder, in parseEffectParameter, and parameterType is DERIVED from what it returns --
      // not decided by a second, parallel if-chain. The two used to be maintained independently: a skew threw
      // at the first script write for scriptables (whose default path was checked by NOTHING) and mismatched
      // at upload for the rest. Same function, same value now, so the type and the default cannot disagree.
      RenderEffectParameter parsed = parseEffectParameter(type, def);
      effectParameter.parameterType = parsed.typeIndex();

      if (p.second.getBool("scriptable", false)) {
        if (def)
          effectParameter.parameterValue = parsed;
        effect.scriptables[p.first] = effectParameter;
      } else {
        // Insert the type-only parameter (parameterValue UNSET) FIRST, then upload the default through
        // setEffectParameter. applyEffectParameter's dedup guard skips the glUniform when the stored value
        // already equals the incoming one -- so pre-populating parameterValue here would silently elide every
        // non-scriptable default upload.
        effect.parameters[p.first] = effectParameter;
        if (def)
          setEffectParameter(p.first, parsed);
      }
    }
  }

  // Assign each texture parameter a texture unit starting with MultiTextureCount, the first
  // few texture units are used by the primary textures being drawn.  Currently,
  // maximum texture units are not checked.
  unsigned parameterTextureUnit = effect.includeVBTextures ? MultiTextureCount : 0;

  for (auto const& p : effectConfig.getObject("effectTextures", {})) {
    EffectTexture effectTexture;
    // A load-time local, not state. It was an EffectTexture field, but nothing ever read it again after this
    // loop -- the sampler unit is bound here, once, and the binding lives in the PROGRAM from then on.
    GLint textureUniform = glGetUniformLocation(program, p.second.getString("textureUniform").utf8Ptr());
    if (textureUniform == -1) {
      Logger::warn("OpenGL20 effect parameter '{}' has no associated uniform, skipping", p.first);
    } else {
        effectTexture.textureUnit = parameterTextureUnit++;
        glUniform1i(textureUniform, effectTexture.textureUnit);

        effectTexture.textureAddressing = TextureAddressingNames.getLeft(p.second.getString("textureAddressing", "clamp"));
        effectTexture.textureFiltering = TextureFilteringNames.getLeft(p.second.getString("textureFiltering", "nearest"));
        if (auto tsu = p.second.optString("textureSizeUniform")) {
          effectTexture.textureSizeUniform = glGetUniformLocation(program, tsu->utf8Ptr());
          if (effectTexture.textureSizeUniform == -1)
            Logger::warn("OpenGL20 effect parameter '{}' has textureSizeUniform '{}' with no associated uniform", p.first, *tsu);
        }

      effect.textures[p.first] = effectTexture;
    }
  }
  
  // doubleBuffered is derived in GlEffects::load now, from the parsed frame-buffer fields -- not re-scanned
  // from JSON here.

  if (DebugEnabled)
    logGlErrorSummary("OpenGL errors setting effect config");
}


RenderEffectParameter OpenGlRenderer::parseEffectParameter(String const& type, Json const& def) {
  // `def` falsy == no "default" key: return the type's zero value. Its bits never reach the GPU (the callers
  // upload only when def is present); it exists solely so parameterType is derivable from .typeIndex() here.
  if (type == "bool")  return (RenderEffectParameter)(def ? def.toBool() : false);
  if (type == "int")   return (RenderEffectParameter)(int)(def ? def.toInt() : 0);
  if (type == "float") return (RenderEffectParameter)(def ? def.toFloat() : 0.0f);
  if (type == "vec2")  return (RenderEffectParameter)(def ? jsonToVec2F(def) : Vec2F());
  if (type == "vec3")  return (RenderEffectParameter)(def ? jsonToVec3F(def) : Vec3F());
  if (type == "vec4")  return (RenderEffectParameter)(def ? jsonToVec4F(def) : Vec4F());
  throw RendererException::format("Unrecognized effect parameter type '{}'", type);
}

void OpenGlRenderer::applyEffectParameter(EffectParameter* ptr, RenderEffectParameter const& value, String const& parameterName) {
  if (ptr->parameterValue && *ptr->parameterValue == value)
    return;

  if (ptr->parameterType != value.typeIndex())
    throw RendererException::format("OpenGlRenderer::setEffectParameter '{}' parameter type mismatch", parameterName);

  flushImmediatePrimitives();

  uploadUniform(ptr->parameterUniform, value);

  ptr->parameterValue = value;
}

void OpenGlRenderer::setEffectParameter(String const& parameterName, RenderEffectParameter const& value) {
  auto ptr = m_pass.effect()->parameters.ptr(parameterName);
  if (!ptr)
    return;
  applyEffectParameter(ptr, value, parameterName);
}

OpenGlRenderer::EffectParameterHandle OpenGlRenderer::getEffectParameterHandle(String const& parameterName) {
  return (EffectParameterHandle)m_pass.effect()->parameters.ptr(parameterName);
}

void OpenGlRenderer::setEffectParameter(EffectParameterHandle handle, RenderEffectParameter const& value) {
  if (!handle)
    return;
  applyEffectParameter((EffectParameter*)handle, value, "<handle>");
}

void OpenGlRenderer::setEffectScriptableParameter(String const& effectName, String const& parameterName, RenderEffectParameter const& value) {
  m_effects.setScriptable(effectName, parameterName, value);
}

Maybe<RenderEffectParameter> OpenGlRenderer::getEffectScriptableParameter(String const& effectName, String const& parameterName) {
  return m_effects.getScriptable(effectName, parameterName);
}
Maybe<VariantTypeIndex> OpenGlRenderer::getEffectScriptableParameterType(String const& effectName, String const& parameterName) {
  return m_effects.getScriptableType(effectName, parameterName);
}

void OpenGlRenderer::setEffectTexture(String const& textureName, ImageView const& image) {
  auto ptr = m_pass.effect()->textures.ptr(textureName);
  if (!ptr)
    return;

  flushImmediatePrimitives();

  // AN UPLOAD SETTER OWNS THE TEXTURE IT UPLOADS INTO. If this sampler is currently BORROWING a framebuffer's
  // colour attachment, we take the fresh-allocation branch: we do not own that storage and must not touch it.
  //
  // Without the borrowed() test the else-branch below did all three of the things GlSurface::specifyStorage
  // declares itself the only owner of -- bind the target's texture, glTexImage2D a whole new storage spec into
  // it, and overwrite the textureSize record that GlSurface::size() reads -- from outside, through an
  // aliased RefPtr. It is reachable in ordinary play: the world effect's `lightMap` sampler is pointed at
  // lightingGpuUpscaled's face by the GPU lighting pass, and then fullbright (StarWorldPainter.cpp) uploads a
  // 1x1 white image into that same sampler, re-specifying a live render target to a 1x1 RGB8.
  //
  // It LOOKED harmless because it self-heals: the next setRenderTarget calls resize(), which sees the wrong
  // size and re-specifies. But resize() compares SIZE ONLY -- `if (size() == newSize) return;` -- against the
  // record this corruption also rewrites. It heals only because the corruption is self-reporting. An upload at
  // the SAME size and a DIFFERENT format is never noticed, and that internal format is wrong for the life of
  // the process.
  if (!ptr->ownsWritableStorage()) {
    ptr->adopt(createGlTexture(image, ptr->textureAddressing, ptr->textureFiltering));
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->texture()->textureId);
    // A FULL RE-SPECIFICATION, so the WHOLE descriptor is rewritten -- by the act that specifies it.
    // uploadTextureImage now records both fields itself; recording only the size (which is what this did) left
    // the format record describing storage that no longer existed, and the half-float SubImage guard trusted it.
    uploadTextureImage(image.format, image.size, image.data, ptr->texture().get());
  }

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->texture()->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, textureSize[0], textureSize[1]);
  }
}

bool OpenGlRenderer::switchEffectConfig(String const& name) {
  flushImmediatePrimitives();
  auto found = m_effects.find(name);
  if (!found)
    return false;

  Effect& effect = *found;
  // ALREADY BOUND -- nothing to do, UNLESS the effect is double-buffered, in which case there is plenty to do.
  //
  // The early-out returns before buf->swap(), before the target bind, and before the frameBufferTextures
  // rebind. For an ordinary effect that is a pure saving. For a DOUBLE-BUFFERED one it silently disables the
  // feature: a post-process layer with a single effect and passes > 1 -- which is the canonical use of
  // `passes`, iterating a feedback shader -- swaps on the first pass and then early-outs on every pass after
  // it, so the shader samples the face it is drawing into. Measured on hardware with passes=2: 306 swaps and
  // 306 early-outs across 306 frames, one of each per frame. The iteration did not iterate.
  //
  // A double-buffered effect has per-invocation work; being already bound does not excuse it.
  if (m_pass.effect() == &effect && !effect.doubleBuffered)
    return true;

  auto effectScreenSize = m_screenSize;
  
  auto const& outFrameBufferId = effect.frameBuffer;
  if (outFrameBufferId) {
    auto buf = m_targets.get(*outFrameBufferId);
    // ASK THE SURFACE HOW BIG IT IS. This was the last hand-rolled copy of the size rule -- `m_screenSize /
    // buf->sizeDiv` -- which F1 collapsed into GlSurface everywhere except here, because switchEffectConfig
    // was not one of the sites F1 was able to see.
    //
    // It silently dropped overrideSize. lightingGpu is 512x512 and lightingGpuUpscaled 2048x2048, both with
    // sizeDiv 1, so this handed those effects the SCREEN size as their `screenSize` uniform -- while bindTarget,
    // three lines below, sets the viewport from the surface's ACTUAL allocated size. The uniform and the
    // viewport disagreed, and every full-screen pass maps vertexPosition through screenSize and depends on them
    // agreeing.
    //
    // In-tree it was covered: every consumer of a sized target calls setRenderTarget(id, size) immediately
    // after, which rewrites the uniform before anything is drawn. It was live for a mod effect that declared
    // `frameBuffer` on a sized surface and simply drew. size() is the same source bindTarget uses for the
    // viewport, so the two now come from one place and cannot drift apart.
    effectScreenSize = buf->size();
    if (effect.doubleBuffered) {
      if (!buf->doubled()) {
        // Allocating a framebuffer MID-FRAME hitches. Say so, and fix it in the config rather than here.
        Logger::warn("Effect '{}' reads the framebuffer '{}' that it writes, but that framebuffer is not "
                     "declared \"double\":true -- giving it a second face now, mid-frame.", name, *outFrameBufferId);
        buf->makeDoubled();
      }
      buf->swap();
    }
    bindTarget(buf);
  } else {
    // No frameBuffer: the effect draws to the screen. unbind() binds framebuffer 0 -- and now ALSO sets the
    // full-screen viewport, which this branch never used to set. THE ONE INTENDED DELTA. It is safe because a
    // screen-target draw wants the full-screen viewport, and every in-tree effect that reaches here either has
    // its viewport already at screenSize (interface, after the world's screen-sized "main" pass) or issues a
    // setRenderTarget that resets the viewport before it draws (lightingSpread / lightingPoint redirect to
    // off-screen targets). Where it changes anything at all -- a mod post-process leaving a sub-screen viewport
    // before a screen draw -- it is a latent FIX, never a regression. bindEffect below writes this effect's
    // screenSize uniform, so unbind must not (and does not) write one.
    m_pass.unbind(m_screenSize);
  }

  m_pass.bindEffect(effect, effectScreenSize);

  setEffectParameter("vertexRounding", m_multiSampling > 0);
  // The frame-buffer texture wiring, parsed at load into an ORDERED list of (textureUniform, framebufferName).
  // No JSON here: switchEffectConfig runs on every effect change.
  for (auto const& ft : effect.frameBufferTextures) {
    String const& textureUniform = ft.first;
    String const& frameBufferId = ft.second;
    auto ptr = m_pass.effect()->textures.ptr(textureUniform);
    if (ptr) {
      auto undefined = !ptr->hasStorage();
      auto swapped = effect.doubleBuffered && frameBufferId.equals(*outFrameBufferId);
      auto buf = m_targets.get(frameBufferId);
      if (undefined || buf->doubled()) {
        // `swapped` means this effect is sampling the very surface it is drawing into: it must read the
        // face it is NOT writing. That is exactly what readFace() answers, so ask it. share() records the
        // borrow: no writing, re-point on rebuild.
        ptr->share(swapped ? buf->readFace().texture : buf->writeFace().texture, frameBufferId);
        if (ptr->textureSizeUniform != -1 && undefined) {
          auto textureSize = ptr->texture()->glTextureSize();
          glUniform2f(ptr->textureSizeUniform, textureSize[0], textureSize[1]);
        }
      }
    }
  }
  
  if (effect.blitFrameBuffer)
    blitGlSurface(m_targets.get(*effect.blitFrameBuffer), effect.doubleBuffered);
  
  return true;
}

void OpenGlRenderer::setRenderTarget(Maybe<String> const& frameBufferId, Vec2U size) {
  flushImmediatePrimitives();

  if (!frameBufferId) {
    // Restore the screen as the draw target and the full-screen viewport. unbind() owns the framebuffer bind
    // and the viewport now (byte-identical: it binds 0 + viewport screenSize, or early-outs only when both are
    // already so). The screenSize UNIFORM write stays here -- bindTarget writes no uniform, and the uniform
    // targets the currently-bound program, which is correct on this path (no glUseProgram intervenes).
    m_pass.unbind(m_screenSize);
    if (m_pass.screenSizeUniform != -1)
      glUniform2f(m_pass.screenSizeUniform, (float)m_screenSize[0], (float)m_screenSize[1]);
    return;
  }

  // Tolerate a missing target rather than crashing the frame: callers (e.g. GpuLightmapPass)
  // degrade to their CPU path. Warn once via the renderer log on the absent id.
  auto buf = m_targets.find(*frameBufferId);
  if (!buf) {
    Logger::warn("setRenderTarget: frame buffer '{}' does not exist; ignoring", *frameBufferId);
    return;
  }

  // THE TARGET RESIZES ITSELF. This block used to derive the format ladder by hand and re-specify the storage
  // itself -- a FOURTH copy of rules that F1 was supposed to have collapsed to one. F1 could not see it: F1
  // was authored on a branch cut from upstream, and setRenderTarget is OURS, so it does not exist there.
  //
  // I wrote, in F1's own commit message, "the next person who adds a fourth resize path will forget one of
  // them. So will I." I had already done it, in a function F1 was structurally unable to look at.
  //
  // It also only ever re-specified writeFace(), so a DOUBLED surface resized here would have been left with
  // two faces of different sizes. resize() re-specifies every live face. Nothing in-tree is doubled today, so
  // this is bit-identical -- but it is bit-identical by accident, and now it is correct by construction.
  if (size[0] != 0 && size[1] != 0)
    buf->resize(size);   // idempotent: no GL calls at all if it is already that size

  bindTarget(buf);

  // THE COVER glViewport IS GONE. bindTarget now keys its bind cache on (target, face, size), so it no longer
  // early-outs before the viewport when a caller re-targets the SAME surface at a NEW size (the lighting
  // passes, every frame) -- it owns the viewport unconditionally. `vp` survives only to feed the screenSize
  // UNIFORM, which bindTarget does not write: `size` if the caller gave one (buf was just resized to it, so it
  // equals buf->size() -- the viewport and the uniform still agree), else the target's actual face size.
  Vec2U vp = (size[0] != 0 && size[1] != 0) ? size : buf->writeFace().texture->glTextureSize();
  if (m_pass.screenSizeUniform != -1)
    glUniform2f(m_pass.screenSizeUniform, (float)vp[0], (float)vp[1]);
}

void OpenGlRenderer::clearRenderTarget(Vec4F clearColor) {
  // Flush pending immediate primitives first so they aren't wiped by the clear. Clears the currently
  // bound GL_DRAW_FRAMEBUFFER (set by setRenderTarget -> GlPass::bindTarget). Default clearColor
  // (0,0,0,1) == the constant startFrame clear (byte-identical to the env-cache path). A non-default
  // color (e.g. transparent (0,0,0,0) for the premultiplied parallax cache) is set then restored, since
  // glClearColor is otherwise a constant. Disable scissor around the clear (mirrors startFrame).
  flushImmediatePrimitives();
  if (m_scissorRect)
    glDisable(GL_SCISSOR_TEST);
  bool custom = clearColor != Vec4F(0.0f, 0.0f, 0.0f, 1.0f);
  if (custom)
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
  glClear(GL_COLOR_BUFFER_BIT);
  if (custom)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  if (m_scissorRect)
    glEnable(GL_SCISSOR_TEST);
}

void OpenGlRenderer::setEffectTextureFromTarget(String const& textureName, String const& frameBufferId) {
  auto ptr = m_pass.effect()->textures.ptr(textureName);
  if (!ptr)
    return;

  flushImmediatePrimitives();

  auto target = m_targets.get(frameBufferId);

  // THE MULTISAMPLE-TO-SAMPLER GUARD (#220 E06). A multisample colour attachment is a
  // GL_TEXTURE_2D_MULTISAMPLE; binding one where the shader declares sampler2D is GL_INVALID_OPERATION and
  // the world renders black. Not hypothetical -- that is #150, reproduced on hardware at 60,267 errors in
  // a single run.
  //
  // THE RECOMMENDED FORM OF THIS CHECK WOULD HAVE CAUGHT NOTHING. It was to warn at loadConfig when a
  // framebuffer declaring "multisampled": true is also named by an effect's frameBufferTextures -- and that
  // set is EMPTY in this tree. The exposure is here instead, on the C++ argument, because that is where a
  // target name is chosen at runtime.
  //
  // Exactly one live candidate today: BackdropPass composes "main" into parallaxRef for the parallax
  // oracle. It is safe only through a coupling spanning two files that is asserted nowhere -- "main is
  // multisample iff antiAliasing" lives in the renderer plus opengl.config, while "the parallax cache path
  // runs iff !antiAliasing" lives in BackdropPass. Relax the parallax AA gate (#151 considered it, #138 may
  // revisit) and this binds a multisample texture to a sampler2D whenever the dev-only oracle is armed.
  //
  // REPORTS, DOES NOT CORRECT. Refusing the bind would leave the sampler pointing at whatever it held and
  // trade a loud failure for a quiet wrong image. The render gate reads this line and goes red instead.
  if (target->multisample) {
    Logger::error("[glguard] setEffectTextureFromTarget('{}', '{}'): target is {}x multisample, and binding it "
                  "to a sampler2D is GL_INVALID_OPERATION (#150). Whatever coupling kept this path "
                  "single-sample no longer holds.", textureName, frameBufferId, target->multisample);
  }

  // Bind the framebuffer's color texture (a GlLoneTexture, same type setEffectTexture produces)
  // directly to the sampler -- no CPU upload. share() records whose it is: no writing, and re-point on rebuild.
  ptr->share(target->writeFace().texture, frameBufferId);
  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->texture()->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureAlias(String const& destTextureName, String const& sourceTextureName) {
  auto dest = m_pass.effect()->textures.ptr(destTextureName);
  auto src = m_pass.effect()->textures.ptr(sourceTextureName);
  if (!dest || !src || !src->texture())
    return;

  flushImmediatePrimitives();

  // Share the source sampler's already-uploaded texture (same GlLoneTexture, ref-counted) with the dest
  // sampler -- the per-draw bind loop will bind it to dest's texture unit. No CPU upload. The alias inherits
  // the source's borrow status: an alias of a borrowed texture is still borrowed, from the same target.
  dest->share(src->texture(), src->borrowFrom());
  if (dest->textureSizeUniform != -1) {
    auto textureSize = dest->texture()->glTextureSize();
    glUniform2f(dest->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureHalf(String const& textureName, Vec2U size, uint16_t const* halfData, unsigned channels) {
  auto ptr = m_pass.effect()->textures.ptr(textureName);
  if (!ptr || size[0] == 0 || size[1] == 0)
    return;

  flushImmediatePrimitives();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool fresh = false;
  // ownsWritableStorage() false -> absent, empty, or BORROWING a framebuffer's colour attachment. Allocate our
  // own storage rather than glTexSubImage2D pixels -- or glTexImage2D a whole new spec -- into a live target.
  if (!ptr->ownsWritableStorage()) {
    ptr->adopt(createEmptyGlTexture(size, ptr->textureAddressing, ptr->textureFiltering));
    fresh = true;
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->texture()->textureId);
  }
  // 16F storage + GL_HALF_FLOAT source: half the bytes of the float upload, no precision loss (the FBOs are
  // 16F anyway). channels==4 packs a fourth component -- used to carry the obstacle flag alongside emission,
  // so the spread shader reads light and obstacle in ONE tap instead of two samplers.
  // Same-size re-upload goes through TexSubImage into the EXISTING storage: the old unconditional
  // glTexImage2D re-spec allocated a fresh driver buffer object per upload at the lighting cadence
  // (measured ~30% of the kernel texture cluster, #127). Size changes (zoom/resolution) still re-spec.
  GLenum const format = channels == 4 ? GL_RGBA : GL_RGB;
  GLint  const internalFormat = channels == 4 ? GL_RGBA16F : GL_RGB16F;
  // Guard + spec + record are one act now (uploadLoneStorage), which tests size AND format. This setter used to
  // consult `uploadChannels`, a field only IT maintained -- the shared chokepoint cannot be given a stale one.
  uploadLoneStorage(*ptr->texture(), size, internalFormat, format, GL_HALF_FLOAT, halfData, fresh);

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->texture()->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureR8(String const& textureName, Vec2U size, uint8_t const* data) {
  auto ptr = m_pass.effect()->textures.ptr(textureName);
  if (!ptr || size[0] == 0 || size[1] == 0)
    return;

  flushImmediatePrimitives();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool fresh = false;
  // ownsWritableStorage() false -> absent, empty, or BORROWING a framebuffer's colour attachment. Allocate our
  // own storage rather than glTexSubImage2D pixels -- or glTexImage2D a whole new spec -- into a live target.
  if (!ptr->ownsWritableStorage()) {
    ptr->adopt(createEmptyGlTexture(size, ptr->textureAddressing, ptr->textureFiltering));
    fresh = true;
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->texture()->textureId);
  }
  // R8 storage + GL_RED source: a third the bytes of RGB24 for the binary obstacle mask (read as .r).
  // Same-size re-upload goes through TexSubImage (see setEffectTextureHalfRGB above) -- this runs twice
  // per recompute (the spread and point effects each own an "obstacle" sampler), so both duplicate
  // uploads become SubImages into persistent storage instead of fresh-BO re-specs.
  // The format test was ABSENT here once -- this setter's own guard trusted the SIZE alone. The "obstacle"
  // sampler is fed by BOTH this setter and setEffectTexture (GpuLightmapPass's uploadObstacle lambda picks
  // between them at runtime), so one RGB24 frame re-specified the storage and every R8 upload after it wrote
  // GL_RED bytes into whatever that left behind, forever, because the size never changed. There is no per-setter guard
  // to get wrong now: uploadLoneStorage owns the one guard, and it tests size AND format.
  uploadLoneStorage(*ptr->texture(), size, GL_R8, GL_RED, GL_UNSIGNED_BYTE, data, fresh);

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->texture()->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

Image OpenGlRenderer::GlRenderOracle::read(String const& frameBufferId) {
  m_renderer.flushImmediatePrimitives();

  auto buf = m_renderer.m_targets.find(frameBufferId);
  if (!buf) {
    Logger::warn("RenderOracle::read: frame buffer '{}' does not exist", frameBufferId);
    return Image();
  }

  // EVERY not-readable condition MUST return an EMPTY image, never a zero-FILLED one. A caller that hashes
  // or compares the result cannot distinguish "the frame really is black" from "the read silently failed" --
  // and a silently-zeroed frame hashes CONSISTENTLY, so a golden-hash gate built on it would report a stable
  // PASS forever while seeing nothing at all. Empty is loud; zero-filled is a false green.
  Vec2U size = buf->writeFace().texture->glTextureSize();
  if (size[0] == 0 || size[1] == 0) {
    Logger::warn("RenderOracle::read: frame buffer '{}' has no recorded size", frameBufferId);
    return Image();
  }

  while (glGetError() != GL_NO_ERROR) {}   // drain pre-existing errors so ours is attributable

  Image result(size, PixelFormat::RGB_F);

  // glReadPixels is INVALID on a multisample framebuffer -- it must be blit-RESOLVED to a single-sample target
  // first. "main" is multisample whenever antiAliasing is on, so without this the whole AA path would be
  // unreadable and therefore unverifiable. Resolve into a scratch single-sample FBO and read that.
  GLuint resolveFbo = 0, resolveTex = 0;
  GLuint readFrom = buf->writeFace().id;
  if (buf->multisample) {
    glGenTextures(1, &resolveTex);
    glBindTexture(GL_TEXTURE_2D, resolveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size[0], size[1], 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, buf->writeFace().id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
    glBlitFramebuffer(0, 0, size[0], size[1], 0, 0, size[0], size[1], GL_COLOR_BUFFER_BIT, GL_NEAREST);
    readFrom = resolveFbo;
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, readFrom);
  glReadPixels(0, 0, size[0], size[1], GL_RGB, GL_FLOAT, result.data());
  // Restore the read binding to whatever draw target is current (screen if none).
  auto current = m_renderer.m_pass.target();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, current ? current->writeFace().id : 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, current ? current->writeFace().id : 0);

  if (resolveFbo) {
    glDeleteFramebuffers(1, &resolveFbo);
    glDeleteTextures(1, &resolveTex);
  }

  if (GLenum err = glGetError(); err != GL_NO_ERROR) {
    Logger::warn("RenderOracle::read: read of '{}' failed (GL error {:#x})", frameBufferId, (unsigned)err);
    return Image();
  }
  return result;
}

pair<size_t, Vec2U> OpenGlRenderer::GlRenderOracle::compare(String const& a, String const& b, float* maxAbsDiff) {
  // Offline bit-identity oracle. Flush pending draws, then GL-read both framebuffers' color to CPU and
  // count per-pixel BIT differences (memcmp of the raw float triples). Reads GL_RGB/GL_FLOAT like
  // RenderOracle::read: GL converts from RGB8 or RGB16F storage, so this is format-agnostic across the hdr
  // FromSetting modes. A raw bit compare is the correct test -- both buffers are the same-format render of
  // the same inputs, so a bit-identical cache path MUST produce identical pixels -- and it is NaN-safe
  // (identical NaN bit patterns compare equal, unlike float !=). glReadPixels stalls; debug/validate only.
  // Returns {NPos, {}} (not-comparable) on absent/multisample/mismatched-size targets or a readback error,
  // so a failed read can never be scored as a false MATCH.
  m_renderer.flushImmediatePrimitives();

  auto aBuf = m_renderer.m_targets.find(a);
  auto bBuf = m_renderer.m_targets.find(b);
  if (!aBuf || !bBuf) {
    Logger::warn("RenderOracle::compare: frame buffer '{}' or '{}' does not exist", a, b);
    return {NPos, Vec2U()};
  }
  // Multisample color attachments cannot be glReadPixels'd (GL_INVALID_OPERATION); the oracle is an AA-off
  // instrument, so treat a multisample target as not-comparable rather than reading garbage.
  if (aBuf->multisample || bBuf->multisample) {
    Logger::warn("RenderOracle::compare:'{}' or '{}' is multisample -- not comparable", a, b);
    return {NPos, Vec2U()};
  }
  Vec2U sizeA = aBuf->size();
  Vec2U sizeB = bBuf->size();
  if (sizeA != sizeB || sizeA[0] == 0 || sizeA[1] == 0) {
    Logger::warn("RenderOracle::compare:size mismatch/empty ('{}'={},{} vs '{}'={},{})",
      a, sizeA[0], sizeA[1], b, sizeB[0], sizeB[1]);
    return {NPos, Vec2U()};
  }

  size_t pixels = (size_t)sizeA[0] * sizeA[1];
  List<float> bufA, bufB;
  bufA.resize(pixels * 3);
  bufB.resize(pixels * 3);

  while (glGetError() != GL_NO_ERROR) {}  // drain pre-existing errors so the post-read check is isolated
  glBindFramebuffer(GL_READ_FRAMEBUFFER, aBuf->writeFace().id);
  glReadPixels(0, 0, sizeA[0], sizeA[1], GL_RGB, GL_FLOAT, bufA.ptr());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, bBuf->writeFace().id);
  glReadPixels(0, 0, sizeB[0], sizeB[1], GL_RGB, GL_FLOAT, bufB.ptr());
  GLenum readErr = glGetError();
  auto current = m_renderer.m_pass.target();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, current ? current->writeFace().id : 0);
  if (readErr != GL_NO_ERROR) {
    // A failed readback leaves the zero-filled buffers untouched -> would score as a false MATCH. Bail.
    Logger::warn("RenderOracle::compare: glReadPixels error 0x{:x} on '{}'/'{}' -- not comparable", (unsigned)readErr, a, b);
    return {NPos, Vec2U()};
  }

  size_t diffPixels = 0;
  size_t firstDiff = NPos;
  float maxDiff = 0.0f;
  for (size_t px = 0; px < pixels; ++px) {
    size_t i = px * 3;
    if (memcmp(&bufA[i], &bufB[i], 3 * sizeof(float)) != 0) {
      ++diffPixels;
      if (firstDiff == NPos)
        firstDiff = px;
      for (int k = 0; k < 3; ++k) {
        float d = bufA[i + k] - bufB[i + k];
        if (d < 0.0f) d = -d;
        if (d > maxDiff) maxDiff = d;
      }
    }
  }
  if (maxAbsDiff)
    *maxAbsDiff = maxDiff;
  // glReadPixels uses a lower-left origin (buffer row 0 = screen bottom), so flip Y to report a top-left
  // screen coordinate that actually locates the divergence.
  Vec2U firstXY;
  if (firstDiff != NPos)
    firstXY = Vec2U((unsigned)(firstDiff % sizeA[0]), sizeA[1] - 1 - (unsigned)(firstDiff / sizeA[0]));
  return {diffPixels, firstXY};
}

bool OpenGlRenderer::hasFrameBuffer(String const& id) const {
  return m_targets.has(id);
}

uint64_t OpenGlRenderer::frameBufferGeneration() const {
  return m_targets.generation();
}

bool OpenGlRenderer::composite(String const& effect, String const& dstFbo, Vec2U dstSize,
    String const& srcSampler, String const& srcFbo, List<pair<String, RenderEffectParameter>> const& params) {
  // Collapses the hand-rolled "sample one FBO into another via a passthrough effect + full-screen quad"
  // pattern. Sets `params` explicitly each call, so a shared passthrough effect (lightingPassthrough) is
  // safe across consumers with different needs -- no cross-consumer param bleed (the == cache still elides
  // unchanged uniforms, so this is ~free). Caller restores its own prior effect/target afterward.
  if (!switchEffectConfig(effect))
    return false;
  for (auto const& p : params)
    setEffectParameter(p.first, p.second);
  setEffectTextureFromTarget(srcSampler, srcFbo);
  setRenderTarget(String(dstFbo), dstSize);   // explicit target overrides the effect's frameBuffer config
  render(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(dstSize)), Vec4B::filled(255), 0.0f));
  return true;
}

void OpenGlRenderer::setBlendMode(BlendMode mode) {
  flushImmediatePrimitives();

  // GL_BLEND is enabled once at renderer init and every mode below only swaps the equation/func -- so None,
  // the only mode that turns blending OFF, has to turn it back on for everyone else.
  if (mode == BlendMode::None) {
    glDisable(GL_BLEND);
    return;
  }
  glEnable(GL_BLEND);

  switch (mode) {
    case BlendMode::None: break;   // unreachable, handled above; listed so the switch stays exhaustive
    case BlendMode::Alpha:    glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    case BlendMode::Additive: glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE); break;
    case BlendMode::Max:      glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE); break;
    // Build a premultiplied intermediate from straight-alpha source draws: rgb over-blends normally,
    // alpha = src_a + (1-src_a)*dst_a (correct coverage).
    case BlendMode::PremultiplyInto:   glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    // Composite a premultiplied source over the destination: rgb + (1-src_a)*dst.
    case BlendMode::PremultipliedOver: glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
  }
}

void OpenGlRenderer::setScissorRect(Maybe<RectI> const& scissorRect) {
  if (scissorRect == m_scissorRect)
    return;

  flushImmediatePrimitives();

  m_scissorRect = scissorRect;
  if (m_scissorRect) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(m_scissorRect->xMin(), m_scissorRect->yMin(), m_scissorRect->width(), m_scissorRect->height());
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
}

TexturePtr OpenGlRenderer::createTexture(Image const& texture, TextureAddressing addressing, TextureFiltering filtering) {
  return createGlTexture(texture, addressing, filtering);
}

void OpenGlRenderer::setSizeLimitEnabled(bool enabled) {
  m_limitTextureGroupSize = enabled;
}

void OpenGlRenderer::setMultiTexturingEnabled(bool enabled) {
  m_useMultiTexturing = enabled;
}

void OpenGlRenderer::setMultiSampling(unsigned multiSampling) {
  if (m_multiSampling == multiSampling)
    return;

  m_multiSampling = multiSampling;
  // GL_SAMPLE_SHADING and glMinSampleShading are GL 4.0 (or ARB_sample_shading); GL_MULTISAMPLE is 1.3.
  // They were called unguarded while the sibling framebuffer multisample value in GlRenderSurface is
  // guarded by GLEW_VERSION_4_0 -- so on a 3.x driver the framebuffer correctly declines multisampling
  // and these three still fire, raising GL_INVALID_ENUM/INVALID_OPERATION every AA toggle (#211).
  //
  // PR 570 removed per-sample shading outright to solve this. We do NOT: #151 measured it at 22.3% of
  // pixels on the parallax path and kept it deliberately, which is why the parallax AA gate still
  // exists. The capability, not the feature, is what was missing.
  // The two capabilities share the ENUM but not the ENTRY POINT, and conflating them re-created the
  // very crash this guard exists to prevent. GLEW loads glMinSampleShading only from its GL 4.0
  // initialiser and glMinSampleShadingARB only from its ARB one, so admitting the ARB capability and
  // then calling the core name is a null dispatch on a 3.x+ARB driver. Select the pointer once; a
  // null result then means "no capability" and needs no second test.
  auto minSampleShading = GLEW_VERSION_4_0        ? glMinSampleShading
                        : GLEW_ARB_sample_shading ? glMinSampleShadingARB
                                                  : nullptr;
  if (m_multiSampling) {
    glEnable(GL_MULTISAMPLE);
    if (minSampleShading) {
      glEnable(GL_SAMPLE_SHADING);
      minSampleShading(1.f);
    }
  } else {
    if (minSampleShading) {
      minSampleShading(0.f);
      glDisable(GL_SAMPLE_SHADING);
    }
    glDisable(GL_MULTISAMPLE);
  }
  loadConfig(m_config);
}

void OpenGlRenderer::setMainHDR(bool enabled) {
  if (m_hdrSetting == enabled)
    return;
  
  m_hdrSetting = enabled;
  loadConfig(m_config);
}

TextureGroupPtr OpenGlRenderer::createTextureGroup(TextureGroupSize textureSize, TextureFiltering filtering) {
  int maxTextureSize;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  maxTextureSize = min(maxTextureSize, (2 << 14));
  // Large texture sizes are not always supported
  if (textureSize == TextureGroupSize::Large && (m_limitTextureGroupSize || maxTextureSize < 4096))
    textureSize = TextureGroupSize::Medium;

  unsigned atlasNumCells;
  if (textureSize == TextureGroupSize::Large)
    atlasNumCells = 256;
  else if (textureSize == TextureGroupSize::Medium)
    atlasNumCells = 128;
  else // TextureGroupSize::Small
    atlasNumCells = 64;

  Logger::info("detected supported OpenGL texture size {}, using atlasNumCells {}", maxTextureSize, atlasNumCells);

  auto glTextureGroup = make_shared<GlTextureGroup>(atlasNumCells);
  glTextureGroup->textureAtlasSet.textureFiltering = filtering;
  m_liveTextureGroups.append(glTextureGroup);
  return glTextureGroup;
}

RenderBufferPtr OpenGlRenderer::createRenderBuffer() {
  return createGlRenderBuffer();
}

List<RenderPrimitive>& OpenGlRenderer::immediatePrimitives() {
  return m_immediatePrimitives;
}

void OpenGlRenderer::render(RenderPrimitive primitive) {
  m_immediatePrimitives.append(std::move(primitive));
}

void OpenGlRenderer::renderBuffer(RenderBufferPtr const& renderBuffer, Mat3F const& transformation) {
  flushImmediatePrimitives();
  renderGlBuffer(*convert<GlRenderBuffer>(renderBuffer.get()), transformation);
}

void OpenGlRenderer::flush(Mat3F const& transformation) {
  flushImmediatePrimitives(transformation);
}

OpenGlRenderer::GlGpuTimer::GlGpuTimer(function<void()> flushPending)
  : m_flushPending(std::move(flushPending)) {}


namespace {
  // REGISTERED ON THE FIRST begin(), NOT THE FIRST DROP. This counter used to be declared inside the
  // drop branch, so a run with no drops never registered it -- and a consumer differencing two
  // snapshots cannot tell ABSENT from ZERO. On the counter that guards every GPU number, that means
  // "we lost no samples" and "nothing was watching" were the same reading.
  //
  // It matters more here than anywhere else: at ring depth 3, 74% of parallax redraws were discarded
  // and the capture rate then shifted 26% -> 98% mid-run, moving the reported cost 4.04x while the
  // GPU did identical work. That survived for exactly one reason -- nothing counted it. A drop
  // counter that can only appear once it has something to report keeps half of that hole open.
  Star::TelemetryCounter& droppedCounter() {
    static auto c = Star::Telemetry::counter("render.gputimer.dropped",
      Star::MetricDesc{Star::MetricDomain::Gpu, Star::MetricOwner::Gl,
                       Star::MetricCadence::Call, Star::MetricRole::Detail});
    return c;
  }
}

void OpenGlRenderer::GlGpuTimer::begin(String const& name, MetricDesc const& desc) {
  droppedCounter();
  if (!Telemetry::deepEnabled())
    return;
  // Task #141: STAR_NO_PERPASS_GPU_TIMERS=1 suppresses the per-pass GL_TIME_ELAPSED queries while LEAVING the
  // whole-frame GL_TIMESTAMP span running. Each GL_TIME_ELAPSED bracket forces a pipeline boundary, so the
  // instrument itself costs GPU time -- and an adversarial analysis put that cost as high as 50-90% of what the
  // compose timers report. Running the frame with and without them makes that cost DIRECTLY MEASURABLE
  // (span_with - span_without) instead of a modelled guess. A diagnostic env var, not a config key: it must
  // never be settable from a live session, and it must not widen the shipped config surface.
  static bool const perPassEnabled = []() {
    char const* e = getenv("STAR_NO_PERPASS_GPU_TIMERS");
    return !(e && *e && *e != '0');
  }();
  if (!perPassEnabled)
    return;
  // REGISTERED ON THE FIRST begin(), NOT ON THE FIRST SUCCESSFUL READBACK. The Telemetry::timer() call in
  // the readback below used to be the ONLY registration for all ten per-pass keys, and it sits inside
  // `if (available)`. A pass that runs every frame but whose queries never resolve was therefore ABSENT from
  // the snapshot rather than zero, and a consumer differencing two snapshots cannot tell those apart -- on
  // the counter set that carries every GPU number. It also meant owner `gl` closed over a DIFFERENT set of
  // parts on the two arms of a lever that switches one compose pass for another.
  //
  // RE-RESOLVED ON EVERY begin, not once behind a flag. Two call sites may name one key (environment and
  // parallax each have two), and descConflict is raised only by a call that PASSES a desc -- registering
  // once would silence the drift detection the descriptor-at-begin design exists for. Caching the handle in
  // the ring pays for it: the readback path no longer touches the registry mutex at all.
  auto& ring = m_rings[name];
  ring.timer = Telemetry::timer(name, desc);
  // NESTING GUARD. GL_TIME_ELAPSED queries CANNOT nest: a glBeginQuery while one is active is
  // GL_INVALID_OPERATION, the inner begin is dropped, and the inner END then closes the OUTER query -- silently
  // darkening both. This bit immediately: the blit timer fires INSIDE the interface timer (blitGlSurface is
  // reached during the interface render), and the resulting numbers were nonsense.
  if (m_active) {
    Logger::warn("GpuTimer::begin('{}') nested inside an active timer -- ignored (GL_TIME_ELAPSED cannot nest)", name);
    return;
  }
  // Submit any pending primitives first so the query measures only the work that follows.
  m_flushPending();
  unsigned slot = ring.writeIdx;
  if (ring.queries[slot] == 0)
    glGenQueries(1, &ring.queries[slot]);
  else if (ring.issued[slot]) {
    // This slot last ran RingDepth begins ago; its result should be ready. Read it without stalling.
    GLuint available = 0;
    glGetQueryObjectuiv(ring.queries[slot], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
      GLuint64 elapsedNs = 0;
      glGetQueryObjectui64v(ring.queries[slot], GL_QUERY_RESULT, &elapsedNs);
      ring.timer.record((int64_t)(elapsedNs / 1000));
      m_lastMicros[name] = (int64_t)(elapsedNs / 1000);
    } else {
      // COUNT THE LOSS. A discarded sample is not a missing datum, it is a BIASED one: the results still
      // in flight are the EXPENSIVE ones, so dropping them silently shifts every consumer toward the cheap
      // tail -- and coverage_scale (scripts/telemetry-window.py) then scales `total` by count-coverage on
      // the assumption the loss was missing-at-random, which under-corrects rather than corrects.
      // Measured, not theorised: at ring depth 3, 74% of parallax redraws were discarded, and the capture
      // rate then shifted 26% -> 98% mid-run, moving the reported cost 4.04x while the GPU did identical
      // work. It survived for exactly one reason -- nothing counted it.
      droppedCounter().inc(1);
    }
    ring.issued[slot] = false; // reuse the query object regardless
  }
  glBeginQuery(GL_TIME_ELAPSED, ring.queries[slot]);
  m_active = true;
  m_current = &ring;
  m_slot = slot;
}

void OpenGlRenderer::GlGpuTimer::end(String const&) {
  if (!m_active)
    return;
  // Submit this scope's primitives so they fall inside the query, then close it.
  m_flushPending();
  glEndQuery(GL_TIME_ELAPSED);
  m_current->issued[m_slot] = true;
  m_current->writeIdx = (m_slot + 1) % RingDepth;
  m_active = false;
  m_current = nullptr;
}

Maybe<int64_t> OpenGlRenderer::GlGpuTimer::lastMicros(String const& name) const {
  if (auto p = m_lastMicros.ptr(name))
    return *p;
  return {};
}

void OpenGlRenderer::setScreenSize(Vec2U screenSize) {
  m_screenSize = screenSize;
  glViewport(0, 0, m_screenSize[0], m_screenSize[1]);
  glUniform2f(m_pass.screenSizeUniform, m_screenSize[0], m_screenSize[1]);

  // A framebuffer that declares an explicit "size" is not screen-sized -- it is sized by its purpose (the
  // lightmap targets are 512x512, the upscale target 2048x2048). Resizing those to the screen resolution
  // allocated ~30MB apiece instead of 2-32MB. sizeFor() now encodes that rule, and resize() is idempotent, so
  // such a surface asks for the size it already has and issues no GL calls -- the hand-written `continue` that
  // used to skip it is gone, along with the thirty lines of format ladder and size arithmetic that followed.
  //
  // The REGISTRY owns which targets exist, so it owns the loop over them. The renderer no longer holds the map.
  m_targets.resizeAll(m_screenSize);
}

void OpenGlRenderer::startFrame() {
  // WHOLE-FRAME GPU SPAN (task #141). The per-pass timers use GL_TIME_ELAPSED and CANNOT NEST -- there is a
  // single m_gpuTimerActive bool -- so they can never tell us the frame TOTAL, and therefore can never tell us
  // how much of the frame they FAIL to account for. GL_TIMESTAMP is a different query target and coexists with
  // them freely, so this measures the GPU-timeline span of the entire frame (every pass, the interface render,
  // the clears, and the final blit) WHILE the per-pass timers still run.
  //
  // That gives three numbers from two runs:
  //   frameSpan with per-pass timers ON   -- what the instrumented frame costs
  //   frameSpan with per-pass timers OFF  -- what the frame REALLY costs (the timers' own cost is the delta)
  //   sum(per-pass timers)                -- what the campaign has been quoting
  // total-vs-sum exposes the UNATTRIBUTED remainder directly. Half the GPU frame may be in it.
  if (Telemetry::deepEnabled()) {
    auto& ring = m_frameSpan;
    unsigned slot = ring.writeIdx;
    if (ring.begins[slot] == 0) {
      glGenQueries(1, &ring.begins[slot]);
      glGenQueries(1, &ring.ends[slot]);
    } else if (ring.issued[slot]) {
      // This slot was issued GpuTimerRingSize frames ago; read it back without stalling the pipeline.
      GLuint availB = 0, availE = 0;
      glGetQueryObjectuiv(ring.begins[slot], GL_QUERY_RESULT_AVAILABLE, &availB);
      glGetQueryObjectuiv(ring.ends[slot], GL_QUERY_RESULT_AVAILABLE, &availE);
      if (availB && availE) {
        GLuint64 t0 = 0, t1 = 0;
        glGetQueryObjectui64v(ring.begins[slot], GL_QUERY_RESULT, &t0);
        glGetQueryObjectui64v(ring.ends[slot], GL_QUERY_RESULT, &t1);
        if (t1 > t0) {
          // role=Total, not Budget: this span IS the Gl owner's whole (every pass, the interface
          // render, the clears and the final blit -- see the comment at the end of finishFrame()), so
          // it is excluded from the sum of parts rather than counted as one of its own parts.
          //
          // Not a GpuTimer/gpuTimer() site -- this is read straight from GL_TIMESTAMP queries -- so it
          // uses the ordinary CPU-style Telemetry::timer(key, desc) overload directly; the desc travels
          // with the call that records it, same as everywhere else.
          Telemetry::timer("render.frame.gpu_span_us",
            MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Total})
            .record((int64_t)((t1 - t0) / 1000));
        }
      }
      ring.issued[slot] = false;
    }
    glQueryCounter(ring.begins[slot], GL_TIMESTAMP);
    m_frameSpanSlot = slot;
    m_frameSpanOpen = true;
  }

  if (m_scissorRect)
    glDisable(GL_SCISSOR_TEST);

  // Task #141: EVERY framebuffer is cleared EVERY frame -- at 2560x1440 that is several full-screen RGBA16F
  // clears, and none of them were ever timed. Part of the unattributed 1.8-3.5ms.
  m_gpuTimer.begin("render.frame.clear.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});

  // The REGISTRY clears its targets; each surface clears its own faces. Nobody reaches for the raw face ids.
  m_targets.clearAll();

  // FRAME-BOUNDARY RESET as ONE act (backlog item 5): put GL_DRAW back on the screen AND drop the pass cache
  // together, welded inside GlPass::resetToScreen() so startFrame can no longer bind 0 and forget the drop.
  // clearAll() bound each face outside GlPass, so GL_DRAW no longer matches whatever the pass cached at the end
  // of the previous frame. Without the drop, a frame that ended with a non-screen target still live (a
  // retained/env-cache surface warmed last -- exactly the Layer-2/3 consumer shape) would leave the cache
  // reading `target == T` while GL reads 0; the next frame's first bindTarget(T) would early-out over that stale
  // cache and draw to the screen. In-tree the interface ends every frame on the screen, so this is a
  // latent-hazard closure, not a live fix.
  m_pass.resetToScreen();

  glClear(GL_COLOR_BUFFER_BIT);

  m_gpuTimer.end("render.frame.clear.gpu_us");

  if (m_scissorRect)
    glEnable(GL_SCISSOR_TEST);
}

// THE GL-STATE AUDIT (#139 phase 1b). The gate the pixel oracles structurally cannot be.
//
// The three in-frame oracles are DIFFERENTIAL: reference and cache-under-test share one draw lambda, at one
// frame position, under one ambient GL state. Anything ambient therefore cancels on both sides and reads as
// MATCH -- which is exactly why a refresh-key omission, an FBO lifecycle change or an ambient-state change
// can be invisible to a green gate, and how four of them reached the Director in play (#136). This asks a
// question no differential comparison can: does the renderer's own idea of what is bound match GL's?
//
// UNCONDITIONAL, and for the reason already written at the bottom of this function for the error drain: a
// diagnostic gated on DebugEnabled is constexpr-false in every build we ship, profile or gate, which
// guarantees its absence exactly where problems are actually found. The cost is five glGetIntegerv --
// driver-side state reads, not sync points, against the thousands of GL calls the frame already issued.
unsigned OpenGlRenderer::auditGlState() {
  bool report = m_glStateReportBudget > 0;
  unsigned bad = m_pass.auditGlState(report);

  // SCISSOR. m_scissorRect is the renderer's belief; GL_SCISSOR_TEST is the fact. Only the enable is
  // compared: the rect itself is set in the same act that records it, so a divergence there is not
  // expressible, while the ENABLE is toggled independently by startFrame around the frame clear.
  GLboolean scissorEnabled = GL_FALSE;
  glGetBooleanv(GL_SCISSOR_TEST, &scissorEnabled);
  if ((bool)scissorEnabled != (bool)m_scissorRect) {
    ++bad;
    if (report)
      Logger::error("[glstate] scissor desync: GL_SCISSOR_TEST is {}, the renderer believes {}.",
        scissorEnabled ? "enabled" : "disabled", m_scissorRect ? "enabled" : "disabled");
  }

  // BLEND, checked against an EXPECTED value rather than a belief, because setBlendMode is fire-and-forget:
  // it issues GL and records nothing, so there is no belief to compare. What is invariant is weaker but
  // real -- BlendMode::None is the only mode that disables blending, and every consumer that sets it
  // restores Alpha immediately afterwards, so no frame should END with blending off. A frame that does has
  // leaked a None out of a compose arm, which is precisely the shape of bug that reaches the Director as an
  // intermittently wrong-looking backdrop.
  GLboolean blendEnabled = GL_FALSE;
  glGetBooleanv(GL_BLEND, &blendEnabled);
  if (!blendEnabled) {
    ++bad;
    if (report)
      Logger::error("[glstate] the frame ended with GL_BLEND disabled. Some consumer set BlendMode::None and "
                    "did not restore Alpha; every draw until the next setBlendMode replaces instead of blends.");
  }

  if (bad) {
    m_glStateMismatches.inc(bad);
    if (report && --m_glStateReportBudget == 0)
      Logger::error("[glstate] further GL-state desync reports suppressed this session; the counter "
                    "render.glstate.mismatches keeps counting.");
  }
  return bad;
}

void OpenGlRenderer::finishFrame() {
  flushImmediatePrimitives();

  // AUDIT BEFORE THE BLIT, not after. Below, glBindFramebuffer(GL_FRAMEBUFFER, 0) unconditionally puts GL
  // back on the screen -- so an audit placed after it would find the screen bound every time and agree with
  // itself, no matter what the frame actually did. Here it sees the state the frame genuinely ended in.
  //
  // FAULT INJECTION, because a check nobody has watched fail is not known to work. STAR_RENDERTEST_GLSTATE_DESYNC
  // binds "main"'s write face behind the pass's back: GL then draws somewhere the cache does not know about,
  // which is the exact desync this audit exists to catch, and the render gate must go red on it.
  static int const forceDesync = []() {
    char const* e = getenv("STAR_RENDERTEST_GLSTATE_DESYNC");
    return e && String(e) != "0" ? 1 : 0;
  }();
  if (forceDesync) {
    if (auto main = m_targets.find("main"))
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, main->writeFace().id);
  }

  auditGlState();

  // Close the whole-frame GPU span AFTER the final blit, not here -- see the end of this function.
  // Make sure that the immediate render buffer doesn't needlessly lock texutres
  // from being compressed.
  List<RenderPrimitive> empty;
  m_immediateRenderBuffer->set(empty);

  filter(m_liveTextureGroups, [](auto const& p) {
        unsigned const CompressionsPerFrame = 1;

        if (!p.unique() || p->textureAtlasSet.totalTextures() > 0) {
          p->textureAtlasSet.compressionPass(CompressionsPerFrame);
          return true;
        }

        return false;
      });

  // Blit if another shader hasn't
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Close the whole-frame GPU span here -- AFTER every pass, the interface render, the clears and the final
  // blit -- so "render.frame.gpu_span_us" is the frame's ENTIRE GPU cost, not just the part we happen to have
  // instrumented. Everything the per-pass timers do NOT cover shows up as (span - sum(passes)).
  if (m_frameSpanOpen) {
    auto& ring = m_frameSpan;
    glQueryCounter(ring.ends[m_frameSpanSlot], GL_TIMESTAMP);
    ring.issued[m_frameSpanSlot] = true;
    ring.writeIdx = (ring.writeIdx + 1) % GpuTimerRingSize;
    m_frameSpanOpen = false;
  }

  // UNCONDITIONAL, deliberately. This was `if (DebugEnabled)`, and DebugEnabled is !NDEBUG -- so in every
  // build we ship, profile or gate it is constexpr false and this whole branch is dead-code eliminated.
  // The result: the ONLY drains in a release run were renderer init, setEffectConfig and shutdown, so an
  // error raised anywhere in a session accumulated silently and surfaced at exit as an unattributable
  // "OpenGL errors during shutdown". Debug-gating a diagnostic guarantees it is absent exactly where
  // problems are actually found -- in release, on the Director's machine.
  //
  // The cost is one glGetError per frame. That is a driver state read, NOT a sync point (contrast
  // glFinish), against the thousands of GL calls this frame already issued. It is free.
  logGlErrorSummary("OpenGL errors this frame");
}

OpenGlRenderer::GlTextureAtlasSet::GlTextureAtlasSet(unsigned atlasNumCells)
  : TextureAtlasSet(16, atlasNumCells) {}

GLuint OpenGlRenderer::GlTextureAtlasSet::createAtlasTexture(Vec2U const& size, PixelFormat pixelFormat) {
  GLuint glTextureId;
  glGenTextures(1, &glTextureId);
  if (glTextureId == 0)
    throw RendererException("Could not generate texture in OpenGlRenderer::TextureGroup::createAtlasTexture()");

  glBindTexture(GL_TEXTURE_2D, glTextureId);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (textureFiltering == TextureFiltering::Nearest) {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  } else {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }

  uploadTextureImage(pixelFormat, size, nullptr);
  return glTextureId;
}

void OpenGlRenderer::GlTextureAtlasSet::destroyAtlasTexture(GLuint const& glTexture) {
  glDeleteTextures(1, &glTexture);
}

void OpenGlRenderer::GlTextureAtlasSet::copyAtlasPixels(
    GLuint const& glTexture, Vec2U const& bottomLeft, Image const& image) {
  glBindTexture(GL_TEXTURE_2D, glTexture);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  GLenum format;
  auto pixelFormat = image.pixelFormat();
  if (pixelFormat == PixelFormat::RGB24)
    format = GL_RGB;
  else if (pixelFormat == PixelFormat::RGBA32)
    format = GL_RGBA;
  else if (pixelFormat == PixelFormat::BGR24)
    format = GL_BGR;
  else if (pixelFormat == PixelFormat::BGRA32)
    format = GL_BGRA;
  else
    throw RendererException("Unsupported texture format in OpenGlRenderer::TextureGroup::copyAtlasPixels");

  glTexSubImage2D(GL_TEXTURE_2D, 0, bottomLeft[0], bottomLeft[1], image.width(), image.height(), format, GL_UNSIGNED_BYTE, image.data());
}

OpenGlRenderer::GlTextureGroup::GlTextureGroup(unsigned atlasNumCells)
  : textureAtlasSet(atlasNumCells) {}

OpenGlRenderer::GlTextureGroup::~GlTextureGroup() {
  textureAtlasSet.reset();
}

TextureFiltering OpenGlRenderer::GlTextureGroup::filtering() const {
  return textureAtlasSet.textureFiltering;
}

TexturePtr OpenGlRenderer::GlTextureGroup::create(Image const& texture) {
  // If the image is empty, or would not fit in the texture atlas with border
  // pixels, just create a regular texture
  Vec2U atlasTextureSize = textureAtlasSet.atlasTextureSize();
  if (texture.empty() || texture.width() + 2 > atlasTextureSize[0] || texture.height() + 2 > atlasTextureSize[1])
    return createGlTexture(texture, TextureAddressing::Clamp, textureAtlasSet.textureFiltering);

  auto glGroupedTexture = make_ref<GlGroupedTexture>();
  glGroupedTexture->parentGroup = shared_from_this();
  glGroupedTexture->parentAtlasTexture = textureAtlasSet.addTexture(texture);

  return glGroupedTexture;
}

OpenGlRenderer::GlGroupedTexture::~GlGroupedTexture() {
  if (parentAtlasTexture)
    parentGroup->textureAtlasSet.freeTexture(parentAtlasTexture);
}

Vec2U OpenGlRenderer::GlGroupedTexture::size() const {
  return parentAtlasTexture->imageSize();
}

TextureFiltering OpenGlRenderer::GlGroupedTexture::filtering() const {
  return parentGroup->filtering();
}

TextureAddressing OpenGlRenderer::GlGroupedTexture::addressing() const {
  return TextureAddressing::Clamp;
}

GLuint OpenGlRenderer::GlGroupedTexture::glTextureId() const {
  return parentAtlasTexture->atlasTexture();
}

Vec2U OpenGlRenderer::GlGroupedTexture::glTextureSize() const {
  return parentGroup->textureAtlasSet.atlasTextureSize();
}

Vec2U OpenGlRenderer::GlGroupedTexture::glTextureCoordinateOffset() const {
  return parentAtlasTexture->atlasTextureCoordinates().min();
}

void OpenGlRenderer::GlGroupedTexture::incrementBufferUseCount() {
  if (bufferUseCount == 0)
    parentAtlasTexture->setLocked(true);
  ++bufferUseCount;
}

void OpenGlRenderer::GlGroupedTexture::decrementBufferUseCount() {
  starAssert(bufferUseCount != 0);
  if (bufferUseCount == 1)
    parentAtlasTexture->setLocked(false);
  --bufferUseCount;
}

// GlLoneTexture's methods moved to StarGlTexturePrimitives.cpp (Tier 1 of the §8.ii extraction).

OpenGlRenderer::GlRenderBuffer::GlRenderBuffer() {
  glGenVertexArrays(1, &vertexArray);
}

OpenGlRenderer::GlRenderBuffer::~GlRenderBuffer() {
  for (auto const& texture : usedTextures) {
    if (auto gt = as<GlGroupedTexture>(texture.get()))
      gt->decrementBufferUseCount();
  }
  for (auto const& vb : vertexBuffers)
    glDeleteBuffers(1, &vb.vertexBuffer);
  glDeleteVertexArrays(1, &vertexArray);
}

void OpenGlRenderer::GlRenderBuffer::set(List<RenderPrimitive>& primitives) {
  for (auto const& texture : usedTextures) {
    if (auto gt = as<GlGroupedTexture>(texture.get()))
      gt->decrementBufferUseCount();
  }
  usedTextures.clear();

  auto oldVertexBuffers = take(vertexBuffers);

  List<GLuint> currentTextures;
  List<Vec2U> currentTextureSizes;
  size_t currentVertexCount = 0;
  glBindVertexArray(vertexArray);
  auto finishCurrentBuffer = [&]() {
    if (currentVertexCount > 0) {
      GlVertexBuffer vb;
      for (size_t i = 0; i < currentTextures.size(); ++i) {
        vb.textures.append(GlVertexBufferTexture{currentTextures[i], currentTextureSizes[i]});
      }
      vb.vertexCount = currentVertexCount;
      if (!oldVertexBuffers.empty()) {
        auto oldVb = oldVertexBuffers.takeLast();
        vb.vertexBuffer = oldVb.vertexBuffer;
        vb.byteCapacity = oldVb.byteCapacity;
        glBindBuffer(GL_ARRAY_BUFFER, vb.vertexBuffer);
        if (vb.byteCapacity >= accumulationBuffer.size()) {
          // BUFFER ORPHANING (task #141 / un-stakes #125). A bare glBufferSubData re-writes a buffer object the
          // GPU may STILL BE READING from the previous flush, which forces an IMPLICIT SYNCHRONISATION -- a full
          // pipeline stall. Measured: the in-game HUD adds ~21 flushes/frame (one per widget, via
          // Widget::render -> setupDrawRegion -> setScissorRect -> flushImmediatePrimitives) and ~8,296us of
          // frame time -- about 400us per flush, for flushes carrying TWELVE QUADS. That is not rasterisation.
          //
          // Passing a null pointer to glBufferData first tells the driver the old contents are dead, so it hands
          // back a FRESH backing store instead of waiting for the in-flight draw to finish reading the old one.
          // Same API calls, same data, no sync.
          //
          // WITNESS for the renderVboOrphan lever. Registered unconditionally and incremented only on the
          // orphaning arm, so with the lever off it reads ZERO rather than ABSENT -- a consumer differencing
          // two snapshots cannot tell those apart. It exists because this lever moves nothing else that is
          // counted: it is byte-identical by construction and its whole effect is stall cost, so an A/B could
          // run with it off on BOTH legs and still report a full table of plausible deltas. This is the one
          // quantity that cannot be nonzero when the lever is off.
          static auto orphaned = Telemetry::counter("render.vbo.orphaned",
            MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
          if (!NoVboOrphan) {
            glBufferData(GL_ARRAY_BUFFER, vb.byteCapacity, nullptr, GL_STREAM_DRAW);
            orphaned.inc(1);
          }
          glBufferSubData(GL_ARRAY_BUFFER, 0, accumulationBuffer.size(), accumulationBuffer.ptr());
        } else {
          glBufferData(GL_ARRAY_BUFFER, accumulationBuffer.size(), accumulationBuffer.ptr(), GL_STREAM_DRAW);
          vb.byteCapacity = accumulationBuffer.size();
        }
      } else {
        glGenBuffers(1, &vb.vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vb.vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, accumulationBuffer.size(), accumulationBuffer.ptr(), GL_STREAM_DRAW);
        vb.byteCapacity = accumulationBuffer.size();
      }

      vertexBuffers.emplace_back(std::move(vb));

      currentTextures.clear();
      currentTextureSizes.clear();
      accumulationBuffer.clear();
      currentVertexCount = 0;
    }
  };

  auto textureCount = useMultiTexturing ? MultiTextureCount : 1;
  auto addCurrentTexture = [&](TexturePtr texture) -> pair<uint8_t, Vec2F> {
    if (!texture)
      texture = whiteTexture;

    auto glTexture = as<GlTexture>(texture.get());
    GLuint glTextureId = glTexture->glTextureId();

    auto textureIndex = currentTextures.indexOf(glTextureId);
    if (textureIndex == NPos) {
      if (currentTextures.size() >= textureCount)
        finishCurrentBuffer();

      textureIndex = currentTextures.size();
      currentTextures.append(glTextureId);
      currentTextureSizes.append(glTexture->glTextureSize());
    }

    if (auto gt = as<GlGroupedTexture>(texture.get()))
      gt->incrementBufferUseCount();
    usedTextures.add(std::move(texture));

    return {float(textureIndex), Vec2F(glTexture->glTextureCoordinateOffset())};
  };

  auto appendBufferVertex = [&](RenderVertex const& v, uint8_t textureIndex, Vec2F textureCoordinateOffset, RenderVertex const& prev, RenderVertex const& next) {
    size_t off = accumulationBuffer.size();
    accumulationBuffer.resize(accumulationBuffer.size() + sizeof(GlRenderVertex));
    GlRenderVertex& glv = *(GlRenderVertex*)(accumulationBuffer.ptr() + off);
    glv.pos = v.screenCoordinate;
    glv.uv = v.textureCoordinate + textureCoordinateOffset;
    glv.color = v.color;
    glv.pack.vars.textureIndex = textureIndex;
    glv.pack.vars.fullbright = v.param1 > 0.0f;
    // Tell the vertex shader to round to the nearest pixel if the vertices form a straight
    // edge, to ensure sharpness with supersampling. If we rounded *all* vertex positions,
    // it'd cause slight visual issues with sprites rotating around a point.
    glv.pack.vars.rX = min(abs(glv.pos.x() - prev.screenCoordinate.x()), abs(glv.pos.x() - next.screenCoordinate.x())) < 0.001f;
    glv.pack.vars.rY = min(abs(glv.pos.y() - prev.screenCoordinate.y()), abs(glv.pos.y() - next.screenCoordinate.y())) < 0.001f;
    glv.pack.vars.unused = 0;
    ++currentVertexCount;
    return glv;
  };

  uint8_t textureIndex = 0;
  Vec2F textureOffset = {};
  for (auto& primitive : primitives) {
    if (auto tri = primitive.ptr<RenderTriangle>()) {
      tie(textureIndex, textureOffset) = addCurrentTexture(std::move(tri->texture));

      appendBufferVertex(tri->a, textureIndex, textureOffset, tri->c, tri->b);
      appendBufferVertex(tri->b, textureIndex, textureOffset, tri->a, tri->c);
      appendBufferVertex(tri->c, textureIndex, textureOffset, tri->b, tri->a);

    } else if (auto quad = primitive.ptr<RenderQuad>()) {
      tie(textureIndex, textureOffset) = addCurrentTexture(std::move(quad->texture));

      // = prev and next are altered - the diagonal across the quad is bad for the rounding check
      appendBufferVertex(quad->a, textureIndex, textureOffset, quad->d, quad->b);
      appendBufferVertex(quad->b, textureIndex, textureOffset, quad->a, quad->c); //
      appendBufferVertex(quad->c, textureIndex, textureOffset, quad->b, quad->d);

      appendBufferVertex(quad->a, textureIndex, textureOffset, quad->d, quad->b);
      appendBufferVertex(quad->c, textureIndex, textureOffset, quad->b, quad->d); //
      appendBufferVertex(quad->d, textureIndex, textureOffset, quad->c, quad->a);

    } else if (auto poly = primitive.ptr<RenderPoly>()) {
      if (poly->vertexes.size() > 2) {
        tie(textureIndex, textureOffset) = addCurrentTexture(std::move(poly->texture));

        for (size_t i = 1; i < poly->vertexes.size() - 1; ++i) {
            RenderVertex const& a = poly->vertexes[0],
                                b = poly->vertexes[i],
                                c = poly->vertexes[i + 1];
          appendBufferVertex(a, textureIndex, textureOffset, c, b);
          appendBufferVertex(b, textureIndex, textureOffset, a, c);
          appendBufferVertex(c, textureIndex, textureOffset, b, a);
        }
      }
    }
  }

  vertexBuffers.reserve(primitives.size() * 6);
  finishCurrentBuffer();

  for (auto const& vb : oldVertexBuffers)
    glDeleteBuffers(1, &vb.vertexBuffer);
}

bool OpenGlRenderer::logGlErrorSummary(String prefix) {
  // EVERY drained error also increments a COUNTER, not just a log line. A log line needs a human to read
  // it; a counter rides in every telemetry capture, is windowable to the run that introduced it, and lets
  // the gate ASSERT zero. cadence=Call because errors are exceptional -- they are not per-anything, so
  // there is no tick count to be a fraction of, and Call is returned unscaled by the consumer.
  static auto glErrors = Telemetry::counter("render.gl.errors",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});

  // RATE LIMIT. Now that this is drained every frame rather than only at shutdown, a persistent error
  // would emit two log lines per frame forever -- burying everything else in the log, which is the exact
  // failure that made the shutdown-only report useless in the first place. Log the first few in full,
  // then go quiet and let the counter carry the signal.
  static int loggedBursts = 0;
  constexpr int MaxLoggedBursts = 16;

  GLenum error = glGetError();
  if (!error)
    return false;

  bool quiet = loggedBursts >= MaxLoggedBursts;
  if (!quiet) {
    ++loggedBursts;
    Logger::error("{}: ", prefix);
  }
  do {
    glErrors.inc(1);
    if (quiet)
      continue;
    if (error == GL_INVALID_ENUM) {
      Logger::error("GL_INVALID_ENUM");
    } else if (error == GL_INVALID_VALUE) {
      Logger::error("GL_INVALID_VALUE");
    } else if (error == GL_INVALID_OPERATION) {
      Logger::error("GL_INVALID_OPERATION");
    } else if (error == GL_INVALID_FRAMEBUFFER_OPERATION) {
      Logger::error("GL_INVALID_FRAMEBUFFER_OPERATION");
    } else if (error == GL_OUT_OF_MEMORY) {
      Logger::error("GL_OUT_OF_MEMORY");
    } else if (error == GL_STACK_UNDERFLOW) {
      Logger::error("GL_STACK_UNDERFLOW");
    } else if (error == GL_STACK_OVERFLOW) {
      Logger::error("GL_STACK_OVERFLOW");
    } else {
      Logger::error("<UNRECOGNIZED GL ERROR>");
    }
  } while ((error = glGetError()));

  if (!quiet && loggedBursts == MaxLoggedBursts)
    Logger::error("(further GL error reports suppressed -- render.gl.errors keeps counting)");
  return true;
}

GLint OpenGlRenderer::uploadTextureImage(PixelFormat pixelFormat, Vec2U size, uint8_t const* data, GlLoneTexture* record) {
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  Maybe<GLenum> internalFormat;
  GLenum format;
  GLenum type = GL_UNSIGNED_BYTE;
  if (pixelFormat == PixelFormat::RGB24)
    format = GL_RGB;
  else if (pixelFormat == PixelFormat::RGBA32)
    format = GL_RGBA;
  else if (pixelFormat == PixelFormat::BGR24)
    format = GL_BGR;
  else if (pixelFormat == PixelFormat::BGRA32)
    format = GL_BGRA;
  else {
    type = GL_FLOAT;
    if (pixelFormat == PixelFormat::RGB_F) {
      internalFormat = GL_RGB32F;
      format = GL_RGB;
    } else if (pixelFormat == PixelFormat::RGBA_F) {
      internalFormat = GL_RGBA32F;
      format = GL_RGBA;
    } else
      throw RendererException("Unsupported texture format in OpenGlRenderer::uploadTextureImage");
  }

  GLint const specified = (GLint)internalFormat.value(format);
  glTexImage2D(GL_TEXTURE_2D, 0, specified, size[0], size[1], 0, format, type, data);
  // Record the WHOLE descriptor for the act that just specified it -- no caller can spec an image and forget
  // half. The raw-GLuint atlas caller passes record=nullptr (it holds no GlLoneTexture descriptor).
  if (record)
    record->recordStorage(size, specified);
  return specified;
}

void OpenGlRenderer::uploadLoneStorage(GlLoneTexture& tex, Vec2U size, GLint internalFormat, GLenum format,
    GLenum type, void const* data, bool fresh) {
  // SubImage into existing storage ONLY when the recorded descriptor still matches BOTH size and format;
  // otherwise re-specify and rewrite the whole descriptor in the same breath. `fresh` (just-allocated:
  // internalFormat still 0, storage unspecified) forces the re-spec.
  if (!fresh && tex.glTextureSize() == size && tex.format() == internalFormat) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size[0], size[1], format, type, data);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size[0], size[1], 0, format, type, data);
    tex.recordStorage(size, internalFormat);
  }
}

void OpenGlRenderer::flushImmediatePrimitives(Mat3F const& transformation) {
  if (m_immediatePrimitives.empty())
    return;

  // Task #141: every flush re-writes the SINGLE shared immediate VBO (glBufferSubData / glBufferData below)
  // and then draws from it. If the GPU is still reading that buffer from the previous flush, the write forces
  // an IMPLICIT SYNCHRONISATION -- a full pipeline stall. Widget::render -> setupDrawRegion -> setScissorRect
  // calls this for EVERY widget, and the in-game HUD has ~92 of them. Count them, and count the primitives per
  // flush: a high flush count with a tiny primitive count is the signature of stall-per-widget.
  static auto flushes = Telemetry::counter("render.flush.count",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  static auto flushPrims = Telemetry::counter("render.flush.primitives",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  flushes.inc(1);
  flushPrims.inc(m_immediatePrimitives.size());

  m_immediateRenderBuffer->set(m_immediatePrimitives);
  m_immediatePrimitives.resize(0);
  renderGlBuffer(*m_immediateRenderBuffer, transformation);
}

auto OpenGlRenderer::createGlTexture(ImageView const& image, TextureAddressing addressing, TextureFiltering filtering)
    ->RefPtr<GlLoneTexture> {
  // createGlTexture = createEmptyGlTexture (allocate + sampling params) + the pixel upload. Delegating keeps
  // glGenTextures to ONE allocator per side (this one, via the empty allocator, plus allocateFace and
  // createAtlasTexture) instead of a second verbatim copy of the gen-bind-param preamble. Same GL call stream:
  // the empty allocator sets WRAP with glTexParameteri and MIN/MAG with glTexParameterf, exactly as here, and
  // leaves the new texture BOUND, so the upload lands in it.
  auto glLoneTexture = createEmptyGlTexture(image.size, addressing, filtering);

  // uploadTextureImage records the descriptor (re-stamping textureSize to the same image.size the allocator
  // already set, plus internalFormat). Empty images skip it: internalFormat stays the 0 sentinel.
  if (!image.empty())
    uploadTextureImage(image.format, image.size, image.data, glLoneTexture.get());

  return glLoneTexture;
}

auto OpenGlRenderer::createEmptyGlTexture(Vec2U size, TextureAddressing addressing, TextureFiltering filtering)
    ->RefPtr<GlLoneTexture> {
  auto tex = make_ref<GlLoneTexture>();
  tex->textureFiltering = filtering;
  tex->textureAddressing = addressing;
  tex->setAllocatedSize(size);
  glGenTextures(1, &tex->textureId);
  // The one effect-side allocator, so the one place the gen can fail. setEffectTextureHalf/R8 and createGlTexture
  // all route their allocation through here and thereby GAIN this throw -- the two upload setters' old inline
  // copies of the preamble lacked it, silently proceeding to bind and spec texture 0. Error path only.
  if (tex->textureId == 0)
    throw RendererException("Could not generate texture in OpenGlRenderer::createEmptyGlTexture");
  glBindTexture(GL_TEXTURE_2D, tex->textureId);
  GLenum wrap = addressing == TextureAddressing::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  GLenum filt = filtering == TextureFiltering::Nearest ? GL_NEAREST : GL_LINEAR;
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
  return tex;
}

auto OpenGlRenderer::createGlRenderBuffer() -> shared_ptr<GlRenderBuffer> {
  auto glrb = make_shared<GlRenderBuffer>();
  glrb->whiteTexture = m_whiteTexture;
  glrb->useMultiTexturing = m_useMultiTexturing;
  return glrb;
}

void OpenGlRenderer::renderGlBuffer(GlRenderBuffer const& renderBuffer, Mat3F const& transformation) {
  // An attribute location is -1 when the linker found that attribute INACTIVE -- absent from the shader, or
  // declared and never read. That is not exotic: lightingPassthrough.vert declares all four vertex inputs and
  // reads only vertexPosition, so three of its four locations are -1 on every fullscreen composite.
  //
  // The index parameter of glEnableVertexAttribArray / glVertexAttribPointer is a GLuint, so -1 arrives as
  // 0xFFFFFFFF -- always >= GL_MAX_VERTEX_ATTRIBS -- and the call raises GL_INVALID_VALUE. Three per composite
  // draw, every frame.
  //
  // Skipping is byte-identical: these are precisely the calls that were already FAILING, and therefore already
  // doing nothing, and an inactive attribute is by definition never read by the program.
  //
  // Why this mattered far beyond three wasted calls: the GL error flag SATURATES -- once set, no further error
  // is recorded until glGetError clears it. A per-frame error therefore MASKS every other GL error the engine
  // can raise. This is what made the long-standing "OpenGL errors during shutdown" unattributable (#131) and
  // what left the render gate's GL assertion unable to mean anything.
  auto bindAttrib = [](GLint index, GLint size, GLenum type, GLboolean normalized, size_t offset) {
    if (index < 0)
      return;
    glEnableVertexAttribArray((GLuint)index);
    glVertexAttribPointer((GLuint)index, size, type, normalized, sizeof(GlRenderVertex), (GLvoid*)offset);
  };

  for (auto const& vb : renderBuffer.vertexBuffers) {
    glUniformMatrix3fv(m_pass.vertexTransformUniform, 1, GL_TRUE, transformation.ptr());

    if (m_pass.effect()->includeVBTextures) {
      for (size_t i = 0; i < vb.textures.size(); ++i) {
        glUniform2f(m_pass.textureSizeUniforms[i], vb.textures[i].size[0], vb.textures[i].size[1]);
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, vb.textures[i].texture);
      }
    }

    for (auto const& p : m_pass.effect()->textures) {
      if (p.second.texture()) {
        glActiveTexture(GL_TEXTURE0 + p.second.textureUnit);
        glBindTexture(GL_TEXTURE_2D, p.second.texture()->textureId);

        // Filtering belongs to the BINDING, not to the texture.
        //
        // An effect declares how it wants to sample ("textureFiltering" per effectTexture), but a texture
        // object carries only one filter mode -- so when the texture comes from a framebuffer, the effect's
        // declaration was silently ignored and it got whatever the framebuffer was allocated with. Real
        // consequence in this tree: lightingSpread.config asks for "nearest" on its lightState sampler and
        // was served "linear", because that is what the lightingGpu framebuffer happens to be (the bicubic
        // upscale needs linear from the SAME surface). It did not corrupt anything only because the spread's
        // sample coordinates land on exact texel centres, where linear degenerates to nearest -- which is
        // luck, not design.
        //
        // Apply the declaration at the point of use. One surface can now be sampled "nearest" by one effect
        // and "linear" by another, which is what the callers were asking for all along. (Sampler objects
        // would be the tidier mechanism but they are GL 3.3; the floor here is 3.2.)
        GLenum filt = p.second.textureFiltering == TextureFiltering::Nearest ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
        GLenum wrap = p.second.textureAddressing == TextureAddressing::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
      }
    }

    glBindBuffer(GL_ARRAY_BUFFER, vb.vertexBuffer);

    bindAttrib(m_pass.positionAttribute, 2, GL_FLOAT, GL_FALSE, offsetof(GlRenderVertex, pos));
    bindAttrib(m_pass.texCoordAttribute, 2, GL_FLOAT, GL_FALSE, offsetof(GlRenderVertex, uv));
    bindAttrib(m_pass.colorAttribute, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(GlRenderVertex, color));
    // vertexData is an INTEGER attribute -- glVertexAttribIPointer, not the normalizing float form. Same -1 rule.
    if (m_pass.dataAttribute >= 0) {
      glEnableVertexAttribArray((GLuint)m_pass.dataAttribute);
      glVertexAttribIPointer((GLuint)m_pass.dataAttribute, 1, GL_INT, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, pack));
    }

    glDrawArrays(GL_TRIANGLES, 0, vb.vertexCount);
  }
}


// Copies `frameBuffer` (or its alt half, when the calling effect is double-buffered) into whatever draw target
// is currently bound -- GlPass::bindTarget binds it, and switchEffectConfig binds the screen (0) for an
// effect with no render target, which is how "main" reaches the display.
//
// NOTE the missing once-per-frame guard. Upstream deliberately dropped the `if (blitted) return;` that vanilla
// had, because with double-buffering (#542) the alt->primary copy must happen on EVERY effect switch, not once
// per frame -- the guard would silently starve the feature. We keep upstream's semantics exactly and only wrap
// them in the timer: at 2560x1440 RGBA16F, MSAA-resolving when antiAliasing is on, this blit is not free, and
// it was part of the 1.8-3.5ms/frame the whole-frame span proved was unaccounted for (task #141).
void OpenGlRenderer::blitGlSurface(RefPtr<GlSurface> const& frameBuffer, bool const& useAlt) {
  m_gpuTimer.begin("render.frame.blit.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});

  auto& size = m_screenSize;
  // useAlt: the caller is a double-buffered effect, so it wants the face it is NOT writing -- the one that
  // still holds the previous content. Exactly readFace().
  glBindFramebuffer(GL_READ_FRAMEBUFFER, useAlt ? frameBuffer->readFace().id : frameBuffer->writeFace().id);
  glBlitFramebuffer(
    0, 0, size[0], size[1],
    0, 0, size[0], size[1],
    GL_COLOR_BUFFER_BIT, GL_NEAREST
  );

  m_gpuTimer.end("render.frame.blit.gpu_us");
}

// The renderer's single door to the pass bind. Every caller goes through here.
void OpenGlRenderer::bindTarget(RefPtr<GlSurface> const& frameBuffer) {
  m_pass.bindTarget(frameBuffer, m_screenSize);
}





}
