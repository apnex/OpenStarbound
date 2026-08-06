#include "StarLuaRoot.hpp"
#include "StarAssets.hpp"
#include "StarTelemetry.hpp"

namespace Star {

LuaRoot::LuaRoot() {
  auto& root = Root::singleton();
  m_scriptCache = make_shared<ScriptCache>();

  restart();

  m_rootReloadListener = make_shared<CallbackListener>([cache = m_scriptCache]() {
      cache->clear();
      // Re-read the L2 Proto-cache toggle HERE (the reload fires this listener,
      // including after a runtime `root.setConfiguration` + reload) so the flip
      // applies at reload-time rather than being deferred to — and dependent on —
      // a later loadContextScript observing the dirty flag across the
      // WorldServer/main-thread boundary. Foreign-thread-safe: only a config read
      // (own mutex) + a bool set under the cache mutex; the lua_State touch
      // (clearProtoCache) stays deferred to the owning thread via the dirty flag.
      bool enabled = Root::singleton().configuration()->get("scriptProtoCacheEnabled").toBool();
      cache->setProtoCacheEnabled(enabled);
      Logger::info("Lua Proto cache (scriptProtoCacheEnabled): {}", enabled ? "ENABLED" : "disabled");
    });
  root.registerReloadListener(m_rootReloadListener);

  m_storageDirectory = root.toStoragePath("lua");
}

LuaRoot::~LuaRoot() {
  shutdown();
}

void LuaRoot::loadScript(String const& assetPath) {
  m_scriptCache->loadScript(*m_luaEngine, assetPath);
}

bool LuaRoot::scriptLoaded(String const& assetPath) const {
  return m_scriptCache->scriptLoaded(assetPath);
}

void LuaRoot::unloadScript(String const& assetPath) {
  m_scriptCache->unloadScript(assetPath);
}

void LuaRoot::restart() {
  shutdown();

  auto& root = Root::singleton();

  m_luaEngine = LuaEngine::create(root.configuration()->get("safeScripts").toBool());

  m_luaEngine->setRecursionLimit(root.configuration()->get("scriptRecursionLimit").toUInt());
  m_luaEngine->setInstructionLimit(root.configuration()->get("scriptInstructionLimit").toUInt());
  m_luaEngine->setProfilingEnabled(root.configuration()->get("scriptProfilingEnabled").toBool());
  m_luaEngine->setInstructionMeasureInterval(root.configuration()->get("scriptInstructionMeasureInterval").toUInt());
  m_scriptCache->setProtoCacheEnabled(root.configuration()->get("scriptProtoCacheEnabled").toBool());
}

void LuaRoot::shutdown() {
  clearScriptCache();

  if (!m_luaEngine)
    return;

  auto profile = m_luaEngine->getProfile();
  if (!profile.empty()) {
    profile.sort([](auto const& a, auto const& b) {
        return a.totalTime > b.totalTime;
      });

    std::function<Json (LuaProfileEntry const&)> jsonFromProfileEntry = [&](LuaProfileEntry const& entry) -> Json {
        JsonObject profile;
        profile.set("function", entry.name.value("<function>"));
        profile.set("scope", entry.nameScope.value("?"));
        profile.set("source", strf("{}:{}", entry.source, entry.sourceLine));
        profile.set("self", entry.selfTime);
        profile.set("total", entry.totalTime);
        List<LuaProfileEntry> calls;
        for (auto p : entry.calls)
          calls.append(*p.second);
        profile.set("calls", calls.sorted([](auto const& a, auto const& b) { return a.totalTime > b.totalTime; }).transformed(jsonFromProfileEntry));
        return profile;
      };

    String profileSummary = Json(profile.transformed(jsonFromProfileEntry)).repr(1);

    if (!File::isDirectory(m_storageDirectory)) {
      Logger::info("Creating lua storage directory");
      File::makeDirectory(m_storageDirectory);
    }

    String filename = strf("{}.luaprofile", Time::printCurrentDateAndTime("<year>-<month>-<day>-<hours>-<minutes>-<seconds>-<millis>"));
    String path = File::relativeTo(m_storageDirectory, filename);
    Logger::info("Writing lua profile {}", filename);
    File::writeFile(profileSummary, path);
  }

  m_luaEngine.reset();
}

LuaContext LuaRoot::createContext(String const& script) {
  return createContext(StringList{script});
}

LuaContext LuaRoot::createContext(StringList const& scriptPaths) {
  auto newContext = m_luaEngine->createContext();

  auto cache = m_scriptCache;
  newContext.setRequireFunction([cache](LuaContext& context, LuaString const& module) {
    if (!context.get("_SBLOADED").is<LuaTable>())
      context.set("_SBLOADED", context.createTable());
    auto t = context.get<LuaTable>("_SBLOADED");
    if (!t.contains(module)) {
      t.set(module, true);
      cache->loadContextScript(context, module.toString());
    }
  });

  auto assets = Root::singleton().assets();

  for (auto const& scriptPath : scriptPaths) {
    if (assets->assetExists(scriptPath))
      cache->loadContextScript(newContext, scriptPath);
    else
      Logger::error("Script '{}' does not exist", scriptPath);
  }

  for (auto const& callbackPair : m_luaCallbacks)
    newContext.setCallbacks(callbackPair.first, callbackPair.second);

  return newContext;
}

void LuaRoot::collectGarbage(Maybe<unsigned> steps) {
  if (m_luaEngine)
    m_luaEngine->collectGarbage(steps);
}

void LuaRoot::setAutoGarbageCollection(bool autoGarbageColleciton) {
  if (m_luaEngine)
    m_luaEngine->setAutoGarbageCollection(autoGarbageColleciton);
}

void LuaRoot::tuneAutoGarbageCollection(float pause, float stepMultiplier) {
  if (m_luaEngine)
    m_luaEngine->tuneAutoGarbageCollection(pause, stepMultiplier);
}

size_t LuaRoot::luaMemoryUsage() const {
  return m_luaEngine ? m_luaEngine->memoryUsage() : 0;
}

size_t LuaRoot::scriptCacheMemoryUsage() const {
  return m_luaEngine ? m_scriptCache->memoryUsage() : 0;
}

void LuaRoot::clearScriptCache() const {
  return m_scriptCache->clear();
}

void LuaRoot::addCallbacks(String const& groupName, LuaCallbacks const& callbacks) {
  m_luaCallbacks[groupName] = callbacks;
}

LuaEngine& LuaRoot::luaEngine() const {
  return *m_luaEngine;
}

void LuaRoot::ScriptCache::loadScript(LuaEngine& engine, String const& assetPath) {
  auto assets = Root::singleton().assets();
  RecursiveMutexLocker locker(mutex);
  // Recompiling an ALREADY-cached path (only reachable via the public
  // LuaRoot::loadScript) makes any Proto cached for it stale -> flush. Guarded
  // on contains() so a fresh first load (the only way loadContextScript reaches
  // here, under its !scriptLoaded check) does NOT thrash the Proto cache during
  // warm-up/exploration, where new scripts stream in constantly.
  if (scripts.contains(assetPath))
    protoCacheDirty = true;
  scripts[assetPath] = engine.compile(*assets->bytes(assetPath), assetPath);
}

bool LuaRoot::ScriptCache::scriptLoaded(String const& assetPath) const {
  RecursiveMutexLocker locker(mutex);
  return scripts.contains(assetPath);
}

void LuaRoot::ScriptCache::unloadScript(String const& assetPath) {
  RecursiveMutexLocker locker(mutex);
  scripts.remove(assetPath);
  protoCacheDirty = true;  // conservative whole-cache flush; engine touch deferred to loadContextScript
}

void LuaRoot::ScriptCache::clear() {
  RecursiveMutexLocker locker(mutex);
  scripts.clear();
  protoCacheDirty = true;  // may run on a foreign reload thread; do NOT touch the lua_State here
}

void LuaRoot::ScriptCache::setProtoCacheEnabled(bool enabled) {
  RecursiveMutexLocker locker(mutex);
  protoCacheEnabled = enabled;
}

void LuaRoot::ScriptCache::loadContextScript(LuaContext& context, String const& assetPath) {
  RecursiveMutexLocker locker(mutex);
  if (protoCacheDirty) {
    // Owning thread, under the mutex: now it is safe to touch the lua_State.
    // Re-read the toggle so a runtime config change + reload flips the live A/B.
    context.engine().clearProtoCache();
    protoCacheEnabled = Root::singleton().configuration()->get("scriptProtoCacheEnabled").toBool();
    protoCacheDirty = false;
  }
  if (!scriptLoaded(assetPath))
    loadScript(context.engine(), assetPath);
  // WITNESS for the scriptProtoCacheEnabled lever. BOTH arms are counted, not just the cached one: a
  // consumer differencing two snapshots cannot tell ABSENT from ZERO, so an A/B needs a counter that moves
  // on the OFF leg too. Registered above the branch so both exist from the first load regardless of which
  // arm ran. Nothing else in the corpus distinguishes these paths -- the off path is documented as the
  // original code byte for byte, so its only signature is that it did not use the cache.
  static auto protoCached = Telemetry::counter("script.proto.cache.loaded",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Call, MetricRole::Detail});
  static auto protoDirect = Telemetry::counter("script.proto.cache.bypassed",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Call, MetricRole::Detail});
  if (protoCacheEnabled) {
    protoCached.inc(1);
    context.loadCached(assetPath, scripts.get(assetPath));
  } else {
    protoDirect.inc(1);
    context.load(scripts.get(assetPath));  // OFF == the original code path, byte for byte
  }
}

size_t LuaRoot::ScriptCache::memoryUsage() const {
  RecursiveMutexLocker locker(mutex);
  size_t total = 0;
  for (auto const& p : scripts)
    total += p.second.size();
  return total;
}

}
