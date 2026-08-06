#include "StarNetworkedAnimator.hpp"
#include "StarJsonExtra.hpp"
#include "StarIterator.hpp"
#include "StarSet.hpp"
#include "StarParticleDatabase.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarConfiguration.hpp"
#include "StarLexicalCast.hpp"
#include "StarDataStreamExtra.hpp"
#include "StarRandom.hpp"
#include "StarGameTypes.hpp"
#include "StarTelemetry.hpp"
#include "StarLogging.hpp"

#include <cstdint>
#include <cstring>
#include <limits>

namespace Star {

NetworkedAnimator::DynamicTarget::~DynamicTarget() {
  stopAudio();
}

List<AudioInstancePtr> NetworkedAnimator::DynamicTarget::pullNewAudios() {
  pendingAudios.exec([this](AudioInstancePtr const& ptr) {
    Vec2F audioBasePosition = ptr->position().value();
    currentAudioBasePositions[ptr] = audioBasePosition;
    ptr->setPosition(position + audioBasePosition);
  });
  return take(pendingAudios);
}

List<Particle> NetworkedAnimator::DynamicTarget::pullNewParticles() {
  pendingParticles.exec([this](Particle& particle) {
    particle.position += position;
    return particle;
  });
  return take(pendingParticles);
}

void NetworkedAnimator::DynamicTarget::stopAudio() {
  for (auto const& pair : currentAudioBasePositions) {
    if (pair.first->loops() != 0)
      pair.first->stop();
  }
}

void NetworkedAnimator::DynamicTarget::updatePosition(Vec2F const& p) {
  clearFinishedAudio();
  position = p;
  for (auto& audioPair : currentAudioBasePositions)
    audioPair.first->setPosition(audioPair.second + p);
}

void NetworkedAnimator::DynamicTarget::clearFinishedAudio() {
  for (auto& p : statePersistentSounds) {
    if (p.second.audio && p.second.audio->finished())
      p.second.audio.reset();
  }

  for (auto& p : stateImmediateSounds) {
    if (p.second.audio && p.second.audio->finished())
      p.second.audio.reset();
  }

  for (auto& p : independentSounds)
    eraseWhere(p.second, [](AudioInstancePtr const& audio) { return audio->finished(); });

  eraseWhere(currentAudioBasePositions, [](pair<AudioInstancePtr, Vec2F> const& pair) {
      return pair.first->finished();
    });
}

NetworkedAnimator::NetworkedAnimator() {
  m_zoom.set(1.0f);
  m_flipped.set(false);
  m_flippedRelativeCenterLine.set(0.0f);
  m_animationRate.set(1.0f);
  m_animatorVersion = 0;
  setupNetStates();
}

NetworkedAnimator::NetworkedAnimator(Json config, String relativePath) : NetworkedAnimator() {
  auto& root = Root::singleton();

  if (config.isNull())
    return;

  if (config.type() == Json::Type::String) {
    if (relativePath.empty())
      relativePath = config.toString();
    config = root.assets()->json(AssetPath::relativeTo(relativePath, config.toString()));
  } else {
    if (relativePath.empty())
      relativePath = "/";
  }
  m_animatorVersion = config.getUInt("version", 0);

  if (version() > 0) {
    if (config.contains("includes"))
      config = mergeIncludes(config, config.get("includes"), relativePath);
  }

  m_animatedParts = AnimatedPartSet(config.get("animatedParts", JsonObject()), version());
  m_relativePath = AssetPath::directory(relativePath);

  for (auto const& pair : config.get("globalTagDefaults", JsonObject()).iterateObject())
    setGlobalTag(pair.first, pair.second.toString());

  for (auto const& part : config.get("partTagDefaults", JsonObject()).iterateObject()) {
    for (auto const& tag : part.second.iterateObject())
      setPartTag(part.first, tag.first, tag.second.toString());
  }

  for (auto const& pair : config.get("transformationGroups", JsonObject()).iterateObject()) {
    auto& tg = m_transformationGroups[pair.first];
    tg.interpolated = pair.second.getBool("interpolated", false);
    tg.setAffineTransform(Mat3F::identity());
    tg.setAnimationAffineTransform(Mat3F::identity());
    tg.setLocalAffineTransform(Mat3F::identity());
  }

  for (auto const& pair : config.get("rotationGroups", JsonObject()).iterateObject()) {
    String rotationGroupName = pair.first;
    Json rotationGroupConfig = pair.second;
    RotationGroup& rotationGroup = m_rotationGroups[std::move(rotationGroupName)];
    rotationGroup.angularVelocity = rotationGroupConfig.getFloat("angularVelocity", 0.0f);
    rotationGroup.rotationCenter = jsonToVec2F(rotationGroupConfig.get("rotationCenter", JsonArray{0, 0}));
  }

  for (auto const& pair : config.get("particleEmitters", JsonObject()).iterateObject()) {
    String particleEmitterName = pair.first;
    Json particleEmitterConfig = pair.second;

    ParticleEmitter& emitter = m_particleEmitters[std::move(particleEmitterName)];
    emitter.emissionRate.set(particleEmitterConfig.getFloat("emissionRate", 1.0f));
    emitter.emissionRateVariance = particleEmitterConfig.getFloat("emissionRateVariance", 0.0f);
    emitter.offsetRegion.set(particleEmitterConfig.opt("offsetRegion").apply(jsonToRectF).value(RectF::null()));
    emitter.anchorPart = particleEmitterConfig.optString("anchorPart");
    emitter.transformationGroups = jsonToStringList(particleEmitterConfig.get("transformationGroups", JsonArray()));
    emitter.rotationGroup = particleEmitterConfig.optString("rotationGroup");
    emitter.rotationCenter = particleEmitterConfig.opt("rotationCenter").apply(jsonToVec2F);

    for (auto const& config : particleEmitterConfig.get("particles").iterateArray()) {
      auto creator = root.particleDatabase()->particleCreator(config.get("particle"), relativePath);
      unsigned count = config.getUInt("count", 1);
      Vec2F offset = jsonToVec2F(config.get("offset", JsonArray{0, 0}));
      bool flip = config.getBool("flip", false);
      emitter.particleList.append({creator, count, offset, flip});
    }

    // default to one cycle through the particle list in a burst
    emitter.burstCount.set(particleEmitterConfig.getUInt("burstCount", 1));

    // default to one of each to preserve current behaviour.
    emitter.randomSelectCount.set(particleEmitterConfig.getUInt("randomSelectCount", emitter.particleList.size()));

    emitter.active.set(particleEmitterConfig.getBool("active", false));
  }

  for (auto const& pair : config.get("lights", JsonObject()).iterateObject()) {
    String lightName = pair.first;
    Json lightConfig = pair.second;

    Light& light = m_lights[std::move(lightName)];
    light.active.set(lightConfig.getBool("active", true));
    auto lightPosition = lightConfig.opt("position").apply(jsonToVec2F).value();
    light.xPosition.set(lightPosition[0]);
    light.yPosition.set(lightPosition[1]);
    light.color.set(lightConfig.opt("color").apply(jsonToColor).value(Color::White));
    light.anchorPart = lightConfig.optString("anchorPart");
    light.transformationGroups = jsonToStringList(lightConfig.get("transformationGroups", JsonArray()));
    light.rotationGroup = lightConfig.optString("rotationGroup");
    light.rotationCenter = lightConfig.opt("rotationCenter").apply(jsonToVec2F);

    if (lightConfig.contains("flickerPeriod")) {
      light.flicker = PeriodicFunction<float>(
          lightConfig.getFloat("flickerPeriod"),
          lightConfig.getFloat("flickerMinIntensity", 0.0),
          lightConfig.getFloat("flickerMaxIntensity", 0.0),
          lightConfig.getFloat("flickerPeriodVariance", 0.0),
          lightConfig.getFloat("flickerIntensityVariance", 0.0)
        );
    }

    light.pointAngle.set(lightConfig.getFloat("pointAngle", 0.0f) * Constants::deg2rad);
    light.pointLight = lightConfig.getBool("pointLight", false);
    light.pointBeam = lightConfig.getFloat("pointBeam", 0.0f);
    light.beamAmbience = lightConfig.getFloat("beamAmbience", 0.0f);
  }

  for (auto const& pair : config.get("sounds", JsonObject()).iterateObject()) {
    String soundName = pair.first;
    Json soundConfig = pair.second;
    Sound& sound = m_sounds[std::move(soundName)];
    if (soundConfig.isType(Json::Type::Array)) {
      sound.rangeMultiplier = 1.0f;
      sound.soundPool.set(jsonToStringList(soundConfig).transformed(bind(&AssetPath::relativeTo, m_relativePath, _1)));
      sound.volumeTarget.set(1.0f);
      sound.volumeRampTime.set(0.0f);
      sound.pitchMultiplierTarget.set(1.0f);
      sound.pitchMultiplierRampTime.set(0.0f);
    } else {
      sound.rangeMultiplier = soundConfig.getFloat("rangeMultiplier", 1.0f);

      auto soundPosition = soundConfig.opt("position").apply(jsonToVec2F).value();
      sound.xPosition.set(soundPosition[0]);
      sound.yPosition.set(soundPosition[1]);

      sound.volumeTarget.set(soundConfig.getFloat("volume", 1.0f));
      sound.volumeRampTime.set(soundConfig.getFloat("volumeRampTime", 0.0f));

      sound.pitchMultiplierTarget.set(soundConfig.getFloat("pitchMultiplier", 1.0f));
      sound.pitchMultiplierRampTime.set(soundConfig.getFloat("pitchMultiplierRampTime", 0.0f));

      sound.soundPool.set(jsonToStringList(soundConfig.get("pool", JsonArray())).transformed(bind(&AssetPath::relativeTo, m_relativePath, _1)));
    }
  }

  for (auto const& pair : config.get("effects", JsonObject()).iterateObject()) {
    String effectName = pair.first;
    Json effectConfig = pair.second;

    Effect& effect = m_effects[effectName];
    effect.type = effectConfig.getString("type");
    effect.time = effectConfig.getFloat("time", 0.0f);
    effect.directives = effectConfig.getString("directives");
  }

  // Sort all the states that contain NetStates handles predictably by key.
  m_transformationGroups.sortByKey();
  m_rotationGroups.sortByKey();
  m_particleEmitters.sortByKey();
  m_lights.sortByKey();
  m_sounds.sortByKey();
  m_effects.sortByKey();

  // Make sure that every state type has an entry in the state info map, and
  // order it predictably by key.
  for (auto const& stateType : m_animatedParts.stateTypes()) {
    StateInfo& stateInfo = m_stateInfo[stateType];
    stateInfo.wasUpdated = true;
  }

  m_stateInfo.sortByKey();

  setupNetStates();
}

NetworkedAnimator::NetworkedAnimator(NetworkedAnimator&& animator) {
  operator=(std::move(animator));
}

NetworkedAnimator::NetworkedAnimator(NetworkedAnimator const& animator) {
  operator=(animator);
}

NetworkedAnimator& NetworkedAnimator::operator=(NetworkedAnimator&& animator) {
  m_relativePath = std::move(animator.m_relativePath);
  m_animatedParts = std::move(animator.m_animatedParts);
  m_stateInfo = std::move(animator.m_stateInfo);
  m_transformationGroups = std::move(animator.m_transformationGroups);
  m_rotationGroups = std::move(animator.m_rotationGroups);
  m_particleEmitters = std::move(animator.m_particleEmitters);
  m_lights = std::move(animator.m_lights);
  m_sounds = std::move(animator.m_sounds);
  m_effects = std::move(animator.m_effects);
  m_processingDirectives = std::move(animator.m_processingDirectives);
  m_zoom = std::move(animator.m_zoom);
  m_flipped = std::move(animator.m_flipped);
  m_flippedRelativeCenterLine = std::move(animator.m_flippedRelativeCenterLine);
  m_animationRate = std::move(animator.m_animationRate);
  m_globalTags = std::move(animator.m_globalTags);
  m_partTags = std::move(animator.m_partTags);
  m_cachedPartDrawables = std::move(animator.m_cachedPartDrawables);
  m_partDrawables = std::move(animator.m_partDrawables);
  m_localTags = std::move(animator.m_localTags);
  m_animatorVersion = std::move(animator.m_animatorVersion);
  setupNetStates();
  // Assignment replaces every drawable-affecting member wholesale; bump the
  // target's own (never-copied) render version so any cached render output is
  // invalidated.  The static cache is never copied: drop it explicitly.  The
  // partition memo's verdicts are for the REPLACED config, and the incoming
  // AnimatedPartSet's generation could collide with the recorded one: drop it
  // too.
  m_staticCacheValid = false;
  m_staticCachePerPart.clear();
  m_partitionMemo.clear();
  m_partitionMemoGeneration = 0;
  bumpRenderVersion();

  return *this;
}

NetworkedAnimator& NetworkedAnimator::operator=(NetworkedAnimator const& animator) {
  m_relativePath = animator.m_relativePath;
  m_animatedParts = animator.m_animatedParts;
  m_stateInfo = animator.m_stateInfo;
  m_transformationGroups = animator.m_transformationGroups;
  m_rotationGroups = animator.m_rotationGroups;
  m_particleEmitters = animator.m_particleEmitters;
  m_lights = animator.m_lights;
  m_sounds = animator.m_sounds;
  m_effects = animator.m_effects;
  m_processingDirectives = animator.m_processingDirectives;
  m_zoom = animator.m_zoom;
  m_flipped = animator.m_flipped;
  m_flippedRelativeCenterLine = animator.m_flippedRelativeCenterLine;
  m_animationRate = animator.m_animationRate;
  m_globalTags = animator.m_globalTags;
  m_partTags = animator.m_partTags;
  m_cachedPartDrawables = animator.m_cachedPartDrawables;
  m_partDrawables = animator.m_partDrawables;
  m_localTags = animator.m_localTags;
  m_animatorVersion = animator.m_animatorVersion;
  setupNetStates();
  // Assignment replaces every drawable-affecting member wholesale; bump the
  // target's own (never-copied) render version so any cached render output is
  // invalidated.  The static cache is never copied: drop it explicitly.  The
  // partition memo's verdicts are for the REPLACED config, and the incoming
  // AnimatedPartSet's generation could collide with the recorded one: drop it
  // too.
  m_staticCacheValid = false;
  m_staticCachePerPart.clear();
  m_partitionMemo.clear();
  m_partitionMemoGeneration = 0;
  bumpRenderVersion();

  return *this;
}

StringList NetworkedAnimator::stateTypes() const {
  return m_animatedParts.stateTypes();
}

StringList NetworkedAnimator::states(String const& stateType) const {
  return m_animatedParts.states(stateType);
}

bool NetworkedAnimator::setState(String const& stateType, String const& state, bool startNew, bool reverse) {
  if (m_animatedParts.setActiveState(stateType, state, startNew, reverse)) {
    m_stateInfo[stateType].wasUpdated = true;
    m_stateInfo[stateType].startedEvent.trigger();
    return true;
  } else {
    return false;
  }
}

bool NetworkedAnimator::setLocalState(String const& stateType, String const& state, bool startNew, bool reverse) {
  return m_animatedParts.setActiveState(stateType, state, startNew, reverse);
}

String NetworkedAnimator::state(String const& stateType) const {
  return m_animatedParts.activeState(stateType).stateName;
}
int NetworkedAnimator::stateFrame(String const& stateType) const {
  return m_animatedParts.activeState(stateType).frame;
}
int NetworkedAnimator::stateNextFrame(String const& stateType) const {
  return m_animatedParts.activeState(stateType).nextFrame;
}
float NetworkedAnimator::stateFrameProgress(String const& stateType) const {
  return m_animatedParts.activeState(stateType).frameProgress;
}
float NetworkedAnimator::stateTimer(String const& stateType) const {
  return m_animatedParts.activeState(stateType).timer;
}
bool NetworkedAnimator::stateReverse(String const& stateType) const {
  return m_animatedParts.activeState(stateType).reverse;
}

float NetworkedAnimator::stateCycle(String const& stateType, Maybe<String> state) const {
  return m_animatedParts.getState(stateType, state.value(m_animatedParts.activeState(stateType).stateName)).cycle;
}
int NetworkedAnimator::stateFrames(String const& stateType, Maybe<String> state) const {
  return m_animatedParts.getState(stateType, state.value(m_animatedParts.activeState(stateType).stateName)).frames;
}

bool NetworkedAnimator::hasState(String const & stateType, Maybe<String> const & state) const {
  if (m_animatedParts.stateTypes().contains(stateType)) {
    if (state) {
      return m_animatedParts.states(stateType).contains(*state);
    }
    return true;
  }
  return false;
}

StringMap<AnimatedPartSet::Part> const& NetworkedAnimator::constParts() const {
  return m_animatedParts.constParts();
}

StringMap<AnimatedPartSet::Part>& NetworkedAnimator::parts() {
  return m_animatedParts.parts();
}

StringList NetworkedAnimator::partNames() const {
  return m_animatedParts.partNames();
}

Json NetworkedAnimator::stateProperty(String const& stateType, String const& propertyName, Maybe<String> state, Maybe<int> frame) const {
  if (state.isValid())
    return m_animatedParts.getStateFrameProperty(stateType, propertyName, *state, *frame);
  return m_animatedParts.activeState(stateType).properties.value(propertyName);
}
Json NetworkedAnimator::stateNextProperty(String const& stateType, String const& propertyName) const {
  return m_animatedParts.activeState(stateType).nextProperties.value(propertyName);
}

Json NetworkedAnimator::partProperty(String const& partName, String const& propertyName, Maybe<String> stateType, Maybe<String> state, Maybe<int> frame) const {
  if (stateType.isValid())
    return m_animatedParts.getPartStateFrameProperty(partName, propertyName, *stateType, *state, *frame);
  return m_animatedParts.activePart(partName).properties.value(propertyName);
}
Json NetworkedAnimator::partNextProperty(String const& partName, String const& propertyName) const {
  return m_animatedParts.activePart(partName).nextProperties.value(propertyName);
}

Mat3F NetworkedAnimator::globalTransformation() const {
  Mat3F transformation = Mat3F::scaling(m_zoom.get());
  if (m_flipped.get())
    transformation = Mat3F::scaling(Vec2F(-1, 1), Vec2F(m_flippedRelativeCenterLine.get(), 0)) * transformation;
  return transformation;
}

Mat3F NetworkedAnimator::groupTransformation(StringList const& transformationGroups) const {
  auto mat = Mat3F::identity();
  for (auto const& tg : transformationGroups)
    mat = m_transformationGroups.get(tg).affineTransform() * m_transformationGroups.get(tg).localAffineTransform() * m_transformationGroups.get(tg).animationAffineTransform() * mat;
  return mat;
}

Mat3F NetworkedAnimator::partTransformation(String const& partName) const {
  auto const& part = m_animatedParts.activePart(partName);
  Mat3F transformation = Mat3F::identity();

  if (auto offset = part.properties.value("offset").opt().apply(jsonToVec2F))
    transformation = Mat3F::translation(*offset) * transformation;

  transformation = part.animationAffineTransform() * transformation;

  auto transformationGroups = jsonToStringList(part.properties.value("transformationGroups", JsonArray()));
  transformation = groupTransformation(transformationGroups) * transformation;

  if (auto rotationGroupName = part.properties.value("rotationGroup").optString()) {
    auto const& rotationGroup = m_rotationGroups.get(*rotationGroupName);
    Vec2F rotationCenter = part.properties.value("rotationCenter").opt().apply(jsonToVec2F).value(rotationGroup.rotationCenter);
    transformation = Mat3F::rotation(rotationGroup.currentAngle, rotationCenter) * transformation;
  }

  if (auto anchorPart = part.properties.ptr("anchorPart"))
    transformation = partTransformation(anchorPart->toString()) * transformation;

  return transformation;
}

Mat3F NetworkedAnimator::finalPartTransformation(String const& partName) const {
  return globalTransformation() * partTransformation(partName);
}

Maybe<Vec2F> NetworkedAnimator::partPoint(String const& partName, String const& propertyName) const {
  auto const& part = m_animatedParts.activePart(partName);
  auto property = part.properties.value(propertyName);
  if (!property)
    return {};

  return finalPartTransformation(partName).transformVec2(jsonToVec2F(property));
}

Maybe<PolyF> NetworkedAnimator::partPoly(String const& partName, String const& propertyName) const {
  auto const& part = m_animatedParts.activePart(partName);
  auto property = part.properties.value(propertyName, {});
  if (!property)
    return {};

  PolyF poly = jsonToPolyF(property);
  poly.transform(finalPartTransformation(partName));
  return poly;
}

void NetworkedAnimator::setGlobalTag(String tagName, Maybe<String> tagValue) {
  if (tagValue) {
    // Observable state identical (tag already has this value) => skip write + version bump.
    if (auto current = m_globalTags.ptr(tagName); current && *current == *tagValue)
      return;
    m_globalTags.set(std::move(tagName), std::move(*tagValue));
  } else {
    // Observable state identical (clearing an absent tag) => skip write + version bump.
    if (!m_globalTags.remove(tagName))
      return;
  }
  bumpRenderVersion();
}

void NetworkedAnimator::removeGlobalTag(String const& tagName) {
  // Observable state identical (removing an absent tag) => skip write + version bump.
  if (m_globalTags.remove(tagName))
    bumpRenderVersion();
}

String const* NetworkedAnimator::globalTagPtr(String const& tagName) const {
  return m_globalTags.ptr(tagName);
}


void NetworkedAnimator::setPartTag(String const& partType, String tagName, Maybe<String> tagValue) {
  if (tagValue) {
    // Observable state identical (tag already has this value) => skip write + version bump.
    // ptr() to avoid operator[]'s insertion: setupNetStates pre-registers an
    // entry per part, so a miss here means an unknown partType.
    if (auto tags = m_partTags.ptr(partType))
      if (auto current = tags->ptr(tagName); current && *current == *tagValue)
        return;
    m_partTags[partType].set(std::move(tagName), std::move(*tagValue));
  } else {
    // Observable state identical (clearing an absent tag) => skip write + version bump.
    auto tags = m_partTags.ptr(partType);
    if (!tags || !tags->remove(tagName))
      return;
  }
  bumpRenderVersion();
}

void NetworkedAnimator::setLocalTag(String tagName, Maybe<String> tagValue) {
  if (tagValue) {
    // Observable state identical (tag already has this value) => skip write + version bump.
    if (auto current = m_localTags.ptr(tagName); current && *current == *tagValue)
      return;
    m_localTags.set(tagName, *tagValue);
  } else {
    // Observable state identical (clearing an absent tag) => skip write + version bump.
    if (!m_localTags.remove(tagName))
      return;
  }
  bumpRenderVersion();
}

namespace {
  // Exact per-field Drawable comparison for the setPartDrawables no-op guard:
  // the same salient fields shadowCompare diffs, but EXACT everywhere (these
  // are caller-provided values compared against their previously-stored
  // selves, not cross-path arithmetic, so no ulp tolerance applies).
  bool drawableEquals(Drawable const& a, Drawable const& b) {
    if (a.position != b.position || !(a.color == b.color) || a.fullbright != b.fullbright)
      return false;
    if (a.isImage() != b.isImage() || a.isLine() != b.isLine() || a.isPoly() != b.isPoly())
      return false;
    if (a.isImage()) {
      auto const& ai = a.imagePart();
      auto const& bi = b.imagePart();
      return ai.image == bi.image && ai.transformation == bi.transformation;
    }
    if (a.isLine()) {
      auto const& al = a.linePart();
      auto const& bl = b.linePart();
      return al.line == bl.line && al.width == bl.width && al.endColor == bl.endColor;
    }
    if (a.isPoly())
      return a.polyPart().poly == b.polyPart().poly;
    return true;  // both part-less
  }

