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
  //
  // ASSETS ARE HANDED IN, NOT FETCHED. WorldPainter is the composition root and is allowed to know about
  // Root; a pass is not. This was the last Root::singleton() read in any L3 pass body, and it left by
  // REMOVAL rather than relocation -- WorldPainter already holds a fresh handle at both call sites, so
  // nothing moved up into the orchestrator to pay for it.
  explicit WorldPass(AssetsConstPtr assets);

  // (Re)create the tile / drawable / text painters on the given renderer. Called from WorldPainter::renderInit,
  // which runs on every world entry -- so, like the pre-extraction code, the painters are fresh each world.
  // Takes the RendererPtr because the painters' constructors do (they keep the renderer alive); WorldPass keeps
  // only a raw Renderer* for its own calls (valid for its lifetime -- the owned painters hold the renderer).
  void renderInit(RendererPtr const& renderer);

  // Takes the frame's assets handle for the same reason the constructor does: the world-body helpers do
  // per-frame asset lookups (particle config, preload images), and the pre-extraction render() re-read Root
  // assets every frame. The caller already refreshes its own handle immediately before this call.
  void setup(WorldCamera const& camera, WorldRenderData& renderData, AssetsConstPtr assets);
  void adjustLighting(WorldRenderData& renderData);

  // AIR-GAP CONTRACT (1) FOR THIS PASS -- and, UNLIKE BackdropPass::Input, deliberately NOT a const view,
  // because it cannot be. renderWorld is a SINK for four of these six and a VIEW for two. #184 found that
  // and wrote it in a comment; this puts it in the type, where the compiler can see it.
  //
  // PASSED BY VALUE, not by const&. `Input const&` would be actively misleading: constness does not
  // propagate through reference members, so the four sinks would remain mutable through it. Six references
  // copy for free; a guarantee that is not one costs more than that.
  //
  // WHY THE CONSUMPTION MATTERS rather than being a curiosity: the rendertest state fingerprint already
  // reads m_renderData AFTER render. It survives only because it reads outer .size(), which the moves
  // leave intact. Extend it to hash nametag or overlay CONTENT -- the obvious next step for its stated
  // purpose -- and it returns a stable, WRONG fingerprint claiming the inputs matched when they were
  // consumed. A diagnostic that lies is worse than none, and this one would lie in the direction that
  // hides a real difference.
  struct Input {
    // CONSUMED -- hollowed by the end of the call; outer container intact, elements moved-from.
    List<EntityDrawables>& entityDrawables;  // std::move out of ed.layers
    List<Drawable>& backgroundOverlays;      // } all three via drawDrawableSet, whose entire body is
    List<Drawable>& foregroundOverlays;      // }   `for (Drawable& d : ds) drawDrawable(camera, move(d));`
    List<Drawable>& nametags;                // }
    // READ-ONLY.
    List<Particle> const* particles;
    List<OverheadBar> const& overheadBars;
  };

  void renderWorld(WorldCamera const& camera, Input in);

  void cleanup(int64_t textureTimeout);

private:
  // Sliced to what they use, for the same reason renderWorld is: each took the whole frame snapshot to
  // read one member of it.
  void renderParticles(WorldCamera const& camera, List<Particle> const* particles, Particle::Layer layer);
  void renderBars(WorldCamera const& camera, List<OverheadBar> const& overheadBars);
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
