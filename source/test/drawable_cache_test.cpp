#include "StarNetworkedAnimator.hpp"
#include "StarAnimatedPartSet.hpp"
#include "StarGameTypes.hpp"
#include "StarJson.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"
#include "StarFormat.hpp"
#include "StarTelemetry.hpp"
#include "gtest/gtest.h"

#include <cstdint>
#include <cstring>
#include <limits>

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
  // group), a static part on an angularVelocity==0 rotation group (vane --
  // STATIC purely via the partition's rotation carve-out, with a non-origin
  // rotationCenter so its cached zero-translate matrix carries a translation
  // component), one LIVE part with a "transforms" property (fan), and one part
  // anchored to it (mount -> LIVE transitively).  body is state-animated with
  // a multi-frame "run" state (<frame> image tag) so frame advances exercise
  // generation()-keyed invalidation; mount's image carries a <color> tag for
  // the mid-loop setGlobalTag flip.  held is imageless and STATIC (offset +
  // the non-interpolated "fixed" group): its drawables come exclusively from
  // setPartDrawables (the engine's Humanoid held-item / Lua
  // animator.setPartDrawables path), entering the build with a NON-ZERO base
  // position -- see the position parity policy at expectDrawableEq.  Every
  // part is non-centered: these image paths are fake, and both
  // Drawable::makeImage(centered=true) and addDirectives with
  // keepImageCenterPosition query the image metadata database (a real asset
  // load).
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
                   "anchorPart": "fan", "offset": [0.5, 0.25] } },
        "vane":  { "properties": { "zLevel": 4, "image": "/vane.png", "centered": false,
                   "rotationGroup": "wind" } },
        "held":  { "properties": { "zLevel": 5,
                   "offset": [0.05, 0.1], "transformationGroups": ["fixed"] } }
      }
    },
    "transformationGroups": { "fixed": {} },
    "rotationGroups": { "wind": { "angularVelocity": 0.0, "rotationCenter": [0.5, 0.5] } },
    "effects": {}, "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";

  NetworkedAnimator makeRichAnim() { return NetworkedAnimator(Json::parse(kRichCfg), "/"); }

  // Position parity policy (ulp tolerance).  The cache path builds drawables
  // at ZERO translate and applies the world translate afterwards
  // (Drawable::translate: position += w), while the rebuild folds the world
  // translate into the part matrix BEFORE Drawable::transform (position =
  // M'.transformVec2(S) with M'.translation == m + w).  For drawables whose
  // base position S is zero -- the plain image path, Drawable::makeImage(...,
  // Vec2F()) -- both orders reduce to the same single rounding and are bitwise
  // identical.  For m_partDrawables with NON-ZERO S (Humanoid held items, Lua
  // animator.setPartDrawables) float addition is non-associative:
  // fl(fl(R*S + m) + w) vs fl(R*S + fl(m + w)) differ by ~1 ulp for a large
  // fraction of inputs (empirically ~28% of random triples under this build's
  // -O3 -ffast-math once the StarDrawable.cpp TU boundary blocks
  // reassociation).  This is invisible in rendering, so position is compared
  // with a tight ulp bound; every other field (image+directives, the image
  // matrix -- whose translation column cancels exactly in both orders --
  // color, fullbright, zLevel) must still be EXACT.  Task 5's live
  // shadowCompare must adopt this same policy or it will be permanently noisy
  // on any entity using setPartDrawables.
  int64_t orderedFloatBits(float f) {
    int32_t i;
    std::memcpy(&i, &f, sizeof(i));
    // Map the IEEE-754 sign-magnitude bit pattern to a monotonically ordered
    // integer so adjacent floats differ by exactly 1.
    return i >= 0 ? int64_t(i) : int64_t(std::numeric_limits<int32_t>::min()) - i;
  }

  int64_t ulpDistance(float a, float b) {
    if (a == b)
      return 0;  // also covers +0.0 == -0.0
    int64_t d = orderedFloatBits(a) - orderedFloatBits(b);
    return d < 0 ? -d : d;
  }

  // 4 ulps: the observed divergence is 1 ulp per component (verified red with
  // a 0-ulp bound: held's position diverges in both components from i==0 on),
  // with headroom for FMA contraction differences; real cache bugs (stale
  // offset, missed invalidation, wrong translate) are orders of magnitude
  // larger.  This duplicates the engine's shadow-compare policy
  // (ShadowComparePositionMaxUlps); the duplication is guarded by
  // CachedEqualsRebuiltAcrossUpdates running with shadow-compare ON and
  // asserting zero shadowMismatch, cross-checking the two policies.
  int64_t const kPositionMaxUlps = 4;

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
    for (size_t c = 0; c < 2; ++c) {
      EXPECT_LE(ulpDistance(cached.position[c], rebuilt.position[c]), kPositionMaxUlps)
          << where.utf8Ptr() << " position[" << c << "]: "
          << strf("{:.9g} vs {:.9g}", cached.position[c], rebuilt.position[c]);
    }
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
// correctness without a display.  Mid-loop master flips (rotateGroup /
// setGlobalTag / translateTransformationGroup / setPartDrawables ->
// renderVersion bump; setState -> generation bump + re-partition) force
// invalidation and rebuild mid-test, and the second cache-path call per
// iteration is a guaranteed pure cache HIT (no state change since the first
// call).
TEST(DrawableCache, CachedEqualsRebuiltAcrossUpdates) {
  auto a = makeRichAnim();
  // vane is STATIC purely via the angularVelocity==0 rotation carve-out; if
  // the partition ever demotes it to LIVE, the rotateGroup flip below would
  // silently stop exercising the cached-rotation path.
  ASSERT_TRUE(a.partIsStaticCacheable("vane"));
  // frame's processingDirectives carries a <tint> tag: version-1 directives
  // are tag-substituted (and the engine's maybeLookupTagsView path requires at
  // least one tag in the string), so resolve it to a real color up front.
  a.setGlobalTag("tint", String("ff0000"));
  // The m_partDrawables path: a drawable with NON-ZERO base position on the
  // STATIC, imageless held part.  This is the one build path where the
  // cache's translate-after order is allowed to differ from the rebuild's
  // translate-folded order by ~1 ulp of position (see the policy at
  // expectDrawableEq); position 0.05 against held's 0.05 offset and the
  // (1, 2) world translate is probe-verified to actually produce that 1-ulp
  // divergence under this build's flags, both before and after the i==20
  // "fixed" group translate.  Non-centered: the image path is fake and
  // centered makeImage hits the image metadata database.
  a.setPartDrawables("held",
      {Drawable::makeImage("/held.png", 1.0f / TilePixels, false, Vec2F(0.05f, 0.05f))});
  // held is STATIC purely via offset + non-interpolated group: if the
  // partition ever demotes it to LIVE, this test would silently stop covering
  // CACHED part-drawables.
  ASSERT_TRUE(a.partIsStaticCacheable("held"));
  auto config = Root::singleton().configuration();
  // Shadow-compare ON for the whole loop: every drawablesWithZLevel call below
  // also runs the ENGINE's cached-vs-rebuilt comparison
  // (NetworkedAnimator::shadowCompare).  held's 1-ulp position divergence is
  // real here, so the zero-shadowMismatch assertion at the end pins the
  // engine's ulp policy (ShadowComparePositionMaxUlps) to the very divergence
  // this test's own kPositionMaxUlps policy tolerates -- if the engine policy
  // ever drifts from the test policy, shadowMismatch fires and this test reds.
  Telemetry::reset();
  config->set("renderDrawableCache", true);
  config->set("renderDrawableCacheShadowCompare", true);
  for (int i = 0; i < 30; ++i) {
    a.update(0.1f, nullptr);
    if (i == 5) {
      // Non-immediate rotation of a STATIC part's 0-angularVelocity group.
      // rotateGroup bumps renderVersion NOW (targetAngle), but the
      // drawable-visible currentAngle only snaps in the NEXT iteration's
      // update() -- whose value-diffed renderVersion bump is the ONLY
      // invalidation for the snap.  This must happen while body is still in
      // the 1-frame "idle" state: generation() is stable here, so a missing
      // snap bump means i==6 serves the stale angle-0 vane from the cache
      // (after i==15 the 4-frame "run" state bumps generation() every
      // iteration and would mask exactly that staleness).  From i==6 on, the
      // cached vane also pins zero-translate caching of a ROTATED static
      // part (rotation about a non-origin center) against the rebuild.
      a.rotateGroup("wind", 0.8f);
    }
    if (i == 10)
      a.setGlobalTag("color", String("red"));                       // master tag setter: renderVersion bump
    if (i == 15)
      a.setState("motion", "run");                                  // state change: generation bump + re-partition
    if (i == 20)
      a.translateTransformationGroup("fixed", Vec2F(0.5f, 0.25f));  // static-part group setter: renderVersion bump
    if (i == 25) {
      // Replacing a cached static part's drawables mid-run: setPartDrawables
      // bumps renderVersion AND drops m_staticCacheValid, so the stale cached
      // "held" entry (single drawable, old position) must not survive.
      a.setPartDrawables("held",
          {Drawable::makeImage("/held.png", 1.0f / TilePixels, false, Vec2F(0.05f, 0.05f)),
           Drawable::makeImage("/held2.png", 1.0f / TilePixels, false, Vec2F(0.65f, 0.35f))});
    }

    auto cached = a.drawablesWithZLevel(Vec2F(1.0f, 2.0f));          // cache path (build or serve)
    auto rebuilt = a.drawablesWithZLevelRebuild(Vec2F(1.0f, 2.0f));  // forced full rebuild (test hook)
    ASSERT_FALSE(cached.empty());
    expectParity(cached, rebuilt, i);

    auto cachedAgain = a.drawablesWithZLevel(Vec2F(1.0f, 2.0f));     // pure cache hit
    expectParity(cachedAgain, rebuilt, i);
  }
  // The engine's shadow-compare must agree with this test's parity policy on
  // every call above (including the genuinely 1-ulp-divergent held part).
  EXPECT_EQ(Telemetry::counter("render.drawable.cache.shadowMismatch").value(), 0u);
  config->set("renderDrawableCacheShadowCompare", false);
  config->set("renderDrawableCache", false);
}

