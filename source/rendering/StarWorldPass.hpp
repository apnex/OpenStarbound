#pragma once

#include "StarWorldRenderData.hpp"
#include "StarTilePainter.hpp"
#include "StarTextPainter.hpp"
#include "StarDrawablePainter.hpp"
#include "StarRenderer.hpp"

namespace Star {

STAR_CLASS(WorldPass);

// The world-body render pass: the interleaved tile / entity / particle / drawable / bar layers -- the intrinsic
// content of a world, as distinct from the sky/parallax backdrop (BackdropPass) and the lightmap (LightmapPass).
// Unlike those passes it OWNS its draw engines (the tile / drawable / text painters) and the entity-render
// config, because the world body is game-coupled by nature (accepted -- see the decomposition design).
//
// WorldPainter drives it across a frame via five entry points, mirroring how the painters were used pre-
// extraction: renderInit (recreate painters on world entry), setup (build this frame's tile geometry, called
// early), adjustLighting (tint the CPU lightmap for liquids, from the CPU-lightmap path), renderWorld (the
// interleaved body), cleanup (time out unused cached textures).
class WorldPass {
public:
  // Loads the entity-render config ONCE (highlights + overhead-bar geometry), matching the pre-extraction
  // WorldPainter constructor. The draw painters are NOT created here -- renderInit() does that, because the
  // pre-extraction renderInit recreated them on every world entry.
  WorldPass();

  // (Re)create the tile / drawable / text painters on the given renderer. Called from WorldPainter::renderInit,
  // which runs on every world entry -- so, like the pre-extraction code, the painters are fresh each world.
  // Takes the RendererPtr because the painters' constructors do (they keep the renderer alive); WorldPass keeps
  // only a raw Renderer* for its own calls (valid for its lifetime -- the owned painters hold the renderer).
  void renderInit(RendererPtr const& renderer);

  void setup(WorldCamera const& camera, WorldRenderData& renderData);
  void adjustLighting(WorldRenderData& renderData);
  void renderWorld(WorldCamera const& camera, WorldRenderData& renderData);
  void cleanup(int64_t textureTimeout);

private:
  void renderParticles(WorldCamera const& camera, WorldRenderData& renderData, Particle::Layer layer);
  void renderBars(WorldCamera const& camera, WorldRenderData& renderData);
  void drawEntityLayer(WorldCamera const& camera, List<Drawable> drawables, EntityHighlightEffect highlightEffect = EntityHighlightEffect());
  void drawDrawable(WorldCamera const& camera, Drawable drawable);
  void drawDrawableSet(WorldCamera const& camera, List<Drawable>& drawables);

  Renderer* m_renderer = nullptr;

  TextPainterPtr m_textPainter;
  DrawablePainterPtr m_drawablePainter;
  TilePainterPtr m_tilePainter;

  AssetsConstPtr m_assets;

  Json m_highlightConfig;
  Map<EntityHighlightEffectType, pair<Directives, Directives>> m_highlightDirectives;
  Vec2F m_entityBarOffset;
  Vec2F m_entityBarSpacing;
  Vec2F m_entityBarSize;
  Vec2F m_entityBarIconOffset;
  float m_preloadTextureChance = 0.0f;
};

}
