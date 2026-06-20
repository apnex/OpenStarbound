#include "StarWorldServer.hpp"
#include "StarEntity.hpp"
#include "StarFile.hpp"
#include "StarRoot.hpp"
#include "StarObject.hpp"
#include "StarObjectDatabase.hpp"

#include "gtest/gtest.h"

using namespace Star;

// Task 6 offline soak / equivalence test for entity dormancy.
//
// HARNESS CHOICE: a REAL WorldServer (mirroring server_test.cpp's TelemetryServerTick
// which stands up the same `WorldServer(size, ephemeralFile())` the engine uses for
// flat/ship worlds) driven by direct WorldServer::update() calls, populated with a
// small synthetic Entity subclass. We exercise the ACTUAL production dormancy loop in
// WorldServer::update (awake-set promotion, the run/horizon state machine, the skip,
// the validate/shadow assertion + telemetry) rather than a re-implemented copy.
//
// WHY A SYNTHETIC Entity (not a real Object): a real Object needs an ObjectDatabase
// config placed on generated terrain, and adding ARBITRARY-behaviour real entities is
// impractical. A synthetic Entity lets each test pin an exact engine-slate behaviour +
// wake horizon. The one hazard a non-factory entity type would hit is WorldStorage's
// sector serialization (entityFactory->storeVersionedEntity knows only the 10 built-in
// types) and the zombie/persistence sweep; we sidestep both by (a) setKeepAlive(true)
// so our sector is never unloaded/zombied, and (b) removeEntity()'ing every synthetic
// before the WorldServer is destroyed, so unloadAll() never serializes one. No clients
// are attached, so the per-client net path (writeNetState/netStoreEntity) never runs.
//
// DETERMINISM: we compare only OUR entities' observable state, read directly from our
// own handles. Each profile's state is a pure function of currentStep (and externally-
// applied pokes replayed at identical tick indices), independent of the weather/sky/
// liquid RNG, and both the OFF and ON runs are fresh WorldServers that step from 0.

namespace {

// A synthetic master entity with fully scriptable engine-slate behaviour + wake horizon.
//  - m_behavior(step): the "engine slate" run inside update(). A dormancy-correct slate
//    is a pure function of currentStep, so it produces identical observable state whether
//    update() runs every tick (OFF) or only on its horizon cadence (ON).
//  - m_horizon(step):  nextEngineWakeStep() — {} = sleep, s+1 = stay awake, >s+1 = schedule.
//  - bumpState(): the unit of net-observable mutation; advances both the observable value
//    and the per-entity net-version aggregate (m_netChange == NetElementVersion::latestChange).
class DormancyTestEntity : public Entity {
public:
  DormancyTestEntity(Vec2F pos) : m_pos(pos) {
    // Keep our sector resident so WorldStorage never tries to serialize/zombie a
    // non-factory entity type during the run (see HARNESS CHOICE above).
    setKeepAlive(true);
  }

  EntityType entityType() const override { return EntityType::Object; }
  Vec2F position() const override { return m_pos; }
  RectF metaBoundBox() const override { return RectF(-0.5f, -0.5f, 0.5f, 0.5f); }

  // Dormancy validate/shadow oracle: the per-entity net-version aggregate.
  Maybe<uint64_t> netVersionLatestChange() const override { return m_netChange; }

  Maybe<uint64_t> nextEngineWakeStep(uint64_t currentStep) const override {
    return m_horizon ? m_horizon(currentStep) : Maybe<uint64_t>(currentStep + 1);
  }

  void update(float, uint64_t currentStep) override {
    ++m_updateCount;
    m_lastUpdateStep = currentStep;
    if (m_behavior)
      m_behavior(*this, currentStep);
  }

  // A net-observable mutation: advances the value and the net-version aggregate.
  void bumpState(int64_t delta = 1) { m_value += delta; ++m_netChange; }

