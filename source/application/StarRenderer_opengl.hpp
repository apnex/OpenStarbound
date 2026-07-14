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
  bool hasFrameBuffer(String const& id) const override;
  uint64_t frameBufferGeneration() const override;
  bool composite(String const& effect, String const& dstFbo, Vec2U dstSize,
                 String const& srcSampler, String const& srcFbo,
                 List<pair<String, RenderEffectParameter>> const& params) override;
  void setEffectTextureFromTarget(String const& textureName, String const& frameBufferId) override;
  void setEffectTextureAlias(String const& destTextureName, String const& sourceTextureName) override;
  void setEffectTextureHalf(String const& textureName, Vec2U size, uint16_t const* halfData, unsigned channels) override;
  void setEffectTextureR8(String const& textureName, Vec2U size, uint8_t const* data) override;
  GpuTimer& gpuTimer() override;
  RenderOracle& oracle() override;
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

    // THE SIZE ORACLE. `size()` is what is ACTUALLY allocated, right now, on every live face -- never (0,0)
    // on a live surface. `sizeFor()` is what this surface OUGHT to be at a given screen size, and it is the
    // only place that rule is written down. The two were previously derived by hand at four call sites, two
    // of which silently dropped `overrideSize`.
    Vec2U size() const;
    Vec2U sizeFor(Vec2U const& screenSize) const;

    // Give this surface its second face, mirroring the first face's RECORDED size. Idempotent. It takes no
    // screenSize: re-deriving the size here is how the two faces could be born different.
    void makeDoubled();
    // Flip which face is written and which is read.
    void swap();

    // Re-specify EVERY live face's storage at `size`, in this surface's configured format, and record it.
    // Idempotent: resizing to the size already allocated issues no GL calls at all.
    void resize(Vec2U const& size);
    // Clear every live face. The one consumer (startFrame) used to reach for faces[0].id / faces[1].id raw,
    // which is what made the seal a convention rather than a contract.
    void clearFaces();

    GlFrameBuffer(String const& name, Json const& config, Vec2U const& screenSize);
    ~GlFrameBuffer();

    // SEALED. The resolver is the contract, so the faces must not be reachable around it -- otherwise the
    // Air-Gap holds only for as long as everyone remembers, which is exactly how altId got leaked and how
    // the viewport went unset. The enclosing renderer is the surface's owner and needs the raw faces to
    // allocate, resize and clear them; nobody else does, and nobody else can.
  private:
    friend class OpenGlRenderer;

    // THE SINGLE OWNER OF FACE STORAGE. Derives the format from this surface's config (hdr / alpha /
    // multisample), specifies the colour storage at `size`, and RECORDS what it got. Nothing else may do any
    // of those three things.
    //
    // They used to be done by hand in three places -- the constructor, makeAlt, and setScreenSize -- each
    // re-deriving the same format ladder, and none of them recording the size. That is not three bugs. It is
    // one absent owner, and it produced: a second face that could be born a different size than the first;
    // a mod-visible textureSize uniform that read (0,0) forever; and a format ladder that any future change
    // would have had to find all three copies of.
    void specifyStorage(Face& face, Vec2U const& size, char const* which);
    // Bring `face` into existence: mint the GL objects, hand the storage to specifyStorage, set the sampling
    // parameters, attach, verify. Creation only -- it does not duplicate the storage or the format rules.
    void allocateFace(Face& face, Vec2U const& size, char const* which);

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

  // THE PASS: the binding of ONE effect to ONE target, for a sequence of draws.
  //
  // GL has exactly one current program and exactly one current draw framebuffer, and THEY ARE COUPLED:
  // binding an effect resolves and binds ITS framebuffer (and swaps its faces, if it reads what it writes);
  // binding a framebuffer writes the screenSize uniform of the CURRENT PROGRAM. That coupling is why a naive
  // "effects here, targets there" split cannot work -- the two halves would each need the other's privates,
  // which is the Air-Gap violated by construction.
  //
  // The coupling is not an obstacle to the decomposition. It IS a component, and nobody had written it.
  // switchEffectConfig, switchGlFrameBuffer, setRenderTarget, composite, blitGlFrameBuffer and
  // setEffectTextureFromTarget are not six problems: they are one missing Pass wearing six hats. A Pass
  // depends DOWNWARD on effects and on targets; neither of them knows the other exists.
  //
  // THIS STEP MOVES THE STATE ONLY. The six functions follow, one at a time, each certified bit-identical
  // against the three GPU oracles -- because three of them carry real behaviour changes that must not ride
  // in on a refactor, or the oracle goes red and we cannot tell a fix from a regression.
  struct GlPass {
    // The flattened locations of the bound program. Read per draw, on the hot path.
    GLuint program = 0;
    GLint positionAttribute = -1;
    GLint colorAttribute = -1;
    GLint texCoordAttribute = -1;
    GLint dataAttribute = -1;
    List<GLint> textureUniforms = {};
    List<GLint> textureSizeUniforms = {};
    GLint screenSizeUniform = -1;
    GLint vertexTransformUniform = -1;

    // THE COUPLED PAIR. This is the whole reason the component exists.
    Effect* effect = nullptr;
    RefPtr<GlFrameBuffer> target;
  };
  GlPass m_pass;

  Json m_config;

  StringMap<Effect> m_effects;

  StringMap<RefPtr<GlFrameBuffer>> m_frameBuffers;
  // Bumped by loadConfig() each time m_frameBuffers is cleared + rebuilt (all content becomes undefined).
  uint64_t m_frameBufferGeneration = 0;

  RefPtr<GlTexture> m_whiteTexture;

  Maybe<RectI> m_scissorRect;

  // THE INSTRUMENTS. Both are sealed: they own their own state and are reached only through the accessors
  // Renderer declares, so no consumer -- and no future backend -- has to know they exist to draw a frame.

  // GL_TIME_ELAPSED, one triple-buffered ring per named scope so results are read back ~3 frames later
  // without stalling the pipeline. Only issues when Telemetry::deepEnabled().
  //
  // It takes a flush thunk rather than an OpenGlRenderer&: the timer's only need of the renderer is "submit
  // what is pending, so the query brackets exactly the enclosed draws". Depending on the whole renderer to say
  // that would be a back-door into every other concern, and would make the timer untestable without a GL
  // context. One declared adapter, one direction.
  class GlGpuTimer : public GpuTimer {
  public:
    explicit GlGpuTimer(function<void()> flushPending);

    void begin(String const& name) override;
    void end(String const& name) override;
    Maybe<int64_t> lastMicros(String const& name) const override;

  private:
    struct Ring {
      GLuint queries[3] = {0, 0, 0};
      bool issued[3] = {false, false, false};
      unsigned writeIdx = 0;
    };

    function<void()> m_flushPending;
    StringMap<Ring> m_rings;
    StringMap<int64_t> m_lastMicros; // last read-back µs per scope (for the /debug HUD)
    Ring* m_current = nullptr;
    unsigned m_slot = 0;
    bool m_active = false;
  };

  // Pixel read-back for bit-identity certification. Unlike the timer, this genuinely needs the renderer's
  // framebuffer set, so it holds a back-reference -- the same one-way gap as GlFrameBuffer's `friend
  // OpenGlRenderer`. The gap that matters is the one the twelve consumers see, and that one is real: they get
  // three methods and cannot name a framebuffer face.
  //
  // Expected lifetime: DELETED. See RenderOracle in StarRenderDiagnostics.hpp.
  class GlRenderOracle : public RenderOracle {
  public:
    explicit GlRenderOracle(OpenGlRenderer& renderer);

    void setEnabled(bool enabled) override;
    pair<size_t, Vec2U> compare(String const& a, String const& b, float* maxAbsDiff) override;
    Image read(String const& frameBufferId) override;

    // Read by loadConfig: when false, framebuffers marked devOnly are not allocated at all.
    bool enabled() const;

  private:
    OpenGlRenderer& m_renderer;
    bool m_enabled = false;
  };

  GlGpuTimer m_gpuTimer;
  GlRenderOracle m_oracle;

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
