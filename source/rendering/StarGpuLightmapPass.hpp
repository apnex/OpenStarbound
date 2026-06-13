#ifndef STAR_GPU_LIGHTMAP_PASS_HPP
#define STAR_GPU_LIGHTMAP_PASS_HPP

#include "StarRenderer.hpp"
#include "StarImage.hpp"

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
// deep-gated timer) and 'lighting.gpu.spread.passes' (Jacobi iterations K run).
class GpuLightmapPass {
public:
  explicit GpuLightmapPass(Renderer* renderer);

  // Runs K Jacobi spread iterations for the given emission + obstacle grids (same dimensions),
  // applies the brightnessLimit cap on the final pass, restores the screen target + "world"
  // effect, and binds the result as the world "lightMap". Returns false (doing nothing) for empty
  // inputs or if the GPU lighting assets are missing -- the caller then binds the CPU lightmap.
  // When shadowCompare is set, also reads the result back and returns it via `gpuResult` for the
  // caller's parity check (diagnostics only).
  bool processSpread(ImageView const& emission, ImageView const& obstacle, unsigned iterations,
      float spreadMaxAir, float spreadMaxObstacle, float brightnessLimit,
      bool shadowCompare = false, Image* gpuResult = nullptr);

private:
  Renderer* m_renderer;
};

}

#endif
