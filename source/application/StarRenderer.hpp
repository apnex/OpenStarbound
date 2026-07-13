#pragma once

#include "StarVariant.hpp"
#include "StarImage.hpp"
#include "StarPoly.hpp"
#include "StarJson.hpp"
#include "StarBiMap.hpp"
#include "StarRefPtr.hpp"

namespace Star {

STAR_EXCEPTION(RendererException, StarException);

class Texture;
typedef RefPtr<Texture> TexturePtr;

STAR_CLASS(TextureGroup);
STAR_CLASS(RenderBuffer);
STAR_CLASS(Renderer);

enum class TextureAddressing {
  Clamp,
  Wrap
};
extern EnumMap<TextureAddressing> const TextureAddressingNames;

enum class TextureFiltering {
  Nearest,
  Linear
};
extern EnumMap<TextureFiltering> const TextureFilteringNames;

// Medium is the maximum guaranteed texture group size
// Where a Medium sized texture group is expected to fill a single page Large can be used,
// but is not guaranteed to be supported by all systems.
// Where Large sized textures are not supported, a Medium one is used
enum class TextureGroupSize {
  Small,
  Medium,
  Large
};

// Both screen coordinates and texture coordinates are in pixels from the
// bottom left to top right.
struct RenderVertex {
  Vec2F screenCoordinate;
  Vec2F textureCoordinate;
  Vec4B color;
  float param1;
};

class RenderTriangle {
public:
  RenderTriangle() = default;
  RenderTriangle(Vec2F posA, Vec2F posB, Vec2F posC, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
  RenderTriangle(TexturePtr tex, Vec2F posA, Vec2F uvA, Vec2F posB, Vec2F uvB, Vec2F posC, Vec2F uvC, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);

  TexturePtr texture;
  RenderVertex a, b, c;
};

class RenderQuad {
public:
  RenderQuad() = default;
  RenderQuad(Vec2F posA, Vec2F posB, Vec2F posC, Vec2F posD, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
  RenderQuad(TexturePtr tex, Vec2F minScreen, float textureScale = 1.0f, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
  RenderQuad(TexturePtr tex, RectF const& screenCoords, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
  RenderQuad(TexturePtr tex, Vec2F posA, Vec2F uvA, Vec2F posB, Vec2F uvB, Vec2F posC, Vec2F uvC, Vec2F posD, Vec2F uvD, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
  RenderQuad(TexturePtr tex, RenderVertex vA, RenderVertex vB, RenderVertex vC, RenderVertex vD);
  RenderQuad(RectF const& rect, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);

  TexturePtr texture;
  RenderVertex a, b, c, d;
};

class RenderPoly {
public:
  RenderPoly() = default;
  RenderPoly(List<Vec2F> const& verts, Vec4B color, float param1 = 0.0f);

  TexturePtr texture;
  List<RenderVertex> vertexes;
};

RenderQuad renderTexturedRect(TexturePtr texture, Vec2F minScreen, float textureScale = 1.0f, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
RenderQuad renderTexturedRect(TexturePtr texture, RectF const& screenCoords, Vec4B color = Vec4B::filled(255), float param1 = 0.0f);
RenderQuad renderFlatRect(RectF const& rect, Vec4B color, float param1 = 0.0f);
RenderPoly renderFlatPoly(PolyF const& poly, Vec4B color, float param1 = 0.0f);

typedef Variant<RenderTriangle, RenderQuad, RenderPoly> RenderPrimitive;

class Texture : public RefCounter {
public:
  virtual ~Texture() = default;

  virtual Vec2U size() const = 0;
  virtual TextureFiltering filtering() const = 0;
  virtual TextureAddressing addressing() const = 0;
};

// Textures may be created individually, or in a texture group.  Textures in
// a texture group will be faster to render when rendered together, and will
// use less texture memory when many small textures are in a common group.
// Texture groups must all have the same texture parameters, and will always
// use clamped texture addressing.
class TextureGroup {
public:
  virtual ~TextureGroup() = default;

  virtual TextureFiltering filtering() const = 0;
  virtual TexturePtr create(Image const& texture) = 0;
};

class RenderBuffer {
public:
  virtual ~RenderBuffer() = default;

  // Transforms the given primitives into a form suitable for the underlying
  // graphics system and stores it for fast replaying.
  virtual void set(List<RenderPrimitive>& primitives) = 0;
};

typedef Variant<float, int, Vec4F, Vec3F, Vec2F, bool> RenderEffectParameter;

// Blend mode for the current draw target. Alpha is the engine default; Additive (dest += src)
// and Max (dest = max(src,dest)) drive the GPU point-lighting accumulation (both order-independent
// so they match the CPU's per-light additive / max blend).
// PremultiplyInto: render straight-alpha source draws INTO an intermediate as premultiplied
// (rgb = a*src + (1-a)*dst, alpha = a + (1-a)*dst_a) -- glBlendFuncSeparate. PremultipliedOver:
// composite a premultiplied intermediate over a destination (rgb + (1-a)*dst). Together they cache an
// alpha-blended layer (e.g. parallax) for later compositing byte-identically (premultiplied "over" is
// associative, unlike straight "over").
enum class BlendMode { Alpha, Additive, Max, PremultiplyInto, PremultipliedOver };

class Renderer {
public:
  virtual ~Renderer() = default;

  virtual String rendererId() const = 0;
  virtual Vec2U screenSize() const = 0;

  virtual void loadConfig(Json const& config) = 0;

  // The actual shaders used by this renderer will be in a default no effects
  // state when constructed, but can be overridden here.  This config will be
  // specific to each type of renderer, so it will be necessary to key the
  // configuration off of the renderId string.  This should not be called every
  // frame, because it will result in a recompile of the underlying shader set.
  virtual void loadEffectConfig(String const& name, Json const& effectConfig, StringMap<String> const& shaders) = 0;

  // The effect config will specify named parameters and textures which can be
  // set here.
  virtual void setEffectParameter(String const& parameterName, RenderEffectParameter const& parameter) = 0;
  // Handle-based fast path for the same-uniform hot loops (e.g. GPU point lighting): resolve the name
  // ONCE against the current effect via getEffectParameterHandle, then set by handle each iteration,
  // skipping the per-call name String-construct + hash + HashMap lookup. The handle is valid only while
  // the resolving effect stays current (switchEffectConfig invalidates it) and no parameters are added.
  // A null handle is a no-op on set (mirrors the string path's unknown-name early-out). Values pushed
  // and the dedup early-out are identical to the string path.
  typedef void* EffectParameterHandle;
  virtual EffectParameterHandle getEffectParameterHandle(String const& parameterName) = 0;
  virtual void setEffectParameter(EffectParameterHandle handle, RenderEffectParameter const& parameter) = 0;
  virtual void setEffectScriptableParameter(String const& effectName, String const& parameterName, RenderEffectParameter const& parameter) = 0;
  virtual Maybe<RenderEffectParameter> getEffectScriptableParameter(String const& effectName, String const& parameterName) = 0;
  virtual Maybe<VariantTypeIndex> getEffectScriptableParameterType(String const& effectName, String const& parameterName) = 0;
  virtual void setEffectTexture(String const& textureName, ImageView const& image) = 0;
  virtual bool switchEffectConfig(String const& name) = 0;

  // Off-screen multi-pass support (e.g. GPU lighting). setRenderTarget binds the named config
  // framebuffer as the draw target, (re)sizing its color texture to `size` and matching the
  // viewport + screenSize uniform; an empty Maybe restores the screen target and full viewport.
  // setEffectTextureFromTarget binds a config framebuffer's color texture to the current effect's
  // named sampler, consuming a prior pass's output with no CPU round-trip.
  virtual void setRenderTarget(Maybe<String> const& frameBufferId, Vec2U size = Vec2U()) = 0;
  // Clear the currently-bound render target (see setRenderTarget) to the renderer's clear color. Used by
  // persistent clear:false FBOs (e.g. the environment cache) to reset to the once-per-frame clear state
  // that clear:true targets like "main" receive in startFrame.
  virtual void clearRenderTarget(Vec4F clearColor = Vec4F(0.0f, 0.0f, 0.0f, 1.0f)) = 0;
  // Debug/validate oracle: read back two config framebuffers' color and count per-pixel differences.
  // Returns {differing pixel count, first differing pixel {x,y}}; {NPos, {}} if a buffer is absent or the
  // two sizes differ. Offline only (glReadPixels stalls the pipeline). Backs the env-cache bit-identity
  // oracle and future retained-surface caches (#133).
  // maxAbsDiff (optional out): the largest per-channel |a-b| over all differing pixels. Lets a caller
  // distinguish an exact match / sub-LSB double-rounding (e.g. the parallax cache, ~1 ULP) from a real
  // divergence. 0 when the buffers match.
  virtual pair<size_t, Vec2U> compareFrameBuffers(String const& a, String const& b, float* maxAbsDiff = nullptr) = 0;
  // True if a config framebuffer with this id is loaded. Lets a consumer guard a setRenderTarget redirect
  // (which silently no-ops on an absent id) so it never accidentally draws into the previously-bound target.
  virtual bool hasFrameBuffer(String const& id) const = 0;
  // Bumped whenever every framebuffer is destroyed and re-created with UNDEFINED content -- i.e. on any
  // renderer config reload (the hdr and antiAliasing client options each trigger one, and both are polled
  // every frame). A retained/persistent (clear:false) surface CANNOT see this: its own refresh key (size,
  // camera, counter) is unchanged across the realloc, so it would happily composite undefined GPU memory.
  // Any such consumer MUST fold this into its refresh key. Not pure: a backend that never reallocates
  // correctly reports a constant, and is thereby never falsely invalidated.
  virtual uint64_t frameBufferGeneration() const { return 0; }
  // Arm/disarm this frame's clears of framebuffers marked "clearGated" in config. Such FBOs (e.g. the
  // env-cache oracle's envRef reference) are startFrame-cleared like any clear:true target only while their
  // debug/optional consumer has armed them, so they cost nothing (no per-frame clear) when that consumer is off.
  virtual void setGatedFrameBufferClears(bool active) = 0;
  // Sample srcFbo's color (bound to `effect`'s `srcSampler`) through `effect` into `dstFbo`, drawn as a
  // full-screen quad sized to dstSize, after applying `params`. Returns false if `effect` is unregistered
  // (caller falls back). Sets `params` explicitly so a shared passthrough effect is bleed-safe across
  // consumers with different needs. Does NOT restore the prior effect/target (caller-specific).
  virtual bool composite(String const& effect, String const& dstFbo, Vec2U dstSize,
                         String const& srcSampler, String const& srcFbo,
                         List<pair<String, RenderEffectParameter>> const& params = {}) = 0;
  virtual void setEffectTextureFromTarget(String const& textureName, String const& frameBufferId) = 0;
  // Alias one effect sampler to another's already-uploaded texture (no CPU re-upload). Used to feed
  // a grid that was uploaded once (e.g. GPU lighting's emission) to a second sampler that needs the
  // same data (the iteration-0 spread state) instead of uploading it twice.
  virtual void setEffectTextureAlias(String const& destTextureName, String const& sourceTextureName) = 0;
  // Upload pre-converted 16-bit half-float RGB data to an effect sampler as an RGB16F texture, halving
  // the per-frame transfer/store vs RGB_F. Used for GPU lighting's emission grid (the pipeline's FBOs
  // are already 16F, so no precision is lost); the float->half conversion is done off the render thread.
  virtual void setEffectTextureHalfRGB(String const& textureName, Vec2U size, uint16_t const* halfData) = 0;
  // Upload single-channel 8-bit data to an effect sampler as an R8 texture (sampled via .r), a third
  // the bytes of RGB24. Used for GPU lighting's obstacle mask (a binary 0/255 flag, read as .r > 0.5).
  virtual void setEffectTextureR8(String const& textureName, Vec2U size, uint8_t const* data) = 0;
  // Read a config framebuffer's color texture back to a CPU RGB_F image (diagnostics, e.g. GPU
  // lighting parity shadow-compare). Returns an empty image if the framebuffer is absent.
  virtual Image readFrameBuffer(String const& frameBufferId) = 0;
  // Set the blend mode for subsequent draws (e.g. additive/max for GPU point-light accumulation);
  // restore to BlendMode::Alpha after. Flushes pending primitives so the mode applies cleanly.
  virtual void setBlendMode(BlendMode mode) = 0;

  // Any further rendering will be scissored based on this rect, specified in
  // pixels
  virtual void setScissorRect(Maybe<RectI> const& scissorRect) = 0;

  virtual TexturePtr createTexture(Image const& texture,
      TextureAddressing addressing = TextureAddressing::Clamp,
      TextureFiltering filtering = TextureFiltering::Nearest) = 0;
  virtual void setSizeLimitEnabled(bool enabled) = 0;
  virtual void setMultiTexturingEnabled(bool enabled) = 0;
  virtual void setMultiSampling(unsigned multiSampling) = 0;
  virtual void setMainHDR(bool enabled) = 0;
  virtual TextureGroupPtr createTextureGroup(TextureGroupSize size = TextureGroupSize::Medium, TextureFiltering filtering = TextureFiltering::Nearest) = 0;
  virtual RenderBufferPtr createRenderBuffer() = 0;

  virtual List<RenderPrimitive>& immediatePrimitives() = 0;
  virtual void render(RenderPrimitive primitive) = 0;
  virtual void renderBuffer(RenderBufferPtr const& renderBuffer, Mat3F const& transformation = Mat3F::identity()) = 0;

  virtual void flush(Mat3F const& transformation = Mat3F::identity()) = 0;

  // GPU-side timer queries. Bracket GPU work to measure its on-GPU execution time
  // (e.g. GL_TIME_ELAPSED). begin/end flush pending primitives so the query spans exactly
  // the enclosed draws; results are read back asynchronously and recorded to the named
  // Telemetry timer. Default no-op; backends gate issuance on Telemetry::deepEnabled().
  // Calls must be paired and non-nested.
  virtual void beginGpuTimer(String const& name) { (void)name; }
  virtual void endGpuTimer(String const& name) { (void)name; }

  // Last GPU-timer result (microseconds) for a named scope, if one has been read back.
  // Default none; backends that implement GPU timers return the most recent sample.
  virtual Maybe<int64_t> gpuTimerLastMicros(String const& name) const { (void)name; return {}; }
};

}
