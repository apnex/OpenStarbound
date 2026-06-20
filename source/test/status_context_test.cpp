#include "StarLua.hpp"
#include "StarString.hpp"
#include "StarList.hpp"
#include "StarLuaRoot.hpp"
#include "StarRoot.hpp"
#include "StarConfiguration.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

using namespace Star;

// Deterministic micro-benchmarks for the UniqueEffect Lua-context-churn levers
// (see specs/2026-06-20-uniqueeffect-context-churn-design.md). The live
// exploring profile attributed ~21% of the WorldServer thread to building a
// fresh Lua context per (re)applied status effect. These benches isolate, in a
// single binary, the per-op cost of the two safe build-side levers:
//
//   L1 (reserve)  -- pre-sizing the callback map before the registerCallback
//                    storm, avoiding the MinCapacity=8 -> 64 doubling rehashes.
//   L3 (move)     -- moving a LuaCallbacks into LuaBaseComponent::addCallbacks
//                    instead of copying the whole std::function table.
//
// Both are behavior-equivalent (the resulting maps are identical), so there is
// no correctness oracle here beyond an equality sanity check -- these are timing
// harnesses, run E-core-pinned and filtered:
//   taskset -c 6-15 ./game_tests --gtest_filter='StatusEffectChurnBench.*'
// They live in their own suite so a normal `game_tests` run skips them.

namespace {
  // A representative status-effect-shaped callback table: K trivial callbacks
  // registered one-by-one, mirroring the registerCallback storm in
  // makeStatusControllerCallbacks (39 entries -- the dominant per-effect group).
  // `doReserve` toggles Lever L1; everything else is identical between runs.
  LuaCallbacks buildTable(List<String> const& names, bool doReserve) {
    LuaCallbacks cb;
    if (doReserve)
      cb.reserve(names.size());
    for (auto const& n : names)
      cb.registerCallback(n, []() { return (int)0; });
    return cb;
  }

  List<String> makeNames(size_t k) {
    List<String> names;
    for (size_t i = 0; i < k; ++i)
      names.append(strf("cb{}", i));
    return names;
  }
}

// L1: building the callback map WITH vs WITHOUT reserve(). The only difference
// is the reserve() call. The map's bucket-array (re)allocations plus the
// volatile size() read below keep the build from being optimized away.
TEST(StatusEffectChurnBench, CallbackBuild) {
  size_t const K = 39;  // makeStatusControllerCallbacks group size
  List<String> const names = makeNames(K);

  volatile size_t sink = 0;

  auto measure = [&](bool doReserve, size_t iters) -> double {
    auto t0 = std::chrono::steady_clock::now();
    for (size_t it = 0; it < iters; ++it) {
      LuaCallbacks cb = buildTable(names, doReserve);
      sink += cb.callbacks().size();
    }
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
  };

  // Calibrate iteration count to ~1s (warms caches too).
  double const targetNs = 1.0e9;
  size_t calibIters = 256;
  double calibNs = 0.0;
  while (true) {
    calibNs = measure(true, calibIters);
    if (calibNs > 5.0e7)  // >50ms -> stable estimate
      break;
    calibIters *= 2;
  }
  size_t iters = (size_t)std::max(1.0, calibIters * (targetNs / std::max(calibNs, 1.0)));

  double reservedNs = measure(true, iters);     // lever path (L1 on)
  double unreservedNs = measure(false, iters);  // baseline path (L1 off)

  double reservedPer = reservedNs / iters;
  double unreservedPer = unreservedNs / iters;
  double deltaPer = unreservedPer - reservedPer;
  double deltaPct = 100.0 * deltaPer / unreservedPer;

  std::printf(
    "\n=== L1 reserve() callback-build micro-benchmark (deterministic) ===\n"
    "  callbacks per build : %zu\n"
    "  iterations          : %zu\n"
    "  baseline (no reserve): %8.1f ns/build\n"
    "  lever    (reserve)   : %8.1f ns/build\n"
    "  saved                : %8.1f ns/build  (%.1f%%)\n"
    "===================================================================\n\n",
    K, iters, unreservedPer, reservedPer, deltaPer, deltaPct);

  // Both paths produce identical tables: same key set (the wrapped-function
  // values are not equality-comparable), order-independent of growth history.
  LuaCallbacks const reservedTable = buildTable(names, true);
  LuaCallbacks const plainTable = buildTable(names, false);
  EXPECT_EQ(reservedTable.callbacks().size(), plainTable.callbacks().size());
  for (auto const& p : reservedTable.callbacks())
    EXPECT_TRUE(plainTable.callbacks().contains(p.first));
  EXPECT_GT(sink, 0u);
}

