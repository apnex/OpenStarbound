#pragma once

#include "StarBusyReading.hpp"

namespace Star {

// What EngineBusyReader::open() answers with, and what it deliberately does NOT: a number.
//
// The predecessor of this type was a BusyReading, because open() returned its first sample. The
// first read after perf_event_open is not a measurement of anything -- perf starts the counter at
// zero and the i915 PMU's first publication delivers everything that accumulated while nobody held
// the counter, 2.74 SECONDS of busy time in one characterised trace. A type with no field to put
// that in is the only version of this rule a caller cannot skip.
struct EngineOpenResult {
  static EngineOpenResult refused(String reason) {
    EngineOpenResult r;
    r.reason = std::move(reason);
    return r;
  }

  bool opened = false;
  String reason;      // non-empty exactly when !opened
};

// One measured window: the busy time and the wall time it was measured over, in one object because
// neither is a result without the other. The quantity this reader exists to produce is a RATIO, and
// its denominator is the wall clock the poll loop actually observed -- which is the `seconds` the
// caller asked for plus however long the closing read took, and is therefore a measurement rather
// than the caller's own request restated.
//
// `busy.engineNs` holds the window's DIFFERENCE, on the same terms as busyDelta's result, so a PMU
// window and a differenced pair of ClientBusyReader samples are the same shape and can be compared
// directly. It is never a raw counter value: see EngineOpenResult for why one of those is not a
// measurement.
struct EngineBusyWindow {
  static EngineBusyWindow unavailable(String reason) {
    EngineBusyWindow w;
    w.busy = BusyReading::unavailable(std::move(reason));
    return w;
  }

  BusyReading busy;
  int64_t wallNs = 0;     // 0 exactly when !busy.available
};

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
//
// THE COUNTER CANNOT BE BRACKETED, WHICH IS WHY THERE IS NO METHOD THAT RETURNS ONE READ. Two
// independent effects were measured on this host, and each on its own is enough to make a two-read
// window report a number the GPU never did:
//
//   * the first read after an open carries unbounded historic catch-up (see EngineOpenResult);
//   * the PMU publishes LAZILY. Under a steady 35% load sampled every 0.5s, 4 of 40 intervals
//     advanced by exactly zero nanoseconds and the next advanced by double. The busy time is not
//     lost, it is delivered LATE, so a window whose last read lands inside a stale interval
//     under-reports by however much is still unpublished. Two-read 8-second windows read 0.000000
//     against a true 29.6%, and exactly half of the truth, on 2 of 20 runs.
//
// The 0.065% agreement with `perf stat` that once had this reader marked PROVEN was taken at 99% GPU
// load, where a 2.74s catch-up over a long window and a one-interval stall are both small fractions
// of the total. It was right for the wrong reason. busyOver() is the corrected shape, and it is the
// shape scripts/pmu-render-busy.py carries 40 clean runs of.
class EngineBusyReader {
public:
  EngineBusyReader() = default;
  ~EngineBusyReader();

  EngineBusyReader(EngineBusyReader const&) = delete;
  EngineBusyReader& operator=(EngineBusyReader const&) = delete;

  // Opens the named i915 PMU event and takes no reading. Any event the DRIVER declares in
  // nanoseconds is accepted -- the per-engine rcs0/bcs0/vcs0-busy, -sema and -wait counters, and
  // also rc6-residency-gt* and software-gt-awake-time-gt*. Refusal names which link of the chain
  // broke: the PMU, the event, its declared unit, or the privilege.
  EngineOpenResult open(String const& event);

  // Measures over a window, and is the only way a busy value leaves this class. BLOCKS for a fixed
  // warm-up plus `seconds`; the warm-up is charged on every call, because whether a window is
  // trustworthy must not depend on what this reader happened to do before it.
  //
  // Unavailable, never clamped, if the counter is not open, the window is not a duration, the read
  // fails, or the counter moves backwards. A clamped zero is indistinguishable from an idle GPU,
  // which is the confusion this component exists to end.
  EngineBusyWindow busyOver(double seconds);

private:
  int m_fd = -1;
  String m_engine;
};

}