// Telemetry: the cache path counts static parts served from the cache
// ("render.drawable.parts.cached") vs parts built ("render.drawable.parts.rebuilt"
// -- static-cache builds plus per-call LIVE builds).  The single "body" part is
// static (non-centered: the image path is fake and centered makeImage hits the
// image metadata database), so the first drawablesWithZLevel builds the cache
// (rebuilt++) and serves it (cached++); the second call, with no state change,
// is a pure cache hit (cached++ only).  Shadow-compare is enabled for both
// calls: matching paths must record ZERO render.drawable.cache.shadowMismatch
// (and no warn -- the harness's strict ErrorLogSink would not catch warns, but
// a mismatch here would fail the counter check regardless).
TEST(DrawableCache, CountsCachedVsRebuilt) {
  char const* cfg = R"JSON({
    "globalTagDefaults": {},
    "animatedParts": { "stateTypes": {}, "parts": {
      "body": { "properties": { "zLevel": 0, "image": "/a.png", "centered": false } } } },
    "transformationGroups": {}, "rotationGroups": {}, "particleEmitters": {},
    "lights": {}, "sounds": {}, "effects": {}
  })JSON";
  Telemetry::reset();
  auto config = Root::singleton().configuration();
  config->set("renderDrawableCache", true);
  config->set("renderDrawableCacheShadowCompare", true);
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  a.update(0.1f, nullptr);
  (void)a.drawablesWithZLevel({});  // first: builds static cache (rebuilt++), serves it (cached++)
  a.update(0.1f, nullptr);
  (void)a.drawablesWithZLevel({});  // unchanged static parts -> served cached
  EXPECT_GT(Telemetry::counter("render.drawable.parts.cached").value(), 0u);
  EXPECT_GT(Telemetry::counter("render.drawable.parts.rebuilt").value(), 0u);
  EXPECT_EQ(Telemetry::counter("render.drawable.cache.shadowMismatch").value(), 0u);
  config->set("renderDrawableCacheShadowCompare", false);
  config->set("renderDrawableCache", false);
}

