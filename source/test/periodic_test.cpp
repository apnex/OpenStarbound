#include "StarPeriodic.hpp"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

using namespace Star;

namespace {

// Baseline cadence: tick() once per step for M steps; record every step where
// tick() returned true (the script "runs").
std::vector<size_t> baselineRuns(unsigned everyXSteps, size_t steps) {
  Periodic p(everyXSteps);
  std::vector<size_t> runs;
  for (size_t s = 0; s < steps; ++s) {
    if (p.tick())
      runs.push_back(s);
  }
  return runs;
}

// Walk the same M steps but with dormancy-style sleeps.  At each wake step we
// may "sleep" for k steps: that mirrors k Object::update() calls that never
// happened.  With compensate=true we account for them via Periodic::skip(k)
// (Task 4a primitive); with compensate=false we only advance the step index
// (the negative control) to prove the cadence drifts without compensation.
//
// Sleep lengths are chosen to honor the real dormancy-horizon invariant:
// k < stepsUntilNext() so a run is never slept past.  *skipsOut counts how
// many non-trivial sleeps actually occurred so callers can prove the
// compensation path was exercised.
std::vector<size_t> compensatedRuns(
    unsigned everyXSteps, size_t steps, bool compensate, uint32_t seed, size_t& skipsOut) {
  Periodic q(everyXSteps);
  std::vector<size_t> runs;
  uint32_t rng = seed;
  auto nextRand = [&rng]() {
    rng = rng * 1664525u + 1013904223u; // numerical-recipes LCG, fully deterministic
    return rng;
  };

  skipsOut = 0;
  size_t s = 0;
  while (s < steps) {
    unsigned su = q.stepsUntilNext();   // everyXSteps != 0 here => su >= 1
    unsigned maxK = su - 1;             // never sleep onto/past the next run
    if (s + static_cast<size_t>(maxK) >= steps)
      maxK = static_cast<unsigned>((steps - 1) - s); // don't walk past the last step
    unsigned k = maxK == 0 ? 0u : (nextRand() % (maxK + 1)); // k in [0, maxK]

    if (k > 0) {
      // Horizon invariant the real dormancy code guarantees.
      EXPECT_LT(k, q.stepsUntilNext());
      if (compensate)
        q.skip(k);
      s += k;
      ++skipsOut;
    }
    if (q.tick())
      runs.push_back(s);
    ++s;
  }
  return runs;
}

} // namespace

// Core oracle for Task 4a: compensated skips preserve the cadence phase.  For a
// range of periods, the run-steps produced while sleeping+skip()-ing are
// IDENTICAL (same absolute steps) to ticking every step.
TEST(PeriodicSkip, SkipPreservesCadence) {
  size_t const M = 2000;
  for (unsigned N : {1u, 2u, 3u, 5u, 7u, 13u}) {
    auto base = baselineRuns(N, M);
    size_t skips = 0;
    auto comp = compensatedRuns(N, M, /*compensate=*/true, 0xC0FFEEu ^ (N * 2654435761u), skips);
    EXPECT_EQ(base, comp) << "cadence diverged for N=" << N;
    if (N > 1)
      EXPECT_GT(skips, 0u) << "no sleeps exercised for N=" << N; // N==1 can never sleep (su always 1)
  }
}

// Negative control: same walk WITHOUT skip() compensation must diverge from the
// baseline, proving the test actually depends on the compensation.
TEST(PeriodicSkip, NegativeControlDiverges) {
  size_t const M = 2000;
  unsigned const N = 7;
  auto base = baselineRuns(N, M);
  size_t skips = 0;
  auto noComp = compensatedRuns(N, M, /*compensate=*/false, 0xBADF00Du, skips);
  EXPECT_GT(skips, 0u) << "negative control did not exercise any sleeps";
  EXPECT_NE(base, noComp) << "cadence stayed aligned without compensation (test is not exercising skip)";
}

// stepsUntilNext()/skip() arithmetic, asserted directly.
TEST(PeriodicSkip, StepsUntilNextDirect) {
  Periodic p(5);
  // Fresh Periodic: counter == 0, so the very next tick() runs => 1 call away.
  EXPECT_EQ(p.stepsUntilNext(), 1u);
  EXPECT_TRUE(p.tick());                 // runs, reloads counter to N-1
  EXPECT_EQ(p.stepsUntilNext(), 5u);     // right after a run it equals N
  EXPECT_FALSE(p.tick());
  EXPECT_EQ(p.stepsUntilNext(), 4u);     // decrements by 1 each plain tick()
  EXPECT_FALSE(p.tick());
  EXPECT_EQ(p.stepsUntilNext(), 3u);
  p.skip(2);                             // k=2 < stepsUntilNext()==3
  EXPECT_EQ(p.stepsUntilNext(), 1u);     // skip(k) reduces it by exactly k
  EXPECT_TRUE(p.tick());                 // and the compensated cadence fires here
  EXPECT_EQ(p.stepsUntilNext(), 5u);
}

// stepCount()==0 ("never") behavior.  stepsUntilNext()==0 is exactly the oracle
// that makes LuaUpdatableComponent::nextUpdateStep() return {} (no self-wake),
// and skip() is a no-op.
TEST(PeriodicSkip, ZeroStepCount) {
  Periodic p(0);
  EXPECT_EQ(p.stepCount(), 0u);
  EXPECT_EQ(p.stepsUntilNext(), 0u);     // -> nextUpdateStep() returns {}
  EXPECT_FALSE(p.tick());
  p.skip(5);                             // no-op
  EXPECT_EQ(p.stepsUntilNext(), 0u);
  EXPECT_FALSE(p.tick());
}

TEST(PeriodicTest, All) {
  Periodic periodic(2);

  EXPECT_TRUE(periodic.ready());
  EXPECT_TRUE(periodic.tick());
  EXPECT_FALSE(periodic.ready());
  EXPECT_FALSE(periodic.tick());
  EXPECT_TRUE(periodic.ready());
  EXPECT_TRUE(periodic.tick());

  periodic = Periodic(0);
  EXPECT_FALSE(periodic.tick());
  EXPECT_FALSE(periodic.tick());

  periodic = Periodic(3);
  EXPECT_TRUE(periodic.ready());
  EXPECT_TRUE(periodic.tick());
  EXPECT_FALSE(periodic.ready());
  EXPECT_FALSE(periodic.tick());
  EXPECT_FALSE(periodic.tick());
  EXPECT_TRUE(periodic.ready());
  EXPECT_TRUE(periodic.tick());
  EXPECT_FALSE(periodic.ready());
}
