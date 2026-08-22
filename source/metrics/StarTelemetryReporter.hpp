#ifndef STAR_TELEMETRY_REPORTER_HPP
#define STAR_TELEMETRY_REPORTER_HPP

#include "StarString.hpp"
#include "StarJson.hpp"

namespace Star {

// JSON-artifact face for a telemetry snapshot. Pure read-only consumer -- and since [#252] step 1 that
// sentence is TRUE. It used to sit above a function that called Telemetry::snapshot() AND advanced
// cpu.process.total_us off getrusage: a read-modify-write into the very store it claimed only to read,
// which is the instrument-writes-into-its-subject defect [#267] found. The projection now arrives BY
// VALUE and the sampling moved to its point of measurement, Telemetry::sampleProcessCpu().
//
// THE SIGNATURE IS THE ENFORCEMENT. Taking the snapshot as a parameter is what lets this file name no
// registry at all, which is both why the claim above can be checked by reading the includes and what
// makes step 3's move to `metrics` possible without an upward include edge.
class TelemetryReporter {
public:
  // Serialize the GIVEN snapshot to <dir>/telemetry/telemetry-<stamp>.json (creating the dir).
  // Returns the absolute path written. Stamp is a passed-in counter to avoid Date.now()-style calls;
  // when 0, a monotonic millisecond stamp is used.
  //
  // `snap` is what Telemetry::snapshot() returned. The caller takes it, which means the caller also
  // chooses WHEN -- and a caller wanting a fresh cpu.process.total_us must call
  // Telemetry::sampleProcessCpu() before snapshot(), because this function no longer does it for them.
  // `meta` carries facts ABOUT THE RUN that no counter can express -- vsync state, GPU clock, package
  // temperature. They are read once at snapshot time. cpu.frame.swap.us means GPU backpressure with vsync off
  // and frame pacing with vsync on: same metric, opposite meanings, so the reader must not be left to
  // remember which run this was.
  //
  // Two further meta fields are ALWAYS written and are not the caller's to supply -- `tMonotonicNs` and
  // `tEpochNs`, stamped after the caller's meta is merged so neither can be shadowed. They are what makes a
  // sequence of snapshots a TIME SERIES rather than a pile of files; see the .cpp for why both clocks are
  // needed and why the filename was not enough. Note they are read from the real clocks even when `stamp` is
  // a caller-supplied counter: the name then ORDERS the files and the fields say WHEN.
  static String writeSnapshot(String const& dir, Json snap, JsonObject meta = {}, uint64_t stamp = 0);
};

}

#endif
