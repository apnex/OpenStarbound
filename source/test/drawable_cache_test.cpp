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
