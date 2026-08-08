#include "StarBusyReading.hpp"
#include "StarClientBusyReader.hpp"
#include "StarMetricSample.hpp"
#include "StarProcFs.hpp"
#include "StarThreadBusyReader.hpp"

#include "StarFormat.hpp"
#include "StarJson.hpp"
#include "StarLexicalCast.hpp"
#include "StarThread.hpp"
#include "StarTime.hpp"

#include <cmath>
#include <csignal>
#include <iostream>
#include <limits>

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
  char const* const CpuSource = "procfs:task-stat";
  // Threads that no rule claims land under owner `unknown` rather than being dropped, so this
  // holds whatever the process was doing -- attributed or not -- and the sum is the process.
  char const* const CpuValidWhen = "always";

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

  // THE JOIN AXIS. Monotonic is the ruler and cannot be compared between processes; epoch is the only
  // clock this tool and the in-process reporter share, and every consumer that puts them on one axis
  // has to line them up by it. Both are emitted per row precisely because they answer different
  // questions -- see the raw-mode banner.
  int64_t epochNanoseconds() {
    return ticksToNanoseconds(Time::epochTicks(), Time::epochTickFrequency());
  }

  int usage(String const& problem) {
    cerrf("metrics: {}\n", problem);
    cerrf("usage: metrics --pid <N> [--for <SECS>] [--json]\n"
          "       metrics --pid <N> --raw [--every <SECS>] [--for <SECS>]\n"
          "  --pid <N>       process to measure, from outside it\n"
          "  --for <SECS>    windowed mode: the sampling window (default {})\n"
          "                  raw mode: stop streaming after this long (default: one sample, then exit)\n"
          "  --json          emit one JSON object instead of the human report (windowed mode only)\n"
          "  --raw           emit CUMULATIVE counter readings as TSV instead of a windowed delta\n"
          "  --every <SECS>  raw mode: sample on this cadence until --for elapses or SIGTERM\n"
          "exit codes: 0 measured, {} measurement unavailable, {} usage error\n",
          DefaultWindowSeconds, ExitUnavailable, ExitUsage);
    return ExitUsage;
  }

  List<MetricSample> samplesFor(BusyReading const& delta, int64_t wallNs, int64_t tMonotonicNs) {
    // busyNs is a hash map, so its iteration order is stable within a build and meaningless across
    // one. Sorting makes two runs of the same tool diffable.
    //
    // The keys ARE engine names here because this function is only ever handed a DRM reader's result;
    // the field itself is keyed by whatever its reader attributes to, which is why it is no longer
    // named for engines. When this moves into the library (task #250) that assumption becomes the
    // caller's to state rather than this function's to assume.
    auto engines = delta.busyNs.keys();
    engines.sort();

    List<MetricSample> samples;
    for (auto const& engine : engines) {
      int64_t busyNs = delta.busyNs.get(engine);
      samples.append(MetricSample(strf("gpu.engine.{}.busy_ns", engine), (double)busyNs, "ns",
                                  strf("{} engine busy time, summed over this process's DRM clients", engine),
                                  SampleValidWhen, SampleSource, tMonotonicNs));
      samples.append(MetricSample(strf("gpu.engine.{}.busy_ratio", engine), (double)busyNs / (double)wallNs, "ratio",
                                  strf("{} engine busy time as a fraction of wall clock", engine),
                                  SampleValidWhen, SampleSource, tMonotonicNs));
    }
    return samples;
  }

  List<MetricSample> cpuSamplesFor(BusyReading const& delta, int64_t wallNs, int64_t tMonotonicNs) {
    auto owners = delta.busyNs.keys();
    owners.sort();

    List<MetricSample> samples;
    for (auto const& owner : owners) {
      int64_t busyNs = delta.busyNs.get(owner);
      samples.append(MetricSample(strf("cpu.owner.{}.busy_ns", owner), (double)busyNs, "ns",
                                  strf("CPU busy time of the threads attributed to owner '{}'", owner),
                                  CpuValidWhen, CpuSource, tMonotonicNs));
      // CORES, NOT A RATIO, AND THE NAME HAS TO SAY SO. The GPU figure beside it is a fraction of one
      // engine's wall time and cannot exceed 1; this one is busy time over wall time for a set of
      // THREADS, so an owner running four of them flat out reads 4.0. Calling it a ratio would invite
      // exactly the reading it cannot bear, and the two numbers sit in the same output.
      samples.append(MetricSample(strf("cpu.owner.{}.busy_cores", owner),
                                  (double)busyNs / (double)wallNs, "ratio",
                                  strf("CPU busy time of owner '{}' as a multiple of ONE core", owner),
                                  CpuValidWhen, CpuSource, tMonotonicNs));
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

  // ============================================================================================
  // RAW MODE -- the cumulative counters themselves, rather than a delta over a window this tool chose.
  //
  // WHY A SECOND OUTPUT SHAPE EXISTS. The windowed mode above picks a window, differences its
  // endpoints and reports a rate. That is the right answer to "how busy is this process now" and the
  // wrong one for a series, because A RATE CANNOT BE RE-WINDOWED: the mean of ninety one-second
  // ratios is not the busy fraction of the ninety seconds, and no arithmetic downstream recovers it.
  // Cumulative counters can be re-differenced over any interval a consumer later cares about, which
  // is exactly what aligning a sovereign reading to an in-process telemetry interval demands. Looping
  // the windowed mode would have produced a stream of rates and locked the join to this tool's
  // cadence forever.
  //
  // `_total` MARKS A COUNTER AND NOTHING ELSE DOES. A key ending `_total` may be differenced; a key
  // without it (gpu.clients, cpu.threads) is a gauge and may only be read. The windowed mode's keys
  // deliberately do NOT carry the suffix -- gpu.engine.rcs0.busy_ns there is already a difference,
  // and one name meaning both a level and a delta is the confusion this whole component exists to
  // end.
  //
  // NOT EVERY COUNTER IS MONOTONIC, AND NO NAME CAN SAY WHICH. cpu.owner.*.busy_ns_total sums an
  // owner's LIVE threads, so a thread exiting makes it FALL -- the same reset case busyDelta refuses,
  // arriving through a different door. A consumer differencing consecutive rows must read a fall as a
  // reset of that key, never as a negative delta. cpu.process.busy_ns_total beside it is the
  // thread-group aggregate, which retains exited threads and does not fall; the gap between the two
  // is what makes the loss a quantity rather than a silence.
  //
  // BOTH CLOCKS ON EVERY ROW, because they answer different questions. epoch is the JOIN AXIS -- the
  // only clock shared with the process being measured -- and monotonic is the RULER, immune to the
  // NTP step that would move every epoch stamp without moving any work. A consumer rates against
  // monotonic, aligns against epoch, and can see when the two disagree.
  //
  // THE PID IS PART OF THE COUNTER'S IDENTITY and therefore a column. Counters reset when the process
  // does, so differencing two rows carrying different pids is not a small error, it is a number about
  // nothing. The harness re-points this tool at each new client, so the file WILL contain more than
  // one pid.
  char const* const RawUnavailable = "UNAVAILABLE";
  char const* const RawNoDetail = "-";
  char const* const RawHeader = "epoch_ns\tmonotonic_ns\tpid\tkey\tvalue\tdetail\n";

  volatile std::sig_atomic_t g_stopRequested = 0;

  void requestStop(int) {
    g_stopRequested = 1;
  }

  // Tabs and newlines are the format, so a reason carrying either would silently shift every column
  // to its right. Reasons are composed with strf from paths and errno strings and none carries one
  // today, which is precisely the kind of fact that stops being true without anyone noticing.
  String rawSafe(String const& text) {
    return text.replace("\t", " ").replace("\n", " ").replace("\r", " ");
  }

  void rawRow(int pid, int64_t tEpochNs, int64_t tMonotonicNs, String const& key,
              String const& value, String const& detail) {
    coutf("{}\t{}\t{}\t{}\t{}\t{}\n", tEpochNs, tMonotonicNs, pid, key, value, rawSafe(detail));
  }

  // One instant of every sovereign counter. Returns whether ANYTHING was measurable, so a stream can
  // distinguish "ran and read nothing the whole time" from "ran".
  bool emitRawSample(int pid) {
    BusyReading gpu = ClientBusyReader::read(pid);
    BusyReading cpu = ThreadBusyReader::read(pid);
    auto process = ThreadBusyReader::processBusyNs(pid);
    // Clocks read AFTER the scans, for the reason the windowed path reads them there: the counters
    // are latched as each scan proceeds, so end-of-scan is the closest instant the whole set can
    // honestly be attributed to.
    int64_t tEpochNs = epochNanoseconds();
    int64_t tMonotonicNs = monotonicNanoseconds();

    bool measured = false;

    if (gpu.available) {
      auto engines = gpu.busyNs.keys();
      engines.sort();
      for (auto const& engine : engines)
        rawRow(pid, tEpochNs, tMonotonicNs, strf("gpu.engine.{}.busy_ns_total", engine),
               strf("{}", gpu.busyNs.get(engine)), RawNoDetail);
      rawRow(pid, tEpochNs, tMonotonicNs, "gpu.clients", strf("{}", gpu.clients), RawNoDetail);
      measured = true;
    } else {
      // A ROW, NOT A SILENCE. An absent reading and an absent sampler look identical in a series that
      // simply stops writing, and the second is a broken harness while the first is a fact about the
      // process. The reason travels with it so a consumer need not guess which.
      rawRow(pid, tEpochNs, tMonotonicNs, "gpu.reader", RawUnavailable, gpu.unavailableReason);
    }

    if (cpu.available) {
      auto owners = cpu.busyNs.keys();
      owners.sort();
      for (auto const& owner : owners)
        rawRow(pid, tEpochNs, tMonotonicNs, strf("cpu.owner.{}.busy_ns_total", owner),
               strf("{}", cpu.busyNs.get(owner)), RawNoDetail);
      rawRow(pid, tEpochNs, tMonotonicNs, "cpu.threads", strf("{}", cpu.threads), RawNoDetail);
      measured = true;
    } else {
      rawRow(pid, tEpochNs, tMonotonicNs, "cpu.reader", RawUnavailable, cpu.unavailableReason);
    }

    if (process) {
      rawRow(pid, tEpochNs, tMonotonicNs, "cpu.process.busy_ns_total", strf("{}", *process), RawNoDetail);
      measured = true;
    } else {
      rawRow(pid, tEpochNs, tMonotonicNs, "cpu.process.busy_ns_total", RawUnavailable,
             strf("no parsable /proc/{}/stat", pid));
    }

    return measured;
  }

  // Liveness asked of procfs directly rather than inferred from three unavailable readings. The
  // readers are unavailable for reasons that are not death -- a client with no DRM fd yet, a task
  // directory racing a thread exit -- and a stream that quit on those would end the moment the game
  // hesitated.
  bool processIsAlive(int pid) {
    return ProcFs::read(strf("/proc/{}/stat", pid)).isValid();
  }

  int rawMain(int pid, Maybe<double> everySeconds, Maybe<double> forSeconds) {
    coutf("{}", RawHeader);

    if (!everySeconds) {
      bool measured = emitRawSample(pid);
      std::cout.flush();
      return measured ? ExitOk : ExitUnavailable;
    }

    std::signal(SIGTERM, requestStop);
    std::signal(SIGINT, requestStop);

    int64_t intervalNs = (int64_t)std::llround(*everySeconds * 1e9);
    int64_t deadlineNs = forSeconds
        ? monotonicNanoseconds() + (int64_t)std::llround(*forSeconds * 1e9)
        : std::numeric_limits<int64_t>::max();
    int64_t nextNs = monotonicNanoseconds();

    bool everMeasured = false;
    while (!g_stopRequested && monotonicNanoseconds() < deadlineNs) {
      if (!processIsAlive(pid)) {
        // Not a failure. The supervisor points this tool at one client, and a client that has
        // finished its leg is the normal way a stream ends; exiting lets the next pid be discovered
        // rather than sampling a dead one for the rest of the run.
        cerrf("metrics: pid {} is gone; ending the stream\n", pid);
        break;
      }
      everMeasured = emitRawSample(pid) || everMeasured;
      std::cout.flush();

      // Cadence from an ABSOLUTE schedule, not from "sleep the interval after each sample", so the
      // scan's own duration does not accumulate into drift over a run measured in thousands of
      // samples. A slot already missed is SKIPPED rather than made up: bursting to catch up would
      // put two samples microseconds apart and hand a consumer an interval it cannot difference.
      nextNs += intervalNs;
      int64_t now = monotonicNanoseconds();
      if (nextNs <= now)
        nextNs = now + intervalNs;
      // Slices, so SIGTERM ends the run within a frame of arriving rather than at the end of the
      // cadence. The harness kills this from an EXIT trap and then waits for it.
      while (!g_stopRequested && monotonicNanoseconds() < nextNs && monotonicNanoseconds() < deadlineNs)
        Thread::sleep(20);
    }

    // A stream that never read anything exits unavailable even though it ran to completion: the file
    // it produced is all UNAVAILABLE rows, and a caller that only checked the exit code would treat
    // that as a measured run of zeroes.
    return everMeasured ? ExitOk : ExitUnavailable;
  }

  int metricsMain(int argc, char** argv) {
    // Hand-parsed rather than routed through OptionParser, which strips exactly one leading dash and
    // documents itself as single-dash only: `--pid` would reach it as a flag literally named "-pid"
    // and every subsequent lookup would carry the stray dash. Three flags do not earn that.
    Maybe<int> pid;
    double windowSeconds = DefaultWindowSeconds;
    Maybe<double> forSeconds;
    Maybe<double> everySeconds;
    bool json = false;
    bool raw = false;

    for (int i = 1; i < argc; ++i) {
      String arg(argv[i]);
      if (arg == "--json") {
        json = true;
      } else if (arg == "--raw") {
        raw = true;
      } else if (arg == "--pid" || arg == "--for" || arg == "--every") {
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
            return usage(strf("{} expects seconds in (0, {}], got '{}'", arg, MaxWindowSeconds, value));
          if (arg == "--for")
            forSeconds = *seconds;
          else
            everySeconds = *seconds;
        }
      } else {
        return usage(strf("unrecognised argument '{}'", arg));
      }
    }

    if (!pid)
      return usage("--pid is required");

    // REJECTED RATHER THAN RECONCILED. Each of these combinations has a reading a caller could
    // reasonably expect and a different one this tool would deliver, and a flag silently ignored is
    // how a run gets quoted as something it was not.
    if (json && raw)
      return usage("--json and --raw are different output shapes: raw mode emits a stream of TSV rows "
                   "because a series is rows, not one object");
    if (everySeconds && !raw)
      return usage("--every applies to --raw only; the windowed mode takes exactly one window, and "
                   "its length is --for");
    if (raw)
      // `--for` means two different things and the flag cannot be split without breaking the windowed
      // mode's contract: there it is THE WINDOW, here it is a safety stop on a stream that otherwise
      // ends when the process does or when SIGTERM arrives.
      return rawMain(*pid, everySeconds, forSeconds);

    windowSeconds = forSeconds.value(DefaultWindowSeconds);

    // The clock is read AFTER each fdinfo scan, not around the pair: the counters are latched as the
    // scan proceeds, so end-of-scan is the closest instant either endpoint can be attributed to, and
    // the window must not silently include the scan's own duration at one end only.
    BusyReading before = ClientBusyReader::read(*pid);
    BusyReading cpuBefore = ThreadBusyReader::read(*pid);
    auto procBefore = ThreadBusyReader::processBusyNs(*pid);
    int64_t t0 = monotonicNanoseconds();
    Thread::sleep((unsigned)std::llround(windowSeconds * 1000.0));
    BusyReading after = ClientBusyReader::read(*pid);
    BusyReading cpuAfter = ThreadBusyReader::read(*pid);
    auto procAfter = ThreadBusyReader::processBusyNs(*pid);
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

    // CPU IS ADDITIVE HERE, NOT A SECOND WAY TO FAIL. The two readers measure different hardware
    // through different kernel interfaces, and a machine that cannot supply one can still supply the
    // other -- so an unavailable CPU reading is reported on stderr and yields no samples, rather than
    // suppressing a GPU measurement that succeeded. The exit code stays the GPU reader's to decide,
    // which is the contract this tool already had.
    BusyReading cpuDelta = busyDelta(cpuBefore, cpuAfter, wallNs);
    if (cpuDelta.available) {
      samples.appendAll(cpuSamplesFor(cpuDelta, wallNs, t1));

      // ATTRIBUTED VS ACTUAL, because the two are not the same number and the difference is invisible
      // otherwise. /proc/<pid>/stat is a thread-group aggregate that RETAINS the time of threads which
      // have since exited, while /proc/<pid>/task lists only the living -- so a thread that finishes
      // inside the window takes its cost out of the owner sums and leaves it here. Measured
      // deterministically at 49 ticks appearing the instant four burning threads exited.
      //
      // Emitted as a residual rather than folded into an owner: nobody can say WHICH owner the departed
      // thread belonged to, and inventing one would be worse than reporting the gap.
      if (procBefore && procAfter && *procAfter >= *procBefore) {
        int64_t attributed = 0;
        for (auto const& o : cpuDelta.busyNs)
          attributed += o.second;
        int64_t residual = (*procAfter - *procBefore) - attributed;
        samples.append(MetricSample("cpu.unattributed.busy_ns", (double)(residual > 0 ? residual : 0), "ns",
                                    "CPU busy time the process spent that no LIVE thread still accounts "
                                    "for -- threads that exited within the window",
                                    "always", CpuSource, t1));
      }
    } else
      cerrf("metrics: no CPU attribution for pid {}: {}\n", *pid, cpuDelta.unavailableReason);

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
