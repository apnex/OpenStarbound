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
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});
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
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});
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

    m_renderer->flush();
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

  m_renderer->flush();
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

  m_renderer->flush();
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

void WorldPass::drawDrawable(WorldCamera const& camera, Drawable drawable) {
  drawable.position = camera.worldToScreen(drawable.position);
  drawable.scale(camera.pixelRatio() * TilePixels, drawable.position);

  if (drawable.isLine())
    drawable.linePart().width *= camera.pixelRatio();

  // draw the drawable if it's on screen
  // if it's not on screen, there's a random chance to pre-load
  // pre-load is not done on every tick because it's expensive to look up images with long paths
  if (RectF::withSize(Vec2F(), Vec2F(camera.screenSize())).intersects(drawable.boundBox(false)))
    m_drawablePainter->drawDrawable(drawable);
  else if (drawable.isImage() && Random::randf() < m_preloadTextureChance)
    m_assets->tryImage(drawable.imagePart().image);
}

void WorldPass::drawDrawableSet(WorldCamera const& camera, List<Drawable>& drawables) {
  for (Drawable& drawable : drawables)
    drawDrawable(camera, std::move(drawable));

  m_renderer->flush();
}

}
