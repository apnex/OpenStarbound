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
class GpuLightmapPass {
public:
  explicit GpuLightmapPass(Renderer* renderer);

  // Runs the passthrough pass for `cpuLightmap` into the "lightingGpu" framebuffer, restores the
  // screen target + the "world" effect, and binds the result texture as the world "lightMap".
  // Takes an ImageView so the engine's Lightmap converts directly (as setEffectTexture does).
  // No-op for an empty lightmap.
  void process(ImageView const& cpuLightmap);

private:
  Renderer* m_renderer;
};

}

#endif
