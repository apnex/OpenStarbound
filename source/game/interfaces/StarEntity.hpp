#pragma once

#include <atomic>
#include <type_traits>

#include "StarCasting.hpp"
#include "StarDamage.hpp"
#include "StarLightSource.hpp"
#include "StarDataStream.hpp"

namespace Star {

STAR_CLASS(RenderCallback);
STAR_CLASS(World);
STAR_STRUCT(DamageNotification);
STAR_CLASS(Entity);
STAR_CLASS(TileEntity);
STAR_CLASS(WireEntity);
// Forward declarations for the fast-path downcast accessors below. These return
// raw pointers, so an incomplete type suffices here; the concrete headers must
// NOT be included (they include this one -> cycle).
STAR_CLASS(Object);
STAR_CLASS(Monster);
STAR_CLASS(Npc);
STAR_CLASS(Player);
STAR_CLASS(ItemDrop);
STAR_CLASS(Stagehand);
STAR_CLASS(Plant);
STAR_CLASS(PhysicsEntity);
STAR_CLASS(ScriptedEntity);
STAR_CLASS(InspectableEntity);
STAR_CLASS(ChattyEntity);
STAR_CLASS(InteractiveEntity);

STAR_EXCEPTION(EntityException, StarException);

// Process-global gates for the entity-dormancy awake-set skip (Arc-A Rung 1),
// set once from worldserver.config in WorldServer::init (both default OFF, so a
// shipped binary is byte-identical to today until a key is flipped). Kept
// process-global rather than threaded through World because the WorldServer tick
// loop and Entity share no back-reference; mirrors the file-static atomic
// pattern in NetElementEarlyOut (StarNetElement.hpp).
namespace EntityDormancy {
  extern std::atomic<bool> enabled;   // gate the awake-set skip
  extern std::atomic<bool> validate;  // shadow-run + assert each "dormant" tick is a no-op
  // Telemetry counters (Task 6), mirroring NetElementEarlyOut::hits/walks. Bumped
  // per-entity only when active(), so the shipped default-OFF path stays zero-cost.
  //  skipped    = would-be-dormant entity-updates (!run). In enabled mode these are
  //               actually skipped; in validate mode they still run but are the exact
  //               set enabled mode WOULD skip -> a consistent dormancy ratio for A/B.
  //  ran        = entity-updates that ran (run == true).
  //  mismatches = validate-mode shadow no-op-assertion failures (cumulative; a true
  //               no-op engine slate must not advance the net-version aggregate).
  // WorldServer logs skipped/(skipped+ran) periodically and resets that window.
  extern std::atomic<uint64_t> skipped;
  extern std::atomic<uint64_t> ran;
  extern std::atomic<uint64_t> mismatches;
  inline bool active() {
    return enabled.load(std::memory_order_relaxed) || validate.load(std::memory_order_relaxed);
  }
}

// Specifies how the client should treat an entity created on the client,
// whether it should always be sent to the server and be a slave on the client,
// whether it is allowed to be master on the client, and whether client master
// entities should contribute to client presence.
enum class ClientEntityMode {
  // Always a slave on the client
  ClientSlaveOnly,
  // Can be a master on the client
  ClientMasterAllowed,
  // Can be a master on the client, and when it is contributes to client
  // presence.
  ClientPresenceMaster
};
extern EnumMap<ClientEntityMode> const ClientEntityModeNames;

// The top-level entity type.  The enum order is intended to be in the order in
// which entities should be updated every tick
enum class EntityType : uint8_t {
  Plant,
  Object,
  Vehicle,
  ItemDrop,
  PlantDrop,
  Projectile,
  Stagehand,
  Monster,
  Npc,
  Player
};
extern EnumMap<EntityType> const EntityTypeNames;

class Entity {
public:
  virtual ~Entity();

  virtual EntityType entityType() const = 0;

  // Fast-path downcasts for the per-tick entity hot paths: a single virtual
  // dispatch instead of an expensive dynamic_cast through the entity's
  // virtual-inheritance hierarchy. The base returns nullptr; each target type
  // overrides to return `this` (concrete leaves on the leaf class, cross-cutting
  // interfaces on the interface so every implementer inherits it). Semantically
  // identical to as<T>() but without the RTTI traversal; the entity-query
  // templates dispatch to these via entityCast<T>() (below). Any type WITHOUT an
  // override here stays byte-identical to as<T>() (entityCast falls back to it).
  // INVARIANT: equivalence with dynamic_cast holds only because every accessor
  // target is reached by *public virtual* inheritance with a single subobject of
  // that type. A future class reaching one of these via a private/protected base,
  // or via two non-virtual subobjects, would make asX() and dynamic_cast disagree
  // (the latter case fails loud at compile time as an ambiguous final overrider).
  virtual TileEntity* asTileEntity() { return nullptr; }
  virtual WireEntity* asWireEntity() { return nullptr; }
  virtual Object* asObject() { return nullptr; }
  virtual Monster* asMonster() { return nullptr; }
  virtual Npc* asNpc() { return nullptr; }
  virtual Player* asPlayer() { return nullptr; }
  virtual ItemDrop* asItemDrop() { return nullptr; }
  virtual Stagehand* asStagehand() { return nullptr; }
  virtual Plant* asPlant() { return nullptr; }
  virtual PhysicsEntity* asPhysicsEntity() { return nullptr; }
  virtual ScriptedEntity* asScriptedEntity() { return nullptr; }
  virtual InspectableEntity* asInspectableEntity() { return nullptr; }
  virtual ChattyEntity* asChattyEntity() { return nullptr; }
  virtual InteractiveEntity* asInteractiveEntity() { return nullptr; }