// L3: copying a built callback table (the per-group cost addCallbacks paid)
// vs moving it (what L3 does instead). The move side pre-builds a bounded batch
// of sources outside the timed region so only the copy/move is measured.
TEST(StatusEffectChurnBench, CallbackCopy) {
  size_t const K = 39;
  List<String> const names = makeNames(K);
  LuaCallbacks const original = buildTable(names, true);

  volatile size_t sink = 0;

  size_t const copyIters = 200000;
  size_t const moveIters = 50000;  // bounded: holds a batch of source tables in memory

  // Copy: the std::function-table copy L3 eliminates.
  double copyNs;
  {
    auto t0 = std::chrono::steady_clock::now();
    for (size_t it = 0; it < copyIters; ++it) {
      LuaCallbacks c = original;
      sink += c.callbacks().size();
    }
    auto t1 = std::chrono::steady_clock::now();
    copyNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
  }

  // Move: pre-build the source batch (untimed), then time moving each out.
  double moveNs;
  {
    std::vector<LuaCallbacks> srcs;
    srcs.reserve(moveIters);
    for (size_t it = 0; it < moveIters; ++it)
      srcs.push_back(original);
    auto t0 = std::chrono::steady_clock::now();
    for (size_t it = 0; it < moveIters; ++it) {
      LuaCallbacks c = std::move(srcs[it]);
      sink += c.callbacks().size();
    }
    auto t1 = std::chrono::steady_clock::now();
    moveNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
  }

  double copyPer = copyNs / copyIters;
  double movePer = moveNs / moveIters;
  double deltaPer = copyPer - movePer;
  double deltaPct = 100.0 * deltaPer / copyPer;

  std::printf(
    "\n=== L3 copy-vs-move callback-table micro-benchmark (deterministic) ===\n"
    "  callbacks per table : %zu\n"
    "  baseline (copy)     : %8.1f ns/op  (%zu iters)\n"
    "  lever    (move)     : %8.1f ns/op  (%zu iters)\n"
    "  saved per group     : %8.1f ns/op  (%.1f%%)\n"
    "=====================================================================\n\n",
    K, copyPer, copyIters, movePer, moveIters, deltaPer, deltaPct);

  EXPECT_GT(sink, 0u);
}

// L2: full create->load->teardown cycle, OFF (ctx.load -> luaU_undump every time)
// vs ON (ctx.loadCached -> undump once, then a fresh per-context closure clone).
// Isolates the per-context bytecode-deserialization cost L2 eliminates (the
// ~6.6% loadContextScript slice). Pure LuaEngine; the chunk pcall + invoke cost
// is paid identically by both arms, so the delta is the undump.
TEST(StatusEffectChurnBench, ProtoLoad) {
  char const* const SCRIPT = R"SCRIPT(
    result = 0
    local priv = 0
    local label = "statuseffect.proto.bench.module.pattern.with.some.constants"
    function bump() priv = priv + 1; result = result + 1; return priv end
    function describe() return label end
  )SCRIPT";

  auto e = LuaEngine::create();
  ByteArray const bc = e->compile(SCRIPT, "proto_bench");

  volatile uint64_t sink = 0;

  auto measure = [&](bool useCache, size_t iters) -> double {
    auto t0 = std::chrono::steady_clock::now();
    for (size_t it = 0; it < iters; ++it) {
      LuaContext ctx = e->createContext();
      if (useCache)
        ctx.loadCached("proto_bench", bc);
      else
        ctx.load(bc);
      sink += ctx.invokePath<int>("bump");  // forces the chunk to have loaded+run
    }
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
  };

  // Calibrate to ~0.8s on the cached path (also warms the Proto cache).
  double const targetNs = 0.8e9;
  size_t calibIters = 256;
  double calibNs = 0.0;
  while (true) {
    calibNs = measure(true, calibIters);
    if (calibNs > 5.0e7)
      break;
    calibIters *= 2;
  }
  size_t iters = (size_t)std::max(1.0, calibIters * (targetNs / std::max(calibNs, 1.0)));

  double offNs = measure(false, iters);  // baseline: undump every context
  double onNs = measure(true, iters);    // lever: cached Proto + clone
  EXPECT_EQ(e->cachedProtoCount(), 1u);

  double offPer = offNs / iters;
  double onPer = onNs / iters;
  double deltaPer = offPer - onPer;
  double deltaPct = 100.0 * deltaPer / offPer;

  std::printf(
    "\n=== L2 Proto-cache load micro-benchmark (deterministic, full ctx cycle) ===\n"
    "  iterations            : %zu\n"
    "  baseline (load/undump): %8.1f ns/ctx\n"
    "  lever    (loadCached) : %8.1f ns/ctx\n"
    "  saved                 : %8.1f ns/ctx  (%.1f%%)\n"
    "==========================================================================\n\n",
    iters, offPer, onPer, deltaPer, deltaPct);

  EXPECT_GT(sink, 0u);
}

