#pragma once

#include "StarWorldRenderData.hpp"
#include "StarTilePainter.hpp"
#include "StarEnvironmentPainter.hpp"
#include "StarTextPainter.hpp"
#include "StarDrawablePainter.hpp"
#include "StarRenderer.hpp"
#include "StarGpuLightmapPass.hpp"

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
  void render(WorldRenderData& renderData, function<bool()> lightWaiter);
  void adjustLighting(WorldRenderData& renderData);
  // Slice 4: did the GPU lightmap pass produce this frame's lightMap? The render loop
  // feeds this back to WorldClient (setGpuLightingActive) so the lighting thread can
  // drop the redundant CPU calculate(). False whenever GPU lighting is off, inputs are
  // invalid, the world is fullbright, or the GPU pass fell back to CPU.
  bool gpuLightingActive() const { return m_gpuLightingActive; }

private:
  void renderParticles(WorldRenderData& renderData, Particle::Layer layer);
  void renderBars(WorldRenderData& renderData);

  void drawEntityLayer(List<Drawable> drawables, EntityHighlightEffect highlightEffect = EntityHighlightEffect());

  void drawDrawable(Drawable drawable);
  void drawDrawableSet(List<Drawable>& drawable);

  WorldCamera m_camera;

  RendererPtr m_renderer;

  TextPainterPtr m_textPainter;
  DrawablePainterPtr m_drawablePainter;
  EnvironmentPainterPtr m_environmentPainter;
  TilePainterPtr m_tilePainter;
  GpuLightmapPassPtr m_gpuLightmapPass;
  // Border (in cells) between the bound lightMap texture and the query region: 0 for the CPU
  // (query-sized) lightMap, borderCells for the GPU (calc-region-sized) result. Persists across
  // non-update frames since the lightMap binding persists. Shifts lightMapOffset accordingly.
  int m_lightMapBorder = 0;
  // Slice 4: latest GPU-lightmap-pass outcome, reported back to WorldClient each frame.
  bool m_gpuLightingActive = false;

  // Environment-cache probe (envRefreshInterval): frame counter gating the env->cache refresh, and the
  // screen size the cache currently holds (a mismatch forces a refresh on the first frame + after resize;
  // {0,0} also forces one on the first AA-off frame after MSAA was on).
  uint64_t m_envRefreshCounter = 0;
  Vec2U m_envCacheSize = {0, 0};

  // Parallax retained-cache (SP-2): like the env cache, but the source scrolls with the camera, so the
  // refresh gate adds camera-position + pixelRatio(zoom) terms (Option B: force-full on any move). Parked
  // camera is bit-stable (StarWorldCamera dead-zone+snap) so exact-equality never fires spuriously.
  uint64_t m_parallaxRefreshCounter = 0;
  Vec2U m_parallaxCacheSize = {0, 0};
  Vec2F m_parallaxCachePosition = {0.0f, 0.0f};
  float m_parallaxCachePixelRatio = 0.0f;
  // Content-adaptive N: the last N we logged, so the [parallaxauto] line fires on change, not every frame.
  unsigned m_lastLoggedParallaxN = 0;

  Json m_highlightConfig;
  Map<EntityHighlightEffectType, pair<Directives, Directives>> m_highlightDirectives;

  Vec2F m_entityBarOffset;
  Vec2F m_entityBarSpacing;
  Vec2F m_entityBarSize;
  Vec2F m_entityBarIconOffset;

  // Updated every frame

  AssetsConstPtr m_assets;
  RectF m_worldScreenRect;

  Vec2F m_previousCameraCenter;
  Vec2F m_parallaxWorldPosition;

  float m_preloadTextureChance;
};

}
