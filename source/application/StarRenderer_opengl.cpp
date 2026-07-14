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

size_t const MultiTextureCount = 4;


char const* DefaultVertexShader = R"SHADER(
#version 150

uniform vec2 textureSize0;
uniform vec2 textureSize1;
uniform vec2 textureSize2;
uniform vec2 textureSize3;
uniform vec2 screenSize;
uniform mat3 vertexTransform;

in vec2 vertexPosition;
in vec4 vertexColor;
in vec2 vertexTextureCoordinate;
in int vertexData;

out vec2 fragmentTextureCoordinate;
flat out int fragmentTextureIndex;
out vec4 fragmentColor;

void main() {
  vec2 screenPosition = (vertexTransform * vec3(vertexPosition, 1.0)).xy;
  gl_Position = vec4(screenPosition / screenSize * 2.0 - 1.0, 0.0, 1.0);
  if (((vertexData >> 3) & 0x1) == 1)
    screenPosition.x = round(screenPosition.x);
  if (((vertexData >> 4) & 0x1) == 1)
    screenPosition.y = round(screenPosition.y);
  int vertexTextureIndex = vertexData & 0x3;
  if (vertexTextureIndex == 3)
    fragmentTextureCoordinate = vertexTextureCoordinate / textureSize3;
  else if (vertexTextureIndex == 2)
    fragmentTextureCoordinate = vertexTextureCoordinate / textureSize2;
  else if (vertexTextureIndex == 1)
    fragmentTextureCoordinate = vertexTextureCoordinate / textureSize1;
  else
    fragmentTextureCoordinate = vertexTextureCoordinate / textureSize0;

  fragmentTextureIndex = vertexTextureIndex;
  fragmentColor = vertexColor;
}
)SHADER";

char const* DefaultFragmentShader = R"SHADER(
#version 150

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D texture3;

in vec2 fragmentTextureCoordinate;
flat in int fragmentTextureIndex;
in vec4 fragmentColor;

out vec4 outColor;

void main() {
  vec4 texColor;
  if (fragmentTextureIndex == 3)
    texColor = texture(texture3, fragmentTextureCoordinate);
  else if (fragmentTextureIndex == 2)
    texColor = texture(texture2, fragmentTextureCoordinate);
  else if (fragmentTextureIndex == 1)
    texColor = texture(texture1, fragmentTextureCoordinate);
  else
    texColor = texture(texture0, fragmentTextureCoordinate);

  if (texColor.a <= 0.0)
    discard;

  outColor = texColor * fragmentColor;
}
)SHADER";

/*
static void GLAPIENTRY GlMessageCallback(GLenum, GLenum type, GLuint, GLenum, GLsizei, const GLchar* message, const void* renderer) {
  if (type == GL_DEBUG_TYPE_ERROR) {
    Logger::error("GL ERROR: {}", message);
    __debugbreak();
  }
}
*/

extern EnumMap<BoolSettingMode> const BoolSettingModeNames{
  {BoolSettingMode::Enabled, "Enabled"},
  {BoolSettingMode::FromSetting, "FromSetting"},
  {BoolSettingMode::Disabled, "Disabled"}
};

bool settingModeValue(BoolSettingMode const& mode, bool const& setting) {
  if (mode == BoolSettingMode::FromSetting) {
    return setting;
  } else {
    return mode == BoolSettingMode::Enabled;
  }
}

// ---------------------------------------------------------------------------------------------------------
// GlTargets -- owns which render targets exist.

RefPtr<OpenGlRenderer::GlFrameBuffer> OpenGlRenderer::GlTargets::find(String const& id) const {
  if (auto ptr = m_byId.ptr(id))
    return *ptr;
  return {};
}

RefPtr<OpenGlRenderer::GlFrameBuffer> OpenGlRenderer::GlTargets::get(String const& id) const {
  if (auto ptr = m_byId.ptr(id))
    return *ptr;
  throw RendererException::format("Frame buffer '{}' does not exist", id);
}

bool OpenGlRenderer::GlTargets::has(String const& id) const {
  return m_byId.contains(id);
}

uint64_t OpenGlRenderer::GlTargets::generation() const {
  return m_generation;
}

void OpenGlRenderer::GlTargets::destroyAll() {
  // ONE act, not two. Every target's content becomes UNDEFINED here, and the generation is how a retained
  // (clear:false) surface finds that out -- its own refresh key (size, camera, counter) is unchanged across
  // the rebuild, so without this it would composite garbage and never know. Bumping it was a separate
  // statement the caller had to remember; now it cannot be forgotten, because it is the same event.
  ++m_generation;
  m_byId.clear();
}

void OpenGlRenderer::GlTargets::add(String const& name, Json const& config, Vec2U const& screenSize) {
  auto target = make_ref<GlFrameBuffer>(name, config, screenSize);
  // "double" (#542): give the surface its second face, so an effect can read what it writes. At CONFIG time --
  // switchEffectConfig used to allocate it mid-frame, on first use.
  if (config.getBool("double", false))
    target->makeDoubled();
  m_byId[name] = target;
}

void OpenGlRenderer::GlTargets::resizeAll(Vec2U const& screenSize) {
  for (auto& target : m_byId)
    target.second->resize(target.second->sizeFor(screenSize));
}