  Vec2F m_pos;
  int64_t m_value = 0;
  uint64_t m_netChange = 0;       // models NetElementVersion::latestChange (monotonic)
  uint64_t m_updateCount = 0;     // how many times update() actually ran
  uint64_t m_lastUpdateStep = 0;  // currentStep of the last update() run
  bool m_externalPending = false; // set by an external poke; applied on next update()
  function<void(DormancyTestEntity&, uint64_t)> m_behavior;
  function<Maybe<uint64_t>(uint64_t)> m_horizon;
};

typedef shared_ptr<DormancyTestEntity> TestEntityPtr;
typedef List<TestEntityPtr> TestEntityList;

// One per-tick observable snapshot of a tracked entity.
struct Snap {
  Vec2F pos;
  int64_t value;
  uint64_t netChange;
  bool alive;
  bool operator==(Snap const& o) const {
    return pos == o.pos && value == o.value && netChange == o.netChange && alive == o.alive;
  }
};

constexpr float Dt = 1.0f / 60.0f;

// Run a scenario in a fresh WorldServer under the given dormancy gates and return a
// per-tick trace of every built entity's observable state. `build` creates + addEntity's
// the tracked entities; `poke` applies external events (identically replayed across runs).
List<List<Snap>> runScenario(bool enabled, bool validate,
    function<TestEntityList(WorldServer&)> build,
    function<void(uint64_t, TestEntityList&)> poke,
    unsigned ticks) {
  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  // init() reset the process-global gates from config (OFF); set them AFTER construction
  // and BEFORE addEntity so the ON run seeds the awake-set on insert.
  EntityDormancy::enabled.store(enabled, std::memory_order_relaxed);
  EntityDormancy::validate.store(validate, std::memory_order_relaxed);

  TestEntityList ents = build(*ws);

  List<List<Snap>> trace;
  for (unsigned t = 0; t < ticks; ++t) {
    poke(t, ents);
    ws->update(Dt);
    List<Snap> row;
    for (auto const& e : ents)
      row.append(Snap{e->m_pos, e->m_value, e->m_netChange, (bool)ws->entity(e->entityId())});
    trace.append(std::move(row));
  }

  // Remove our synthetic entities before the WorldServer is destroyed so its
  // unloadAll() never serializes a non-factory type.
  for (auto const& e : ents)
    ws->removeEntity(e->entityId(), false);
  EntityDormancy::enabled.store(false, std::memory_order_relaxed);
  EntityDormancy::validate.store(false, std::memory_order_relaxed);
  return trace; // ws (and its world) is torn down here, after our entities were removed
}

// Assert two traces are identical tick-for-tick; report the first divergence precisely.
void expectTracesEqual(List<List<Snap>> const& a, List<List<Snap>> const& b) {
  ASSERT_EQ(a.size(), b.size());
  for (size_t t = 0; t < a.size(); ++t) {
    ASSERT_EQ(a[t].size(), b[t].size()) << "entity count mismatch at tick " << t;
    for (size_t i = 0; i < a[t].size(); ++i) {
      EXPECT_TRUE(a[t][i] == b[t][i])
          << "DIVERGENCE at tick " << t << " entity " << i
          << ": OFF{pos=(" << a[t][i].pos[0] << "," << a[t][i].pos[1] << ") value=" << a[t][i].value
          << " netChange=" << a[t][i].netChange << " alive=" << a[t][i].alive
          << "} != ON{pos=(" << b[t][i].pos[0] << "," << b[t][i].pos[1] << ") value=" << b[t][i].value
          << " netChange=" << b[t][i].netChange << " alive=" << b[t][i].alive << "}";
      if (!(a[t][i] == b[t][i]))
        return; // stop at first divergence to keep output readable
    }
  }
}

bool tracesEqual(List<List<Snap>> const& a, List<List<Snap>> const& b) {
  if (a.size() != b.size())
    return false;
  for (size_t t = 0; t < a.size(); ++t) {
    if (a[t].size() != b[t].size())
      return false;
    for (size_t i = 0; i < a[t].size(); ++i)
      if (!(a[t][i] == b[t][i]))
        return false;
  }
  return true;
}

// A mixed population covering the dormancy profiles. Returns handles in a stable order.
TestEntityList buildMixedPopulation(WorldServer& ws) {
  TestEntityList ents;

  // 0: static idle object — never mutates, sleeps forever ({} horizon).
  auto idle = make_shared<DormancyTestEntity>(Vec2F(100.0f, 100.0f));
  idle->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; };
  ents.append(idle);

  // 1: scripted-cadence object — mutates (and drifts) every K steps; horizon = next K.
  uint64_t const K = 10;
  auto scripted = make_shared<DormancyTestEntity>(Vec2F(110.0f, 100.0f));
  scripted->m_behavior = [K](DormancyTestEntity& e, uint64_t s) {
    if (s % K == 0) { e.m_pos[0] += 1.0f; e.bumpState(); }
  };
  scripted->m_horizon = [K](uint64_t s) -> Maybe<uint64_t> { return (s / K + 1) * K; };
  ents.append(scripted);

  // 2: animated object — frame is a pure function of step; horizon = next frame boundary.
  uint64_t const F = 7;
  auto animated = make_shared<DormancyTestEntity>(Vec2F(120.0f, 100.0f));
  animated->m_behavior = [F](DormancyTestEntity& e, uint64_t s) {
    int64_t frame = (int64_t)(s / F);
    if (frame != e.m_value) { e.m_value = frame; ++e.m_netChange; }
  };
  animated->m_horizon = [F](uint64_t s) -> Maybe<uint64_t> { return (s / F + 1) * F; };
  ents.append(animated);

