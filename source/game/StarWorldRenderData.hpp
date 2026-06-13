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
  bool lightingInputsValid = false;

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
  entityDrawables.clear();
  particles = nullptr;
  overheadBars.clear();
  nametags.clear();
  backgroundOverlays.clear();
  foregroundOverlays.clear();
  parallaxLayers.clear();
}

}
