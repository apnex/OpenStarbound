#pragma once

#include "StarTextureAtlas.hpp"
#include "StarRenderer.hpp"
#include "StarGlRenderSurface.hpp"
#include "StarGlTexturePrimitives.hpp"
#include "StarTelemetry.hpp"   // TelemetryCounter, held by value for the GL-state audit (#139)

#include "GL/glew.h"

namespace Star {

STAR_CLASS(OpenGlRenderer);

constexpr size_t FrameBufferCount = 1;


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

  // PUBLIC because the renderer does not own the whole frame. ImGui renders AFTER finishFrame() (see
  // StarMainApplication_sdl), so anything it raises is invisible to the renderer's own end-of-frame drain
  // and gets mis-attributed to the NEXT frame -- the GL error flag persists until read. Whoever owns the
  // frame ordering must be able to drain at the true end of it. Returns true if any error was drained.
  static bool logGlErrorSummary(String prefix);

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

  // GlTexture and GlLoneTexture now live in StarGlTexturePrimitives.hpp (Tier 1 of the §8.ii extraction) --
  // GlLoneTexture is atlas-free and self-contained, so the render-surface substrate depends on it there
  // without depending on this whole class. GlGroupedTexture stays here because it IS the atlas coupling.
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


  


  // Specifies an image's storage AND records the descriptor on `record` (if given), in the same call as the
  // glTexImage2D -- so an effect-image spec cannot leave the descriptor unwritten. The raw-GLuint atlas caller
  // passes record=nullptr (no descriptor). Still returns the internal format for callers that want it.
  static GLint uploadTextureImage(PixelFormat pixelFormat, Vec2U size, uint8_t const* data, GlLoneTexture* record = nullptr);

  // THE one home of the "re-spec vs sub-upload" decision for a lone texture: the guard, the glTex{Sub}Image2D,
  // and the whole-descriptor record are ONE indivisible act, so no numeric setter writes its own guard. That is
  // what makes RB-6 unreintroducible from a call site: setEffectTextureR8's original size-only guard (which let
  // a GL_RED upload land in RGB storage forever) is now unrepresentable here -- the guard tests size AND format,
  // always, because there is only one guard. `fresh` (a just-allocated texture, internalFormat still 0) forces
  // the re-spec. Callers own the glPixelStorei/glBindTexture before calling; this touches only storage + record.
  static void uploadLoneStorage(GlLoneTexture& tex, Vec2U size, GLint internalFormat, GLenum format, GLenum type,
      void const* data, bool fresh);


  static RefPtr<GlLoneTexture> createGlTexture(ImageView const& image, TextureAddressing addressing, TextureFiltering filtering);
  // Mint an EMPTY GL texture object -- generated, bound, sampling params set, NO storage specified -- for a
  // caller that will glTexImage2D its own. setEffectTextureHalf and setEffectTextureR8 minted it identically,
  // 29 lines each; this is that allocator, once. It leaves GL_TEXTURE_2D bound to the new texture so the
  // caller's upload lands on it.
  static RefPtr<GlLoneTexture> createEmptyGlTexture(Vec2U size, TextureAddressing addressing, TextureFiltering filtering);

  shared_ptr<GlRenderBuffer> createGlRenderBuffer();

  void flushImmediatePrimitives(Mat3F const& transformation = Mat3F::identity());

  void renderGlBuffer(GlRenderBuffer const& renderBuffer, Mat3F const& transformation);


  // The one home of the effect-parameter type-name ladder. Parses a default of the named GLSL type ("bool" ..
  // "vec4") into a typed RenderEffectParameter, throwing on an unrecognized type. `def` may be an unset Json
  // (no "default" key): then the type's zero value is returned, so parameterType is still derivable from it.
  static RenderEffectParameter parseEffectParameter(String const& type, Json const& def);

  void applyEffectParameter(EffectParameter* parameter, RenderEffectParameter const& value, String const& parameterName);

  void blitGlSurface(RefPtr<GlSurface> const& frameBuffer, bool const& useAlt = false);

  // The renderer's single door to the pass bind.
  void bindTarget(RefPtr<GlSurface> const& frameBuffer);

