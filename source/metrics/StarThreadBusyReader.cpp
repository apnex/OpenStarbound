#include "StarThreadBusyReader.hpp"

#include "StarFile.hpp"
#include "StarLexicalCast.hpp"
#include "StarProcFs.hpp"

#ifndef STAR_SYSTEM_WINDOWS
#include <unistd.h>
#endif

namespace Star {

namespace {

  // The owner key strings. They MUST match ownerName() in source/core/StarTelemetry.cpp, because a
  // consumer joining this reader's output to the in-process snapshot joins on these exact words -- and
  // that function is private to a translation unit in another component, so this is a second copy of
  // one vocabulary. Task #252 moves the projection tables into metrics/ and deletes this copy; until
  // then the duplication is named rather than hidden, and the static_assert below makes at least the
  // COMPLETENESS of the mapping a compile error rather than a silent gap.
  char const* ownerKey(MetricOwner o) {
    static char const* const names[] = {"unknown", "frame", "gl", "sim", "lighting", "process"};
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)MetricOwner::Count,
                  "MetricOwner gained an enumerator and ThreadBusyReader's key table was not updated");
    return names[(size_t)o];
  }

  // A thread's CPU busy time in clock ticks, or nothing if the line is not a stat line.
  //
  // THE COMM FIELD IS THE TRAP, and it is why this is hand-parsed rather than split on spaces. Field 2
  // of /proc/<tid>/stat is the thread name in parentheses, and a thread name may contain spaces AND
  // parentheses -- the kernel does not escape them. Splitting on whitespace therefore shifts every
  // later field by however many spaces the name happened to contain, and utime lands on some other
  // number entirely: a plausible one, of the right type, silently wrong. The name ends at the LAST
  // ')' on the line, which is the only rule the format actually guarantees.
  //
  // After that ')' the fields resume at field 3 (state), so utime (14) and stime (15) sit at offsets
  // 11 and 12 of what remains.
  Maybe<pair<String, int64_t>> parseStat(String const& text) {
    size_t open = text.utf8().find('(');
    size_t close = text.utf8().rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open)
      return {};
    String comm = String(text.utf8().substr(open + 1, close - open - 1));

    auto rest = String(text.utf8().substr(close + 1)).splitAny(" \t\n");
    if (rest.size() < 13)
      return {};
    auto utime = maybeLexicalCast<int64_t>(rest.at(11));
    auto stime = maybeLexicalCast<int64_t>(rest.at(12));
    if (!utime || !stime)
      return {};
    return make_pair(comm, *utime + *stime);
  }

  // Nanoseconds per clock tick. utime/stime are in USER_HZ units, which is a runtime property of the
  // kernel and NOT the 100 everyone assumes -- asking is one syscall and getting it wrong scales every
  // CPU number by a constant nobody would question, because the shape of the series stays right.
  int64_t nsPerTick() {
    #ifndef STAR_SYSTEM_WINDOWS
    long hz = sysconf(_SC_CLK_TCK);
    if (hz > 0)
      return 1'000'000'000LL / (int64_t)hz;
    #endif
    return 10'000'000LL;  // 100Hz, the near-universal value, used only if the system refuses to say
  }

}

MetricOwner ThreadBusyReader::ownerOfThread(String const& comm) {
  // ORDER MATTERS AND THE DRIVER THREAD IS WHY. Its comm is "starboun:gdrv0" -- the kernel truncates
  // to 15 characters and the driver appends its own suffix, so it shares a prefix with the main
  // thread's "starbound" and a prefix test in the other order would bill every GPU driver tick to
  // Frame. The specific pattern is tested first, deliberately.
  if (comm.contains(":gdrv") || comm.beginsWith("starboun:"))
    return MetricOwner::Gl;
  if (comm == "starbound")
    return MetricOwner::Frame;

  // PREFIXES, because comm is truncated to 15 characters: "WorldServerThread" arrives as
  // "WorldServerThre" and "WorldClient::lightingCalc" as "WorldClient::li". Matching the name a
  // reader of the engine source would expect would match nothing at all.
  if (comm.beginsWith("WorldServerThre"))
    return MetricOwner::Sim;
  if (comm.beginsWith("WorldClient::li"))
    return MetricOwner::Lighting;

  // EVERYTHING ELSE IS Unknown, ON PURPOSE, AND THAT INCLUDES THREADS THAT LOOK LIKE SIMULATION.
  // UniverseServer and SystemWorldServer are simulation work by any reasonable reading -- and owner
  // `sim`'s declared TOTAL is tick.server.total.us, which brackets the WorldServerThread loop and
  // nothing else. Billing them to Sim would put parts under a whole that does not contain them, and
  // the closure check would then be comparing a sum against a denominator it exceeds. Under-claiming
  // is recoverable by adding a rule; over-claiming corrupts the one arithmetic that can catch it.
  return MetricOwner::Unknown;
}

Maybe<int64_t> ThreadBusyReader::processBusyNs(int pid) {
  auto text = ProcFs::read(strf("/proc/{}/stat", pid));
  if (!text)
    return {};
  auto stat = parseStat(*text);
  if (!stat)
    return {};
  return stat->second * nsPerTick();
}

BusyReading ThreadBusyReader::read(int pid) {
  String dir = strf("/proc/{}/task", pid);
  if (!File::isDirectory(dir))
    return BusyReading::unavailable(strf("pid {} exposes no tasks: '{}' is not a directory", pid, dir));
  return readTaskDir(dir);
}

BusyReading ThreadBusyReader::readTaskDir(String const& dir) {
  List<pair<String, bool>> entries;
  try {
    entries = File::dirList(dir);
  } catch (StarException const& e) {
    return BusyReading::unavailable(strf("cannot list '{}': {}", dir, e.what()));
  }

  int64_t const perTick = nsPerTick();
  BusyReading reading;
  unsigned threads = 0;
  for (auto const& entry : entries) {
    // A thread that exits between the listing and the open is normal traffic, not a failure: ProcFs
    // returns nothing and this skips it. It is NOT recorded as a zero, which would claim the thread
    // was measured and found idle.
    auto text = ProcFs::read(File::relativeTo(dir, entry.first + "/stat"));
    if (!text)
      continue;
    auto stat = parseStat(*text);
    if (!stat)
      continue;
    reading.busyNs[ownerKey(ownerOfThread(stat->first))] += stat->second * perTick;
    ++threads;
  }

  if (!threads)
    return BusyReading::unavailable(strf("no task under '{}' supplied a parsable stat line", dir));

  reading.threads = threads;
  reading.available = true;
  return reading;
}

}
