#ifndef STAR_TELEMETRY_REPORTER_HPP
#define STAR_TELEMETRY_REPORTER_HPP

#include "StarString.hpp"

namespace Star {

// JSON-artifact face for Telemetry. Pure read-only consumer of Telemetry::snapshot().
class TelemetryReporter {
public:
  // Serialize the current snapshot to <dir>/telemetry/telemetry-<stamp>.json (creating the dir).
  // Returns the absolute path written. Stamp is a passed-in counter to avoid Date.now()-style calls;
  // when 0, a monotonic millisecond stamp is used.
  static String writeSnapshot(String const& dir, uint64_t stamp = 0);
};

}

#endif