void OpenGlRenderer::GlTargets::clearAll() {
  for (auto& target : m_byId)
    if (target.second->clear)
      target.second->clearFaces();
}

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

  glClearColor(0.0, 0.0, 0.0, 1.0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  if (GLEW_VERSION_4_3) {
    //glEnable(GL_DEBUG_OUTPUT);
    //glDebugMessageCallback(GlMessageCallback, this);
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
  m_effects.destroyAll();

  m_targets.destroyAll();
  logGlErrorSummary("OpenGL errors during shutdown");
}

String OpenGlRenderer::rendererId() const {
  return "OpenGL20";
}

Vec2U OpenGlRenderer::screenSize() const {
  return m_screenSize;
}

// Every GL call in here is checked. GL reports an allocation failure (GL_OUT_OF_MEMORY) or a bad
// format/type combination (GL_INVALID_ENUM) ONLY through glGetError; left unchecked, the texture is simply
// never allocated and the failure resurfaces further down as an incomplete framebuffer -- whose message then
// names none of the things needed to act on it: which framebuffer, what size, what format, or which call
// actually failed.
// THE SIZE RULE, written once. Every hand-rolled copy of it dropped something: two dropped overrideSize
// entirely, and makeAlt's copy could give the second face a different size than the first.
Vec2U OpenGlRenderer::GlFrameBuffer::sizeFor(Vec2U const& screenSize) const {
  if (overrideSize)
    return *overrideSize;
  return Vec2U(screenSize[0] / sizeDiv, screenSize[1] / sizeDiv);
}

// What is ACTUALLY allocated. Both faces always agree (specifyStorage is the only writer, and resize()
// re-specifies every live face together), so face 0 speaks for the surface.
Vec2U OpenGlRenderer::GlFrameBuffer::size() const {
  return faces[0].texture ? faces[0].texture->textureSize : Vec2U(0, 0);
}

void OpenGlRenderer::GlFrameBuffer::specifyStorage(Face& face, Vec2U const& size, char const* which) {
  RefPtr<GlLoneTexture>& tex = face.texture;
  bool hdr = settingModeValue(hdrMode, config.getBool("hdrSetting", false));
  GLenum target = multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

  glBindTexture(target, tex->glTextureId());

  // glGetError pops from a queue that ACCUMULATES until drained, so an error still pending from earlier,
  // unrelated code would otherwise be blamed on the calls below. Start from a clean slate.
  while (glGetError() != GL_NO_ERROR) {}

  if (multisample) {
    auto internalFormat = hdr ? GL_RGBA16F : GL_RGBA8;
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, multisample, internalFormat, size[0], size[1], GL_TRUE);
  } else {
    auto format = alpha ? GL_RGBA : GL_RGB;
    auto internalFormat = hdr ?
        (alpha ? GL_RGBA16F : GL_RGB16F) :
        (alpha ? GL_RGBA8 : GL_RGB8);
    auto type = hdr ? GL_FLOAT : GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size[0], size[1], 0, format, type, NULL);
  }

  if (GLenum e = glGetError(); e != GL_NO_ERROR)
    throw RendererException::format(
        "Framebuffer '{}' ({}): {} failed with OpenGL error {:#06x} -- {}x{}, {}, alpha={}, {} samples",
        name, which, multisample ? "glTexImage2DMultisample" : "glTexImage2D", (unsigned)e,
        size[0], size[1], hdr ? "HDR (16F)" : "8-bit", alpha, multisample);

  // RECORD WHAT WE GOT. Only here, and only after the call that got it succeeded -- so size() can never
  // report a size we did not actually allocate. The mod-visible <name>Size uniform reads this; it used to
  // read the (0,0) it was born with, because nothing ever wrote it.
  tex->textureSize = size;
}

void OpenGlRenderer::GlFrameBuffer::allocateFace(Face& face, Vec2U const& size, char const* which) {
  RefPtr<GlLoneTexture>& tex = face.texture;
  GLuint& fboId = face.id;
  GLenum target = multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

  tex = make_ref<GlLoneTexture>();
  tex->textureFiltering = TextureFiltering::Nearest;
  tex->textureAddressing = TextureAddressing::Clamp;
  glGenTextures(1, &tex->textureId);
  if (tex->textureId == 0)
    throw RendererException::format("Framebuffer '{}' ({}): could not generate OpenGL texture", name, which);

  // The format rules and the storage call live in ONE place. This is not it.
  specifyStorage(face, size, which);

  // Sampling parameters belong to the texture OBJECT, not to its storage: they are set once, at creation,
  // and survive every resize. A multisample texture has none -- it cannot be sampled.
  if (!multisample) {
    while (glGetError() != GL_NO_ERROR) {}
    auto addressing = TextureAddressingNames.getLeft(config.getString("textureAddressing", "clamp"));
    auto filtering = TextureFilteringNames.getLeft(config.getString("textureFiltering", "nearest"));
    GLint wrap = addressing == TextureAddressing::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GLint filter = filtering == TextureFiltering::Nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
    if (GLenum e = glGetError(); e != GL_NO_ERROR)
      throw RendererException::format("Framebuffer '{}' ({}): glTexParameteri failed with OpenGL error {:#06x}",
          name, which, (unsigned)e);
  }

  fboId = 0;
  glGenFramebuffers(1, &fboId);
  if (!fboId)
    throw RendererException::format("Framebuffer '{}' ({}): could not create OpenGL framebuffer", name, which);

  glBindFramebuffer(GL_FRAMEBUFFER, fboId);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, tex->glTextureId(), 0);

  if (GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE)
    throw RendererException::format(
        "Framebuffer '{}' ({}) is not complete: status {:#06x} -- {}x{}, alpha={}, {} samples",
        name, which, (unsigned)status, size[0], size[1], alpha, multisample);
}

void OpenGlRenderer::GlFrameBuffer::resize(Vec2U const& newSize) {
  // Idempotent, and it must be: setScreenSize runs on every config reload, and re-specifying storage would
  // otherwise discard the contents of every retained (clear:false) surface for nothing.
  if (size() == newSize)
    return;

  for (unsigned i = 0; i < (doubled ? 2u : 1u); ++i)
    specifyStorage(faces[i], newSize, i == 0 ? "primary" : "second face");
}

void OpenGlRenderer::GlFrameBuffer::clearFaces() {
  for (unsigned i = 0; i < (doubled ? 2u : 1u); ++i) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, faces[i].id);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

OpenGlRenderer::GlFrameBuffer::GlFrameBuffer(String const& fbName, Json const& fbConfig, Vec2U const& screenSize)
  : config(fbConfig), name(fbName) {
  clear = config.getBool("clear",true);

  multisample = GLEW_VERSION_4_0 ? config.getUInt("multisample", 0) : 0;
  hdrMode = BoolSettingModeNames.getLeft(config.getString("hdr","Disabled"));
  alpha = config.getBool("alpha",false) || multisample;

  sizeDiv = config.getUInt("sizeDiv", 1);
  if (auto oSize = config.optArray("size"))
    overrideSize = jsonToVec2U(*oSize);

  // BORN CORRECT, at its real size. It used to be born 256x256 -- a placeholder that setScreenSize corrected
  // moments later, so in between, the surface reported a size it did not have. There is no two-phase
  // construction protocol left for a caller to get wrong, and no window in which size() lies.
  allocateFace(faces[0], sizeFor(screenSize), "primary");
}

void OpenGlRenderer::GlFrameBuffer::makeDoubled() {
  if (doubled)
    return;   // idempotent: a surface has at most two faces

  // MIRROR the first face's RECORDED size. It used to re-derive the size from screenSize/sizeDiv -- which is
  // how a doubled surface could be born with two faces of DIFFERENT sizes: the first at its overrideSize (or
  // at its 256x256 placeholder), the second at whatever the screen happened to be.
  allocateFace(faces[1], size(), "second face");
  doubled = true;
}

