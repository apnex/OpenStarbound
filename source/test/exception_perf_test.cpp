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

// A control-flow throw+catch+discard must be cheap (no eager backtrace resolution).
TEST(ExceptionPerf, ThrowCatchDiscardIsCheap) {
  const int N = 20000;
  volatile size_t sink = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < N; ++i) {
    try {
      throw StarException("simulated control-flow exception");   // genStackTrace defaults true
    } catch (StarException const& e) {
      sink += reinterpret_cast<size_t>(&e);   // use it, but do NOT call .what() (caught-and-discarded)
    }
  }
  auto t1 = std::chrono::steady_clock::now();
  double usPer = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
  std::printf("[ExceptionPerf] throw+catch+discard (no .what()): %.2f us/exception (N=%d)\n", usPer, N);
  // Eager DWARF resolution costs tens-to-hundreds of us/exception; lazy raw capture is a few us.
  EXPECT_LT(usPer, 15.0) << "StarException is resolving a backtrace eagerly on construction (should be lazy)";
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
