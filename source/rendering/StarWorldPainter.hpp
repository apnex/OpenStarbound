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

  // --- Dirty-REGION Stage 2 render-side state (flags lightingDirtyRegionPartial / ...Validate, off by
  // default). The persistent spread (persistS) + lightmap (persistL) are only captured/maintained while
  // the feature is on, so m_persistValid tracks whether they hold valid full-grid data at m_prevCalcSize.
  // m_lastMaxEmission is the previous frame's peak emission (M_old) for the subtractive-edit dilation;
  // the rest are the previous-frame values whose change forces a full recompute (caveats / Option B). ---
  Vec2U m_prevCalcSize = Vec2U();
  bool m_persistValid = false;
  float m_lastMaxEmission = 0.0f;
  bool m_lastMaxEmissionValid = false;
  Vec2I m_lastLightMinPosition = Vec2I();
  float m_lastBrightnessScale = 1.0f;
  bool m_lastTonemap = false;
  float m_lastBrightnessLimit = 0.0f;
  // Stage-2 diagnostic "opportunity meter" (render side): on edit frames, which side blocks the partial
  // path + how often it actually RAN. Pairs with the gather-side meter in WorldClient. Logged each 600.
  uint64_t m_rffTotal = 0, m_rffGather = 0, m_rffSizePersist = 0, m_rffCap = 0, m_rffCompose = 0,
           m_rffScroll = 0, m_rffMaxEm = 0, m_rffDegrade = 0, m_rffPartial = 0;

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
