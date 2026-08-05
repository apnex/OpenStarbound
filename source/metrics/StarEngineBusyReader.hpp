#pragma once

#include "StarBusyReading.hpp"

namespace Star {

// System-wide GPU engine busy time from the i915 PMU (/sys/bus/event_source/devices/i915).
//
// SCOPE, stated because it is the whole difference between this and ClientBusyReader: the PMU counts
// the whole device, including work other processes caused. It is therefore the CROSS-CHECK on the
// per-client reader, not a substitute for it. With exactly one GPU client running the two measure the
// same physical quantity by different kernel paths and must agree -- and two instruments that CAN
// disagree is the property the old GPU telemetry never had. Its absence is why a 4x arithmetic error
// and a 4.04x sampling artefact both survived a full day of analysis.
//
// PRIVILEGE is not a portability footnote: perf_event_open against the i915 PMU needs
// perf_event_paranoid <= 0 or CAP_PERFMON, and the development host sits at 2. The ordinary path here
// is UNAVAILABLE, and it must say so -- a privileged instrument that silently reads zero is worse
// than one that is absent, because absence is visible.
class EngineBusyReader {
public:
  EngineBusyReader() = default;
  ~EngineBusyReader();

  EngineBusyReader(EngineBusyReader const&) = delete;
  EngineBusyReader& operator=(EngineBusyReader const&) = delete;

  // Opens the named i915 PMU event ("rcs0-busy", "actual-frequency-gt0", "rc6-residency-gt0") and
  // takes an initial reading. Returns unavailable(reason) when the PMU, the event, or the privilege
  // is missing. The reason NAMES which of the three it was.
  BusyReading open(String const& event);

  // Reads the currently-open counter. Unavailable if open() did not succeed.
  BusyReading sample();

private:
  int m_fd = -1;
  String m_engine;
};

}
