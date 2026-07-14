#pragma once

#include "StarTextureAtlas.hpp"
#include "StarRenderer.hpp"

#include "GL/glew.h"

namespace Star {

STAR_CLASS(OpenGlRenderer);

constexpr size_t FrameBufferCount = 1;

// TODO: Bott - This isn't really rendering-specific. Don't really like it either. It's nicer than having redundant bool options, though.
enum class BoolSettingMode {
  Enabled,
  FromSetting,
  Disabled
};
extern EnumMap<BoolSettingMode> const BoolSettingModeNames;

bool settingModeValue(BoolSettingMode const& mode, bool const& setting);

// OpenGL 2.0 implementation of Renderer.  OpenGL context must be created and
// active during construction, destruction, and all method calls.
class OpenGlRenderer : public Renderer {
public:
  OpenGlRenderer();
  ~OpenGlRenderer();

  String rendererId() const override;
  Vec2U screenSize() const override;

  void loadConfig(Json const& config) override;
  void loadEffectConfig(String const& name, Json const& effectConfig, StringMap<String> const& shaders) override;

  void setEffectParameter(String const& parameterName, RenderEffectParameter const& parameter) override;
  EffectParameterHandle getEffectParameterHandle(String const& parameterName) override;
  void setEffectParameter(EffectParameterHandle handle, RenderEffectParameter const& parameter) override;
  void setEffectScriptableParameter(String const& effectName, String const& parameterName, RenderEffectParameter const& parameter) override;
  Maybe<RenderEffectParameter> getEffectScriptableParameter(String const& effectName, String const& parameterName) override;
  Maybe<VariantTypeIndex> getEffectScriptableParameterType(String const& effectName, String const& parameterName) override;
  void setEffectTexture(String const& textureName, ImageView const& image) override;

  void setScissorRect(Maybe<RectI> const& scissorRect) override;

  bool switchEffectConfig(String const& name) override;

  void setRenderTarget(Maybe<String> const& frameBufferId, Vec2U size = Vec2U()) override;
  void clearRenderTarget(Vec4F clearColor) override;
  pair<size_t, Vec2U> compareFrameBuffers(String const& a, String const& b, float* maxAbsDiff) override;
  bool hasFrameBuffer(String const& id) const override;
  uint64_t frameBufferGeneration() const override;
  void setGatedFrameBufferClears(bool active) override;
  void setOracleSurfaces(bool enabled) override;
  bool composite(String const& effect, String const& dstFbo, Vec2U dstSize,
                 String const& srcSampler, String const& srcFbo,
                 List<pair<String, RenderEffectParameter>> const& params) override;
  void setEffectTextureFromTarget(String const& textureName, String const& frameBufferId) override;
  void setEffectTextureAlias(String const& destTextureName, String const& sourceTextureName) override;
  void setEffectTextureHalf(String const& textureName, Vec2U size, uint16_t const* halfData, unsigned channels) override;
  void setEffectTextureR8(String const& textureName, Vec2U size, uint8_t const* data) override;
  Image readFrameBuffer(String const& frameBufferId) override;
  void setBlendMode(BlendMode mode) override;

  TexturePtr createTexture(Image const& texture, TextureAddressing addressing, TextureFiltering filtering) override;
  void setSizeLimitEnabled(bool enabled) override;
  void setMultiTexturingEnabled(bool enabled) override;
  void setMultiSampling(unsigned multiSampling) override;
  void setMainHDR(bool enabled) override;
  void setVboOrphan(bool enabled) override;
  TextureGroupPtr createTextureGroup(TextureGroupSize size, TextureFiltering filtering) override;
  RenderBufferPtr createRenderBuffer() override;

  List<RenderPrimitive>& immediatePrimitives() override;
  void render(RenderPrimitive primitive) override;
  void renderBuffer(RenderBufferPtr const& renderBuffer, Mat3F const& transformation) override;

  void flush(Mat3F const& transformation) override;

  void beginGpuTimer(String const& name) override;
  void endGpuTimer(String const& name) override;
  Maybe<int64_t> gpuTimerLastMicros(String const& name) const override;

  void setScreenSize(Vec2U screenSize);

  void startFrame();
  void finishFrame();

private:
  struct GlTextureAtlasSet : public TextureAtlasSet<GLuint> {
  public:
    GlTextureAtlasSet(unsigned atlasNumCells);

