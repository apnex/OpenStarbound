#pragma once

#include "StarBusyReading.hpp"
#include "StarMetricDesc.hpp"

namespace Star {

// Reads per-thread CPU BUSY time out of /proc/<pid>/task/*/stat and attributes it to the DECLARED
// owner vocabulary -- the missing half of the busy model, and the half that was never an engine
// problem.
//
// WHY THIS EXISTS AT ALL, when the process total is already recorded. cpu.process.total_us (getrusage
// in TelemetryReporter) has carried whole-process CPU busy since the telemetry model shipped, so the
// coarsest cell was never empty. What it cannot do is say WHOSE. A saving that moves work from the
// lighting thread to the main thread leaves that total unchanged, and a single-thread saturation --
// one core pegged while fifteen idle -- is invisible in a number that averages across all of them.
//
// THE THREAD NAMES ALREADY CARRY THE TAXONOMY, which is why this needs no engine change whatsoever.
// Measured against a live client over a 3s window:
//      starbound          24.0% of a core   -> Frame
//      WorldServerThre     6.0%             -> Sim
//      WorldClient::li     1.0%             -> Lighting
//      starboun:gdrv0      0.3%             -> Gl      (the driver's own thread)
//      SDLAudioP15, UniverseServer/Connect, SystemWorldServ -> Unknown
// The kernel truncates comm to 15 characters, which is why the matches below are PREFIXES of the
// names the engine sets and not the names themselves. That truncation is the reason a match must
// never be an equality test against the full name a reader of the source would expect.
//
// SOVEREIGN, like its two siblings: it reads /proc for any pid, needs no privilege, and does not
// perturb what it measures. It can therefore be pointed at the shipped, uninstrumented binary.
class ThreadBusyReader {
public:
  // Reads /proc/<pid>/task/*/stat. Returns BusyReading::unavailable(reason) if the process is gone,
  // the directory is unreadable, or no task supplied a parsable stat line.
  //
  // The result's busyNs is keyed by OWNER, not by thread: "which budget did this cost land in" is
  // the question the whole descriptor vocabulary is built around, and "which thread ran it" never
  // was -- WorldClient::lightingCalc() runs on its own thread or inline on the main one depending on
  // configuration, and belongs to Lighting either way.
  //
  // EVERY THREAD IS COUNTED, including ones no rule claims: they land under `unknown` rather than
  // being dropped. A reader that silently discarded the threads it could not name would under-report
  // CPU busy by an amount that is invisible precisely because it is missing. The sum over owners
  // therefore equals the sum over threads, always, and a test asserts it.
  static BusyReading read(int pid);

  // Same logic against an arbitrary directory of task-shaped subdirectories, so the parsing and the
  // owner mapping are testable without a live process -- the same door readFdinfoDir opens for the
  // per-client reader.
  static BusyReading readTaskDir(String const& dir);

  // The process's OWN cumulative CPU busy time, from /proc/<pid>/stat rather than from the tasks.
  //
  // IT IS NOT THE SUM OF read()'s OWNERS, AND THAT IS THE POINT. /proc/<pid>/stat carries a
  // THREAD-GROUP AGGREGATE that keeps the time of threads which have since EXITED; /proc/<pid>/task
  // holds only the living. Measured deterministically: a process with a zero gap grew one of exactly
  // 49 ticks the instant four CPU-burning threads finished, while the live sum returned to zero.
  //
  // So a thread that exits inside a measurement window takes its within-window cost out of the
  // attributed sum and leaves it in this number. Without this reading that loss is INVISIBLE -- the
  // owners still add up to each other, and the total they add up to is quietly short. Reading both
  // makes the residual a figure a consumer can see and, if it grows, act on.
  //
  // Nothing if the process is gone or its stat line does not parse -- never 0, which would read as a
  // process that used no CPU.
  static Maybe<int64_t> processBusyNs(int pid);

  // Which owner a thread name belongs to. Exposed because the mapping is POLICY, not parsing: it is
  // the one part of this reader a reasonable person could disagree with, so it must be assertable on
  // its own rather than only through a whole /proc read.
  static MetricOwner ownerOfThread(String const& comm);
};

}
