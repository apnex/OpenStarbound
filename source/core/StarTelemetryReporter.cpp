#include "StarTelemetryReporter.hpp"
#include "StarTelemetry.hpp"
#include "StarFile.hpp"
#include "StarJson.hpp"
#include "StarTime.hpp"

namespace Star {

String TelemetryReporter::writeSnapshot(String const& dir, uint64_t stamp) {
  String subdir = File::relativeTo(dir, "telemetry");
  if (!File::isDirectory(subdir))
    File::makeDirectory(subdir);
  uint64_t s = stamp ? stamp : (uint64_t)Time::monotonicMilliseconds();
  String path = File::relativeTo(subdir, strf("telemetry-{}.json", s));
  Json snap = Telemetry::snapshot();
  File::writeFile(snap.repr(2, true), path); // pretty=2, sort=true
  return path;
}

}
