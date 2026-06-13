#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

bool GpuLightmapPass::process(ImageView const& cpuLightmap) {
  // Telemetry handles (by-value static idiom: registered once on first call, then lock-free).
  // The scope times the whole body -- the render-thread CPU cost of driving the GPU lighting pass
  // (deep-gated; records only under deep tracing). The counter tracks spread iterations run.
  static auto cpuCostTimer = Telemetry::timer("lighting.gpu.cpu_cost.us");
  static auto spreadPasses = Telemetry::counter("lighting.gpu.spread.passes");
  TelemetryScope cpuCostScope(cpuCostTimer);

  Vec2U size = cpuLightmap.size;
  if (size[0] == 0 || size[1] == 0)
    return false;

  // Bind the passthrough program. If it isn't loaded (e.g. out-of-date asset pack), bail without
  // touching the render target -- the caller falls back to the CPU lightmap path. This also keeps
  // us from reaching setRenderTarget("lightingGpu") when that framebuffer is likewise absent.
  if (!m_renderer->switchEffectConfig("lightingPassthrough"))
    return false;

  // The spread pass is running. The Slice-1 passthrough is a single pass, so bump by 1 here as a
  // placeholder hook; Task 4 replaces the passthrough with the K-iteration Jacobi ping-pong and
  // sets this to the iteration count K (inc(K)).
  spreadPasses.inc(1);

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
