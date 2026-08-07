#include "StarProcFs.hpp"

#include "StarFile.hpp"

namespace Star {

namespace ProcFs {

  Maybe<String> read(String const& path) {
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

}

}
