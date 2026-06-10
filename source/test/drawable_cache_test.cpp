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
