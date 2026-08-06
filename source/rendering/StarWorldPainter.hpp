#pragma once

#include "StarWorldRenderData.hpp"
#include "StarTilePainter.hpp"
#include "StarEnvironmentPainter.hpp"
#include "StarTextPainter.hpp"
#include "StarDrawablePainter.hpp"
#include "StarRenderer.hpp"
#include "StarGpuLightmapPass.hpp"
#include "StarBackdropPass.hpp"
#include "StarWorldPass.hpp"

namespace Star {

STAR_CLASS(WorldPainter);

// Will update client rendering window internally
class WorldPainter {
public:
  WorldPainter();

  void renderInit(RendererPtr renderer);

  void setCameraPosition(WorldGeometry const& worldGeometry, Vec2F const& position);

  WorldCamera& camera();

  void update(float dt);
  // Render-harness determinism; see EnvironmentPainter::pinRayAnimation for why the sky needs it.
  void pinRayAnimation(uint64_t seed, double timer);
  void render(WorldRenderData& renderData, function<bool()> lightWaiter);
  void adjustLighting(WorldRenderData& renderData);
  // Slice 4: did the GPU lightmap pass produce this frame's lightMap? The render loop
  // feeds this back to WorldClient (setGpuLightingActive) so the lighting thread can
  // drop the redundant CPU calculate(). False whenever GPU lighting is off, inputs are
  // invalid, the world is fullbright, or the GPU pass fell back to CPU.
  bool gpuLightingActive() const { return m_gpuLightingActive; }

private:
  // GPU-lightmap dispatch: prepare the pass inputs from renderData + config/assets, run GpuLightmapPass, and
  // return its explicit LightmapResult. Returns {active=false} (caller falls back to the CPU lightMap) when GPU
  // lighting is off, the inputs are invalid, or the pass declines. Pulled out of render() so the orchestrator
  // stays thin and the lightmap phase has one entry.
  LightmapResult runGpuLightmapPass(WorldRenderData& renderData);

  WorldCamera m_camera;

  RendererPtr m_renderer;

  EnvironmentPainterPtr m_environmentPainter;
  GpuLightmapPassPtr m_gpuLightmapPass;
  // Sky/environment + parallax backdrop pass: owns the two retained caches + the cross-surface refresh arbiter.
  BackdropPassPtr m_backdropPass;
  // World-body pass: owns the tile/drawable/text painters + entity-render config; draws the interleaved world
  // layers (tiles + entities + particles + drawables + bars).
  WorldPassPtr m_worldPass;
  // Border (in cells) between the bound lightMap texture and the query region: 0 for the CPU
  // (query-sized) lightMap, borderCells for the GPU (calc-region-sized) result. Persists across
  // non-update frames since the lightMap binding persists. Shifts lightMapOffset accordingly.
  int m_lightMapBorder = 0;
  // Slice 4: latest GPU-lightmap-pass outcome, reported back to WorldClient each frame.
  bool m_gpuLightingActive = false;

  // Updated every frame
  AssetsConstPtr m_assets;
};

}