void OpenGlRenderer::GlFrameBuffer::swap() {
  if (!doubled)
    throw RendererException::format("Framebuffer '{}': swap() on a surface with only one face", name);

  write ^= 1;
  justSwapped = true;
}

OpenGlRenderer::GlFrameBuffer::~GlFrameBuffer() {
  // One loop, and the second face cannot be forgotten. It used to be, on every loadConfig -- i.e. on every
  // HDR or AA toggle -- because the destructor knew about `id` and not about `altId`.
  for (unsigned i = 0; i < (doubled ? 2u : 1u); ++i) {
    glDeleteFramebuffers(1, &faces[i].id);
    faces[i].texture.reset();
  }
}

void OpenGlRenderer::loadConfig(Json const& config) {
  // Every framebuffer below is destroyed and re-created with UNDEFINED content. Retained (clear:false)
  // surfaces have no other way to learn this -- their refresh keys (size/camera/counter) are unchanged
  // across the realloc -- so bump the generation and let them invalidate. Both setMainHDR and
  // setMultiSampling land here, and ClientApplication polls both client options every frame.
  m_targets.destroyAll();

  for (auto& pair : config.getObject("frameBuffers", {})) {
    Json config = pair.second;
    // Multisampling is OPT-IN per framebuffer, and only the target that is MSAA-RESOLVED to the screen
    // (glBlitFramebuffer, i.e. "main") may opt in. Any framebuffer that is SAMPLED AS A TEXTURE must stay
    // single-sample: a GL_TEXTURE_2D_MULTISAMPLE bound to a plain sampler2D reads as ZERO.
    //
    // This was forced onto EVERY framebuffer unconditionally, which meant that with antiAliasing on, the
    // GPU lightmap targets (lightingGpu / lightingGpuB / lightingGpuUpscaled) became multisample textures --
    // so the world shader's lightMap sampler read zero and THE ENTIRE WORLD RENDERED BLACK. Only the sky and
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

OpenGlRenderer::Effect* OpenGlRenderer::GlEffects::find(String const& name) {
  return m_byName.ptr(name);
}

void OpenGlRenderer::GlEffects::destroyAll() {
  for (auto& effect : m_byName)
    glDeleteProgram(effect.second.program);
  m_byName.clear();
}

void OpenGlRenderer::GlEffects::setScriptable(String const& effectName, String const& parameterName, RenderEffectParameter const& value) {
  auto effect = find(effectName);
  if (!effect)
    return;

  auto ptr = effect->scriptables.ptr(parameterName);
  if (!ptr || (ptr->parameterValue && *ptr->parameterValue == value))
    return;

  if (ptr->parameterType != value.typeIndex())
    throw RendererException::format("OpenGlRenderer::setEffectScriptableParameter '{}' parameter type mismatch", parameterName);

  // A CPU write, and ONLY a CPU write. The effect named here may not be the bound one, so there is no program
  // to issue a glUniform against. GlPass::bindEffect replays this the next time the effect is bound.
  ptr->parameterValue = value;
}

Maybe<RenderEffectParameter> OpenGlRenderer::GlEffects::getScriptable(String const& effectName, String const& parameterName) {
  auto effect = find(effectName);
  if (!effect)
    return {};
  auto ptr = effect->scriptables.ptr(parameterName);
  if (!ptr)
    return {};
  return ptr->parameterValue;
}

Maybe<VariantTypeIndex> OpenGlRenderer::GlEffects::getScriptableType(String const& effectName, String const& parameterName) {
  auto effect = find(effectName);
  if (!effect)
    return {};
  auto ptr = effect->scriptables.ptr(parameterName);
  if (!ptr)
    return {};
  return ptr->parameterType;
}

// Compile, link, register. It does NOT glUseProgram and it does NOT touch the pass: binding is GlPass's job,
// and loadEffectConfig does it on the very next line. That separation is the whole point -- registering an
// effect and making it the one you are drawing with are two different acts, and for as long as they were one
// function the second was an unannounced side effect of the first.
OpenGlRenderer::Effect& OpenGlRenderer::GlEffects::load(String const& name, Json const& effectConfig, StringMap<String> const& shaders) {
  GLint status = 0;
  char logBuffer[1024];

  // The second parameter is a LABEL and the third is the SOURCE. It used to be one parameter doing three
  // jobs -- a key into `shaders`, the source, and the error label -- and that is how the fallback below came
  // to compile nothing at all.
  auto compileStage = [&](GLenum type, char const* label, String const& source) -> GLuint {
    if (source.empty())
      return 0;   // this stage was not supplied; an effect may legitimately provide only one

    GLuint shader = glCreateShader(type);
    char const* sourcePtr = source.utf8Ptr();
    glShaderSource(shader, 1, &sourcePtr, NULL);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
      glGetShaderInfoLog(shader, sizeof(logBuffer), NULL, logBuffer);
      glDeleteShader(shader);   // the old lambda threw straight past this and leaked the handle
      throw RendererException(strf("Failed to compile {} shader of effect '{}': {}\n", label, name, logBuffer));
    }

    return shader;
  };

  auto sourceOf = [&](char const* key) -> String {
    auto* s = shaders.ptr(key);
    return s ? *s : String();
  };

  GLuint vertexShader = 0, fragmentShader = 0;
  try {
    vertexShader = compileStage(GL_VERTEX_SHADER, "vertex", sourceOf("vertex"));
    fragmentShader = compileStage(GL_FRAGMENT_SHADER, "fragment", sourceOf("fragment"));
  }
  catch (RendererException const& e) {
    // "using default" NOW USES THE DEFAULT. It used to pass DefaultVertexShader -- a char const* holding raw
    // GLSL -- as the lambda's `name`, which was a KEY into `shaders`. The lookup missed, the lambda returned
    // 0, nothing was attached, and this message announced a fallback that had not happened. Whether the empty
    // program then linked or threw was left to the driver.
    Logger::error("Shader compile error in effect '{}', falling back to the built-in default: {}", name, e.what());
    if (vertexShader) glDeleteShader(vertexShader);
    if (fragmentShader) glDeleteShader(fragmentShader);
    vertexShader = compileStage(GL_VERTEX_SHADER, "default vertex", DefaultVertexShader);
    fragmentShader = compileStage(GL_FRAGMENT_SHADER, "default fragment", DefaultFragmentShader);
  }

  GLuint program = glCreateProgram();

  if (vertexShader)
    glAttachShader(program, vertexShader);
  if (fragmentShader)
    glAttachShader(program, fragmentShader);
  glLinkProgram(program);

  if (vertexShader)
    glDeleteShader(vertexShader);
  if (fragmentShader)
    glDeleteShader(fragmentShader);

  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    glGetProgramInfoLog(program, sizeof(logBuffer), NULL, logBuffer);
    glDeleteProgram(program);
    throw RendererException(strf("Failed to link program for effect '{}': {}\n", name, logBuffer));
  }

  // NOTHING IS DESTROYED UNTIL THE NEW PROGRAM HAS LINKED. Every throw above leaves the registry exactly as
  // it was, so a failed reload costs an error in the log and nothing else -- the old effect keeps rendering.
  //
  // The delete and the erase used to run FIRST, before a single line of the new program had been compiled. A
  // link failure then left the effect PERMANENTLY ABSENT: its program deleted, its entry gone, and
  // switchEffectConfig(name) returning false forever after -- a return value that StarClientApplication
  // IGNORES for both "world" and "interface", so those layers would silently keep drawing under whatever
  // effect happened to be bound last. One bad shader in a mod, and the game renders wrong rather than saying
  // so.
  if (auto existing = m_byName.ptr(name)) {
    Logger::info("Reloading OpenGL effect {}", name);
    glDeleteProgram(existing->program);
    m_byName.erase(name);
  }

  auto& effect = m_byName.emplace(name, Effect()).first->second;
  effect.program = program;
  effect.config = effectConfig;
  effect.includeVBTextures = effectConfig.getBool("includeVBTextures", true);
  return effect;
}

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
      if (type == "bool") {
        effectParameter.parameterType = RenderEffectParameter::typeIndexOf<bool>();
      } else if (type == "int") {
        effectParameter.parameterType = RenderEffectParameter::typeIndexOf<int>();
      } else if (type == "float") {
        effectParameter.parameterType = RenderEffectParameter::typeIndexOf<float>();
      } else if (type == "vec2") {
        effectParameter.parameterType = RenderEffectParameter::typeIndexOf<Vec2F>();
      } else if (type == "vec3") {
        effectParameter.parameterType = RenderEffectParameter::typeIndexOf<Vec3F>();
      } else if (type == "vec4") {
        effectParameter.parameterType = RenderEffectParameter::typeIndexOf<Vec4F>();
      } else {
        throw RendererException::format("Unrecognized effect parameter type '{}'", type);
      }

      if (p.second.getBool("scriptable",false)) {
        if (Json def = p.second.get("default", {})) {
          if (type == "bool") {
            effectParameter.parameterValue = (RenderEffectParameter)def.toBool();
          } else if (type == "int") {
            effectParameter.parameterValue = (RenderEffectParameter)(int)def.toInt();
          } else if (type == "float") {
            effectParameter.parameterValue = (RenderEffectParameter)def.toFloat();
          } else if (type == "vec2") {
            effectParameter.parameterValue = (RenderEffectParameter)jsonToVec2F(def);
          } else if (type == "vec3") {
            effectParameter.parameterValue = (RenderEffectParameter)jsonToVec3F(def);
          } else if (type == "vec4") {
            effectParameter.parameterValue = (RenderEffectParameter)jsonToVec4F(def);
          }
        }
        effect.scriptables[p.first] = effectParameter;
      } else {
        effect.parameters[p.first] = effectParameter;
        if (Json def = p.second.get("default", {})) {
          if (type == "bool") {
            setEffectParameter(p.first, def.toBool());
          } else if (type == "int") {
            setEffectParameter(p.first, (int)def.toInt());
          } else if (type == "float") {
            setEffectParameter(p.first, def.toFloat());
          } else if (type == "vec2") {
            setEffectParameter(p.first, jsonToVec2F(def));
          } else if (type == "vec3") {
            setEffectParameter(p.first, jsonToVec3F(def));
          } else if (type == "vec4") {
            setEffectParameter(p.first, jsonToVec4F(def));
          }
        }
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
  
  if (auto outFrameBufferId = effect.config.optString("frameBuffer")) {
    if (auto blitFrameBufferId = effect.config.optString("blitFrameBuffer")) {
      if ((*outFrameBufferId).equals((*blitFrameBufferId))) {
        effect.doubleBuffered = true;
      }
    }
    if (!effect.doubleBuffered) {
      if (auto fbts = effect.config.optArray("frameBufferTextures")) {
        for (auto const& fbt : *fbts) {
          if (auto inFrameBufferId = fbt.optString("framebuffer")) {
            if ((*outFrameBufferId).equals((*inFrameBufferId))) {
              effect.doubleBuffered = true;
              break;
            }
          }
        }
      }
    }
  }

  if (DebugEnabled)
    logGlErrorSummary("OpenGL errors setting effect config");
}

