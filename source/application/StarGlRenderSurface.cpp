#include "StarGlRenderSurface.hpp"

#include "StarJsonExtra.hpp"
#include "StarLogging.hpp"

namespace Star {

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

RefPtr<GlFrameBuffer> GlTargets::find(String const& id) const {
  if (auto ptr = m_byId.ptr(id))
    return *ptr;
  return {};
}

RefPtr<GlFrameBuffer> GlTargets::get(String const& id) const {
  if (auto ptr = m_byId.ptr(id))
    return *ptr;
  throw RendererException::format("Frame buffer '{}' does not exist", id);
}

bool GlTargets::has(String const& id) const {
  return m_byId.contains(id);
}

uint64_t GlTargets::generation() const {
  return m_generation;
}

void GlTargets::destroyAll() {
  // ONE act, not two. Every target's content becomes UNDEFINED here, and the generation is how a retained
  // (clear:false) surface finds that out -- its own refresh key (size, camera, counter) is unchanged across
  // the rebuild, so without this it would composite garbage and never know. Bumping it was a separate
  // statement the caller had to remember; now it cannot be forgotten, because it is the same event.
  ++m_generation;
  m_byId.clear();
}

void GlTargets::add(String const& name, Json const& config, Vec2U const& screenSize) {
  auto target = make_ref<GlFrameBuffer>(name, config, screenSize);
  // "double" (#542): give the surface its second face, so an effect can read what it writes. At CONFIG time --
  // switchEffectConfig used to allocate it mid-frame, on first use.
  if (config.getBool("double", false))
    target->makeDoubled();
  m_byId[name] = target;
}

void GlTargets::resizeAll(Vec2U const& screenSize) {
  for (auto& target : m_byId)
    target.second->resize(target.second->sizeFor(screenSize));
}

void GlTargets::clearAll() {
  for (auto& target : m_byId)
    if (target.second->clear)
      target.second->clearFaces();
}

// Every GL call in here is checked. GL reports an allocation failure (GL_OUT_OF_MEMORY) or a bad
// format/type combination (GL_INVALID_ENUM) ONLY through glGetError; left unchecked, the texture is simply
// never allocated and the failure resurfaces further down as an incomplete framebuffer -- whose message then
// names none of the things needed to act on it: which framebuffer, what size, what format, or which call
// actually failed.
// THE SIZE RULE, written once. Every hand-rolled copy of it dropped something: two dropped overrideSize
// entirely, and makeAlt's copy could give the second face a different size than the first.
Vec2U GlFrameBuffer::sizeFor(Vec2U const& screenSize) const {
  if (overrideSize)
    return *overrideSize;
  return Vec2U(screenSize[0] / sizeDiv, screenSize[1] / sizeDiv);
}

// What is ACTUALLY allocated. Both faces always agree (specifyStorage is the only writer, and resize()
// re-specifies every live face together), so the front face speaks for the surface.
Vec2U GlFrameBuffer::size() const {
  return front.texture ? front.texture->textureSize : Vec2U(0, 0);
}

void GlFrameBuffer::specifyStorage(Face& face, Vec2U const& size, char const* which) {
  RefPtr<GlLoneTexture>& tex = face.texture;
  bool hdr = settingModeValue(hdrMode, config.getBool("hdrSetting", false));
  GLenum target = multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

  glBindTexture(target, tex->glTextureId());

  // glGetError pops from a queue that ACCUMULATES until drained, so an error still pending from earlier,
  // unrelated code would otherwise be blamed on the calls below. Start from a clean slate.
  while (glGetError() != GL_NO_ERROR) {}

  GLint internalFormat = 0;
  if (multisample) {
    internalFormat = hdr ? GL_RGBA16F : GL_RGBA8;
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, multisample, internalFormat, size[0], size[1], GL_TRUE);
  } else {
    auto format = alpha ? GL_RGBA : GL_RGB;
    internalFormat = hdr ?
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

  // RECORD WHAT WE GOT -- ALL of it. Only here, and only after the call that got it succeeded, so size() can
  // never report a size we did not actually allocate. The mod-visible <name>Size uniform reads this; it used
  // to read the (0,0) it was born with, because nothing ever wrote it.
  //
  // The FORMAT is half the descriptor and it is recorded for the same reason: a texture that cannot say what
  // format it is cannot be safely sub-uploaded into, and this one can be handed to an effect sampler.
  tex->textureSize = size;
  tex->internalFormat = internalFormat;
}

void GlFrameBuffer::allocateFace(Face& face, Vec2U const& size, char const* which) {
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

  // A COMPLETENESS FLOOR, not the sampling config. A face texture reaches the GPU only through an
  // EffectTexture sampler, and renderGlBuffer re-specifies MIN/MAG/WRAP on it every draw from the SAMPLING
  // effect's declaration -- so whatever is set here is overwritten before anything reads it. The binding owns
  // the sampling; the face owns only being COMPLETE (GL's default MIN_FILTER is GL_NEAREST_MIPMAP_LINEAR, and a
  // mip-less face with that is incomplete). So set the floor from the SAME Nearest/Clamp the object records
  // above -- not from config. This deletes the read of `textureFiltering`/`textureAddressing`, which did
  // nothing, and its lie: when the config said "linear", GL was LINEAR while this object's filtering() still
  // self-reported Nearest. The C++ record contradicted its own GL state for the whole life of the surface.
  if (!multisample) {
    while (glGetError() != GL_NO_ERROR) {}
    GLint wrap = tex->textureAddressing == TextureAddressing::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GLint filter = tex->textureFiltering == TextureFiltering::Nearest ? GL_NEAREST : GL_LINEAR;
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

void GlFrameBuffer::resize(Vec2U const& newSize) {
  // Idempotent, and it must be: setScreenSize runs on every config reload, and re-specifying storage would
  // otherwise discard the contents of every retained (clear:false) surface for nothing.
  if (size() == newSize)
    return;

  specifyStorage(front, newSize, "primary");
  if (back)
    specifyStorage(*back, newSize, "second face");
}

void GlFrameBuffer::clearFaces() {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, front.id);
  glClear(GL_COLOR_BUFFER_BIT);
  if (back) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, back->id);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}

GlFrameBuffer::GlFrameBuffer(String const& fbName, Json const& fbConfig, Vec2U const& screenSize)
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
  allocateFace(front, sizeFor(screenSize), "primary");
}

