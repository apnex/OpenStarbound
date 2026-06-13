#ifndef STAR_GPU_LIGHTMAP_PASS_HPP
#define STAR_GPU_LIGHTMAP_PASS_HPP

#include "StarRenderer.hpp"
#include "StarImage.hpp"

namespace Star {

STAR_CLASS(GpuLightmapPass);

// Slice-1 GPU plumbing spike. Rounds the CPU-computed lightmap through one passthrough GPU pass
// into a float off-screen target and binds the result as the world shader's "lightMap" sampler.
// Identity by construction -- a visually identical screen with the pass enabled proves the whole
// upload -> off-screen float pass -> consume chain on the GPU. Slices 2-4 replace the passthrough
// with the real spread / point / compose passes; the drive sequence here is the scaffold.
//
// Runs on the render thread (the async lighting thread has no GL context). Same-thread only.
//
// Emits telemetry: 'lighting.gpu.cpu_cost.us' (render-thread CPU cost of driving the pass, a
// deep-gated timer) and 'lighting.gpu.spread.passes' (spread iterations run; placeholder of 1 per
// passthrough call until Task 4 makes it the K Jacobi iterations).
class GpuLightmapPass {
public:
  explicit GpuLightmapPass(Renderer* renderer);

  // Runs the passthrough pass for `cpuLightmap` into the "lightingGpu" framebuffer, restores the
  // screen target + the "world" effect, and binds the result texture as the world "lightMap".
  // Takes an ImageView so the engine's Lightmap converts directly (as setEffectTexture does).
  // Returns false (doing nothing) for an empty lightmap or if the GPU lighting assets are missing
  // -- the caller must then bind the CPU lightmap itself (fail-forward: never crash the frame).
  bool process(ImageView const& cpuLightmap);

private:
  Renderer* m_renderer;
};

}

#endif
