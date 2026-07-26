#pragma once

#include "StarJson.hpp"
#include "StarThread.hpp"
#include "StarVersion.hpp"

namespace Star {

STAR_CLASS(Configuration);

STAR_EXCEPTION(ConfigurationException, StarException);

class Configuration {
public:
  Configuration(Json defaultConfiguration, Json currentConfiguration);

  Json defaultConfiguration() const;
  Json currentConfiguration() const;
  String printConfiguration() const;

  Json get(String const& key, Json def = {}) const;
  Json getPath(String const& path, Json def = {}) const;

  Json getDefault(String const& key) const;
  Json getDefaultPath(String const& path) const;

  // Read a key, falling back to the value DECLARED in the default configuration rather than to a literal
  // typed at the call site. Prefer this over get(key, someLiteral) for anything that has a declared
  // default -- see the note in StarConfiguration.cpp for what those literals were costing.
  Json getOrDefault(String const& key) const;
  Json getOrDefaultPath(String const& path) const;

  void set(String const& key, Json const& value);
  void setPath(String const& path, Json const& value);

private:
  mutable Mutex m_mutex;

  Json m_defaultConfig;
  Json m_currentConfig;
};

}
