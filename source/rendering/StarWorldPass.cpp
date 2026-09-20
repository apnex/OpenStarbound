#include "StarWorldPass.hpp"
// StarRoot.hpp is DELIBERATELY ABSENT. The assets handle is handed in by WorldPainter -- the composition
// root, which is allowed to know about Root; a pass is not. render_layering greps for the CALL, not the
// include, so the compile is the only thing that can prove this dependency edge is actually gone.
#include "StarAssets.hpp"
#include "StarJsonExtra.hpp"
#include "StarAssetTextureGroup.hpp"
#include "StarWorldCamera.hpp"
#include "StarAnimation.hpp"
#include "StarRandom.hpp"
#include "StarTelemetry.hpp"

namespace Star {

namespace {
  // Eager, by name -- see the note at the head of StarBackdropPass.cpp. This one is reached on every frame
  // that renders a world, so it has never been observed absent; it is registered here for the same reason
  // the others are, and so gpu_pass_keys can assert the set rather than a subset somebody curated.
  auto s_worldTimer = Telemetry::timer("render.pass.world.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail, MetricUnit::Microseconds, MetricClock::GpuTimeline});
  // Its .nested rejection counter -- see the note at the head of StarBackdropPass.cpp. Cadence::Call,
  // not Frame: a rejection is an event at a begin(), not a per-frame quantity, whatever the timer is.
  auto s_worldNested = Telemetry::counter("render.pass.world.gpu_us.nested",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail, MetricUnit::Count, MetricClock::NotApplicable});

  // THE SPAN CALIBRATOR ([#286] step 2). render.painter.body.us is the largest part of the painter --
  // 43% standing -- and MetricClock::Wall bills blocking as work, so until this key reads, that number
  // is of unknown KIND: cost, or wait on the driver. [#246] is what that costs when nobody checks --
  // a bracket reported as a cost turned out to be 2.5x the device's entire busy time, because it was
  // a span. This measures the submit calls directly so the doubt is a number instead of a caveat.
  //
  // NESTED, NEVER SUMMED: no .whole. It is a SUBSET of body.us (and of tiles.us's siblings), not a
  // sibling of the painter members -- declaring the whole would count these microseconds twice.
  // Modelled on lighting.gpu.drive.flush.us, the one flush bracket in the tree that is read WITH ITS
  // COUNT at sites where primitives are provably pending.
  //
  // SIX SITES, ONE KEY, 22 SAMPLES PER FRAME: renderEntitiesUntil x11, renderParticles x3,
  // renderBars x1, drawDrawableSet x3 here; TilePainter::renderLiquid x1 and renderTerrainChunks x3
  // there. A count that is not 22x frames means renderWorld changed shape and this comment is stale.
  //
  // REGISTERED IN TWO TRANSLATION UNITS, AND THAT IS SAFE ONLY BECAUSE EVERY PROSE FIELD IS NULL.
  // MetricDesc::operator== compares measures/validWhen/whole BY POINTER, so two identical string
  // literals in two TUs would be UNEQUAL and set descConflict. With all three defaulted to nullptr
  // and the enums identical, the two descriptors compare equal and static-init order cannot matter.
  // Adding a .whole or a .measures here without removing the other registration would break that.
  auto s_painterSubmit = Telemetry::timer("render.painter.submit.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame,
               .cadence = MetricCadence::Call, .role = MetricRole::Detail,
               .unit = MetricUnit::Microseconds, .clock = MetricClock::Wall});
}