// Re-key reason split: when the cache key mismatches, the cache path must
// attribute WHICH component moved ("render.drawable.cache.rekey.version" /
// "render.drawable.cache.rekey.generation"; a cold cache counts toward both),
// and rebuildStaticCache must count its static-part builds separately as
// "render.drawable.parts.rebuilt.rekey" alongside the existing plain rebuilt
// counter -- so rebuilt.rekey isolates re-key churn and rebuilt - rebuilt.rekey
// is the genuinely-live build load.  A pure cache hit moves no rekey counter.
TEST(DrawableCache, RekeyReasonCounters) {
  Telemetry::reset();
  auto config = Root::singleton().configuration();
  config->set("renderDrawableCache", true);
  auto a = makeRichAnim();
  // frame's processingDirectives carries a <tint> tag: resolve it up front,
  // exactly as CachedEqualsRebuiltAcrossUpdates does.
  a.setGlobalTag("tint", String("ff0000"));
  a.update(0.1f, nullptr);
  (void)a.drawablesWithZLevel(Vec2F());  // prime: first build is a cold-cache rekey too
  uint64_t rekeyV0 = Telemetry::counter("render.drawable.cache.rekey.version").value();
  uint64_t rekeyG0 = Telemetry::counter("render.drawable.cache.rekey.generation").value();
  uint64_t rebuiltRekey0 = Telemetry::counter("render.drawable.parts.rebuilt.rekey").value();
  a.setGlobalTag("k", String("v1"));     // version bump -> next call re-keys for 'version'
  (void)a.drawablesWithZLevel(Vec2F());
  EXPECT_GT(Telemetry::counter("render.drawable.cache.rekey.version").value(), rekeyV0);
  // generation() is stable across these two calls (no update(); setGlobalTag
  // never touches the AnimatedPartSet), so the re-key must be attributed to
  // 'version' ONLY.  An implementation that bumps both reasons on every
  // mismatch is exactly the mis-signal that would corrupt the version-churn
  // vs animation-frame-churn diagnosis this split exists for.
  EXPECT_EQ(Telemetry::counter("render.drawable.cache.rekey.generation").value(), rekeyG0);
  // The warm version re-key itself rebuilt static parts, so rebuilt.rekey must
  // have grown SINCE the prime (not merely be non-zero from the cold build).
  EXPECT_GT(Telemetry::counter("render.drawable.parts.rebuilt.rekey").value(), rebuiltRekey0);
  // A pure cache-hit call must not move any rekey counter (no state change
  // since the previous call: renderVersion and generation() are both stable --
  // the parity test's per-iteration cachedAgain hit relies on the same).
  uint64_t rv = Telemetry::counter("render.drawable.cache.rekey.version").value();
  uint64_t rg = Telemetry::counter("render.drawable.cache.rekey.generation").value();
  uint64_t rk = Telemetry::counter("render.drawable.parts.rebuilt.rekey").value();
  uint64_t rb = Telemetry::counter("render.drawable.parts.rebuilt").value();
  (void)a.drawablesWithZLevel(Vec2F());
  EXPECT_EQ(Telemetry::counter("render.drawable.cache.rekey.version").value(), rv);
  EXPECT_EQ(Telemetry::counter("render.drawable.cache.rekey.generation").value(), rg);
  // rebuilt.rekey counts ONLY rebuildStaticCache's static-part builds: the
  // pure hit still builds the rich config's LIVE parts (fan, mount) through
  // the live-part site, so plain rebuilt grows while rebuilt.rekey stays flat
  // -- pinning the two counters' sites apart (rebuilt - rebuilt.rekey =
  // genuinely-live builds).
  EXPECT_GT(Telemetry::counter("render.drawable.parts.rebuilt").value(), rb);
  EXPECT_EQ(Telemetry::counter("render.drawable.parts.rebuilt.rekey").value(), rk);
  config->set("renderDrawableCache", false);
}

