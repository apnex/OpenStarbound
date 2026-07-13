#include "StarRenderer_opengl.hpp"
#include "StarJsonExtra.hpp"
#include "StarCasting.hpp"
#include "StarLogging.hpp"
#include "StarTelemetry.hpp"

#include <cstring>  // memcmp (compareFrameBuffers bit-identity)

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
#version 140

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
#version 140

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

OpenGlRenderer::OpenGlRenderer() {
  auto glewResult = glewInit();
  if (glewResult != GLEW_OK && glewResult != GLEW_ERROR_NO_GLX_DISPLAY)
    throw RendererException::format("Could not initialize GLEW: {}", (char*)glewGetErrorString(glewResult));

  if (!GLEW_VERSION_2_0)
    throw RendererException("OpenGL 2.0 not available!");

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
  for (auto& effect : m_effects)
    glDeleteProgram(effect.second.program);

  m_frameBuffers.clear();
  logGlErrorSummary("OpenGL errors during shutdown");
}

String OpenGlRenderer::rendererId() const {
  return "OpenGL20";
}

Vec2U OpenGlRenderer::screenSize() const {
  return m_screenSize;
}

OpenGlRenderer::GlFrameBuffer::GlFrameBuffer(Json const& fbConfig) : config(fbConfig) {
  texture = make_ref<GlLoneTexture>();
  texture->textureFiltering = TextureFiltering::Nearest;
  texture->textureAddressing = TextureAddressing::Clamp;
  texture->textureSize = {0, 0};
  glGenTextures(1, &texture->textureId);
  if (texture->textureId == 0)
    throw RendererException("Could not generate OpenGL texture for framebuffer");

  clear = config.getBool("clear",true);
  clearGated = config.getBool("clearGated", false);

  multisample = GLEW_VERSION_4_0 ? config.getUInt("multisample", 0) : 0;
  GLenum target = multisample ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
  glBindTexture(target, texture->glTextureId());
  
  hdrMode = BoolSettingModeNames.getLeft(config.getString("hdr","Disabled"));
  bool hdr = settingModeValue(hdrMode,config.getBool("hdrSetting",false));
  alpha = config.getBool("alpha",false) || multisample;

  sizeDiv = config.getUInt("sizeDiv", 1);
  Vec2U size = jsonToVec2U(config.getArray("size", { 256, 256 })) / sizeDiv;

  if (multisample) {
    auto internalFormat =  hdr ? GL_RGBA16F : GL_RGBA8;
    
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, multisample, internalFormat, size[0], size[1], GL_TRUE);
  } else {
    auto format = alpha ? GL_RGBA : GL_RGB;
    auto internalFormat =  hdr ? 
        (alpha ? GL_RGBA16F : GL_RGB16F) :
        (alpha ? GL_RGBA8 : GL_RGB8);
    auto type = hdr ? GL_FLOAT : GL_UNSIGNED_BYTE;
    
    glTexImage2D(
      GL_TEXTURE_2D, 0, internalFormat, size[0], size[1], 0, format, type, NULL);
  }
  auto addressing = TextureAddressingNames.getLeft(config.getString("textureAddressing", "clamp"));
  auto filtering = TextureFilteringNames.getLeft(config.getString("textureFiltering", "nearest"));
  if (!multisample) {
    if (addressing == TextureAddressing::Clamp) {
      glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    if (filtering == TextureFiltering::Nearest) {
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
  }

  glGenFramebuffers(1, &id);
  if (!id)
    throw RendererException("Failed to create OpenGL framebuffer");

  glBindFramebuffer(GL_FRAMEBUFFER, id);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture->glTextureId(), 0);

  auto framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE)
    throw RendererException("OpenGL framebuffer is not complete!");
}


OpenGlRenderer::GlFrameBuffer::~GlFrameBuffer() {
  glDeleteFramebuffers(1, &id);
  texture.reset();
}

void OpenGlRenderer::loadConfig(Json const& config) {
  // Every framebuffer below is destroyed and re-created with UNDEFINED content. Retained (clear:false)
  // surfaces have no other way to learn this -- their refresh keys (size/camera/counter) are unchanged
  // across the realloc -- so bump the generation and let them invalidate. Both setMainHDR and
  // setMultiSampling land here, and ClientApplication polls both client options every frame.
  ++m_frameBufferGeneration;
  m_frameBuffers.clear();

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
    Logger::info("Creating framebuffer {}", pair.first);
    m_frameBuffers[pair.first] = make_ref<GlFrameBuffer>(config);

  }
  setScreenSize(m_screenSize);
  m_config = config;
}

