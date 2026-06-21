#include "StarEntityMap.hpp"
#include "StarEntity.hpp"
#include "StarGameTypes.hpp"
#include "StarRect.hpp"
#include "StarMap.hpp"
#include "StarInspectableEntity.hpp"
#include "StarScriptedEntity.hpp"
#include "StarPhysicsEntity.hpp"

#include "gtest/gtest.h"

using namespace Star;

// Correctness gate for the EntityMap spatial-query path (Lever #1: templated
// EntityMap::forEachEntity/findEntity; Lever #5: World query callbacks by
// const&). This is the EntityMap-layer sibling of spatial_hash_test.cpp's
// RandomizedOracle -- one level up, over real Entity objects in a real EntityMap.
//
// It pins the OBSERVABLE query contract: forEachEntity(box) yields exactly the
// SET of entities whose meta bound box intersects the query box, with NO
// duplicates (even for multi-sector entities). The oracle is order-agnostic
// (both sides are sorted before comparison) and implementation-independent
// (compared against a brute-force scan), so it gates the template/const& change
// as behavior-equivalent and will keep gating the later forEach levers (#2/#3/#4/#7).
//
// EntityMap reads only position()/metaBoundBox() for spatial indexing
// (addEntity / updateAllEntities store splitRect(metaBoundBox, position) as the
// entity's spatial rect). We keep all coordinates well inside the world interior
// (far from the x=0 / x=width wrap seam, y within [0,height)), so WorldGeometry::
// splitRect is a plain translation and the stored rect is exactly
// metaBoundBox().translated(position()). That lets the brute-force oracle compare
// plain RectF intersections (RectF::intersects, includeEdges=true -- the very
// call SpatialHash2D::forEach uses), mirroring spatial_hash_test.cpp.

namespace {

// Minimal synthetic master entity (cf. entity_dormancy_test.cpp's
// DormancyTestEntity), but here we drive a bare EntityMap directly.
class QueryTestEntity : public Entity {
public:
  QueryTestEntity(EntityId id, RectF const& worldRect) {
    setRect(worldRect);
    // EntityMap + our callbacks never dereference world(); a non-null sentinel
    // satisfies Entity::init's null-world guard -- the only way to assign the
    // private base entityId without standing up a whole (abstract) World.
    init(reinterpret_cast<World*>(0x1), id, EntityMode::Master);
  }

  EntityType entityType() const override { return EntityType::Object; }
  Vec2F position() const override { return m_position; }
  RectF metaBoundBox() const override { return m_relBox; }

  // Set the entity's absolute world rect, decomposed into a position + a
  // position-relative meta bound box (the form EntityMap consumes).
  void setRect(RectF const& worldRect) {
    m_position = worldRect.min();
    m_relBox = RectF(Vec2F(), worldRect.size());
  }

  Vec2F m_position;
  RectF m_relBox;
};

typedef shared_ptr<QueryTestEntity> QueryTestEntityPtr;

// World big enough that the interior test window never nears the x-wrap seam.
Vec2U const TestWorldSize(2048, 2048);

// The absolute world rect an entity currently occupies (what the oracle scans).
RectF worldRectOf(QueryTestEntityPtr const& e) {
  return e->metaBoundBox().translated(e->position());
}

// Brute-force oracle: sorted set of entity ids whose world rect intersects q.
List<EntityId> oracleQuery(Map<EntityId, QueryTestEntityPtr> const& world, RectF const& q) {
  List<EntityId> out;
  for (auto const& p : world) {
    if (worldRectOf(p.second).intersects(q))
      out.append(p.first);
  }
  sort(out);
  return out;
}

// Query the EntityMap via forEachEntity, asserting NO duplicate ids (the dedup
// contract), returned sorted for comparison against the oracle.
List<EntityId> mapQuery(EntityMap const& map, RectF const& q) {
  List<EntityId> v;
  map.forEachEntity(q, [&v](EntityPtr const& e) { v.append(e->entityId()); });
  sort(v);
  for (size_t i = 1; i < v.size(); ++i)
    EXPECT_NE(v[i], v[i - 1]) << "forEachEntity returned a duplicate entity " << v[i];
  return v;
}

QueryTestEntityPtr addEntityAt(EntityMap& map, RectF const& worldRect) {
  EntityId id = map.reserveEntityId();
  auto e = make_shared<QueryTestEntity>(id, worldRect);
  map.addEntity(e);
  return e;
}

// Synthetic master entity that derives the real interface mixins, so that a
// dynamic_cast<InterfaceT> genuinely succeeds and the asX() override returns
// `this` -- the positive case the de-RTTI equivalence test needs. Plain
// QueryTestEntity (derives only Entity) is the negative case. Entity is a
// virtual base here (as in production), shared with each mixin's virtual Entity.
class QueryMixinEntity
  : public virtual Entity,
    public virtual InspectableEntity,
    public virtual ScriptedEntity,
    public virtual PhysicsEntity {
public:
  QueryMixinEntity(EntityId id, RectF const& worldRect) {
    m_position = worldRect.min();
    m_relBox = RectF(Vec2F(), worldRect.size());
    init(reinterpret_cast<World*>(0x1), id, EntityMode::Master);
  }

  EntityType entityType() const override { return EntityType::Object; }
  Vec2F position() const override { return m_position; }
  RectF metaBoundBox() const override { return m_relBox; }

  // ScriptedEntity's only pure virtuals; never called by the query path.
  Maybe<LuaValue> callScript(String const&, LuaVariadic<LuaValue> const&) override { return {}; }
  Maybe<LuaValue> evalScript(String const&) override { return {}; }

  Vec2F m_position;
  RectF m_relBox;
};

typedef shared_ptr<QueryMixinEntity> QueryMixinEntityPtr;

}  // namespace

