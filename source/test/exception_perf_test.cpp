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
// THIS TOOK TWO GOES, AND THE SECOND ONE IS THE INTERESTING FAILURE.
//
// It began as an absolute bound -- `EXPECT_LT(usPer, 15.0)` -- measured on one Linux workstation. On
// windows-latest it read 491.03 and failed core_tests on every run (#194); the regression was not
// present, the number simply does not travel. So it became a RATIO, on the reasoning that resolution
// dwarfs capture and a ratio cancels the machine out. Windows then failed the other way: resolve 755.97
// vs discard 496.48, a ratio of 1.5.
//
// The ratio was not mis-tuned. It was pointed at code that does not exist there. StarException has TWO
// implementations -- StarException_unix.cpp captures with cpptrace::generate_raw_trace() and resolves
// lazily in the print lambda, which is the fix this test guards; StarException_windows.cpp uses
// captureStack() (StackWalk64) in the ctor and SymFromAddr at print time, and has ALWAYS been lazy. It
// never had the bug. Worse for a timing test, its costs are inverted: the stack walk dominates and
// symbolisation adds only ~50%, so no threshold can separate "captured" from "captured and resolved"
// above the noise of a shared runner.
//
// So the assertion is scoped to the implementation it guards, and on every other platform the test
// GTEST_SKIPs with a reason. Skipping is the honest state here and it is REPORTED as skipped -- what
// this project does not tolerate is an assertion that silently evaluates to nothing.
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

#ifndef STAR_SYSTEM_FAMILY_UNIX
  GTEST_SKIP() << "guards the cpptrace lazy-resolve path in StarException_unix.cpp, which is not the "
                  "implementation compiled here. StarException_windows.cpp captures with StackWalk64 in "
                  "the ctor and symbolises at print time -- always lazy, never had this bug -- and its "
                  "capture cost dominates symbolisation, so no timing ratio separates the two states "
                  "above runner noise. Numbers above are informational.";
#else
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
#endif
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
