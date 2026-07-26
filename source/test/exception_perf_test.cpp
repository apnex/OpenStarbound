#include "StarException.hpp"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdio>
#include <string>

// Regression guard for the StarException eager-backtrace perf bug.
//
// JSON-patch "test" operations (StarJsonPatch.cpp) throw + catch + DISCARD TraversalExceptions as
// routine control flow (a missing path => test fails => skip patch). With eager backtrace resolution
// in the StarException ctor (cpptrace generate_trace + resolve = full DWARF symbolize), every such
// throw paid a heavy cost even though the exception is never printed -- ~4% of the render thread at a
// dense FU base. The fix captures the trace lazily (raw capture in the ctor; resolve only when a
// fullStacktrace print is actually requested -- and .what() requests fullStacktrace=false, so the
// common caught path never resolves).

using namespace Star;

namespace {
  template <typename F>
  double usPerOp(int n, F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i)
      f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count() / n;
  }
}

// A control-flow throw+catch+discard must not pay for backtrace RESOLUTION.
//
// THIS USED TO BE AN ABSOLUTE BOUND -- `EXPECT_LT(usPer, 15.0)` -- and 15.0 was measured on one Linux
// workstation. On windows-latest it read 491.03 and failed core_tests on every CI run (#194). The
// regression was not present there; the number simply does not travel. Windows captures a raw backtrace
// through dbghelp, which is globally serialised and orders of magnitude slower than Linux's fast unwind,
// so an absolute microsecond ceiling measures THE MACHINE, not the property under test.
//
// The property we actually care about is comparative: discarding must be far cheaper than resolving. A
// ratio cancels the machine out. Same lesson as the render oracles -- a differential comparison survives
// a change of hardware, an absolute threshold does not.
TEST(ExceptionPerf, ThrowCatchDiscardDoesNotResolve) {
  volatile size_t sink = 0;

  // Resolve first, so any one-off symbol-table warm-up is billed to the RESOLVE side. That biases the
  // ratio downward -- against the test passing -- which is the conservative direction for a guard.
  const int kResolve = 200;
  double resolveUs = usPerOp(kResolve, [&] {
    try {
      throw StarException("resolved control-flow exception");
    } catch (StarException const& e) {
      sink += printException(e, true).size();   // fullStacktrace=true -> the lazy resolve happens here
    }
  });

  const int kDiscard = 20000;
  double discardUs = usPerOp(kDiscard, [&] {
    try {
      throw StarException("discarded control-flow exception");   // genStackTrace defaults true
    } catch (StarException const& e) {
      sink += reinterpret_cast<size_t>(&e);     // use it, but never ask for the trace
    }
  });

  std::printf("[ExceptionPerf] discard %.2f us (N=%d), resolve %.2f us (N=%d), resolve/discard = %.1fx\n",
              discardUs, kDiscard, resolveUs, kResolve, resolveUs / discardUs);

  // WHERE 4.0 COMES FROM -- BOTH ENDS MEASURED, not reasoned about (this workstation, 2026-07-26):
  //   fixed (lazy ctor):                   1.90us vs 605.88us  =  318.2x   passes, with 80x to spare
  //   injected (discard body resolves):  206.26us vs 601.88us  =    2.9x   fails, as it must
  //
  // The injected case does NOT converge on ~1x the way the theory says it should: cpptrace caches
  // resolved symbols, so the 20000-iteration discard loop amortises what the 200-iteration resolve loop
  // pays cold. A regressed build therefore shows ~3x, not ~1x -- and a threshold picked from the theory
  // instead of from the run could easily have sat below it and passed a genuine regression. Measure
  // BOTH ends of a ratio gate; the failing end is the one that decides the constant.
  EXPECT_GT(resolveUs, discardUs * 4.0)
      << "throw+catch+discard costs within 4x of throw+catch+resolve. That is what it costs when "
         "StarException resolves its backtrace eagerly in the constructor instead of lazily on demand.";
}

// Lazy capture must NOT lose the backtrace: a full-stacktrace print still resolves to frames.
TEST(ExceptionPerf, FullStacktraceStillResolves) {
  try {
    throw StarException("error with trace");
  } catch (StarException const& e) {
    std::string full = printException(e, true);   // fullStacktrace=true -> lazy resolve happens here
    EXPECT_NE(full.find("(StarException)"), std::string::npos) << "type/message missing";
    EXPECT_NE(full.find("error with trace"), std::string::npos) << "message missing";
    EXPECT_GT(full.size(), 60u) << "full stacktrace should resolve to actual frames, not just the message";
  }
}