WorldPass::WorldPass(AssetsConstPtr assets) : m_assets(std::move(assets)) {
  m_highlightConfig = m_assets->json("/highlights.config");
  for (auto p : m_highlightConfig.get("highlightDirectives").iterateObject())
    m_highlightDirectives.set(EntityHighlightEffectTypeNames.getLeft(p.first), {p.second.getString("underlay", ""), p.second.getString("overlay", "")});

  m_entityBarOffset = jsonToVec2F(m_assets->json("/rendering.config:entityBarOffset"));
  m_entityBarSpacing = jsonToVec2F(m_assets->json("/rendering.config:entityBarSpacing"));
  m_entityBarSize = jsonToVec2F(m_assets->json("/rendering.config:entityBarSize"));
  m_entityBarIconOffset = jsonToVec2F(m_assets->json("/rendering.config:entityBarIconOffset"));
  m_preloadTextureChance = m_assets->json("/rendering.config:preloadTextureChance").toFloat();
}

void WorldPass::renderInit(RendererPtr const& renderer) {
  m_renderer = renderer.get();
  auto textureGroup = renderer->createTextureGroup(TextureGroupSize::Large);
  m_textPainter = make_shared<TextPainter>(renderer, textureGroup);
  m_tilePainter = make_shared<TilePainter>(renderer);
  m_drawablePainter = make_shared<DrawablePainter>(renderer, make_shared<AssetTextureGroup>(textureGroup));
}

void WorldPass::setup(WorldCamera const& camera, WorldRenderData& renderData, AssetsConstPtr assets) {
  // Refresh the current asset set (mirrors the pre-extraction render() which re-read Root assets each frame);
  // the world-body helpers below do per-frame asset lookups (particle config, preload images). The handle now
  // arrives from the caller, which refreshes its own on the line before this call -- same value, same frame.
  m_assets = std::move(assets);
  m_tilePainter->setup(camera, renderData);
}

void WorldPass::adjustLighting(WorldRenderData& renderData) {
  m_tilePainter->adjustLighting(renderData);
}

