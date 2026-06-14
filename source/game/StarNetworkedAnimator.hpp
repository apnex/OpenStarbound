#pragma once

#include "StarPeriodicFunction.hpp"
#include "StarSet.hpp"
#include "StarAnimatedPartSet.hpp"
#include "StarNetElementSystem.hpp"
#include "StarDrawable.hpp"
#include "StarParticle.hpp"
#include "StarLightSource.hpp"
#include "StarMixer.hpp"

namespace Star {

STAR_CLASS(NetworkedAnimator);
STAR_EXCEPTION(NetworkedAnimatorException, StarException);

// Wraps an AnimatedPartSet with a set of optional light sources and particle
// emitters to produce a network capable animation system.
class NetworkedAnimator : public NetElementSyncGroup {
public:
  // Target for dynamic render data such as sounds and particles that are not
  // persistent and are instead produced during a call to update, and may need
  // to be tracked over time.
  class DynamicTarget {
  public:
    // Calls stopAudio()
    ~DynamicTarget();

    List<AudioInstancePtr> pullNewAudios();
    List<Particle> pullNewParticles();

    // Stops all looping audio immediately and lets non-looping audio finish
    // normally
    void stopAudio();

    // Updates the base position of all un-pulled particles and all active
    // audio.  Not necessary to call, but if not called all pulled data will be
    // relative to (0, 0).
    void updatePosition(Vec2F const& position);

  private:
    friend class NetworkedAnimator;

    void clearFinishedAudio();

    struct PersistentSound {
      Json sound;
      AudioInstancePtr audio;
      float stopRampTime;
    };

    struct ImmediateSound {
      Json sound;
      AudioInstancePtr audio;
    };

    Vec2F position;
    List<AudioInstancePtr> pendingAudios;
    List<Particle> pendingParticles;
    StringMap<PersistentSound> statePersistentSounds;
    StringMap<ImmediateSound> stateImmediateSounds;
    StringMap<List<AudioInstancePtr>> independentSounds;
    HashMap<AudioInstancePtr, Vec2F> currentAudioBasePositions;
  };

  NetworkedAnimator();
  // If passed a string as config, NetworkedAnimator will interpret this as a
  // config path, otherwise it is interpreted as the literal config.
  NetworkedAnimator(Json config, String relativePath = String());

  NetworkedAnimator(NetworkedAnimator&& animator);
  NetworkedAnimator(NetworkedAnimator const& animator);

  NetworkedAnimator& operator=(NetworkedAnimator&& animator);
  NetworkedAnimator& operator=(NetworkedAnimator const& animator);

  StringList stateTypes() const;
  StringList states(String const& stateType) const;

  // Returns whether a state change occurred.  If startNew is true, always
  // forces a state change and starts the state off at the beginning even if
  // this state is already the current state.
  bool setState(String const& stateType, String const& state, bool startNew = false, bool reverse = false);
  bool setLocalState(String const& stateType, String const& state, bool startNew = false, bool reverse = false);
  String state(String const& stateType) const;
  int stateFrame(String const& stateType) const;
  int stateNextFrame(String const& stateType) const;
  float stateFrameProgress(String const& stateType) const;
  float stateTimer(String const& stateType) const;
  bool stateReverse(String const& stateType) const;

  float stateCycle(String const& stateType, Maybe<String> state) const;
  int stateFrames(String const& stateType, Maybe<String> state) const;

  bool hasState(String const& stateType, Maybe<String> const& state = {}) const;

  StringMap<AnimatedPartSet::Part> const& constParts() const;
  StringMap<AnimatedPartSet::Part>& parts();
  StringList partNames() const;

  // Queries, if it exists, a property value from the underlying
  // AnimatedPartSet for the given state or part.  If the property does not
  // exist, returns null.
  Json stateProperty(String const& stateType, String const& propertyName, Maybe<String> state = {}, Maybe<int> frame = {}) const;
  Json stateNextProperty(String const& stateType, String const& propertyName) const;
  Json partProperty(String const& partName, String const& propertyName, Maybe<String> stateType = {}, Maybe<String> state = {}, Maybe<int> frame = {}) const;
  Json partNextProperty(String const & partName, String const & propertyName) const;

  // Returns the transformation from flipping and zooming that is applied to
  // all parts in the NetworkedAnimator.
  Mat3F globalTransformation() const;
  // The transformation applied from the given set of transformation groups
  Mat3F groupTransformation(StringList const& transformationGroups) const;
  // The transformation that is applied to the given part NOT including the
  // global transformation
  Mat3F partTransformation(String const& partName) const;
  // Returns the total transformation for the given part, which includes the
  // globalTransformation, as well as the part rotation, scaling, and
  // translation.
  Mat3F finalPartTransformation(String const& partName) const;

