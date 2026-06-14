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
  bool processFull(ImageView const& emission, List<uint16_t> const& emissionHalf, ImageView const& obstacle,
      List<ColoredCellularLightArray::PointLight> const& lights, unsigned spreadIterations,
      PointParameters const& params, float brightnessScale = 1.0f,
      bool shadowCompare = false, Image* gpuResult = nullptr);

private:
  Renderer* m_renderer;
};

}

#endif
