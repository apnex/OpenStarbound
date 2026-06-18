#pragma once

#include <atomic>

#include "StarDataStream.hpp"

namespace Star {

// Monotonically increasing NetElementVersion shared between all NetElements in
// a network.
class NetElementVersion {
public:
  uint64_t current() const;
  uint64_t increment();

  // Per-entity aggregate: the highest version at which ANY element in this
  // network recorded a change. Enables the O(1) net-delta dirty-version
  // early-out (Lever #4). const + mutable because children hold a
  // NetElementVersion const* and must bump the aggregate without re-threading
  // a non-const pointer through the whole hierarchy. markChanged() records the
  // UN-incremented m_version (== current()) — the same units every per-element
  // delta test compares against (leaf m_latestUpdateVersion, signal/dynamic/map
  // change-version).
  void markChanged() const;
  uint64_t latestChange() const;

private:
  uint64_t m_version = 0;
  mutable uint64_t m_latestChange = 0;
};

// Process-global gates for the net-delta dirty-version early-out (Lever #4),
// set once from worldserver.config in WorldServer::init (both default OFF, so a
// shipped binary is byte-identical to today until a key is flipped). Kept
// process-global rather than threaded through World because
// NetElementTop::writeNetState is per-element and has no World/Entity
// back-pointer; mirrors the file-static atomic pattern in StarTelemetry.cpp.
namespace NetElementEarlyOut {
  extern std::atomic<bool> enabled;   // gate the O(1) early-out
  extern std::atomic<bool> validate;  // dual-run + contract-equality check
  // Coverage counters (Lever #4 measurement): hits = entity-writes the
  // dirty-version check let us skip (or WOULD skip in validate mode); walks =
  // entity-writes that still needed a real delta tree-walk. WorldServer logs
  // hits/(hits+walks) periodically and resets the window. Only bumped when a
  // gate is on, so the shipped default path stays byte-for-byte zero-cost.
  extern std::atomic<uint64_t> hits;
  extern std::atomic<uint64_t> walks;
  // True when the pump is needed this tick (either gate on). When BOTH are OFF
  // the drivers skip netStorePump() entirely, so the shipped default adds zero
  // cost (no redundant store pass on top of the unchanged writeNetDelta walk).
  inline bool active() {
    return enabled.load(std::memory_order_relaxed) || validate.load(std::memory_order_relaxed);
  }
}

// Primary interface for the composable network synchronizable element system.
class NetElement {
public:
  virtual ~NetElement() = default;

  // A network of NetElements will have a shared monotonically increasing
  // NetElementVersion.  When elements are updated, they will mark the version
  // number at the time they are updated so that a delta can be constructed
  // that contains only changes since any past version.
  virtual void initNetVersion(NetElementVersion const* version = nullptr) = 0;

  // Full store / load of the entire element.
  virtual void netStore(DataStream& ds, NetCompatibilityRules rules) const = 0;
  virtual void netLoad(DataStream& ds, NetCompatibilityRules rules) = 0;

  // Enables interpolation mode.  If interpolation mode is enabled, then
  // NetElements will delay presenting incoming delta data for the
  // 'interpolationTime' parameter given in readNetDelta, and smooth between
  // received values.  When interpolation is enabled, tickNetInterpolation must
  // be periodically called to smooth values forward in time.  If
  // extrapolationHint is given, this may be used as a hint for the amount of
  // time to extrapolate forward if no deltas are received.
  virtual void enableNetInterpolation(float extrapolationHint = 0.0f);
  virtual void disableNetInterpolation();
  virtual void tickNetInterpolation(float dt);

  // Write all the state changes that have happened since (and including)
  // fromVersion.  The normal way to use this would be to call writeDelta with
  // the version at the time of the *last* call to writeDelta, + 1.  If
  // fromVersion is 0, this will always write the full state.  Should return
  // true if a delta was needed and was written to DataStream, false otherwise.
  virtual bool writeNetDelta(DataStream& ds, uint64_t fromVersion, NetCompatibilityRules rules) const = 0;
  // Read a delta written by writeNetDelta.  'interpolationTime' is the time in
  // the future that data from this delta should be delayed and smoothed into,
  // if interpolation is enabled.
  virtual void readNetDelta(DataStream& ds, float interpolationTime = 0.0f, NetCompatibilityRules rules = {}) = 0;
  // When extrapolating, it is important to notify when a delta WOULD have been
  // received even if no deltas are produced, so no extrapolation takes place.
  virtual void blankNetDelta(float interpolationTime);

  // Run any deferred lazy stores (e.g. NetElementSyncGroup::netElementsNeedStore
  // and embedded composite stores) so the shared per-entity NetElementVersion
  // aggregate is authoritative BEFORE a dirty-version early-out in
  // NetElementTop::writeNetState can skip the delta tree-walk (Lever #4). Must
  // be driven once per master entity per tick before its first writeNetState.
  // Default no-op (leaves and signals have no deferred store).
  virtual void netStorePump() {}

  VersionNumber compatibilityVersion() const;
  void setCompatibilityVersion(VersionNumber version);
  bool checkWithRules(NetCompatibilityRules const& rules) const;
private:
  VersionNumber m_netCompatibilityVersion = AnyVersion;
};

inline VersionNumber NetElement::compatibilityVersion() const {
  return m_netCompatibilityVersion;
}

inline void NetElement::setCompatibilityVersion(VersionNumber version) {
  m_netCompatibilityVersion = version;
}

inline bool NetElement::checkWithRules(NetCompatibilityRules const& rules) const {
  if (m_netCompatibilityVersion != AnyVersion)
    return rules.version() >= m_netCompatibilityVersion;
  return true;
}

}