// The one place a RenderEffectParameter becomes a glUniform call. It was written out twice, character for
// character -- here and in the scriptable replay at bind -- which is one variant ladder to keep in step every
// time RenderEffectParameter gains a type. It is a free function, not a method: it needs no effect, no pass and
// no registry, only a location and a value. Hanging it on a component would be the "and also uploads uniforms"
// clause that the Law of One forbids.
//
// It writes into whatever program is CURRENTLY BOUND. That is not a defect of this function -- it is how
// glUniform works -- but it means every caller owes the reader a reason why the right program is bound.
static void uploadUniform(GLint location, RenderEffectParameter const& value) {
  if (auto v = value.ptr<bool>())
    glUniform1i(location, *v);
  else if (auto v = value.ptr<int>())
    glUniform1i(location, *v);
  else if (auto v = value.ptr<float>())
    glUniform1f(location, *v);
  else if (auto v = value.ptr<Vec2F>())
    glUniform2f(location, (*v)[0], (*v)[1]);
  else if (auto v = value.ptr<Vec3F>())
    glUniform3f(location, (*v)[0], (*v)[1], (*v)[2]);
  else if (auto v = value.ptr<Vec4F>())
    glUniform4f(location, (*v)[0], (*v)[1], (*v)[2], (*v)[3]);
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
  auto ptr = m_pass.effect->parameters.ptr(parameterName);
  if (!ptr)
    return;
  applyEffectParameter(ptr, value, parameterName);
}