void GlFrameBuffer::makeDoubled() {
  if (back)
    return;   // idempotent: a surface has at most two faces

  // MIRROR the first face's RECORDED size. It used to re-derive the size from screenSize/sizeDiv -- which is
  // how a doubled surface could be born with two faces of DIFFERENT sizes: the first at its overrideSize (or
  // at its 256x256 placeholder), the second at whatever the screen happened to be.
  //
  // Allocate into a local, then move it into `back`. So `back` becomes valid ONLY once the second face is
  // fully built: there is no instant at which doubled() is true but the storage behind it is half-formed.
  Face second;
  allocateFace(second, size(), "second face");
  back.emplace(std::move(second));
}

void GlFrameBuffer::swap() {
  if (!back)
    throw RendererException::format("Framebuffer '{}': swap() on a surface with only one face", name);

  // Flip which face is written. GlPass::bindTarget keys on the write-face index (writingBack()), so this flip
  // changes its bind key and forces the rebind by itself -- the old `justSwapped` bool that defeated an
  // identity-only early-out is gone, retired by the (target, face, size) key.
  writeToBack = !writeToBack;
}

GlFrameBuffer::~GlFrameBuffer() {
  // The second face cannot be forgotten. It used to be, on every loadConfig -- i.e. on every HDR or AA toggle
  // -- because the destructor knew about `id` and not about `altId`. Now `back` is one object: if it exists,
  // its framebuffer is freed; if it does not, there is nothing to free and nothing to remember.
  glDeleteFramebuffers(1, &front.id);
  front.texture.reset();
  if (back) {
    glDeleteFramebuffers(1, &back->id);
    back->texture.reset();
  }
}

Effect* GlEffects::find(String const& name) {
  return m_byName.ptr(name);
}

void GlEffects::destroyAll() {
  for (auto& effect : m_byName)
    glDeleteProgram(effect.second.program);
  m_byName.clear();
}

void GlEffects::rebindBorrows(GlTargets& targets) {
  for (auto& effect : m_byName) {
    for (auto& entry : effect.second.textures) {
      EffectTexture& tex = entry.second;
      if (!tex.borrowed())
        continue;

      if (auto buf = targets.find(tex.borrowedFrom)) {
        // The same framebuffer, freshly rebuilt: re-point to its new face, still borrowed from the same name.
        // The old face is now an orphan and this drops our last reference to it, so it is finally deleted.
        tex.share(buf->writeFace().texture, tex.borrowedFrom);
      } else {
        // That framebuffer is gone from the config altogether. Let go rather than hold a dangling orphan; the
        // per-draw bind loop skips a null sampler, and switchEffectConfig's `undefined` guard will re-point it
        // if the effect declares one.
        tex.release();
      }
    }
  }
}

void GlEffects::setScriptable(String const& effectName, String const& parameterName, RenderEffectParameter const& value) {
  auto effect = find(effectName);
  if (!effect)
    return;

  auto ptr = effect->scriptables.ptr(parameterName);
  if (!ptr || (ptr->parameterValue && *ptr->parameterValue == value))
    return;

  if (ptr->parameterType != value.typeIndex())
    throw RendererException::format("setEffectScriptableParameter '{}' parameter type mismatch", parameterName);

  // A CPU write, and ONLY a CPU write. The effect named here may not be the bound one, so there is no program
  // to issue a glUniform against. GlPass::bindEffect replays this the next time the effect is bound.
  ptr->parameterValue = value;
}

Maybe<RenderEffectParameter> GlEffects::getScriptable(String const& effectName, String const& parameterName) {
  auto effect = find(effectName);
  if (!effect)
    return {};
  auto ptr = effect->scriptables.ptr(parameterName);
  if (!ptr)
    return {};
  return ptr->parameterValue;
}