  // partPoint / partPoly takes a propertyName and looks up the associated part
  // property and interprets is a Vec2F or a PolyF, then applies the final part
  // transformation and returns it.
  Maybe<Vec2F> partPoint(String const& partName, String const& propertyName) const;
  Maybe<PolyF> partPoly(String const& partName, String const& propertyName) const;

  // Every part image can have one or more <tag> directives in it, which if set
  // here will be replaced by the tag value when constructing Drawables.  All
  // Drawables can also have a <frame> tag which will be set to whatever the
  // current state frame is (1 indexed, so the first frame is 1).
  void setGlobalTag(String tagName, Maybe<String> tagValue = {});
  void removeGlobalTag(String const& tagName);
  String const* globalTagPtr(String const& tagName) const;
  void setPartTag(String const& partType, String tagName, Maybe<String> tagValue = {});
  void setLocalTag(String tagName, Maybe<String> tagValue = {});

  void setPartDrawables(String const& partName, List<Drawable> drawables);
  void addPartDrawables(String const& partName, List<Drawable> drawables);

  String applyPartTags(String const& partName, String apply) const;

  void setProcessingDirectives(Directives const& directives);
  void setZoom(float zoom);
  bool flipped() const;
  float flippedRelativeCenterLine() const;
  void setFlipped(bool flipped, float relativeCenterLine = 0.0f);

  // Animation rate defaults to 1.0, which means normal animation speed.  This
  // can be used to globally speed up or slow down all components of
  // NetworkedAnimator together.
  void setAnimationRate(float rate);
  float animationRate();

  // Given angle is an absolute angle.  Will rotate over time at the configured
  // angular velocity unless the immediate flag is set.
  bool hasRotationGroup(String const& rotationGroup) const;
  void rotateGroup(String const& rotationGroup, float targetAngle, bool immediate = false);
  float currentRotationAngle(String const& rotationGroup) const;

  // Transformation groups can be used for arbitrary part transforamtions.
  // They apply immediately, and are optionally interpolated on slaves.
  bool hasTransformationGroup(String const& transformationGroup) const;
  void translateTransformationGroup(String const& transformationGroup, Vec2F const& translation);
  void rotateTransformationGroup(String const& transformationGroup, float rotation, Vec2F const& rotationCenter = Vec2F());
  void scaleTransformationGroup(String const& transformationGroup, float scale, Vec2F const& scaleCenter = Vec2F());
  void scaleTransformationGroup(String const& transformationGroup, Vec2F const& scale, Vec2F const& scaleCenter = Vec2F());
  void transformTransformationGroup(String const& transformationGroup, float a, float b, float c, float d, float tx, float ty);
  void resetTransformationGroup(String const& transformationGroup);
  void setTransformationGroup(String const& transformationGroup, Mat3F transform);
  Mat3F getTransformationGroup(String const& transformationGroup);

  void translateLocalTransformationGroup(String const& transformationGroup, Vec2F const& translation);
  void rotateLocalTransformationGroup(String const& transformationGroup, float rotation, Vec2F const& rotationCenter = Vec2F());
  void scaleLocalTransformationGroup(String const& transformationGroup, float scale, Vec2F const& scaleCenter = Vec2F());
  void scaleLocalTransformationGroup(String const& transformationGroup, Vec2F const& scale, Vec2F const& scaleCenter = Vec2F());
  void transformLocalTransformationGroup(String const& transformationGroup, float a, float b, float c, float d, float tx, float ty);
  void resetLocalTransformationGroup(String const& transformationGroup);
  void setLocalTransformationGroup(String const& transformationGroup, Mat3F transform);
  Mat3F getLocalTransformationGroup(String const& transformationGroup);

  bool hasParticleEmitter(String const& emitterName) const;
  // Active particle emitters emit over time based on emission rate/variance.
  void setParticleEmitterActive(String const& emitterName, bool active);
  // Set the emission rate in particles / sec for a given emitter
  void setParticleEmitterEmissionRate(String const& emitterName, float emissionRate);
  // Set the optional particle emitter offset region, which particles will be
  // spread around randomly before being spawned
  void setParticleEmitterOffsetRegion(String const& emitterName, RectF const& offsetRegion);