    GLuint createAtlasTexture(Vec2U const& size, PixelFormat pixelFormat) override;
    void destroyAtlasTexture(GLuint const& glTexture) override;
    void copyAtlasPixels(GLuint const& glTexture, Vec2U const& bottomLeft, Image const& image) override;

    TextureFiltering textureFiltering;
  };

  struct GlTextureGroup : enable_shared_from_this<GlTextureGroup>, public TextureGroup {
    GlTextureGroup(unsigned atlasNumCells);
    ~GlTextureGroup();

    TextureFiltering filtering() const override;
    TexturePtr create(Image const& texture) override;

    GlTextureAtlasSet textureAtlasSet;
  };

  struct GlTexture : public Texture {
    virtual GLuint glTextureId() const = 0;
    virtual Vec2U glTextureSize() const = 0;
    virtual Vec2U glTextureCoordinateOffset() const = 0;
  };

  struct GlGroupedTexture : public GlTexture {
    ~GlGroupedTexture();

    Vec2U size() const override;
    TextureFiltering filtering() const override;
    TextureAddressing addressing() const override;

    GLuint glTextureId() const override;
    Vec2U glTextureSize() const override;
    Vec2U glTextureCoordinateOffset() const override;

    void incrementBufferUseCount();
    void decrementBufferUseCount();

    unsigned bufferUseCount = 0;
    shared_ptr<GlTextureGroup> parentGroup;
    GlTextureAtlasSet::TextureHandle parentAtlasTexture = nullptr;
  };

  struct GlLoneTexture : public GlTexture {
    ~GlLoneTexture();

    Vec2U size() const override;
    TextureFiltering filtering() const override;
    TextureAddressing addressing() const override;

    GLuint glTextureId() const override;
    Vec2U glTextureSize() const override;
    Vec2U glTextureCoordinateOffset() const override;

    GLuint textureId = 0;
    Vec2U textureSize;
    // Channel count of the last half-float upload. Without it, switching a texture from 3 to 4 channels at the
    // same size would TexSubImage RGBA data into RGB storage -- a silent corruption, not an error.
    unsigned uploadChannels = 0;
    TextureAddressing textureAddressing = TextureAddressing::Clamp;
    TextureFiltering textureFiltering = TextureFiltering::Nearest;
  };

  struct GlPackedVertexData {
    uint32_t textureIndex : 2;
    uint32_t fullbright : 1;
    uint32_t rX : 1;
    uint32_t rY : 1;
    uint32_t unused : 27;
  };

  struct GlRenderVertex {
    Vec2F pos;
    Vec2F uv;
    Vec4B color;
    union Packed {
      uint32_t packed;
      GlPackedVertexData vars;
    } pack;
  };

  struct GlRenderBuffer : public RenderBuffer {
    struct GlVertexBufferTexture {
      GLuint texture;
      Vec2U size;
    };

    struct GlVertexBuffer {
      List<GlVertexBufferTexture> textures;
      GLuint vertexBuffer = 0;
      size_t vertexCount = 0;
      // True allocated storage size (bytes). Distinct from vertexCount (the draw count): tracking
      // the high-water capacity means a grow->shrink->grow size sequence reuses existing storage
      // via glBufferSubData instead of re-specifying it with glBufferData every time it re-grows.
      size_t byteCapacity = 0;
    };

    GlRenderBuffer();
    ~GlRenderBuffer();

    void set(List<RenderPrimitive>& primitives) override;

    RefPtr<GlTexture> whiteTexture;
    ByteArray accumulationBuffer;

    HashSet<TexturePtr> usedTextures;
    List<GlVertexBuffer> vertexBuffers;
    GLuint vertexArray = 0;

    bool useMultiTexturing{true};
  };

  struct EffectParameter {
    GLint parameterUniform = -1;
    VariantTypeIndex parameterType = 0;
    Maybe<RenderEffectParameter> parameterValue;
  };

  struct EffectTexture {
    GLint textureUniform = -1;
    unsigned textureUnit = 0;
    TextureAddressing textureAddressing = TextureAddressing::Clamp;
    TextureFiltering textureFiltering = TextureFiltering::Linear;
    GLint textureSizeUniform = -1;
    RefPtr<GlLoneTexture> textureValue;
  };
  
