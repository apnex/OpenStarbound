#include "StarNetworkedAnimator.hpp"
#include "StarJson.hpp"
#include "gtest/gtest.h"

using namespace Star;

namespace {
  // Minimal animator: one part "body" on a 1-frame state; no groups/transforms (static).
  char const* kCfg = R"JSON({
    "globalTagDefaults": {},
    "animatedParts": { "stateTypes": {}, "parts": {
      "body": { "properties": { "zLevel": 0, "image": "/a.png" } } } },
    "transformationGroups": {}, "rotationGroups": {}, "particleEmitters": {},
    "lights": {}, "sounds": {}, "effects": {}
  })JSON";

  NetworkedAnimator makeAnim() { return NetworkedAnimator(Json::parse(kCfg), "/"); }
}

TEST(NetworkedAnimator, RenderVersionBumpsOnMasterSetters) {
  auto a = makeAnim();
  uint64_t v0 = a.renderVersion();
  a.setZoom(2.0f);
  EXPECT_GT(a.renderVersion(), v0);
  uint64_t v1 = a.renderVersion();
  a.setFlipped(true);
  EXPECT_GT(a.renderVersion(), v1);
  uint64_t v2 = a.renderVersion();
  a.setGlobalTag("x", String("y"));
  EXPECT_GT(a.renderVersion(), v2);
}

// operator= replaces every drawable-affecting member wholesale (engine does this on
// live objects, e.g. Humanoid identity reload), so it must invalidate too.
TEST(NetworkedAnimator, RenderVersionBumpsOnAssignment) {
  auto a = makeAnim();
  auto b = makeAnim();
  b.setZoom(3.0f);

  uint64_t v0 = a.renderVersion();
  a = b;  // copy assignment
  EXPECT_GT(a.renderVersion(), v0);

  uint64_t v1 = a.renderVersion();
  a = makeAnim();  // move assignment
  EXPECT_GT(a.renderVersion(), v1);
}

// A discrete networked change applied via the net funnel must bump renderVersion;
// a pure interpolation tick (no discrete change) must NOT.
//
// NetworkedAnimator is a NetElementSyncGroup; in production it is synced inside an
// entity's NetElementTop-wrapped group.  NetElementTop<NetworkedAnimator> gives the
// same real net path (netStore/netLoad full sync, writeNetDelta/readNetDelta with a
// live NetElementVersion) via writeNetState/readNetState.  Slaves never call the
// setters -- deserialization writes NetElement storage directly -- so the bump must
// come from the netElementsNeedLoad funnel.
TEST(NetworkedAnimator, RenderVersionBumpsOnNetApplyDiscreteOnly) {
  NetElementTop<NetworkedAnimator> master;
  NetElementTop<NetworkedAnimator> slave;
  static_cast<NetworkedAnimator&>(master) = makeAnim();
  static_cast<NetworkedAnimator&>(slave) = makeAnim();
  slave.enableNetInterpolation();

  master.setZoom(3.0f);  // discrete change on master

  // Serialize master state, apply to slave through the real net path (full load).
  auto initial = master.writeNetState();
  uint64_t before = slave.renderVersion();
  slave.readNetState(initial.first);             // netLoad -> netElementsNeedLoad(true)
  EXPECT_GT(slave.renderVersion(), before);      // slave saw the discrete zoom change
  uint64_t after = slave.renderVersion();

  // A delta carrying only a continuously-interpolated, non-drawable-discrete change
  // (animationRate) must not bump, and neither may the net interpolation ticks that
  // follow it (or the static cache thrashes on moving entities).
  master.setAnimationRate(2.0f);
  auto delta1 = master.writeNetState(initial.second);
  ASSERT_FALSE(delta1.first.empty());
  slave.readNetState(delta1.first);              // readNetDelta -> netElementsNeedLoad(false)
  EXPECT_EQ(slave.renderVersion(), after);       // no discrete change -> no bump
  EXPECT_EQ(slave.animationRate(), 2.0f);        // ...but the delta really applied
  slave.tickNetInterpolation(0.05f);             // pure interpolation tick -> no bump
  EXPECT_EQ(slave.renderVersion(), after);
  slave.tickNetInterpolation(0.05f);
  EXPECT_EQ(slave.renderVersion(), after);

  // A discrete change arriving through the delta funnel must bump.
  master.setZoom(4.0f);
  auto delta2 = master.writeNetState(delta1.second);
  ASSERT_FALSE(delta2.first.empty());
  slave.readNetState(delta2.first);
  EXPECT_GT(slave.renderVersion(), after);
}