  // Number of times to cycle when emitting a burst of particles.
  void setParticleEmitterBurstCount(String const& emitterName, unsigned burstCount);

  // Cause one time burst of all types of particles in an emitter looping around
  // burstCount times
  void burstParticleEmitter(String const& emitterName);

  bool hasLight(String const& lightName) const;
  void setLightActive(String const& lightName, bool active);
  void setLightPosition(String const& lightName, Vec2F position);
  void setLightColor(String const& lightName, Color color);
  void setLightPointAngle(String const& lightName, float angle);

  bool hasSound(String const& soundName) const;
  void setSoundPool(String const& soundName, StringList soundPool);
  // Plays a sound from the given independent sound pool.  Multiple sounds may
  // be played as part of this group, and playing a new one will not interrupt
  // an older one.
  void playSound(String const& soundName, int loops = 0);

  // Setting the sound position, volume, and speed will affect future sounds in
  // this group, as well as any still active sounds from this group.
  void setSoundPosition(String const& soundName, Vec2F const& position);

  void setSoundVolume(String const& soundName, float volume, float rampTime = 0.0f);
  void setSoundPitchMultiplier(String const& soundName, float pitchMultiplier, float rampTime = 0.0f);

  // Stop all sounds played from this sound group
  void stopAllSounds(String const& soundName, float rampTime = 0.0f);

  void setEffectEnabled(String const& effect, bool enabled);

  List<Drawable> drawables(Vec2F const& translate = Vec2F()) const;
  List<pair<Drawable, float>> drawablesWithZLevel(Vec2F const& translate = Vec2F()) const;
  // The always-rebuild drawables path (the pre-cache behaviour, kept verbatim).
  // drawablesWithZLevel delegates here when renderDrawableCache is off, and the
  // shadow-compare / parity tests use it as the golden master.
  List<pair<Drawable, float>> drawablesWithZLevelRebuild(Vec2F const& translate = Vec2F()) const;

  List<LightSource> lightSources(Vec2F const& translate = Vec2F()) const;

  // Dynamic target is optional, if not given, generated particles and sounds
  // will be discarded
  void update(float dt, DynamicTarget* dynamicTarget);

  // Run through the current animations until the final frame, including any
  // transition animations.
  void finishAnimations();
  uint8_t version() const;

  // Bumps when any drawable-affecting animator state changes (not
  // generation()'s job).  Main-thread only: not synchronized.
  uint64_t renderVersion() const;

  // Static/live partition (conservative + transitive): true iff this part's
  // drawable is fully determined by (renderVersion, generation), i.e. it and
  // its whole anchorPart chain reference no continuously-interpolated state.
  // Conservative: anything unintelligible is treated as live (not cacheable).
  // Re-evaluate whenever generation() bumps (a state change can re-target
  // which transformation group an active-state property animates).
  // Main-thread only, like renderVersion().
  bool partIsStaticCacheable(String const& partName) const;

private:
  struct RotationGroup {
    float angularVelocity;
    Vec2F rotationCenter;

    NetElementFloat targetAngle;
    float currentAngle;

    NetElementEvent netImmediateEvent;

    // Shadow of targetAngle.get() last seen by netElementsNeedLoad, for the
    // slave-side discrete-change diff (rotateGroup never runs on slaves, and
    // update() snaps/approaches currentAngle from the netted targetAngle).
    // targetAngle never has an interpolator, so the diff is tick-quiet.
    // Matches NetElementFloat's default value.
    float lastSeenTargetAngle = 0.0f;
  };

  struct TransformationGroup {
    Mat3F affineTransform() const;
    void setAffineTransform(Mat3F const& matrix);

    Mat3F localAffineTransform() const;
    void setLocalAffineTransform(Mat3F const& matrix);

    Mat3F animationAffineTransform() const;
    void setAnimationAffineTransform(Mat3F const& matrix);
    void setAnimationAffineTransform(Mat3F const& mat1, Mat3F const& mat2, float progress);

    bool interpolated;

    Mat3F localTransform;

    NetElementFloat xTranslation;
    NetElementFloat yTranslation;
    NetElementFloat xScale;
    NetElementFloat yScale;
    NetElementFloat xShear;
    NetElementFloat yShear;

    float xTranslationAnimation;
    float yTranslationAnimation;
    float xScaleAnimation;
    float yScaleAnimation;
    float xShearAnimation;
    float yShearAnimation;