void WorldPass::renderWorld(WorldCamera const& camera, Input in) {
  // The descriptor travels WITH begin(), not separately: the value is recorded generically inside
  // OpenGlRenderer (Telemetry::timer(name, desc).record(...), ~3 frames after the GPU did the work, at
  // GlGpuTimer::begin's own readback), which has no idea what "render.pass.world.gpu_us" MEANS. This pass
  // is the one place that does, so it supplies the meaning at the call that starts the timing -- the same
  // call that must happen for the metric to exist at all. That makes an undeclared GPU metric
  // unrepresentable: there is no longer a separate declare statement that a branch or a config flag could
  // route around.
  m_renderer->gpuTimer().begin("render.pass.world.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail, MetricUnit::Microseconds, MetricClock::GpuTimeline});
  Map<EntityRenderLayer, List<pair<EntityHighlightEffect, List<Drawable>>>> entityDrawables;
  for (auto& ed : in.entityDrawables) {
    for (auto& p : ed.layers)
      entityDrawables[p.first].append({ed.highlightEffect, std::move(p.second)});
  }

  auto entityDrawableIterator = entityDrawables.begin();
  auto renderEntitiesUntil = [this, &camera, &entityDrawables, &entityDrawableIterator](Maybe<EntityRenderLayer> until) {
    while (true) {
      if (entityDrawableIterator == entityDrawables.end())
        break;
      if (until && entityDrawableIterator->first >= *until)
        break;
      for (auto& edl : entityDrawableIterator->second)
        drawEntityLayer(camera, std::move(edl.second), edl.first);
      ++entityDrawableIterator;
    }

    { TelemetryScope s(s_painterSubmit); m_renderer->flush(); }
  };

  renderEntitiesUntil(RenderLayerBackgroundOverlay);
  drawDrawableSet(camera, in.backgroundOverlays);
  renderEntitiesUntil(RenderLayerBackgroundTile);
  m_tilePainter->renderBackground(camera);
  renderEntitiesUntil(RenderLayerPlatform);
  m_tilePainter->renderMidground(camera);
  renderEntitiesUntil(RenderLayerBackParticle);
  renderParticles(camera, in.particles, Particle::Layer::Back);
  renderEntitiesUntil(RenderLayerLiquid);
  m_tilePainter->renderLiquid(camera);
  renderEntitiesUntil(RenderLayerMiddleParticle);
  renderParticles(camera, in.particles, Particle::Layer::Middle);
  renderEntitiesUntil(RenderLayerForegroundTile);
  m_tilePainter->renderForeground(camera);
  renderEntitiesUntil(RenderLayerForegroundOverlay);
  drawDrawableSet(camera, in.foregroundOverlays);
  renderEntitiesUntil(RenderLayerFrontParticle);
  renderParticles(camera, in.particles, Particle::Layer::Front);
  renderEntitiesUntil(RenderLayerOverlay);
  drawDrawableSet(camera, in.nametags);
  renderBars(camera, in.overheadBars);
  renderEntitiesUntil({});
  m_renderer->gpuTimer().end("render.pass.world.gpu_us");
}

void WorldPass::cleanup(int64_t textureTimeout) {
  m_textPainter->cleanup(textureTimeout);
  m_drawablePainter->cleanup(textureTimeout);
  m_tilePainter->cleanup();
}

void WorldPass::renderParticles(WorldCamera const& camera, List<Particle> const* particles, Particle::Layer layer) {
  const int textParticleFontSize = m_assets->json("/rendering.config:textParticleFontSize").toInt();
  const RectF particleRenderWindow = RectF::withSize(Vec2F(), Vec2F(camera.screenSize())).padded(m_assets->json("/rendering.config:particleRenderWindowPadding").toInt());

  if (!particles)
    return;

  for (Particle const& particle : *particles) {
    if (layer != particle.layer)
      continue;

    Vec2F position = camera.worldToScreen(particle.position);

    if (!particleRenderWindow.contains(position))
      continue;

    Vec2F size = Vec2F::filled(particle.size * camera.pixelRatio());

    if (particle.type == Particle::Type::Ember) {
      m_renderer->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(),
        RectF(position - size / 2, position + size / 2),
        particle.color.toRgba(),
        particle.fullbright ? 0.0f : 1.0f);

    } else if (particle.type == Particle::Type::Streak) {
      // Draw a rotated quad streaking in the direction the particle is coming from.
      // Sadly this looks awful.
      Vec2F dir = particle.velocity.normalized();
      Vec2F sideHalf = dir.rot90() * camera.pixelRatio() * particle.size / 2;
      float length = particle.length * camera.pixelRatio();
      Vec4B color = particle.color.toRgba();
      float lightMapMultiplier = particle.fullbright ? 0.0f : 1.0f;
      m_renderer->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(),
        position - sideHalf,
        position + sideHalf,
        position - dir * length + sideHalf,
        position - dir * length - sideHalf,
        color, lightMapMultiplier);

    } else if (particle.type == Particle::Type::Textured || particle.type == Particle::Type::Animated) {
      Drawable drawable;
      if (particle.type == Particle::Type::Textured)
        drawable = Drawable::makeImage(particle.image, 1.0f / TilePixels, true, Vec2F(0, 0));
      else
        drawable = particle.animation->drawable(1.0f / TilePixels);

      if (particle.flip && particle.flippable)
        drawable.scale(Vec2F(-1, 1));
      if (drawable.isImage() && particle.type != Particle::Type::Animated)
        drawable.imagePart().addDirectivesGroup(particle.directives, true);
      drawable.fullbright = particle.fullbright;
      drawable.color = particle.color;
      drawable.rotate(particle.rotation);
      drawable.scale(particle.size);
      drawable.translate(particle.position);
      drawDrawable(camera, std::move(drawable));

    } else if (particle.type == Particle::Type::Text) {
      Vec2F position = camera.worldToScreen(particle.position);
      int size = min(128.0f, round((float)textParticleFontSize * camera.pixelRatio() * particle.size));
      if (size > 0) {
        m_textPainter->setFontSize(size);
        m_textPainter->setFontColor(particle.color.toRgba());
        m_textPainter->setProcessingDirectives("");
        m_textPainter->setFont("");
        m_textPainter->renderText(particle.string, {position, HorizontalAnchor::HMidAnchor, VerticalAnchor::VMidAnchor});
      }
    }
  }

  { TelemetryScope s(s_painterSubmit); m_renderer->flush(); }
}

