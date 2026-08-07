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
  String unavailableReason;      // non-empty exactly when !available

  // BUSY NANOSECONDS, KEYED BY WHATEVER THAT READER ATTRIBUTES TO -- a GPU engine class ("render",
  // "copy") for the two DRM readers, a MetricOwner ("sim", "lighting") for the thread reader. It was
  // called `engineNs` while both readers happened to key by engine, and the third one does not: a CPU
  // thread's nanoseconds under a field named for a GPU engine is a name asserting one quantity while
  // holding another, which is the defect class this whole component exists to remove. The field says
  // WHAT the number is; naming the key is each reader's job, in its own header.
  StringMap<int64_t> busyNs;

  // HOW MANY SUBJECTS WERE SUMMED, in the terms each reader counts in. Exactly one of these is set by
  // any given reader and the rest stay 0, which is why no consumer may read either without knowing
  // which reader produced the value -- a 0 here means "not this reader's unit", never "none found".
  // A reader with nothing to count returns unavailable() instead, so the ambiguity never reaches a
  // caller that is holding an available reading.
  //
  // Two near-identical counters is the seam task #250 collapses: once every reader hands back
  // MetricSamples, the count becomes a declared metric with its own descriptor rather than a field
  // whose meaning depends on provenance the type does not carry.
  unsigned clients = 0;   // DISTINCT DRM clients        -- ClientBusyReader only
  unsigned threads = 0;   // tasks with a parsable stat  -- ThreadBusyReader only
};

// Difference two readings taken wallNs apart. Either endpoint being unavailable poisons the result,
// and so does a counter that moved backwards (the client restarted): extrapolating across a reset
// invents a delta the hardware never did.
BusyReading busyDelta(BusyReading const& a, BusyReading const& b, int64_t wallNs);

}
