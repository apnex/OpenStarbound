#include "StarBusyReading.hpp"
#include "StarClientBusyReader.hpp"
#include "StarMetricSample.hpp"

#include "StarFormat.hpp"
#include "StarJson.hpp"
#include "StarLexicalCast.hpp"
#include "StarThread.hpp"
#include "StarTime.hpp"

#include <cmath>
#include <iostream>

namespace Star {

namespace {

  // 64 is EX_USAGE from sysexits.h. The three codes are the tool's contract, not decoration: a caller
  // that cannot separate "could not measure" from "measured zero" has rebuilt the defect this
  // component exists to end, so unavailability gets its own non-zero code and prints nothing to
  // stdout.
  int const ExitOk = 0;
  int const ExitUnavailable = 3;
  int const ExitUsage = 64;

  char const* const SampleSource = "fdinfo:drm-engine";
  char const* const SampleValidWhen = "always";

  double const DefaultWindowSeconds = 5.0;
  double const MaxWindowSeconds = 3600.0;

  // Exact for any tick frequency a platform reports (1 GHz on Linux, 10 MHz on macOS): the split
  // keeps the numerator under 2^63 without the double rounding a floating-point scale would carry
  // into a nanosecond field.
  int64_t ticksToNanoseconds(int64_t ticks, int64_t frequency) {
    return (ticks / frequency) * 1'000'000'000 + (ticks % frequency) * 1'000'000'000 / frequency;
  }

  int64_t monotonicNanoseconds() {
    return ticksToNanoseconds(Time::monotonicTicks(), Time::monotonicTickFrequency());
  }

  int usage(String const& problem) {
    cerrf("metrics: {}\n", problem);
    cerrf("usage: metrics --pid <N> [--for <SECS>] [--json]\n"
          "  --pid <N>       process to measure, from outside it\n"
          "  --for <SECS>    sampling window in seconds (default {})\n"
          "  --json          emit one JSON object instead of the human report\n"
          "exit codes: 0 measured, {} measurement unavailable, {} usage error\n",
          DefaultWindowSeconds, ExitUnavailable, ExitUsage);
    return ExitUsage;
  }

  List<MetricSample> samplesFor(BusyReading const& delta, int64_t wallNs, int64_t tMonotonicNs) {
    // engineNs is a hash map, so its iteration order is stable within a build and meaningless across
    // one. Sorting makes two runs of the same tool diffable.
    auto engines = delta.engineNs.keys();
    engines.sort();

    List<MetricSample> samples;
    for (auto const& engine : engines) {
      int64_t busyNs = delta.engineNs.get(engine);
      samples.append(MetricSample(strf("gpu.engine.{}.busy_ns", engine), (double)busyNs, "ns",
                                  strf("{} engine busy time, summed over this process's DRM clients", engine),
                                  SampleValidWhen, SampleSource, tMonotonicNs));
      samples.append(MetricSample(strf("gpu.engine.{}.busy_ratio", engine), (double)busyNs / (double)wallNs, "ratio",
                                  strf("{} engine busy time as a fraction of wall clock", engine),
                                  SampleValidWhen, SampleSource, tMonotonicNs));
    }
    return samples;
  }

  // One rule for both units, so the printer never has to know which one it holds: busy time is a
  // whole number of nanoseconds and a ratio is not.
  String formatValue(double value) {
    if (value == std::floor(value) && std::fabs(value) < 1e15)
      return strf("{:.0f}", value);
    return strf("{:.6f}", value);
  }

  void printHuman(int pid, int64_t wallNs, BusyReading const& delta, List<MetricSample> const& samples) {
    coutf("metrics: pid {}, window {:.3f}s, {} DRM client(s)\n", pid, (double)wallNs / 1e9, delta.clients);
    for (auto const& sample : samples) {
      // measures and validWhen are printed by DEFAULT, not behind a verbose flag. A tool that prints
      // a bare number teaches its reader to supply the meaning from memory, which is precisely how
      // render.pass.parallax.gpu_us came to mean something other than its name.
      coutf("  {:<36} {:>14} {:<6} measures: {} | valid when: {}\n",
            sample.key, formatValue(sample.value), sample.unit, sample.measures, sample.validWhen);
    }
  }

