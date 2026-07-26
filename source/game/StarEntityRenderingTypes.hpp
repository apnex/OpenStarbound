#pragma once

#include "StarJson.hpp"
#include "StarColor.hpp"
#include "StarDrawable.hpp"
#include "StarGameTypes.hpp"

namespace Star {

typedef uint32_t EntityRenderLayer;

unsigned const RenderLayerUpperBits = 5;
unsigned const RenderLayerLowerBits = 32 - RenderLayerUpperBits;
EntityRenderLayer const RenderLayerLowerMask = (EntityRenderLayer)-1 >> RenderLayerUpperBits;

EntityRenderLayer const RenderLayerBackgroundOverlay = 1 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerBackgroundTile = 2 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerPlatform = 3 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerPlant = 4 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerPlantDrop = 5 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerObject = 6 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerPreviewObject = 7 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerBackParticle = 8 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerVehicle = 9 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerEffect = 10 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerProjectile = 11 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerMonster = 12 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerNpc = 13 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerPlayer = 14 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerItemDrop = 15 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerLiquid = 16 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerMiddleParticle = 17 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerForegroundTile = 18 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerForegroundEntity = 19 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerForegroundOverlay = 20 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerFrontParticle = 21 << RenderLayerLowerBits;
EntityRenderLayer const RenderLayerOverlay = 22 << RenderLayerLowerBits;

EntityRenderLayer parseRenderLayer(String renderLayer);

struct PreviewTile {
  PreviewTile();
  PreviewTile(Vec2I const& position, bool foreground, MaterialId matId, MaterialHue hueShift, bool updateMatId);
  PreviewTile(Vec2I const& position, bool foreground, Vec3B const& light, bool updateLight);
  PreviewTile(Vec2I const& position, bool foreground, MaterialId matId, MaterialHue hueShift, bool updateMatId, Vec3B const& light, bool updateLight, MaterialColorVariant colorVariant);
  PreviewTile(Vec2I const& position, LiquidId liqId);

  Vec2I position;
  bool foreground;

  LiquidId liqId;
  MaterialId matId;
  MaterialHue hueShift;
  bool updateMatId;
  MaterialColorVariant colorVariant;
  Vec3B light;
  bool updateLight;
};

struct OverheadBar {
  OverheadBar();
  OverheadBar(Json const& json);
  OverheadBar(Maybe<String> icon, float percentage, Color color, bool detailOnly);

  Vec2F entityPosition;
  Maybe<String> icon;
  float percentage;
  Color color;
  bool detailOnly;
};

enum class EntityHighlightEffectType {
  None,
  Interactive,
  Inspectable,
  Interesting,
  Inspected
};
extern EnumMap<EntityHighlightEffectType> const EntityHighlightEffectTypeNames;

struct EntityHighlightEffect {
  EntityHighlightEffectType type = EntityHighlightEffectType::None;
  float level = 0.0f;
};

// MOVED HERE FROM StarWorldRenderData.hpp (#191). It is built from exactly the two things this header
// already declares -- EntityHighlightEffect above and EntityRenderLayer at the top -- so this is where it
// always belonged; it sat in WorldRenderData.hpp only because that is where its first consumer lived.
//
// THE LOCATION IS LOAD-BEARING, not tidiness. WorldRenderData.hpp is the fat header the L3 decomposition
// exists to escape: 35 members and eleven transitive includes. While EntityDrawables was DEFINED inside it,
// any signature naming EntityDrawables dragged the whole thing in -- so WorldPass::Input could not shed it
// even in principle, and contract (1) was unfinishable for a reason that had nothing to do with WorldPass.
// A type a sliced interface must name cannot live inside the header the slice exists to avoid.
struct EntityDrawables {
  EntityHighlightEffect highlightEffect;
  Map<EntityRenderLayer, List<Drawable>> layers;
};

}
