#include "StarWorldPainter.hpp"
#include "StarAnimation.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarAssets.hpp"
#include "StarJsonExtra.hpp"
#include "StarTelemetry.hpp"
#include "StarLogging.hpp"  // LogMap (/debug HUD per-pass GPU timings, Rung 0)
#include "StarCellularLightArray.hpp"  // spreadJacobiReference (CPU spread oracle for parity)

namespace Star {

// GPU-lighting FULL parity shadow-compare (diagnostics only, Slice 3). The GPU result (spread +
// point + cap) is calc-region sized; the CPU lightMap is the full (spread+point+cap) query-region
// result. With point lighting now on BOTH sides this is apples-to-apples in ANY scene. Crop the
// GPU result to the query sub-rect (offset = border on each axis) and compare per channel.
// Tolerance allows RGBA16F + additive float-add order + the DDA-vs-Xiaolin-Wu residual: mean abs
// <= 3/255, max <= 10/255.
static void shadowCompareFull(Image const& gpuCalc, Lightmap const& cpuQuery, int border,
    ImageView const& emission, ImageView const& obstacle,
    List<ColoredCellularLightArray::PointLight> const& lights, PointParameters const& params, unsigned iterations) {
  static auto mismatchCounter = Telemetry::counter("lighting.gpu.point.mismatch");
  unsigned qw = cpuQuery.width(), qh = cpuQuery.height();
  Vec2U gpuSize = gpuCalc.size();
  if (qw == 0 || qh == 0 || border < 0 || gpuSize[0] < qw + 2 * border || gpuSize[1] < qh + 2 * border)
    return;

  float const meanTol = 3.0f / 255.0f, maxTol = 10.0f / 255.0f;
  double sumAbs = 0.0;
  float worst = 0.0f;
  unsigned worstX = 0, worstY = 0;
  Vec3F worstCpu, worstGpu;
  float const* gpuData = (float const*)gpuCalc.data();
  for (unsigned y = 0; y < qh; ++y) {
    for (unsigned x = 0; x < qw; ++x) {
      Vec3F cpu = cpuQuery.get(x, y);
      size_t g = ((size_t)(y + border) * gpuSize[0] + (x + border)) * 3;
      Vec3F gpu(gpuData[g], gpuData[g + 1], gpuData[g + 2]);
      for (size_t c = 0; c < 3; ++c) {
        float e = std::fabs(cpu[c] - gpu[c]);
        sumAbs += e;
        if (e > worst) { worst = e; worstX = x; worstY = y; worstCpu = cpu; worstGpu = gpu; }
      }
    }
  }
  float mean = (float)(sumAbs / (qw * qh * 3));
  if (mean > meanTol || worst > maxTol) {
    mismatchCounter.inc(1);
    static int warnBudget = 8;   // rate-limited; the counter carries the running total
    if (warnBudget > 0) {
      --warnBudget;
      // 3-way localizer: compute the CPU reference (proven spreadJacobiReference + pointLightingReference
      // on the SAME exported inputs) at the worst cell. production==reference but GPU differs => GLSL
      // port bug at this geometry; reference==GPU but production differs => export/reference issue.
      Vec3F ref(-1, -1, -1);
      size_t w = emission.size[0], h = emission.size[1];
      if (w == gpuSize[0] && h == gpuSize[1] && emission.size == obstacle.size) {
        List<Vec3F> em; em.resize(w * h);
        List<uint8_t> ob; ob.resize(w * h);
        float const* eData = (float const*)emission.data;
        uint8_t const* oData = (uint8_t const*)obstacle.data;
        for (size_t x = 0; x < w; ++x)
          for (size_t y = 0; y < h; ++y) {
            size_t px = (y * w + x) * 3;
            em[x * h + y] = Vec3F(eData[px], eData[px + 1], eData[px + 2]);
            ob[x * h + y] = oData[px] > 127 ? 1 : 0;
          }
        auto spread = spreadJacobiReference(em, ob, w, h, SpreadParameters{params.spreadMaxAir, params.spreadMaxObstacle, params.brightnessLimit}, iterations);
        auto full = pointLightingReference(spread, ob, lights, w, h, params);
        ref = full[((size_t)worstX + border) * h + ((size_t)worstY + border)];
      }
      Logger::warn("GPU lighting full parity: mean={:.4f}/255 max={:.4f}/255 at ({},{}) cpu=({:.3f},{:.3f},{:.3f}) gpu=({:.3f},{:.3f},{:.3f}) ref=({:.3f},{:.3f},{:.3f})",
          mean * 255.0f, worst * 255.0f, worstX, worstY,
          worstCpu[0], worstCpu[1], worstCpu[2], worstGpu[0], worstGpu[1], worstGpu[2], ref[0], ref[1], ref[2]);
    }
  }
}

WorldPainter::WorldPainter() {
  m_assets = Root::singleton().assets();

  m_camera.setScreenSize({800, 600});
  m_camera.setCenterWorldPosition(Vec2F());
  m_camera.setPixelRatio(Root::singleton().configuration()->get("zoomLevel").toFloat());

  m_highlightConfig = m_assets->json("/highlights.config");
  for (auto p : m_highlightConfig.get("highlightDirectives").iterateObject())
    m_highlightDirectives.set(EntityHighlightEffectTypeNames.getLeft(p.first), {p.second.getString("underlay", ""), p.second.getString("overlay", "")});

  m_entityBarOffset = jsonToVec2F(m_assets->json("/rendering.config:entityBarOffset"));
  m_entityBarSpacing = jsonToVec2F(m_assets->json("/rendering.config:entityBarSpacing"));
  m_entityBarSize = jsonToVec2F(m_assets->json("/rendering.config:entityBarSize"));
  m_entityBarIconOffset = jsonToVec2F(m_assets->json("/rendering.config:entityBarIconOffset"));
  m_preloadTextureChance = m_assets->json("/rendering.config:preloadTextureChance").toFloat();
}

void WorldPainter::renderInit(RendererPtr renderer) {
  m_assets = Root::singleton().assets();

  m_renderer = std::move(renderer);
  auto textureGroup = m_renderer->createTextureGroup(TextureGroupSize::Large);
  m_textPainter = make_shared<TextPainter>(m_renderer, textureGroup);
  m_tilePainter = make_shared<TilePainter>(m_renderer);
  m_drawablePainter = make_shared<DrawablePainter>(m_renderer, make_shared<AssetTextureGroup>(textureGroup));
  m_environmentPainter = make_shared<EnvironmentPainter>(m_renderer);
  m_backdropPass = make_shared<BackdropPass>(m_renderer.get());
}

void WorldPainter::setCameraPosition(WorldGeometry const& geometry, Vec2F const& position) {
  m_camera.setWorldGeometry(geometry);
  m_camera.setCenterWorldPosition(position);
}

WorldCamera& WorldPainter::camera() {
  return m_camera;
}

void WorldPainter::update(float dt) {
  m_environmentPainter->update(dt);
}

LightmapResult WorldPainter::runGpuLightmapPass(WorldRenderData& renderData) {
  auto config = Root::singleton().configuration();
  if (!(config->get("lightingGpu").optBool().value(false) && renderData.lightingInputsValid))
    return {};   // GPU lighting off / inputs invalid -> caller uses the CPU lightMap
  // Slice 3: compute the COMPLETE lightmap on the GPU (spread + per-light point + cap) from the exported
  // emission/obstacle/point-light grids; processFull restores the world effect + binds the result as lightMap,
  // and returns {active=false} (caller falls back to CPU) if assets are missing.
  if (!m_gpuLightmapPass)
    m_gpuLightmapPass = make_shared<GpuLightmapPass>(m_renderer.get());
  auto lc = m_assets->json("/lighting.config:lighting");
  PointParameters params{
      lc.getFloat("pointMaxAir"), lc.getFloat("pointMaxObstacle"),
      lc.getFloat("pointObstacleBoost"),
      config->get("newLighting").optBool().value(true),   // pointAdditive (matches lightingCalc)
      lc.getFloat("spreadMaxAir"), lc.getFloat("spreadMaxObstacle"),
      lc.getFloat("brightnessLimit")};
  // Auto-scale spread Jacobi iterations to the emission's peak intensity. Production's Gauss-Seidel sweep
  // propagates fully in 2 sweeps; a parallel Jacobi needs ~ceil(maxIntensity * spreadMaxAir) steps to reach the
  // same distance (the de-risk's K bound). A fixed K under-propagated bright (>1.0) FU spread lights ->
  // dimmer-far-from-source cells. Scan the (small) emission for its max channel; clamp [8, cfg spread cap].
  float maxEmission = 0.0f;
  {
    auto const& em = renderData.lightingEmission;
    float const* ed = (float const*)em.data();
    size_t n = (size_t)em.size()[0] * em.size()[1] * 3;
    for (size_t i = 0; i < n; ++i)
      maxEmission = std::max(maxEmission, ed[i]);
  }
  unsigned cap = config->get("lightingGpuSpreadIterations").optUInt().value(64);
  unsigned iterations = std::min(cap, std::max(8u, (unsigned)std::ceil(maxEmission * params.spreadMaxAir)));
  bool shadowCompare = config->get("lightingGpuShadowCompare").optBool().value(false);
  Image gpuResult;
  float brightnessScale = config->get("lightingGpuBrightness").optFloat().value(1.0f);
  bool tonemap = config->get("lightingTonemap").optBool().value(false);
  LightmapResult lm = m_gpuLightmapPass->processFull(renderData.lightingEmission, renderData.lightingEmissionHalf,
      renderData.lightingObstacle, renderData.lightingObstacleR8, renderData.lightingPointLights,
      iterations, params, brightnessScale, tonemap, shadowCompare,
      config->get("lightingWorldUpscale").optFloat().value(1.0f), &gpuResult, renderData.lightMapBorder);
  // shadowCompare (diagnostics only): parity-check the GPU result against the CPU reference. It uses the border
  // the active result carries (lm.border == renderData.lightMapBorder), which now travels IN the result.
  if (lm.active && shadowCompare && gpuResult.size()[0] > 0)
    shadowCompareFull(gpuResult, renderData.lightMap, lm.border,
        renderData.lightingEmission, renderData.lightingObstacle, renderData.lightingPointLights, params, iterations);
  return lm;
}

void WorldPainter::render(WorldRenderData& renderData, function<bool()> lightWaiter) {
  m_camera.setScreenSize(m_renderer->screenSize());
  m_camera.setTargetPixelRatio(Root::singleton().configuration()->get("zoomLevel").toFloat());

  m_assets = Root::singleton().assets();

  m_tilePainter->setup(m_camera, renderData);

  // Stars, Debris Fields, Sky, and Orbiters

  // RENDER-PASS ABLATION MASK (task #141). The per-pass GL_TIME_ELAPSED timers are NOT ADDITIVE -- with 12 of
  // them their sum overshot the real frame by 5ms, because each bracket serialises the pipeline and measures
  // its own stall. They RANK passes; they cannot BUDGET them. The only honest way to cost a pass is to REMOVE
  // it and re-measure the whole frame. This mask does that, in a LIVE world (a frozen world is required for the
  // byte-identity gate but measures a floor, not real play -- and worse, a frozen+unthrottled world saturates
  // the GPU and manufactures pipeline stalls that do not exist in real play).
  // A SET bit RENDERS its pass; a clear bit ABLATES it. Only the passes listed here exist -- do not document a
  // bit that nothing reads, or setting it measures an un-ablated frame and reports the pass as free, which is
  // precisely the measurement lie this tool was built to prevent.
  //   bit0 env/sky   bit1 parallax
  // Base 0 (NOT 10): the mask is written in hex, and strtol(...,10) parses "0xF" as 0 -- silently ablating every
  // pass, the exact inverse of what was asked for.
  static int const PassAll = 0x3;
  static int const passMask = []() {
    char const* e = getenv("STAR_RENDERTEST_PASS_MASK");
    return e && *e ? (int)strtol(e, nullptr, 0) : PassAll;
  }();
  bool const ablateEnv      = !(passMask & 1);
  bool const ablateParallax = !(passMask & 2);
  // An ablated run must never be mistaken for a normal one -- say so, once.
  if (passMask != PassAll) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      Logger::warn("[rendertest] PASS ABLATION ACTIVE mask={:#x}: env={} parallax={}",
          passMask, ablateEnv ? "ABLATED" : "on", ablateParallax ? "ABLATED" : "on");
    }
  }

  // Sky/environment backdrop: the FBO-generation drop + the env retained cache (N-cadence refresh) + the
  // per-frame compose into "main". renderEnvironment records whether it refreshed, for the parallax arbiter.
  m_backdropPass->renderEnvironment(m_camera, renderData, *m_environmentPainter, ablateEnv);

  m_renderer->flush();

  bool lightMapUpdated = lightWaiter ? lightWaiter() : false;

  m_renderer->setEffectParameter("lightMapEnabled", !renderData.isFullbright);
  if (renderData.isFullbright) {
    m_renderer->setEffectTexture("lightMap", Image::filled(Vec2U(1, 1), { 255, 255, 255, 255 }, PixelFormat::RGB24));
    m_renderer->setEffectParameter("lightMapMultiplier", 1.0f);
    // Invariant: m_lightMapBorder always matches the currently-bound lightMap. The 1x1 white
    // texture is offset-invariant so this is defensive, but keeps the border consistent if the
    // binding persists into a later non-fullbright frame before a fresh lighting frame arrives.
    m_lightMapBorder = 0;
  } else {
    if (lightMapUpdated) {
      adjustLighting(renderData);
      // GPU-lightmap dispatch is pulled into runGpuLightmapPass(): it prepares the pass inputs and returns an
      // explicit LightmapResult (active + its matching border) instead of a bool plus a separately-read border.
      LightmapResult lm = runGpuLightmapPass(renderData);
      if (lm.active) {
        // The bound lightMap is the calc-region (border-padded) result; the border travels WITH the active
        // flag in the result, so the world shader's lightMapOffset can never pair an active GPU lightMap with a
        // stale/garbage border (it must NOT be reverse-derived from renderData.lightMap width, which is empty
        // => a garbage offset, dark world, when the CPU calc is skipped).
        m_lightMapBorder = lm.border;
      } else if (!renderData.lightMap.empty()) {
        // CPU lightMap upload (deep-gated; also the fallback when the GPU path is off/unavailable).
        static auto uploadTimer = Telemetry::timer("lighting.upload.us");
        TelemetryScope uploadScope(uploadTimer);
        m_renderer->setEffectTexture("lightMap", renderData.lightMap);
        m_lightMapBorder = 0;
      }
      // else (Slice 4): GPU pass failed AND no CPU lightMap this frame -- the skip-calculate latch raced a
      // transient GPU failure. Keep the previous lightMap binding for this one frame; reporting
      // m_gpuLightingActive=false (below) re-arms the CPU path on the lighting thread, so a real lightMap
      // returns within ~1 frame. (An empty upload would flash black.)
      // Slice 4: report this frame's GPU outcome so the lighting thread can drop the redundant CPU calculate().
      m_gpuLightingActive = lm.active;
    }
    m_renderer->setEffectParameter("lightMapMultiplier", m_assets->json("/rendering.config:lightMapMultiplier").toFloat());
    m_renderer->setEffectParameter("lightMapScale", Vec2F::filled(TilePixels * m_camera.pixelRatio()));
    // m_lightMapBorder persists across non-update frames to match the persistent lightMap binding.
    m_renderer->setEffectParameter("lightMapOffset",
        m_camera.worldToScreen(Vec2F(renderData.lightMinPosition) - Vec2F((float)m_lightMapBorder, (float)m_lightMapBorder)));
    m_renderer->setEffectParameter("lightmapBilinear",
        Root::singleton().configuration()->get("lightingWorldSampleBilinear").optBool().value(false));
    m_renderer->setEffectParameter("lightmapUpscale",
        Root::singleton().configuration()->get("lightingWorldUpscale").optFloat().value(1.0f));
  }

  // Parallax layers (env-cache-coupled backdrop: owns the parallax retained cache + the cross-surface arbiter).
  m_backdropPass->renderParallax(m_camera, renderData, *m_environmentPainter, ablateParallax);

  // Main world layers

  m_renderer->gpuTimer().begin("render.pass.world.gpu_us");
  Map<EntityRenderLayer, List<pair<EntityHighlightEffect, List<Drawable>>>> entityDrawables;
  for (auto& ed : renderData.entityDrawables) {
    for (auto& p : ed.layers)
      entityDrawables[p.first].append({ed.highlightEffect, std::move(p.second)});
  }

  auto entityDrawableIterator = entityDrawables.begin();
  auto renderEntitiesUntil = [this, &entityDrawables, &entityDrawableIterator](Maybe<EntityRenderLayer> until) {
    while (true) {
      if (entityDrawableIterator == entityDrawables.end())
        break;
      if (until && entityDrawableIterator->first >= *until)
        break;
      for (auto& edl : entityDrawableIterator->second)
        drawEntityLayer(std::move(edl.second), edl.first);
      ++entityDrawableIterator;
    }

    m_renderer->flush();
  };

  renderEntitiesUntil(RenderLayerBackgroundOverlay);
  drawDrawableSet(renderData.backgroundOverlays);
  renderEntitiesUntil(RenderLayerBackgroundTile);
  m_tilePainter->renderBackground(m_camera);
  renderEntitiesUntil(RenderLayerPlatform);
  m_tilePainter->renderMidground(m_camera);
  renderEntitiesUntil(RenderLayerBackParticle);
  renderParticles(renderData, Particle::Layer::Back);
  renderEntitiesUntil(RenderLayerLiquid);
  m_tilePainter->renderLiquid(m_camera);
  renderEntitiesUntil(RenderLayerMiddleParticle);
  renderParticles(renderData, Particle::Layer::Middle);
  renderEntitiesUntil(RenderLayerForegroundTile);
  m_tilePainter->renderForeground(m_camera);
  renderEntitiesUntil(RenderLayerForegroundOverlay);
  drawDrawableSet(renderData.foregroundOverlays);
  renderEntitiesUntil(RenderLayerFrontParticle);
  renderParticles(renderData, Particle::Layer::Front);
  renderEntitiesUntil(RenderLayerOverlay);
  drawDrawableSet(renderData.nametags);
  renderBars(renderData);
  renderEntitiesUntil({});
  m_renderer->gpuTimer().end("render.pass.world.gpu_us");

  m_renderer->gpuTimer().begin("render.pass.compose.gpu_us");
  auto dimLevel = round(renderData.dimLevel * 255);
  if (dimLevel != 0)
    m_renderer->render(renderFlatRect(RectF::withSize({}, Vec2F(m_camera.screenSize())), Vec4B(renderData.dimColor, dimLevel), 0.0f));
  m_renderer->gpuTimer().end("render.pass.compose.gpu_us");

  // Rung 0: surface the per-pass GPU timings (populated only under deep telemetry) on the /debug HUD.
  for (auto const& key : {"render.pass.environment.gpu_us", "render.pass.parallax.gpu_us",
                          "render.pass.world.gpu_us", "render.pass.compose.gpu_us"}) {
    if (auto us = m_renderer->gpuTimer().lastMicros(key))
      LogMap::set(String(key), strf("{:05d}us", *us));
  }

  int64_t textureTimeout = m_assets->json("/rendering.config:textureTimeout").toInt();
  m_textPainter->cleanup(textureTimeout);
  m_drawablePainter->cleanup(textureTimeout);
  m_environmentPainter->cleanup(textureTimeout);
  m_tilePainter->cleanup();
}

