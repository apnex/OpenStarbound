#include "StarNetworkedAnimator.hpp"
#include "StarJson.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarFormat.hpp"
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

// Conservative + transitive static/live partition.
//
// Schema note (verified against AnimatedPartSet's constructor): a part config only
// has "properties" and "partStates" keys; anchorPart/rotationGroup/transformationGroups/
// transforms all live INSIDE "properties" (partTransformation reads the merged
// activePart.properties).
TEST(NetworkedAnimator, StaticLivePartition) {
  // body: static. fan: has a "transforms" property -> LIVE. mount: anchored to fan -> LIVE (transitive).
  char const* cfg = R"JSON({
    "animatedParts": { "stateTypes": {}, "parts": {
      "body":  { "properties": { "image": "/b.png" } },
      "fan":   { "properties": { "image": "/f.png", "transforms": [ ["rotate", 1.0, [0,0]] ] } },
      "mount": { "properties": { "image": "/m.png", "anchorPart": "fan" } }
    } },
    "transformationGroups": {}, "rotationGroups": {}, "effects": {}, "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  EXPECT_TRUE(a.partIsStaticCacheable("body"));
  EXPECT_FALSE(a.partIsStaticCacheable("fan"));    // own transforms
  EXPECT_FALSE(a.partIsStaticCacheable("mount"));  // anchored to a LIVE part (transitive)
}

// The remaining LIVE triggers (rotation group, interpolated transformation group,
// active flash effect), plus the audit's verified-safe-as-static cases
// (angularVelocity==0 rotation, non-interpolated non-animated group).
TEST(NetworkedAnimator, StaticLivePartitionTriggers) {
  char const* cfg = R"JSON({
    "animatedParts": { "stateTypes": {}, "parts": {
      "body":   { "properties": { "image": "/b.png" } },
      "turret": { "properties": { "image": "/t.png", "rotationGroup": "aim" } },
      "vane":   { "properties": { "image": "/v.png", "rotationGroup": "wind" } },
      "piston": { "properties": { "image": "/p.png", "transformationGroups": ["slide"] } },
      "frame":  { "properties": { "image": "/fr.png", "transformationGroups": ["fixed"] } }
    } },
    "transformationGroups": { "slide": { "interpolated": true }, "fixed": {} },
    "rotationGroups": { "aim": { "angularVelocity": 2.0 }, "wind": { "angularVelocity": 0.0 } },
    "effects": { "blink": { "type": "flash", "time": 0.5, "directives": "fade=ffffff=0.85" } },
    "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  EXPECT_FALSE(a.partIsStaticCacheable("turret"));  // angularVelocity != 0: continuous currentAngle approach
  EXPECT_TRUE(a.partIsStaticCacheable("vane"));     // angularVelocity == 0 rotation: verified-safe-as-static
  EXPECT_FALSE(a.partIsStaticCacheable("piston"));  // interpolated transformation group (slave-side lerp)
  EXPECT_TRUE(a.partIsStaticCacheable("frame"));    // non-interpolated, not state-animated: static
  EXPECT_TRUE(a.partIsStaticCacheable("body"));

  // An enabled flash-type effect toggles purely off the per-tick effect timer and its
  // directive is prepended to EVERY part: global cache-bust.
  a.setEffectEnabled("blink", true);
  EXPECT_FALSE(a.partIsStaticCacheable("body"));
  a.setEffectEnabled("blink", false);
  EXPECT_TRUE(a.partIsStaticCacheable("body"));
}

