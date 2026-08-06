#pragma once

#include "StarParallax.hpp"
#include "StarWorldRenderData.hpp"
#include "StarAssetTextureGroup.hpp"
#include "StarRenderer.hpp"
#include "StarWorldCamera.hpp"
#include "StarPerlin.hpp"
#include "StarRandomPoint.hpp"

namespace Star {

STAR_CLASS(EnvironmentPainter);

class EnvironmentPainter {
public:
  EnvironmentPainter(RendererPtr renderer);

  void update(float dt);

  // THE RENDER SIDE'S HALF OF pinSkyEpochTime, and the sky is not reproducible without it.
  //
  // The sun rays carry the only two per-RUN terms in the whole backdrop chain. m_rayPerlin is seeded
  // from Random::randu64(), whose source is Time::monotonicTicks() -- one draw per painter, so it is
  // constant within a process and different in the next one. m_timer accumulates real dt through the
  // harness's UNPAUSED load phase, whose duration is wall-clock, so its value at the freeze differs
  // run to run as well. (6e66f36e stopped the timer advancing AFTER the freeze; it never pinned the
  // value AT it.) Both feed per-ray alpha inside the env draw, so two runs of one binary render
  // different skies at an identical world state -- which is what a cross-run golden hash trips over.
  //
  // Idempotent in the seed: re-seeding per frame would rebuild the Perlin for nothing, and the point
  // is that the noise field is the SAME field in every run.
  void pinRayAnimation(uint64_t seed, double timer);

  void renderStars(float pixelRatio, Vec2F const& screenSize, SkyRenderData const& sky);
  void renderDebrisFields(float pixelRatio, Vec2F const& screenSize, SkyRenderData const& sky);
  void renderBackOrbiters(float pixelRatio, Vec2F const& screenSize, SkyRenderData const& sky);
  void renderPlanetHorizon(float pixelRatio, Vec2F const& screenSize, SkyRenderData const& sky);
  void renderFrontOrbiters(float pixelRatio, Vec2F const& screenSize, SkyRenderData const& sky);
  void renderSky(Vec2F const& screenSize, SkyRenderData const& sky);

  void renderParallaxLayers(Vec2F parallaxWorldPosition, WorldCamera const& camera, ParallaxLayers const& layers, SkyRenderData const& sky);

  void cleanup(int64_t textureTimeout);

private:
  static float const SunriseTime;
  static float const SunsetTime;
  static float const SunFadeRate;
  static float const MaxFade;
  static float const RayPerlinFrequency;
  static float const RayPerlinAmplitude;
  static int const RayCount;
  static float const RayMinWidth;
  static float const RayWidthVariance;
  static float const RayAngleVariance;
  static float const SunRadius;
  static float const RayColorDependenceLevel;
  static float const RayColorDependenceScale;
  static float const RayUnscaledAlphaVariance;
  static float const RayMinUnscaledAlpha;
  static Vec3B const RayColor;

  void drawRays(float pixelRatio, SkyRenderData const& sky, Vec2F start, float length, double time, float alpha);
  void drawRay(float pixelRatio,
      SkyRenderData const& sky,
      Vec2F start,
      float width,
      float length,
      float angle,
      double time,
      Vec3B color,
      float alpha);
  void drawOrbiter(float pixelRatio, Vec2F const& screenSize, SkyRenderData const& sky, SkyOrbiter const& orbiter);

  uint64_t starsHash(SkyRenderData const& sky, Vec2F const& viewSize) const;
  void setupStars(SkyRenderData const& sky);

  RendererPtr m_renderer;
  AssetTextureGroupPtr m_textureGroup;

  double m_timer;
  PerlinF m_rayPerlin;
  bool m_rayPerlinPinned = false;   // see pinRayAnimation: the seed is pinned once, the timer every call

  uint64_t m_starsHash{};
  List<TexturePtr> m_starTextures;
  shared_ptr<Random2dPointGenerator<pair<size_t, float>>> m_starGenerator;
  List<shared_ptr<Random2dPointGenerator<pair<String, float>, double>>> m_debrisGenerators;
};

}