// Cross-state-type animation tags: generation() is LAZY.  A state type that NO
// part lists in partStates is freshened only by update()'s forEachActiveState
// or by drawableBuildContext -- NOT by the cache path's part enumeration
// (freshenActivePart only touches state types with matching partStates).  A
// master that mutates such a state type (setState/finishAnimations) between
// update() and drawablesWithZLevel would otherwise leave generation() -- and
// with it the cache key -- unchanged, and a fully-static animator whose cached
// part images resolve the state type's <T_state>/<T_frame> tags would serve
// ONE stale call (self-healing on the next update()).  The cache path must
// settle every state type before keying.
TEST(DrawableCache, CrossStateTypeTagSettledWithoutUpdate) {
  // "aux" has NO matching partStates on any part; lamp is STATIC and its image
  // resolves through the <aux_state> animation tag (version 1:
  // drawableBuildContext injects <stateType>_state).  Non-centered: the image
  // path is fake and centered makeImage hits the image metadata database.
  char const* cfg = R"JSON({
    "version": 1,
    "animatedParts": {
      "stateTypes": { "aux": { "default": "off", "states": {
        "off": { "frames": 1 },
        "on":  { "frames": 1 }
      } } },
      "parts": {
        "lamp": { "properties": { "zLevel": 0, "image": "/lamp_<aux_state>.png", "centered": false } }
      }
    },
    "transformationGroups": {}, "rotationGroups": {}, "effects": {},
    "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  ASSERT_TRUE(a.partIsStaticCacheable("lamp"));
  auto config = Root::singleton().configuration();
  config->set("renderDrawableCache", true);

  a.update(0.1f, nullptr);
  auto primed = a.drawablesWithZLevel({});  // prime: cache holds /lamp_off.png
  ASSERT_FALSE(primed.empty());

  // Master-side state flip with NO intervening update(): exactly the
  // update() -> mutate -> render ordering an entity can produce.  Cache path
  // FIRST -- calling the rebuild first would freshen "aux" through
  // drawableBuildContext, bump generation() and mask the stale key.
  ASSERT_TRUE(a.setState("aux", "on"));
  auto cached = a.drawablesWithZLevel({});
  auto rebuilt = a.drawablesWithZLevelRebuild({});
  ASSERT_FALSE(cached.empty());
  expectParity(cached, rebuilt, 0);  // stale serve: cached still /lamp_off.png

  config->set("renderDrawableCache", false);
}