OpenGlRenderer::EffectParameterHandle OpenGlRenderer::getEffectParameterHandle(String const& parameterName) {
  return (EffectParameterHandle)m_pass.effect->parameters.ptr(parameterName);
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
  auto ptr = m_pass.effect->textures.ptr(textureName);
  if (!ptr)
    return;

  flushImmediatePrimitives();

  // AN UPLOAD SETTER OWNS THE TEXTURE IT UPLOADS INTO. If this sampler is currently BORROWING a framebuffer's
  // colour attachment, we take the fresh-allocation branch: we do not own that storage and must not touch it.
  //
  // Without the targetOwned test the else-branch below did all three of the things GlFrameBuffer::specifyStorage
  // declares itself the only owner of -- bind the target's texture, glTexImage2D a whole new storage spec into
  // it, and overwrite the textureSize record that GlFrameBuffer::size() reads -- from outside, through an
  // aliased RefPtr. It is reachable in ordinary play: the world effect's `lightMap` sampler is pointed at
  // lightingGpuUpscaled's face by the GPU lighting pass, and then fullbright (StarWorldPainter.cpp) uploads a
  // 1x1 white image into that same sampler, re-specifying a live render target to a 1x1 RGB8.
  //
  // It LOOKED harmless because it self-heals: the next setRenderTarget calls resize(), which sees the wrong
  // size and re-specifies. But resize() compares SIZE ONLY -- `if (size() == newSize) return;` -- against the
  // record this corruption also rewrites. It heals only because the corruption is self-reporting. An upload at
  // the SAME size and a DIFFERENT format is never noticed, and that internal format is wrong for the life of
  // the process.
  if (!ptr->textureValue || ptr->textureValue->textureId == 0 || ptr->targetOwned) {
    ptr->textureValue = createGlTexture(image, ptr->textureAddressing, ptr->textureFiltering);
    ptr->targetOwned = false;
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->textureValue->textureId);
    ptr->textureValue->textureSize = image.size;
    uploadTextureImage(image.format, image.size, image.data);
  }

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
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
  if (m_pass.effect == &effect && !effect.doubleBuffered)
    return true;

  auto effectScreenSize = m_screenSize;
  
  auto outFrameBufferId = effect.config.optString("frameBuffer");
  if (outFrameBufferId) {
    auto buf = m_targets.get(*outFrameBufferId);
    effectScreenSize = m_screenSize / (buf->sizeDiv);
    if (effect.doubleBuffered) {
      if (!buf->doubled) {
        // Allocating a framebuffer MID-FRAME hitches. Say so, and fix it in the config rather than here.
        Logger::warn("Effect '{}' reads the framebuffer '{}' that it writes, but that framebuffer is not "
                     "declared \"double\":true -- giving it a second face now, mid-frame.", name, *outFrameBufferId);
        buf->makeDoubled();
      }
      buf->swap();
    }
    bindTarget(buf);
  } else {
    m_pass.target.reset();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  }

  m_pass.bindEffect(effect, effectScreenSize);

  setEffectParameter("vertexRounding", m_multiSampling > 0);
  if (auto fbts = effect.config.optArray("frameBufferTextures")) {
    for (auto const& fbt : *fbts) {
      if (auto frameBufferId = fbt.optString("framebuffer")) {
        auto textureUniform = fbt.getString("texture");
        auto ptr = m_pass.effect->textures.ptr(textureUniform);
        if (ptr) {
          auto undefined = !ptr->textureValue || ptr->textureValue->textureId == 0;
          auto swapped = effect.doubleBuffered && (*frameBufferId).equals(*outFrameBufferId);
          auto buf = m_targets.get(*frameBufferId);
          if (undefined || buf->doubled) {
            // `swapped` means this effect is sampling the very surface it is drawing into: it must read the
            // face it is NOT writing. That is exactly what readFace() answers, so ask it.
            ptr->textureValue = swapped ? buf->readFace().texture : buf->writeFace().texture;
            ptr->targetOwned = true;   // borrowed from GlTargets; the upload setters must not write to it
            if (ptr->textureSizeUniform != -1 && undefined) {
              auto textureSize = ptr->textureValue->glTextureSize();
              glUniform2f(ptr->textureSizeUniform, textureSize[0], textureSize[1]);
            }
          }
        }
      }
    }
  }
  
  if (auto blitFrameBufferId = effect.config.optString("blitFrameBuffer"))
    blitGlFrameBuffer(m_targets.get(*blitFrameBufferId), effect.doubleBuffered);
  
  return true;
}

void OpenGlRenderer::setRenderTarget(Maybe<String> const& frameBufferId, Vec2U size) {
  flushImmediatePrimitives();

  if (!frameBufferId) {
    // Restore the screen as the draw target and the full-screen viewport/screenSize.
    m_pass.target.reset();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenSize[0], m_screenSize[1]);
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

  // THIS IS NOT REDUNDANT WITH bindTarget, and I nearly deleted it as such. bindTarget early-outs when the
  // target is ALREADY BOUND -- and it early-outs BEFORE setting the viewport. So a caller that re-targets the
  // SAME surface at a NEW size (which the lighting passes do every frame) would keep the previous viewport.
  // The early-out is skipping work it should not skip; the honest fix is to key the bind cache on (target,
  // size) as well as identity, and that is a behaviour change for F2b. Until then this line covers for it.
  Vec2U vp = (size[0] != 0 && size[1] != 0) ? size : buf->writeFace().texture->textureSize;
  glViewport(0, 0, vp[0], vp[1]);
  if (m_pass.screenSizeUniform != -1)
    glUniform2f(m_pass.screenSizeUniform, (float)vp[0], (float)vp[1]);
}

