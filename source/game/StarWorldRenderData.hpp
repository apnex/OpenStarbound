#pragma once

#include "StarImage.hpp"
#include "StarWorldTiles.hpp"
#include "StarEntityRenderingTypes.hpp"
#include "StarSkyRenderData.hpp"
#include "StarParallax.hpp"
#include "StarParticle.hpp"
#include "StarWeatherTypes.hpp"
#include "StarEntity.hpp"
#include "StarThread.hpp"
#include "StarCellularLighting.hpp"
#include "StarCellularLightArray.hpp"

namespace Star {

struct EntityDrawables {
  EntityHighlightEffect highlightEffect;
  Map<EntityRenderLayer, List<Drawable>> layers;
};


struct WorldRenderData {
  void clear();

  WorldGeometry geometry;

  Vec2I tileMinPosition;
  RenderTileArray tiles;
  Vec2I lightMinPosition;
  Lightmap lightMap;

  // GPU-spread inputs (Slice 2), populated by waitForLighting only when the
  // 'lightingGpu' flag is on. emission is RGB_F per-cell seeded light; obstacle
  // is RGB24 (255 obstacle / 0 air). lightingInputsValid gates their use.
  Image lightingEmission;
  Image lightingObstacle;
  // The emission grid as 16-bit half-floats (RGB packed), pre-converted on the lighting thread so the
  // GPU upload is RGB16F (half the per-frame transfer). Travels alongside lightingEmission; the RGB_F
  // version is still kept for the auto-K scan + shadow-compare reference.
  List<uint16_t> lightingEmissionHalf;
  // The obstacle mask as single-channel R8 bytes (0/255), pre-extracted on the lighting thread so the
  // GPU upload is R8 (a third the bytes of RGB24). Travels alongside lightingObstacle (RGB24 kept for
  // the shadow-compare reference).
  List<uint8_t> lightingObstacleR8;
  // The point lights (array-relative, insertion order) for the GPU per-light-quad
  // point pass (Slice 3). Travels alongside emission/obstacle, gated by
  // lightingInputsValid.
  List<ColoredCellularLightArray::PointLight> lightingPointLights;
  bool lightingInputsValid = false;
  // GPU lightmap border in cells: the GPU result is calc-region-sized (= emission size), so the
  // world shader offsets by this border (calc-vs-query padding) to sample the query region. Carried
  // here from the calculator's known geometry (Slice 4) because the CPU lightMap -- which WorldPainter
  // previously reverse-derived the border from -- is empty when the redundant CPU calc is skipped.
  // 0 for the CPU path (the lightMap is itself query-sized).
  int lightMapBorder = 0;

  // dirty-REGION Stage 2: the coalesced dirty bbox of lighting-relevant tile writes since the last
  // consumed lightmap, translated to CALC-region cell coords (origin = calculationRegion().min(),
  // exclusive max) by the lighting thread (Hop A). The render thread dilates it for the partial
  // spread scissor. lightForceFull ORs every gather-side reason the partial path cannot reuse the
  // prior frame (light-set change / scroll / gather-race / first frame); it is defaulted TRUE in
  // clear() so a frame that skips the lighting publish never consumes a stale rect.
  RectI lightDirtyRect = RectI::null();
  bool lightForceFull = true;

  List<EntityDrawables> entityDrawables;
  List<Particle> const* particles;

  List<OverheadBar> overheadBars;
  List<Drawable> nametags;

  List<Drawable> backgroundOverlays;
  List<Drawable> foregroundOverlays;

  List<ParallaxLayer> parallaxLayers;

  SkyRenderData skyRenderData;

  bool isFullbright = false;
  float dimLevel = 0.0f;
  Vec3B dimColor;
};

inline void WorldRenderData::clear() {
  tiles.resize({0, 0}); // keep reserved

  lightingInputsValid = false;
  // dirty-REGION Stage 2: a frame that does not consume a fresh lighting publish (Hop B not reached)
  // must never partial-recompute against a stale rect -> default to force-full + an empty rect.
  lightDirtyRect = RectI::null();
  lightForceFull = true;
  entityDrawables.clear();
  particles = nullptr;
  overheadBars.clear();
  nametags.clear();
  backgroundOverlays.clear();
  foregroundOverlays.clear();
  parallaxLayers.clear();
}

}