  // 3: externally-woken object — sleeps ({} horizon), mutates only when an external poke
  //    set m_externalPending and requested a wake.
  auto woken = make_shared<DormancyTestEntity>(Vec2F(130.0f, 100.0f));
  woken->m_behavior = [](DormancyTestEntity& e, uint64_t) {
    if (e.m_externalPending) { e.bumpState(); e.m_externalPending = false; }
  };
  woken->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; };
  ents.append(woken);

  for (auto const& e : ents)
    ws.addEntity(e);
  return ents;
}

// External pokes for the mixed population: wake entity #3 at a few fixed tick indices.
void pokeMixed(uint64_t t, TestEntityList& ents) {
  if (t == 50 || t == 123 || t == 400 || t == 999) {
    ents[3]->m_externalPending = true;
    ents[3]->requestWake();
  }
}

// RAII cleanup for the tests that drive a WorldServer directly (rather than via
// runScenario, which already cleans up before returning). A mid-test ASSERT_*
// failure returns immediately; without this guard that would leave the process-
// global dormancy gates set AND non-factory synthetic entities in the
// WorldServer, whose ~WorldServer->unloadAll() then tries to serialize them — a
// crash that masks the real assertion failure. The dtor removes our entities
// (so unloadAll never sees them) and resets the gates, running on every exit
// path. Declare it AFTER the WorldServer so it destructs BEFORE the ws.
struct DormancyTestGuard {
  WorldServer* ws = nullptr;
  List<EntityId> ids;
  ~DormancyTestGuard() {
    if (ws) {
      for (EntityId id : ids)
        if (ws->entity(id))
          ws->removeEntity(id, false);
    }
    EntityDormancy::enabled.store(false, std::memory_order_relaxed);
    EntityDormancy::validate.store(false, std::memory_order_relaxed);
  }
};

// A REAL Object whose protected setOrientationIndex is exposed so a test can
// mutate its DEFERRED net state (m_orientationIndexNetState — written only at
// pump time via Object::setNetStates) WITHOUT arming a wake. This faithfully
// models a would-be-dormant slate that nonetheless mutates net state: the
// ContainerObject item API can't (itemsUpdated()->requestWake() would wake the
// entity, so it could never be would-be-dormant), but setOrientationIndex does
// NOT wake — and orientation is one of the two exact real deferred-store
// adopters the validate hole was missing. Everything that the oracle exercises
// (m_netGroup, m_orientationIndexNetState, setNetStates, netStorePump,
// netVersionLatestChange) is the real production Object code.
class OrientationProbeObject : public Object {
public:
  OrientationProbeObject(ObjectConfigConstPtr config, Json const& parameters = JsonObject())
    : Object(config, parameters) {}
  void pokeOrientation(size_t idx) { setOrientationIndex(idx); }
  void keepResident() { setKeepAlive(true); } // keep the sector resident in-test
};

// Find a dead-simple vanilla object for the real-oracle test: a plain "object"
// type that is scriptless (no Lua to error on or to self-schedule a wake via the
// script cadence), has at least one orientation all of which are single-frame
// (frames > 1 would keep it awake every tick via the animation horizon -> never
// would-be-dormant), has no animation config (an active networked-animator could
// keep it awake / mutate net state), and no liquid-level requirement (which on
// bare test terrain would mark it broken and despawn it). Vanilla has hundreds.
Maybe<String> findSimpleDormantObject() {
  auto db = Root::singleton().objectDatabase();
  for (auto const& name : db->allObjects()) {
    auto cfg = db->getConfig(name);
    if (cfg->type != "object")
      continue;
    if (!cfg->scripts.empty() || !cfg->animationScripts.empty())
      continue;
    if (!cfg->animationConfig.isNull())
      continue;
    if (cfg->orientations.empty())
      continue;
    if (cfg->minimumLiquidLevel || cfg->maximumLiquidLevel)
      continue;
    bool allSingleFrame = true;
    for (auto const& o : cfg->orientations)
      if (o->frames > 1) { allSingleFrame = false; break; }
    if (!allSingleFrame)
      continue;
    return name;
  }
  return {};
}

} // namespace

// EQUIVALENCE: the same mixed scenario with dormancy OFF vs ON must produce byte-identical
// observable entity state (position, net-version aggregate, alive) every tick for ~2000 ticks.
TEST(EntityDormancy, EquivalenceOffVsOnMixedProfiles) {
  unsigned const Ticks = 2000;
  auto off = runScenario(false, false, buildMixedPopulation, pokeMixed, Ticks);
  auto on = runScenario(true, false, buildMixedPopulation, pokeMixed, Ticks);
  expectTracesEqual(off, on);

  // Sanity: the scenario actually did meaningful work (not a trivially-equal no-op trace).
  EXPECT_GT(off.last()[1].netChange, 0u) << "scripted entity should have mutated";
  EXPECT_GT(off.last()[2].netChange, 0u) << "animated entity should have mutated";
  EXPECT_EQ(off.last()[3].netChange, 4u) << "externally-woken entity should have mutated 4x";
  EXPECT_EQ(off.last()[0].netChange, 0u) << "idle entity should never mutate";
}