// Slave-side net-apply coverage for the two static-safe carve-outs in
// partIsStaticCacheable.  An angularVelocity==0 rotation group and a
// non-interpolated transformation group are STATIC only because every
// drawable-affecting change to them funnels through a renderVersion bump: on a
// slave, rotateGroup / *TransformationGroup arrive as NetElement
// deserialization (setters never run), so netElementsNeedLoad must value-diff
// RotationGroup::targetAngle and the six non-interpolated group floats.
// Interpolated groups lerp their floats every tick on slaves; parts that
// reference them are LIVE already, so the funnel must NOT bump for them (or
// the static cache thrashes on every continuously-animated entity).
TEST(NetworkedAnimator, RenderVersionBumpsOnNetApplyGroupChanges) {
  char const* cfg = R"JSON({
    "animatedParts": { "stateTypes": {}, "parts": {
      "vane":   { "properties": { "image": "/v.png", "rotationGroup": "wind" } },
      "frame":  { "properties": { "image": "/fr.png", "transformationGroups": ["fixed"] } },
      "piston": { "properties": { "image": "/p.png", "transformationGroups": ["slide"] } }
    } },
    "transformationGroups": { "fixed": {}, "slide": { "interpolated": true } },
    "rotationGroups": { "wind": { "angularVelocity": 0.0 } },
    "effects": {}, "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";

  NetElementTop<NetworkedAnimator> master;
  NetElementTop<NetworkedAnimator> slave;
  static_cast<NetworkedAnimator&>(master) = NetworkedAnimator(Json::parse(cfg), "/");
  static_cast<NetworkedAnimator&>(slave) = NetworkedAnimator(Json::parse(cfg), "/");
  slave.enableNetInterpolation();

  auto initial = master.writeNetState();
  slave.readNetState(initial.first);

  // The carve-outs under test: both parts are STATIC on the slave, which is
  // only sound if the deltas below bump renderVersion.
  EXPECT_TRUE(slave.partIsStaticCacheable("vane"));
  EXPECT_TRUE(slave.partIsStaticCacheable("frame"));
  EXPECT_FALSE(slave.partIsStaticCacheable("piston"));
  uint64_t v0 = slave.renderVersion();

  // rotateGroup on a 0-angularVelocity group: the slave's update() snaps
  // currentAngle to the netted targetAngle, so the delta-apply must bump.
  master.rotateGroup("wind", 1.5f);
  auto delta1 = master.writeNetState(initial.second);
  ASSERT_FALSE(delta1.first.empty());
  slave.readNetState(delta1.first);
  EXPECT_GT(slave.renderVersion(), v0);
  EXPECT_TRUE(slave.partIsStaticCacheable("vane"));  // stays static; now funnel-covered
  uint64_t v1 = slave.renderVersion();

  // translateTransformationGroup on a non-interpolated group: the six floats
  // are networked with no interpolators (discrete at delta-apply): must bump.
  master.translateTransformationGroup("fixed", Vec2F(1, 0));
  auto delta2 = master.writeNetState(delta1.second);
  ASSERT_FALSE(delta2.first.empty());
  slave.readNetState(delta2.first);
  EXPECT_GT(slave.renderVersion(), v1);
  EXPECT_TRUE(slave.partIsStaticCacheable("frame"));
  uint64_t v2 = slave.renderVersion();

  // Interpolated group: delta applies into interpolation data points and the
  // floats lerp across the following ticks.  No bump at apply or per tick.
  master.translateTransformationGroup("slide", Vec2F(2, 0));
  auto delta3 = master.writeNetState(delta2.second);
  ASSERT_FALSE(delta3.first.empty());
  slave.readNetState(delta3.first, 0.1f);
  EXPECT_EQ(slave.renderVersion(), v2);
  slave.tickNetInterpolation(0.05f);  // mid-lerp: slide's floats are changing
  EXPECT_EQ(slave.renderVersion(), v2);
  slave.tickNetInterpolation(0.05f);
  EXPECT_EQ(slave.renderVersion(), v2);
}

namespace {
  // Richer config for the cache parity test: two-plus static parts on distinct
  // zLevels (bg/body/frame -- frame on a non-interpolated transformation
  // group), one LIVE part with a "transforms" property (fan), and one part
  // anchored to it (mount -> LIVE transitively).  body is state-animated with
  // a multi-frame "run" state (<frame> image tag) so frame advances exercise
  // generation()-keyed invalidation; mount's image carries a <color> tag for
  // the mid-loop setGlobalTag flip.  Every part is non-centered: these image
  // paths are fake, and both Drawable::makeImage(centered=true) and
  // addDirectives with keepImageCenterPosition query the image metadata
  // database (a real asset load).
  char const* kRichCfg = R"JSON({
    "version": 1,
    "animatedParts": {
      "stateTypes": { "motion": { "default": "idle", "states": {
        "idle": { "frames": 1 },
        "run":  { "frames": 4, "cycle": 0.4, "mode": "loop" }
      } } },
      "parts": {
        "bg":    { "properties": { "zLevel": -1, "image": "/bg.png", "centered": false } },
        "body":  { "properties": { "zLevel": 0, "fullbright": true, "centered": false },
                   "partStates": { "motion": {
                     "idle": { "properties": { "image": "/body_idle.png" } },
                     "run":  { "properties": { "image": "/body_run_<frame>.png" } } } } },
        "frame": { "properties": { "zLevel": 1, "image": "/fr.png", "centered": false,
                   "transformationGroups": ["fixed"],
                   "processingDirectives": "?multiply=<tint>" } },
        "fan":   { "properties": { "zLevel": 2, "image": "/fan.png", "centered": false,
                   "transforms": [ ["rotate", 0.3, [0, 0]] ] } },
        "mount": { "properties": { "zLevel": 3, "image": "/mount_<color>.png", "centered": false,
                   "anchorPart": "fan", "offset": [0.5, 0.25] } }
      }
    },
    "transformationGroups": { "fixed": {} },
    "rotationGroups": {}, "effects": {}, "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";