  // Called when an entity is first inserted into a World.  Calling base class
  // init sets the world pointer, entityId, and entityMode.
  virtual void init(World* world, EntityId entityId, EntityMode mode);

  // Should do whatever steps necessary to take an entity out of a world,
  // default implementation clears the world pointer, entityMode, and entityId.
  virtual void uninit();

  // Write state data that changes over time, and is used to keep slaves in
  // sync.  Can return empty and this is the default.  May be called
  // uninitalized.  Should return the delta to be written to the slave, along
  // with the version to pass into writeDeltaState on the next call.  The first
  // delta written to a slave entity will always be the delta starting with 0.
  virtual pair<ByteArray, uint64_t> writeNetState(uint64_t fromVersion = 0, NetCompatibilityRules rules = {});
  // Runs deferred NetElement stores before a writeNetState early-out (Lever #4).
  virtual void netStorePump() {}
  // Will be called with deltas written by writeDeltaState, including if the
  // delta is empty.  interpolationTime will be provided if interpolation is
  // enabled.
  virtual void readNetState(ByteArray data, float interpolationTime = 0.0f, NetCompatibilityRules rules = {});

  virtual void enableInterpolation(float extrapolationHint);
  virtual void disableInterpolation();

  // Base position of this entity, bound boxes, drawables, and other entity
  // positions are relative to this.
  virtual Vec2F position() const = 0;

  // Largest bounding-box of this entity.  Any damage boxes / drawables / light
  // or sound *sources* must be contained within this bounding box.  Used for
  // all top-level spatial queries.
  virtual RectF metaBoundBox() const = 0;

  // By default returns a null rect, if non-null, it defines the area around
  // this entity where it is likely for the entity to physically collide with
  // collision geometry.
  virtual RectF collisionArea() const;

  // Should this entity allow object / block placement over it, and can the
  // entity immediately be despawned without terribly bad effects?
  virtual bool ephemeral() const;

  // How should this entity be treated if created on the client?  Defaults to
  // ClientSlave.
  virtual ClientEntityMode clientEntityMode() const;
  // Should this entity only exist on the master side?
  virtual bool masterOnly() const;

  virtual String name() const;
  virtual String description() const;

  // Gameplay affecting light sources (separate from light sources added during
  // rendering)
  virtual List<LightSource> lightSources() const;

  // All damage sources for this frame.
  virtual List<DamageSource> damageSources() const;

  // Cheap conservative predicate (L-DMG-SKIP-0): MUST return true whenever
  // damageSources() could be non-empty (a strict superset; false ONLY when provably
  // empty). Lets DamageManager skip the per-tick damageSources() call for the
  // statically-empty majority. Base = false (Entity::damageSources() == {}).
  virtual bool hasDamageSources() const;

  // Return the damage that would result from being hit by the given damage
  // source.  Will be called on master and slave entities.  Culling based on
  // team damage and self damage will be done outside of this query.
  virtual Maybe<HitType> queryHit(DamageSource const& source) const;

  // Return the polygonal area in which the entity can be hit. Not used for
  // actual hit computation, only for determining more precisely where a
  // hit intersection occurred (e.g. by projectiles)
  virtual Maybe<PolyF> hitPoly() const;

  // Apply a request to damage this entity. Will only be called on Master
  // entities. DamageRequest might be adjusted based on protection and other
  // effects
  virtual List<DamageNotification> applyDamage(DamageRequest const& damage);

  // Pull any pending damage notifications applied internally, only called on
  // Master entities.
  virtual List<DamageNotification> selfDamageNotifications();

  // Called on master entities when a DamageRequest has been generated due to a
  // DamageSource from this entity being applied to another entity.  Will be
  // called on the *causing* entity of the damage.
  virtual void hitOther(EntityId targetEntityId, DamageRequest const& damageRequest);

