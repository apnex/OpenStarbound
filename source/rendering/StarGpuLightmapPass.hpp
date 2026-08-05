#ifndef STAR_GPU_LIGHTMAP_PASS_HPP
#define STAR_GPU_LIGHTMAP_PASS_HPP

#include "StarRenderer.hpp"
#include "StarImage.hpp"
#include "StarCellularLightArray.hpp"   // PointParameters, ColoredCellularLightArray::PointLight

namespace Star {

STAR_CLASS(GpuLightmapPass);

// The explicit outcome of a lightmap pass: whether the GPU pass bound a lightMap this frame and, when it did,
// the calc-region border the bound lightMap carries (the padding between the calc region and the query region
// the world shader samples). Returned as ONE value so a bound lightMap can never desync from its border -- the
// caller applies both from a single result instead of a bool plus a separately-sourced border (the dark-world
// bug the WorldPainter lightMapOffset comment warns against, where the border was reverse-derived from an empty
// lightMap width).
struct LightmapResult {
  bool active = false;
  int border = 0;
  // The Jacobi K the pass actually chose. The pass DERIVES this from the emission it was handed (see
  // spreadIterationsFor), so it is the only thing that knows the value -- and the caller's parity
  // diagnostic needs the same K to build its reference. Reporting it back beats making the caller
  // recompute a number the pass already decided, which is exactly how the two would drift.
  unsigned spreadIterations = 0;
};

// AIR-GAP CONTRACT (2) FOR THE LIGHTMAP PASS: every knob the pass runs on, resolved ONCE by the
// composition root and handed in as a value. The same shape BackdropParams established, for the same
// reason -- a sovereign pass should be a pure function of its parameters.
//
// It replaces five loose trailing arguments plus a PointParameters in what had become a 13-parameter
// call, where transposing two floats compiles cleanly and yields a plausible wrong picture. Grouping
// them also makes the seam legible: everything in here is config the ORCHESTRATOR reads, and nothing
// else about the pass's behaviour comes from anywhere but its explicit inputs.
struct LightmapParams {
  PointParameters point;
  // Upper bound on the auto-scaled Jacobi iteration count (`lightingGpuSpreadIterations`). A cap of 0
  // disables the pass via the size/iterations early-out -- preserved deliberately, not incidentally.
  // Matches the declared default in StarRootLoader, which is authoritative -- WorldPainter always
  // supplies this field via getOrDefault, so this initializer governs only a construction that omits
  // it. It disagreed (64 vs 32) for as long as both existed; keeping them equal costs nothing.
  unsigned spreadIterationCap = 48;
  bool shadowCompare = false;
  float brightnessScale = 1.0f;
  bool tonemap = false;
  float worldUpscale = 1.0f;
};

// GPU lighting pass driver (render thread; the async lighting thread has no GL context).
//
// Slice 2: computes the lightmap SPREAD on the GPU via K parallel-Jacobi relaxation iterations
// (the lightingSpread effect), ping-ponging two float framebuffers, from uploaded emission +
// obstacle grids -- matching the CPU spread (spreadJacobiReference is the oracle). Restores the
// screen target + the "world" effect and binds the result as the world "lightMap". Point lighting
// is NOT yet on the GPU (Slice 3), so this is correct only in spread-only scenes.
//
// Emits telemetry: 'lighting.gpu.cpu_cost.us' (render-thread CPU cost of driving the pass, a
// deep-gated timer) and 'lighting.gpu.spread.passes' (Jacobi iterations K run). Also registers the
// point-pass counters 'lighting.gpu.point.lights' (point lights drawn per frame) and
// 'lighting.gpu.point.mismatch' (parity mismatches); these are driven later (Task 4/5).
class GpuLightmapPass {
public:
  explicit GpuLightmapPass(Renderer* renderer);

  // Computes the COMPLETE lightmap on the GPU: spreadIterations Jacobi spread passes (no cap) from
  // emission+obstacle, then one blended per-light-quad point pass on top (additive or GL_MAX per
  // params.pointAdditive), then a brightnessLimit cap-compose; restores the screen target + "world"
  // effect and binds the result as the world "lightMap". emission/obstacle are calc-region grids
  // (ImageView so Lightmap converts directly); lights are array-relative.
  // Returns a LightmapResult with active=false (doing nothing) for empty inputs or if the GPU lighting
  // assets are missing -- the caller then binds the CPU lightmap (fail-forward: never crash the frame). On
  // success returns {active=true, border=lightMapBorder}, echoing the caller's calc-region border WITH the
  // active flag so the two can never be paired incorrectly. When shadowCompare is set, reads the final result
  // back into `gpuResult` for the caller's parity check (diagnostics).
  // emissionHalf is the emission grid pre-converted to 16-bit half-floats (RGB packed) on the lighting
  // thread; when it matches emission's texel count it is uploaded as RGB16F (half the bytes), else the
  // RGB_F emission is uploaded as a fallback.
  // obstacleR8 is the obstacle mask as single-channel bytes; when it matches the texel count it is
  // uploaded as R8 (a third the bytes of the RGB24 obstacle), else the RGB24 obstacle is the fallback.
  LightmapResult processFull(ImageView const& emission, List<uint16_t> const& emissionHalf,
      ImageView const& obstacle, List<uint8_t> const& obstacleR8,
      List<ColoredCellularLightArray::PointLight> const& lights,
      LightmapParams const& lp, Image* gpuResult = nullptr, int lightMapBorder = 0);

private:
  // Auto-scale the spread Jacobi iterations to the emission's peak intensity.
  //
  // THIS LIVED IN WorldPainter UNTIL #137, and had no business there: it is an O(cells) scan over the
  // pass's own first argument, producing a number only the pass consumes. Its presence in the
  // orchestrator is most of why this pass could show a clean per-file singleton count while the work
  // it needed sat one level up -- "bought, not earned".
  //
  // Production's Gauss-Seidel sweep propagates fully in 2 sweeps; a parallel Jacobi needs
  // ~ceil(maxIntensity * spreadMaxAir) steps to reach the same distance (the de-risk's K bound). A fixed
  // K under-propagated bright (>1.0) FU spread lights, giving dimmer-far-from-source cells. So: scan the
  // (small) emission for its max channel, clamp to [8, configured cap].
  static unsigned spreadIterationsFor(ImageView const& emission, LightmapParams const& lp);

  Renderer* m_renderer;

  // Persistent full-quad geometry: the size-covering rect drawn by every spread iteration and the
  // compose pass. Built once via set() when `size` changes, then replayed with renderBuffer() —
  // instead of rebuilding the identical 144-byte VBO through the immediate buffer on each of the
  // ~spreadIterations+1 draws. Byte-identical: same VBO contents, same draw order/target/effect.
  RenderBufferPtr m_fullQuadBuffer;
  // Emission repacked as RGBA16F with the obstacle flag in alpha (J-2). Kept as a member so the per-recompute
  // repack reuses its storage instead of reallocating a ~1MB buffer at the lighting cadence.
  List<uint16_t> m_emissionRGBA;
  Vec2U m_fullQuadSize;
};

}

#endif