Maybe<VariantTypeIndex> GlEffects::getScriptableType(String const& effectName, String const& parameterName) {
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
Effect& GlEffects::load(String const& name, Json const& effectConfig, StringMap<String> const& shaders) {
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
  effect.resolveLocations();   // fixed attribute/uniform locations, once, now that the program is linked
  return effect;
}

// The one place a RenderEffectParameter becomes a glUniform call. It was written out twice, character for
// character -- here and in the scriptable replay at bind -- which is one variant ladder to keep in step every
// time RenderEffectParameter gains a type. It is a free function, not a method: it needs no effect, no pass and
// no registry, only a location and a value. Hanging it on a component would be the "and also uploads uniforms"
// clause that the Law of One forbids.
//
// It writes into whatever program is CURRENTLY BOUND. That is not a defect of this function -- it is how
// glUniform works -- but it means every caller owes the reader a reason why the right program is bound.
void uploadUniform(GLint location, RenderEffectParameter const& value) {
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

//Assumes the passed effect program is currently in use.
void GlPass::bindEffect(Effect& newEffect, Vec2U const& screenSize) {
  Effect& effect = newEffect;
  glUseProgram(effect.program);
  this->effect = &effect;

  // Copy the locations the effect resolved once at load. No glGet*, no by-name map lookup.
  positionAttribute = effect.positionAttribute;
  colorAttribute = effect.colorAttribute;
  texCoordAttribute = effect.texCoordAttribute;
  dataAttribute = effect.dataAttribute;
  screenSizeUniform = effect.screenSizeUniform;
  vertexTransformUniform = effect.vertexTransformUniform;

  // textureSizeUniforms is read per draw (renderGlBuffer), so it stays flattened onto the pass.
  textureSizeUniforms.clear();
  if (effect.includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i)
      textureSizeUniforms.append(effect.vbTextureSizeUniforms[i]);
  }

  if (effect.includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i)
      glUniform1i(effect.vbTextureUniforms[i], i);
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

void GlPass::bindTarget(RefPtr<GlFrameBuffer> const& newTarget, Vec2U const& screenSize) {
  // A null target IS THE SCREEN (framebuffer 0). This is the one place the screen is bound -- the two
  // hand-rolled `target.reset(); glBindFramebuffer(0)` sites now route here through unbind().
  if (!newTarget) {
    // Early-out only when the cache proves the screen is already bound at this size. The cache is trustworthy
    // in-frame: `target == null` coincides with GL_DRAW == 0 because every screen bind runs through here and
    // startFrame/finishFrame re-bind 0 without changing size. The ONE place it goes stale is loadConfig, which
    // reallocates targets under GL while resetting `target` -- so loadConfig calls invalidate() to make the
    // {0,0} sentinel here refuse this early-out. Without that, a stale (screen, screenSize) would skip the
    // rebind and leave draws landing in a just-reallocated target FBO (the AA/HDR-toggle regression).
    if (!target && boundViewport == screenSize)
      return;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, screenSize[0], screenSize[1]);
    target = {};
    boundViewport = screenSize;
    boundWriteToBack = false;
    return;
  }

  // THE (target, face, size) BIND KEY. A live surface is never (0,0) -- F1 makes every surface born correct
  // at its real size, so size() is always the viewport and the old `vp == 0 -> screenSize / sizeDiv` fallback
  // is dead code, removed. size() reads front.texture->textureSize, which equals the old
  // writeFace().texture->glTextureSize() on every single-faced (in-tree / oracle) surface -- byte-identical.
  //
  // The key adds the write-FACE index and the VIEWPORT to the old identity-only test. That fixes two real
  // skips the old key made: a swap() flips the write face without changing the target (was patched by
  // justSwapped, now folded in), and a caller re-targeting the SAME surface at a NEW size wants a new viewport
  // (was patched by setRenderTarget's cover glViewport, now folded in and deleted). Setting the viewport HERE
  // is also why an effect declaring `frameBuffer` + `sizeDiv` no longer draws through a stale full-screen
  // viewport -- silently, and only for mods, since nothing in-tree ships a sizeDiv surface.
  Vec2U vp = newTarget->size();
  bool wtb = newTarget->writingBack();
  if (target == newTarget && boundWriteToBack == wtb && boundViewport == vp)
    return;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, newTarget->writeFace().id);
  glViewport(0, 0, vp[0], vp[1]);
  target = newTarget;
  boundWriteToBack = wtb;
  boundViewport = vp;
}

void Effect::resolveLocations() {
  positionAttribute = glGetAttribLocation(program, "vertexPosition");
  colorAttribute = glGetAttribLocation(program, "vertexColor");
  texCoordAttribute = glGetAttribLocation(program, "vertexTextureCoordinate");
  dataAttribute = glGetAttribLocation(program, "vertexData");
  screenSizeUniform = glGetUniformLocation(program, "screenSize");
  vertexTransformUniform = glGetUniformLocation(program, "vertexTransform");
  if (includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i) {
      vbTextureUniforms[i] = glGetUniformLocation(program, strf("texture{}", i).c_str());
      vbTextureSizeUniforms[i] = glGetUniformLocation(program, strf("textureSize{}", i).c_str());
    }
  }
}

}