void OpenGlRenderer::loadEffectConfig(String const& name, Json const& effectConfig, StringMap<String> const& shaders) {
  if (auto effect = m_effects.ptr(name)) {
    Logger::info("Reloading OpenGL effect {}", name);
    glDeleteProgram(effect->program);
    m_effects.erase(name);
  }

  GLint status = 0;
  char logBuffer[1024];

  auto compileShader = [&](GLenum type, String const& name) -> GLuint {
    GLuint shader = glCreateShader(type);
    auto* source = shaders.ptr(name);
    if (!source)
      return 0;
    char const* sourcePtr = source->utf8Ptr();
    glShaderSource(shader, 1, &sourcePtr, NULL);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
      glGetShaderInfoLog(shader, sizeof(logBuffer), NULL, logBuffer);
      throw RendererException(strf("Failed to compile {} shader: {}\n", name, logBuffer));
    }

    return shader;
  };

  GLuint vertexShader = 0, fragmentShader = 0;
  try {
    vertexShader = compileShader(GL_VERTEX_SHADER, "vertex");
    fragmentShader = compileShader(GL_FRAGMENT_SHADER, "fragment");
  }
  catch (RendererException const& e) {
    Logger::error("Shader compile error, using default: {}", e.what());
    if (vertexShader) glDeleteShader(vertexShader);
    if (fragmentShader) glDeleteShader(fragmentShader);
    vertexShader = compileShader(GL_VERTEX_SHADER, DefaultVertexShader);
    fragmentShader = compileShader(GL_FRAGMENT_SHADER, DefaultFragmentShader);
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
    throw RendererException(strf("Failed to link program: {}\n", logBuffer));
  }

  glUseProgram(m_program = program);

  auto& effect = m_effects.emplace(name, Effect()).first->second;
  effect.program = m_program;
  effect.config = effectConfig;
  effect.includeVBTextures = effectConfig.getBool("includeVBTextures",true);
  m_currentEffect = &effect;
  setupGlUniforms(effect, m_screenSize);

  for (auto const& p : effectConfig.getObject("effectParameters", {})) {
    EffectParameter effectParameter;

    effectParameter.parameterUniform = glGetUniformLocation(m_program, p.second.getString("uniform").utf8Ptr());
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
    effectTexture.textureUniform = glGetUniformLocation(m_program, p.second.getString("textureUniform").utf8Ptr());
    if (effectTexture.textureUniform == -1) {
      Logger::warn("OpenGL20 effect parameter '{}' has no associated uniform, skipping", p.first);
    } else {
        effectTexture.textureUnit = parameterTextureUnit++;
        glUniform1i(effectTexture.textureUniform, effectTexture.textureUnit);

        effectTexture.textureAddressing = TextureAddressingNames.getLeft(p.second.getString("textureAddressing", "clamp"));
        effectTexture.textureFiltering = TextureFilteringNames.getLeft(p.second.getString("textureFiltering", "nearest"));
        if (auto tsu = p.second.optString("textureSizeUniform")) {
          effectTexture.textureSizeUniform = glGetUniformLocation(m_program, tsu->utf8Ptr());
          if (effectTexture.textureSizeUniform == -1)
            Logger::warn("OpenGL20 effect parameter '{}' has textureSizeUniform '{}' with no associated uniform", p.first, *tsu);
        }

      effect.textures[p.first] = effectTexture;
    }
  }

  if (DebugEnabled)
    logGlErrorSummary("OpenGL errors setting effect config");
}