// A single entity spanning a 3x3 block of 16-unit sectors must be returned
// exactly once (cross-sector dedup), and only by overlapping queries.
TEST(EntityMap, MultiSectorDedup) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  auto e = addEntityAt(map, RectF(512, 512, 552, 552));  // 40x40 -> ~3x3 sectors
  EntityId id = e->entityId();

  EXPECT_EQ(mapQuery(map, RectF(512, 512, 552, 552)), List<EntityId>{id});
  EXPECT_EQ(mapQuery(map, RectF(520, 520, 521, 521)), List<EntityId>{id});  // interior point
  EXPECT_TRUE(mapQuery(map, RectF(600, 600, 610, 610)).empty());           // disjoint
}

// Completeness + boundary behavior across sector lines, validated against the
// brute-force oracle so the expectations can't be silently mis-hardcoded.
TEST(EntityMap, CompletenessAndBoundary) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  Map<EntityId, QueryTestEntityPtr> world;
  auto add = [&](RectF r) { auto e = addEntityAt(map, r); world[e->entityId()] = e; return e; };

  add(RectF(512, 512, 516, 516));    // sector (32,32)
  add(RectF(527, 527, 529, 529));    // straddles sector (32,32)/(33,33)
  add(RectF(700, 700, 704, 704));    // far away

  for (RectF q : {RectF(525, 525, 531, 531),     // only the straddling entity
                  RectF(512, 512, 540, 540),     // the two near entities
                  RectF(560, 560, 580, 580),     // empty space
                  RectF(517, 517, 519, 519),     // just past the first entity's max edge
                  RectF(400, 400, 800, 800)}) {   // everything
    ASSERT_EQ(mapQuery(map, q), oracleQuery(world, q));
  }
}

// Moving an entity (via updateAllEntities, the production re-index path) and
// removing one must keep the spatial index in lockstep with the oracle.
TEST(EntityMap, AddMoveRemove) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  Map<EntityId, QueryTestEntityPtr> world;
  auto add = [&](RectF r) { auto e = addEntityAt(map, r); world[e->entityId()] = e; return e; };

  auto e1 = add(RectF(500, 500, 504, 504));
  auto e2 = add(RectF(800, 800, 805, 805));
  EXPECT_EQ(map.size(), 2u);
  ASSERT_EQ(mapQuery(map, RectF(498, 498, 506, 506)), List<EntityId>{e1->entityId()});

  // Move e1 far; the old region must go empty only after updateAllEntities reindexes.
  e1->setRect(RectF(1200, 1200, 1204, 1204));
  map.updateAllEntities();
  EXPECT_TRUE(mapQuery(map, RectF(498, 498, 506, 506)).empty());
  ASSERT_EQ(mapQuery(map, RectF(1198, 1198, 1206, 1206)), List<EntityId>{e1->entityId()});

  // Remove e2.
  map.removeEntity(e2->entityId());
  world.remove(e2->entityId());
  EXPECT_EQ(map.size(), 1u);
  EXPECT_TRUE(mapQuery(map, RectF(798, 798, 807, 807)).empty());

  ASSERT_EQ(mapQuery(map, RectF(0, 0, 2000, 2000)), oracleQuery(world, RectF(0, 0, 2000, 2000)));
}