  Json toJson(int pid, int64_t wallNs, BusyReading const& delta, List<MetricSample> const& samples) {
    JsonArray sampleArray;
    for (auto const& sample : samples) {
      sampleArray.append(JsonObject{
          {"key", sample.key},
          {"value", sample.value},
          {"unit", sample.unit},
          {"measures", sample.measures},
          {"valid_when", sample.validWhen},
          {"source", sample.source},
          {"t_monotonic_ns", sample.tMonotonicNs}
        });
    }

    return JsonObject{
        {"pid", (int64_t)pid},
        {"wall_ns", wallNs},
        {"clients", (uint64_t)delta.clients},
        {"samples", std::move(sampleArray)}
      };
  }

  int metricsMain(int argc, char** argv) {
    // Hand-parsed rather than routed through OptionParser, which strips exactly one leading dash and
    // documents itself as single-dash only: `--pid` would reach it as a flag literally named "-pid"
    // and every subsequent lookup would carry the stray dash. Three flags do not earn that.
    Maybe<int> pid;
    double windowSeconds = DefaultWindowSeconds;
    bool json = false;

    for (int i = 1; i < argc; ++i) {
      String arg(argv[i]);
      if (arg == "--json") {
        json = true;
      } else if (arg == "--pid" || arg == "--for") {
        if (i + 1 >= argc)
          return usage(strf("{} requires a value", arg));
        String value(argv[++i]);
        if (arg == "--pid") {
          pid = maybeLexicalCast<int>(value);
          if (!pid || *pid <= 0)
            return usage(strf("--pid expects a positive process id, got '{}'", value));
        } else {
          auto seconds = maybeLexicalCast<double>(value);
          // Bounded above because Thread::sleep takes unsigned milliseconds: an unbounded value would
          // wrap the cast and sleep for an arbitrary shorter time, which is a window the caller asked
          // for and did not get.
          if (!seconds || *seconds <= 0.0 || *seconds > MaxWindowSeconds)
            return usage(strf("--for expects seconds in (0, {}], got '{}'", MaxWindowSeconds, value));
          windowSeconds = *seconds;
        }
      } else {
        return usage(strf("unrecognised argument '{}'", arg));
      }
    }

    if (!pid)
      return usage("--pid is required");

    // The clock is read AFTER each fdinfo scan, not around the pair: the counters are latched as the
    // scan proceeds, so end-of-scan is the closest instant either endpoint can be attributed to, and
    // the window must not silently include the scan's own duration at one end only.
    BusyReading before = ClientBusyReader::read(*pid);
    int64_t t0 = monotonicNanoseconds();
    Thread::sleep((unsigned)std::llround(windowSeconds * 1000.0));
    BusyReading after = ClientBusyReader::read(*pid);
    int64_t t1 = monotonicNanoseconds();

    int64_t wallNs = t1 - t0;
    BusyReading delta = busyDelta(before, after, wallNs);
    if (!delta.available) {
      // stdout stays EMPTY. A caller parsing stdout must find no number at all rather than a zero it
      // could mistake for an idle GPU.
      cerrf("metrics: cannot measure pid {}: {}\n", *pid, delta.unavailableReason);
      return ExitUnavailable;
    }

    auto samples = samplesFor(delta, wallNs, t1);
    if (json)
      coutf("{}\n", toJson(*pid, wallNs, delta, samples).printJson(2, true));
    else
      printHuman(*pid, wallNs, delta, samples);
    return ExitOk;
  }

}

}

int main(int argc, char** argv) {
  try {
    return Star::metricsMain(argc, argv);
  } catch (std::exception const& e) {
    // An escaping exception is a failure to measure and exits as one; falling through to 0 would hand
    // the caller a silent success with no samples behind it.
    Star::cerrf("metrics: {}\n", Star::outputException(e, true));
    return Star::ExitUnavailable;
  }
}