void OpenGlRenderer::applyEffectParameter(EffectParameter* ptr, RenderEffectParameter const& value, String const& parameterName) {
  if (ptr->parameterValue && *ptr->parameterValue == value)
    return;

  if (ptr->parameterType != value.typeIndex())
    throw RendererException::format("OpenGlRenderer::setEffectParameter '{}' parameter type mismatch", parameterName);

  flushImmediatePrimitives();

  if (auto v = value.ptr<bool>())
    glUniform1i(ptr->parameterUniform, *v);
  else if (auto v = value.ptr<int>())
    glUniform1i(ptr->parameterUniform, *v);
  else if (auto v = value.ptr<float>())
    glUniform1f(ptr->parameterUniform, *v);
  else if (auto v = value.ptr<Vec2F>())
    glUniform2f(ptr->parameterUniform, (*v)[0], (*v)[1]);
  else if (auto v = value.ptr<Vec3F>())
    glUniform3f(ptr->parameterUniform, (*v)[0], (*v)[1], (*v)[2]);
  else if (auto v = value.ptr<Vec4F>())
    glUniform4f(ptr->parameterUniform, (*v)[0], (*v)[1], (*v)[2], (*v)[3]);

  ptr->parameterValue = value;
}

void OpenGlRenderer::setEffectParameter(String const& parameterName, RenderEffectParameter const& value) {
  auto ptr = m_currentEffect->parameters.ptr(parameterName);
  if (!ptr)
    return;
  applyEffectParameter(ptr, value, parameterName);
}

OpenGlRenderer::EffectParameterHandle OpenGlRenderer::getEffectParameterHandle(String const& parameterName) {
  return (EffectParameterHandle)m_currentEffect->parameters.ptr(parameterName);
}

void OpenGlRenderer::setEffectParameter(EffectParameterHandle handle, RenderEffectParameter const& value) {
  if (!handle)
    return;
  applyEffectParameter((EffectParameter*)handle, value, "<handle>");
}

void OpenGlRenderer::setEffectScriptableParameter(String const& effectName, String const& parameterName, RenderEffectParameter const& value) {
  auto find = m_effects.find(effectName);
  if (find == m_effects.end())
    return;

  Effect& effect = find->second;
  
  auto ptr = effect.scriptables.ptr(parameterName);
  if (!ptr || (ptr->parameterValue && *ptr->parameterValue == value))
    return;

  if (ptr->parameterType != value.typeIndex())
    throw RendererException::format("OpenGlRenderer::setEffectScriptableParameter '{}' parameter type mismatch", parameterName);

  ptr->parameterValue = value;
}

Maybe<RenderEffectParameter> OpenGlRenderer::getEffectScriptableParameter(String const& effectName, String const& parameterName) {
  auto find = m_effects.find(effectName);
  if (find == m_effects.end())
    return {};

  Effect& effect = find->second;

  auto ptr = effect.scriptables.ptr(parameterName);
  if (!ptr)
    return {};
  
  return ptr->parameterValue;
}
Maybe<VariantTypeIndex> OpenGlRenderer::getEffectScriptableParameterType(String const& effectName, String const& parameterName) {
  auto find = m_effects.find(effectName);
  if (find == m_effects.end())
    return {};

  Effect& effect = find->second;

  auto ptr = effect.scriptables.ptr(parameterName);
  if (!ptr)
    return {};
  
  return ptr->parameterType;
}

void OpenGlRenderer::setEffectTexture(String const& textureName, ImageView const& image) {
  auto ptr = m_currentEffect->textures.ptr(textureName);
  if (!ptr)
    return;

  flushImmediatePrimitives();

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
}

bool OpenGlRenderer::switchEffectConfig(String const& name) {
  flushImmediatePrimitives();
  auto find = m_effects.find(name);
  if (find == m_effects.end())
    return false;

  Effect& effect = find->second;
  if (m_currentEffect == &effect)
    return true;

  if (auto blitFrameBufferId = effect.config.optString("blitFrameBuffer"))
    blitGlFrameBuffer(getGlFrameBuffer(*blitFrameBufferId));

  auto effectScreenSize = m_screenSize;
  if (auto frameBufferId = effect.config.optString("frameBuffer")) {
    auto buf = getGlFrameBuffer(*frameBufferId);
    switchGlFrameBuffer(buf);
    effectScreenSize = m_screenSize / (buf->sizeDiv);
  } else {
    m_currentFrameBuffer.reset();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  }

  glUseProgram(m_program = effect.program);
  setupGlUniforms(effect, effectScreenSize);
  m_currentEffect = &effect;

  setEffectParameter("vertexRounding", m_multiSampling > 0);
  if (auto fbts = effect.config.optArray("frameBufferTextures")) {
    for (auto const& fbt : *fbts) {
      if (auto frameBufferId = fbt.optString("framebuffer")) {
        auto textureUniform = fbt.getString("texture");
        auto ptr = m_currentEffect->textures.ptr(textureUniform);
        if (ptr) {
          if (!ptr->textureValue || ptr->textureValue->textureId == 0) {  
            auto texture = getGlFrameBuffer(*frameBufferId)->texture;
            ptr->textureValue = texture;
            if (ptr->textureSizeUniform != -1) {
              auto textureSize = ptr->textureValue->glTextureSize();
              glUniform2f(ptr->textureSizeUniform, textureSize[0], textureSize[1]);
            }
          }
        }
      }
    }
  }
  return true;
}

