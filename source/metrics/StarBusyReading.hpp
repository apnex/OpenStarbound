#pragma once

#include "StarMap.hpp"
#include "StarString.hpp"

namespace Star {

// A reader result that cannot be mistaken for a measurement of zero.
//
// render.pass.compose.gpu_us reported 0us for its entire existence, because it bracketed a
// conditional that never ran and an empty bracket looks exactly like free work. `available` is
// therefore not a convenience: it is the difference between "measured nothing" and "could not
// measure", which no caller may conflate.
struct BusyReading {
  static BusyReading unavailable(String reason) {
    BusyReading r;
    r.unavailableReason = std::move(reason);
    return r;
  }

  bool available = false;
  String unavailableReason;        // non-empty exactly when !available
  StringMap<int64_t> engineNs;     // "render" -> busy nanoseconds, summed over DISTINCT clients
  unsigned clients = 0;
};

}