  bool drawableListsEqual(List<Drawable> const& a, List<Drawable> const& b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (!drawableEquals(a[i], b[i]))
        return false;
    return true;
  }
}

void NetworkedAnimator::setPartDrawables(String const& partName, List<Drawable> drawables) {
  // Observable state identical (stored list matches field-for-field) => skip
  // write + version bump.  Only when an entry already exists: the first set
  // also establishes the m_partDrawables entry addPartDrawables appends to.
  if (auto current = m_partDrawables.ptr(partName); current && drawableListsEqual(*current, drawables))
    return;
  m_partDrawables.set(partName, drawables);
  // The render-version bump already re-keys the static cache; the explicit
  // invalidation is belt-and-braces for changes to the part set itself.
  m_staticCacheValid = false;
  bumpRenderVersion();
}
void NetworkedAnimator::addPartDrawables(String const& partName, List<Drawable> drawables) {
  // Observable state identical (appending nothing) => skip write + version bump.
  if (drawables.empty())
    return;
  m_partDrawables.ptr(partName)->appendAll(drawables);
  m_staticCacheValid = false;
  bumpRenderVersion();
}
String NetworkedAnimator::applyPartTags(String const& partName, String apply) const {
  HashMap<String, String> animationTags = m_localTags;
  Maybe<unsigned> frame;
  String frameStr;
  String frameIndexStr;
  auto activePart = m_animatedParts.activePart(partName);
  auto partTags = m_partTags.get(partName);
  if (activePart.activeState) {
    unsigned stateFrame = activePart.activeState->frame;
    frame = stateFrame;
    frameStr = static_cast<String>(toString(stateFrame + 1));
    frameIndexStr = static_cast<String>(toString(stateFrame));
  }
  if (version() > 0) {
    animationTags.set("relativePath", m_relativePath);
    for (auto& stateTypeName : m_animatedParts.stateTypes()) {
      auto& activeState = m_animatedParts.activeState(stateTypeName);
      unsigned stateFrame = activeState.frame;
      Maybe<unsigned> frame;
      String frameStr;
      String frameIndexStr;

      frame = stateFrame;
      frameStr = static_cast<String>(toString(stateFrame + 1));
      frameIndexStr = static_cast<String>(toString(stateFrame));
      if (frame) {
        animationTags.set(stateTypeName + "_frame", frameStr);
        animationTags.set(stateTypeName + "_frameIndex", frameIndexStr);
      }
      animationTags.set(stateTypeName + "_state", activeState.stateName);

      if (auto p = activeState.properties.ptr("animationTags")) {
        for (auto tag : p->iterateObject())
          if (!animationTags.contains(tag.first))
            animationTags.set(tag.first, tag.second.toString());
      }
    }
  }


  auto applied = apply.maybeLookupTagsView([&](StringView tag) -> StringView {
    if (tag == "frame") {
      if (frame)
        return frameStr;
    } else if (tag == "frameIndex") {
      if (frame)
        return frameIndexStr;
    } else if (auto p = animationTags.ptr(tag)) {
      return StringView(*p);
    } else if (auto p = partTags.ptr(tag)) {
      return StringView(*p);
    } else if (auto p = m_globalTags.ptr(tag)) {
      return StringView(*p);
    }

    return StringView("default");
  });
  return applied ? applied.get() : apply;
}


void NetworkedAnimator::setProcessingDirectives(Directives const& directives) {
  m_processingDirectives.set(directives);
  bumpRenderVersion();
}

void NetworkedAnimator::setZoom(float zoom) {
  m_zoom.set(zoom);
  bumpRenderVersion();
}

bool NetworkedAnimator::flipped() const {
  return m_flipped.get();
}

float NetworkedAnimator::flippedRelativeCenterLine() const {
  return m_flippedRelativeCenterLine.get();
}

void NetworkedAnimator::setFlipped(bool flipped, float relativeCenterLine) {
  m_flipped.set(flipped);
  m_flippedRelativeCenterLine.set(relativeCenterLine);
  bumpRenderVersion();
}

void NetworkedAnimator::setAnimationRate(float rate) {
  m_animationRate.set(rate);
}

float NetworkedAnimator::animationRate() {
  return m_animationRate.get();
}

bool NetworkedAnimator::hasRotationGroup(String const& rotationGroup) const {
  return m_rotationGroups.contains(rotationGroup);
}

void NetworkedAnimator::rotateGroup(String const& rotationGroup, float targetAngle, bool immediate) {
  auto& group = m_rotationGroups.get(rotationGroup);
  // Observable state identical (same target, and nothing to snap unless
  // immediate) => skip write + version bump.
  if (group.targetAngle.get() == targetAngle && (!immediate || group.currentAngle == targetAngle))
    return;
  group.targetAngle.set(targetAngle);

  if (immediate) {
    group.currentAngle = targetAngle;
    group.netImmediateEvent.trigger();
  }
  bumpRenderVersion();
}

float NetworkedAnimator::currentRotationAngle(String const& rotationGroup) const {
  return m_rotationGroups.get(rotationGroup).currentAngle;
}

bool NetworkedAnimator::hasTransformationGroup(String const& transformationGroup) const {
  return m_transformationGroups.contains(transformationGroup);
}

void NetworkedAnimator::translateTransformationGroup(String const& transformationGroup, Vec2F const& translation) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setAffineTransform(Mat3F::translation(translation) * group.affineTransform());
  bumpRenderVersion();
}

