#pragma once

#include "StarOrderedMap.hpp"
#include "StarJson.hpp"
#include "StarMatrix3.hpp"

namespace Star {

STAR_EXCEPTION(AnimatedPartSetException, StarException);

// Defines a "animated" data set constructed in such a way that it is very
// useful for doing generic animations with lots of additional animation data.
// It is made up of two concepts, "states" and "parts".
//
// States:
//
// There are N "state types" defined, which each defines a set of mutually
// exclusive states that each "state type" can be in.  For example, one state
// type might be "movement", and the "movement" states might be "idle", "walk",
// and "run.  Another state type might be "attack" which could have as its
// states "idle", and "melee".  Each state type will have exactly one currently
// active state, so this class may, for example,  be in the total state of
// "movement:idle" and "attack:melee".  Each state within each state type is
// animated, so that over time the state frame increases and may loop around,
// or transition into another state so that that state type without interaction
// may go from "melee" to "idle" when the "melee" state animation is finished.
// This is defined by the individual state config in the configuration passed
// into the constructor.
//
// Parts:
//
// Each instance of this class also can have N "Parts" defined, which are
// groups of properties that "listen" to active states.  Each part can "listen"
// to one or more state types, and the first matching state x state type pair
// (in order of state type priority which is specified in the config) is
// chosen, and the properties from that state type and state are merged into
// the part to produce the final active part information.  Rather than having a
// single image or image set for each part, since this class is intended to be
// as generic as possible, all of this data is assumed to be queried from the
// part properties, so that things such as image data as well as other things
// like damage or collision polys can be stored along with the animation
// frames, the part state, the base part, whichever is most applicable.
class AnimatedPartSet {
public:
  struct ActiveStateInformation {
    String stateTypeName;
    String stateName;
    float timer;
    unsigned frame;
    float frameProgress;
    JsonObject properties;
    bool reverse;
    unsigned nextFrame;
    JsonObject nextProperties;
  };

  struct ActivePartInformation {
    String partName;
    // If a state match is found, this will be set.
    Maybe<ActiveStateInformation> activeState;
    JsonObject properties;
    JsonObject nextProperties;

    Mat3F animationAffineTransform() const;
    void setAnimationAffineTransform(Mat3F const& matrix);
    void setAnimationAffineTransform(Mat3F const& mat1, Mat3F const& mat2, float progress);

    float xTranslationAnimation;
    float yTranslationAnimation;
    float xScaleAnimation;
    float yScaleAnimation;
    float xShearAnimation;
    float yShearAnimation;
  };

    enum AnimationMode {
    End,
    Loop,
    Transition
  };

  struct State {
    unsigned frames;
    float cycle;
    AnimationMode animationMode;
    String transitionState;
    JsonObject stateProperties;
    JsonObject stateFrameProperties;
  };

  struct StateType {
    float priority;
    bool enabled;
    String defaultState;
    JsonObject stateTypeProperties;
    OrderedHashMap<String, shared_ptr<State const>> states;

    ActiveStateInformation activeState;
    State const* activeStatePointer;
    bool activeStateDirty;

    // Memo of the last resolved key for which activeState.properties/nextProperties were merged.
    String resolvedStateName;
    unsigned resolvedFrame = ~0u;
    unsigned resolvedNextFrame = ~0u;
    bool resolvedReverse = false;
    bool resolvedValid = false;

    // Per-state-type counterpart of generation(): bumped only when THIS state
    // type's resolved (stateName, frame, nextFrame, reverse) key changes -- i.e.
    // exactly when its <T_state>/<T_frame>/<T_frameIndex> tags (and re-merged
    // custom animationTags) change. Lets the per-part drawable cache invalidate
    // a part that resolves a FOREIGN state type's tag. Per-instance; not serialized.
    uint64_t generation = 1;
  };

  struct PartState {
    JsonObject partStateProperties;
    JsonObject partStateFrameProperties;
  };

  struct Part {
    JsonObject partProperties;
    StringMap<StringMap<PartState>> partStates;

    ActivePartInformation activePart;
    bool activePartDirty;

    // Memo of the matched (stateType,state,frame,nextFrame) for which the property merge ran.
    String resolvedStateTypeName;
    String resolvedStateName;
    unsigned resolvedFrame = ~0u;
    unsigned resolvedNextFrame = ~0u;
    bool resolvedValid = false;

    // Per-part counterpart of generation(): bumped only when THIS part's resolved
    // (stateType,state,frame,nextFrame) key changes (same site as the whole-entity
    // generation() bump).  Lets the per-part drawable cache invalidate one part
    // without rebuilding its siblings.  Per-instance; never serialized.
    uint64_t partGeneration = 1;
  };

  AnimatedPartSet();
  AnimatedPartSet(Json config, uint8_t animatiorVersion);

  // Returns the available state types.
  StringList stateTypes() const;