// Lever #3 (interior-cell box-test skip) classification gate at the EntityMap
// layer. Mirrors spatial_hash_test.cpp's InteriorCellSkip, shifted by +512 (=
// cell boundary 32*16) into the world interior. The crux case: entity 4 is
// registered in a perimeter cell via getSectors' ceil over-reach yet does NOT
// intersect a query whose edge stops just short -- the perimeter test must
// reject it (a misclassification as interior would leak it in). All checked
// against the brute-force oracle. (16-unit sectors; coords are cell-aligned so
// the query edges land exactly on cell boundaries.)
TEST(EntityMap, InteriorCellSkip) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  Map<EntityId, QueryTestEntityPtr> world;
  auto add = [&](RectF r) { auto e = addEntityAt(map, r); world[e->entityId()] = e; return e; };

  add(RectF(532, 532, 540, 540));        // interior cell (33,33) for a [512,592) query
  add(RectF(542, 542, 582, 582));        // straddles interior cells AND a perimeter cell
  add(RectF(514, 514, 522, 522));        // perimeter cell (32,32)
  add(RectF(552, 591.5f, 560, 592));     // ceil-over-reach into perimeter cell y; 591.5..592 band

  for (RectF q : {RectF(512, 512, 592, 592),   // interior 33..35; entity 4 touches yMax==592
                  RectF(512, 512, 592, 591),   // yMax==591: entity 4's perimeter cell must REJECT it
                  RectF(528, 528, 576, 576),   // edges exactly on cell boundaries (conservative perimeter)
                  RectF(530, 530, 542, 542),   // single-cell span: no interior cells
                  RectF(545, 545, 578, 578)}) { // deep interior, excludes the perimeter-only entities
    ASSERT_EQ(mapQuery(map, q), oracleQuery(world, q)) << "interior/perimeter mismatch for query " << q;
  }
}

// findEntity returns SOME matching entity (order is implementation-defined) or
// null; gates the templated findEntity path.
TEST(EntityMap, FindEntity) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  auto matchAny = [](EntityPtr const&) { return true; };
  auto matchNone = [](EntityPtr const&) { return false; };

  // Empty map -> null.
  EXPECT_FALSE((bool)map.findEntity(RectF(500, 500, 510, 510), matchAny));

  auto e = addEntityAt(map, RectF(520, 520, 524, 524));

  // Intersecting box + accepting filter -> the (only) matching entity.
  auto found = map.findEntity(RectF(518, 518, 526, 526), matchAny);
  ASSERT_TRUE((bool)found);
  EXPECT_EQ(found->entityId(), e->entityId());

  // Intersecting box + rejecting filter -> null.
  EXPECT_FALSE((bool)map.findEntity(RectF(518, 518, 526, 526), matchNone));
  // Disjoint box -> null.
  EXPECT_FALSE((bool)map.findEntity(RectF(600, 600, 610, 610), matchAny));
}