// Value-diff no-op guards in the per-frame master setters: a call that leaves
// observable animator state identical (same tag value, clearing an absent tag,
// same part-drawables list, same rotation target) must NOT bump renderVersion
// -- Humanoid::render issues exactly such calls every frame on visually
// stationary entities, and each spurious bump re-keys the static cache.  Real
// changes must keep bumping.
TEST(NetworkedAnimator, NoOpSettersDoNotBumpRenderVersion) {
  // makeAnim's config has no rotation groups; mirror it with one added
  // ("rot", angularVelocity 0 like StaticLivePartition's "wind").
  char const* cfg = R"JSON({
    "globalTagDefaults": {},
    "animatedParts": { "stateTypes": {}, "parts": {
      "body": { "properties": { "zLevel": 0, "image": "/a.png" } } } },
    "transformationGroups": {}, "rotationGroups": { "rot": { "angularVelocity": 0.0 } },
    "particleEmitters": {}, "lights": {}, "sounds": {}, "effects": {}
  })JSON";
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  a.setGlobalTag("x", String("y"));
  a.setPartTag("body", "pt", String("pv"));
  a.setPartDrawables("body", {});  // establish empty
  a.rotateGroup("rot", 0.5f);
  uint64_t v = a.renderVersion();
  a.setGlobalTag("x", String("y"));        // same value -> no bump
  a.setPartTag("body", "pt", String("pv"));  // same value -> no bump
  a.setLocalTag("l");                      // clearing an absent local tag -> no bump
  a.removeGlobalTag("absent");             // removing an absent global tag -> no bump
  a.setPartDrawables("body", {});          // same (empty) drawables -> no bump
  a.addPartDrawables("body", {});          // appending nothing -> no bump
  a.rotateGroup("rot", 0.5f);              // same target angle -> no bump
  EXPECT_EQ(a.renderVersion(), v);
  // and real changes still bump:
  a.setGlobalTag("x", String("z"));
  EXPECT_GT(a.renderVersion(), v);
  uint64_t v2 = a.renderVersion();
  a.setPartDrawables("body", {Drawable::makeLine(Line2F({0, 0}, {1, 1}), 1.0f, Color::White)});
  EXPECT_GT(a.renderVersion(), v2);
  // The per-field comparison must be pinned on NON-EMPTY lists too (the cases
  // above only ever reach the size check, never drawableEquals' field diffs):
  uint64_t v3 = a.renderVersion();
  a.setPartDrawables("body", {Drawable::makeLine(Line2F({0, 0}, {1, 1}), 1.0f, Color::White)});  // identical non-empty list -> no bump
  EXPECT_EQ(a.renderVersion(), v3);
  a.setPartDrawables(  // same size, only position changed -> must bump (stale-cache hazard otherwise)
      "body", {Drawable::makeLine(Line2F({0, 0}, {1, 1}), 1.0f, Color::White, Vec2F(0, 2))});
  EXPECT_GT(a.renderVersion(), v3);
  // Image branch: centered=false avoids any Root/asset access.
  a.setPartDrawables("body", {Drawable::makeImage("/a.png", 1.0f, false, Vec2F())});
  uint64_t v4 = a.renderVersion();
  EXPECT_GT(v4, v3);  // line -> image swap at equal size is a change
  a.setPartDrawables("body", {Drawable::makeImage("/a.png", 1.0f, false, Vec2F())});  // identical image drawable -> no bump
  EXPECT_EQ(a.renderVersion(), v4);
  a.setPartDrawables("body", {Drawable::makeImage("/b.png", 1.0f, false, Vec2F())});  // same size, image path changed -> bump
  EXPECT_GT(a.renderVersion(), v4);
}