// NEGATIVE CONTROL — fail-without-wake: an entity that mutates every tick but reports a far
// horizon and never requests a wake. With dormancy ON it sleeps and its state freezes, so the
// ON trace MUST DIVERGE from OFF. This is what the soak/validate would CATCH (a missing wake).
TEST(EntityDormancy, NegativeControlMissingWakeDiverges) {
  unsigned const Ticks = 300;
  auto build = [](WorldServer& ws) {
    auto e = make_shared<DormancyTestEntity>(Vec2F(200.0f, 100.0f));
    e->m_behavior = [](DormancyTestEntity& e, uint64_t) { e.bumpState(); }; // mutates, NO requestWake
    e->m_horizon = [](uint64_t s) -> Maybe<uint64_t> { return s + 100000; }; // wrongly-far horizon
    ws.addEntity(e);
    return TestEntityList{e};
  };
  auto noPoke = [](uint64_t, TestEntityList&) {};
  auto off = runScenario(false, false, build, noPoke, Ticks);
  auto on = runScenario(true, false, build, noPoke, Ticks);

  EXPECT_FALSE(tracesEqual(off, on)) << "missing-wake bug should make ON diverge from OFF";
  // OFF mutates every tick; ON freezes after its single initial wake.
  EXPECT_EQ(off.last()[0].value, (int64_t)Ticks);
  EXPECT_EQ(on.last()[0].value, 1) << "ON should run only the initial wake then sleep";
}

// NEGATIVE CONTROL — pass-with-wake: the SAME buggy entity, but it now re-arms requestWake()
// each update (the missing wake source restored). It stays awake every tick, so ON == OFF.
TEST(EntityDormancy, NegativeControlWithWakePasses) {
  unsigned const Ticks = 300;
  auto build = [](WorldServer& ws) {
    auto e = make_shared<DormancyTestEntity>(Vec2F(210.0f, 100.0f));
    e->m_behavior = [](DormancyTestEntity& e, uint64_t) { e.bumpState(); e.requestWake(); }; // wake restored
    e->m_horizon = [](uint64_t s) -> Maybe<uint64_t> { return s + 100000; }; // still a far horizon
    ws.addEntity(e);
    return TestEntityList{e};
  };
  auto noPoke = [](uint64_t, TestEntityList&) {};
  auto off = runScenario(false, false, build, noPoke, Ticks);
  auto on = runScenario(true, false, build, noPoke, Ticks);

  expectTracesEqual(off, on);
  EXPECT_EQ(on.last()[0].value, (int64_t)Ticks) << "with the wake restored ON runs every tick";
}

// VALIDATE/SHADOW MODE (Part 1): in validate mode every entity's update() still runs, and a
// would-be-dormant slate that mutates net state is flagged. The buggy missing-wake entity must
// trip the mismatch counter; a genuinely-idle entity must not.
TEST(EntityDormancy, ValidateModeFlagsMissingWake) {
  unsigned const Ticks = 200;
  auto noPoke = [](uint64_t, TestEntityList&) {};

  EntityDormancy::mismatches.store(0, std::memory_order_relaxed);
  auto buggy = [](WorldServer& ws) {
    auto e = make_shared<DormancyTestEntity>(Vec2F(220.0f, 100.0f));
    e->m_behavior = [](DormancyTestEntity& e, uint64_t) { e.bumpState(); }; // mutates while "dormant"
    e->m_horizon = [](uint64_t s) -> Maybe<uint64_t> { return s + 100000; };
    ws.addEntity(e);
    return TestEntityList{e};
  };
  runScenario(false, true, buggy, noPoke, Ticks); // validate-only (enabled=false)
  EXPECT_GT(EntityDormancy::mismatches.load(std::memory_order_relaxed), 0u)
      << "validate mode must flag a would-be-dormant entity that mutates net state";

  EntityDormancy::mismatches.store(0, std::memory_order_relaxed);
  auto idle = [](WorldServer& ws) {
    auto e = make_shared<DormancyTestEntity>(Vec2F(230.0f, 100.0f));
    e->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; }; // sleeps, never mutates
    ws.addEntity(e);
    return TestEntityList{e};
  };
  runScenario(false, true, idle, noPoke, Ticks);
  EXPECT_EQ(EntityDormancy::mismatches.load(std::memory_order_relaxed), 0u)
      << "validate mode must stay quiet for a genuinely no-op dormant slate";
}