void WorldPass::renderBars(WorldCamera const& camera, List<OverheadBar> const& overheadBars) {
  auto offset = m_entityBarOffset;
  for (auto const& bar : overheadBars) {
    auto position = bar.entityPosition + offset;
    offset += m_entityBarSpacing;
    if (bar.icon) {
      auto iconDrawPosition = position - (m_entityBarSize / 2).round() + m_entityBarIconOffset;
      drawDrawable(camera, Drawable::makeImage(*bar.icon, 1.0f / TilePixels, true, iconDrawPosition));
    }

    if (!bar.detailOnly) {
      auto fullBar = RectF({}, {m_entityBarSize.x() * bar.percentage, m_entityBarSize.y()});
      auto emptyBar = RectF({m_entityBarSize.x() * bar.percentage, 0.0f}, m_entityBarSize);
      auto fullColor = bar.color;
      auto emptyColor = Color::Black;

      drawDrawable(camera, Drawable::makePoly(PolyF(emptyBar), emptyColor, position));
      drawDrawable(camera, Drawable::makePoly(PolyF(fullBar), fullColor, position));
    }
  }

  { TelemetryScope s(s_painterSubmit); m_renderer->flush(); }
}

void WorldPass::drawEntityLayer(WorldCamera const& camera, List<Drawable> drawables, EntityHighlightEffect highlightEffect) {
  highlightEffect.level *= m_highlightConfig.getFloat("maxHighlightLevel", 1.0);
  if (m_highlightDirectives.contains(highlightEffect.type) && highlightEffect.level > 0) {
    // first pass, draw underlay
    auto underlayDirectives = m_highlightDirectives[highlightEffect.type].first;
    if (!underlayDirectives.empty()) {
      for (auto& d : drawables) {
        if (d.isImage()) {
          auto underlayDrawable = Drawable(d);
          underlayDrawable.fullbright = true;
          underlayDrawable.color = Color::rgbaf(1, 1, 1, highlightEffect.level * d.color.alphaF());
          underlayDrawable.imagePart().addDirectives(underlayDirectives, true);
          drawDrawable(camera, std::move(underlayDrawable));
        }
      }
    }

    // second pass, draw main drawables and overlays
    auto overlayDirectives = m_highlightDirectives[highlightEffect.type].second;
    for (auto& d : drawables) {
      drawDrawable(camera, d);
      if (!overlayDirectives.empty() && d.isImage()) {
        auto overlayDrawable = Drawable(d);
        overlayDrawable.fullbright = true;
        overlayDrawable.color = Color::rgbaf(1, 1, 1, highlightEffect.level * d.color.alphaF());
        overlayDrawable.imagePart().addDirectives(overlayDirectives, true);
        drawDrawable(camera, std::move(overlayDrawable));
      }
    }
  } else {
    for (auto& d : drawables)
      drawDrawable(camera, std::move(d));
  }
}

// [rendertest] STAR_RENDERTEST_PIN_PRELOAD=1 (RustBound M81): the off-screen texture preload below rolls the
// GLOBAL random source once per off-screen image drawable per frame, and the roll decides the ORDER images
// enter the atlas. Two captures of one fixture at the same pins, every entity field and image string agreeing,
// hashed apart by 4 pixels in one column of a humanoid whose 43-pixel image is centred on a whole pixel: there
// every texel boundary sits on a pixel centre, the sampled texel follows the texture coordinate's rounding,
// and the coordinate is the atlas offset plus the texel index - the census read the same image at (161,81) in
// one run and (369,65) in the other. Under the pin nothing preloads and an image enters the atlas when first
// painted, in paint order, which the frozen fixture makes the same run to run. Read once; unset costs one
// static read.
static bool renderTestPinPreload() {
  static bool const on = []() {
    char const* e = getenv("STAR_RENDERTEST_PIN_PRELOAD");
    return e && *e && *e != '0';
  }();
  return on;
}