void OpenGlRenderer::clearRenderTarget(Vec4F clearColor) {
  // Flush pending immediate primitives first so they aren't wiped by the clear. Clears the currently
  // bound GL_DRAW_FRAMEBUFFER (set by setRenderTarget -> switchGlFrameBuffer). Default clearColor
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
  auto ptr = m_pass.effect->textures.ptr(textureName);
  if (!ptr)
    return;

  flushImmediatePrimitives();

  // Bind the framebuffer's color texture (a GlLoneTexture, same type setEffectTexture produces)
  // directly to the sampler -- no CPU upload.
  ptr->textureValue = m_targets.get(frameBufferId)->writeFace().texture;
  ptr->targetOwned = true;   // borrowed from GlTargets; the upload setters must not write to it
  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureAlias(String const& destTextureName, String const& sourceTextureName) {
  auto dest = m_pass.effect->textures.ptr(destTextureName);
  auto src = m_pass.effect->textures.ptr(sourceTextureName);
  if (!dest || !src || !src->textureValue)
    return;

  flushImmediatePrimitives();

  // Share the source sampler's already-uploaded texture (same GlLoneTexture, ref-counted) with the
  // dest sampler -- the per-draw bind loop will bind it to dest's texture unit. No CPU upload.
  dest->textureValue = src->textureValue;
  dest->targetOwned = src->targetOwned;   // an alias of a borrowed texture is still borrowed
  if (dest->textureSizeUniform != -1) {
    auto textureSize = dest->textureValue->glTextureSize();
    glUniform2f(dest->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureHalf(String const& textureName, Vec2U size, uint16_t const* halfData, unsigned channels) {
  auto ptr = m_pass.effect->textures.ptr(textureName);
  if (!ptr || size[0] == 0 || size[1] == 0)
    return;

  flushImmediatePrimitives();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool fresh = false;
  // targetOwned: this sampler is BORROWING a framebuffer's colour attachment. Allocate our own storage rather
  // than glTexSubImage2D pixels -- or glTexImage2D a whole new spec -- into a live render target.
  if (!ptr->textureValue || ptr->textureValue->textureId == 0 || ptr->targetOwned) {
    auto tex = make_ref<GlLoneTexture>();
    tex->textureFiltering = ptr->textureFiltering;
    tex->textureAddressing = ptr->textureAddressing;
    tex->textureSize = size;
    glGenTextures(1, &tex->textureId);
    glBindTexture(GL_TEXTURE_2D, tex->textureId);
    GLenum wrap = ptr->textureAddressing == TextureAddressing::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    GLenum filt = ptr->textureFiltering == TextureFiltering::Nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    ptr->textureValue = tex;
    ptr->targetOwned = false;
    fresh = true;
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->textureValue->textureId);
  }
  // 16F storage + GL_HALF_FLOAT source: half the bytes of the float upload, no precision loss (the FBOs are
  // 16F anyway). channels==4 packs a fourth component -- used to carry the obstacle flag alongside emission,
  // so the spread shader reads light and obstacle in ONE tap instead of two samplers.
  // Same-size re-upload goes through TexSubImage into the EXISTING storage: the old unconditional
  // glTexImage2D re-spec allocated a fresh driver buffer object per upload at the lighting cadence
  // (measured ~30% of the kernel texture cluster, #127). Size changes (zoom/resolution) still re-spec.
  GLenum const format = channels == 4 ? GL_RGBA : GL_RGB;
  GLint  const internalFormat = channels == 4 ? GL_RGBA16F : GL_RGB16F;
  if (!fresh && ptr->textureValue->textureSize == size && ptr->textureValue->uploadChannels == channels) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size[0], size[1], format, GL_HALF_FLOAT, halfData);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size[0], size[1], 0, format, GL_HALF_FLOAT, halfData);
    ptr->textureValue->textureSize = size;
    ptr->textureValue->uploadChannels = channels;
  }

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureR8(String const& textureName, Vec2U size, uint8_t const* data) {
  auto ptr = m_pass.effect->textures.ptr(textureName);
  if (!ptr || size[0] == 0 || size[1] == 0)
    return;

  flushImmediatePrimitives();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool fresh = false;
  // targetOwned: this sampler is BORROWING a framebuffer's colour attachment. Allocate our own storage rather
  // than glTexSubImage2D pixels -- or glTexImage2D a whole new spec -- into a live render target.
  if (!ptr->textureValue || ptr->textureValue->textureId == 0 || ptr->targetOwned) {
    auto tex = make_ref<GlLoneTexture>();
    tex->textureFiltering = ptr->textureFiltering;
    tex->textureAddressing = ptr->textureAddressing;
    tex->textureSize = size;
    glGenTextures(1, &tex->textureId);
    glBindTexture(GL_TEXTURE_2D, tex->textureId);
    GLenum wrap = ptr->textureAddressing == TextureAddressing::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    GLenum filt = ptr->textureFiltering == TextureFiltering::Nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    ptr->textureValue = tex;
    ptr->targetOwned = false;
    fresh = true;
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->textureValue->textureId);
  }
  // R8 storage + GL_RED source: a third the bytes of RGB24 for the binary obstacle mask (read as .r).
  // Same-size re-upload goes through TexSubImage (see setEffectTextureHalfRGB above) -- this runs twice
  // per recompute (the spread and point effects each own an "obstacle" sampler), so both duplicate
  // uploads become SubImages into persistent storage instead of fresh-BO re-specs.
  if (!fresh && ptr->textureValue->textureSize == size) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size[0], size[1], GL_RED, GL_UNSIGNED_BYTE, data);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size[0], size[1], 0, GL_RED, GL_UNSIGNED_BYTE, data);
    ptr->textureValue->textureSize = size;
  }

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
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
  Vec2U size = buf->writeFace().texture->textureSize;
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
  auto current = m_renderer.m_pass.target;
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
  auto current = m_renderer.m_pass.target;
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
  if (m_multiSampling) {
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_SAMPLE_SHADING);
    glMinSampleShading(1.f);
  } else {
    glMinSampleShading(0.f);
    glDisable(GL_SAMPLE_SHADING);
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

void OpenGlRenderer::GlGpuTimer::begin(String const& name) {
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
  // NESTING GUARD. GL_TIME_ELAPSED queries CANNOT nest: a glBeginQuery while one is active is
  // GL_INVALID_OPERATION, the inner begin is dropped, and the inner END then closes the OUTER query -- silently
  // darkening both. This bit immediately: the blit timer fires INSIDE the interface timer (blitGlFrameBuffer is
  // reached during the interface render), and the resulting numbers were nonsense.
  if (m_active) {
    Logger::warn("GpuTimer::begin('{}') nested inside an active timer -- ignored (GL_TIME_ELAPSED cannot nest)", name);
    return;
  }
  // Submit any pending primitives first so the query measures only the work that follows.
  m_flushPending();
  auto& ring = m_rings[name];
  unsigned slot = ring.writeIdx;
  if (ring.queries[slot] == 0)
    glGenQueries(1, &ring.queries[slot]);
  else if (ring.issued[slot]) {
    // This slot last ran 3 frames ago; its result should be ready. Read it without stalling.
    GLuint available = 0;
    glGetQueryObjectuiv(ring.queries[slot], GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
      GLuint64 elapsedNs = 0;
      glGetQueryObjectui64v(ring.queries[slot], GL_QUERY_RESULT, &elapsedNs);
      Telemetry::timer(name).record((int64_t)(elapsedNs / 1000));
      m_lastMicros[name] = (int64_t)(elapsedNs / 1000);
    }
    ring.issued[slot] = false; // reuse the query object regardless (drops a rare not-ready sample)
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
  m_current->writeIdx = (m_slot + 1) % 3;
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
      // This slot was issued 3 frames ago; read it back without stalling the pipeline.
      GLuint availB = 0, availE = 0;
      glGetQueryObjectuiv(ring.begins[slot], GL_QUERY_RESULT_AVAILABLE, &availB);
      glGetQueryObjectuiv(ring.ends[slot], GL_QUERY_RESULT_AVAILABLE, &availE);
      if (availB && availE) {
        GLuint64 t0 = 0, t1 = 0;
        glGetQueryObjectui64v(ring.begins[slot], GL_QUERY_RESULT, &t0);
        glGetQueryObjectui64v(ring.ends[slot], GL_QUERY_RESULT, &t1);
        if (t1 > t0)
          Telemetry::timer("render.frame.gpu_span_us").record((int64_t)((t1 - t0) / 1000));
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
  m_gpuTimer.begin("render.frame.clear.gpu_us");

  // The REGISTRY clears its targets; each surface clears its own faces. Nobody reaches for faces[N].id.
  m_targets.clearAll();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glClear(GL_COLOR_BUFFER_BIT);

  m_gpuTimer.end("render.frame.clear.gpu_us");

  if (m_scissorRect)
    glEnable(GL_SCISSOR_TEST);
}

void OpenGlRenderer::finishFrame() {
  flushImmediatePrimitives();
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

  if (DebugEnabled)
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

OpenGlRenderer::GlLoneTexture::~GlLoneTexture() {
  if (textureId != 0)
    glDeleteTextures(1, &textureId);
}

Vec2U OpenGlRenderer::GlLoneTexture::size() const {
  return textureSize;
}

TextureFiltering OpenGlRenderer::GlLoneTexture::filtering() const {
  return textureFiltering;
}

TextureAddressing OpenGlRenderer::GlLoneTexture::addressing() const {
  return textureAddressing;
}

GLuint OpenGlRenderer::GlLoneTexture::glTextureId() const {
  return textureId;
}

Vec2U OpenGlRenderer::GlLoneTexture::glTextureSize() const {
  return textureSize;
}

Vec2U OpenGlRenderer::GlLoneTexture::glTextureCoordinateOffset() const {
  return Vec2U();
}

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
          if (!NoVboOrphan)
            glBufferData(GL_ARRAY_BUFFER, vb.byteCapacity, nullptr, GL_STREAM_DRAW);
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
  if (GLenum error = glGetError()) {
    Logger::error("{}: ", prefix);
    do {
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
    return true;
  }
  return false;
}

void OpenGlRenderer::uploadTextureImage(PixelFormat pixelFormat, Vec2U size, uint8_t const* data) {
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

  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat.value(format), size[0], size[1], 0, format, type, data);
}

void OpenGlRenderer::flushImmediatePrimitives(Mat3F const& transformation) {
  if (m_immediatePrimitives.empty())
    return;

  // Task #141: every flush re-writes the SINGLE shared immediate VBO (glBufferSubData / glBufferData below)
  // and then draws from it. If the GPU is still reading that buffer from the previous flush, the write forces
  // an IMPLICIT SYNCHRONISATION -- a full pipeline stall. Widget::render -> setupDrawRegion -> setScissorRect
  // calls this for EVERY widget, and the in-game HUD has ~92 of them. Count them, and count the primitives per
  // flush: a high flush count with a tiny primitive count is the signature of stall-per-widget.
  static auto flushes = Telemetry::counter("render.flush.count");
  static auto flushPrims = Telemetry::counter("render.flush.primitives");
  flushes.inc(1);
  flushPrims.inc(m_immediatePrimitives.size());

  m_immediateRenderBuffer->set(m_immediatePrimitives);
  m_immediatePrimitives.resize(0);
  renderGlBuffer(*m_immediateRenderBuffer, transformation);
}

auto OpenGlRenderer::createGlTexture(ImageView const& image, TextureAddressing addressing, TextureFiltering filtering)
    ->RefPtr<GlLoneTexture> {
  auto glLoneTexture = make_ref<GlLoneTexture>();
  glLoneTexture->textureFiltering = filtering;
  glLoneTexture->textureAddressing = addressing;
  glLoneTexture->textureSize = image.size;

  glGenTextures(1, &glLoneTexture->textureId);
  if (glLoneTexture->textureId == 0)
    throw RendererException("Could not generate texture in OpenGlRenderer::createGlTexture");

  glBindTexture(GL_TEXTURE_2D, glLoneTexture->textureId);

  if (addressing == TextureAddressing::Clamp) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }

  if (filtering == TextureFiltering::Nearest) {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  } else {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }


  if (!image.empty())
    uploadTextureImage(image.format, image.size, image.data);

  return glLoneTexture;
}

auto OpenGlRenderer::createGlRenderBuffer() -> shared_ptr<GlRenderBuffer> {
  auto glrb = make_shared<GlRenderBuffer>();
  glrb->whiteTexture = m_whiteTexture;
  glrb->useMultiTexturing = m_useMultiTexturing;
  return glrb;
}

void OpenGlRenderer::renderGlBuffer(GlRenderBuffer const& renderBuffer, Mat3F const& transformation) {
  for (auto const& vb : renderBuffer.vertexBuffers) {
    glUniformMatrix3fv(m_pass.vertexTransformUniform, 1, GL_TRUE, transformation.ptr());

    if (m_pass.effect->includeVBTextures) {
      for (size_t i = 0; i < vb.textures.size(); ++i) {
        glUniform2f(m_pass.textureSizeUniforms[i], vb.textures[i].size[0], vb.textures[i].size[1]);
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, vb.textures[i].texture);
      }
    }

    for (auto const& p : m_pass.effect->textures) {
      if (p.second.textureValue) {
        glActiveTexture(GL_TEXTURE0 + p.second.textureUnit);
        glBindTexture(GL_TEXTURE_2D, p.second.textureValue->textureId);

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

    glEnableVertexAttribArray(m_pass.positionAttribute);
    glEnableVertexAttribArray(m_pass.texCoordAttribute);
    glEnableVertexAttribArray(m_pass.colorAttribute);
    glEnableVertexAttribArray(m_pass.dataAttribute);

    glVertexAttribPointer(m_pass.positionAttribute, 2, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, pos));
    glVertexAttribPointer(m_pass.texCoordAttribute, 2, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, uv));
    glVertexAttribPointer(m_pass.colorAttribute, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, color));
    glVertexAttribIPointer(m_pass.dataAttribute, 1, GL_INT, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, pack));

    glDrawArrays(GL_TRIANGLES, 0, vb.vertexCount);
  }
}