  // Called on master entities when this entity has damaged another entity.
  // Only called on the *source entity* of the damage, which may be different
  // than the causing entity.
  virtual void damagedOther(DamageNotification const& damage);

  // Returning true here indicates that this entity should be removed from the
  // world, default returns false.
  virtual bool shouldDestroy() const;
  // Will be called once before removing the entity from the World on both
  // master and slave entities.
  virtual void destroy(RenderCallback* renderCallback);

  // Entities can send other entities potentially remote messages and get
  // responses back from them, and should implement this to receive and respond
  // to messages.  If the message is NOT handled, should return Nothing,
  // otherwise should return some Json value.
  // This will only ever be called on master entities.
  virtual Maybe<Json> receiveMessage(ConnectionId sendingConnection, String const& message, JsonArray const& args);

  virtual void update(float dt, uint64_t currentStep);

  // Lever Rung-1 dormancy seam. Default = always-awake (next step), so a type
  // that does not override these behaves exactly as today.
  // nextEngineWakeStep: the earliest step at which this entity's ENGINE update
  //   slate needs to run again, given its own timers/animations. {} means "no
  //   self-scheduled work — sleep until an external wake". currentStep+1 = "every
  //   tick" (the default). The script is NOT part of this — it runs on its own
  //   scriptDelta cadence, which an adopter folds INTO this horizon.
  virtual Maybe<uint64_t> nextEngineWakeStep(uint64_t currentStep) const { return currentStep + 1; }
  // requestWake: an external mutation point calls this to force the entity awake
  // next tick (the WorldServer consumes m_wakeRequested). Cheap, idempotent.
  // THREADING INVARIANT: m_wakeRequested is a plain bool, safe because all server
  // entity mutation is serialized under WorldServerThread::m_mutex (held across the
  // whole tick). Callers MUST hold that lock — every current wake source
  // (interaction, wiring, receiveMessage, tile damage) runs inside the locked tick.
  // An off-lock caller (e.g. a network-thread path) would need this made atomic.
  void requestWake() { m_wakeRequested = true; }
  bool takeWakeRequested() { bool w = m_wakeRequested; m_wakeRequested = false; return w; }

  // Dormancy validate/shadow oracle (Task 6). The per-entity net-version aggregate
  // (Lever #4's NetElementVersion::latestChange — the highest version at which ANY
  // net element in this entity recorded a change). Monotonically non-decreasing; a
  // genuine no-op engine slate does NOT advance it. In validate mode the WorldServer
  // captures this BEFORE a would-be-dormant entity's still-run update(), then runs
  // netStorePump() to flush deferred stores, and asserts it did not advance AFTER ->
  // proof the skipped slate was a true no-op. {} = "this type exposes no aggregate"
  // -> not validatable (the shadow check is skipped). The aggregate lives on the
  // shared NetElementVersion, so a single override on the base of a type family
  // (Object, via its m_netGroup) covers every subclass automatically — subclasses
  // need NOT re-override this even when they adopt dormancy (ContainerObject,
  // FarmableObject override nextEngineWakeStep but inherit this oracle unchanged).
  // COVERAGE: because the validate loop pumps deferred stores before the after-
  // capture, this now flags BOTH eager NetElements AND deferred-store net state
  // written only at pump time via setNetStates() — i.e. ContainerObject item
  // contents (m_itemsNetState) and Object orientation (m_orientationIndexNetState)
  // ARE covered. The residual blind spot is only a genuinely NON-NETTED side effect
  // (an outgoing entity message, a direct world-tile mutation, a spawn) from a
  // skipped slate. These are bounded: dormancy only ever skips the ENGINE slate (the
  // script always runs on its own cadence) and the Task-5 wake-source audit covers
  // the external mutators. SUBTLETY: latestChange = m_version at markChanged() time,
  // and m_version only advances when writeNetState() actually emits a delta — so an
  // entity NOT monitored by any client may not advance latestChange on a net
  // mutation (unmonitored => non-observable, no desync; it self-arms the moment a
  // client attaches and writeNetState starts incrementing the version).
  virtual Maybe<uint64_t> netVersionLatestChange() const { return {}; }

  virtual void render(RenderCallback* renderer);

  virtual void renderLightSources(RenderCallback* renderer);

  EntityId entityId() const;

  EntityDamageTeam getTeam() const;

  // Returns true if an entity is initialized in a world, and thus has a valid
  // world pointer, entity id, and entity mode.
  bool inWorld() const;

  // Throws an exception if not currently in a world.
  World* world() const;
  // Returns nullptr if not currently in a world.
  World* worldPtr() const;

  // Specifies if the entity is to be saved to disk alongside the sector or
  // despawned.
  bool persistent() const;

  // Entity should keep any sector it is in alive.  Default implementation
  // returns false.
  bool keepAlive() const;