// ---------------------------------------------------------------------------
// Repro for the same-session toggle bug: flipping scriptProtoCacheEnabled +
// invalidating the cache (the /serverreload path) should make the NEXT context
// load re-read the toggle and switch load<->loadCached. Driven via unloadScript
// (which sets protoCacheDirty, the same flag the reload listener's clear() sets),
// so it isolates the dirty-flush re-read from the reload-listener plumbing.
// ---------------------------------------------------------------------------
TEST(ProtoCacheToggle, DirtyFlushReReadsConfig) {
  auto& root = Root::singleton();
  auto luaRoot = make_shared<LuaRoot>();
  String const script = "/scripts/opensb/worldserver/worldserver.lua";

  // OFF: config false, mark the cache dirty, then a context load must re-read
  // false and take the plain load() path -> proto cache stays empty.
  root.configuration()->set("scriptProtoCacheEnabled", false);
  luaRoot->unloadScript(script);  // sets protoCacheDirty = true
  { auto c = luaRoot->createContext(StringList{script}); (void)c; }
  EXPECT_EQ(luaRoot->luaEngine().cachedProtoCount(), 0u) << "OFF: proto cache must stay empty";

  // ON: flip config, mark dirty again, load -> re-read true -> loadCached().
  root.configuration()->set("scriptProtoCacheEnabled", true);
  luaRoot->unloadScript(script);  // protoCacheDirty = true
  { auto c = luaRoot->createContext(StringList{script}); (void)c; }
  EXPECT_GE(luaRoot->luaEngine().cachedProtoCount(), 1u)
      << "ON: the dirty-flush must re-read scriptProtoCacheEnabled=true and use loadCached";
}

// Tests the FULL /serverreload path: root.reload() must fire the LuaRoot's reload
// listener -> ScriptCache::clear() -> protoCacheDirty=true, so the next context
// load re-reads the (flipped) toggle. If this fails where DirtyFlushReReadsConfig
// passes, the bug is the reload-listener not reaching the cache.
TEST(ProtoCacheToggle, ReloadFiresListenerAndReReads) {
  auto& root = Root::singleton();
  auto luaRoot = make_shared<LuaRoot>();
  String const script = "/scripts/opensb/worldserver/worldserver.lua";

  // Force protoCacheEnabled=false deterministically (config false + dirty re-read).
  root.configuration()->set("scriptProtoCacheEnabled", false);
  luaRoot->unloadScript(script);
  { auto c = luaRoot->createContext(StringList{script}); (void)c; }
  ASSERT_EQ(luaRoot->luaEngine().cachedProtoCount(), 0u) << "precondition: OFF";

  // Flip ON, then the actual /serverreload trigger.
  root.configuration()->set("scriptProtoCacheEnabled", true);
  root.reload();
  { auto c = luaRoot->createContext(StringList{script}); (void)c; }
  EXPECT_GE(luaRoot->luaEngine().cachedProtoCount(), 1u)
      << "root.reload() must fire the listener -> clear() -> dirty -> re-read true";
}