// The strongest gate: randomized add/move/remove churn compared against a
// brute-force oracle every step (cf. spatial_hash_test.cpp's RandomizedOracle).
// Order-agnostic + implementation-independent, so it gates Lever #1/#5 and the
// later forEach levers regardless of internal ordering or sector size.
TEST(EntityMap, RandomizedOracle) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  Map<EntityId, QueryTestEntityPtr> world;  // oracle: id -> entity
  Map<int, QueryTestEntityPtr> slots;       // stable slot -> current entity (for move/remove)

  uint64_t s = 0x9E3779B97F4A7C15ull;
  auto next = [&]() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); };
  auto frand = [&](float lo, float hi) { return lo + (hi - lo) * (next() / 2147483648.0f); };

  float const Base = 500.0f;  // interior window origin, far from the wrap seam
  auto randRect = [&]() {
    float x = Base + frand(0.0f, 120.0f), y = Base + frand(0.0f, 120.0f);
    // Mostly small entities; occasionally a multi-sector (>=3x3 cell) box to
    // stress cross-sector dedup at the EntityMap layer.
    float maxSize = (next() % 8 == 0) ? 60.0f : 10.0f;
    float w = frand(0.5f, maxSize), h = frand(0.5f, maxSize);
    return RectF(x, y, x + w, y + h);
  };

  int const SlotSpace = 32;
  for (int step = 0; step < 6000; ++step) {
    int slot = (int)(next() % SlotSpace);
    uint32_t op = next() % 10;
    auto it = slots.find(slot);
    bool present = it != slots.end();

    if (op < 2) {
      // remove
      if (present) {
        EntityId id = it->second->entityId();
        map.removeEntity(id);
        world.remove(id);
        slots.erase(slot);
      }
    } else if (present && op < 5) {
      // move existing (production re-index path)
      it->second->setRect(randRect());
      map.updateAllEntities();
    } else {
      // insert (replace whatever occupies the slot, keeping one entity per slot)
      if (present) {
        EntityId id = it->second->entityId();
        map.removeEntity(id);
        world.remove(id);
        slots.erase(slot);
      }
      auto e = addEntityAt(map, randRect());
      world[e->entityId()] = e;
      slots[slot] = e;
    }

    RectF q = randRect();
    ASSERT_EQ(mapQuery(map, q), oracleQuery(world, q)) << "mismatch at step " << step;
  }

  // Final full-window sweep.
  RectF full(Base - 50, Base - 50, Base + 250, Base + 250);
  ASSERT_EQ(mapQuery(map, full), oracleQuery(world, full));
}

// Lever #4 (stamp dedup) re-entrancy gate at the EntityMap layer: a
// forEachEntity callback that issues a NESTED forEachEntity over an overlapping
// region advances the shared per-query stamp. The outer dedup stays correct
// because forEach is two-phase -- it finishes gathering before dispatching any
// callback, so the nested query cannot perturb the outer gather (forEach also
// snapshots its stamp into a local as defensive future-proofing). Both passes
// must stay complete and duplicate-free.
TEST(EntityMap, ReentrantForEachEntity) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  Map<EntityId, QueryTestEntityPtr> world;
  auto add = [&](RectF r) { auto e = addEntityAt(map, r); world[e->entityId()] = e; return e; };

  // Overlapping multi-sector entities so outer + nested both gather each via
  // several cells.
  add(RectF(512, 512, 552, 552));  // ~3x3 sectors
  add(RectF(520, 520, 580, 580));  // ~4x4 sectors, overlaps
  add(RectF(530, 530, 534, 534));  // small interior
  add(RectF(540, 540, 600, 600));  // multi-sector

  RectF q(500, 500, 620, 620);  // covers all
  List<EntityId> const expected = oracleQuery(world, q);

  List<EntityId> outerSeen;
  map.forEachEntity(q, [&](EntityPtr const& e) {
      outerSeen.append(e->entityId());
      List<EntityId> nestedSeen;
      map.forEachEntity(q, [&](EntityPtr const& ne) { nestedSeen.append(ne->entityId()); });
      sort(nestedSeen);
      for (size_t i = 1; i < nestedSeen.size(); ++i)
        EXPECT_NE(nestedSeen[i], nestedSeen[i - 1]) << "nested forEachEntity duplicated " << nestedSeen[i];
      EXPECT_EQ(nestedSeen, expected) << "nested forEachEntity incomplete";
    });

  sort(outerSeen);
  for (size_t i = 1; i < outerSeen.size(); ++i)
    EXPECT_NE(outerSeen[i], outerSeen[i - 1]) << "outer forEachEntity duplicated " << outerSeen[i] << " after nested re-entrancy";
  EXPECT_EQ(outerSeen, expected) << "outer forEachEntity lost entries to nested re-entrancy";
}