  // END-OF-RENDER GL-STATE AUDIT (#139 phase 1b). Asks GL what is actually bound and compares it against what
  // the renderer believes, then counts and reports any disagreement. Called unconditionally from
  // finishFrame(); the rationale for unconditional lives there. Returns the number of disagreements.
  unsigned auditGlState();

  // Rate limit for the [glstate] diagnostic. A desync is typically per-frame and persistent, so an unlimited
  // logger would write a line every frame forever and bury the FIRST occurrence -- the only one whose
  // surrounding log lines say what caused it. The COUNTER below is never rate-limited.
  int m_glStateReportBudget = 8;

  // Registered in the constructor, never lazily inside the audit. A counter created inside a block that only
  // runs when something is wrong makes ABSENT indistinguishable from ZERO to a snapshot-differencing
  // consumer -- the exact defect #181 closed for the backdrop's contract-violation counter.
  TelemetryCounter m_glStateMismatches;

  GlTargets m_targets;

  Vec2U m_screenSize;

  GlPass m_pass;

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

    void begin(String const& name, MetricDesc const& desc) override;
    void end(String const& name) override;
    Maybe<int64_t> lastMicros(String const& name) const override;

  private:
    // A slot is polled when it comes round again, i.e. after RingDepth further begins on the same scope.
    // At depth 3 a per-frame scope got ~50ms at 60fps, and the GPU is routinely more than three frames
    // behind: measured drop rates were 33% for the frame-cadence scopes and 74% for parallax. Depth 16
    // buys ~266ms, which is past any plausible pipeline depth. The drop is NOT random -- see begin() --
    // so the cost of an undersized ring is a biased metric, not a few missing samples.
    static constexpr unsigned RingDepth = 16;
    struct Ring {
      GLuint queries[RingDepth] = {};
      bool issued[RingDepth] = {};
      unsigned writeIdx = 0;
      // The handle the readback records through, resolved at begin() rather than at the readback. That is
      // what makes the key EXIST from the first bracketed frame instead of from the first query that
      // happens to resolve -- see GlGpuTimer::begin.
      TelemetryTimer timer;
      // PER KEY, because "some samples were suppressed" is not actionable and "this key lost N" is. The
      // nesting guard used to only warn, so a timer whose every sample the guard rejected reported a count
      // that was neither frames nor calls, and nothing said which timer or how many. See begin().
      TelemetryCounter nested;
      // RESOLVED ONCE, NOT PER BEGIN (R15). Re-resolving it rebuilt the key with `name + ".nested"` --
      // a heap allocation per pass per frame on the render path, which StarMetricDesc.hpp forbids in as
      // many words and for this exact reason. The TIMER above is deliberately still re-resolved every
      // begin: two call sites may name one key, and descConflict is raised only by a call that PASSES a
      // desc. The nested counter carries one descriptor everywhere, so it has nothing to drift against
      // and nothing to detect -- the asymmetry is the point, not an oversight.
      bool nestedResolved = false;
    };

    function<void()> m_flushPending;
    StringMap<Ring> m_rings;
    StringMap<int64_t> m_lastMicros; // last read-back µs per scope (for the /debug HUD)
    Ring* m_current = nullptr;
    unsigned m_slot = 0;
    bool m_active = false;
  };

  // Pixel read-back for bit-identity certification. Unlike the timer, this genuinely needs the renderer's
  // framebuffer set, so it holds a concrete back-reference to OpenGlRenderer -- a one-way coupling internal to
  // the backend. That is a diagnostic seam, not a hole in the seal: no Layer-1 component re-opens its privates
  // to the renderer any more (the compile proved none needed to). The gap that matters is the one the twelve
  // consumers see through the Renderer interface, and that one is real: they get three methods and cannot name
  // a framebuffer face.
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
  // Depth 16 for the same reason as GlGpuTimer::RingDepth, and it matters MORE here: this span is the
  // owner's WHOLE, so under-capturing it makes the sum of parts exceed 100% and reads as a double-count.
  // At depth 3 it captured 67% of frames while the parts captured 100%, and the accounting oracle reported
  // 121.6% -- parts correct, whole short. Both ends of a ratio have to be measured the same way.
  static constexpr unsigned GpuTimerRingSize = 16;
  struct FrameSpanRing {
    GLuint begins[GpuTimerRingSize] = {};
    GLuint ends[GpuTimerRingSize] = {};
    bool issued[GpuTimerRingSize] = {};
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
