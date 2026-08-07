#include "StarEngineBusyReader.hpp"

#include "StarFile.hpp"
#include "StarLexicalCast.hpp"

#ifdef STAR_SYSTEM_LINUX
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#endif

namespace Star {

#ifdef STAR_SYSTEM_LINUX

namespace {

  String const PmuDir = "/sys/bus/event_source/devices/i915";
  String const ParanoidPath = "/proc/sys/kernel/perf_event_paranoid";

  // Bounded so a mistyped window is refused instead of parking the calling thread for a year. Same
  // ceiling as `metrics --for`, because they are the same quantity asked for by two front doors.
  double const MaxWindowSeconds = 3600.0;

  // THE COUNTER IS PUBLISHED LAZILY, AND READING IT ONLY AT THE ENDPOINTS READS IT WRONG. Measured on
  // this host against a steady ~25-37% render load, sampling every 0.5s: four of forty intervals
  // advanced by exactly ZERO nanoseconds, and the interval after each one advanced by twice the
  // expected amount. The busy time is not lost, it is DELIVERED LATE.
  //
  // It is staleness relative to the READER, not a fixed kernel period: at a 0.05s interval the
  // zero-advance reads were 10% and never ran more than two deep, at 0.2s they were 6% and never
  // more than two deep. Sampling throughout the window therefore bounds the endpoint error at
  // roughly one poll interval of busy time (~7ms at 35% load) instead of leaving it unbounded.
  int64_t const PollNs = 20'000'000;       // 0.02s, as scripts/pmu-render-busy.py

  // AND THE FIRST READ AFTER perf_event_open IS NOT A ZERO. perf starts the counter at zero and
  // accumulates the difference between consecutive publications, so the first publication after an
  // open delivers everything that accumulated while nobody held the counter: 2.74 SECONDS of busy
  // time arrived in the first 0.5s interval of one trace. Polling across a warm-up lands that
  // catch-up BEFORE t0, where it belongs, rather than inside the window as a fictitious burst.
  int64_t const WarmupNs = 300'000'000;    // 0.30s, as scripts/pmu-render-busy.py

  // Nothing built on File::atEnd can read a sysfs attribute, and the failure is worse than the
  // procfs one it rhymes with. atEnd is `ftell >= fsize`, and fsize is an lseek(SEEK_END) that sysfs
  // answers with the PAGE size, 4096, for a three-byte attribute -- so after the one short read that
  // already yielded the whole value the position sits at 3, every further read returns 0, and
  // File::readFileString spins forever. procfs answers the same lseek with EINVAL, making fsize 0,
  // so the loop exits before reading anything and hands back "". One hangs, one lies. Reading until
  // read() yields 0 is the only end-of-file either filesystem answers truthfully.
  Maybe<String> slurp(String const& path) {
    try {
      FilePtr file = File::open(path, IOMode::Read);
      std::string text;
      char buffer[512];
      while (size_t got = file->read(buffer, sizeof(buffer)))
        text.append(buffer, got);
      return String(std::move(text));
    } catch (StarException const&) {
      return {};
    }
  }

  // The event file holds a perf term list, which for every i915 event today is the single term
  // "config=0x...". Parsed as a list anyway: a kernel that adds a second term must not have it
  // silently absorbed into the value of the one term this reader understands.
  Maybe<uint64_t> parseEventConfig(String const& text) {
    for (auto const& term : text.trim().split(',')) {
      String field = term.trim();
      if (!field.beginsWith("config="))
        continue;
      String value = field.substr(7);
      char const* begin = value.utf8Ptr();
      char* end = nullptr;
      errno = 0;
      uint64_t config = strtoull(begin, &end, 0);   // base 0, because the kernel writes "0x..."
      if (errno != 0 || end == begin || *end != '\0')
        return {};
      return config;
    }
    return {};
  }

  // The trailing token names the counter KIND, not the engine: rcs0-busy, rcs0-sema and rcs0-wait
  // are three quantities of one engine. Stripping it is what leaves an engine name behind. The
  // whole-device time counters the unit check also admits -- rc6-residency-gt0,
  // software-gt-awake-time-gt0 -- carry no such suffix and keep their full name, which is right:
  // they name a device state, and there is no engine to shorten them to.
  String engineNameFor(String const& event) {
    for (String const& kind : {String("-busy"), String("-sema"), String("-wait")}) {
      if (event.endsWith(kind))
        return event.substr(0, event.size() - kind.size());
    }
    return event;
  }

  int64_t monotonicNs() {
    // CLOCK_MONOTONIC directly, rather than Time::monotonicTicks: this code is already inside a
    // Linux-only block, so the portable clock would buy no reach and would cost a second copy of
    // metrics_main.cpp's tick-to-nanosecond conversion.
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1'000'000'000 + (int64_t)now.tv_nsec;
  }

