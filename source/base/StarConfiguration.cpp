#include "StarConfiguration.hpp"
#include "StarFile.hpp"
#include "StarLogging.hpp"

namespace Star {

Configuration::Configuration(Json defaultConfiguration, Json currentConfiguration)
  : m_defaultConfig(defaultConfiguration), m_currentConfig(currentConfiguration) {}

Json Configuration::defaultConfiguration() const {
  return m_defaultConfig;
}

Json Configuration::currentConfiguration() const {
  MutexLocker locker(m_mutex);
  return m_currentConfig;
}

String Configuration::printConfiguration() const {
  MutexLocker locker(m_mutex);
  return m_currentConfig.printJson(2, true);
}

Json Configuration::get(String const& key, Json def) const {
  MutexLocker locker(m_mutex);
  return m_currentConfig.get(key, def);
}

Json Configuration::getPath(String const& path, Json def) const {
  MutexLocker locker(m_mutex);
  return m_currentConfig.query(path, def);
}

Json Configuration::getDefault(String const& key) const {
  MutexLocker locker(m_mutex);
  return m_defaultConfig.get(key, {});
}

Json Configuration::getDefaultPath(String const& path) const {
  MutexLocker locker(m_mutex);
  return m_defaultConfig.query(path, {});
}

// ONE FALLBACK, AND IT IS THE DECLARED ONE.
//
// `get(key, literal)` makes the literal authoritative whenever the key is absent, which turns every call
// site into a competing declaration of the same knob. That is not hypothetical: the render subsystem had
// five keys whose shipped default and call-site literal disagreed -- lightingGpu (true vs false),
// lightingGpuSpreadIterations (32 vs 64), lightingTonemap (true vs false), lightingWorldUpscale (2.0 vs
// 1.0f), and envRefreshInterval (ships 4, against a `/rendercache status` readout of 1). The status line
// could tell the operator the cache was in one mode while the render path ran in another.
//
// Root back-fills every DECLARED key into the current config at load (StarRoot.cpp), so those literals
// normally lie dormant. What wakes them is an ERASE: Configuration::set(key, {}) removes the key, and the
// render A/B harness restores by writing back whatever `get(key)` returned before the run -- a null Json
// if the key was never declared at all. One A/B on an undeclared key therefore erases it, the config is
// persisted on exit, and the call-site literal becomes the authority for every session afterwards.
//
// Reading through here gives a key exactly one declaration: the default block. This is deliberately NOT a
// change to get(key, def) -- plenty of callers genuinely need "absent means absent".
Json Configuration::getOrDefault(String const& key) const {
  MutexLocker locker(m_mutex);
  return m_currentConfig.get(key, m_defaultConfig.get(key, {}));
}

Json Configuration::getOrDefaultPath(String const& path) const {
  MutexLocker locker(m_mutex);
  return m_currentConfig.query(path, m_defaultConfig.query(path, {}));
}

void Configuration::set(String const& key, Json const& value) {
  MutexLocker locker(m_mutex);
  if (key == "configurationVersion")
    throw ConfigurationException("cannot set configurationVersion");

  if (value)
    m_currentConfig = m_currentConfig.set(key, value);
  else
    m_currentConfig = m_currentConfig.eraseKey(key);
}

void Configuration::setPath(String const& path, Json const& value) {
  MutexLocker locker(m_mutex);
  if (path.splitAny("[].").get(0) == "configurationVersion")
    throw ConfigurationException("cannot set configurationVersion");

  if (value)
    m_currentConfig = m_currentConfig.setPath(path, value);
  else
    m_currentConfig = m_currentConfig.erasePath(path);
}

}