    // Shadows of the six networked floats last seen by netElementsNeedLoad,
    // for the slave-side discrete-change diff (the *TransformationGroup
    // setters never run on slaves).  Only diffed for NON-interpolated groups:
    // those floats have no interpolators and change only at delta-apply, so
    // the diff is tick-quiet; interpolated groups lerp every tick, and parts
    // referencing them are LIVE in partIsStaticCacheable anyway.  Defaults
    // match the identity affine transform the constructor installs.
    float lastSeenXTranslation = 0.0f;
    float lastSeenYTranslation = 0.0f;
    float lastSeenXScale = 1.0f;
    float lastSeenYScale = 1.0f;
    float lastSeenXShear = 0.0f;
    float lastSeenYShear = 0.0f;
  };

  struct ParticleEmitter {
    struct ParticleConfig {
      ParticleVariantCreator creator;
      unsigned count;
      Vec2F offset;
      bool flip;
    };

    NetElementFloat emissionRate;
    float emissionRateVariance;
    NetElementData<RectF> offsetRegion;
    Maybe<String> anchorPart;
    StringList transformationGroups;
    Maybe<String> rotationGroup;
    Maybe<Vec2F> rotationCenter;

    List<ParticleConfig> particleList;

    NetElementBool active;
    NetElementUInt burstCount;
    NetElementUInt randomSelectCount;
    NetElementEvent burstEvent;

    float timer;
  };

  struct Light {
    NetElementBool active;
    NetElementFloat xPosition;
    NetElementFloat yPosition;
    NetElementData<Color> color;
    NetElementFloat pointAngle;
    Maybe<String> anchorPart;
    StringList transformationGroups;
    Maybe<String> rotationGroup;
    Maybe<Vec2F> rotationCenter;

    Maybe<PeriodicFunction<float>> flicker;
    bool pointLight;
    float pointBeam;
    float beamAmbience;
  };

  enum class SoundSignal {
    Play,
    StopAll
  };

  struct Sound {
    float rangeMultiplier;
    NetElementData<StringList> soundPool;
    NetElementFloat xPosition;
    NetElementFloat yPosition;
    NetElementFloat volumeTarget;
    NetElementFloat volumeRampTime;
    NetElementFloat pitchMultiplierTarget;
    NetElementFloat pitchMultiplierRampTime;
    NetElementInt loops;
    NetElementSignal<SoundSignal> signals;
  };

  struct Effect {
    String type;
    float time;
    Directives directives;

    NetElementBool enabled;
    // Shadow of enabled.get() last seen by netElementsNeedLoad, for the
    // slave-side discrete-change diff.
    bool lastSeenEnabled = false;
    float timer;
  };

  struct StateInfo {
    NetElementSize stateIndex;
    NetElementEvent startedEvent;
    bool wasUpdated;
    NetElementBool reverse;
  };

  void setupNetStates();

  void bumpRenderVersion();

  // Order-stable hash of every transformation group's localTransform matrix.
  // Local matrices are excluded from m_renderVersion (their setters are called
  // in reset+rotate pairs every frame by Humanoid; per-call bumps would re-key
  // a visually-stationary animator), so the combined matrix state keys the
  // static cache as the third cache-key component instead.  Hashes ALL groups
  // (no dirty bits); computed only on the cache path.  Iteration order only
  // ever compares against the SAME animator's previous value, so the
  // OrderedHashMap's stable per-instance order is sufficient.
  uint64_t localTransformHash() const;

  // Helpers for partIsStaticCacheable.  Each scans the part's full config
  // structure (base partProperties plus every partState's properties and
  // frameProperties), so the answer is conservative across state changes.
  bool anyFlashEffectActive() const;
  // The full structural walk behind partIsStaticCacheable (anchor chain,
  // rotation/transformation groups, transforms property), memoized per part in
  // m_partitionMemo.  The flash-effect gate is a runtime input and stays
  // OUTSIDE, in partIsStaticCacheable itself.
  bool partIsStaticCacheableStructural(String const& partName) const;
  bool partReferencesLiveRotationGroup(AnimatedPartSet::Part const& part) const;
  bool partReferencesLiveTransformationGroup(AnimatedPartSet::Part const& part) const;
  static bool partHasTransformsProperty(AnimatedPartSet::Part const& part);

  // Helpers shared by drawablesWithZLevelRebuild and the static-cache path.
  // The per-part build (appendPartDrawables) is extracted verbatim from the
  // old drawablesWithZLevel loop and is the SINGLE source of truth both paths
  // call, so cached and live parts are built identically (parity by
  // construction).

