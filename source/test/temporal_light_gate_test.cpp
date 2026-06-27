#include "StarTemporalLightingGate.hpp"

#include "gtest/gtest.h"

using namespace Star;

static LightSource lightAt(float x, float y, Vec3F color, LightType type = LightType::Point) {
  LightSource l;
  l.position = {x, y};
  l.color = color;
  l.type = type;
  l.pointBeam = 0.0f;
  l.beamAngle = 0.0f;
  l.beamAmbience = 0.0f;
  return l;
}

TEST(TemporalLightingGate, FlickerIsCalm) {
  // Same positions, different colours (flicker) -> identical signature.
  List<LightSource> a{lightAt(10, 20, {1.0f, 1.0f, 1.0f})};
  List<LightSource> b{lightAt(10, 20, {0.7f, 0.7f, 0.7f})};
  EXPECT_TRUE(TemporalLightingGate::signatureOf(a) == TemporalLightingGate::signatureOf(b));
}

TEST(TemporalLightingGate, MoveIsActivity) {
  List<LightSource> a{lightAt(10, 20, {1.0f, 1.0f, 1.0f})};
  List<LightSource> b{lightAt(11, 20, {1.0f, 1.0f, 1.0f})};
  EXPECT_FALSE(TemporalLightingGate::signatureOf(a) == TemporalLightingGate::signatureOf(b));
}

TEST(TemporalLightingGate, SignatureOrderIndependent) {
  List<LightSource> a{lightAt(1, 1, {1, 1, 1}), lightAt(9, 9, {1, 1, 1})};
  List<LightSource> b{lightAt(9, 9, {1, 1, 1}), lightAt(1, 1, {1, 1, 1})};
  EXPECT_TRUE(TemporalLightingGate::signatureOf(a) == TemporalLightingGate::signatureOf(b));
}

TEST(TemporalLightingGate, CountAndTypeMatter) {
  List<LightSource> one{lightAt(5, 5, {1, 1, 1})};
  List<LightSource> two{lightAt(5, 5, {1, 1, 1}), lightAt(7, 7, {1, 1, 1})};
  EXPECT_FALSE(TemporalLightingGate::signatureOf(one) == TemporalLightingGate::signatureOf(two));
  List<LightSource> spread{lightAt(5, 5, {1, 1, 1}, LightType::Spread)};
  List<LightSource> point{lightAt(5, 5, {1, 1, 1}, LightType::Point)};
  EXPECT_FALSE(TemporalLightingGate::signatureOf(spread) == TemporalLightingGate::signatureOf(point));
}

TEST(TemporalLightingGate, Decision) {
  TemporalLightingGate::Baseline base;
  base.valid = true;
  base.tileEpoch = 5;
  base.lightRange = RectI(0, 0, 100, 100);
  base.lightSig = TemporalLightingGate::signatureOf({lightAt(10, 20, {1, 1, 1})});
  base.lastRecomputeMs = 1000;
  auto sig = base.lightSig;
  RectI range = base.lightRange;

  // off (disabled) -> always recompute
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(base, false, 33.0, 5, range, sig, 1010));
  // floorMs <= 0 -> always recompute
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(base, true, 0.0, 5, range, sig, 1010));
  // calm within the floor -> skip
  EXPECT_FALSE(TemporalLightingGate::shouldRecompute(base, true, 33.0, 5, range, sig, 1010));
  // floor elapsed -> recompute
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(base, true, 33.0, 5, range, sig, 1040));
  // tile edit -> recompute
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(base, true, 33.0, 6, range, sig, 1010));
  // scroll (lightRange changed) -> recompute
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(base, true, 33.0, 5, RectI(1, 0, 101, 100), sig, 1010));
  // entity light moved -> recompute
  auto moved = TemporalLightingGate::signatureOf({lightAt(11, 20, {1, 1, 1})});
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(base, true, 33.0, 5, range, moved, 1010));
  // no baseline -> recompute
  TemporalLightingGate::Baseline none;
  EXPECT_TRUE(TemporalLightingGate::shouldRecompute(none, true, 33.0, 5, range, sig, 1010));
}