//Assumes the passed effect program is currently in use.
void OpenGlRenderer::GlPass::bindEffect(Effect& newEffect, Vec2U const& screenSize) {
  Effect& effect = newEffect;
  glUseProgram(effect.program);
  this->effect = &effect;

  positionAttribute = effect.getAttribute("vertexPosition");
  colorAttribute = effect.getAttribute("vertexColor");
  texCoordAttribute = effect.getAttribute("vertexTextureCoordinate");
  dataAttribute = effect.getAttribute("vertexData");

  // textureUniforms used to be cached here alongside textureSizeUniforms. It was a cache of a cache --
  // Effect::getUniform already memoizes the location -- and unlike textureSizeUniforms (read per draw, at
  // :1900) it never escaped this function: filled in one loop, read in the next, never again. The sampler-unit
  // binding it performs lives in the PROGRAM once set, so there is nothing to remember.
  textureSizeUniforms.clear();
  if (effect.includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i)
      textureSizeUniforms.append(effect.getUniform(strf("textureSize{}", i).c_str()));
  }
  screenSizeUniform = effect.getUniform("screenSize");
  vertexTransformUniform = effect.getUniform("vertexTransform");

  if (effect.includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i)
      glUniform1i(effect.getUniform(strf("texture{}", i).c_str()), i);
  }

  glUniform2f(screenSizeUniform, screenSize[0], screenSize[1]);

  // Scriptable parameters live on the CPU until a bind. This is that bind: the only path by which a value a
  // script set on an unbound effect reaches the GPU.
  //
  // It does NOT go through applyEffectParameter, and must not. That function's job is to skip a redundant
  // upload by comparing against the value it last uploaded -- but setEffectScriptableParameter has ALREADY
  // written parameterValue, so the comparison would match on every single scriptable and elide every upload,
  // silently and permanently. Same ladder, different contract.
  for (auto& param : effect.scriptables) {
    if (param.second.parameterValue)
      uploadUniform(param.second.parameterUniform, *param.second.parameterValue);
  }
}