  // A framebuffer is a SURFACE with one or two FACES.
  //
  // A face is one drawable copy of the surface: a colour texture, plus the framebuffer object that draws
  // into it. Normally there is one. A surface declared "double" (#542) has two, and the second is what lets
  // a shader read what it is writing -- swap() flips which face is being written and which is readable.
  //
  // The faces are reached ONLY through writeFace() / readFace(). That is not style: every framebuffer bug
  // this struct has had came from a consumer reaching past them. `altId` was leaked because the destructor
  // knew about `id` and forgot its twin. `makeAlt` duplicated the entire constructor because there was no
  // one allocator. And blitGlFrameBuffer / the effect-texture resolution each had to re-derive "which half
  // do I mean?" by hand. One resolver, and none of those are expressible.
  struct GlFrameBuffer : RefCounter {
    struct Face {
      GLuint id = 0;
      RefPtr<GlLoneTexture> texture;
    };

    Json config;
    String name;
    Maybe<Vec2U> overrideSize;
    BoolSettingMode hdrMode = BoolSettingMode::Disabled;
    bool alpha = false;
    bool clear = true;
    // When true, this clear:true FBO is only startFrame-cleared while gated clears are armed
    // (setGatedFrameBufferClears) -- zero per-frame cost when its debug/optional consumer is off.
    bool clearGated = false;
    unsigned multisample = 0;
    unsigned sizeDiv = 1;

    // Set by swap(); defeats switchGlFrameBuffer's "already bound" early-out so the new write face is
    // actually bound rather than silently skipped.
    bool justSwapped = false;

    // THE RESOLVER. Ask which face you are writing, or which you may read. Never reach for a texture or an
    // id directly.
    Face& writeFace() { return faces[write]; }
    Face& readFace() { return faces[doubled ? (write ^ 1) : write]; }
    Face const& writeFace() const { return faces[write]; }
    Face const& readFace() const { return faces[doubled ? (write ^ 1) : write]; }

    // Give this surface its second face. Idempotent.
    void makeDoubled(Vec2U const& screenSize);
    // Flip which face is written and which is read.
    void swap();

    // THE one allocation path: size `face` in this surface's configured format, attach it to a fresh
    // framebuffer object, verify, and record what was allocated. Every face -- first or second -- comes
    // through here, so everything that must happen on allocation happens exactly once, in one place.
    void allocateTarget(Face& face, Vec2U const& size, char const* which);

    GlFrameBuffer(String const& name, Json const& config);
    ~GlFrameBuffer();

    Face faces[2];
    unsigned write = 0;      // index of the face currently being drawn into
    bool doubled = false;    // true iff faces[1] exists
  };

  class Effect {
  public:
    GLuint program = 0;
    Json config;
    StringMap<EffectParameter> parameters;
    StringMap<EffectParameter> scriptables; // scriptable parameters which can be changed when the effect is not loaded
    StringMap<EffectTexture> textures;

    StringMap<GLuint> attributes;
    StringMap<GLuint> uniforms;

    GLuint getAttribute(String const& name);
    GLuint getUniform(String const& name);
    bool includeVBTextures;
    bool doubleBuffered = false;
  };

  static bool logGlErrorSummary(String prefix);
  static void uploadTextureImage(PixelFormat pixelFormat, Vec2U size, uint8_t const* data);

  
  static RefPtr<GlLoneTexture> createGlTexture(ImageView const& image, TextureAddressing addressing, TextureFiltering filtering);

  shared_ptr<GlRenderBuffer> createGlRenderBuffer();

  void flushImmediatePrimitives(Mat3F const& transformation = Mat3F::identity());

  void renderGlBuffer(GlRenderBuffer const& renderBuffer, Mat3F const& transformation);

  void setupGlUniforms(Effect& effect, Vec2U screenSize);

  void applyEffectParameter(EffectParameter* parameter, RenderEffectParameter const& value, String const& parameterName);

  RefPtr<OpenGlRenderer::GlFrameBuffer> getGlFrameBuffer(String const& id);
  void blitGlFrameBuffer(RefPtr<OpenGlRenderer::GlFrameBuffer> const& frameBuffer, bool const& useAlt = false);
  void switchGlFrameBuffer(RefPtr<OpenGlRenderer::GlFrameBuffer> const& frameBuffer);

  Vec2U m_screenSize;

  GLuint m_program = 0;

  GLint m_positionAttribute = -1;
  GLint m_colorAttribute = -1;
  GLint m_texCoordAttribute = -1;
  GLint m_dataAttribute = -1;
  List<GLint> m_textureUniforms = {};
  List<GLint> m_textureSizeUniforms = {};
  GLint m_screenSizeUniform = -1;
  GLint m_vertexTransformUniform = -1;

