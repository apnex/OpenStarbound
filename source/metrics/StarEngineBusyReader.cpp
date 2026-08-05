#include "StarEngineBusyReader.hpp"

#include "StarFile.hpp"
#include "StarLexicalCast.hpp"

#ifdef STAR_SYSTEM_LINUX
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#endif

namespace Star {

#ifdef STAR_SYSTEM_LINUX

namespace {

  String const PmuDir = "/sys/bus/event_source/devices/i915";
  String const ParanoidPath = "/proc/sys/kernel/perf_event_paranoid";

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
  // are three quantities of one engine. Stripping it is what leaves an engine name behind, and only
  // the per-engine events have one -- actual-frequency-gt0 and rc6-residency-gt0 describe the whole
  // GT, so they keep the event name and are labelled with it.
  String engineNameFor(String const& event) {
    for (String const& kind : {String("-busy"), String("-sema"), String("-wait")}) {
      if (event.endsWith(kind))
        return event.substr(0, event.size() - kind.size());
    }
    return event;
  }

}

BusyReading EngineBusyReader::open(String const& event) {
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  m_engine = String();

  // The event name is concatenated into a sysfs path. A name carrying a separator would read some
  // other PMU's config and then submit it under the i915 type -- a wrong number wearing a right
  // label, which is precisely the outcome this component exists to make impossible.
  if (event.contains("/"))
    return BusyReading::unavailable(strf("i915 PMU event name '{}' is not a single path component", event));

  String typePath = strf("{}/type", PmuDir);
  auto typeText = slurp(typePath);
  Maybe<uint32_t> type = typeText ? maybeLexicalCast<uint32_t>(typeText->trim()) : Maybe<uint32_t>();
  if (!type)
    return BusyReading::unavailable(strf("no i915 PMU on this host: '{}' is unreadable or holds no "
                                         "PMU type number", typePath));

  String eventPath = strf("{}/events/{}", PmuDir, event);
  auto eventText = slurp(eventPath);
  Maybe<uint64_t> config = eventText ? parseEventConfig(*eventText) : Maybe<uint64_t>();
  if (!config)
    return BusyReading::unavailable(strf("the i915 PMU exposes no event '{}': '{}' is unreadable or "
                                         "carries no config= term", event, eventPath));

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
    return BusyReading::unavailable(strf("perf_event_open('{}', i915 type {}, config {:#x}) failed: "
                                         "{} -- perf_event_paranoid is {}, and a system-wide PMU "
                                         "open needs <=0 or CAP_PERFMON", event, *type, *config,
                                         strerror(err), paranoid));
  }

  m_fd = fd;
  m_engine = engineNameFor(event);
  return sample();
}

BusyReading EngineBusyReader::sample() {
  if (m_fd < 0)
    return BusyReading::unavailable("no i915 PMU counter is open: open() did not succeed");

  uint64_t value = 0;
  ssize_t got = ::read(m_fd, &value, sizeof(value));
  if (got != (ssize_t)sizeof(value)) {
    int err = errno;
    return BusyReading::unavailable(strf("short read from the i915 PMU counter for '{}': {} of {} "
                                         "bytes ({})", m_engine, got, sizeof(value),
                                         got < 0 ? strerror(err) : "no error reported"));
  }

  BusyReading reading;
  reading.engineNs[m_engine] = (int64_t)value;
  // `clients` stays 0. It counts DISTINCT GPU clients, which is a thing only the per-client reader
  // can resolve; the PMU sees one device and no clients at all. Writing 1 here would be inventing
  // the very quantity the cross-check against ClientBusyReader depends on being independent.
  reading.available = true;
  return reading;
}

#else

// The PMU, perf_event_open and perf_event_paranoid are Linux. Reporting that plainly keeps the one
// contract that matters everywhere: a reader that cannot measure says so instead of returning zero.
BusyReading EngineBusyReader::open(String const& event) {
  return BusyReading::unavailable(strf("the i915 PMU event '{}' is readable only on Linux: this "
                                       "build has no perf_event_open", event));
}

BusyReading EngineBusyReader::sample() {
  return BusyReading::unavailable("the i915 PMU is readable only on Linux: this build has no "
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