// HORIZON STATE MACHINE: drive three entities through {no-horizon->sleep,
// far-horizon->scheduled-wake-fires-on-time, next-tick->stay-awake} in one ON server and
// assert exactly which steps each entity's update() ran on.
TEST(EntityDormancy, HorizonStateMachine) {
  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  EntityDormancy::enabled.store(true, std::memory_order_relaxed);
  EntityDormancy::validate.store(false, std::memory_order_relaxed);

  // no-horizon -> sleeps after the single initial wake.
  auto idle = make_shared<DormancyTestEntity>(Vec2F(300.0f, 100.0f));
  idle->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; };

  // far horizon (+5) -> scheduled wake fires on time: runs at steps 1, 6, 11, ...
  auto sched = make_shared<DormancyTestEntity>(Vec2F(310.0f, 100.0f));
  sched->m_horizon = [](uint64_t s) -> Maybe<uint64_t> { return s + 5; };

  // next-tick horizon -> stays awake every tick.
  auto awake = make_shared<DormancyTestEntity>(Vec2F(320.0f, 100.0f));
  awake->m_horizon = [](uint64_t s) -> Maybe<uint64_t> { return s + 1; };

  ws->addEntity(idle);
  ws->addEntity(sched);
  ws->addEntity(awake);
  // Cleanup (entities + gates) runs even if an assertion below fails (see guard).
  DormancyTestGuard guard{ws.get(), {idle->entityId(), sched->entityId(), awake->entityId()}};

  unsigned const Ticks = 50; // steps 1..50
  for (unsigned t = 0; t < Ticks; ++t)
    ws->update(Dt);

  EXPECT_EQ(idle->m_updateCount, 1u) << "no-horizon entity must sleep after its initial wake";
  EXPECT_EQ(idle->m_lastUpdateStep, 1u);

  // Scheduled wakes at steps 1,6,11,...,46 within 50 ticks = 10 runs, last at step 46.
  EXPECT_EQ(sched->m_updateCount, 10u) << "scheduled wake must fire on time each horizon";
  EXPECT_EQ(sched->m_lastUpdateStep, 46u);

  EXPECT_EQ(awake->m_updateCount, (uint64_t)Ticks) << "next-tick horizon must stay awake every tick";
  EXPECT_EQ(awake->m_lastUpdateStep, (uint64_t)Ticks);
}

// RECYCLED ID: a removed entity's stale scheduled-wake bucket entry must not corrupt a NEW
// entity that later recycles its EntityId. (Promotion's liveness filter means the worst case
// is a harmless redundant no-op wake of the new entity, never wrong state — the dormancy
// invariant is "never UNDER-wake"; an over-wake is safe.)
TEST(EntityDormancy, RecycledIdNoStaleCarryOver) {
  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  EntityDormancy::enabled.store(true, std::memory_order_relaxed);
  EntityDormancy::validate.store(false, std::memory_order_relaxed);

  // A: schedules a wake at a fixed far step (100), then we remove it well before then.
  auto a = make_shared<DormancyTestEntity>(Vec2F(400.0f, 100.0f));
  a->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return 100; };
  ws->addEntity(a);
  EntityId recycledId = a->entityId();
  // Cleanup runs even if the ASSERT_EQ below fails: removes whatever currently
  // holds recycledId (A, then B after it recycles the id) and resets the gates.
  DormancyTestGuard guard{ws.get(), {recycledId}};

  for (unsigned t = 0; t < 10; ++t) // steps 1..10: A sleeps with a pending wake queued at 100
    ws->update(Dt);
  ws->removeEntity(recycledId, false); // stale m_scheduledWakes[100] entry is NOT scrubbed

  // B: idle (sleeps, never mutates), inserted at the RECYCLED id.
  auto b = make_shared<DormancyTestEntity>(Vec2F(410.0f, 100.0f));
  b->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; };
  b->m_behavior = [](DormancyTestEntity& e, uint64_t) { e.bumpState(); }; // would expose any spurious *state* change
  ws->addEntity(b, recycledId);
  ASSERT_EQ(b->entityId(), recycledId) << "id must actually be recycled for this test to mean anything";

  // Run well past step 100 so A's stale schedule would fire if it carried over to B.
  for (unsigned t = 0; t < 110; ++t)
    ws->update(Dt);

  EXPECT_TRUE((bool)ws->entity(recycledId)) << "B must still be alive";
  // B was woken once on insertion; bumpState ran then. A stale wake at step 100 would wake B
  // once more (a safe no-op for a real idle entity). We assert B's update count is BOUNDED
  // and small (initial wake +/- the at-most-one benign stale wake), never an unbounded leak.
  EXPECT_LE(b->m_updateCount, 2u) << "recycled id must not cause repeated/unbounded stale wakes";
  EXPECT_GE(b->m_updateCount, 1u);
}

