#pragma once

#include "StarString.hpp"
#include "StarMaybe.hpp"

namespace Star {

namespace ProcFs {

  // Read a /proc file whole, or nothing.
  //
  // File::readFileString CANNOT READ PROCFS and returns "" instead of failing. It loops until
  // File::atEnd(), which is `ftell >= fsize`, and fsize is an lseek(SEEK_END) that procfs answers
  // with EINVAL -- so the loop ends before the first read and every /proc file comes back empty. A
  // caller then reports a driver exposing no engines, or a process with no threads, having in fact
  // read nothing at all. Reading until read() yields 0 is the only end-of-file procfs answers
  // truthfully.
  //
  // Nothing here returns an error for a missing file. Under /proc the thing being enumerated can
  // disappear between the listing and the open -- fds open and close constantly, threads exit -- and
  // that is normal traffic rather than a failed measurement. An EMPTY Maybe therefore means "this
  // entry went away or could not be opened", which every caller must treat as skip-this-one, never
  // as zero.
  //
  // Shared rather than copied: this is the third reader that needs it, and the paragraph above is the
  // part that must not diverge. It was private to ClientBusyReader while there was one caller.
  Maybe<String> read(String const& path);

}

}
