#include "StarAnimatedPartSet.hpp"
#include "StarJson.hpp"
#include "StarMatrix3.hpp"
#include "gtest/gtest.h"

using namespace Star;

namespace {
  // animatorVersion 1 so the affine-transform block (cpp:362-397) is active.
  // "movement": idle = 1-frame end; walk = 2-frame loop (each frame 0.5s at cycle 1.0).
  // "spin": 2-frame loop (frame stays 0 within a 0.5s sub-frame window at cycle 1.0).
  // part "body" listens to movement; part "arm" listens to spin and carries per-frame
  //   transforms that DIFFER between frame 0 and frame 1, each led by a "reset" op so the
  //   affine recomputation starts from identity (neutralizing cross-tick accumulation).
  //   This makes the interpolated rotate depend purely on frameProgress sub-frame.
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
            "on" : {
              "properties" : { "interpolated" : true },
              "frameProperties" : {
                "transforms" : [
                  [ [ "reset" ], [ "rotate", 0.0, [0.0, 0.0] ] ],
                  [ [ "reset" ], [ "rotate", 1.5, [0.0, 0.0] ] ]
                ]
              }
            }
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
  EXPECT_EQ(st.stateName, "idle");
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

// GUARD (anti-freeze, part transform): the arm's affine transform must change between two
// sub-frame updates SOLELY because frameProgress advanced. The per-frame transforms each begin
// with a "reset" op, so processTransforms starts from identity rather than the accumulated prior
// affine -- frame 0 yields R(0.0) and frame 1 yields R(1.5) independent of history. With the
// integer frame pinned to 0 (asserted below), the only thing feeding setAnimationAffineTransform's
// lerp between mat and nextMat is frameProgress (0.2 -> 0.4). If a refactor froze/removed
// frameProgress-driven interpolation, mat==result both ticks and this guard FAILS.
TEST(AnimatedPartSet, InterpolatedTransformAdvancesWithinFrame) {
  auto set = makeSet();
  set.update(0.1f);
  Mat3F m1 = set.activePart("arm").animationAffineTransform();
  EXPECT_EQ(set.activeState("spin").frame, 0u); // precondition: still within frame 0
  set.update(0.1f);
  Mat3F m2 = set.activePart("arm").animationAffineTransform();
  EXPECT_EQ(set.activeState("spin").frame, 0u); // precondition: still within frame 0
  EXPECT_NE(m1, m2); // must advance purely from frameProgress interpolation
}