// Humanoid's per-frame pattern: reset + rotate to the SAME angle must not
// re-key the cache; rotating to a DIFFERENT angle must.  Local transformation
// matrices are excluded from renderVersion (reset->identity->rotate-back is
// two real value changes netting to zero, so per-call value-diffs cannot
// help); instead the combined localTransform matrix state keys the cache via
// "render.drawable.cache.rekey.localtransform".  Same literal in = same bits
// out: Mat3F::rotation(0.7f, ...) * identity is deterministic same-TU float
// math, so the steady-state matrix is bitwise stable across iterations.
TEST(DrawableCache, StationaryLocalTransformPatternDoesNotRekey) {
  Telemetry::reset();
  Root::singleton().configuration()->set("renderDrawableCache", true);
  // The rich config's "fixed" group is non-interpolated and referenced by the
  // STATIC parts frame and held; body's default "motion" state is the 1-frame
  // "idle", so generation() is stable across the no-update() calls below (the
  // pure-hit assertions in RekeyReasonCounters rely on the same).
  auto a = makeRichAnim();
  // frame's processingDirectives carries a <tint> tag: resolve it up front,
  // exactly as CachedEqualsRebuiltAcrossUpdates does.
  a.setGlobalTag("tint", String("ff0000"));
  a.update(0.1f, nullptr);
  (void)a.drawablesWithZLevel(Vec2F());           // prime
  auto rekeys = [] {
    return Telemetry::counter("render.drawable.cache.rekey.version").value()
         + Telemetry::counter("render.drawable.cache.rekey.generation").value()
         + Telemetry::counter("render.drawable.cache.rekey.localtransform").value();
  };
  // settle one frame of the pattern so the local matrix reaches its steady value:
  a.resetLocalTransformationGroup("fixed");
  a.rotateLocalTransformationGroup("fixed", 0.7f, Vec2F(1, 2));
  (void)a.drawablesWithZLevel(Vec2F());
  uint64_t r0 = rekeys();
  for (int i = 0; i < 3; ++i) {                   // stationary frames: reset + same rotate
    a.resetLocalTransformationGroup("fixed");
    a.rotateLocalTransformationGroup("fixed", 0.7f, Vec2F(1, 2));
    (void)a.drawablesWithZLevel(Vec2F());
  }
  EXPECT_EQ(rekeys(), r0);                        // no re-keys while visually stationary
  a.resetLocalTransformationGroup("fixed");
  a.rotateLocalTransformationGroup("fixed", 0.9f, Vec2F(1, 2));  // real change
  (void)a.drawablesWithZLevel(Vec2F());
  EXPECT_GT(rekeys(), r0);                        // localtransform re-key fired
  Root::singleton().configuration()->set("renderDrawableCache", false);
}

// Partition memoization: the structural static/live walk scans each part's
// FULL config (every state's properties), and rebuildStaticCache re-runs it
// for every part on EVERY re-key -- including pure renderVersion re-keys
// (master tag flips), where no partition input changed.  The structural
// verdict must be memoized per part ("render.drawable.partition.scans" counts
// the structural walks, i.e. memo misses): a version-only re-key must serve
// every verdict from the memo and add ZERO new scans.  The flash-effect gate
// is a runtime input and stays OUTSIDE the memo (StaticLivePartitionTriggers
// pins that it remains live).
TEST(DrawableCache, PartitionScansAreMemoized) {
  Telemetry::reset();
  Root::singleton().configuration()->set("renderDrawableCache", true);
  auto a = makeRichAnim();
  // frame's processingDirectives carries a <tint> tag: resolve it up front,
  // exactly as CachedEqualsRebuiltAcrossUpdates does.
  a.setGlobalTag("tint", String("ff0000"));
  a.update(0.1f, nullptr);
  (void)a.drawablesWithZLevel(Vec2F());           // cold rebuild: scans > 0
  uint64_t s0 = Telemetry::counter("render.drawable.partition.scans").value();
  EXPECT_GT(s0, 0u);
  a.setGlobalTag("k", String("v1"));              // force a version re-key -> rebuildStaticCache runs again
  (void)a.drawablesWithZLevel(Vec2F());
  EXPECT_EQ(Telemetry::counter("render.drawable.partition.scans").value(), s0);  // memo served, no new scans
  Root::singleton().configuration()->set("renderDrawableCache", false);
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

// Per-part generation: advancing a multi-frame state on ONE part must bump that
// part's partGeneration but leave an unrelated static part's stamp untouched --
// this is what lets the per-part drawable cache invalidate one part without its
// siblings.  generation() is LAZY (bumped inside freshenActivePart), so freshen
// every part (forEachActivePart) before reading, exactly as the drawable path does.
TEST(AnimatedPartSet, PartGenerationIsPerPart) {
  char const* cfg = R"JSON({
    "stateTypes": { "motion": { "default": "run", "states": {
      "run": { "frames": 4, "cycle": 0.4, "mode": "loop" } } } },
    "parts": {
      "bg":   { "properties": { "image": "/bg.png" } },
      "body": { "partStates": { "motion": { "run": { "properties": { "image": "/body_<frame>.png" } } } } }
    }
  })JSON";
  AnimatedPartSet a(Json::parse(cfg), 1);
  a.setActiveState("motion", "run");
  auto freshen = [&] {
    a.forEachActivePart([](String const&, AnimatedPartSet::ActivePartInformation const&) {});
  };
  freshen();                                  // initial resolve (bumps both parts once)
  uint64_t bgG0 = a.partGeneration("bg");
  uint64_t bodyG0 = a.partGeneration("body");
  bool bodyBumped = false;
  for (int i = 0; i < 8; ++i) {
    a.update(0.1f);                           // advances "run" by ~1 frame each tick
    freshen();
    if (a.partGeneration("body") != bodyG0)
      bodyBumped = true;
  }
  EXPECT_TRUE(bodyBumped) << "body's animation frame advanced; its partGeneration must bump";
  EXPECT_EQ(a.partGeneration("bg"), bgG0) << "bg never changed; its partGeneration must stay fixed";
  EXPECT_EQ(a.partGeneration("nonexistent"), 0u) << "unknown part name returns 0";
}