void WorldPainter::adjustLighting(WorldRenderData& renderData) {
  m_tilePainter->adjustLighting(renderData);
}

void WorldPainter::renderParticles(WorldRenderData& renderData, Particle::Layer layer) {
  const int textParticleFontSize = m_assets->json("/rendering.config:textParticleFontSize").toInt();
  const RectF particleRenderWindow = RectF::withSize(Vec2F(), Vec2F(m_camera.screenSize())).padded(m_assets->json("/rendering.config:particleRenderWindowPadding").toInt());

  if (!renderData.particles)
    return;

  for (Particle const& particle : *renderData.particles) {
    if (layer != particle.layer)
      continue;

    Vec2F position = m_camera.worldToScreen(particle.position);

    if (!particleRenderWindow.contains(position))
      continue;

    Vec2F size = Vec2F::filled(particle.size * m_camera.pixelRatio());

    if (particle.type == Particle::Type::Ember) {
      m_renderer->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(),
        RectF(position - size / 2, position + size / 2),
        particle.color.toRgba(),
        particle.fullbright ? 0.0f : 1.0f);

    } else if (particle.type == Particle::Type::Streak) {
      // Draw a rotated quad streaking in the direction the particle is coming from.
      // Sadly this looks awful.
      Vec2F dir = particle.velocity.normalized();
      Vec2F sideHalf = dir.rot90() * m_camera.pixelRatio() * particle.size / 2;
      float length = particle.length * m_camera.pixelRatio();
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
      drawDrawable(std::move(drawable));

    } else if (particle.type == Particle::Type::Text) {
      Vec2F position = m_camera.worldToScreen(particle.position);
      int size = min(128.0f, round((float)textParticleFontSize * m_camera.pixelRatio() * particle.size));
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

void WorldPainter::renderBars(WorldRenderData& renderData) {
  auto offset = m_entityBarOffset;
  for (auto const& bar : renderData.overheadBars) {
    auto position = bar.entityPosition + offset;
    offset += m_entityBarSpacing;
    if (bar.icon) {
      auto iconDrawPosition = position - (m_entityBarSize / 2).round() + m_entityBarIconOffset;
      drawDrawable(Drawable::makeImage(*bar.icon, 1.0f / TilePixels, true, iconDrawPosition));
    }

    if (!bar.detailOnly) {
      auto fullBar = RectF({}, {m_entityBarSize.x() * bar.percentage, m_entityBarSize.y()});
      auto emptyBar = RectF({m_entityBarSize.x() * bar.percentage, 0.0f}, m_entityBarSize);
      auto fullColor = bar.color;
      auto emptyColor = Color::Black;

      drawDrawable(Drawable::makePoly(PolyF(emptyBar), emptyColor, position));
      drawDrawable(Drawable::makePoly(PolyF(fullBar), fullColor, position));
    }
  }

  m_renderer->flush();
}

void WorldPainter::drawEntityLayer(List<Drawable> drawables, EntityHighlightEffect highlightEffect) {
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
          drawDrawable(std::move(underlayDrawable));
        }
      }
    }

    // second pass, draw main drawables and overlays
    auto overlayDirectives = m_highlightDirectives[highlightEffect.type].second;
    for (auto& d : drawables) {
      drawDrawable(d);
      if (!overlayDirectives.empty() && d.isImage()) {
        auto overlayDrawable = Drawable(d);
        overlayDrawable.fullbright = true;
        overlayDrawable.color = Color::rgbaf(1, 1, 1, highlightEffect.level * d.color.alphaF());
        overlayDrawable.imagePart().addDirectives(overlayDirectives, true);
        drawDrawable(std::move(overlayDrawable));
      }
    }
  } else {
    for (auto& d : drawables)
      drawDrawable(std::move(d));
  }
}

void WorldPainter::drawDrawable(Drawable drawable) {
  drawable.position = m_camera.worldToScreen(drawable.position);
  drawable.scale(m_camera.pixelRatio() * TilePixels, drawable.position);

  if (drawable.isLine())
    drawable.linePart().width *= m_camera.pixelRatio();

  // draw the drawable if it's on screen
  // if it's not on screen, there's a random chance to pre-load
  // pre-load is not done on every tick because it's expensive to look up images with long paths
  if (RectF::withSize(Vec2F(), Vec2F(m_camera.screenSize())).intersects(drawable.boundBox(false)))
    m_drawablePainter->drawDrawable(drawable);
  else if (drawable.isImage() && Random::randf() < m_preloadTextureChance)
    m_assets->tryImage(drawable.imagePart().image);
}

void WorldPainter::drawDrawableSet(List<Drawable>& drawables) {
  for (Drawable& drawable : drawables)
    drawDrawable(std::move(drawable));

  m_renderer->flush();
}

}