// [rendertest] STAR_RENDERTEST_SNAP_IMAGES=1 (RustBound M81): holding the preload roll was not enough - the atlas
// order still differed run to run (entities reach the client over the local wire in a thread's order, and the
// group's timeouts are wall-clock), and the pair still hashed apart at the same column. The pin that does not
// depend on the atlas order at all: an image drawable whose screen-space corner is off the pixel grid is moved
// to the nearest pixel, floor(corner + 0.5), so a pixel centre samples a texel CENTRE, half a texel from any
// boundary, and no rounding of the texture coordinate can reach it. A 43-pixel image centred on a whole pixel
// moves half a pixel; an image whose corner is already on the grid does not move. Harness only: the shipped
// path draws the corner where the entity put it, rounding and all.
static bool renderTestSnapImages() {
  static bool const on = []() {
    char const* e = getenv("STAR_RENDERTEST_SNAP_IMAGES");
    return e && *e && *e != '0';
  }();
  return on;
}

// [rendertest] RustBound M52 leg 3c. The two steps that put a drawable into SCREEN pixels, and the snap rule
// that decides which pixel its corner lands on. Lifted out of drawDrawable unchanged - same calls, same order -
// so that renderTestScreenCorner below can report the corner the DRAW uses rather than a second opinion about
// it. A census that re-derives the transform is a second implementation, and the defect this exists to find is
// exactly a disagreement of half a pixel.
static void renderTestToScreen(WorldCamera const& camera, Drawable& drawable) {
  drawable.position = camera.worldToScreen(drawable.position);
  drawable.scale(camera.pixelRatio() * TilePixels, drawable.position);
}

static Vec2F renderTestSnapCorner(Vec2F corner) {
  return Vec2F(std::floor(corner[0] + 0.5f), std::floor(corner[1] + 0.5f));
}

// [rendertest] RustBound M52 leg 3c. Where an image drawable's low corner LANDS ON SCREEN. `snap` applies the
// harness's image snap on top, which is the corner a pinned capture actually draws; without it, the corner the
// shipped path draws. The caller hands a drawable in the same state drawDrawable receives one - world position,
// entity translation already applied - because everything after that is what this reproduces.
Vec2F WorldPass::renderTestScreenCorner(WorldCamera const& camera, Drawable drawable, bool snap) {
  renderTestToScreen(camera, drawable);
  Vec2F corner = drawable.boundBox(false).min();
  return snap ? renderTestSnapCorner(corner) : corner;
}

void WorldPass::drawDrawable(WorldCamera const& camera, Drawable drawable) {
  renderTestToScreen(camera, drawable);

  if (drawable.isLine())
    drawable.linePart().width *= camera.pixelRatio();

  if (drawable.isImage() && renderTestSnapImages()) {
    Vec2F corner = drawable.boundBox(false).min();
    Vec2F snapped = renderTestSnapCorner(corner);
    if (snapped != corner)
      drawable.translate(snapped - corner);
  }

  // draw the drawable if it's on screen
  // if it's not on screen, there's a random chance to pre-load
  // pre-load is not done on every tick because it's expensive to look up images with long paths
  if (RectF::withSize(Vec2F(), Vec2F(camera.screenSize())).intersects(drawable.boundBox(false)))
    m_drawablePainter->drawDrawable(drawable);
  else if (drawable.isImage() && !renderTestPinPreload() && Random::randf() < m_preloadTextureChance)
    m_assets->tryImage(drawable.imagePart().image);
}

void WorldPass::drawDrawableSet(WorldCamera const& camera, List<Drawable>& drawables) {
  for (Drawable& drawable : drawables)
    drawDrawable(camera, std::move(drawable));

  { TelemetryScope s(s_painterSubmit); m_renderer->flush(); }
}

}
