#include "StarGpuLightmapPass.hpp"
#include "StarTelemetry.hpp"
#include "StarMathCommon.hpp"

namespace Star {

GpuLightmapPass::GpuLightmapPass(Renderer* renderer) : m_renderer(renderer) {}

bool GpuLightmapPass::processSpread(ImageView const& emission, ImageView const& obstacle, unsigned iterations,
    float spreadMaxAir, float spreadMaxObstacle, float brightnessLimit, bool shadowCompare, Image* gpuResult) {
  // Telemetry: time the whole drive (render-thread CPU cost; deep-gated) + record the iteration count.
  static auto cpuCostTimer = Telemetry::timer("lighting.gpu.cpu_cost.us");
  static auto spreadPasses = Telemetry::counter("lighting.gpu.spread.passes");
  TelemetryScope cpuCostScope(cpuCostTimer);

  Vec2U size = emission.size;
  if (size[0] == 0 || size[1] == 0 || iterations == 0)
    return false;

  // Bind the spread program; bail (caller falls back to CPU) if the assets are missing.
  if (!m_renderer->switchEffectConfig("lightingSpread"))
    return false;

  // Constant inputs across iterations: the seeded emission (also the iteration-0 light state) and
  // the obstacle grid. lightState is re-pointed at the previous ping-pong buffer each iteration.
  m_renderer->setEffectTexture("emission", emission);
  m_renderer->setEffectTexture("obstacle", obstacle);
  m_renderer->setEffectTexture("lightState", emission);   // iteration-0 seed == emission
  m_renderer->setEffectParameter("dropoffAir", 1.0f / spreadMaxAir);
  m_renderer->setEffectParameter("dropoffObstacle", 1.0f / spreadMaxObstacle);
  m_renderer->setEffectParameter("brightnessLimit", brightnessLimit);
  m_renderer->setEffectParameter("applyCap", false);

  auto quad = renderFlatRect(RectF::withSize(Vec2F(), Vec2F(size)), Vec4B::filled(255), 0.0f);
  char const* targets[2] = {"lightingGpu", "lightingGpuB"};
  char const* lastTarget = nullptr;

  for (unsigned i = 0; i < iterations; ++i) {
    char const* target = targets[i % 2];
    if (i + 1 == iterations)
      m_renderer->setEffectParameter("applyCap", true);   // proportional brightnessLimit on the final pass
    m_renderer->setRenderTarget(String(target), size);     // resize + viewport + screenSize uniform
    if (i > 0)
      m_renderer->setEffectTextureFromTarget("lightState", lastTarget);   // previous iteration's output
    m_renderer->render(quad);
    lastTarget = target;
  }
  m_renderer->flush();   // flush the final iteration's quad into lastTarget

  if (shadowCompare && gpuResult && lastTarget)
    *gpuResult = m_renderer->readFrameBuffer(lastTarget);

  // Restore the screen target + the world effect, then bind the spread result as the world lightMap.
  m_renderer->setRenderTarget({});
  m_renderer->switchEffectConfig("world");
  m_renderer->setEffectTextureFromTarget("lightMap", lastTarget);

  spreadPasses.inc(iterations);
  return true;
}

}