// Per-part parity: with the PER-PART cache on, drawables must EQUAL the full
// rebuild across the same update/flip sequence the whole-entity parity test
// uses, with shadow-compare ON (zero mismatches).  The final cached-counter
// assertion guarantees the per-part path actually served from cache (not the
// rebuild fallback), so this is a genuine red before the path exists.
TEST(DrawableCache, PerPartCachedEqualsRebuiltAcrossUpdates) {
  auto a = makeRichAnim();
  ASSERT_TRUE(a.partIsStaticCacheable("vane"));
  a.setGlobalTag("tint", String("ff0000"));
  a.setPartDrawables("held",
      {Drawable::makeImage("/held.png", 1.0f / TilePixels, false, Vec2F(0.05f, 0.05f))});
  ASSERT_TRUE(a.partIsStaticCacheable("held"));
  auto config = Root::singleton().configuration();
  Telemetry::reset();
  config->set("renderDrawableCachePerPart", true);
  config->set("renderDrawableCacheShadowCompare", true);
  for (int i = 0; i < 30; ++i) {
    a.update(0.1f, nullptr);
    if (i == 5)
      a.rotateGroup("wind", 0.8f);
    if (i == 10)
      a.setGlobalTag("color", String("red"));
    if (i == 15)
      a.setState("motion", "run");
    if (i == 20)
      a.translateTransformationGroup("fixed", Vec2F(0.5f, 0.25f));
    if (i == 25)
      a.setPartDrawables("held",
          {Drawable::makeImage("/held.png", 1.0f / TilePixels, false, Vec2F(0.05f, 0.05f)),
           Drawable::makeImage("/held2.png", 1.0f / TilePixels, false, Vec2F(0.65f, 0.35f))});

    auto cached = a.drawablesWithZLevel(Vec2F(1.0f, 2.0f));          // per-part path
    auto rebuilt = a.drawablesWithZLevelRebuild(Vec2F(1.0f, 2.0f));  // forced full rebuild
    ASSERT_FALSE(cached.empty());
    expectParity(cached, rebuilt, i);

    auto cachedAgain = a.drawablesWithZLevel(Vec2F(1.0f, 2.0f));     // pure cache hit
    expectParity(cachedAgain, rebuilt, i);
  }
  EXPECT_EQ(Telemetry::counter("render.drawable.cache.shadowMismatch").value(), 0u);
  EXPECT_GT(Telemetry::counter("render.drawable.parts.cached").value(), 0u)
      << "per-part path must actually serve from cache (not fall back to rebuild)";
  config->set("renderDrawableCacheShadowCompare", false);
  config->set("renderDrawableCachePerPart", false);
}

