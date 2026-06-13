#include "StarGpuLightmapPass.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

bool GpuLightmapPass::process(ImageView const& cpuLightmap) {
  Vec2U size = cpuLightmap.size;
  if (size[0] == 0 || size[1] == 0)
    return false;

  // Bind the passthrough program. If it isn't loaded (e.g. out-of-date asset pack), bail without
  // touching the render target -- the caller falls back to the CPU lightmap path. This also keeps
  // us from reaching setRenderTarget("lightingGpu") when that framebuffer is likewise absent.
  if (!m_renderer->switchEffectConfig("lightingPassthrough"))
    return false;

  // Target the lightmap-sized off-screen float buffer (setRenderTarget resizes it + sets the
  // viewport and the screenSize uniform to `size`).
  m_renderer->setRenderTarget(String("lightingGpu"), size);
  m_renderer->setEffectTexture("inputTexture", cpuLightmap);

  // Fullscreen quad in the target's pixel space [0,size]; the vertex shader maps it to clip space
  // via the screenSize uniform and derives the [0,1] sample coordinate from the clip position.
  m_renderer->render(renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f));
  m_renderer->flush();

  // Restore the screen target + viewport and the world effect, then feed the result back as the
  // world shader's lightMap (the world effect's lightMapSize/Scale/Offset still apply unchanged,
  // since the target is exactly lightmap-sized).
  m_renderer->setRenderTarget({});
  m_renderer->switchEffectConfig("world");
  m_renderer->setEffectTextureFromTarget("lightMap", "lightingGpu");
  return true;
}

}
