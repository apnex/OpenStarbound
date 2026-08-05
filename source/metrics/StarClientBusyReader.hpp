#pragma once

#include "StarBusyReading.hpp"

namespace Star {

// Reads DRM engine busy time out of fdinfo, DEDUPLICATED BY CLIENT ID.
//
// /proc/<pid>/fdinfo holds one file per open fd, and a process that dups the DRM device gets a file
// per dup -- each reporting the SAME drm-client-id and the SAME whole-client drm-engine-* total,
// not a per-fd share of it. starbound holds four. Summing across fds therefore multiplied busy time
// by four and reported the GPU 96.8% busy against a truth of 24.2%; the error was caught only
// because the next sample read 117.3%, and one engine cannot exceed 100%. Every reading here is
// keyed by drm-client-id and summed over DISTINCT clients, which is the whole reason this class
// exists rather than a loop over dirList.
class ClientBusyReader {
public:
  // Reads /proc/<pid>/fdinfo. Returns BusyReading::unavailable(reason) if the process is gone, the
  // directory is unreadable, or no fd carries drm-engine-* lines.
  static BusyReading read(int pid);

  // Same logic against an arbitrary directory of fdinfo-shaped files, so the dedup rule is testable
  // without a GPU, a driver, or root.
  static BusyReading readFdinfoDir(String const& dir);
};

}
