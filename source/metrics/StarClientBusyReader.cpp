#include "StarClientBusyReader.hpp"

#include "StarFile.hpp"
#include "StarLexicalCast.hpp"

namespace Star {

namespace {

  String const EnginePrefix = "drm-engine-";
  String const ClientIdKey = "drm-client-id";

  // What one fdinfo file said. Kept separate from the accumulator so a file that yields nothing can
  // be discarded whole, rather than half-merged.
  struct FdInfo {
    Maybe<int64_t> clientId;
    StringMap<int64_t> engineNs;
  };

  // File::readFileString cannot read procfs and returns "" instead of failing. It loops until
  // File::atEnd(), which is `ftell >= fsize`, and fsize is an lseek(SEEK_END) that procfs answers
  // with EINVAL -- so the loop ends before the first read and every /proc file comes back empty,
  // which this reader would then report as a driver exposing no engines. Reading until read()
  // yields 0 is the only end-of-file procfs answers truthfully.
  //
  // Nothing here returns an error for a missing file: fds open and close constantly, and one that
  // vanishes between the dirList and the open is normal traffic, not a failed measurement.
  Maybe<String> slurp(String const& path) {
    try {
      FilePtr file = File::open(path, IOMode::Read);
      std::string text;
      char buffer[4096];
      while (size_t got = file->read(buffer, sizeof(buffer)))
        text.append(buffer, got);
      return String(std::move(text));
    } catch (StarException const&) {
      return {};
    }
  }

  FdInfo parseFdinfo(String const& text) {
    FdInfo info;
    for (auto const& line : text.split('\n')) {
      auto halves = line.split(':', 1);
      if (halves.size() != 2)
        continue;
      String key = halves[0].trim();
      String value = halves[1].trim();

      if (key == ClientIdKey) {
        info.clientId = maybeLexicalCast<int64_t>(value);
      } else if (key.beginsWith(EnginePrefix)) {
        // The unit token is load-bearing, not decoration: the same prefix also carries
        // drm-engine-capacity-<class>, a bare engine count with no unit. Requiring "ns" admits the
        // busy-time lines and rejects the capacity lines, which would otherwise register as an
        // engine named "capacity-render" holding a value that is not a duration.
        auto fields = value.splitWhitespace();
        if (fields.size() == 2 && fields[1] == "ns") {
          if (auto ns = maybeLexicalCast<int64_t>(fields[0]))
            info.engineNs.set(key.substr(EnginePrefix.size()), *ns);
        }
      }
    }
    return info;
  }

}

BusyReading ClientBusyReader::read(int pid) {
  String dir = strf("/proc/{}/fdinfo", pid);
  if (!File::isDirectory(dir))
    return BusyReading::unavailable(strf("pid {} exposes no fdinfo: '{}' is not a directory", pid, dir));
  return readFdinfoDir(dir);
}

BusyReading ClientBusyReader::readFdinfoDir(String const& dir) {
  List<pair<String, bool>> entries;
  try {
    entries = File::dirList(dir);
  } catch (StarException const& e) {
    return BusyReading::unavailable(strf("cannot list '{}': {}", dir, e.what()));
  }

  // ASSIGNED per client, never accumulated. Each fd of a dup'd handle reports the whole client's
  // total, so a second fd carrying client 158 is a restatement of the first, and adding it is
  // exactly the multiply-by-fd-count defect this reader was written to end. Ordered by id so the
  // sum below is reproducible.
  Map<int64_t, StringMap<int64_t>> byClient;
  for (auto const& entry : entries) {
    if (entry.second)
      continue;
    auto text = slurp(File::relativeTo(dir, entry.first));
    if (!text)
      continue;
    auto info = parseFdinfo(*text);
    // An fd with no engine line may not displace one that has them. Last-write-wins is a statement
    // about duplicate totals; it is not a licence for a silent fd to erase a real reading.
    if (info.clientId && !info.engineNs.empty())
      byClient[*info.clientId] = std::move(info.engineNs);
  }

  BusyReading reading;
  for (auto const& client : byClient) {
    for (auto const& engine : client.second)
      reading.busyNs[engine.first] += engine.second;
  }

  if (reading.busyNs.empty())
    return BusyReading::unavailable(strf("no fd under '{}' carries drm-engine-* busy time", dir));

  reading.clients = (unsigned)byClient.size();
  reading.available = true;
  return reading;
}

BusyReading busyDelta(BusyReading const& a, BusyReading const& b, int64_t wallNs) {
  if (!a.available)
    return BusyReading::unavailable(strf("start sample unavailable: {}", a.unavailableReason));
  if (!b.available)
    return BusyReading::unavailable(strf("end sample unavailable: {}", b.unavailableReason));
  if (wallNs <= 0)
    return BusyReading::unavailable(strf("non-positive sampling window: {} ns", wallNs));

  BusyReading d;
  for (auto const& key : b.busyNs) {
    // A subject that exits and restarts resets its counters. The WHOLE reading is discarded, not just
    // the one key: the reset means some unknown amount of busy time happened under an identity this
    // pair of samples cannot see, so no key's difference is trustworthy either.
    //
    // Worded by KEY rather than by engine because three readers now share this: a DRM client that
    // restarted, a PMU counter that was reopened, and a THREAD that exited and had its tid reused --
    // the last being why the thread reader needs this and not a variant of it.
    int64_t before = a.busyNs.value(key.first, 0);
    if (key.second < before)
      return BusyReading::unavailable(strf("busy counter '{}' moved backwards ({} -> {}): the subject "
                                           "this pair was measuring is not the same one", key.first,
                                           before, key.second));
    d.busyNs[key.first] = key.second - before;
  }

  // b's count, not a's: the delta describes the window's end state, and a client that appeared or
  // left mid-window is exactly what the caller must be able to see.
  d.clients = b.clients;
  d.available = true;
  return d;
}

}