  // Tag-dependency capture for the per-part cache. Populated by drawableBuildContext
  // (per drawable call), read by appendPartDrawables. stateTagOwner maps a BUILT-IN
  // animation tag key (<T>_frame/_frameIndex/_state) to its owning state type T
  // (unique definer). customTags lists CUSTOM animationTags keys, whose first-definer
  // owner can shift between state types -> a consumer of any custom tag must depend on
  // the global stateTypesEpoch, not a single owner.
  struct TagDeps {
    HashMap<String, String> stateTagOwner;
    Set<String> customTags;
  };

  // The per-call build context: effect/processing directives prefix plus the
  // resolved animation tags.  baseProcessingDirectives is per-part
  // appended/restored by appendPartDrawables, hence non-const.
  void drawableBuildContext(List<Directives>& baseProcessingDirectives,
      HashMap<String, String>& animationTags, TagDeps* tagDeps = nullptr) const;

  // All active parts enumerated and stable-sorted by zLevel, exactly the
  // ordering the rebuild path draws in.  Enumerating freshens every part
  // (AnimatedPartSet does this lazily), settling generation() before the
  // cache is keyed on it.  drawableCount accumulates m_partDrawables extras
  // for reserve().
  List<tuple<AnimatedPartSet::ActivePartInformation const*, String const*, float>> sortedActiveParts(int& drawableCount) const;

  // Builds the given part's drawables (image drawable plus m_partDrawables
  // extras) at the given translate and appends them to the output list.
  void appendPartDrawables(String const& partName, AnimatedPartSet::ActivePartInformation const& activePart,
      float zLevel, Vec2F const& translate, List<Directives>& baseProcessingDirectives,
      HashMap<String, String> const& animationTags, List<pair<Drawable, float>>& drawables,
      TagDeps const* tagDeps = nullptr, Set<String>* consumedStateTypes = nullptr,
      bool* consumedCustomTag = nullptr) const;

  // Re-partitions and rebuilds m_staticCache (static parts only, ZERO
  // translate) for the given sorted part list, recording the given cache key.
  void rebuildStaticCache(List<tuple<AnimatedPartSet::ActivePartInformation const*, String const*, float>> const& parts,
      tuple<uint64_t, uint64_t, uint64_t> const& key) const;
  List<pair<Drawable, float>> drawablesWithZLevelPerPart(Vec2F const& position) const;

  // Shadow-compare (runtime flag renderDrawableCacheShadowCompare):
  // diagnostics-only check of the assembled cache-path output against a
  // forced rebuild for the same state.  Same size + per-drawable equality;
  // position is compared with a small ulp tolerance (the two paths apply the
  // world translate in a different order -- see the parity note in
  // drawablesWithZLevel), every other field exactly.  Each mismatching
  // drawable counts into render.drawable.cache.shadowMismatch; the
  // Logger::warn is rate-limited to once per animator.  partStarts maps
  // assembled drawable index -> source part name for the warn's diagnostics.
  void shadowCompare(List<pair<Drawable, float>> const& cached, List<pair<Drawable, float>> const& rebuilt,
      List<pair<size_t, String const*>> const& partStarts) const;

  void netElementsNeedLoad(bool full) override;
  void netElementsNeedStore() override;

  Json mergeIncludes(Json config, Json includes, String relativePath);

  String m_relativePath;
  uint8_t m_animatorVersion;

  AnimatedPartSet m_animatedParts;
  OrderedHashMap<String, StateInfo> m_stateInfo;
  OrderedHashMap<String, RotationGroup> m_rotationGroups;
  OrderedHashMap<String, TransformationGroup> m_transformationGroups;
  OrderedHashMap<String, ParticleEmitter> m_particleEmitters;
  OrderedHashMap<String, Light> m_lights;
  OrderedHashMap<String, Sound> m_sounds;
  OrderedHashMap<String, Effect> m_effects;

  NetElementData<Directives> m_processingDirectives;
  NetElementFloat m_zoom;

  NetElementBool m_flipped;
  NetElementFloat m_flippedRelativeCenterLine;

  NetElementFloat m_animationRate;

  NetElementHashMap<String, String> m_globalTags;
  StableStringMap<NetElementHashMap<String, String>> m_partTags;
  HashMap<String, String> m_localTags;

  HashMap<String,List<Drawable>> m_partDrawables;

  mutable StringMap<std::pair<size_t, Drawable>> m_cachedPartDrawables;

