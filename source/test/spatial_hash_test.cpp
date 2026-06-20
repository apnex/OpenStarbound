#include "StarSpatialHash2D.hpp"
#include "StarRect.hpp"
#include "StarMap.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>

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

// The single-rect / single-coord set() convenience overloads previously
// infinite-recursed (a braced-init-list `{rect}` can't deduce the
// RectCollection template parameter, so it re-selected the single-arg
// overload). This exercises all four; before the initializer_list<Rect>{...}
// fix it stack-overflowed.
TEST(SpatialHash2D, ConvenienceOverloads) {
  TestHash hash(16.0f);
  hash.set(1, RectF(0, 0, 4, 4), 1);            // set(Key, Rect, Value)
  hash.set(2, TestHash::Coord(20, 20), 2);      // set(Key, Coord, Value)
  EXPECT_EQ(hash.size(), 2u);
  EXPECT_EQ(hashQuery(hash, RectF(0, 0, 4, 4)), List<int>{1});
  EXPECT_EQ(hashQuery(hash, RectF(19, 19, 21, 21)), List<int>{2});

  // 2-arg forms update an existing entry's rects (the key must already exist).
  hash.set(1, RectF(50, 50, 54, 54));           // set(Key, Rect)
  EXPECT_TRUE(hashQuery(hash, RectF(0, 0, 4, 4)).empty());
  EXPECT_EQ(hashQuery(hash, RectF(48, 48, 56, 56)), List<int>{1});
  hash.set(2, TestHash::Coord(60, 60));         // set(Key, Coord)
  EXPECT_TRUE(hashQuery(hash, RectF(19, 19, 21, 21)).empty());
  EXPECT_EQ(hashQuery(hash, RectF(59, 59, 61, 61)), List<int>{2});
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

// ============================================================================
// Deterministic micro-benchmark of SpatialHash2D::forEach -- the EntityMap
// entity-query hot path, profiled at 22.5% self of the exploring
// WorldServerThread. Design ref:
// /root/kubebound/specs/2026-06-20-foreach-optimization.md (§6, A/B plan).
//
// This is a MEASUREMENT, not a correctness gate: it times + PRINTS ns/query and
// does NOT assert any threshold. It lives in its own suite (SpatialHashBench)
// so the normal `--gtest_filter='SpatialHash2D.*'` runs never touch it. Run it:
//     ./core_tests --gtest_filter='SpatialHashBench.*'
//
// The prior live A/B was confounded by activity variance; this gives a clean,
// reproducible ns/query number. It (a) quantifies the v1 win just landed
// (Lever #1 de-std::function-ized the per-entity dispatch) and (b) is the
// harness for the v2 levers (#3 contained-cell skip, #4 stamp-dedup, #7
// granularity) -- rebuild core_tests + rerun this filter to measure their
// effect on forEach's internals.
//
// Isolating Lever #1 in a single build: the SAME query mix runs over the SAME
// data TWICE -- once with a direct lambda (the inlined, post-#1 path) and once
// with that lambda wrapped in std::function (the pre-#1 type-erased dispatch).
// The delta IS Lever #1's per-entity dispatch win, measured without two builds.
// The callback accumulates into a sink escaped to a `volatile` so the optimizer
// cannot elide the work (and thus the whole forEach loop).
// ============================================================================
TEST(SpatialHashBench, ForEachDispatch) {
  // Mirror the EntityMap's real instantiation: SpatialHash2D<int, float,
  // shared_ptr<Entity>, int, 4096>. The value is `int` here (not a shared_ptr)
  // because the cost is in the cell sweep / sort / dedup / dispatch, not the
  // value payload; `4096` is the BlockAllocator block size, NOT the grid
  // granularity. The real granularity is the sector size, set below.
  typedef SpatialHash2D<int, float, int, int, 4096> BenchHash;
  float const SectorSize = 16.0f;  // EntityMapSpatialHashSectorSize (StarEntityMap.cpp:8)
  BenchHash hash(SectorSize);

  // Fixed LCG (same constants as RandomizedOracle above) -> reproducible
  // population + query set, build-over-build.
  uint64_t s = 0x9E3779B97F4A7C15ull;
  auto next = [&]() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); };
  auto frand = [&](float lo, float hi) { return lo + (hi - lo) * (next() / 2147483648.0f); };

  // ---- Populate ~965 entities across a representative world span ----
  // Size distribution: most small (items/projectiles/small monsters, 1-4
  // units), some medium, a few large multi-cell (big monsters/vehicles
  // spanning several 16-unit sectors).
  int const EntityCount = 965;      // the profiled exploring entity count
  float const WorldSpan = 3000.0f;  // a few thousand units square
  List<Vec2F> centers;              // entity centers, reused to aim collision queries
  for (int i = 0; i < EntityCount; ++i) {
    float x = frand(0.0f, WorldSpan), y = frand(0.0f, WorldSpan);
    uint32_t roll = next() % 100;
    float w, h;
    if (roll < 85)      { w = frand(1.0f, 4.0f);   h = frand(1.0f, 4.0f); }    // ~85% small
    else if (roll < 97) { w = frand(4.0f, 16.0f);  h = frand(4.0f, 16.0f); }   // ~12% medium
    else                { w = frand(16.0f, 64.0f); h = frand(16.0f, 64.0f); }  // ~3% large multi-cell
    hash.set(i, rc(x, y, x + w, y + h), i);
    centers.append(Vec2F(x + w * 0.5f, y + h * 0.5f));
  }
  ASSERT_EQ(hash.size(), (size_t)EntityCount);

  // ---- Fixed query mix matching the real per-tick caller mix ----
  // Many SMALL boxes (collision broad-phase / point-ish, a few units) + a few
  // LARGE boxes (net-monitoring / lighting regions, tens-to-hundreds of units).
  // The SMALL boxes are centered ON entity positions, because the dominant
  // small-query caller is the per-actor collision/force broad-phase, which
  // queries a padded box around each moving entity -- so those queries DO hit
  // (the querying entity + neighbors), unlike uniform-random boxes over mostly
  // empty sky. The LARGE boxes are placed anywhere (window/monitoring regions).
  List<RectF> queries;
  int const SmallQueries = 500;  // collision / point-ish, entity-centered
  int const LargeQueries = 20;   // monitoring / lighting, anywhere
  for (int i = 0; i < SmallQueries; ++i) {
    Vec2F c = centers[next() % centers.size()];
    float w = frand(2.0f, 8.0f), h = frand(2.0f, 8.0f);
    queries.append(RectF(c[0] - w * 0.5f, c[1] - h * 0.5f, c[0] + w * 0.5f, c[1] + h * 0.5f));
  }
  for (int i = 0; i < LargeQueries; ++i) {
    float x = frand(0.0f, WorldSpan), y = frand(0.0f, WorldSpan);
    float w = frand(40.0f, 200.0f), h = frand(40.0f, 200.0f);
    queries.append(RectF(x, y, x + w, y + h));
  }
  size_t const QPI = queries.size();  // queries per iteration

  // Count unique-entity dispatches across one full pass of the mix (for the
  // ns/callback figures below).
  size_t callbacksPerIter = 0;
  for (auto const& q : queries)
    hash.forEach(q, [&callbacksPerIter](int const&) { ++callbacksPerIter; });

  // Sink: the accumulated work escapes to a `volatile`, so the optimizer cannot
  // drop the callback (and therefore the whole forEach) as dead code.
  volatile int64_t sink = 0;
  int64_t acc = 0;

  // Time `iters` passes over the full query mix with callback `cb`; returns ns.
  // `cb` is taken by lvalue ref so forEach deduces Function = LambdaType& (Run
  // A, inlinable) or std::function& (Run B, type-erased) -- the ONLY difference
  // between the two runs.
  auto measure = [&](auto& cb, size_t iters) -> double {
    auto t0 = std::chrono::steady_clock::now();
    for (size_t it = 0; it < iters; ++it)
      for (auto const& q : queries)
        hash.forEach(q, cb);
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
  };

  // The per-entity work -- identical body in both runs.
  auto lambdaWork = [&acc](int const& v) { acc += v; };
  std::function<void(int const&)> fnWork = [&acc](int const& v) { acc += v; };

  // ---- Calibrate iteration count to ~1.5s total (warms caches too) ----
  double const targetNs = 1.5e9;
  size_t calibIters = 64;
  double calibNs = 0.0;
  while (true) {
    acc = 0;
    calibNs = measure(lambdaWork, calibIters);
    if (calibNs > 5.0e7)  // >50ms -> stable estimate
      break;
    calibIters *= 2;
  }
  size_t iters = (size_t)std::max(1.0, calibIters * (targetNs / std::max(calibNs, 1.0)));

  // ---- Run A: direct lambda (inlined, post-Lever-#1) ----
  acc = 0;
  double lambdaNs = measure(lambdaWork, iters);
  sink += acc;
  int64_t lambdaAcc = acc;

  // ---- Run B: same lambda wrapped in std::function (type-erased, pre-#1) ----
  acc = 0;
  double fnNs = measure(fnWork, iters);
  sink += acc;
  int64_t fnAcc = acc;

  // Sanity: identical data + queries -> identical callback totals, so the runs
  // are genuinely comparable (only the callback's dispatch type differs).
  EXPECT_EQ(lambdaAcc, fnAcc) << "callback totals diverged; runs are not comparable";

  double totalQueries = (double)iters * (double)QPI;
  double totalCallbacks = (double)iters * (double)callbacksPerIter;
  double lambdaNsPerQ = lambdaNs / totalQueries;
  double fnNsPerQ = fnNs / totalQueries;
  double lambdaNsPerCb = lambdaNs / totalCallbacks;
  double fnNsPerCb = fnNs / totalCallbacks;
  double deltaNsPerQ = fnNsPerQ - lambdaNsPerQ;
  double deltaNsPerCb = fnNsPerCb - lambdaNsPerCb;
  double deltaPct = 100.0 * deltaNsPerQ / fnNsPerQ;

  std::printf(
    "\n=== SpatialHash2D::forEach micro-benchmark (deterministic) ===\n"
    "  entities        : %d  (world span %.0f, sector size %.1f)\n"
    "  query mix       : %d small + %d large = %zu queries/iteration\n"
    "  callbacks/iter  : %zu  (unique entity hits across the mix)\n"
    "  iterations      : %zu  (%.3g total queries, %.3g total callbacks)\n"
    "  lambda  (inlined / post-#1)   : %8.2f ns/query  %7.2f ns/callback  %7.2f Mq/s\n"
    "  std::fn (type-erased / pre-#1): %8.2f ns/query  %7.2f ns/callback  %7.2f Mq/s\n"
    "  Lever #1 dispatch delta       : %8.2f ns/query  %7.2f ns/callback  (%.1f%% of std::function path)\n\n",
    EntityCount, (double)WorldSpan, (double)SectorSize,
    SmallQueries, LargeQueries, QPI,
    callbacksPerIter,
    iters, totalQueries, totalCallbacks,
    lambdaNsPerQ, lambdaNsPerCb, 1.0e3 / lambdaNsPerQ,
    fnNsPerQ, fnNsPerCb, 1.0e3 / fnNsPerQ,
    deltaNsPerQ, deltaNsPerCb, deltaPct);
  std::fflush(stdout);

  (void)sink;
}