// TELEMETRY (Part 2): the process-global skipped/ran counters must register dormancy activity
// while ON (they are the basis of the periodic dormancy-ratio log + the A/B measurement).
TEST(EntityDormancy, TelemetryCountersRegisterActivity) {
  unsigned const Ticks = 100;
  EntityDormancy::skipped.store(0, std::memory_order_relaxed);
  EntityDormancy::ran.store(0, std::memory_order_relaxed);

  auto build = [](WorldServer& ws) {
    auto e = make_shared<DormancyTestEntity>(Vec2F(500.0f, 100.0f));
    e->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; }; // sleeps -> mostly skipped
    ws.addEntity(e);
    return TestEntityList{e};
  };
  auto noPoke = [](uint64_t, TestEntityList&) {};
  runScenario(true, false, build, noPoke, Ticks);

  EXPECT_GE(EntityDormancy::ran.load(std::memory_order_relaxed), 1u) << "the initial wake should count as ran";
  EXPECT_GT(EntityDormancy::skipped.load(std::memory_order_relaxed), 0u)
      << "a sleeping entity should accumulate would-be-dormant (skipped) updates";
}

// REAL-ORACLE / DEFERRED-STORE (Task 6 fix): the tests above drive a SYNTHETIC
// stand-in whose netVersionLatestChange is a hand-bumped counter. These two drive
// a REAL vanilla Object end-to-end, proving the production
// Object::netVersionLatestChange() oracle works AND that the validate loop now
// flushes deferred NetElement stores BEFORE the after-capture.
//
// The hole this closes: deferred-store net state (Object::m_orientationIndexNetState,
// ContainerObject::m_itemsNetState) is written only at pump time via setNetStates().
// The real pump runs LATER (queueUpdatePackets, the client phase) than the validate
// after-capture, so before WOULD == after every tick and a missed-wake on these
// fields would NEVER be flagged. The fix pumps the entity in the validate branch
// before the after-capture. (WITHOUT the fix, the positive case below reads
// before == after and EntityDormancy::mismatches stays 0 — verified by reasoning:
// the orientation markChanged() would only fire in the later writeNetState, after
// the after-capture.)
//
// VERSION NOTE: latestChange = NetElementVersion::m_version at markChanged() time,
// and m_version only advances when writeNetState() emits a delta. A test
// WorldServer has no clients, so we drive writeNetState() ourselves each tick to
// mirror a monitoring client (exactly what advances the version in-game) — without
// it the version stays frozen at 0 and latestChange could never advance.

// POSITIVE: a real would-be-dormant Object whose DEFERRED orientation net state
// mutates every tick (via setOrientationIndex, which does NOT arm a wake) MUST be
// flagged by validate mode -> proves Fix #1 closed the pump-timing hole.
TEST(EntityDormancy, ValidateModeFlagsRealObjectDeferredStore) {
  Maybe<String> name = findSimpleDormantObject();
  ASSERT_TRUE((bool)name) << "no simple scriptless single-frame object found in loaded assets";
  auto cfg = Root::singleton().objectDatabase()->getConfig(*name);
  ASSERT_GE(cfg->orientations.size(), 1u);

  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  EntityDormancy::enabled.store(false, std::memory_order_relaxed);   // validate-only
  EntityDormancy::validate.store(true, std::memory_order_relaxed);
  EntityDormancy::mismatches.store(0, std::memory_order_relaxed);

  auto obj = make_shared<OrientationProbeObject>(cfg);
  obj->setTilePosition(Vec2I(40, 40));
  ws->addEntity(obj);
  obj->keepResident(); // keep the sector resident so the object is never unloaded
  DormancyTestGuard guard{ws.get(), {obj->entityId()}};

  // Lift m_version off 0 (full store increments), then advance it each tick below.
  uint64_t fromVer = obj->writeNetState(0).second;

  unsigned const Ticks = 80; // the object sleeps between ~30-step liquid-check wakes
  for (unsigned t = 0; t < Ticks; ++t) {
    // Mutate the deferred orientation net state WITHOUT a wake. 0 <-> NPos both
    // keep currentOrientation() safe (NPos -> none; 0 -> a real orientation) and
    // differ every tick, so each pump's setNetStates() records a real change.
    obj->pokeOrientation(t % 2 == 0 ? 0 : NPos);
    ws->update(Dt); // validate loop pumps deferred stores before the after-capture
    fromVer = obj->writeNetState(fromVer).second; // mirror a monitoring client
  }

  EXPECT_GT(EntityDormancy::mismatches.load(std::memory_order_relaxed), 0u)
      << "validate mode must flag a would-be-dormant REAL Object whose deferred "
         "m_orientationIndexNetState mutates (proves the pump-timing hole is closed)";
}