  Json m_config;

  StringMap<Effect> m_effects;
  Effect* m_currentEffect;

  StringMap<RefPtr<GlFrameBuffer>> m_frameBuffers;
  // Bumped by loadConfig() each time m_frameBuffers is cleared + rebuilt (all content becomes undefined).
  uint64_t m_frameBufferGeneration = 0;
  RefPtr<GlFrameBuffer> m_currentFrameBuffer;

  RefPtr<GlTexture> m_whiteTexture;

  Maybe<RectI> m_scissorRect;

  // Armed by setGatedFrameBufferClears; when false, startFrame skips clearing any FBO marked clearGated.
  bool m_gatedClearsActive = false;
  // Armed by setOracleSurfaces; when false, loadConfig does not allocate framebuffers marked devOnly.
  bool m_oracleSurfaces = false;

  // GPU timer queries (GL_TIME_ELAPSED), one triple-buffered ring per named scope so results
  // are read back ~3 frames later without stalling the pipeline. Only used when deepEnabled.
  struct GpuTimerRing {
    GLuint queries[3] = {0, 0, 0};
    bool issued[3] = {false, false, false};
    unsigned writeIdx = 0;
  };
  StringMap<GpuTimerRing> m_gpuTimers;
  bool m_gpuTimerActive = false;
  GpuTimerRing* m_gpuTimerCurrent = nullptr;
  unsigned m_gpuTimerSlot = 0;
  StringMap<int64_t> m_gpuTimerLastMicros; // last read-back µs per scope (for the /debug HUD)

  // WHOLE-FRAME GPU SPAN (task #141). GL_TIME_ELAPSED cannot nest -- there is one m_gpuTimerActive bool -- so
  // the per-pass timers can never report the frame TOTAL, and therefore can never reveal how much of the frame
  // they FAIL to account for. GL_TIMESTAMP is a separate query target that coexists with them, so a pair of
  // timestamps around startFrame..finishFrame gives the frame's entire GPU span (every pass, the interface
  // render, the clears, the final blit) WHILE the per-pass timers still run. (span - sum(passes)) is the
  // unattributed remainder -- which may be more than half the frame.
  static constexpr unsigned GpuTimerRingSize = 3;
  struct FrameSpanRing {
    GLuint begins[GpuTimerRingSize] = {0, 0, 0};
    GLuint ends[GpuTimerRingSize] = {0, 0, 0};
    bool issued[GpuTimerRingSize] = {false, false, false};
    unsigned writeIdx = 0;
  };
  FrameSpanRing m_frameSpan;
  unsigned m_frameSpanSlot = 0;
  bool m_frameSpanOpen = false;


  bool m_limitTextureGroupSize;
  bool m_useMultiTexturing;
  unsigned m_multiSampling = 0; // if non-zero, is enabled and acts as sample count
  // MUST be initialized. loadConfig injects this into every framebuffer's config as "hdrSetting", and it runs
  // for the first time from renderInit -- BEFORE setMainHDR (its only writer) is ever called. Left
  // indeterminate, every framebuffer declaring "hdr":"FromSetting" received a bool holding neither 0 nor 1;
  // settingModeValue passes that byte through verbatim, and the compiler lowers `hdr ? GL_FLOAT :
  // GL_UNSIGNED_BYTE` to `GL_UNSIGNED_BYTE + 5*hdr`, so a garbage byte of e.g. 116 yields 0x1645 -- not a GL
  // type enum. glTexImage2D then fails GL_INVALID_ENUM, allocates nothing, and the framebuffer surfaces as
  // "OpenGL framebuffer is not complete!". Intermittent, because the garbage varied from run to run.
  //
  // `true` matches upstream (36a389c6, "Fix HDR crash (#535)") and is not arbitrary: ClientApplication defaults
  // the hdr option to true, so starting true means the first loadConfig already builds the framebuffers in the
  // format the very next setMainHDR asks for -- no redundant rebuild of the whole set at startup.
  bool m_hdrSetting = true;
  List<shared_ptr<GlTextureGroup>> m_liveTextureGroups;

  List<RenderPrimitive> m_immediatePrimitives;
  shared_ptr<GlRenderBuffer> m_immediateRenderBuffer;
};

}