  // If set, then the entity will be discoverable by its unique id and will be
  // indexed in the stored world.  Unique ids must be different across all
  // entities in a single world.
  Maybe<String> uniqueId() const;

  // EntityMode will only be set if the entity is initialized, if the entity is
  // uninitialized then isMaster and isSlave will both return false.
  Maybe<EntityMode> entityMode() const;
  bool isMaster() const;
  bool isSlave() const;

protected:
  Entity();

  void setPersistent(bool persistent);
  void setKeepAlive(bool keepAlive);
  void setUniqueId(Maybe<String> uniqueId);
  void setTeam(EntityDamageTeam newTeam);

private:
  EntityId m_entityId;
  Maybe<EntityMode> m_entityMode;
  bool m_persistent;
  bool m_keepAlive;
  Maybe<String> m_uniqueId;
  World* m_world;
  EntityDamageTeam m_team;
  bool m_wakeRequested = true;
};

template <typename EntityT>
using EntityCallbackOf = function<void(shared_ptr<EntityT> const&)>;

template <typename EntityT>
using EntityFilterOf = function<bool(shared_ptr<EntityT> const&)>;

typedef EntityCallbackOf<Entity> EntityCallback;
typedef EntityFilterOf<Entity> EntityFilter;

// Turns the per-candidate query downcast from a dynamic_pointer_cast (an RTTI
// traversal of the virtual-inheritance diamond, run once per entity in every
// spatial query) into a single virtual dispatch via the asX() accessors above.
//
// EntityDowncast<T> maps a target type to its accessor; the primary template
// has no accessor, so any T without a specialization falls through entityCast's
// dynamic_cast fallback and is byte-identical to as<T>(). A type WITH an
// accessor is reconstructed through the shared_ptr aliasing constructor, which
// shares the source control block (zero allocation) and stores exactly the
// adjusted pointer the override's `return this` yields -- the same pointer
// dynamic_pointer_cast would produce, with identical lifetime/deleter semantics.
template <typename T>
struct EntityDowncast { static constexpr bool HasAccessor = false; };

#define STAR_ENTITY_DOWNCAST(Type, accessor)                  \
  template <>                                                 \
  struct EntityDowncast<Type> {                               \
    static constexpr bool HasAccessor = true;                 \
    static Type* get(Entity* e) { return e->accessor(); }     \
  };
STAR_ENTITY_DOWNCAST(TileEntity, asTileEntity)
STAR_ENTITY_DOWNCAST(WireEntity, asWireEntity)
STAR_ENTITY_DOWNCAST(Object, asObject)
STAR_ENTITY_DOWNCAST(Monster, asMonster)
STAR_ENTITY_DOWNCAST(Npc, asNpc)
STAR_ENTITY_DOWNCAST(Player, asPlayer)
STAR_ENTITY_DOWNCAST(ItemDrop, asItemDrop)
STAR_ENTITY_DOWNCAST(Stagehand, asStagehand)
STAR_ENTITY_DOWNCAST(Plant, asPlant)
STAR_ENTITY_DOWNCAST(PhysicsEntity, asPhysicsEntity)
STAR_ENTITY_DOWNCAST(ScriptedEntity, asScriptedEntity)
STAR_ENTITY_DOWNCAST(InspectableEntity, asInspectableEntity)
STAR_ENTITY_DOWNCAST(ChattyEntity, asChattyEntity)
STAR_ENTITY_DOWNCAST(InteractiveEntity, asInteractiveEntity)
#undef STAR_ENTITY_DOWNCAST

// Drop-in replacement for as<T>(EntityPtr) on the query hot paths. Templated on
// the source pointer U so a caller holding a derived pointer (e.g. atTile's
// TileEntityPtr) need not build a temporary EntityPtr. Identity/upcast and the
// accessor path avoid RTTI entirely; everything else keeps the dynamic_cast.
template <typename T, typename U>
shared_ptr<T> entityCast(shared_ptr<U> const& e) {
  if constexpr (std::is_same_v<T, U> || std::is_same_v<T, Entity>) {
    return e;
  } else if constexpr (EntityDowncast<T>::HasAccessor) {
    if (e) {
      if (T* p = EntityDowncast<T>::get(e.get()))
        return shared_ptr<T>(e, p);
    }
    return {};
  } else {
    return as<T>(e);
  }
}

// Filters based first on dynamic casting to the given type, then optionally on
// the given derived type filter.
template <typename EntityT>
EntityFilter entityTypeFilter(function<bool(shared_ptr<EntityT> const&)> filter = {}) {
  return [filter](EntityPtr const& e) -> bool {
    if (auto entity = entityCast<EntityT>(e)) {
      return !filter || filter(entity);
    } else {
      return false;
    }
  };
}
}
