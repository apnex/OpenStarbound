#include "StarAnimatedPartSet.hpp"
#include "StarJson.hpp"
#include "gtest/gtest.h"

using namespace Star;

namespace {
  // animatorVersion 1 so the affine-transform block (cpp:362-397) is active.
  // "movement": idle = 1-frame end; walk = 2-frame loop (each frame 0.5s at cycle 1.0).
  // "spin": single 1-frame state carrying an interpolated rotate transform (sub-frame).
  // part "body" listens to movement; part "arm" listens to spin (interpolated transform).
  char const* kConfig = R"JSON(
  {
    "stateTypes" : {
      "movement" : {
        "default" : "idle",
        "states" : {
          "idle" : { "frames" : 1, "cycle" : 1.0, "mode" : "end",  "properties" : { "foo" : "idleVal" } },
          "walk" : { "frames" : 2, "cycle" : 1.0, "mode" : "loop", "properties" : { "foo" : "walkVal" },
                     "frameProperties" : { "label" : [ "w0", "w1" ] } }
        }
      },
      "spin" : {
        "default" : "on",
        "states" : { "on" : { "frames" : 2, "cycle" : 1.0, "mode" : "loop" } }
      }
    },
    "parts" : {
      "body" : {
        "properties" : { "zlevel" : 1 },
        "partStates" : {
          "movement" : {
            "idle" : { "properties" : { "image" : "body_idle" } },
            "walk" : { "properties" : { "image" : "body_walk" },
                       "frameProperties" : { "image" : [ "body_w0", "body_w1" ] } }
          }
        }
      },
      "arm" : {
        "partStates" : {
          "spin" : {
            "on" : { "properties" : {
              "interpolated" : true,
              "transforms" : [ [ "rotate", 1.0, [0.0, 0.0] ] ]
            } }
          }
        }
      }
    }
  }
  )JSON";

  AnimatedPartSet makeSet() { return AnimatedPartSet(Json::parse(kConfig), 1); }
}

// Default state resolves to idle/frame0 with merged properties (also covers first-read-after-construction).
TEST(AnimatedPartSet, DefaultStateResolves) {
  auto set = makeSet();
  auto const& st = set.activeState("movement");
  EXPECT_EQ(st.stateName, String("idle"));
  EXPECT_EQ(st.frame, 0u);
  EXPECT_EQ(Json(st.properties).getString("foo"), "idleVal");
}

// walk advances integer frame across update(dt) and merges per-frame properties.
TEST(AnimatedPartSet, WalkAdvancesFrameAndProperties) {
  auto set = makeSet();
  ASSERT_TRUE(set.setActiveState("movement", "walk"));
  EXPECT_EQ(set.activeState("movement").frame, 0u);
  EXPECT_EQ(Json(set.activeState("movement").properties).getString("label"), "w0");
  set.update(0.6f); // crosses the 0.5s frame boundary -> frame 1
  EXPECT_EQ(set.activeState("movement").frame, 1u);
  EXPECT_EQ(Json(set.activeState("movement").properties).getString("label"), "w1");
}

// Part resolves the matching image for the active state+frame.
TEST(AnimatedPartSet, PartResolvesImagePerFrame) {
  auto set = makeSet();
  EXPECT_EQ(Json(set.activePart("body").properties).getString("image"), "body_idle");
  ASSERT_TRUE(set.setActiveState("movement", "walk"));
  EXPECT_EQ(Json(set.activePart("body").properties).getString("image"), "body_w0");
  set.update(0.6f);
  EXPECT_EQ(Json(set.activePart("body").properties).getString("image"), "body_w1");
}

// GUARD (anti-freeze): frameProgress must advance on sub-frame updates that do NOT cross a frame.
TEST(AnimatedPartSet, FrameProgressAdvancesWithinFrame) {
  auto set = makeSet();
  ASSERT_TRUE(set.setActiveState("movement", "walk"));
  set.update(0.1f);
  float p1 = set.activeState("movement").frameProgress;
  EXPECT_EQ(set.activeState("movement").frame, 0u);
  set.update(0.1f);
  float p2 = set.activeState("movement").frameProgress;
  EXPECT_EQ(set.activeState("movement").frame, 0u);
  EXPECT_GT(p2, p1); // must NOT freeze
}

// GUARD (anti-freeze, part transform): an interpolated transform part keeps moving sub-frame.
TEST(AnimatedPartSet, InterpolatedTransformAdvancesWithinFrame) {
  auto set = makeSet();
  set.update(0.1f);
  Mat3F m1 = set.activePart("arm").animationAffineTransform();
  set.update(0.1f);
  Mat3F m2 = set.activePart("arm").animationAffineTransform();
  EXPECT_NE(m1, m2); // rotate interpolates with frameProgress every tick
}