// IDLE NEGATIVE: the SAME real Object, same loop and live (advancing) net version,
// but it does nothing (no orientation poke) while would-be-dormant -> NO mismatch.
// Isolates the orientation mutation as the sole cause above (no false positive).
TEST(EntityDormancy, ValidateModeQuietForIdleRealObject) {
  Maybe<String> name = findSimpleDormantObject();
  ASSERT_TRUE((bool)name) << "no simple scriptless single-frame object found in loaded assets";
  auto cfg = Root::singleton().objectDatabase()->getConfig(*name);

  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  EntityDormancy::enabled.store(false, std::memory_order_relaxed);
  EntityDormancy::validate.store(true, std::memory_order_relaxed);
  EntityDormancy::mismatches.store(0, std::memory_order_relaxed);

  auto obj = make_shared<OrientationProbeObject>(cfg);
  obj->setTilePosition(Vec2I(60, 60));
  ws->addEntity(obj);
  obj->keepResident();
  DormancyTestGuard guard{ws.get(), {obj->entityId()}};

  uint64_t fromVer = obj->writeNetState(0).second;
  unsigned const Ticks = 80;
  for (unsigned t = 0; t < Ticks; ++t) {
    ws->update(Dt);
    fromVer = obj->writeNetState(fromVer).second;
  }

  EXPECT_EQ(EntityDormancy::mismatches.load(std::memory_order_relaxed), 0u)
      << "validate mode must stay quiet for a genuinely no-op dormant real Object";
}

// MAX-SLEEP CAP BACKSTOP (Task 7): an entity with a {} horizon (no self-scheduled work)
// that is NEVER externally woken would, without the cap, sleep forever after its single
// initial wake (cf. HorizonStateMachine's idle case: updateCount == 1). The staggered
// max-sleep cap bounds that indefinite sleep: the entity MUST be re-woken within
// m_dormancyMaxSleepSteps, the cap-wake lands NEAR the cap (inside the per-entity stagger
// window just below it), and it does NOT run every tick.
TEST(EntityDormancy, MaxSleepCapBackstopWakesIndefiniteSleeper) {
  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  EntityDormancy::enabled.store(true, std::memory_order_relaxed);
  EntityDormancy::validate.store(false, std::memory_order_relaxed);

  // Drive the cap via the same WorldServer (its configured cap, default 10s == 600 steps).
  uint64_t const cap = ws->dormancyMaxSleepSteps();
  ASSERT_GE(cap, 2u) << "cap must be a genuine future step (> currentStep+1) to mean anything";
  uint64_t const staggerWindow = std::min<uint64_t>(cap, 64);

  // {} horizon, never externally woken -> would sleep forever without the cap.
  auto sleeper = make_shared<DormancyTestEntity>(Vec2F(600.0f, 100.0f));
  sleeper->m_horizon = [](uint64_t) -> Maybe<uint64_t> { return {}; };
  ws->addEntity(sleeper);
  DormancyTestGuard guard{ws.get(), {sleeper->entityId()}};

  // The scheduler stamps the wake at currentStep + cap - (id % staggerWindow). The initial
  // wake runs at step 1 (see HorizonStateMachine), so the first cap-wake lands at exactly
  // 1 + cap - (id % staggerWindow): within the stagger window just below 1 + cap.
  uint64_t const id = (uint64_t)sleeper->entityId();
  uint64_t const expectedCapWake = 1 + cap - (id % staggerWindow);

  // Run just past the cap so the cap-wake MUST have fired, but not far enough for a second
  // cap-wake (the next would be ~cap steps later) — so the count cleanly distinguishes a
  // single staggered cap-wake from an every-tick run.
  unsigned const Ticks = (unsigned)(cap + 1);
  for (unsigned t = 0; t < Ticks; ++t)
    ws->update(Dt);

  // Does NOT sleep forever: re-woken after its initial wake, and within the cap latency.
  EXPECT_GE(sleeper->m_updateCount, 2u) << "indefinite sleeper must be re-woken by the cap, not freeze forever";
  EXPECT_EQ(sleeper->m_lastUpdateStep, expectedCapWake) << "cap-wake must land at the staggered cap step";
  EXPECT_LE(sleeper->m_lastUpdateStep - 1, cap) << "must be re-woken within m_dormancyMaxSleepSteps of the prior wake";

  // Near-cap, NOT every tick: the cap-wake is inside the stagger window just below the cap
  // (so the entity slept ~cap steps), and the total update count is tiny — an every-tick
  // bug over (cap + 1) ticks would be ~cap updates.
  EXPECT_GT(expectedCapWake, cap - staggerWindow) << "cap-wake must be within the stagger window below the cap";
  EXPECT_LE(expectedCapWake, cap + 1);
  EXPECT_LT(sleeper->m_updateCount, cap) << "cap-wake must not run every tick (slept ~cap steps between wakes)";
}