void OpenGlRenderer::setRenderTarget(Maybe<String> const& frameBufferId, Vec2U size) {
  flushImmediatePrimitives();

  if (!frameBufferId) {
    // Restore the screen as the draw target and the full-screen viewport/screenSize.
    m_currentFrameBuffer.reset();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenSize[0], m_screenSize[1]);
    if (m_screenSizeUniform != -1)
      glUniform2f(m_screenSizeUniform, (float)m_screenSize[0], (float)m_screenSize[1]);
    return;
  }

  // Tolerate a missing target rather than crashing the frame: callers (e.g. GpuLightmapPass)
  // degrade to their CPU path. Warn once via the renderer log on the absent id.
  auto bufPtr = m_frameBuffers.ptr(*frameBufferId);
  if (!bufPtr) {
    Logger::warn("setRenderTarget: frame buffer '{}' does not exist; ignoring", *frameBufferId);
    return;
  }
  auto buf = *bufPtr;

  // (Re)allocate the target's color texture when a non-zero size differs from the current one.
  // Off-screen lighting targets are lightmap-sized (small, view-dependent), not screen-sized.
  if (size[0] != 0 && size[1] != 0 && buf->texture->textureSize != size) {
    bool hdr = settingModeValue(buf->hdrMode, buf->config.getBool("hdrSetting", false));
    auto format = buf->alpha ? GL_RGBA : GL_RGB;
    auto internalFormat = hdr ? (buf->alpha ? GL_RGBA16F : GL_RGB16F) : (buf->alpha ? GL_RGBA8 : GL_RGB8);
    auto type = hdr ? GL_FLOAT : GL_UNSIGNED_BYTE;
    glBindTexture(GL_TEXTURE_2D, buf->texture->textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, size[0], size[1], 0, format, type, NULL);
    buf->texture->textureSize = size;
  }

  switchGlFrameBuffer(buf);
  Vec2U vp = (size[0] != 0 && size[1] != 0) ? size : buf->texture->textureSize;
  glViewport(0, 0, vp[0], vp[1]);
  if (m_screenSizeUniform != -1)
    glUniform2f(m_screenSizeUniform, (float)vp[0], (float)vp[1]);
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
  auto ptr = m_currentEffect->textures.ptr(textureName);
  if (!ptr)
    return;

  flushImmediatePrimitives();

  // Bind the framebuffer's color texture (a GlLoneTexture, same type setEffectTexture produces)
  // directly to the sampler -- no CPU upload.
  ptr->textureValue = getGlFrameBuffer(frameBufferId)->texture;
  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureAlias(String const& destTextureName, String const& sourceTextureName) {
  auto dest = m_currentEffect->textures.ptr(destTextureName);
  auto src = m_currentEffect->textures.ptr(sourceTextureName);
  if (!dest || !src || !src->textureValue)
    return;

  flushImmediatePrimitives();

  // Share the source sampler's already-uploaded texture (same GlLoneTexture, ref-counted) with the
  // dest sampler -- the per-draw bind loop will bind it to dest's texture unit. No CPU upload.
  dest->textureValue = src->textureValue;
  if (dest->textureSizeUniform != -1) {
    auto textureSize = dest->textureValue->glTextureSize();
    glUniform2f(dest->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureHalfRGB(String const& textureName, Vec2U size, uint16_t const* halfData) {
  auto ptr = m_currentEffect->textures.ptr(textureName);
  if (!ptr || size[0] == 0 || size[1] == 0)
    return;

  flushImmediatePrimitives();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool fresh = false;
  if (!ptr->textureValue || ptr->textureValue->textureId == 0) {
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
    fresh = true;
  } else {
    glBindTexture(GL_TEXTURE_2D, ptr->textureValue->textureId);
  }
  // RGB16F storage + GL_HALF_FLOAT source: half the bytes of RGB_F, no precision loss (FBOs are 16F).
  // Same-size re-upload goes through TexSubImage into the EXISTING storage: the old unconditional
  // glTexImage2D re-spec allocated a fresh driver buffer object per upload at the lighting cadence
  // (measured ~30% of the kernel texture cluster, #127). Size changes (zoom/resolution) still re-spec.
  if (!fresh && ptr->textureValue->textureSize == size) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size[0], size[1], GL_RGB, GL_HALF_FLOAT, halfData);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, size[0], size[1], 0, GL_RGB, GL_HALF_FLOAT, halfData);
    ptr->textureValue->textureSize = size;
  }

  if (ptr->textureSizeUniform != -1) {
    auto textureSize = ptr->textureValue->glTextureSize();
    glUniform2f(ptr->textureSizeUniform, (float)textureSize[0], (float)textureSize[1]);
  }
}

