#pragma once

#include "StarWorldRenderData.hpp"
#include "StarTilePainter.hpp"
#include "StarEnvironmentPainter.hpp"
#include "StarTextPainter.hpp"
#include "StarDrawablePainter.hpp"
#include "StarRenderer.hpp"

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

  // Renderer framebuffer generation the retained caches were last filled under. A renderer config reload
  // (the hdr / antiAliasing client options, both polled every frame) destroys and re-creates EVERY FBO with
  // UNDEFINED content, which no other refresh-key term can see -- size, camera and counter are all unchanged
  // across it. Both caches invalidate on a bump, or they composite undefined GPU memory.
  uint64_t m_cacheFrameBufferGeneration = 0;

  // Environment-cache probe (envRefreshInterval): frame counter gating the env->cache refresh, and the
  // screen size the cache currently holds (a mismatch forces a refresh on the first frame + after resize;
  // {0,0} also forces one on the first AA-off frame after MSAA was on).
  uint64_t m_envRefreshCounter = 0;
  Vec2U m_envCacheSize = {0, 0};
  // The env draw scales stars/debris/orbiters by camera pixelRatio (starAndDebrisRatio / orbiterAndPlanetRatio),
  // so a zoom change alters the cached image while size/counter stay put. Part of the env refresh key.
  float m_envCachePixelRatio = -1.0f;

  // Parallax retained-cache (SP-2): like the env cache, but the source scrolls with the camera, so the
  // refresh gate adds camera-position + pixelRatio(zoom) terms (Option B: force-full on any move). Parked
  // camera is bit-stable (StarWorldCamera dead-zone+snap) so exact-equality never fires spuriously.
  uint64_t m_parallaxRefreshCounter = 0;
  Vec2U m_parallaxCacheSize = {0, 0};
  Vec2F m_parallaxCachePosition = {0.0f, 0.0f};
  float m_parallaxCachePixelRatio = 0.0f;
  // Everything OTHER than camera/zoom/size that changes the drawn parallax image: renderParallaxLayers tints
  // each non-unlit/non-lightMapped layer with sky.environmentLight and fades it by floor(255*layer.alpha) --
  // and layer.alpha is exactly what the biome CROSSFADE and timeOfDayCorrelation animate. Neither was in the
  // refresh key, so a cached parallax froze its tint and stalled crossfades for up to N frames. Hashed over
  // the same quantities the draw actually quantizes to, so the key moves iff the image would.
  uint64_t m_parallaxCacheContentKey = 0;
  // Last frame's parallax anchor + zoom, so we can ask "is the camera moving RIGHT NOW" -- distinct from
  // "does the cache hold a different position", which stays true on the first parked frame and would
  // otherwise keep the bypass latched on forever. ParkFrames of stillness engages the cache.
  Vec2F m_parallaxPrevPosition = {0.0f, 0.0f};
  float m_parallaxPrevPixelRatio = 0.0f;
  unsigned m_parallaxStillFrames = 0;
  // Cross-surface refresh arbiter: a parallax TIME-gate firing on a frame the env cache is also refreshing
  // would stack both heavy redraws into one frame. Defer it by one frame instead. (The old +N/2 phase offset
  // could not do this: env fires on counter%4==0, so any even N whose half is a multiple of 4 -- notably the
  // static-content default N=16 -- collided on EVERY refresh.)
  bool m_parallaxRefreshDeferred = false;
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
