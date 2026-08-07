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
  JsonObject merged = snap.getObject("meta");
  for (auto const& kv : meta)
    merged[kv.first] = kv.second;

  // WHEN THIS SNAPSHOT WAS TAKEN, ON BOTH CLOCKS, IN THE FILE -- and written AFTER the caller's meta is
  // merged, so a caller cannot shadow them. A timestamp the caller can override is a timestamp a joiner
  // cannot trust.
  //
  // Until now the only time a snapshot carried was its FILENAME, from monotonicMilliseconds(). That put the
  // one field the series is indexed by outside the data: rename, archive or copy the file and its position on
  // the axis is gone. The window arithmetic never noticed because it only ever used two endpoints.
  //
  // TWO CLOCKS, because neither alone does both jobs. MONOTONIC is immune to NTP steps, so it is the one to
  // subtract when computing an interval. EPOCH survives a reboot, is what every plotting tool wants on an
  // x-axis, and is the base the out-of-process samplers already publish (scripts/pmu-engine-sample.py writes
  // epoch; telemetry-window's windowStartEpoch is epoch). Carrying one and deriving the other needs an offset
  // that is itself only valid until the next boot, which is a second thing to get wrong.
  //
  // NANOSECONDS FOR BOTH, matching MetricSample::tMonotonicNs exactly, because a us/ms/ns mixture across one
  // seam is how a consumer ends up off by 1000 with no error -- the defect MetricUnit was earned by. The
  // underlying resolution is coarser (epochTicks is microseconds on unix, 100ns on Windows) and the unit does
  // not claim otherwise; it states the SCALE so arithmetic composes, not the precision.
  //
  // The multiply is exact on both platforms -- 1e9/1e6 and 1e9/1e7 are whole -- and is spelled out here rather
  // than calling a shared helper because the only ticksToNanoseconds in the tree is private to
  // metrics/metrics_main.cpp and StarTime.cpp is stock upstream, where a new helper is merge debt for three
  // lines. Second instance, noted not fixed: task #252 re-homes both when the projection moves to metrics/.
  merged["tMonotonicNs"] = Json(Time::monotonicTicks() * (1'000'000'000 / Time::monotonicTickFrequency()));
  merged["tEpochNs"] = Json(Time::epochTicks() * (1'000'000'000 / Time::epochTickFrequency()));
  snap = snap.set("meta", Json(std::move(merged)));

  File::writeFile(snap.repr(2, true), path); // pretty=2, sort=true
  return path;
}

}