void OpenGlRenderer::setEffectTextureR8(String const& textureName, Vec2U size, uint8_t const* data) {
  auto ptr = m_currentEffect->textures.ptr(textureName);
  if (!ptr || size[0] == 0 || size[1] == 0)
    return;

  flushImmediatePrimitives();

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool fresh = false;
  if (!ptr->textureValue || ptr->textureValue->textureId == 0) {
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

Image OpenGlRenderer::readFrameBuffer(String const& frameBufferId) {
  flushImmediatePrimitives();

  auto bufPtr = m_frameBuffers.ptr(frameBufferId);
  if (!bufPtr) {
    Logger::warn("readFrameBuffer: frame buffer '{}' does not exist", frameBufferId);
    return Image();
  }
  auto buf = *bufPtr;

  // EVERY not-readable condition MUST return an EMPTY image, never a zero-FILLED one. A caller that hashes
  // or compares the result cannot distinguish "the frame really is black" from "the read silently failed" --
  // and a silently-zeroed frame hashes CONSISTENTLY, so a golden-hash gate built on it would report a stable
  // PASS forever while seeing nothing at all. Empty is loud; zero-filled is a false green.
  Vec2U size = buf->texture->textureSize;
  if (size[0] == 0 || size[1] == 0) {
    Logger::warn("readFrameBuffer: frame buffer '{}' has no recorded size", frameBufferId);
    return Image();
  }

  while (glGetError() != GL_NO_ERROR) {}   // drain pre-existing errors so ours is attributable

  Image result(size, PixelFormat::RGB_F);

  // glReadPixels is INVALID on a multisample framebuffer -- it must be blit-RESOLVED to a single-sample target
  // first. "main" is multisample whenever antiAliasing is on, so without this the whole AA path would be
  // unreadable and therefore unverifiable. Resolve into a scratch single-sample FBO and read that.
  GLuint resolveFbo = 0, resolveTex = 0;
  GLuint readFrom = buf->id;
  if (buf->multisample) {
    glGenTextures(1, &resolveTex);
    glBindTexture(GL_TEXTURE_2D, resolveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size[0], size[1], 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, buf->id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
    glBlitFramebuffer(0, 0, size[0], size[1], 0, 0, size[0], size[1], GL_COLOR_BUFFER_BIT, GL_NEAREST);
    readFrom = resolveFbo;
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, readFrom);
  glReadPixels(0, 0, size[0], size[1], GL_RGB, GL_FLOAT, result.data());
  // Restore the read binding to whatever draw target is current (screen if none).
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_currentFrameBuffer ? m_currentFrameBuffer->id : 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_currentFrameBuffer ? m_currentFrameBuffer->id : 0);

  if (resolveFbo) {
    glDeleteFramebuffers(1, &resolveFbo);
    glDeleteTextures(1, &resolveTex);
  }

  if (GLenum err = glGetError(); err != GL_NO_ERROR) {
    Logger::warn("readFrameBuffer: read of '{}' failed (GL error {:#x})", frameBufferId, (unsigned)err);
    return Image();
  }
  return result;
}

pair<size_t, Vec2U> OpenGlRenderer::compareFrameBuffers(String const& a, String const& b, float* maxAbsDiff) {
  // Offline bit-identity oracle. Flush pending draws, then GL-read both framebuffers' color to CPU and
  // count per-pixel BIT differences (memcmp of the raw float triples). Reads GL_RGB/GL_FLOAT like
  // readFrameBuffer: GL converts from RGB8 or RGB16F storage, so this is format-agnostic across the hdr
  // FromSetting modes. A raw bit compare is the correct test -- both buffers are the same-format render of
  // the same inputs, so a bit-identical cache path MUST produce identical pixels -- and it is NaN-safe
  // (identical NaN bit patterns compare equal, unlike float !=). glReadPixels stalls; debug/validate only.
  // Returns {NPos, {}} (not-comparable) on absent/multisample/mismatched-size targets or a readback error,
  // so a failed read can never be scored as a false MATCH.
  flushImmediatePrimitives();

  auto aPtr = m_frameBuffers.ptr(a);
  auto bPtr = m_frameBuffers.ptr(b);
  if (!aPtr || !bPtr) {
    Logger::warn("compareFrameBuffers: frame buffer '{}' or '{}' does not exist", a, b);
    return {NPos, Vec2U()};
  }
  // Multisample color attachments cannot be glReadPixels'd (GL_INVALID_OPERATION); the oracle is an AA-off
  // instrument, so treat a multisample target as not-comparable rather than reading garbage.
  if ((*aPtr)->multisample || (*bPtr)->multisample) {
    Logger::warn("compareFrameBuffers: '{}' or '{}' is multisample -- not comparable", a, b);
    return {NPos, Vec2U()};
  }
  Vec2U sizeA = (*aPtr)->texture->textureSize;
  Vec2U sizeB = (*bPtr)->texture->textureSize;
  if (sizeA != sizeB || sizeA[0] == 0 || sizeA[1] == 0) {
    Logger::warn("compareFrameBuffers: size mismatch/empty ('{}'={},{} vs '{}'={},{})",
      a, sizeA[0], sizeA[1], b, sizeB[0], sizeB[1]);
    return {NPos, Vec2U()};
  }

  size_t pixels = (size_t)sizeA[0] * sizeA[1];
  List<float> bufA, bufB;
  bufA.resize(pixels * 3);
  bufB.resize(pixels * 3);

  while (glGetError() != GL_NO_ERROR) {}  // drain pre-existing errors so the post-read check is isolated
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (*aPtr)->id);
  glReadPixels(0, 0, sizeA[0], sizeA[1], GL_RGB, GL_FLOAT, bufA.ptr());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (*bPtr)->id);
  glReadPixels(0, 0, sizeB[0], sizeB[1], GL_RGB, GL_FLOAT, bufB.ptr());
  GLenum readErr = glGetError();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_currentFrameBuffer ? m_currentFrameBuffer->id : 0);
  if (readErr != GL_NO_ERROR) {
    // A failed readback leaves the zero-filled buffers untouched -> would score as a false MATCH. Bail.
    Logger::warn("compareFrameBuffers: glReadPixels error 0x{:x} on '{}'/'{}' -- not comparable", (unsigned)readErr, a, b);
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
  return m_frameBuffers.contains(id);
}

