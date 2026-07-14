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
  void setEffectScriptableParameter(String const& effectName, String const& parameterName, RenderEffectParameter const& parameter) override;
  Maybe<RenderEffectParameter> getEffectScriptableParameter(String const& effectName, String const& parameterName) override;
  Maybe<VariantTypeIndex> getEffectScriptableParameterType(String const& effectName, String const& parameterName) override;
  void setEffectTexture(String const& textureName, ImageView const& image) override;

  void setScissorRect(Maybe<RectI> const& scissorRect) override;

  bool switchEffectConfig(String const& name) override;

  TexturePtr createTexture(Image const& texture, TextureAddressing addressing, TextureFiltering filtering) override;
  void setSizeLimitEnabled(bool enabled) override;
  void setMultiTexturingEnabled(bool enabled) override;
  void setMultiSampling(unsigned multiSampling) override;
  void setMainHDR(bool enabled) override;
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
  RefPtr<GlFrameBuffer> m_currentFrameBuffer;

  RefPtr<GlTexture> m_whiteTexture;

  Maybe<RectI> m_scissorRect;

  bool m_limitTextureGroupSize;
  bool m_useMultiTexturing;
  unsigned m_multiSampling; // if non-zero, is enabled and acts as sample count
  bool m_hdrSetting;
  List<shared_ptr<GlTextureGroup>> m_liveTextureGroups;

  List<RenderPrimitive> m_immediatePrimitives;
  shared_ptr<GlRenderBuffer> m_immediateRenderBuffer;
};

}