  // Static-partition drawable cache (runtime flag renderDrawableCache), keyed
  // by part name: presence in the map IS the static partition recorded at
  // build time, so the serve path builds exactly the complement (LIVE parts)
  // fresh each call.  Drawables are cached at ZERO translate; the world
  // translate is applied live after assembly.  Valid only while
  // m_staticCacheKey == (m_renderVersion, m_animatedParts.generation(),
  // localTransformHash()).  Main-thread only like m_renderVersion (no
  // atomics); mutable for the const drawables path.
  mutable StringMap<List<pair<Drawable, float>>> m_staticCache;
  mutable bool m_staticCacheValid = false;
  mutable tuple<uint64_t, uint64_t, uint64_t> m_staticCacheKey;

  // Per-part static drawable cache (runtime flag renderDrawableCachePerPart):
  // the part-granular refinement of m_staticCache above.  Each static part
  // caches its zero-translate drawables under its OWN key
  // (m_renderVersion, m_animatedParts.partGeneration(part), localTransformHash()),
  // so a sibling part advancing its animation frame -- which bumps the
  // whole-entity generation() and would re-key the single m_staticCacheKey,
  // clearing the WHOLE m_staticCache -- no longer invalidates parts that did not
  // themselves change.  Same zero-translate / world-translate-after convention
  // and same partIsStaticCacheable partition as m_staticCache.  Correctness on
  // config replacement is covered by the renderVersion key component (operator=
  // bumps renderVersion); also cleared in operator= for memory hygiene.
  // Main-thread only; mutable for the const drawables path.
  // Cross-state-type animation tags ARE captured per entry (the partGeneration
  // key tracks only a part's OWN resolved state).  A static part resolving
  // another state type's built-in <T_state>/<T_frame>/<T_frameIndex> tag records
  // a PRECISE per-state-type dependency (stateTypeDeps, checked against that
  // state type's generation); resolving any custom animationTags key records the
  // conservative stateTypesEpoch dependency (dependsAllStateTypes).  Either
  // invalidates the entry when the foreign state type changes, so the per-part
  // cache stays correct (DrawableCache.PerPart* tests).
  struct StaticPartCacheEntry {
    List<pair<Drawable, float>> drawables;
    uint64_t renderVersion = 0;
    uint64_t partGeneration = 0;
    uint64_t localTransformHash = 0;
    // Cross-state-type-tag dependency (Lever 1b): built-in foreign tags -> exact
    // per-state-type deps; any custom-tag consumption -> depend on stateTypesEpoch.
    List<pair<String, uint64_t>> stateTypeDeps;
    bool dependsAllStateTypes = false;
    uint64_t stateTypesEpoch = 0;
  };
  mutable StringMap<StaticPartCacheEntry> m_staticCachePerPart;

  // Memo of partIsStaticCacheableStructural verdicts.  Every structural input
  // is construction-constant (part configs, anchor chain, group structure,
  // angularVelocity, interpolated, version()) EXCEPT the active-state-animated
  // transformation-group check, which reads activeState(...).properties --
  // those re-merge only under a generation() bump (AnimatedPartSet's freshen
  // layers), so the memo is keyed on generation() (m_partitionMemoGeneration)
  // and stale verdicts are impossible.  Cleared by operator= (assignment
  // replaces the whole config; generations are per-AnimatedPartSet counters
  // and could collide).  The flash-effect gate is a runtime input and stays
  // OUTSIDE the memo.  Main-thread only like the cache; mutable for the const
  // drawables path.
  mutable StringMap<bool> m_partitionMemo;
  mutable uint64_t m_partitionMemoGeneration = 0;

  // Rate-limits the shadow-mismatch Logger::warn to once per animator (the
  // shadowMismatch counter still counts every mismatch).  Main-thread only
  // like the cache; mutable for the const drawables path.
  mutable bool m_shadowMismatchWarned = false;

  // Main-thread only (no atomics): drawables() and netElementsNeedLoad are
  // expected to run on the same thread.
  uint64_t m_renderVersion = 1;

  // Shadow copies of the last values of the discrete drawable-affecting
  // NetElements seen by netElementsNeedLoad.  On slaves the setters are never
  // called (NetElement deserialization writes storage directly), so the net
  // funnel value-diffs against these to detect discrete changes.  Initialised
  // to the same defaults the default constructor gives their NetElements.
  Directives m_lastSeenProcessingDirectives;
  float m_lastSeenZoom = 1.0f;
  bool m_lastSeenFlipped = false;
  float m_lastSeenCenterLine = 0.0f;
};

}