  // If a state type is disabled, no parts will match against it even
  // if they have entries for that state type.
  void setStateTypeEnabled(String const& stateTypeName, bool enabled);
  void setEnabledStateTypes(StringList const& stateTypeNames);
  bool stateTypeEnabled(String const& stateTypeName) const;

  // Returns the available states for the given state type.
  StringList states(String const& stateTypeName) const;

  StringList partNames() const;

  // Sets the active state for this state type.  If the state is different than
  // the previously set state, will start the new states animation off at the
  // beginning.  If alwaysStart is true, then starts the state animation off at
  // the beginning even if no state change has occurred.  Returns true if a
  // state animation reset was done.
  bool setActiveState(String const& stateTypeName, String const& stateName, bool alwaysStart = false, bool reverse = false);

  // Restart this given state type's timer off at the beginning.
  void restartState(String const& stateTypeName);

  ActiveStateInformation const& activeState(String const& stateTypeName) const;
  ActivePartInformation const& activePart(String const& partName) const;
  State const& getState(String const& stateTypeName, String const& stateName) const;
  // The raw (unmerged) state-type-level properties. Used by the per-part drawable
  // cache to scan custom animationTags keys defined at the state-type level, which
  // the flat (overwrite) state/frame merge can hide from any single active state.
  JsonObject const& stateTypeProperties(String const& stateTypeName) const;

  StringMap<Part> const& constParts() const;
  StringMap<Part>& parts();

  // Function will be given the name of each state type, and the
  // ActiveStateInformation for the active state for that state type.
  void forEachActiveState(function<void(String const&, ActiveStateInformation const&)> callback) const;

  // Function will be given the name of each part, and the
  // ActivePartInformation for the active part.
  void forEachActivePart(function<void(String const&, ActivePartInformation const&)> callback) const;

  // Like forEachActiveState, but stops as soon as the visitor returns true and
  // yields the (already-freshened) StateType so the caller can read both the
  // active state and its State definition without a second name lookup. Iterates
  // m_stateTypes directly -- its forward order is exactly stateTypes()/keys()
  // order (keys() is built over the same ordered storage), so this is byte-identical
  // to a `for (name : stateTypes()) activeState(name)` scan while avoiding the
  // per-call keys() StringList allocation and the per-iteration hash lookup. Used
  // for highest-priority-wins and any-still-animating scans on the hot server path.
  // Visitor: bool(String const& stateTypeName, StateType const& stateType).
  template <typename Visitor>
  void forEachStateTypeUntil(Visitor&& visitor) const {
    for (auto const& p : m_stateTypes) {
      const_cast<AnimatedPartSet*>(this)->freshenActiveState(const_cast<StateType&>(p.second));
      if (visitor(p.first, p.second))
        break;
    }
  }

  // Useful for serializing state changes.  Since each set of states for a
  // state type is ordered, it is possible to simply serialize and deserialize
  // the state index for that state type.
  size_t activeStateIndex(String const& stateTypeName) const;
  bool activeStateReverse(String const& stateTypeName) const;
  bool setActiveStateIndex(String const& stateTypeName, size_t stateIndex, bool alwaysStart = false, bool reverse = false);

  // Animate each state type forward 'dt' time, and either change state frames
  // or transition to new states, depending on the config.
  void update(float dt);

  // Pushes all the animations into their final state
  void finishAnimations();

  uint8_t version() const;

  // Monotonic stamp bumped only when the resolved static output (active state name,
  // integer frame/nextFrame, reverse, or part property resolution) changes. Does NOT
  // change for sub-frame frameProgress / continuous transforms. Per-instance; never serialized.
  uint64_t generation() const;

  // Per-part counterpart of generation(): the monotonic stamp for one part,
  // bumped only when that part's resolved key changes.  Returns 0 for an unknown
  // part name.  Read AFTER freshening (forEachActivePart / the drawable path's
  // part enumeration), exactly like generation().
  uint64_t partGeneration(String const& partName) const;

  // Per-state-type counterpart of generation(); 0 for an unknown state type.
  uint64_t stateTypeGeneration(String const& stateTypeName) const;
  // Bumps whenever ANY state type's resolved key changes. Conservative dependency
  // for parts that consume custom animationTags (whose first-definer owner can shift).
  uint64_t stateTypesEpoch() const;

  Json getStateFrameProperty(String const& stateType, String const& propertyName, String state, int frame) const;
  Json getPartStateFrameProperty(String const& partName, String const& propertyName, String const& stateType, String state, int frame) const;

private:
  static AnimationMode stringToAnimationMode(String const& string);

  void freshenActiveState(StateType& stateType);
  void freshenActivePart(Part& part);

  OrderedHashMap<String, StateType> m_stateTypes;
  StringMap<Part> m_parts;

  uint8_t m_animatorVersion;
  uint64_t m_generation = 1;
  uint64_t m_stateTypesEpoch = 1;
};

}