void NetworkedAnimator::rotateTransformationGroup(
    String const& transformationGroup, float rotation, Vec2F const& rotationCenter) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setAffineTransform(Mat3F::rotation(rotation, rotationCenter) * group.affineTransform());
  bumpRenderVersion();
}

void NetworkedAnimator::scaleTransformationGroup(
    String const& transformationGroup, float scale, Vec2F const& scaleCenter) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setAffineTransform(Mat3F::scaling(scale, scaleCenter) * group.affineTransform());
  bumpRenderVersion();
}

void NetworkedAnimator::scaleTransformationGroup(
    String const& transformationGroup, Vec2F const& scale, Vec2F const& scaleCenter) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setAffineTransform(Mat3F::scaling(scale, scaleCenter) * group.affineTransform());
  bumpRenderVersion();
}

void NetworkedAnimator::transformTransformationGroup(
    String const& transformationGroup, float a, float b, float c, float d, float tx, float ty) {
  auto& group = m_transformationGroups.get(transformationGroup);
  Mat3F transform = Mat3F(a, b, tx, c, d, ty, 0, 0, 1);
  group.setAffineTransform(transform * group.affineTransform());
  bumpRenderVersion();
}

void NetworkedAnimator::resetTransformationGroup(String const& transformationGroup) {
  m_transformationGroups.get(transformationGroup).setAffineTransform(Mat3F::identity());
  bumpRenderVersion();
}

void NetworkedAnimator::setTransformationGroup(String const& transformationGroup, Mat3F transform) {
  m_transformationGroups.get(transformationGroup).setAffineTransform(transform);
  bumpRenderVersion();
}

Mat3F NetworkedAnimator::getTransformationGroup(String const& transformationGroup) {
  return m_transformationGroups.get(transformationGroup).affineTransform();
}
// The Local transformation-group setters deliberately do NOT bump
// renderVersion: Humanoid::render calls them in reset+rotate(SAME angle) pairs
// every frame, and reset->identity->rotate-back is two real value changes
// netting to zero, so neither per-call bumps nor per-call value-diffs can keep
// a visually-stationary animator's cache key stable.  Instead the combined
// matrix state keys the static cache directly via localTransformHash() (see
// drawablesWithZLevel): if ANY group's localTransform matrix differs, the key
// differs -- exactly the invalidation coverage the bumps used to provide.
void NetworkedAnimator::translateLocalTransformationGroup(String const& transformationGroup, Vec2F const& translation) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setLocalAffineTransform(Mat3F::translation(translation) * group.localAffineTransform());
}

void NetworkedAnimator::rotateLocalTransformationGroup(
    String const& transformationGroup, float rotation, Vec2F const& rotationCenter) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setLocalAffineTransform(Mat3F::rotation(rotation, rotationCenter) * group.localAffineTransform());
}

void NetworkedAnimator::scaleLocalTransformationGroup(
    String const& transformationGroup, float scale, Vec2F const& scaleCenter) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setLocalAffineTransform(Mat3F::scaling(scale, scaleCenter) * group.localAffineTransform());
}

void NetworkedAnimator::scaleLocalTransformationGroup(
    String const& transformationGroup, Vec2F const& scale, Vec2F const& scaleCenter) {
  auto& group = m_transformationGroups.get(transformationGroup);
  group.setLocalAffineTransform(Mat3F::scaling(scale, scaleCenter) * group.localAffineTransform());
}

void NetworkedAnimator::transformLocalTransformationGroup(
    String const& transformationGroup, float a, float b, float c, float d, float tx, float ty) {
  auto& group = m_transformationGroups.get(transformationGroup);
  Mat3F transform = Mat3F(a, b, tx, c, d, ty, 0, 0, 1);
  group.setLocalAffineTransform(transform * group.localAffineTransform());
}

void NetworkedAnimator::resetLocalTransformationGroup(String const& transformationGroup) {
  m_transformationGroups.get(transformationGroup).setLocalAffineTransform(Mat3F::identity());
}

void NetworkedAnimator::setLocalTransformationGroup(String const& transformationGroup, Mat3F transform) {
  m_transformationGroups.get(transformationGroup).setLocalAffineTransform(transform);
}

Mat3F NetworkedAnimator::getLocalTransformationGroup(String const& transformationGroup) {
  return m_transformationGroups.get(transformationGroup).localAffineTransform();
}

bool NetworkedAnimator::hasParticleEmitter(String const& emitterName) const {
  return m_particleEmitters.contains(emitterName);
}

void NetworkedAnimator::setParticleEmitterActive(String const& emitterName, bool active) {
  m_particleEmitters.get(emitterName).active.set(active);
}

void NetworkedAnimator::setParticleEmitterEmissionRate(String const& emitterName, float emissionRate) {
  m_particleEmitters.get(emitterName).emissionRate.set(emissionRate);
}

void NetworkedAnimator::setParticleEmitterOffsetRegion(String const& emitterName, RectF const& offsetRegion) {
  m_particleEmitters.get(emitterName).offsetRegion.set(offsetRegion);
}

void NetworkedAnimator::setParticleEmitterBurstCount(String const& emitterName, unsigned burstCount) {
  m_particleEmitters.get(emitterName).burstCount.set(burstCount);
}

void NetworkedAnimator::burstParticleEmitter(String const& emitterName) {
  m_particleEmitters.get(emitterName).burstEvent.trigger();
}

bool NetworkedAnimator::hasLight(String const& lightName) const {
  return m_lights.contains(lightName);
}

void NetworkedAnimator::setLightActive(String const& lightName, bool active) {
  m_lights.get(lightName).active.set(active);
}

void NetworkedAnimator::setLightPosition(String const& lightName, Vec2F position) {
  auto& light = m_lights.get(lightName);
  light.xPosition.set(position[0]);
  light.yPosition.set(position[1]);
}

void NetworkedAnimator::setLightColor(String const& lightName, Color color) {
  m_lights.get(lightName).color.set(color);
}

void NetworkedAnimator::setLightPointAngle(String const& lightName, float angle) {
  m_lights.get(lightName).pointAngle.set(angle * Constants::deg2rad);
}

bool NetworkedAnimator::hasSound(String const& soundName) const {
  return m_sounds.contains(soundName);
}

void NetworkedAnimator::setSoundPool(String const& soundName, StringList soundPool) {
  m_sounds.get(soundName).soundPool.set(std::move(soundPool));
}

void NetworkedAnimator::setSoundPosition(String const& soundName, Vec2F const& position) {
  auto& sound = m_sounds.get(soundName);
  sound.xPosition.set(position[0]);
  sound.yPosition.set(position[1]);
}

void NetworkedAnimator::setSoundVolume(String const& soundName, float volume, float rampTime) {
  auto& sound = m_sounds.get(soundName);
  sound.volumeTarget.set(volume);
  sound.volumeRampTime.set(rampTime);
}

void NetworkedAnimator::setSoundPitchMultiplier(String const& soundName, float pitchMultiplier, float rampTime) {
  auto& sound = m_sounds.get(soundName);
  sound.pitchMultiplierTarget.set(pitchMultiplier);
  sound.pitchMultiplierRampTime.set(rampTime);
}

void NetworkedAnimator::playSound(String const& soundName, int loops) {
  auto& sound = m_sounds.get(soundName);
  sound.loops.set(loops);
  sound.signals.send(SoundSignal::Play);
}

void NetworkedAnimator::stopAllSounds(String const& soundName, float rampTime) {
  auto& sound = m_sounds.get(soundName);
  sound.volumeRampTime.set(rampTime);
  sound.signals.send(SoundSignal::StopAll);
}

void NetworkedAnimator::setEffectEnabled(String const& effect, bool enabled) {
  m_effects.get(effect).enabled.set(enabled);
  bumpRenderVersion();
}

List<Drawable> NetworkedAnimator::drawables(Vec2F const& position) const {
  List<Drawable> drawables;
  for (auto& p : drawablesWithZLevel(position))
    drawables.append(std::move(p.first));
  return drawables;
}

