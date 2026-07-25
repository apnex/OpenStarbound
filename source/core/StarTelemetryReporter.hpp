#ifndef STAR_TELEMETRY_REPORTER_HPP
#define STAR_TELEMETRY_REPORTER_HPP

#include "StarString.hpp"
#include "StarJson.hpp"

namespace Star {

// JSON-artifact face for Telemetry. Pure read-only consumer of Telemetry::snapshot().
class TelemetryReporter {
public:
  // Serialize the current snapshot to <dir>/telemetry/telemetry-<stamp>.json (creating the dir).
  // Returns the absolute path written. Stamp is a passed-in counter to avoid Date.now()-style calls;
  // when 0, a monotonic millisecond stamp is used.
  // `meta` carries facts ABOUT THE RUN that no counter can express -- vsync state, GPU clock, package
  // temperature. They are read once at snapshot time. cpu.frame.swap.us means GPU backpressure with vsync off
  // and frame pacing with vsync on: same metric, opposite meanings, so the reader must not be left to
  // remember which run this was.
  static String writeSnapshot(String const& dir, JsonObject meta = {}, uint64_t stamp = 0);
};

}

#endif