uint64_t OpenGlRenderer::frameBufferGeneration() const {
  return m_frameBufferGeneration;
}

void OpenGlRenderer::setGatedFrameBufferClears(bool active) {
  m_gatedClearsActive = active;
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
  switch (mode) {
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

void OpenGlRenderer::beginGpuTimer(String const& name) {
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
  if (m_gpuTimerActive) {
    Logger::warn("beginGpuTimer('{}') nested inside an active timer -- ignored (GL_TIME_ELAPSED cannot nest)", name);
    return;
  }
  // Submit any pending primitives first so the query measures only the work that follows.
  flushImmediatePrimitives();
  auto& ring = m_gpuTimers[name];
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
      m_gpuTimerLastMicros[name] = (int64_t)(elapsedNs / 1000);
    }
    ring.issued[slot] = false; // reuse the query object regardless (drops a rare not-ready sample)
  }
  glBeginQuery(GL_TIME_ELAPSED, ring.queries[slot]);
  m_gpuTimerActive = true;
  m_gpuTimerCurrent = &ring;
  m_gpuTimerSlot = slot;
}

void OpenGlRenderer::endGpuTimer(String const&) {
  if (!m_gpuTimerActive)
    return;
  // Submit this scope's primitives so they fall inside the query, then close it.
  flushImmediatePrimitives();
  glEndQuery(GL_TIME_ELAPSED);
  m_gpuTimerCurrent->issued[m_gpuTimerSlot] = true;
  m_gpuTimerCurrent->writeIdx = (m_gpuTimerSlot + 1) % 3;
  m_gpuTimerActive = false;
  m_gpuTimerCurrent = nullptr;
}