List<pair<Drawable, float>> NetworkedAnimator::drawablesWithZLevel(Vec2F const& position) const {
  // Telemetry (static-handle idiom: registration/lookup once, then lock-free increments): "cached"
  // counts static parts served from the cache, "rebuilt" counts parts built (LIVE parts every call,
  // static parts inside rebuildStaticCache).
  //
  // REGISTERED HERE, ABOVE THE BRANCH, NOT AT THE INCREMENT SITE. These used to be block-scope statics
  // inside the caching arm, so with renderDrawableCache OFF they never registered at all and the keys
  // read ABSENT rather than ZERO -- which a consumer differencing two snapshots cannot tell apart from
  // "no such metric". It is the same bug class StarBackdropPass.hpp already names (block-scope statics
  // that never REGISTER in runtime-gated functions); these predate that lesson. Found by the lever
  // matrix, which could not compare the renderDrawableCache off leg to its baseline and correctly
  // VOIDed it rather than reporting a delta.
  //
  // This function is the single dispatcher -- per-part, rebuild and whole-entity cache all leave from
  // here -- so registering at the top makes both keys exist on every path. Registration is idempotent,
  // so the handles taken further down and in drawablesWithZLevelPerPart resolve to these same nodes.
  static auto s_cachedCounter = Telemetry::counter("render.drawable.parts.cached",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  static auto s_rebuiltCounter = Telemetry::counter("render.drawable.parts.rebuilt",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});

  auto configuration = Root::singleton().configuration();
  // Per-part cache wins over whole-entity when both are on (A/B sets exactly one).
  if (configuration->get("renderDrawableCachePerPart", false).toBool())
    return drawablesWithZLevelPerPart(position);
  if (!configuration->get("renderDrawableCache", false).toBool())
    return drawablesWithZLevelRebuild(position);
  bool shadow = configuration->get("renderDrawableCacheShadowCompare", false).toBool();

  size_t partCount = m_animatedParts.constParts().size();
  if (!partCount)
    return {};

  // Enumerate + stable-sort the active parts exactly as the rebuild does.
  // Enumerating also freshens every part (AnimatedPartSet resolves lazily and
  // can bump generation() mid-enumeration), but parts only settle the state
  // types they actually listen to (matching partStates).  A state type NO part
  // lists still feeds every part's build through drawableBuildContext's
  // <stateType_*> animation tags, and a pending setState/finishAnimations on
  // it (master mutation between update() and render) would otherwise not land
  // in generation() until the next update() -- a one-call stale serve of any
  // cached image resolving its tags.  So explicitly freshen every state type
  // too (forEachActiveState is the same lazy-resolve accessor update() uses;
  // read-only here), settling generation() fully BEFORE the cache is keyed
  // on it.
  int drawableCount = 0;
  auto parts = sortedActiveParts(drawableCount);
  m_animatedParts.forEachActiveState([](String const&, AnimatedPartSet::ActiveStateInformation const&) {});

  // The third key component covers the local transformation-group matrices,
  // which are excluded from m_renderVersion (see localTransformHash and the
  // note at the Local setters).  Computed here only -- after the flag check --
  // so the flag-off path stays untouched.
  uint64_t lth = localTransformHash();
  auto key = std::make_tuple(m_renderVersion, m_animatedParts.generation(), lth);
  if (!m_staticCacheValid || m_staticCacheKey != key) {
    // Re-key reason attribution (diagnostics): which key component moved --
    // renderVersion (0) vs generation (1) vs the local-transform hash (2).  A
    // cold/invalid cache counts toward all reasons.
    static auto s_rekeyVersionCounter = Telemetry::counter("render.drawable.cache.rekey.version",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
    static auto s_rekeyGenerationCounter = Telemetry::counter("render.drawable.cache.rekey.generation",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
    static auto s_rekeyLocalTransformCounter = Telemetry::counter("render.drawable.cache.rekey.localtransform",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
    if (!m_staticCacheValid || std::get<0>(m_staticCacheKey) != std::get<0>(key))
      s_rekeyVersionCounter.inc();
    if (!m_staticCacheValid || std::get<1>(m_staticCacheKey) != std::get<1>(key))
      s_rekeyGenerationCounter.inc();
    if (!m_staticCacheValid || std::get<2>(m_staticCacheKey) != lth)
      s_rekeyLocalTransformCounter.inc();
    rebuildStaticCache(parts, key);
  }

  // Assemble in the sorted part order: static parts are served from the cache
  // (zero-translate copies), LIVE parts are built fresh through the same
  // per-part helper the rebuild uses.  Drawable order is identical to the
  // rebuild by construction, so no merge re-sort is needed (and equal-zLevel
  // ordering is preserved exactly).
  List<pair<Drawable, float>> drawables;
  drawables.reserve(partCount + drawableCount);
  // Maps assembled drawable index -> source part name, for shadow-compare
  // diagnostics only (populated only while the shadow flag is on).
  List<pair<size_t, String const*>> partStarts;
  if (shadow)
    partStarts.reserve(parts.size());
  List<Directives> baseProcessingDirectives;
  HashMap<String, String> animationTags;
  bool contextBuilt = false;
  for (auto& entry : parts) {
    auto& partName = *get<1>(entry);
    if (shadow)
      partStarts.append({drawables.size(), &partName});
    if (auto cached = m_staticCache.ptr(partName)) {
      s_cachedCounter.inc();
      for (auto const& p : *cached)
        drawables.append(p);
    } else {
      if (!contextBuilt) {
        drawableBuildContext(baseProcessingDirectives, animationTags);
        contextBuilt = true;
      }
      s_rebuiltCounter.inc();
      appendPartDrawables(partName, *get<0>(entry), get<2>(entry), Vec2F(), baseProcessingDirectives, animationTags, drawables);
    }
  }

  // World translate applied live (everything above is at zero translate).
  //
  // Parity policy vs the rebuild: the rebuild folds the world translate into
  // the part matrix BEFORE Drawable::transform, this path adds it AFTER.
  // Float addition is non-associative, so for drawables entering the build
  // with a non-zero base position (m_partDrawables: Humanoid held items, Lua
  // animator.setPartDrawables) Drawable::position may differ from the rebuild
  // by ~1 ulp; the plain image path (base position zero) and every other
  // field, including the image matrix, are bitwise identical.  Anything
  // comparing the two paths (the DrawableCache parity test, Task 5's
  // shadowCompare) must compare position with a small ulp tolerance and all
  // other fields exactly.
  for (auto& p : drawables)
    p.first.translate(position);

  // Shadow-compare (runtime flag): verify this output against a forced full
  // rebuild for the same state.  Diagnostics only -- the cache-path output is
  // returned either way.
  if (shadow)
    shadowCompare(drawables, drawablesWithZLevelRebuild(position), partStarts);

  return drawables;
}

void NetworkedAnimator::rebuildStaticCache(List<tuple<AnimatedPartSet::ActivePartInformation const*, String const*, float>> const& parts,
    tuple<uint64_t, uint64_t, uint64_t> const& key) const {
  static auto s_rebuiltCounter = Telemetry::counter("render.drawable.parts.rebuilt",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  // rebuilt.rekey counts ONLY the static-part builds done here (re-key churn);
  // the live-part build site in drawablesWithZLevel increments plain rebuilt
  // only, so rebuilt - rebuilt.rekey = genuinely-live part builds.
  static auto s_rebuiltRekeyCounter = Telemetry::counter("render.drawable.parts.rebuilt.rekey",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  m_staticCache.clear();
  List<Directives> baseProcessingDirectives;
  HashMap<String, String> animationTags;
  bool contextBuilt = false;
  for (auto& entry : parts) {
    auto& partName = *get<1>(entry);
    if (!partIsStaticCacheable(partName))
      continue;
    if (!contextBuilt) {
      drawableBuildContext(baseProcessingDirectives, animationTags);
      contextBuilt = true;
    }
    List<pair<Drawable, float>> partDrawables;
    s_rebuiltCounter.inc();
    s_rebuiltRekeyCounter.inc();
    appendPartDrawables(partName, *get<0>(entry), get<2>(entry), Vec2F(), baseProcessingDirectives, animationTags, partDrawables);
    m_staticCache.set(partName, std::move(partDrawables));
  }
  m_staticCacheKey = key;
  m_staticCacheValid = true;
}

List<pair<Drawable, float>> NetworkedAnimator::drawablesWithZLevelPerPart(Vec2F const& position) const {
  auto configuration = Root::singleton().configuration();
  bool shadow = configuration->get("renderDrawableCacheShadowCompare", false).toBool();

  size_t partCount = m_animatedParts.constParts().size();
  if (!partCount)
    return {};

  // Freshen all parts (enumerate + stable-sort, settling each part's
  // partGeneration) and settle every state type so parts that DO rebuild this
  // call resolve current animation tags.  partGeneration alone tracks only a
  // part's OWN resolved state, so cross-state-type tag dependencies are tracked
  // separately in the cache entry: when a part resolves ANOTHER state type's
  // built-in <T_state>/<T_frame>/<T_frameIndex> tag, appendPartDrawables records
  // a PRECISE per-state-type dep (stateTypeDeps, checked against that state
  // type's generation); when it resolves any custom animationTags key (whose
  // first-definer owner can shift), it records the conservative stateTypesEpoch
  // dep (dependsAllStateTypes).  Either firing invalidates the entry, so a static
  // part stays correct when a foreign state type changes (proven by the
  // DrawableCache.PerPart* invalidation tests).
  int drawableCount = 0;
  auto parts = sortedActiveParts(drawableCount);
  m_animatedParts.forEachActiveState([](String const&, AnimatedPartSet::ActiveStateInformation const&) {});

  // renderVersion and localTransformHash are GLOBAL key components (a global
  // tag/directive/effect or local-transform change re-validates every part);
  // partGeneration is the per-part component that lets a looping part rebuild
  // alone.
  uint64_t rv = m_renderVersion;
  uint64_t lth = localTransformHash();

  static auto s_cachedCounter = Telemetry::counter("render.drawable.parts.cached",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  static auto s_rebuiltCounter = Telemetry::counter("render.drawable.parts.rebuilt",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  static auto s_rebuiltRekeyCounter = Telemetry::counter("render.drawable.parts.rebuilt.rekey",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});

  List<pair<Drawable, float>> drawables;
  drawables.reserve(partCount + drawableCount);
  List<pair<size_t, String const*>> partStarts;
  if (shadow)
    partStarts.reserve(parts.size());
  List<Directives> baseProcessingDirectives;
  HashMap<String, String> animationTags;
  TagDeps tagDeps;
  bool contextBuilt = false;
  auto ensureContext = [&]() {
    if (!contextBuilt) {
      drawableBuildContext(baseProcessingDirectives, animationTags, &tagDeps);
      contextBuilt = true;
    }
  };

  for (auto& entry : parts) {
    auto& partName = *get<1>(entry);
    if (shadow)
      partStarts.append({drawables.size(), &partName});

    if (partIsStaticCacheable(partName)) {
      uint64_t pg = m_animatedParts.partGeneration(partName);
      auto cached = m_staticCachePerPart.ptr(partName);
      bool depsFresh = cached
          && cached->renderVersion == rv && cached->partGeneration == pg
          && cached->localTransformHash == lth
          && (!cached->dependsAllStateTypes || cached->stateTypesEpoch == m_animatedParts.stateTypesEpoch());
      if (depsFresh)
        for (auto const& dep : cached->stateTypeDeps)
          if (m_animatedParts.stateTypeGeneration(dep.first) != dep.second) { depsFresh = false; break; }
      if (depsFresh) {
        s_cachedCounter.inc();
        for (auto const& p : cached->drawables) drawables.append(p);
        continue;
      }
      // MISS: rebuild ONLY this part and store its entry under its own key.
      // rebuilt.rekey counts per-part static rebuilds (re-key churn), exactly as
      // in rebuildStaticCache, so rebuilt - rebuilt.rekey stays = genuinely-live builds.
      ensureContext();
      List<pair<Drawable, float>> partDrawables;
      s_rebuiltCounter.inc();
      s_rebuiltRekeyCounter.inc();
      Set<String> consumed;
      bool consumedCustom = false;
      appendPartDrawables(partName, *get<0>(entry), get<2>(entry), Vec2F(),
          baseProcessingDirectives, animationTags, partDrawables, &tagDeps, &consumed, &consumedCustom);
      for (auto const& p : partDrawables)
        drawables.append(p);
      List<pair<String, uint64_t>> deps;
      deps.reserve(consumed.size());
      for (auto const& st : consumed) deps.append({st, m_animatedParts.stateTypeGeneration(st)});
      m_staticCachePerPart.set(partName, StaticPartCacheEntry{std::move(partDrawables), rv, pg, lth,
          std::move(deps), consumedCustom, m_animatedParts.stateTypesEpoch()});
    } else {
      // LIVE part: never cached, always built fresh (same as the whole-entity path).
      ensureContext();
      s_rebuiltCounter.inc();
      appendPartDrawables(partName, *get<0>(entry), get<2>(entry), Vec2F(),
          baseProcessingDirectives, animationTags, drawables);
    }
  }

  // World translate applied live (everything above is at zero translate) -- same
  // parity policy as the whole-entity path (see the comment block below).
  for (auto& p : drawables)
    p.first.translate(position);

  if (shadow)
    shadowCompare(drawables, drawablesWithZLevelRebuild(position), partStarts);

  return drawables;
}

// Position parity policy for the shadow compare, shared with the DrawableCache
// parity test (see the policy note in drawablesWithZLevel and the full
// derivation in source/test/drawable_cache_test.cpp): the cache path applies
// the world translate AFTER the part matrix, the rebuild folds it in BEFORE,
// so drawables entering the build with a non-zero base position
// (m_partDrawables: Humanoid held items, Lua animator.setPartDrawables) may
// differ by ~1 ulp per position component.  Position is therefore compared
// with a tight ulp bound; every other field must be EXACT.  Real cache bugs
// (stale offset, missed invalidation, wrong translate) are orders of magnitude
// larger than 4 ulps.
namespace {
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

  int64_t const ShadowComparePositionMaxUlps = 4;
}

void NetworkedAnimator::shadowCompare(List<pair<Drawable, float>> const& cached,
    List<pair<Drawable, float>> const& rebuilt,
    List<pair<size_t, String const*>> const& partStarts) const {
  static auto s_mismatchCounter = Telemetry::counter("render.drawable.cache.shadowMismatch",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});

  auto partNameAt = [&](size_t index) -> String {
    String const* name = nullptr;
    for (auto const& start : partStarts) {
      if (start.first > index)
        break;
      name = start.second;
    }
    return name ? *name : String("<unknown>");
  };

  // The warn is rate-limited to once per animator, NOT per frame (the mismatch
  // counter still counts every mismatching drawable).  Always warn, never
  // error: the test harness installs a strict ErrorLogSink that fails any test
  // logging an Error.
  auto report = [&](size_t index, char const* field) {
    s_mismatchCounter.inc();
    if (!m_shadowMismatchWarned) {
      m_shadowMismatchWarned = true;
      Logger::warn("NetworkedAnimator drawable cache shadow mismatch: part '{}' drawable {} field '{}'",
          partNameAt(index), index, field);
    }
  };

  if (cached.size() != rebuilt.size()) {
    s_mismatchCounter.inc();
    if (!m_shadowMismatchWarned) {
      m_shadowMismatchWarned = true;
      Logger::warn("NetworkedAnimator drawable cache shadow mismatch: {} cached vs {} rebuilt drawables", cached.size(), rebuilt.size());
    }
    return;
  }

  for (size_t i = 0; i < cached.size(); ++i) {
    auto const& c = cached[i].first;
    auto const& r = rebuilt[i].first;
    if (cached[i].second != rebuilt[i].second) {
      report(i, "zLevel");
      continue;
    }
    if (c.isImage() != r.isImage() || c.isLine() != r.isLine() || c.isPoly() != r.isPoly()) {
      report(i, "part type");
      continue;
    }
    if (c.isImage()) {
      if (!(c.imagePart().image == r.imagePart().image)) {
        report(i, "image");
        continue;
      }
      // The translation column of the image matrix cancels exactly in both
      // translate orders, so this compare is exact.
      if (!(c.imagePart().transformation == r.imagePart().transformation)) {
        report(i, "image transformation");
        continue;
      }
    }
    // Line/poly vertex data is bitwise identical between the paths by
    // construction (Drawable::transform strips the translation before
    // touching vertices; it lands entirely in position), so position, color
    // and fullbright are the remaining comparable fields.
    if (ulpDistance(c.position[0], r.position[0]) > ShadowComparePositionMaxUlps
        || ulpDistance(c.position[1], r.position[1]) > ShadowComparePositionMaxUlps) {
      report(i, "position");
      continue;
    }
    if (!(c.color == r.color)) {
      report(i, "color");
      continue;
    }
    if (c.fullbright != r.fullbright)
      report(i, "fullbright");
  }
}

List<pair<Drawable, float>> NetworkedAnimator::drawablesWithZLevelRebuild(Vec2F const& position) const {
  size_t partCount = m_animatedParts.constParts().size();
  if (!partCount)
    return {};

  List<Directives> baseProcessingDirectives;
  HashMap<String, String> animationTags;
  drawableBuildContext(baseProcessingDirectives, animationTags);

  int drawableCount = 0;
  auto parts = sortedActiveParts(drawableCount);

  List<pair<Drawable, float>> drawables;
  drawables.reserve(partCount + drawableCount);
  for (auto& entry : parts)
    appendPartDrawables(*get<1>(entry), *get<0>(entry), get<2>(entry), position, baseProcessingDirectives, animationTags, drawables);

  return drawables;
}

void NetworkedAnimator::drawableBuildContext(List<Directives>& baseProcessingDirectives, HashMap<String, String>& animationTags, TagDeps* tagDeps) const {
  baseProcessingDirectives.append(m_processingDirectives.get());
  for (auto& pair : m_effects) {
    auto const& effectState = pair.second;

    if (effectState.enabled.get()) {
      auto const& effect = m_effects.get(pair.first);
      if (effect.type == "flash") {
        if (effectState.timer > effect.time / 2) {
          baseProcessingDirectives.append(effect.directives);
        }
      } else if (effect.type == "directive") {
        baseProcessingDirectives.append(effect.directives);
      } else {
        throw NetworkedAnimatorException(strf("No such NetworkedAnimator effect type '{}'", effect.type));
      }
    }
  }
  animationTags = m_localTags;
  if (version() > 0) {
    animationTags.set("relativePath", m_relativePath);
    for (auto& stateTypeName : m_animatedParts.stateTypes()) {
      auto& activeState = m_animatedParts.activeState(stateTypeName);
      unsigned stateFrame = activeState.frame;
      Maybe<unsigned> frame;
      String frameStr;
      String frameIndexStr;

      frame = stateFrame;
      frameStr = static_cast<String>(toString(stateFrame + 1));
      frameIndexStr = static_cast<String>(toString(stateFrame));
      if (frame) {
        animationTags.set(stateTypeName + "_frame", frameStr);
        animationTags.set(stateTypeName + "_frameIndex", frameIndexStr);
        if (tagDeps) {
          tagDeps->stateTagOwner[stateTypeName + "_frame"] = stateTypeName;
          tagDeps->stateTagOwner[stateTypeName + "_frameIndex"] = stateTypeName;
        }
      }
      animationTags.set(stateTypeName + "_state", activeState.stateName);
      if (tagDeps) tagDeps->stateTagOwner[stateTypeName + "_state"] = stateTypeName;

      if (auto p = activeState.properties.ptr("animationTags")) {
        for (auto tag : p->iterateObject())
          if (!animationTags.contains(tag.first)) {
            animationTags.set(tag.first, tag.second.toString());
            if (tagDeps) tagDeps->customTags.insert(tag.first);
          }
      }
      // Scan ALL states (including inactive ones) for potential custom tags.
      // A custom tag defined only in an inactive state is absent from animationTags
      // now, but a part whose image references it would silently consume a stale
      // "default" and never call recordDep.  Collecting every possible custom tag
      // key lets recordDep correctly mark any consumer as epoch-dependent.
      //
      // The scan must cover EVERY source that freshenActiveState merges into
      // activeState.properties (see StarAnimatedPartSet.cpp): the state type's
      // stateTypeProperties, each state's stateProperties, and each state's
      // stateFrameProperties (per-frame, an object of arrays).  The merge is a
      // flat overwrite, so a key present only at one level (e.g. a frame-varying
      // tag, or a state-type-level tag hidden by a frame override in the active
      // state) would otherwise be missed -> stale serve.  Over-collecting keys is
      // safe (conservative epoch dep); under-collecting is a stale-serve bug.
      if (tagDeps) {
        auto collect = [&](Json const& animTags) {
          if (animTags.isType(Json::Type::Object))
            for (auto const& kv : animTags.iterateObject())
              tagDeps->customTags.insert(kv.first);
        };
        collect(m_animatedParts.stateTypeProperties(stateTypeName).maybe("animationTags").value(Json()));
        for (auto const& stateName : m_animatedParts.states(stateTypeName)) {
          auto const& state = m_animatedParts.getState(stateTypeName, stateName);
          collect(state.stateProperties.maybe("animationTags").value(Json()));
          // stateFrameProperties["animationTags"] is an array of per-frame objects.
          if (auto p = state.stateFrameProperties.ptr("animationTags"))
            if (p->isType(Json::Type::Array))
              for (auto const& frameTags : p->iterateArray())
                collect(frameTags);
        }
      }
    }
  }
}

List<tuple<AnimatedPartSet::ActivePartInformation const*, String const*, float>> NetworkedAnimator::sortedActiveParts(int& drawableCount) const {
  List<tuple<AnimatedPartSet::ActivePartInformation const*, String const*, float>> parts;
  parts.reserve(m_animatedParts.constParts().size());
  m_animatedParts.forEachActivePart([&](String const& partName, AnimatedPartSet::ActivePartInformation const& activePart) {
    Maybe<float> maybeZLevel;
    if (m_flipped.get()) {
      if (auto maybeFlipped = activePart.properties.value("flippedZLevel").optFloat())
        maybeZLevel = *maybeFlipped;
    }
    if (!maybeZLevel)
      maybeZLevel = activePart.properties.value("zLevel").optFloat();

    if (auto drawables = m_partDrawables.contains(partName))
      drawableCount += m_partDrawables.get(partName).size();
    parts.append(make_tuple(&activePart, &partName, maybeZLevel.value(0.0f)));
  });

  sort(parts, [](auto const& a, auto const& b) { return get<2>(a) < get<2>(b); });
  return parts;
}

// The per-part drawable build, extracted VERBATIM from the old
// drawablesWithZLevel loop body.  Single source of truth: the rebuild path,
// the static-cache build, and the live-part serve path all call this, so
// cached and live parts are built identically (output parity by construction).
void NetworkedAnimator::appendPartDrawables(String const& partName, AnimatedPartSet::ActivePartInformation const& activePart,
    float zLevel, Vec2F const& translate, List<Directives>& baseProcessingDirectives,
    HashMap<String, String> const& animationTags, List<pair<Drawable, float>>& drawables,
    TagDeps const* tagDeps, Set<String>* consumedStateTypes, bool* consumedCustomTag) const {
  // Make sure we don't copy the original image
  String fallback = "";
  Json jImage = activePart.properties.value("image", {});
  if (version() > 0 && m_flipped.get()) {
    if (auto maybeFlipped = activePart.properties.value("flippedImage").optString())
      jImage = *maybeFlipped;
  }

  String const& image = jImage.isType(Json::Type::String) ? *jImage.stringPtr() : fallback;

  bool centered = activePart.properties.value("centered").optBool().value(true);
  bool fullbright = activePart.properties.value("fullbright").optBool().value(false);

  size_t originalDirectivesSize = baseProcessingDirectives.size();

  auto const& partTags = m_partTags.get(partName);

  auto recordDep = [&](StringView tag) {
    if (!tagDeps) return;
    String tagStr(tag);
    if (auto owner = tagDeps->stateTagOwner.ptr(tagStr)) {
      if (consumedStateTypes) consumedStateTypes->insert(*owner);
    } else if (tagDeps->customTags.contains(tagStr)) {
      if (consumedCustomTag) *consumedCustomTag = true;
    }
  };

  if (auto directives = activePart.properties.value("processingDirectives").optString()) {
    if (version() > 0){
      directives = directives->maybeLookupTagsView([&](StringView tag) -> StringView {
        // recordDep fires for EVERY resolved tag, before the lookups: a built-in
        // <T_*> tag records its precise per-state-type dep, a custom tag (active,
        // inactive, or shadowed by a higher-precedence source) marks the epoch
        // dep, and a genuine global/part-only tag is a self-gated no-op.  This
        // catches a global/part default shadowed by a per-state custom key.
        recordDep(tag);
        if (auto p = animationTags.ptr(tag)) {
          return StringView(*p);
        } else if (auto p = partTags.ptr(tag)) {
          return StringView(*p);
        } else if (auto p = m_globalTags.ptr(tag)) {
          return StringView(*p);
        }
        return StringView("default");
      });
    }
    baseProcessingDirectives.append(*directives);
  }

  Maybe<unsigned> frame;
  String frameStr;
  String frameIndexStr;
  if (activePart.activeState) {
    unsigned stateFrame = activePart.activeState->frame;
    frame = stateFrame;
    frameStr = static_cast<String>(toString(stateFrame + 1));
    frameIndexStr = static_cast<String>(toString(stateFrame));

    if (auto directives = activePart.activeState->properties.value("processingDirectives").optString()) {
      if (version() > 0){
        directives = directives->maybeLookupTagsView([&](StringView tag) -> StringView {
          // recordDep before the lookups -- see the part-level lambda above.
          recordDep(tag);
          if (auto p = animationTags.ptr(tag)) {
            return StringView(*p);
          } else if (auto p = partTags.ptr(tag)) {
            return StringView(*p);
          } else if (auto p = m_globalTags.ptr(tag)) {
            return StringView(*p);
          }
          return StringView("default");
        });
      }
      baseProcessingDirectives.append(*directives);
    }
  }

  Maybe<String> processedImage = image.maybeLookupTagsView([&](StringView tag) -> StringView {
    // recordDep before the lookups -- see the part-level lambda above.  The literal
    // <frame>/<frameIndex> special-cases are self-gated no-ops here (not built-in
    // <T_*> keys nor custom keys); the per-part <T_frame>/<T_frameIndex> deps come
    // from the prefixed tags in animationTags/stateTagOwner.
    recordDep(tag);
    if (tag == "frame") {
      if (frame)
        return frameStr;
    } else if (tag == "frameIndex") {
      if (frame)
        return frameIndexStr;
    } else if (auto p = animationTags.ptr(tag)) {
      return StringView(*p);
    } else if (auto p = partTags.ptr(tag)) {
      return StringView(*p);
    } else if (auto p = m_globalTags.ptr(tag)) {
      return StringView(*p);
    }
    return StringView("default");
  });
  String const& usedImage = processedImage ? processedImage.get() : image;

  auto transformation = globalTransformation() * partTransformation(partName);
  transformation.translate(translate);

  if (!usedImage.empty() && usedImage[0] != ':' && usedImage[0] != '?') {
    size_t hash = hashOf(usedImage);
    auto find = m_cachedPartDrawables.find(partName);
    if (find == m_cachedPartDrawables.end() || find->second.first != hash) {
      String relativeImage;
      if (usedImage[0] != '/')
        relativeImage = AssetPath::relativeTo(m_relativePath, usedImage);

      Drawable drawable = Drawable::makeImage(!relativeImage.empty() ? relativeImage : usedImage, 1.0f / TilePixels, centered, Vec2F());
      if (find == m_cachedPartDrawables.end())
        find = m_cachedPartDrawables.emplace(partName, std::pair{ hash, std::move(drawable) }).first;
      else {
        find->second.first = hash;
        find->second.second = std::move(drawable);
      }
    }

    Drawable drawable = find->second.second;
    auto& imagePart = drawable.imagePart();
    for (Directives const& directives : baseProcessingDirectives)
      imagePart.addDirectives(directives, centered);
    drawable.fullbright = fullbright;
    drawable.transform(transformation);
    drawables.append({std::move(drawable), zLevel});
  }

  if (m_partDrawables.contains(partName)) {
    auto partDrawables = m_partDrawables.get(partName);
    Drawable::transformAll(partDrawables, transformation);
    for (auto drawable : partDrawables) {
    drawables.append({drawable, zLevel});
    }
  }

  baseProcessingDirectives.resize(originalDirectivesSize);
}

List<LightSource> NetworkedAnimator::lightSources(Vec2F const& translate) const {
  List<LightSource> lightSources;
  for (auto const& pair : m_lights) {
    if (!pair.second.active.get())
      continue;

    Vec2F position = {pair.second.xPosition.get(), pair.second.yPosition.get()};
    float pointAngle = constrainAngle(pair.second.pointAngle.get());
    Mat3F transformation = Mat3F::identity();
    if (pair.second.anchorPart)
      transformation = partTransformation(*pair.second.anchorPart);
    transformation = groupTransformation(pair.second.transformationGroups) * transformation;
    position = transformation.transformVec2(position);
    pointAngle = transformation.transformAngle(pointAngle);
    if (pair.second.rotationGroup) {
      auto const& rg = m_rotationGroups.get(*pair.second.rotationGroup);
      position = (position - pair.second.rotationCenter.value(rg.rotationCenter)).rotate(rg.currentAngle)
          + pair.second.rotationCenter.value(rg.rotationCenter);
      pointAngle += rg.currentAngle;
    }
    position = globalTransformation().transformVec2(position);
    if (m_flipped.get()) {
      if (pointAngle > 0)
        pointAngle = Constants::pi / 2 + constrainAngle(Constants::pi / 2 - pointAngle);
      else
        pointAngle = -Constants::pi / 2 - constrainAngle(pointAngle + Constants::pi / 2);
    }

    Color color = pair.second.color.get();
    if (pair.second.flicker)
      color.setValue(clamp(color.value() * pair.second.flicker->value(SinWeightOperator<float>()), 0.0f, 1.0f));

    lightSources.append(LightSource{
      position + translate,
      color.toRgbF(),
      pair.second.pointLight ? LightType::Point : LightType::Spread,
      pair.second.pointBeam,
      pointAngle,
      pair.second.beamAmbience
    });
  }
  return lightSources;
}

void NetworkedAnimator::update(float dt, DynamicTarget* dynamicTarget) {
  dt *= m_animationRate.get();

  m_animatedParts.update(dt);

  m_animatedParts.forEachActiveState([&](String const& stateTypeName, AnimatedPartSet::ActiveStateInformation const& activeState) {
      if (dynamicTarget) {
        dynamicTarget->clearFinishedAudio();

        Json persistentSound = activeState.properties.value("persistentSound", "");
        String persistentSoundFile;

        if (persistentSound.isType(Json::Type::String))
          persistentSoundFile = persistentSound.toString();
        else if (persistentSound.isType(Json::Type::Array))
          persistentSoundFile = Random::randValueFrom(persistentSound.toArray(), "").toString();

        if (!persistentSoundFile.empty())
          persistentSoundFile = AssetPath::relativeTo(m_relativePath, persistentSoundFile);

        auto& activePersistentSound = dynamicTarget->statePersistentSounds[stateTypeName];

        bool changedPersistentSound = persistentSound != activePersistentSound.sound;
        if (changedPersistentSound || !activePersistentSound.audio) {
          activePersistentSound.sound = std::move(persistentSound);
          if (activePersistentSound.audio)
            activePersistentSound.audio->stop(activePersistentSound.stopRampTime);

          if (!persistentSoundFile.empty()) {
            activePersistentSound.audio = make_shared<AudioInstance>(*Root::singleton().assets()->audio(persistentSoundFile));
            activePersistentSound.audio->setRangeMultiplier(activeState.properties.value("persistentSoundRangeMultiplier", 1.0f).toFloat());
            activePersistentSound.audio->setLoops(-1);
            activePersistentSound.audio->setPosition(globalTransformation().transformVec2(Vec2F()));
            activePersistentSound.stopRampTime = activeState.properties.value("persistentSoundStopTime", 0.0f).toFloat();
            dynamicTarget->pendingAudios.append(activePersistentSound.audio);
          } else {
            dynamicTarget->statePersistentSounds.remove(stateTypeName);
          }
        }

        Json immediateSound = activeState.properties.value("immediateSound", "");
        String immediateSoundFile = "";

        if (immediateSound.isType(Json::Type::String))
          immediateSoundFile = immediateSound.toString();
        else if (immediateSound.isType(Json::Type::Array))
          immediateSoundFile = Random::randValueFrom(immediateSound.toArray(), "").toString();

        if (!immediateSoundFile.empty())
          immediateSoundFile = AssetPath::relativeTo(m_relativePath, immediateSoundFile);

        auto& activeImmediateSound = dynamicTarget->stateImmediateSounds[stateTypeName];

        bool changedImmediateSound = immediateSound != activeImmediateSound.sound;
        if (changedImmediateSound) {
          activeImmediateSound.sound = std::move(immediateSound);
          if (!immediateSoundFile.empty()) {
            activeImmediateSound.audio = make_shared<AudioInstance>(*Root::singleton().assets()->audio(immediateSoundFile));
            activeImmediateSound.audio->setRangeMultiplier(activeState.properties.value("immediateSoundRangeMultiplier", 1.0f).toFloat());
            activeImmediateSound.audio->setPosition(globalTransformation().transformVec2(Vec2F()));
            dynamicTarget->pendingAudios.append(activeImmediateSound.audio);
          }
        }
      }

      if (auto lightsOn = activeState.properties.ptr("lightsOn")) {
        for (auto const& name : lightsOn->iterateArray())
          m_lights.get(name.toString()).active.set(true);
      }
      if (auto lightsOff = activeState.properties.ptr("lightsOff")) {
        for (auto const& name : lightsOff->iterateArray())
          m_lights.get(name.toString()).active.set(false);
      }

      if (auto particleEmittersOn = activeState.properties.ptr("particleEmittersOn")) {
        for (auto const& name : particleEmittersOn->iterateArray())
          m_particleEmitters.get(name.toString()).active.set(true);
      }
      if (auto particleEmittersOff = activeState.properties.ptr("particleEmittersOff")) {
        for (auto const& name : particleEmittersOff->iterateArray())
          m_particleEmitters.get(name.toString()).active.set(false);
      }


    });
  if (version() > 0) {
    auto processTransforms = [](Mat3F mat, JsonArray transforms, JsonObject properties) -> Mat3F {
      for (auto const& v : transforms) {
        auto action = v.getString(0);
        if (action == "reset") {
          mat = Mat3F::identity();
        } else if (action == "translate") {
          mat.translate(jsonToVec2F(v.getArray(1)));
        } else if (action == "rotate") {
          mat.rotate(v.getFloat(1), jsonToVec2F(v.getArray(2, properties.maybe("rotationCenter").value(JsonArray({0,0})).toArray())));
        } else if (action == "rotateDegrees") { // because radians are fucking annoying
          mat.rotate(v.getFloat(1) * Star::Constants::pi / 180, jsonToVec2F(v.getArray(2, properties.maybe("rotationCenter").value(JsonArray({0,0})).toArray())));
        } else if (action == "scale") {
          mat.scale(jsonToVec2F(v.getArray(1)), jsonToVec2F(v.getArray(2, properties.maybe("scalingCenter").value(JsonArray({0,0})).toArray())));
        } else if (action == "transform") {
          mat = Mat3F(v.getFloat(1), v.getFloat(2), v.getFloat(3), v.getFloat(4), v.getFloat(5), v.getFloat(6), 0, 0, 1) * mat;
        }
      }
      return mat;
    };
    for (auto& pair : m_transformationGroups) {
      // L-ANIM-ITER: direct-iterate state types (priority order) instead of a
      // per-group stateTypes() keys() alloc + per-iteration activeState(name) hash
      // lookup. Byte-identical: same freshen set/order, same first-match break.
      m_animatedParts.forEachStateTypeUntil([&](String const&, AnimatedPartSet::StateType const& stateType) -> bool {
        auto const& activeState = stateType.activeState;
        if (auto transforms = activeState.properties.ptr(pair.first)) {
          auto mat = processTransforms(pair.second.animationAffineTransform(), transforms->toArray(), activeState.properties);
          if (pair.second.interpolated) {
            if (auto nextTransforms = activeState.nextProperties.ptr(pair.first)) {
              auto nextMat = processTransforms(pair.second.animationAffineTransform(), nextTransforms->toArray(), activeState.nextProperties);
              pair.second.setAnimationAffineTransform(mat, nextMat, activeState.frameProgress);
            } else {
              pair.second.setAnimationAffineTransform(mat);
            }
          } else {
            pair.second.setAnimationAffineTransform(mat);
          }
          return true;//we got one with the highest priority so stop the scan
        }
        return false;
      });
    }
  }

  for (auto& pair : m_rotationGroups) {
    auto& rotationGroup = pair.second;
    if (rotationGroup.angularVelocity == 0.0f) {
      // Value-diffed bump: parts on a 0-angularVelocity rotation group are
      // STATIC-cacheable.  rotateGroup / the net funnel bump renderVersion
      // when targetAngle changes, but the drawable-visible currentAngle only
      // snaps HERE -- a drawables() call between the bump and this snap would
      // otherwise re-key the static cache on the stale angle.
      float targetAngle = rotationGroup.targetAngle.get();
      if (rotationGroup.currentAngle != targetAngle) {
        rotationGroup.currentAngle = targetAngle;
        bumpRenderVersion();
      }
    } else {
      rotationGroup.currentAngle = approachAngle(rotationGroup.targetAngle.get(), rotationGroup.currentAngle, rotationGroup.angularVelocity * dt);
    }
  }

  if (dynamicTarget) {
    auto addParticles = [this, dynamicTarget](ParticleEmitter::ParticleConfig const& config, RectF const& offsetRegion, Mat3F const& transformation) {
      for (unsigned i = 0; i < config.count; ++i) {
        Particle particle = config.creator();
        particle.position += config.offset;

        if (!offsetRegion.isNull()) {
          particle.position[0] += Random::randf() * offsetRegion.width() + offsetRegion.xMin();
          particle.position[1] += Random::randf() * offsetRegion.height() + offsetRegion.yMin();
        }

        float speed = particle.velocity.magnitude();
        particle.velocity = Vec2F::withAngle(transformation.transformAngle(particle.velocity.angle())) * speed;
        particle.position = transformation.transformVec2(particle.position);
        particle.rotation = transformation.transformAngle(particle.rotation);

        particle.size *= m_zoom.get();
        if (config.flip)
          particle.flip = !particle.flip;

        if (transformation.determinant() < 0) {
          particle.flip = !particle.flip;
          particle.rotation += Constants::pi;
        }

        dynamicTarget->pendingParticles.append(std::move(particle));
      }
    };

    for (auto& pair : m_particleEmitters) {
      Mat3F transformation = Mat3F::identity();
      if (pair.second.anchorPart)
        transformation = partTransformation(*pair.second.anchorPart);
      transformation = groupTransformation(pair.second.transformationGroups) * transformation;

      if (pair.second.rotationGroup) {
        auto const& rg = m_rotationGroups.get(*pair.second.rotationGroup);
        Vec2F rotationCenter = pair.second.rotationCenter.value(rg.rotationCenter);
        transformation = Mat3F::rotation(rg.currentAngle, rotationCenter) * transformation;
      }

      transformation = globalTransformation() * transformation;

      // assume we emit no particles
      unsigned numEmissionCycles = 0;

      if (pair.second.active.get()) {
        pair.second.timer = min(pair.second.timer, 1.0f / (pair.second.emissionRate.get() + pair.second.emissionRateVariance));
        if (pair.second.timer <= 0.0f) {
          // timer causes us to emit one set
          ++numEmissionCycles;
          pair.second.timer = 1.0f / (pair.second.emissionRate.get() + Random::randf(-pair.second.emissionRateVariance, pair.second.emissionRateVariance));
        } else {
          pair.second.timer -= dt;
        }
      }

      auto bursts = pair.second.burstEvent.pullOccurrences();
      for (uint64_t i = 0; i < bursts; ++i)
        numEmissionCycles += pair.second.burstCount.get();

      if (numEmissionCycles > 0) {
        RectF rect = pair.second.offsetRegion.get();
        unsigned numToSelect = pair.second.randomSelectCount.get();

        for (unsigned i = 0; i < numEmissionCycles; ++i) {
          if (numToSelect >= pair.second.particleList.size()) {
            for (auto const& particleConfig : pair.second.particleList)
              addParticles(particleConfig, rect, transformation);
          } else {
            List<ParticleEmitter::ParticleConfig> shuffledList = pair.second.particleList;
            Random::shuffle(shuffledList);

            for (unsigned i = 0; i < numToSelect; ++i)
              addParticles(shuffledList.at(i), rect, transformation);
          }
        }
      }
    }

    for (auto& pair : m_sounds) {
      auto const& soundName = pair.first;
      auto& soundEntry = pair.second;

      for (auto signal : soundEntry.signals.receive()) {
        if (signal == SoundSignal::StopAll) {
          for (auto& sound : take(dynamicTarget->independentSounds[soundName]))
            sound->stop(soundEntry.volumeRampTime.get());
        } else if (signal == SoundSignal::Play) {
          String soundFile = Random::randValueFrom(soundEntry.soundPool.get());
          if (!soundFile.empty()) {
            auto sound = make_shared<AudioInstance>(*Root::singleton().assets()->audio(soundFile));
            sound->setRangeMultiplier(soundEntry.rangeMultiplier);
            sound->setLoops(soundEntry.loops.get());
            sound->setPosition(globalTransformation().transformVec2(Vec2F(soundEntry.xPosition.get(), soundEntry.yPosition.get())));
            sound->setVolume(soundEntry.volumeTarget.get(), soundEntry.volumeRampTime.get());
            sound->setPitchMultiplier(soundEntry.pitchMultiplierTarget.get(), soundEntry.pitchMultiplierRampTime.get());
            dynamicTarget->independentSounds[soundName].append(sound);
            dynamicTarget->pendingAudios.append(std::move(sound));
          }
        }
      }

      // Update all still active independent sounds position, volume, and speed
      for (auto const& activeIndependentSound : dynamicTarget->independentSounds.value(soundName)) {
        if (auto basePosition = dynamicTarget->currentAudioBasePositions.ptr(activeIndependentSound))
          *basePosition = globalTransformation().transformVec2(Vec2F(soundEntry.xPosition.get(), soundEntry.yPosition.get()));
        activeIndependentSound->setVolume(soundEntry.volumeTarget.get(), soundEntry.volumeRampTime.get());
        activeIndependentSound->setPitchMultiplier(soundEntry.pitchMultiplierTarget.get(), soundEntry.pitchMultiplierRampTime.get());
      }
    }
  }

  for (auto& pair : m_lights) {
    if (pair.second.flicker)
      pair.second.flicker->update(dt);
  }

  for (auto& pair : m_effects) {
    if (pair.second.enabled.get()) {
      auto& effect = pair.second;
      if (effect.timer <= 0.0f)
        effect.timer = effect.time;
      else
        effect.timer -= dt;
    }
  }
}

bool NetworkedAnimator::hasActiveAnimationWork() const {
  // Any active animation state still advancing keeps the master updating:
  //  - Loop never settles; Transition auto-advances the NETTED state index once
  //    its cycle completes (the master must run update() to drive that change);
  //  - an End state is still animating until its timer reaches its cycle.
  // L-ANIM-WAKE: direct-iterate state types instead of a per-call stateTypes()
  // keys() alloc + activeState(name) (outer hash get) + getState(name,...) (outer
  // hash get again). Conservative variant -- still resolves the State via the same
  // states.get(active.stateName) as getState(), so it is byte-identical; only the
  // redundant outer m_stateTypes lookups and the keys() allocation are removed.
  // Run once per awake animated object every step (Object::nextEngineWakeStep).
  bool stillAnimating = false;
  m_animatedParts.forEachStateTypeUntil([&](String const&, AnimatedPartSet::StateType const& stateType) -> bool {
    auto const& active = stateType.activeState;
    auto const& state = *stateType.states.get(active.stateName);
    if (state.animationMode != AnimatedPartSet::End || active.timer < state.cycle) {
      stillAnimating = true;
      return true;
    }
    return false;
  });
  if (stillAnimating)
    return true;

  // A rotation group still approaching its target (or pending the one-shot snap of
  // a zero-angularVelocity group) advances currentAngle on the next update().
  for (auto const& pair : m_rotationGroups) {
    if (pair.second.currentAngle != pair.second.targetAngle.get())
      return true;
  }

  return false;
}

void NetworkedAnimator::finishAnimations() {
  m_animatedParts.finishAnimations();
}

Mat3F NetworkedAnimator::TransformationGroup::affineTransform() const {
  return Mat3F(
      xScale.get() * cos(xShear.get()), xScale.get() * sin(xShear.get()), xTranslation.get(),
      yScale.get() * sin(yShear.get()), yScale.get() * cos(yShear.get()), yTranslation.get(),
      0, 0, 1
    );
}

void NetworkedAnimator::TransformationGroup::setAffineTransform(Mat3F const& matrix) {
  xTranslation.set(matrix[0][2]);
  yTranslation.set(matrix[1][2]);
  xScale.set(sqrt(square(matrix[0][0]) + square(matrix[0][1])));
  yScale.set(sqrt(square(matrix[1][0]) + square(matrix[1][1])));
  xShear.set(atan2(matrix[0][1], matrix[0][0]));
  yShear.set(atan2(matrix[1][0], matrix[1][1]));
}

void NetworkedAnimator::TransformationGroup::setLocalAffineTransform(Mat3F const& matrix) {
  localTransform = matrix;
}

Mat3F NetworkedAnimator::TransformationGroup::localAffineTransform() const {
  return localTransform;
}

void NetworkedAnimator::TransformationGroup::setAnimationAffineTransform(Mat3F const& matrix) {
  xTranslationAnimation = matrix[0][2];
  yTranslationAnimation = matrix[1][2];
  xScaleAnimation = sqrt(square(matrix[0][0]) + square(matrix[0][1]));
  yScaleAnimation = sqrt(square(matrix[1][0]) + square(matrix[1][1]));
  xShearAnimation = atan2(matrix[0][1], matrix[0][0]);
  yShearAnimation = atan2(matrix[1][0], matrix[1][1]);
}
void NetworkedAnimator::TransformationGroup::setAnimationAffineTransform(Mat3F const& mat1, Mat3F const& mat2, float progress) {
  xTranslationAnimation = lerp(progress, mat1[0][2], mat2[0][2]);
  yTranslationAnimation = lerp(progress, mat1[1][2], mat2[1][2]);
  xScaleAnimation = lerp(progress, sqrt(square(mat1[0][0]) + square(mat1[0][1])), sqrt(square(mat2[0][0]) + square(mat2[0][1])));
  yScaleAnimation = lerp(progress, sqrt(square(mat1[1][0]) + square(mat1[1][1])), sqrt(square(mat2[1][0]) + square(mat2[1][1])));
  xShearAnimation = angleLerp(progress, atan2(mat1[0][1], mat1[0][0]), atan2(mat2[0][1], mat2[0][0]));
  yShearAnimation = angleLerp(progress, atan2(mat1[1][0], mat1[1][1]), atan2(mat2[1][0], mat2[1][1]));
}

Mat3F NetworkedAnimator::TransformationGroup::animationAffineTransform() const {
  return Mat3F(
      xScaleAnimation * cos(xShearAnimation), xScaleAnimation * sin(xShearAnimation), xTranslationAnimation,
      yScaleAnimation * sin(yShearAnimation), yScaleAnimation * cos(yShearAnimation), yTranslationAnimation,
      0, 0, 1
    );
}

void NetworkedAnimator::setupNetStates() {
  clearNetElements();

  addNetElement(&m_processingDirectives);
  addNetElement(&m_zoom);
  addNetElement(&m_flipped);
  addNetElement(&m_flippedRelativeCenterLine);

  addNetElement(&m_animationRate);
  m_animationRate.setInterpolator(lerp<float, float>);

  addNetElement(&m_globalTags);

  for (auto const& part : sorted(m_animatedParts.partNames()))
    addNetElement(&m_partTags[part]);

  for (auto& pair : m_stateInfo) {
    pair.second.reverse.setCompatibilityVersion(10);
    addNetElement(&pair.second.reverse);
    addNetElement(&pair.second.stateIndex);
    addNetElement(&pair.second.startedEvent);
  }

  for (auto& pair : m_transformationGroups) {
    addNetElement(&pair.second.xTranslation);
    addNetElement(&pair.second.yTranslation);
    addNetElement(&pair.second.xScale);
    addNetElement(&pair.second.yScale);
    addNetElement(&pair.second.xShear);
    addNetElement(&pair.second.yShear);

    if (pair.second.interpolated) {
      pair.second.xTranslation.setInterpolator(lerp<float, float>);
      pair.second.yTranslation.setInterpolator(lerp<float, float>);
      pair.second.xScale.setInterpolator(lerp<float, float>);
      pair.second.yScale.setInterpolator(lerp<float, float>);
      pair.second.xShear.setInterpolator(angleLerp<float, float>);
      pair.second.yShear.setInterpolator(angleLerp<float, float>);
    }
  }

  for (auto& pair : m_rotationGroups) {
    addNetElement(&pair.second.targetAngle);
    addNetElement(&pair.second.netImmediateEvent);
  }

  for (auto& pair : m_particleEmitters) {
    addNetElement(&pair.second.emissionRate);
    addNetElement(&pair.second.burstCount);
    addNetElement(&pair.second.randomSelectCount);
    addNetElement(&pair.second.offsetRegion);
    addNetElement(&pair.second.active);
    addNetElement(&pair.second.burstEvent);

    pair.second.burstEvent.setIgnoreOccurrencesOnNetLoad(true);
  }

  for (auto& pair : m_lights) {
    addNetElement(&pair.second.active);
    addNetElement(&pair.second.xPosition);
    addNetElement(&pair.second.yPosition);
    addNetElement(&pair.second.color);
    addNetElement(&pair.second.pointAngle);

    pair.second.xPosition.setFixedPointBase(0.0125f);
    pair.second.yPosition.setFixedPointBase(0.0125f);
    pair.second.pointAngle.setFixedPointBase(0.01f);

    pair.second.xPosition.setInterpolator(lerp<float, float>);
    pair.second.yPosition.setInterpolator(lerp<float, float>);
    pair.second.pointAngle.setInterpolator(angleLerp<float, float>);
  }

  for (auto& pair : m_sounds) {
    addNetElement(&pair.second.soundPool);
    addNetElement(&pair.second.xPosition);
    addNetElement(&pair.second.yPosition);
    addNetElement(&pair.second.volumeTarget);
    addNetElement(&pair.second.volumeRampTime);
    addNetElement(&pair.second.pitchMultiplierTarget);
    addNetElement(&pair.second.pitchMultiplierRampTime);
    addNetElement(&pair.second.loops);
    addNetElement(&pair.second.signals);

    pair.second.xPosition.setFixedPointBase(0.0125f);
    pair.second.yPosition.setFixedPointBase(0.0125f);

    pair.second.xPosition.setInterpolator(lerp<float, float>);
    pair.second.yPosition.setInterpolator(lerp<float, float>);
  }

  for (auto& pair : m_effects)
    addNetElement(&pair.second.enabled);

}

void NetworkedAnimator::netElementsNeedLoad(bool initial) {
  for (auto& pair : m_stateInfo) {
    if (pair.second.startedEvent.pullOccurred() || initial)
      m_animatedParts.setActiveStateIndex(pair.first, pair.second.stateIndex.get(), true, pair.second.reverse.get());
  }

  for (auto& pair : m_rotationGroups) {
    if (pair.second.netImmediateEvent.pullOccurred() || initial)
      pair.second.currentAngle = pair.second.targetAngle.get();
  }

  // Slaves never call the setters; NetElement deserialization writes storage
  // directly, and this funnel runs after netLoad, readNetDelta, and net
  // interpolation ticks.  Bump the render version ONLY on a discrete
  // drawable-affecting change (NOT every interpolation tick, or the static
  // cache thrashes on moving entities).
  bool discrete = false;
  discrete |= m_globalTags.pullUpdated();
  for (auto& pair : m_partTags)
    discrete |= pair.second.pullUpdated();
  // Value-diff the scalar/data NetElements against shadow copies kept on the
  // animator (NetElementFloating has no pullUpdated, and a value-diff stays
  // quiet across pure interpolation ticks since none of these interpolate).
  if (m_processingDirectives.get() != m_lastSeenProcessingDirectives) {
    m_lastSeenProcessingDirectives = m_processingDirectives.get();
    discrete = true;
  }
  if (m_zoom.get() != m_lastSeenZoom) {
    m_lastSeenZoom = m_zoom.get();
    discrete = true;
  }
  if (m_flipped.get() != m_lastSeenFlipped) {
    m_lastSeenFlipped = m_flipped.get();
    discrete = true;
  }
  if (m_flippedRelativeCenterLine.get() != m_lastSeenCenterLine) {
    m_lastSeenCenterLine = m_flippedRelativeCenterLine.get();
    discrete = true;
  }
  for (auto& pair : m_effects) {
    bool en = pair.second.enabled.get();
    if (en != pair.second.lastSeenEnabled) {
      pair.second.lastSeenEnabled = en;
      discrete = true;
    }
  }
  // RotationGroup::targetAngle and the non-interpolated TransformationGroup
  // floats are likewise storage-written by deserialization on slaves, and
  // partIsStaticCacheable keeps parts referencing them STATIC, so this funnel
  // is their only invalidation path.  Neither has an interpolator (rotation
  // targetAngle never; group floats only when interpolated, see
  // setupNetStates), so these value-diffs only fire at delta-apply.
  // Interpolated groups' floats lerp every tick and their referencing parts
  // are LIVE already: skip them, or the static cache thrashes.
  for (auto& pair : m_rotationGroups) {
    float targetAngle = pair.second.targetAngle.get();
    if (targetAngle != pair.second.lastSeenTargetAngle) {
      pair.second.lastSeenTargetAngle = targetAngle;
      discrete = true;
    }
  }
  for (auto& pair : m_transformationGroups) {
    auto& group = pair.second;
    if (group.interpolated)
      continue;
    auto diff = [&discrete](NetElementFloat const& element, float& lastSeen) {
      float value = element.get();
      if (value != lastSeen) {
        lastSeen = value;
        discrete = true;
      }
    };
    diff(group.xTranslation, group.lastSeenXTranslation);
    diff(group.yTranslation, group.lastSeenYTranslation);
    diff(group.xScale, group.lastSeenXScale);
    diff(group.yScale, group.lastSeenYScale);
    diff(group.xShear, group.lastSeenXShear);
    diff(group.yShear, group.lastSeenYShear);
  }
  if (discrete)
    bumpRenderVersion();
}

void NetworkedAnimator::netElementsNeedStore() {
  for (auto& pair : m_stateInfo) {
    if (pair.second.wasUpdated || (version() < 1)) {
      pair.second.wasUpdated = false;
      pair.second.stateIndex.set(m_animatedParts.activeStateIndex(pair.first));
      pair.second.reverse.set(m_animatedParts.activeStateReverse(pair.first));
    }
  }
}

uint8_t NetworkedAnimator::version() const {
  return m_animatorVersion;
}

uint64_t NetworkedAnimator::renderVersion() const {
  return m_renderVersion;
}

void NetworkedAnimator::bumpRenderVersion() {
  ++m_renderVersion;
}

uint64_t NetworkedAnimator::localTransformHash() const {
  // Local transformation-group matrices are excluded from m_renderVersion
  // (their setters are called in reset+rotate pairs every frame by Humanoid;
  // per-call bumps would re-key a visually-stationary animator).  Instead the
  // combined matrix state keys the static cache.  Hashes the raw float bits of
  // ALL groups' localTransform (no dirty bits): if any matrix differs, the
  // hash -- and with it the cache key -- differs.
  uint64_t h = 5381;
  for (auto const& pair : m_transformationGroups) {
    auto const& m = pair.second.localTransform;
    for (size_t r = 0; r < 3; ++r)
      for (size_t c = 0; c < 3; ++c) {
        uint32_t bits;
        std::memcpy(&bits, &m[r][c], sizeof(bits));
        h = (h * 1099511628211ull) ^ bits;
      }
  }
  return h;
}

// Collects every value the given key could resolve to in a part's merged
// activePart.properties: the base partProperties, every partState's
// partStateProperties, and every per-frame value in partStateFrameProperties
// (freshenActivePart merges exactly these three layers).  State-independent,
// so the answer is conservative across state changes.
static void collectPartPropertyValues(AnimatedPartSet::Part const& part, String const& key, List<Json>& values) {
  if (auto v = part.partProperties.ptr(key))
    values.append(*v);
  for (auto const& stateTypePair : part.partStates) {
    for (auto const& statePair : stateTypePair.second) {
      if (auto v = statePair.second.partStateProperties.ptr(key))
        values.append(*v);
      if (auto frameValues = statePair.second.partStateFrameProperties.ptr(key)) {
        // Frame properties are arrays of per-frame values.
        if (frameValues->isType(Json::Type::Array)) {
          for (auto const& v : frameValues->iterateArray())
            values.append(v);
        } else {
          values.append(*frameValues);
        }
      }
    }
  }
}

bool NetworkedAnimator::anyFlashEffectActive() const {
  for (auto const& pair : m_effects) {
    if (pair.second.enabled.get() && pair.second.type == "flash")
      return true;
  }
  return false;
}

bool NetworkedAnimator::partReferencesLiveRotationGroup(AnimatedPartSet::Part const& part) const {
  List<Json> refs;
  collectPartPropertyValues(part, "rotationGroup", refs);
  for (auto const& ref : refs) {
    if (!ref.isType(Json::Type::String))
      return true; // unintelligible reference: conservative live
    auto group = m_rotationGroups.ptr(ref.toString());
    if (!group)
      return true; // unknown group: conservative live
    // angularVelocity != 0 approaches the target angle continuously in
    // update(); angularVelocity == 0 snaps (verified-safe-as-static).
    if (group->angularVelocity != 0.0f)
      return true;
  }
  return false;
}

bool NetworkedAnimator::partReferencesLiveTransformationGroup(AnimatedPartSet::Part const& part) const {
  List<Json> refs;
  collectPartPropertyValues(part, "transformationGroups", refs);
  for (auto const& ref : refs) {
    if (!ref.isType(Json::Type::Array))
      return true; // unintelligible reference list: conservative live
    for (auto const& nameJson : ref.iterateArray()) {
      if (!nameJson.isType(Json::Type::String))
        return true;
      String name = nameJson.toString();
      auto group = m_transformationGroups.ptr(name);
      if (!group)
        return true; // unknown group: conservative live
      // Interpolated groups lerp their networked affine components between
      // deltas on slaves, and blend state animation by frameProgress: live.
      if (group->interpolated)
        return true;
      // The active-state-animated case (update()): a group currently named by
      // an active-state property is re-seeded from its own current animation
      // transform every tick, so non-reset transforms accumulate continuously.
      // Conservative: any currently-animated group is live.  Mirrors the
      // version() > 0 gate and all-state-types scan of update().
      if (version() > 0) {
        for (auto const& stateTypeName : m_animatedParts.stateTypes()) {
          if (m_animatedParts.activeState(stateTypeName).properties.contains(name))
            return true;
        }
      }
    }
  }
  return false;
}

bool NetworkedAnimator::partHasTransformsProperty(AnimatedPartSet::Part const& part) {
  // Part-local transforms interpolate by frameProgress or accumulate per tick
  // (AnimatedPartSet::freshenActivePart).  Conservative: ANY entry is live.
  List<Json> refs;
  collectPartPropertyValues(part, "transforms", refs);
  return !refs.empty();
}

bool NetworkedAnimator::partIsStaticCacheable(String const& partName) const {
  // A flash-type effect toggles its directive purely off the per-tick effect
  // timer and is prepended to every part's directives: global cache-bust.
  // Runtime input (effect enabled flags): checked LIVE, OUTSIDE the
  // structural memo.
  if (anyFlashEffectActive())
    return false;
  return partIsStaticCacheableStructural(partName);
}

bool NetworkedAnimator::partIsStaticCacheableStructural(String const& partName) const {
  // Memoized: every input of the walk below is construction-constant (part
  // configs scanned across ALL states, anchor chain, group structure,
  // RotationGroup::angularVelocity, TransformationGroup::interpolated,
  // version()) EXCEPT the active-state-animated transformation-group check,
  // which reads activeState(...).properties -- those re-merge only under a
  // generation() bump (AnimatedPartSet's freshen layers).  Keying the memo on
  // generation() therefore makes stale verdicts impossible, while a
  // renderVersion-only re-key (the per-frame Humanoid tag/part-drawables
  // pattern) reuses the partition wholesale instead of re-scanning every
  // part's full config.
  uint64_t generation = m_animatedParts.generation();
  if (m_partitionMemoGeneration != generation) {
    m_partitionMemo.clear();
    m_partitionMemoGeneration = generation;
  }
  if (auto memo = m_partitionMemo.maybe(partName))
    return *memo;

  // Counts structural walks, i.e. memo MISSES -- the direct 'partition work'
  // signal for the cache A/B.  Cache-path only: rebuildStaticCache is the
  // sole engine caller (static-handle idiom: registration/lookup once).
  static auto s_partitionScansCounter = Telemetry::counter("render.drawable.partition.scans",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
  s_partitionScansCounter.inc();

  // A part is static-cacheable iff it AND its whole anchorPart chain have: no
  // live rotation-group ref, no interpolated/active-state-animated
  // transformation group, and no "transforms" property (partTransformation
  // composes anchor transforms transitively).  anchorPart may be introduced by
  // any partState, so follow every possible anchor value; `seen` guards
  // against anchor cycles.
  bool result = [&]() {
    auto const& parts = m_animatedParts.constParts();
    StringList pending = {partName};
    Set<String> seen;
    while (!pending.empty()) {
      String current = pending.takeLast();
      if (!seen.add(current))
        continue;
      auto part = parts.ptr(current);
      if (!part)
        return false; // unknown part: conservative live
      if (partReferencesLiveRotationGroup(*part))
        return false;
      if (partReferencesLiveTransformationGroup(*part))
        return false;
      if (partHasTransformsProperty(*part))
        return false;
      List<Json> anchors;
      collectPartPropertyValues(*part, "anchorPart", anchors);
      for (auto const& anchor : anchors) {
        if (!anchor.isType(Json::Type::String))
          return false; // unintelligible anchor: conservative live
        pending.append(anchor.toString());
      }
    }
    return true;
  }();
  m_partitionMemo[partName] = result;
  return result;
}

Json NetworkedAnimator::mergeIncludes(Json config, Json includes, String relativePath){
  Json includedConfigs;
  for (Json const& path : includes.iterateArray()) {
    auto includeConfig = Root::singleton().assets()->json(AssetPath::relativeTo(relativePath, path.toString()));
    if (includeConfig.contains("includes"))
      includeConfig = mergeIncludes(includeConfig, includeConfig.get("includes"), relativePath);
    includedConfigs = jsonMerge(includedConfigs, includeConfig);
  }
  return jsonMerge(includedConfigs, config);
}

}