// The win: when ONE part loops (body's 4-frame "run" state advances generation()
// every tick), the whole-entity cache rebuilds ALL static parts each frame, while
// the per-part cache rebuilds only body and serves bg/frame/vane/held from cache.
// Compare re-key churn (rebuilt.rekey counts static-part rebuilds): per-part must
// be strictly, substantially smaller.
TEST(DrawableCache, PerPartRebuildsOnlyChangedParts) {
  auto config = Root::singleton().configuration();
  auto runSequence = [&](bool perPart) -> uint64_t {
    Telemetry::reset();
    config->set("renderDrawableCachePerPart", perPart);
    config->set("renderDrawableCache", !perPart);
    auto a = makeRichAnim();
    a.setGlobalTag("tint", String("ff0000"));
    a.setState("motion", "run");              // 4-frame loop on the "body" part only
    a.update(0.1f, nullptr);
    (void)a.drawablesWithZLevel(Vec2F());     // prime: build all static parts
    uint64_t r0 = Telemetry::counter("render.drawable.parts.rebuilt.rekey").value();
    uint64_t c0 = Telemetry::counter("render.drawable.parts.cached").value();
    for (int i = 0; i < 8; ++i) {             // 8 frame advances (cycle 0.4 / 4 frames)
      a.update(0.1f, nullptr);
      (void)a.drawablesWithZLevel(Vec2F());
    }
    uint64_t churn = Telemetry::counter("render.drawable.parts.rebuilt.rekey").value() - r0;
    if (perPart) {
      // Static siblings (bg/frame/vane/held) keep getting served while body loops.
      EXPECT_GT(Telemetry::counter("render.drawable.parts.cached").value(), c0)
          << "per-part: static siblings must be served from cache while body animates";
    }
    config->set("renderDrawableCachePerPart", false);
    config->set("renderDrawableCache", false);
    return churn;
  };
  uint64_t wholeEntityChurn = runSequence(false);   // renderDrawableCache only
  uint64_t perPartChurn = runSequence(true);        // renderDrawableCachePerPart only
  // body bumps generation() every frame advance.  Whole-entity rebuilds every
  // static part each time (>=4 static siblings + body); per-part rebuilds body only.
  EXPECT_LT(perPartChurn, wholeEntityChurn)
      << "per-part churn=" << perPartChurn << " whole-entity churn=" << wholeEntityChurn;
  EXPECT_GT(wholeEntityChurn, perPartChurn * 2)
      << "kRichCfg has 5 static parts; whole-entity should rebuild far more per frame";
}

// KNOWN LIMITATION of the per-part cache (renderDrawableCachePerPart): a static
// part whose image resolves ANOTHER state type's animation tag (<aux_state>) is
// NOT invalidated when that state type changes -- partGeneration tracks only the
// part's own resolved key, and setState does not bump renderVersion.  The
// whole-entity path settles generation() into its key and stays correct
// (CrossStateTypeTagSettledWithoutUpdate); the per-part path serves STALE.  This
// test PINS that the gap exists AND that shadow-compare (the A/B safety net)
// detects it.  The flag is default-off; this gap must be fixed (per-part
// tag-dependency tracking) before renderDrawableCachePerPart can default-on (see
// plan 2026-06-14-drawable-cache-perpart.md Task 6 Step 4).  WHEN FIXED: the
// shadowMismatch below drops to 0 -> this test fails -> flip it to assert parity
// and proceed with the default-on decision.
TEST(DrawableCache, PerPartCrossStateTypeTagKnownStaleLimitation) {
  char const* cfg = R"JSON({
    "version": 1,
    "animatedParts": {
      "stateTypes": { "aux": { "default": "off", "states": {
        "off": { "frames": 1 },
        "on":  { "frames": 1 }
      } } },
      "parts": {
        "lamp": { "properties": { "zLevel": 0, "image": "/lamp_<aux_state>.png", "centered": false } }
      }
    },
    "transformationGroups": {}, "rotationGroups": {}, "effects": {},
    "particleEmitters": {}, "lights": {}, "sounds": {}
  })JSON";
  auto a = NetworkedAnimator(Json::parse(cfg), "/");
  ASSERT_TRUE(a.partIsStaticCacheable("lamp"));
  auto config = Root::singleton().configuration();
  Telemetry::reset();
  config->set("renderDrawableCachePerPart", true);
  config->set("renderDrawableCacheShadowCompare", true);

  a.update(0.1f, nullptr);
  auto primed = a.drawablesWithZLevel({});   // prime: per-part caches /lamp_off.png (parity, no mismatch)
  ASSERT_FALSE(primed.empty());
  ASSERT_EQ(Telemetry::counter("render.drawable.cache.shadowMismatch").value(), 0u)
      << "prime call must be parity-clean";

  // Master-side state flip with NO intervening update() (the update -> mutate ->
  // render ordering an entity produces).  setState does not bump renderVersion,
  // and lamp does not list "aux", so its partGeneration/key do not move -> the
  // per-part cache serves the stale /lamp_off.png.
  ASSERT_TRUE(a.setState("aux", "on"));
  (void)a.drawablesWithZLevel({});           // per-part serves STALE; shadow-compare flags it
  EXPECT_GT(Telemetry::counter("render.drawable.cache.shadowMismatch").value(), 0u)
      << "KNOWN per-part limitation: cross-state-type tag served stale; shadow-compare must catch it. "
         "When the gap is fixed this stays 0 -> flip this test to assert parity and gate default-on.";

  config->set("renderDrawableCacheShadowCompare", false);
  config->set("renderDrawableCachePerPart", false);
}