// Copies `frameBuffer` (or its alt half, when the calling effect is double-buffered) into whatever draw target
// is currently bound -- switchGlFrameBuffer binds it, and switchEffectConfig binds the screen (0) for an
// effect with no render target, which is how "main" reaches the display.
//
// NOTE the missing once-per-frame guard. Upstream deliberately dropped the `if (blitted) return;` that vanilla
// had, because with double-buffering (#542) the alt->primary copy must happen on EVERY effect switch, not once
// per frame -- the guard would silently starve the feature. We keep upstream's semantics exactly and only wrap
// them in the timer: at 2560x1440 RGBA16F, MSAA-resolving when antiAliasing is on, this blit is not free, and
// it was part of the 1.8-3.5ms/frame the whole-frame span proved was unaccounted for (task #141).
void OpenGlRenderer::blitGlFrameBuffer(RefPtr<GlFrameBuffer> const& frameBuffer, bool const& useAlt) {
  m_gpuTimer.begin("render.frame.blit.gpu_us");

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
void OpenGlRenderer::bindTarget(RefPtr<GlFrameBuffer> const& frameBuffer) {
  m_pass.bindTarget(frameBuffer, m_screenSize);
}

void OpenGlRenderer::GlPass::bindTarget(RefPtr<GlFrameBuffer> const& newTarget, Vec2U const& screenSize) {
  // justSwapped defeats this early-out: a swap changes which FACE is the write face without changing the
  // TARGET, so "already bound" would otherwise skip the rebind and the pass would keep drawing into the face
  // it just stopped writing. (The honest fix is a (target, face) bind key, which retires this bool -- but
  // that is a behaviour change and it belongs in F2b, with its own gate. It stays, verbatim, for now.)
  if (target == newTarget && !newTarget->justSwapped)
    return;

  newTarget->justSwapped = false;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, newTarget->writeFace().id);
  target = newTarget;

  // THE bind path, so this happens here and cannot be forgotten anywhere else. Nothing used to set the
  // viewport when an effect switched to a framebuffer, so an effect declaring `frameBuffer` together with a
  // `sizeDiv` drew into a smaller surface through a stale full-screen viewport -- silently, and only for
  // mods, since nothing in-tree ships a sizeDiv surface.
  Vec2U vp = newTarget->writeFace().texture->glTextureSize();
  if (vp[0] == 0 || vp[1] == 0)
    vp = screenSize / newTarget->sizeDiv;
  glViewport(0, 0, vp[0], vp[1]);

  // A `glUniform2f(screenSizeUniform, vp)` used to close this function, and a comment above it called that
  // statement "THE COUPLING" -- a target operation writing an effect-program uniform, proof that targets and
  // effects could not be split. It was the stated reason this component exists.
  //
  // It was dead at both call sites, and I never checked.
  //
  //   switchEffectConfig: bindTarget runs BEFORE glUseProgram. glUniform writes into the program that is
  //     CURRENTLY bound -- the OUTGOING one -- through the OUTGOING program's cached location. The incoming
  //     program then gets its screenSize from bindEffect a few lines later. The write landed on the wrong
  //     program and was overwritten the next time that program was bound.
  //   setRenderTarget: the very next statements re-write the viewport and the same uniform with the same
  //     value, because bindTarget's early-out means the caller cannot rely on either happening here.
  //
  // So it wrote to the wrong program, or it wrote a value that was immediately rewritten. No draw could ever
  // observe it. The viewport call above is real and load-bearing; that one was ceremony defending an argument.
  //
  // GlPass still earns its keep -- it is the coupled (effect, target) pair and the flattened locations the
  // draw path reads -- but on THAT, honestly, and not on a uniform write no frame could see.
}

GLuint OpenGlRenderer::Effect::getAttribute(String const& name) {
  auto find = attributes.find(name);
  if (find == attributes.end()) {
    GLuint attrib = glGetAttribLocation(program, name.utf8Ptr());
    attributes[name] = attrib;
    return attrib;
  }
  return find->second;
}

GLuint OpenGlRenderer::Effect::getUniform(String const& name) {
  auto find = uniforms.find(name);
  if (find == uniforms.end()) {
    GLuint uniform = glGetUniformLocation(program, name.utf8Ptr());
    uniforms[name] = uniform;
    return uniform;
  }
  return find->second;
}


}