Maybe<int64_t> OpenGlRenderer::gpuTimerLastMicros(String const& name) const {
  if (auto p = m_gpuTimerLastMicros.ptr(name))
    return *p;
  return {};
}

void OpenGlRenderer::setScreenSize(Vec2U screenSize) {
  m_screenSize = screenSize;
  glViewport(0, 0, m_screenSize[0], m_screenSize[1]);
  glUniform2f(m_screenSizeUniform, m_screenSize[0], m_screenSize[1]);

  for (auto& frameBuffer : m_frameBuffers) {
    unsigned sizeDiv = frameBuffer.second->sizeDiv;
    bool hdr = settingModeValue(frameBuffer.second->hdrMode,m_hdrSetting);
    if (unsigned multisample = frameBuffer.second->multisample) {
      auto internalFormat =  hdr ? GL_RGBA16F : GL_RGBA8;
      
      glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, frameBuffer.second->texture->glTextureId());
      glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, multisample, internalFormat, m_screenSize[0] / sizeDiv, m_screenSize[1] / sizeDiv, GL_TRUE);
    } else {
      auto format = frameBuffer.second->alpha ? GL_RGBA : GL_RGB;
      auto internalFormat =  hdr ? 
          (frameBuffer.second->alpha ? GL_RGBA16F : GL_RGB16F) :
          (frameBuffer.second->alpha ? GL_RGBA8 : GL_RGB8);
      auto type = hdr ? GL_FLOAT : GL_UNSIGNED_BYTE;
      
      glBindTexture(GL_TEXTURE_2D, frameBuffer.second->texture->glTextureId());
      glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_screenSize[0] / sizeDiv, m_screenSize[1] / sizeDiv, 0, format, type, NULL);
    }
    // Record the size we just (re)allocated. Screen-sized FBOs otherwise keep their construction-time
    // textureSize {0,0} forever (this loop reallocs the GL texture but never wrote the size back), which
    // (a) makes readFrameBuffer / compareFrameBuffers read a 0-sized buffer and (b) makes a later
    // setRenderTarget(id, size) see {0,0} != size and realloc the texture mid-frame -- silently discarding
    // the startFrame clear:true black. Recording it here fixes both for every screen-sized FBO consumer (#133).
    frameBuffer.second->texture->textureSize = Vec2U(m_screenSize[0] / sizeDiv, m_screenSize[1] / sizeDiv);
  }
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
  beginGpuTimer("render.frame.clear.gpu_us");

  for (auto& frameBuffer : m_frameBuffers) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer.second->id);
    // clearGated FBOs (e.g. the oracle's envRef) are only cleared while their consumer is armed, so a
    // dormant debug surface costs no per-frame clear.
    if (frameBuffer.second->clear && (!frameBuffer.second->clearGated || m_gatedClearsActive))
      glClear(GL_COLOR_BUFFER_BIT);
    frameBuffer.second->blitted = false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glClear(GL_COLOR_BUFFER_BIT);

  endGpuTimer("render.frame.clear.gpu_us");

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
    glUniformMatrix3fv(m_vertexTransformUniform, 1, GL_TRUE, transformation.ptr());

    if (m_currentEffect->includeVBTextures) {
      for (size_t i = 0; i < vb.textures.size(); ++i) {
        glUniform2f(m_textureSizeUniforms[i], vb.textures[i].size[0], vb.textures[i].size[1]);
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, vb.textures[i].texture);
      }
    }

    for (auto const& p : m_currentEffect->textures) {
      if (p.second.textureValue) {
        glActiveTexture(GL_TEXTURE0 + p.second.textureUnit);
        glBindTexture(GL_TEXTURE_2D, p.second.textureValue->textureId);
      }
    }

    glBindBuffer(GL_ARRAY_BUFFER, vb.vertexBuffer);

    glEnableVertexAttribArray(m_positionAttribute);
    glEnableVertexAttribArray(m_texCoordAttribute);
    glEnableVertexAttribArray(m_colorAttribute);
    glEnableVertexAttribArray(m_dataAttribute);

    glVertexAttribPointer(m_positionAttribute, 2, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, pos));
    glVertexAttribPointer(m_texCoordAttribute, 2, GL_FLOAT, GL_FALSE, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, uv));
    glVertexAttribPointer(m_colorAttribute, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, color));
    glVertexAttribIPointer(m_dataAttribute, 1, GL_INT, sizeof(GlRenderVertex), (GLvoid*)offsetof(GlRenderVertex, pack));

    glDrawArrays(GL_TRIANGLES, 0, vb.vertexCount);
  }
}

