#ifndef STAR_GPU_LIGHTMAP_PASS_HPP
#define STAR_GPU_LIGHTMAP_PASS_HPP

#include "StarRenderer.hpp"
#include "StarImage.hpp"
#include "StarCellularLightArray.hpp"   // PointParameters, ColoredCellularLightArray::PointLight

namespace Star {

STAR_CLASS(GpuLightmapPass);

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
  // Returns false (doing nothing) for empty inputs or if the GPU lighting assets are missing --
  // the caller then binds the CPU lightmap (fail-forward: never crash the frame). When shadowCompare
  // is set, reads the final result back into `gpuResult` for the caller's parity check (diagnostics).
  // emissionHalf is the emission grid pre-converted to 16-bit half-floats (RGB packed) on the lighting
  // thread; when it matches emission's texel count it is uploaded as RGB16F (half the bytes), else the
  // RGB_F emission is uploaded as a fallback.
  // obstacleR8 is the obstacle mask as single-channel bytes; when it matches the texel count it is
  // uploaded as R8 (a third the bytes of the RGB24 obstacle), else the RGB24 obstacle is the fallback.
  // captureSpread (dirty-REGION Stage 2): after the spread loop and BEFORE the point pass, copy the
  // pure-spread result (lastTarget) into the persistent `lightingSpreadPersist` buffer (persistS) so
  // the NEXT frame's partial path can hold it as a fixed Dirichlet boundary. It is a read-only copy of
  // lastTarget into a separate buffer, so the visible output is byte-identical to captureSpread=false.
  bool processFull(ImageView const& emission, List<uint16_t> const& emissionHalf,
      ImageView const& obstacle, List<uint8_t> const& obstacleR8,
      List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
      PointParameters const& params, float brightnessScale = 1.0f, bool tonemap = false,
      bool shadowCompare = false, Image* gpuResult = nullptr, bool captureSpread = false);

  // dirty-REGION Stage 2: PARTIAL spread. Re-relax ONLY the dilated dirty-rect interior (seeded
  // from-below by current emission, +1 ring held fixed from persistS), write the fresh interior back
  // into persistS, materialize the full-grid spread, then run the SAME full point+compose+bind tail.
  // interior/ring are calc-region FBO pixel rects (exclusive max). Byte-identity vs processFull on the
  // same inputs is what the output oracle proves; off by default. Returns false on empty/missing inputs.
  bool processPartial(ImageView const& emission, List<uint16_t> const& emissionHalf,
      ImageView const& obstacle, List<uint8_t> const& obstacleR8,
      List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
      PointParameters const& params, RectI const& interior, RectI const& ring,
      float brightnessScale = 1.0f, bool tonemap = false);

  // dirty-REGION Stage 2 output oracle (lightingDirtyRegionValidate): dual-run partial-vs-full and
  // exact-compare the composed lightmap (GPU-vs-GPU, zero tolerance, ±0/NaN float fallback). The
  // authoritative FULL runs LAST and total-overwrites persistS+persistL and ends with the world bind,
  // so the visible frame is always the full result (no snapshot/restore needed at Stage 2). selfCheck
  // (lightingDirtyRegionSelfCheck) runs full-vs-full once to validate the oracle before trusting partial.
  bool processValidateDirtyRegion(ImageView const& emission, List<uint16_t> const& emissionHalf,
      ImageView const& obstacle, List<uint8_t> const& obstacleR8,
      List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
      PointParameters const& params, RectI const& interior, RectI const& ring,
      bool forceFull, bool selfCheck, float brightnessScale = 1.0f, bool tonemap = false,
      Image* gpuResult = nullptr);

private:
  // Shared point+compose+bind tail (extracted from processFull so processFull and processPartial run
  // BYTE-IDENTICAL bytes): blended per-light point quads onto the spread in `lastTarget`, then the
  // brightnessLimit cap-compose into persistL, then restore the screen target + "world" effect and
  // bind persistL as the world lightMap. lastTarget holds the full-grid spread for both callers.
  void composeTail(Vec2U size, float w, float h, char const* lastTarget,
      ImageView const& obstacle, List<uint8_t> const& obstacleR8,
      List<ColoredCellularLightArray::PointLight> const& lights, PointParameters const& params,
      float brightnessScale, bool tonemap, bool shadowCompare, Image* gpuResult);

  Renderer* m_renderer;
  // dirty-REGION Stage 2 output oracle: set true once a full-vs-full self-check has read back identical
  // (proves the readback/compare oracle is sound). While selfCheck is enabled and this is false, the
  // validate path runs ONLY the self-check and does NOT count/trust the partial-vs-full comparison.
  bool m_dirtyRegionSelfCheckPassed = false;
};

}

#endif
