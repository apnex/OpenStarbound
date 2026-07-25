#include "StarTelemetryReporter.hpp"
#include "StarTelemetry.hpp"
#include "StarFile.hpp"
#include "StarJson.hpp"
#include "StarTime.hpp"

#ifndef STAR_SYSTEM_WINDOWS
#include <sys/resource.h>
#endif

namespace Star {

String TelemetryReporter::writeSnapshot(String const& dir, JsonObject meta, uint64_t stamp) {
  String subdir = File::relativeTo(dir, "telemetry");
  if (!File::isDirectory(subdir))
    File::makeDirectory(subdir);
  uint64_t s = stamp ? stamp : (uint64_t)Time::monotonicMilliseconds();
  String path = File::relativeTo(subdir, strf("telemetry-{}.json", s));

  // The core-contention blind spot a single-thread budget cannot see: if the server thread saturates a core, a
  // CPU-cheaper render path frees nothing, and the frame budget alone would never say so. Recorded as a
  // COUNTER rather than a meta field so the generic differencing path windows it with no special case.
  #ifndef STAR_SYSTEM_WINDOWS
  {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
      uint64_t us = (uint64_t)ru.ru_utime.tv_sec * 1000000u + (uint64_t)ru.ru_utime.tv_usec
                  + (uint64_t)ru.ru_stime.tv_sec * 1000000u + (uint64_t)ru.ru_stime.tv_usec;
      auto c = Telemetry::counter("cpu.process.total_us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Process, MetricCadence::Call, MetricRole::Total});
      // getrusage is already cumulative; advance the counter to it rather than adding, so repeated snapshots
      // do not compound. Guarded because the counter is monotonic and a clock that went backwards would wrap.
      //
      // THIS READ-MODIFY-WRITE IS SAFE ONLY BECAUSE writeSnapshot IS CALLED FROM ONE THREAD. value() and inc()
      // are each atomic, but the read-compare-advance sequence is not: two concurrent callers would both see
      // the same `prior` and the second inc() would overcount. Both production callers reach here on the
      // client's main thread -- ClientApplication::update's interval path, and /telemetry snapshot via
      // MainInterface::update earlier in that same update() call. Adding a caller on any other thread (a
      // server-side writer is the obvious one) REQUIRES replacing this with a compare-exchange or a fetch-max
      // primitive. Written down here rather than left in a review comment, because the next contributor will
      // read the code and not the review.
      uint64_t prior = c.value();
      if (us > prior)
        c.inc(us - prior);
    }
  }
  #endif

  Json snap = Telemetry::snapshot();
  if (!meta.empty()) {
    JsonObject merged = snap.getObject("meta");
    for (auto const& kv : meta)
      merged[kv.first] = kv.second;
    snap = snap.set("meta", Json(std::move(merged)));
  }
  File::writeFile(snap.repr(2, true), path); // pretty=2, sort=true
  return path;
}

}