// Behavior-equivalence gate for the entity-query de-RTTI (entityCast<T>): the
// per-candidate downcast was switched from dynamic_pointer_cast (as<T>) to the
// asX() virtual accessors. This pins entityCast<T> as selecting the EXACT same
// objects (including nulls) dynamic_pointer_cast does, and that EntityMap::query<T>
// (which now uses entityCast internally) returns the identical result.
//
// QueryMixinEntity derives the real interface mixins so dynamic_cast<T> genuinely
// succeeds and the accessor override returns `this`; plain QueryTestEntity derives
// none of them, exercising the null path. The concrete-leaf accessors (asObject/
// asMonster/...) are one-line `return this` overrides validated structurally and by
// the compiler's return-type check on the EntityDowncast wiring -- instantiating
// real Objects/Monsters needs a full Root + databases and is out of scope here.
TEST(EntityMap, VirtualAccessorEquivalence) {
  EntityMap map(TestWorldSize, MinServerEntityId, MaxServerEntityId);
  Map<EntityId, EntityPtr> world;
  float const Base = 500.0f;

  auto addMixin = [&](RectF r) {
    EntityId id = map.reserveEntityId();
    auto e = make_shared<QueryMixinEntity>(id, r);
    map.addEntity(e);
    world[id] = e;
    return e;
  };
  auto addPlain = [&](RectF r) {
    auto e = addEntityAt(map, r);
    world[e->entityId()] = e;
    return e;
  };

  // Interleave the two kinds across an interior window.
  int const MixinCount = 10;
  for (int i = 0; i < MixinCount; ++i) {
    addMixin(RectF(Base + i * 5.0f, Base + i * 3.0f, Base + i * 5.0f + 4, Base + i * 3.0f + 4));
    addPlain(RectF(Base + i * 4.0f + 2, Base + i * 6.0f, Base + i * 4.0f + 6, Base + i * 6.0f + 5));
  }

  // (A) Decisive: entityCast<T> picks the EXACT same object (incl. null) as
  // as<T> = dynamic_pointer_cast<T>, for every entity and every accessored T.
  size_t mixinHits = 0;
  for (auto const& p : world) {
    EntityPtr const& e = p.second;
    EXPECT_EQ(entityCast<InspectableEntity>(e).get(), as<InspectableEntity>(e).get());
    EXPECT_EQ(entityCast<ScriptedEntity>(e).get(), as<ScriptedEntity>(e).get());
    EXPECT_EQ(entityCast<PhysicsEntity>(e).get(), as<PhysicsEntity>(e).get());
    EXPECT_EQ((bool)entityCast<InspectableEntity>(e), (bool)as<InspectableEntity>(e));
    // Identity/upcast fast-path: entityCast<Entity> is exactly the same pointer.
    EXPECT_EQ(entityCast<Entity>(e).get(), e.get());
    if (as<InspectableEntity>(e))
      ++mixinHits;
  }
  EXPECT_EQ(mixinHits, (size_t)MixinCount) << "expected the mixin entities to dynamic_cast to InspectableEntity";

  // (D) null in -> empty out (matches as<>(null)).
  EXPECT_FALSE((bool)entityCast<InspectableEntity>(EntityPtr{}));
  EXPECT_FALSE((bool)entityCast<ScriptedEntity>(EntityPtr{}));
  EXPECT_FALSE((bool)entityCast<Entity>(EntityPtr{}));

  // (B) End-to-end: EntityMap::query<T> (now internally entityCast) returns the
  // IDENTICAL id list AND order as a hand-rolled as<T> filter over the same
  // entityQuery traversal. No sort -- both walk identical order, so raw equality
  // pins membership and ordering.
  for (RectF q : {RectF(Base - 20, Base - 20, Base + 90, Base + 90),  // mixed
                  RectF(Base, Base, Base + 20, Base + 20),            // partial
                  RectF(Base + 300, Base + 300, Base + 360, Base + 360)}) {  // empty
    List<EntityId> inspTemplate, inspDynamic, scriptTemplate, scriptDynamic;
    for (auto const& x : map.query<InspectableEntity>(q))
      inspTemplate.append(x->entityId());
    for (auto const& x : map.query<ScriptedEntity>(q))
      scriptTemplate.append(x->entityId());
    for (auto const& e : map.entityQuery(q)) {
      if (as<InspectableEntity>(e))
        inspDynamic.append(e->entityId());
      if (as<ScriptedEntity>(e))
        scriptDynamic.append(e->entityId());
    }
    EXPECT_EQ(inspTemplate, inspDynamic) << "query<InspectableEntity> diverged from dynamic_cast for " << q;
    EXPECT_EQ(scriptTemplate, scriptDynamic) << "query<ScriptedEntity> diverged from dynamic_cast for " << q;
  }
}