  // nanosleep rather than Thread::sleep, which takes unsigned MILLISECONDS: the last poll of a
  // window is whatever remains of it, routinely under a millisecond, and rounding that to zero turns
  // the tail of every window into a spin on read(). A short sleep costs one wasted poll and nothing
  // else -- the loops below are driven by the clock, not by a count of iterations -- so an EINTR
  // wake needs no restart.
  void sleepNs(int64_t ns) {
    if (ns <= 0)
      return;
    timespec request;
    request.tv_sec = (time_t)(ns / 1'000'000'000);
    request.tv_nsec = (long)(ns % 1'000'000'000);
    nanosleep(&request, nullptr);
  }

  // The counter, or nothing with `reason` saying why. Kept separate from the poll loops so a failed
  // read aborts the window instead of leaving a hole in it that the endpoints cannot see.
  Maybe<uint64_t> readCounter(int fd, String const& engine, String& reason) {
    uint64_t value = 0;
    ssize_t got = ::read(fd, &value, sizeof(value));
    if (got != (ssize_t)sizeof(value)) {
      int err = errno;
      reason = strf("short read from the i915 PMU counter for '{}': {} of {} bytes ({})", engine, got,
                    sizeof(value), got < 0 ? strerror(err) : "no error reported");
      return {};
    }
    return value;
  }

}

EngineOpenResult EngineBusyReader::open(String const& event) {
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  m_engine = String();

  // The event name is concatenated into a sysfs path. A name carrying a separator would read some
  // other PMU's config and then submit it under the i915 type -- a wrong number wearing a right
  // label, which is precisely the outcome this component exists to make impossible.
  if (event.contains("/"))
    return EngineOpenResult::refused(strf("i915 PMU event name '{}' is not a single path component", event));

  String typePath = strf("{}/type", PmuDir);
  auto typeText = slurp(typePath);
  Maybe<uint32_t> type = typeText ? maybeLexicalCast<uint32_t>(typeText->trim()) : Maybe<uint32_t>();
  if (!type)
    return EngineOpenResult::refused(strf("no i915 PMU on this host: '{}' is unreadable or holds no "
                                          "PMU type number", typePath));

  String eventPath = strf("{}/events/{}", PmuDir, event);
  auto eventText = slurp(eventPath);
  Maybe<uint64_t> config = eventText ? parseEventConfig(*eventText) : Maybe<uint64_t>();
  if (!config)
    return EngineOpenResult::refused(strf("the i915 PMU exposes no event '{}': '{}' is unreadable or "
                                          "carries no config= term", event, eventPath));

  // THE UNIT IS ASKED OF THE DRIVER, NOT INFERRED FROM THE NAME. The result field is busyNs and
  // the result is a ratio against wall-clock nanoseconds, so only a counter of TIME may reach it:
  // this PMU also exposes actual-frequency-gt0 in megahertz and interrupts as a bare count, and
  // admitting either would put a number under a name asserting a unit it does not have -- the exact
  // defect this component exists to end, reproduced inside it.
  //
  // This used to be a test on the event's name suffix, which is a guess about a convention rather
  // than a reading of a declaration. It refused rc6-residency-* and software-gt-awake-time-*, both
  // of which the driver declares in ns; and the header above it listed actual-frequency-gt0 and
  // rc6-residency-gt0 among the events to open, while the rule silently refused both. A rule a
  // machine reads can be checked against the hardware. A naming convention drifts from the sentence
  // beside it and nothing notices.
  String unitPath = strf("{}.unit", eventPath);
  auto unit = slurp(unitPath);
  if (!unit)
    return EngineOpenResult::refused(strf("the i915 PMU declares no unit for event '{}': '{}' is "
                                          "unreadable, and an undeclared counter cannot be assumed "
                                          "to be nanoseconds", event, unitPath));
  if (unit->trim() != "ns")
    return EngineOpenResult::refused(strf("the i915 PMU declares event '{}' in '{}', not nanoseconds: "
                                          "a ratio of it against wall-clock nanoseconds would not be "
                                          "a busy fraction", event, unit->trim()));

  perf_event_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.type = *type;
  attr.config = *config;

  // pid -1, cpu 0: the i915 PMU counts a device rather than a task, and a per-task open of it is
  // rejected outright. That system-wide scope is exactly what perf_event_paranoid gates, so the
  // refusal below is the ORDINARY path on an unprivileged host, not an exceptional one.
  int fd = (int)syscall(__NR_perf_event_open, &attr, -1, 0, -1, 0);
  if (fd < 0) {
    int err = errno;
    String paranoid = "unreadable";
    if (auto text = slurp(ParanoidPath))
      paranoid = text->trim();
    return EngineOpenResult::refused(strf("perf_event_open('{}', i915 type {}, config {:#x}) failed: "
                                          "{} -- perf_event_paranoid is {}, and a system-wide PMU "
                                          "open needs <=0 or CAP_PERFMON", event, *type, *config,
                                          strerror(err), paranoid));
  }

  m_fd = fd;
  m_engine = engineNameFor(event);

  EngineOpenResult result;
  result.opened = true;
  return result;
}

EngineBusyWindow EngineBusyReader::busyOver(double seconds) {
  // Argument before state: a window that is not a duration is a caller error whether or not a
  // counter happens to be open. Written as a positive range rather than `seconds <= 0` so that a
  // NaN is refused HERE -- converting one to the integer deadline below is undefined, and whatever
  // it lands on the loop exits immediately and the zero-length window is then reported under the
  // wall-clock guard's name instead of the caller's.
  if (!(seconds > 0.0 && seconds <= MaxWindowSeconds))
    return EngineBusyWindow::unavailable(strf("{} is not a measurable window: busyOver expects "
                                              "seconds in (0, {}]", seconds, MaxWindowSeconds));
  if (m_fd < 0)
    return EngineBusyWindow::unavailable("no i915 PMU counter is open: open() did not succeed");

  String reason;

  // Drain the open-time catch-up. Its size is however long the counter went unread, which is not a
  // quantity this process can know, so it is discarded rather than corrected.
  int64_t warmEnd = monotonicNs() + WarmupNs;
  while (monotonicNs() < warmEnd) {
    if (!readCounter(m_fd, m_engine, reason))
      return EngineBusyWindow::unavailable(reason);
    sleepNs(PollNs);
  }

  // The clock is read immediately AFTER each sample, not around the pair: the counter is latched by
  // the read, so end-of-read is the instant the value belongs to, and bracketing would fold this
  // process's own syscall time into one end of the window only.
  auto busy0 = readCounter(m_fd, m_engine, reason);
  if (!busy0)
    return EngineBusyWindow::unavailable(reason);
  int64_t t0 = monotonicNs();
  int64_t end = t0 + (int64_t)(seconds * 1e9);

  uint64_t busy1 = *busy0;
  int64_t t1 = t0;
  while (t1 < end) {
    sleepNs(std::min(PollNs, end - t1));
    auto busy = readCounter(m_fd, m_engine, reason);
    if (!busy)
      return EngineBusyWindow::unavailable(reason);
    busy1 = *busy;
    t1 = monotonicNs();
  }

  // Every guard below reports unavailability rather than clamping. A clamped zero is
  // indistinguishable from an idle GPU, which is the confusion this whole component exists to end.
  int64_t wallNs = t1 - t0;
  if (wallNs <= 0)
    return EngineBusyWindow::unavailable(strf("non-positive sampling window: {} ns", wallNs));
  if (busy1 < *busy0)
    return EngineBusyWindow::unavailable(strf("the i915 counter for '{}' moved backwards ({} -> {})",
                                              m_engine, *busy0, busy1));
  uint64_t busyNs = busy1 - *busy0;
  // 2^63 nanoseconds is 292 years, so this is not a window anyone measured: it is a counter reading
  // that would land in a signed field as a NEGATIVE busy time, which is a wrong number wearing the
  // right label rather than a large one.
  if (busyNs > (uint64_t)std::numeric_limits<int64_t>::max())
    return EngineBusyWindow::unavailable(strf("the i915 counter for '{}' advanced {} ns over {} ns, "
                                              "which no window contains", m_engine, busyNs, wallNs));

  EngineBusyWindow window;
  window.busy.busyNs[m_engine] = (int64_t)busyNs;
  // `clients` stays 0. It counts DISTINCT GPU clients, which is a thing only the per-client reader
  // can resolve; the PMU sees one device and no clients at all. Writing 1 here would be inventing
  // the very quantity the cross-check against ClientBusyReader depends on being independent.
  window.busy.available = true;
  window.wallNs = wallNs;
  return window;
}

#else

// The PMU, perf_event_open and perf_event_paranoid are Linux. Reporting that plainly keeps the one
// contract that matters everywhere: a reader that cannot measure says so instead of returning zero.
EngineOpenResult EngineBusyReader::open(String const& event) {
  return EngineOpenResult::refused(strf("the i915 PMU event '{}' is readable only on Linux: this "
                                        "build has no perf_event_open", event));
}

EngineBusyWindow EngineBusyReader::busyOver(double) {
  return EngineBusyWindow::unavailable("the i915 PMU is readable only on Linux: this build has no "
                                       "perf_event_open");
}

#endif

EngineBusyReader::~EngineBusyReader() {
#ifdef STAR_SYSTEM_LINUX
  if (m_fd >= 0)
    ::close(m_fd);
#endif
}

}