//Assumes the passed effect program is currently in use.
void OpenGlRenderer::setupGlUniforms(Effect& effect, Vec2U screenSize) {
  m_positionAttribute = effect.getAttribute("vertexPosition");
  m_colorAttribute = effect.getAttribute("vertexColor");
  m_texCoordAttribute = effect.getAttribute("vertexTextureCoordinate");
  m_dataAttribute = effect.getAttribute("vertexData");

  m_textureUniforms.clear();
  m_textureSizeUniforms.clear();
  if (effect.includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i) {
      m_textureUniforms.append(effect.getUniform(strf("texture{}", i).c_str()));
      m_textureSizeUniforms.append(effect.getUniform(strf("textureSize{}", i).c_str()));
    }
  }
  m_screenSizeUniform = effect.getUniform("screenSize");
  m_vertexTransformUniform = effect.getUniform("vertexTransform");

  if (effect.includeVBTextures) {
    for (size_t i = 0; i < MultiTextureCount; ++i)
      glUniform1i(m_textureUniforms[i], i);
  }

  glUniform2f(m_screenSizeUniform, screenSize[0], screenSize[1]);
  
  for (auto& param : effect.scriptables) {
    auto ptr = &param.second;
    auto mvalue = ptr->parameterValue;
    if (mvalue) {
      RenderEffectParameter value = mvalue.value();
      if (auto v = value.ptr<bool>())
        glUniform1i(ptr->parameterUniform, *v);
      else if (auto v = value.ptr<int>())
        glUniform1i(ptr->parameterUniform, *v);
      else if (auto v = value.ptr<float>())
        glUniform1f(ptr->parameterUniform, *v);
      else if (auto v = value.ptr<Vec2F>())
        glUniform2f(ptr->parameterUniform, (*v)[0], (*v)[1]);
      else if (auto v = value.ptr<Vec3F>())
        glUniform3f(ptr->parameterUniform, (*v)[0], (*v)[1], (*v)[2]);
      else if (auto v = value.ptr<Vec4F>())
        glUniform4f(ptr->parameterUniform, (*v)[0], (*v)[1], (*v)[2], (*v)[3]);
    }
  }
}

RefPtr<OpenGlRenderer::GlFrameBuffer> OpenGlRenderer::getGlFrameBuffer(String const& id) {
  if (auto ptr = m_frameBuffers.ptr(id))
    return *ptr;
  else
    throw RendererException::format("Frame buffer '{}' does not exist", id);
}

void OpenGlRenderer::blitGlFrameBuffer(RefPtr<GlFrameBuffer> const& frameBuffer) {
  if (frameBuffer->blitted)
    return;

  // Task #141: the final resolve of "main" to the screen was never timed. At 2560x1440 RGBA16F, and MSAA-
  // resolving when antiAliasing is on, this is not free -- and it is part of the 1.8-3.5ms/frame that the
  // whole-frame span proved was unaccounted for.
  beginGpuTimer("render.frame.blit.gpu_us");

  auto& size = m_screenSize;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer->id);
  glBlitFramebuffer(
    0, 0, size[0], size[1],
    0, 0, size[0], size[1],
    GL_COLOR_BUFFER_BIT, GL_NEAREST
  );

  endGpuTimer("render.frame.blit.gpu_us");
  frameBuffer->blitted = true;
}

void OpenGlRenderer::switchGlFrameBuffer(RefPtr<GlFrameBuffer> const& frameBuffer) {
  if (m_currentFrameBuffer == frameBuffer)
    return;

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->id);
  m_currentFrameBuffer = frameBuffer;
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