// AWAKE-SET PRUNE (Task 8): stale ids must not accumulate in m_awakeEntities when entities
// leave the world via a path that BYPASSES WorldServer::removeEntity. The real bypass is
// WorldStorage sector-unload, which removes entities by calling m_entityMap->removeEntity
// DIRECTLY (StarWorldStorage.cpp) — WorldServer::removeEntity (which does the O(1)
// m_awakeEntities.remove) is never invoked, so the awake-set keeps the dead ids forever
// (harmless to the tick loop, which iterates live entities + only tests contains(), but an
// unbounded leak that skews the awake/live telemetry + dormancy ratio). This drives the REAL
// public unload path (WorldServer::unloadAll -> WorldStorage::unloadAll -> unloadSectorToLevel
// -> m_entityMap->removeEntity) on entities that genuinely went through the sector machinery,
// reproduces the leak, then proves the periodic prune in update() clears it.
TEST(EntityDormancy, PruneStaleAwakeIdsAfterUnloadBypass) {
  auto ws = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  ws->setSpawningEnabled(false);
  EntityDormancy::enabled.store(true, std::memory_order_relaxed);
  EntityDormancy::validate.store(false, std::memory_order_relaxed);
  DormancyTestGuard guard{ws.get(), {}}; // resets the gates on every exit path

  // Load the sector our entities live in so the sector-unload machinery actually owns them
  // (unloadAll iterates loaded sectors; an entity in an unloaded sector is never queried).
  ws->generateRegion(RectI(690, 690, 730, 730));

  // unloadAll(force=true) overrides the keepAlive guard; NON-persistent (default) routes them
  // to the bypass-remove branch (m_entityMap->removeEntity + destructEntity) with NO
  // serialization (a non-factory synthetic type can't be serialized). next-tick (s+1) horizon keeps each
  // one resident in the awake-set every tick, so removing it leaves a genuine stale id.
  TestEntityList ents;
  List<EntityId> ids;
  for (int i = 0; i < 3; ++i) {
    auto e = make_shared<DormancyTestEntity>(Vec2F(700.5f + i, 700.5f));
    e->m_horizon = [](uint64_t s) -> Maybe<uint64_t> { return s + 1; }; // stay awake every tick
    ws->addEntity(e);
    ents.append(e);
    ids.append(e->entityId());
  }

  // A few ticks: the entities are alive and held in the awake-set.
  for (unsigned t = 0; t < 5; ++t)
    ws->update(Dt);
  for (EntityId id : ids) {
    ASSERT_TRUE((bool)ws->entity(id)) << "entity must be live before the unload";
    ASSERT_TRUE(ws->isEntityAwake(id)) << "next-tick-horizon entity must be in the awake-set";
  }
  size_t const awakeBefore = ws->awakeEntityCount();
  ASSERT_GE(awakeBefore, ids.size());

  // REAL BYPASS: full production unload. Removes our entities via m_entityMap->removeEntity
  // directly, never touching WorldServer::removeEntity -> the awake-set is NOT cleaned here.
  ws->unloadAll(true);

  // The leak: each id is gone from the world but STILL in the awake-set, and the awake count
  // now exceeds the live entity count (exactly the telemetry skew the prune fixes).
  size_t live = 0;
  ws->forAllEntities([&live](EntityPtr const&) { ++live; });
  for (EntityId id : ids) {
    ASSERT_FALSE((bool)ws->entity(id)) << "unloadAll must have removed the entity via the bypass";
    EXPECT_TRUE(ws->isEntityAwake(id)) << "stale id leaked: bypass removal left it in the awake-set";
  }
  EXPECT_GT(ws->awakeEntityCount(), live) << "awake-set must outgrow live entities (the leak)";

  // Tick past the prune cadence (the prune runs once per ~600 ticks while a dormancy gate is
  // on; m_dormancyStatTick is <= 5 here, so 600 more ticks guarantees at least one fire).
  for (unsigned t = 0; t < 600; ++t)
    ws->update(Dt);

  // The fix: every stale id is gone and the awake-set tracks the live entity count again.
  live = 0;
  ws->forAllEntities([&live](EntityPtr const&) { ++live; });
  for (EntityId id : ids)
    EXPECT_FALSE(ws->isEntityAwake(id)) << "prune must drop the dead id from the awake-set";
  EXPECT_EQ(ws->awakeEntityCount(), live) << "after the prune the awake-set must track live entities";
}
