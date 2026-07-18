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

    // THE STORAGE DESCRIPTOR. Size and internal format together, because storage is ONE act and describing
    // half of it is worse than describing none: a glTexSubImage2D fast path that consults a HALF-TRUE record
    // will happily write into storage whose format has changed underneath it, and GL will not complain.
    //
    // internalFormat is 0 until storage has been specified. WHOEVER SPECIFIES STORAGE RECORDS WHAT IT
    // SPECIFIED -- in the same breath, or the next reader is consulting a lie.
    //
    // This replaced an `uploadChannels` field that only ONE of the four storage-spec paths maintained. The
    // other three re-specified the texture and left it stale, so the SubImage guard passed on a format that no
    // longer existed. Proven: emission storage went RGBA16F -> RGB32F behind the guard's back and the pass
    // wrote half-float RGBA into three-channel storage for the rest of the run, silently dropping the alpha
    // that carries the obstacle flag.
    Vec2U textureSize;
    GLint internalFormat = 0;
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
    // NON-EMPTY when textureValue is a framebuffer's OWN colour attachment -- handed to us by
    // setEffectTextureFromTarget, by the frameBufferTextures block of an effect config, or by an alias of one
    // of those. It names WHICH framebuffer. We are then a BORROWER: we may bind that texture and sample it,
    // and we may NOT write to it.
    //
    // It answers two questions, and it has to be a name rather than a flag to answer the second:
    //
    //   "may I write here?"  -- no. The upload setters (setEffectTexture / Half / R8) each have a "reuse the
    //      texture object I already have" branch, and without this that branch re-specified storage belonging
    //      to GlTargets: glTexImage2D through a sampler, into a live render target (RB-1).
    //
    //   "where do I come from?" -- loadConfig destroys and rebuilds every target. ~GlFrameBuffer only
    //      RELEASES its RefPtr to the face texture; glDeleteTextures lives in ~GlLoneTexture and does not run
    //      while a sampler still holds a reference. So the texture is not freed, it is ORPHANED, and the
    //      sampler goes on sampling a framebuffer that no longer exists. Knowing the name lets us re-point it
    //      at the rebuilt one (RB-5).
    String borrowedFrom;
    bool borrowed() const { return !borrowedFrom.empty(); }

    // THE BORROW, SET AS ONE ACT. The texture and its borrow-status are two facts that must agree -- write
    // into a texture you are only borrowing and you re-specify a live render target (RB-1). They used to be set
    // by two independent field pokes at every call site, and the upload setters hand-copied the "may I write
    // here?" predicate three times to guess whether they had diverged. Now the pair moves together:
    //   adopt()   -- we allocated this texture, it is OURS to write, so we are no longer borrowing.
    //   share()   -- we point at storage someone else owns (a framebuffer face, or another sampler's texture),
    //                recording whose it is; `from` empty == shared-but-owned-elsewhere, not a target.
    //   release() -- let go of everything.
    //   ownsWritableStorage() -- the single predicate the upload setters branch on. False -> allocate fresh
    //                rather than write into storage that is absent, empty, or borrowed.
    void adopt(RefPtr<GlLoneTexture> tex) { textureValue = std::move(tex); borrowedFrom = ""; }
    void share(RefPtr<GlLoneTexture> tex, String from) { textureValue = std::move(tex); borrowedFrom = std::move(from); }
    void release() { textureValue.reset(); borrowedFrom = ""; }
    bool ownsWritableStorage() const { return textureValue && textureValue->textureId != 0 && !borrowed(); }

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

    // THE RESOLVER. Ask which face you are writing, or which you may read. Never reach for a texture or an
    // id directly. A single-faced surface answers `front` to both.
    Face& writeFace() { return writeToBack ? *back : front; }
    Face& readFace() { return (back && !writeToBack) ? *back : front; }
    Face const& writeFace() const { return writeToBack ? *back : front; }
    Face const& readFace() const { return (back && !writeToBack) ? *back : front; }

    // WHICH FACE IS THE WRITE FACE, as a value the bind cache can key on. `writeToBack` is private (the seal),
    // so GlPass -- a sibling nested type, not a friend of this one -- reaches it through here. swap() flips it;
    // that flip changes GlPass's (target, face, size) key and forces the rebind that the old `justSwapped`
    // bool used to force. Single-faced surfaces (everything in-tree) always answer false.
    bool writingBack() const { return writeToBack; }

    // Whether this surface can read what it writes -- i.e. has a second face. Existence IS the fact: this is
    // `back.isValid()` given a name, not a bool that could disagree with whether a second texture exists.
    bool doubled() const { return back.isValid(); }

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
    // the viewport went unset. Everything a consumer legitimately needs is public: the resolver
    // (writeFace/readFace), the size oracle, and the lifecycle (makeDoubled/resize/clearFaces). Verified:
    // NOTHING outside this struct reaches faces[], specifyStorage or allocateFace.
  private:
    // So this friend currently guards nothing -- it is a doorbell on a wall with no door, because
    // GlFrameBuffer is a private nested type and OpenGlRenderer is its own outer class. The seal is real only
    // for a genuine outsider; making it structural (moving these components to their own translation unit, at
    // which point this friend comes OUT) is step §8.ii of the finish plan.
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

    Face front;                     // the primary face -- present the moment the surface exists
    Maybe<Face> back;               // the second face. Its EXISTENCE is doubledness. There is no `bool doubled`
                                    // to disagree with whether a second texture is actually allocated -- and
                                    // that exact disagreement is how upstream's altId leaked back in: a face
                                    // the destructor knew about as a bool but not as a thing to free. Here the
                                    // fact and the storage are one object; you cannot have one without the other.
    bool writeToBack = false;       // which face draws land on. Only ever true while `back` exists.
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
  // Returns the internal format it specified -- the caller MUST record it on the texture.
  static GLint uploadTextureImage(PixelFormat pixelFormat, Vec2U size, uint8_t const* data);

  
  static RefPtr<GlLoneTexture> createGlTexture(ImageView const& image, TextureAddressing addressing, TextureFiltering filtering);
  // Mint an EMPTY GL texture object -- generated, bound, sampling params set, NO storage specified -- for a
  // caller that will glTexImage2D its own. setEffectTextureHalf and setEffectTextureR8 minted it identically,
  // 29 lines each; this is that allocator, once. It leaves GL_TEXTURE_2D bound to the new texture so the
  // caller's upload lands on it.
  static RefPtr<GlLoneTexture> createEmptyGlTexture(Vec2U size, TextureAddressing addressing, TextureFiltering filtering);

  shared_ptr<GlRenderBuffer> createGlRenderBuffer();

  void flushImmediatePrimitives(Mat3F const& transformation = Mat3F::identity());

  void renderGlBuffer(GlRenderBuffer const& renderBuffer, Mat3F const& transformation);


  void applyEffectParameter(EffectParameter* parameter, RenderEffectParameter const& value, String const& parameterName);

  void blitGlFrameBuffer(RefPtr<OpenGlRenderer::GlFrameBuffer> const& frameBuffer, bool const& useAlt = false);

  // The renderer's single door to the pass bind.
  void bindTarget(RefPtr<GlFrameBuffer> const& frameBuffer);

  // GlTargets -- owns which render targets exist.
  //
  // That is the whole duty. Not what they contain, not what draws into them, not how they are bound: just
  // which of them are alive, at what size, in the current config generation.
  //
  // The generation is the point. loadConfig destroys and rebuilds every target, so all their content becomes
  // UNDEFINED -- and a retained (clear:false) surface cannot see that, because its own refresh key (size,
  // camera, counter) is unchanged across the rebuild. It would happily composite garbage. So consumers fold
  // the generation into their refresh key.
  //
  // It used to be two statements a caller had to remember to pair:
  //     ++m_frameBufferGeneration;
  //     m_frameBuffers.clear();
  // Forget the first and a retained surface silently composites undefined GPU memory. Here they are ONE act,
  // because they ARE one act: destroying the set IS the event the generation exists to announce.
  struct GlTargets {
    // The tolerant lookup: null when absent. Callers that can degrade (the lighting passes fall back to CPU)
    // use this one.
    RefPtr<GlFrameBuffer> find(String const& id) const;
    // The strict lookup: throws when absent. Callers that cannot proceed without it use this one.
    RefPtr<GlFrameBuffer> get(String const& id) const;
    bool has(String const& id) const;
    uint64_t generation() const;

    // Destroy every target, and announce it. The bump cannot be forgotten because it is not a separate step.
    void destroyAll();
    void add(String const& name, Json const& config, Vec2U const& screenSize);

    // Operations over the whole set. Each was a hand-rolled loop reaching into the map.
    void resizeAll(Vec2U const& screenSize);
    void clearAll();                 // the per-frame clear, honouring each target's clear:false

  private:
    // Vestigial, like GlFrameBuffer's: verified that nothing outside GlTargets touches m_byId or
    // m_generation -- the oracle reaches targets through the public find(), not around it. Comes out at
    // §8.ii with the rest.
    friend class OpenGlRenderer;

    StringMap<RefPtr<GlFrameBuffer>> m_byId;
    uint64_t m_generation = 0;
  };
  GlTargets m_targets;

  Vec2U m_screenSize;

  // THE PASS: what is CURRENTLY bound -- one effect and one target -- for a sequence of draws.
  //
  // GL has exactly one current program and exactly one current draw framebuffer. Every draw reads BOTH, plus
  // the flattened attribute/uniform locations of that program. GlPass owns that ambient draw state as one
  // thing because it IS one thing: the pair is set together and read together. And the two are coupled --
  // binding an effect resolves and binds ITS declared framebuffer (and swaps its faces, if it reads what it
  // writes) -- so they cannot be owned by two components blind to each other without violating the Air-Gap.
  //
  // The coupling is not an obstacle to the decomposition. It IS a component, and nobody had written it.
  // switchEffectConfig, setRenderTarget, composite, blitGlFrameBuffer and setEffectTextureFromTarget are not
  // five problems: they are one missing Pass wearing several hats. A Pass depends DOWNWARD on effects and on
  // targets; neither of them knows the other exists.
  //
  // (The header once made a STRONGER claim for this component -- that bindTarget itself writes the current
  // program's screenSize uniform, so target-binding was also an effect operation. F3b.1 deleted that write as
  // dead at both call sites. The component stands on the pair-and-locations above, not on the retracted write.)
  //
  // THIS STEP MOVES THE STATE ONLY. The six functions follow, one at a time, each certified bit-identical
  // against the three GPU oracles -- because three of them carry real behaviour changes that must not ride
  // in on a refactor, or the oracle goes red and we cannot tell a fix from a regression.
  struct GlPass {
    // The flattened locations of the bound program. Read per draw, on the hot path.
    GLint positionAttribute = -1;
    GLint colorAttribute = -1;
    GLint texCoordAttribute = -1;
    GLint dataAttribute = -1;
    List<GLint> textureSizeUniforms = {};
    GLint screenSizeUniform = -1;
    GLint vertexTransformUniform = -1;

    // THE COUPLED PAIR. This is the whole reason the component exists. `target` == null means the screen
    // (framebuffer 0); a non-null target is the surface subsequent draws land on. Read externally (oracle
    // restores, config teardown), so it stays public.
    Effect* effect = nullptr;
    RefPtr<GlFrameBuffer> target;

    // Make `newTarget` the surface subsequent draws land on: bind its write face and set the viewport to that
    // face's size. A null newTarget binds the screen instead (see unbind). Keyed on (target, write-face,
    // viewport): it early-outs only when the cache proves all three already match, so a swap() (face flip) or
    // a same-target resize rebinds where the old identity-only key would have wrongly skipped. It writes NO
    // uniform -- that write was dead at both call sites and was deleted in F3b.1 (the .cpp explains why).
    void bindTarget(RefPtr<GlFrameBuffer> const& newTarget, Vec2U const& screenSize);

    // THE ONE screen-bind path: bind framebuffer 0 at the full-screen viewport, via bindTarget's null branch.
    // Replaces the two hand-rolled `target.reset(); glBindFramebuffer(0)` inverse-of-bindTarget sites. Public:
    // its callers (setRenderTarget / switchEffectConfig) are OpenGlRenderer methods, external to GlPass.
    void unbind(Vec2U const& screenSize) { bindTarget({}, screenSize); }

    // DROP THE CACHE without touching GL. loadConfig calls this: it destroys and rebuilds every target,
    // leaving GL_DRAW on a just-reallocated FBO while `target` becomes null -- so the cache would otherwise
    // read (screen, stale screenSize) and let the next screen unbind early-out over that corpse binding. The
    // {0,0} viewport sentinel matches no real screen or target size, forcing the next bind to emit real GL.
    void invalidate() { target = {}; boundWriteToBack = false; boundViewport = Vec2U(0, 0); }

    // Make `newEffect` the program that subsequent draws run: bind it, flatten its attribute and uniform
    // locations for the draw path to read, point its vertex-buffer samplers at their texture units, tell it
    // the screen size, and replay any scriptable values a script set while it was unbound.
    //
    // NO IDENTITY EARLY-OUT, deliberately. A reload erases an effect and re-emplaces it, and can hand back
    // the SAME address carrying a BRAND-NEW program -- an "already bound, skip" test here would leave the
    // pass holding the dead program's flattened locations. switchEffectConfig's early-out is a different
    // animal: it guards a whole target-bind and texture-rebind sequence, not this.
    void bindEffect(Effect& newEffect, Vec2U const& screenSize);

    // THE REST OF THE BIND KEY. A REAL seal, now, not §8.ii ceremony: nothing outside GlPass's own methods
    // reads these two fields, so -- unlike GlFrameBuffer / GlTargets / GlEffects, whose `friend class
    // OpenGlRenderer` re-opens their privates to the enclosing class -- GlPass deliberately adds NO friend.
    // The enclosing class has no special access to a nested type's privates (C++ [class.access.nest]), so
    // without the friend the seal genuinely holds: the renderer cannot poke boundViewport, which is the point.
  private:
    bool boundWriteToBack = false;  // which face of `target` GL currently draws into
    Vec2U boundViewport = {};       // the viewport GL currently has set (== screenSize when target is null)
  };
  GlPass m_pass;

  // GlEffects -- owns the compiled GPU programs. Which effects exist, what program each one compiled to, and
  // what a script has set on them. It binds nothing, draws nothing, and does not know a pass exists.
  //
  // THE REFERENCE DIES AT THE NEXT load(). StringMap is a FLAT, open-addressed hash map (StringMap -> HashMap
  // -> MapMixin<FlatHashMap>): every Effect is stored INLINE in a std::vector of buckets, so an insert that
  // rehashes MOVES all of them and an erase back-shifts. An `Effect&` or `Effect*` handed out here is valid
  // only until the next load().
  //
  // GlPass caches exactly one such pointer, and it survives only because loadEffectConfig re-binds it
  // immediately after every load -- an invariant that used to be an accident of a stray assignment buried in
  // the middle of the load path, and is now the line after it. Do not cache a second Effect*.
  struct GlEffects {
    // Compile, link, and register `name`, replacing whatever was under it. Throws RendererException when the
    // shaders will not compile or the program will not link.
    Effect& load(String const& name, Json const& config, StringMap<String> const& shaders);

    Effect* find(String const& name);   // null when absent

    // THE SCRIPTABLE SURFACE. Addressed BY NAME, on an effect that may not be the bound one -- that is the
    // whole point of it, and the reason it lives here and not on the pass. These write a CPU value and issue
    // no GL: the value reaches the GPU when GlPass::bindEffect next binds that effect.
    void setScriptable(String const& effectName, String const& parameterName, RenderEffectParameter const& value);
    Maybe<RenderEffectParameter> getScriptable(String const& effectName, String const& parameterName);
    Maybe<VariantTypeIndex> getScriptableType(String const& effectName, String const& parameterName);

    void destroyAll();   // delete every program, then forget them

    // GlTargets has just destroyed and rebuilt every render target. Point every sampler that was BORROWING a
    // target's texture at the new one of the same name -- or, if that target is gone from the config
    // entirely, let go of it. Without this the sampler holds an orphan: a live GL texture belonging to a
    // framebuffer that has been destroyed, which it will happily keep sampling.
    void rebindBorrows(GlTargets& targets);

  private:
    friend class OpenGlRenderer;
    StringMap<Effect> m_byName;
  };
  GlEffects m_effects;

  Json m_config;


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
