#include "StarTelemetryReporter.hpp"
#include "StarFile.hpp"
#include "StarJson.hpp"
#include "StarTime.hpp"

// NO StarTelemetry.hpp. Its absence is the point of [#252] step 1 and the precondition for step 3:
// this translation unit now names no registry, so it can move to the sovereign `metrics` component
// without that move creating a `metrics -> telemetry` include edge -- which grant-sweep reads via
// INCLUDE, and which the ruling identifies as its own reversal. If a future edit reaches for
// Telemetry:: here, the fix is a parameter, not an include.

namespace Star {

String TelemetryReporter::writeSnapshot(String const& dir, Json snap, JsonObject meta, uint64_t stamp) {
  String subdir = File::relativeTo(dir, "telemetry");
  if (!File::isDirectory(subdir))
    File::makeDirectory(subdir);
  uint64_t s = stamp ? stamp : (uint64_t)Time::monotonicMilliseconds();
  String path = File::relativeTo(subdir, strf("telemetry-{}.json", s));

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
