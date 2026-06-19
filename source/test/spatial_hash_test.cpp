#include "StarSpatialHash2D.hpp"
#include "StarRect.hpp"
#include "StarMap.hpp"

#include "gtest/gtest.h"

using namespace Star;

// Regression coverage for SpatialHash2D (the EntityMap spatial index). The
// per-sector membership container and query/dedup path had ZERO direct test
// coverage; a membership bug would be silent (missed/duplicate entities ->
// lost collision/render/damage). These tests pin the OBSERVABLE query
// semantics so the SectorEntrySet container can be refactored (Lever #12a)
// with a real regression gate. value == key throughout, so a query result is
// just the set of keys whose any rect intersects the query rect.
//
// NOTE: rects are always passed via the RectCollection (List<RectF>) overload
// of set() -- the path EntityMap actually uses. The single-rect convenience
// overloads set(Key, Rect[, Value]) / set(Key, Coord[, Value]) are dormant and
// currently infinite-recurse (their `set(key, {rect}, ...)` braced-init-list
// can't deduce the RectCollection template parameter, so it re-selects the
// single-rect overload). RectF (== Box<float,2> == the hash's Rect) is used
// directly rather than a local typedef (`Rect` would collide with Star::Rect).

namespace {
  typedef SpatialHash2D<int, float, int> TestHash;

  // One-rect RectCollection helper (avoids the recursive single-rect overload).
  List<RectF> rc(float x0, float y0, float x1, float y1) {
    List<RectF> l;
    l.append(RectF(x0, y0, x1, y1));
    return l;
  }

  // Brute-force oracle: the set of keys with at least one rect intersecting q.
  List<int> oracleQuery(Map<int, List<RectF>> const& world, RectF const& q) {
    List<int> out;
    for (auto const& p : world) {
      for (auto const& r : p.second) {
        if (!r.isNull() && r.intersects(q)) {
          out.append(p.first);
          break;
        }
      }
    }
    sort(out);
    return out;
  }

  // Query the hash and assert the result contains NO duplicates (the core
  // dedup contract), returning it sorted for comparison against the oracle.
  List<int> hashQuery(TestHash const& h, RectF const& q) {
    List<int> v = h.queryValues(q);
    sort(v);
    for (size_t i = 1; i < v.size(); ++i)
      EXPECT_NE(v[i], v[i - 1]) << "queryValues returned a duplicate value " << v[i];
    return v;
  }
}

TEST(SpatialHash2D, MultiSectorDedup) {
  TestHash hash(16.0f);
  // One entity spanning a 3x3 block of 16-unit sectors.
  hash.set(1, rc(0, 0, 40, 40), 1);
  // A query overlapping many of its sectors must return it exactly once.
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 40, 40)), List<int>{1});
  EXPECT_EQ(hashQuery(hash, RectF(8, 8, 9, 9)), List<int>{1});
}

TEST(SpatialHash2D, SharedSectorMultiRect) {
  TestHash hash(16.0f);
  // Two rects of the SAME entity that map into the SAME sector (the case an
  // unconditional flat append could double-insert). Must still dedup to one.
  hash.set(7, List<RectF>{RectF(1, 1, 3, 3), RectF(2, 2, 5, 5)}, 7);
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 8, 8)), List<int>{7});
}

TEST(SpatialHash2D, CompletenessAndBoundary) {
  TestHash hash(16.0f);
  hash.set(1, rc(0, 0, 4, 4), 1);          // sector (0,0)
  hash.set(2, rc(15, 15, 17, 17), 2);      // straddles (0,0)/(1,1) boundary
  hash.set(3, rc(100, 100, 104, 104), 3);  // far away

  // Query touching the boundary entity but not the far one.
  EXPECT_EQ(hashQuery(hash, RectF(14, 14, 18, 18)), (List<int>{2}));
  // Query covering the two near entities, excluding the far one.
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 20, 20)), (List<int>{1, 2}));
  // Query in empty space returns nothing.
  EXPECT_TRUE(hashQuery(hash, RectF(50, 50, 60, 60)).empty());
  // Non-intersecting adjacency: a query just past entity 1's max edge.
  EXPECT_TRUE(hashQuery(hash, RectF(5, 5, 6, 6)).empty());
}

TEST(SpatialHash2D, RemoveAndPrune) {
  TestHash hash(16.0f);
  hash.set(1, rc(0, 0, 40, 40), 1);  // spans multiple sectors
  hash.set(2, rc(8, 8, 9, 9), 2);
  EXPECT_EQ(hash.size(), 2u);
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 40, 40)), (List<int>{1, 2}));

  auto removed = hash.remove(1);
  EXPECT_TRUE(removed.isValid());
  EXPECT_EQ(*removed, 1);
  EXPECT_FALSE(hash.contains(1));
  EXPECT_EQ(hash.size(), 1u);
  // Region vacated by entity 1 (but outside entity 2) returns nothing.
  EXPECT_EQ(hashQuery(hash, RectF(30, 30, 39, 39)), List<int>{});
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 40, 40)), List<int>{2});

  // Removing a non-existent key is a safe no-op.
  EXPECT_FALSE(hash.remove(999).isValid());
}

TEST(SpatialHash2D, UpdateMovesSectors) {
  TestHash hash(16.0f);
  hash.set(5, rc(0, 0, 4, 4), 5);
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 8, 8)), List<int>{5});

  // Move the entity far away; the old region must no longer return it.
  hash.set(5, rc(200, 200, 204, 204), 5);
  EXPECT_TRUE(hashQuery(hash, RectF(0, 0, 8, 8)).empty());
  EXPECT_EQ(hashQuery(hash, RectF(196, 196, 208, 208)), List<int>{5});
  EXPECT_EQ(hash.size(), 1u);
}

// The strongest gate: a randomized insert/update/remove/query churn compared
// against a brute-force oracle every step. Implementation-independent, so it
// holds for both the HashSet and the flat-list SectorEntrySet.
TEST(SpatialHash2D, RandomizedOracle) {
  TestHash hash(16.0f);
  Map<int, List<RectF>> world;  // oracle: key -> its rects

  uint64_t s = 0x9E3779B97F4A7C15ull;
  auto next = [&]() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); };
  auto frand = [&](float lo, float hi) { return lo + (hi - lo) * (next() / 2147483648.0f); };
  auto randRect = [&]() {
    float x = frand(0.0f, 60.0f), y = frand(0.0f, 60.0f);
    float w = frand(0.5f, 10.0f), h = frand(0.5f, 10.0f);
    return RectF(x, y, x + w, y + h);
  };

  int const KeySpace = 24;
  for (int step = 0; step < 4000; ++step) {
    int key = (int)(next() % KeySpace);
    uint32_t op = next() % 10;

    if (op < 2) {
      // remove
      if (hash.contains(key)) {
        hash.remove(key);
        world.remove(key);
      }
    } else {
      // insert/update with 1 or 2 rects (2-rect exercises multi-sector + the
      // occasional shared-sector overlap, the flat-append dedup edge case).
      List<RectF> rects;
      rects.append(randRect());
      if (next() % 3 == 0)
        rects.append(randRect());
      hash.set(key, rects, key);
      world[key] = rects;
    }

    // Compare a random query against the oracle every step.
    RectF q = randRect();
    ASSERT_EQ(hashQuery(hash, q), oracleQuery(world, q)) << "mismatch at step " << step;
  }

  // Final full-world sweep.
  ASSERT_EQ(hashQuery(hash, RectF(-10, -10, 80, 80)), oracleQuery(world, RectF(-10, -10, 80, 80)));
}
