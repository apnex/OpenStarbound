#pragma once

#include "StarNetElement.hpp"
#include "StarLogging.hpp"

namespace Star {

// Mixin for the NetElement that should be the top element for a network, wraps
// any NetElement class and manages the NetElementVersion.
template <typename BaseNetElement>
class NetElementTop : public BaseNetElement {
public:
  NetElementTop();

  // Writes the state update to the given DataStream then returns the version
  // code that should be passed to the next call to writeState.  If
  // 'fromVersion' is 0, then this is a full write for an initial read of a
  // slave NetElementTop.
  pair<ByteArray, uint64_t> writeNetState(uint64_t fromVersion = 0, NetCompatibilityRules rules = {});
  // Reads a state produced by a call to writeState, optionally with the
  // interpolation delay time for the data contained in this state update.  If
  // the state is a full update rather than a delta, the interoplation delay
  // will be ignored.  Blank updates are not necessary to send to be read by
  // readState, *unless* extrapolation is enabled.  If extrapolation is
  // enabled, reading a blank update calls 'blankNetDelta' which is necessary
  // to not improperly extrapolate past the end of incoming deltas.
  void readNetState(ByteArray data, float interpolationTime = 0.0f, NetCompatibilityRules rules = {});

private:
  using BaseNetElement::initNetVersion;
  using BaseNetElement::netStore;
  using BaseNetElement::netLoad;
  using BaseNetElement::writeNetDelta;
  using BaseNetElement::readNetDelta;
  using BaseNetElement::blankNetDelta;
  using BaseNetElement::checkWithRules;

  // The verbatim pre-lever delta walk, shared by the real path and validate-mode.
  pair<ByteArray, uint64_t> writeNetDeltaState(uint64_t fromVersion, NetCompatibilityRules rules);

  NetElementVersion m_netVersion;
};

template <typename BaseNetElement>
NetElementTop<BaseNetElement>::NetElementTop() {
  BaseNetElement::initNetVersion(&m_netVersion);
}

template <typename BaseNetElement>
pair<ByteArray, uint64_t> NetElementTop<BaseNetElement>::writeNetState(uint64_t fromVersion, NetCompatibilityRules rules) {
  // Full store: never an early-out (always serializes the whole element).
  if (fromVersion == 0) {
    DataStreamBuffer ds;
    ds.setStreamCompatibilityVersion(rules);
    ds.write<bool>(true);
    BaseNetElement::netStore(ds, rules);
    return {ds.takeData(), m_netVersion.increment()};
  }

  // Lever #4: O(1) net-delta dirty-version early-out, config-gated (both flags
  // default OFF -> identical to the plain walk below; the only added cost is two
  // relaxed atomic loads, and latestChange() is not even evaluated). PRECONDITION
  // when enabled: netStorePump() already ran this tick for this master entity so
  // the aggregate is authoritative.
  bool enabled = NetElementEarlyOut::enabled.load(std::memory_order_relaxed);
  bool validate = NetElementEarlyOut::validate.load(std::memory_order_relaxed);
  // STRICT '<' is mandatory: a change stamped exactly at fromVersion still needs
  // sending (matches every per-element 'stamp >= fromVersion' emit test); '<='
  // would drop a steady-state change -> silent desync.
  bool wouldEarlyOut = (enabled || validate) && m_netVersion.latestChange() < fromVersion;

  // Fast path: real early-out (validation off) -> skip the buffer alloc + the
  // whole tree walk. This is the win.
  if (enabled && !validate && wouldEarlyOut)
    return {ByteArray(), m_netVersion.current()};  // no increment

  // Authoritative walk (byte-for-byte the pre-lever behaviour).
  pair<ByteArray, uint64_t> walk = writeNetDeltaState(fromVersion, rules);

  // Validate-mode: cross-check the early-out's CONTRACT against the real walk
  // (wouldEarlyOut => the walk produced {empty, current()}). Always returns the
  // authoritative walk, so this is provably desync-free to run live in-game and
  // surfaces a missing composite netStorePump override as a logged MISMATCH.
  if (validate) {
    bool walkEmpty = walk.first.empty();
    if (wouldEarlyOut && !walkEmpty) {
      Logger::error("netDelta early-out MISMATCH (would have dropped a delta): latestChange={} fromVersion={} bytes={}",
          m_netVersion.latestChange(), fromVersion, walk.first.size());
      starAssert(false);
    } else if (wouldEarlyOut && walk.second != m_netVersion.current()) {
      Logger::error("netDelta early-out MISMATCH (version drift): walkVer={} current={}",
          walk.second, m_netVersion.current());
      starAssert(false);
    } else if (!wouldEarlyOut && walkEmpty && m_netVersion.latestChange() >= fromVersion) {
      Logger::debug("netDelta over-signal (needless walk): latestChange={} fromVersion={}",
          m_netVersion.latestChange(), fromVersion);
    }
  }
  return walk;
}

template <typename BaseNetElement>
pair<ByteArray, uint64_t> NetElementTop<BaseNetElement>::writeNetDeltaState(uint64_t fromVersion, NetCompatibilityRules rules) {
  DataStreamBuffer ds;
  ds.setStreamCompatibilityVersion(rules);
  ds.write<bool>(false);
  if (!BaseNetElement::writeNetDelta(ds, fromVersion, rules))
    return {ByteArray(), m_netVersion.current()};
  else
    return {ds.takeData(), m_netVersion.increment()};
}

template <typename BaseNetElement>
void NetElementTop<BaseNetElement>::readNetState(ByteArray data, float interpolationTime, NetCompatibilityRules rules) {
  if (data.empty()) {
    BaseNetElement::blankNetDelta(interpolationTime);
  } else {
    DataStreamBuffer ds(std::move(data));
    ds.setStreamCompatibilityVersion(rules);
    if (ds.read<bool>())
      BaseNetElement::netLoad(ds, rules);
    else
      BaseNetElement::readNetDelta(ds, interpolationTime, rules);
  }
}

}