  NetworkedAnimator makeRichAnim() { return NetworkedAnimator(Json::parse(kRichCfg), "/"); }

  // Drawable has no operator== in the engine; compare the salient fields
  // (image part path+directives, transformation, position, color, fullbright).
  void expectDrawableEq(Drawable const& cached, Drawable const& rebuilt, String const& where) {
    ASSERT_EQ(cached.isImage(), rebuilt.isImage()) << where.utf8Ptr();
    if (cached.isImage()) {
      EXPECT_TRUE(cached.imagePart().image == rebuilt.imagePart().image)
          << where.utf8Ptr() << " image: " << AssetPath::join(cached.imagePart().image).utf8Ptr()
          << " vs " << AssetPath::join(rebuilt.imagePart().image).utf8Ptr();
      EXPECT_TRUE(cached.imagePart().transformation == rebuilt.imagePart().transformation) << where.utf8Ptr() << " transformation";
    }
    EXPECT_TRUE(cached.position == rebuilt.position) << where.utf8Ptr() << " position";
    EXPECT_TRUE(cached.color == rebuilt.color) << where.utf8Ptr() << " color";
    EXPECT_EQ(cached.fullbright, rebuilt.fullbright) << where.utf8Ptr() << " fullbright";
  }

  void expectParity(List<pair<Drawable, float>> const& cached, List<pair<Drawable, float>> const& rebuilt, int i) {
    ASSERT_EQ(cached.size(), rebuilt.size()) << "i=" << i;
    for (size_t k = 0; k < cached.size(); ++k) {
      String where = strf("i={} k={}", i, k);
      EXPECT_EQ(cached[k].second, rebuilt[k].second) << where.utf8Ptr() << " zLevel";
      expectDrawableEq(cached[k].first, rebuilt[k].first, where);
    }
  }
}

// With the cache ON, the drawables produced must EQUAL the rebuild path across
// a sequence of updates and state changes (shadow-compare).  This pins
// correctness without a display.  Mid-loop master flips (setGlobalTag /
// translateTransformationGroup -> renderVersion bump; setState -> generation
// bump + re-partition) force invalidation and rebuild mid-test, and the second
// cache-path call per iteration is a guaranteed pure cache HIT (no state
// change since the first call).
TEST(DrawableCache, CachedEqualsRebuiltAcrossUpdates) {
  auto a = makeRichAnim();
  // frame's processingDirectives carries a <tint> tag: version-1 directives
  // are tag-substituted (and the engine's maybeLookupTagsView path requires at
  // least one tag in the string), so resolve it to a real color up front.
  a.setGlobalTag("tint", String("ff0000"));
  auto config = Root::singleton().configuration();
  config->set("renderDrawableCache", true);
  for (int i = 0; i < 30; ++i) {
    a.update(0.1f, nullptr);
    if (i == 10)
      a.setGlobalTag("color", String("red"));                       // master tag setter: renderVersion bump
    if (i == 15)
      a.setState("motion", "run");                                  // state change: generation bump + re-partition
    if (i == 20)
      a.translateTransformationGroup("fixed", Vec2F(0.5f, 0.25f));  // static-part group setter: renderVersion bump

    auto cached = a.drawablesWithZLevel(Vec2F(1.0f, 2.0f));          // cache path (build or serve)
    auto rebuilt = a.drawablesWithZLevelRebuild(Vec2F(1.0f, 2.0f));  // forced full rebuild (test hook)
    ASSERT_FALSE(cached.empty());
    expectParity(cached, rebuilt, i);

    auto cachedAgain = a.drawablesWithZLevel(Vec2F(1.0f, 2.0f));     // pure cache hit
    expectParity(cachedAgain, rebuilt, i);
  }
  config->set("renderDrawableCache", false);
}

// A version>0 animator whose active state currently names a transformation group
// animates that group every tick (the transforms seed from the group's current
// animation transform, so non-reset entries accumulate continuously): LIVE.
TEST(NetworkedAnimator, StaticLivePartitionStateAnimatedGroup) {
  char const* cfg = R"JSON({
    "version": 1,
    "animatedParts": {
      "stateTypes": { "motion": { "default": "spin", "states": {
        "spin": { "frames": 1, "properties": { "fixed": [ ["rotate", 0.1] ] } } } } },
      "parts": {
        "body":  { "properties": { "image": "/b.png" } },
        "frame": { "properties": { "image": "/fr.png", "transformationGroups": ["fixed"] } }
      }
    },
    "transformationGroups": { "fixed": {} },
    "rotationGroups": {}, "effects": {}, "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  EXPECT_FALSE(a.partIsStaticCacheable("frame"));  // group currently named by an active-state property
  EXPECT_TRUE(a.partIsStaticCacheable("body"));
}
