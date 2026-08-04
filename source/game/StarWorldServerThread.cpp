#include "StarWorldServerThread.hpp"
#include "StarTickRateMonitor.hpp"
#include "StarNpc.hpp"
#include "StarRoot.hpp"
#include "StarLogging.hpp"
#include "StarAssets.hpp"
#include "StarPlayer.hpp"
#include "StarTelemetry.hpp"

namespace Star {

WorldServerThread::WorldServerThread(WorldServerPtr server, WorldId worldId)
  : Thread("WorldServerThread: " + printWorldId(worldId)),
    m_worldServer(std::move(server)),
    m_worldId(std::move(worldId)),
    m_stop(false),
    m_errorOccurred(false),
    m_shouldExpire(true) {
  if (m_worldServer)
    m_worldServer->setWorldId(printWorldId(m_worldId));
}

WorldServerThread::~WorldServerThread() {
  m_stop = true;
  join();

  RecursiveMutexLocker locker(m_mutex);
  for (auto clientId : m_worldServer->clientIds())
    removeClient(clientId);
}

WorldId WorldServerThread::worldId() const {
  return m_worldId;
}

void WorldServerThread::start() {
  m_stop = false;
  m_errorOccurred = false;
  Thread::start();
}

void WorldServerThread::stop() {
  m_stop = true;
  Thread::join();
}

void WorldServerThread::setPause(shared_ptr<const atomic<bool>> pause) {
  m_pause = pause;
}

bool WorldServerThread::serverErrorOccurred() {
  return m_errorOccurred;
}

bool WorldServerThread::shouldExpire() {
  return m_shouldExpire;
}

bool WorldServerThread::spawnTargetValid(SpawnTarget const& spawnTarget) {
  try {
    RecursiveMutexLocker locker(m_mutex);
    return m_worldServer->spawnTargetValid(spawnTarget);
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
    return false;
  }
}

bool WorldServerThread::addClient(ConnectionId clientId, SpawnTarget const& spawnTarget, bool isLocal, bool isAdmin, NetCompatibilityRules netRules) {
  try {
    RecursiveMutexLocker locker(m_mutex);
    if (m_worldServer->addClient(clientId, spawnTarget, isLocal, isAdmin, netRules)) {
      m_clients.add(clientId);
      return true;
    }

    return false;
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
    return false;
  }
}

List<PacketPtr> WorldServerThread::removeClient(ConnectionId clientId) {
  RecursiveMutexLocker locker(m_mutex);
  if (!m_clients.contains(clientId))
    return {};

  RecursiveMutexLocker queueLocker(m_queueMutex);

  List<PacketPtr> outgoingPackets;
  try {
    auto incomingPackets = take(m_incomingPacketQueue[clientId]);
    if (m_worldServer->hasClient(clientId))
      m_worldServer->handleIncomingPackets(clientId, std::move(incomingPackets));

    outgoingPackets = take(m_outgoingPacketQueue[clientId]);
    if (m_worldServer->hasClient(clientId))
      outgoingPackets.appendAll(m_worldServer->removeClient(clientId));

  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
  }

  m_clients.remove(clientId);
  m_incomingPacketQueue.remove(clientId);
  m_outgoingPacketQueue.remove(clientId);
  return outgoingPackets;
}

List<ConnectionId> WorldServerThread::clients() const {
  RecursiveMutexLocker locker(m_mutex);
  return m_clients.values();
}

bool WorldServerThread::hasClient(ConnectionId clientId) const {
  RecursiveMutexLocker locker(m_mutex);
  return m_clients.contains(clientId);
}

bool WorldServerThread::noClients() const {
  RecursiveMutexLocker locker(m_mutex);
  return m_clients.empty();
}


List<ConnectionId> WorldServerThread::erroredClients() const {
  RecursiveMutexLocker locker(m_mutex);
  auto unerroredClients = HashSet<ConnectionId>::from(m_worldServer->clientIds());
  return m_clients.difference(unerroredClients).values();
}

void WorldServerThread::pushIncomingPackets(ConnectionId clientId, List<PacketPtr> packets) {
  RecursiveMutexLocker queueLocker(m_queueMutex);
  m_incomingPacketQueue[clientId].appendAll(std::move(packets));
}

List<PacketPtr> WorldServerThread::pullOutgoingPackets(ConnectionId clientId) {
  RecursiveMutexLocker queueLocker(m_queueMutex);
  return take(m_outgoingPacketQueue[clientId]);
}

Maybe<Vec2F> WorldServerThread::playerRevivePosition(ConnectionId clientId) const {
  try {
    RecursiveMutexLocker locker(m_mutex);
    if (auto player = m_worldServer->clientPlayer(clientId))
      return player->position() + player->feetOffset();
    return {};
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
    return {};
  }
}

Maybe<pair<String, String>> WorldServerThread::pullNewPlanetType() {
  try {
    RecursiveMutexLocker locker(m_mutex);
    return m_worldServer->pullNewPlanetType();
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
    return {};
  }
}

void WorldServerThread::executeAction(WorldServerAction action) {
  RecursiveMutexLocker locker(m_mutex);
  action(this, m_worldServer.get());
}

void WorldServerThread::setUpdateAction(WorldServerAction updateAction) {
  RecursiveMutexLocker locker(m_mutex);
  m_updateAction = updateAction;
}

void WorldServerThread::passMessages(List<Message>&& messages) {
  RecursiveMutexLocker locker(m_messageMutex);
  m_messages.appendAll(std::move(messages));
}

void WorldServerThread::unloadAll(bool force) {
  try {
    RecursiveMutexLocker locker(m_mutex);
    m_worldServer->unloadAll(force);
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
  }
}

WorldChunks WorldServerThread::readChunks() {
  try {
    RecursiveMutexLocker locker(m_mutex);
    return m_worldServer->readChunks();
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
    return {};
  }
}

void WorldServerThread::run() {
  try {
    auto& root = Root::singleton();
    double updateMeasureWindow = root.assets()->json("/universe_server.config:updateMeasureWindow").toDouble();
    double fidelityDecrementScore = root.assets()->json("/universe_server.config:fidelityDecrementScore").toDouble();
    double fidelityIncrementScore = root.assets()->json("/universe_server.config:fidelityIncrementScore").toDouble();

    String serverFidelityMode = root.configuration()->get("serverFidelity").toString();
    Maybe<WorldServerFidelity> lockedFidelity;
    if (!serverFidelityMode.equalsIgnoreCase("automatic"))
      lockedFidelity = WorldServerFidelityNames.getLeft(serverFidelityMode);

    double storageInterval = root.assets()->json("/universe_server.config:worldStorageInterval").toDouble() / 1000.0;
    Timer storageTimer = Timer::withTime(storageInterval);

    TickRateApproacher tickApproacher(1.0f / ServerGlobalTimestep, updateMeasureWindow);
    double fidelityScore = 0.0;
    WorldServerFidelity automaticFidelity = WorldServerFidelity::Medium;

    while (!m_stop && !m_errorOccurred) {
      // THE DENOMINATOR INCREMENTS FIRST, before any phase opens.
      //
      // It used to sit inside update(), after the mutex was taken -- which left two phases (loophead
      // and the lock acquisition itself) running AHEAD of the tick they belong to, so their count read
      // one higher than tick.server.seq and the consumer's count<=denominator assertion fired on
      // perfectly correct data. That is not hypothetical: harness/profiles/branchless.json and
      // harnesstest.json both carry violations of exactly this shape in their committed `violations`
      // arrays ("count 2702 > expected 2701"). Marking here makes every Tick-cadence sim part count at
      // most seq, by construction. The count itself is unchanged: update() is PRIVATE and this loop is
      // its only call site, so the compiler -- not this sentence -- is what keeps one mark per update.
      Telemetry::markTick("server");

      // Hoisted purely so the phase scopes below can exist. `fidelity` is established inside loophead
      // and consumed by update(); `spareTime` is written inside the fidelity phase and read by the
      // pacing sleep, which is deliberately OUTSIDE the total.
      WorldServerFidelity fidelity = automaticFidelity;
      double spareTime = 0.0;

      {
        // OWNER `sim`'s TOTAL -- the whole loop body EXCEPT the pacing sleep. The sleep is the last
        // statement, so it is excluded STRUCTURALLY rather than by subtraction, and this body has
        // exactly one control-flow path (no continue, no break, no return anywhere in it) so the scope
        // opens and closes exactly once per tick.
        //
        // BUSY WITH RESPECT TO THE PACING SLEEP, NOT WITH RESPECT TO BLOCKING. Six lock acquisitions
        // live inside this scope. Each is named (tick.server.lock.*) so `busy = total - blocked` stays
        // recoverable, but the total itself must be read as busy+blocked. Claiming it is "busy by
        // construction" -- which an earlier draft of this design did -- is wrong, and on a contended
        // multiplayer server it would be wrong by a lot.
        static auto tTotal = Telemetry::timer("tick.server.total.us",
          MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Total});
        TelemetryScope sTotal(tTotal);

        {
          static auto t = Telemetry::timer("tick.server.loophead.us",
            MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
          TelemetryScope s(t);
          fidelity = lockedFidelity.value(automaticFidelity);
          LogMap::set(strf("server_{}_fidelity", m_worldId), WorldServerFidelityNames.getRight(fidelity));
          LogMap::set(strf("server_{}_update", m_worldId), strf("{:4.2f}Hz", tickApproacher.rate()));
        }

        update(fidelity);

        {
          static auto t = Telemetry::timer("tick.server.approach.us",
            MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
          TelemetryScope s(t);
          tickApproacher.setTargetTickRate(1.0f / ServerGlobalTimestep);
          tickApproacher.tick();
        }

        {
          // WRAPS the `if`, rather than sitting inside it. Periodic disk sync fires on maybe one tick
          // in a thousand; a scope inside the branch would fire only on those ticks, and at
          // cadence=Tick the consumer reads the shortfall as sampling loss and scales it up by ~1000x.
          // Outside the branch it fires every tick and simply records ~0 when the timer has not
          // elapsed. This phase is also the ONLY reason the total wraps the run() loop body rather
          // than update(): no update()-scoped timer can ever see this disk I/O.
          static auto t = Telemetry::timer("tick.server.storage.us",
            MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
          TelemetryScope s(t);
          if (storageTimer.timeUp()) {
            sync();
            storageTimer.restart(storageInterval);
          }
        }

        {
          // The adaptive-fidelity governor. serverFidelity defaults to "automatic"
          // (StarRootLoader.cpp), so this is ALWAYS live unless the Director pins it -- and it is
          // negative feedback on precisely the quantity the total measures: a slower tick lowers
          // fidelity, which makes the next tick cheaper. Any A/B on owner `sim` must therefore either
          // pin serverFidelity or report the fidelity level alongside the delta, or it is measuring
          // the governor's response rather than the change under test.
          static auto t = Telemetry::timer("tick.server.fidelity.us",
            MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
          TelemetryScope s(t);
          spareTime = tickApproacher.spareTime();
          fidelityScore += spareTime;

          if (fidelityScore <= fidelityDecrementScore) {
            if (automaticFidelity > WorldServerFidelity::Minimum)
              automaticFidelity = (WorldServerFidelity)((int)automaticFidelity - 1);
            fidelityScore = 0.0;
          }

          if (fidelityScore >= fidelityIncrementScore) {
            if (automaticFidelity < WorldServerFidelity::High)
              automaticFidelity = (WorldServerFidelity)((int)automaticFidelity + 1);
            fidelityScore = 0.0;
          }
        }
      }

      int64_t spareMilliseconds = floor(spareTime * 1000);
      if (spareMilliseconds > 0)
        Thread::sleepPrecise(spareMilliseconds);
    }
  } catch (std::exception const& e) {
    Logger::error("WorldServerThread exception caught: {}", outputException(e, true));
    m_errorOccurred = true;
  }
}

// The BLOCKED-TIME metrics. Every lock acquisition inside owner `sim`'s total gets one, so blocked
// time is separable from work everywhere it occurs and `busy = total - blocked` is recoverable.
//
// role=Detail, NOT Budget, and the distinction is load-bearing: these are NESTED inside the Budget
// phases that contain them (the queue acquisitions inside publish/sync, the message acquisition
// inside messages, sync's re-acquisition inside storage). Declaring them Budget would make the
// consumer count the same microseconds twice and push closure past 100%. The one acquisition that is
// NOT nested -- m_mutex at the top of update() -- is a Budget phase in its own right, below.
//
// tick.server.lock.us IS A LATENCY METRIC, NOT A COST METRIC. WorldServerThread::readChunks and
// unloadAll hold m_mutex across disk work on another thread, so a single acquisition here can run for
// seconds. Its max and p99 say how long the world stopped ticking, not how much CPU anything used.
namespace {
  TelemetryTimer blockedQueueTimer() {
    static auto t = Telemetry::timer("tick.server.lock.queue.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Call, MetricRole::Detail});
    return t;
  }
}

void WorldServerThread::update(WorldServerFidelity fidelity) {
  // Deferred acquisition so the WAIT can be timed. This is the only one of the six that is not nested
  // inside another phase, so it is a Budget part and carries its share of the tick directly.
  RecursiveMutexLocker locker(m_mutex, false);
  {
    static auto t = Telemetry::timer("tick.server.lock.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    locker.lock();
  }

  // Hoisted out of the phase scopes that read them. `unerroredClientIds` is established in setup,
  // iterated in publish and sync, and MUTATED in publish's catch block, so it cannot be const.
  List<ConnectionId> unerroredClientIds;
  float dt = 0.0f;

  {
    static auto t = Telemetry::timer("tick.server.setup.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    unerroredClientIds = m_worldServer->clientIds();
  }

  {
    // Telemetry publish phase: drain + handle this tick's incoming client packets.
    static auto t = Telemetry::timer("tick.server.publish.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    for (auto clientId : unerroredClientIds) {
      RecursiveMutexLocker queueLocker(m_queueMutex, false);
      { TelemetryScope q(blockedQueueTimer()); queueLocker.lock(); }
      auto incomingPackets = take(m_incomingPacketQueue[clientId]);
      queueLocker.unlock();
      try {
        m_worldServer->handleIncomingPackets(clientId, std::move(incomingPackets));
      } catch (std::exception const& e) {
        Logger::error("WorldServerThread exception caught handling incoming packets for client {}: {}",
            clientId, outputException(e, true));
        RecursiveMutexLocker errorQueueLocker(m_queueMutex, false);
        { TelemetryScope q(blockedQueueTimer()); errorQueueLocker.lock(); }
        m_outgoingPacketQueue[clientId].appendAll(m_worldServer->removeClient(clientId));
        // PRE-EXISTING DEFECT, left alone deliberately: this mutates the very list the range-for above
        // is iterating. Instrumenting a bug is not fixing it, and fixing it here would bury a
        // behaviour change inside a byte-identical telemetry change. Filed separately.
        unerroredClientIds.remove(clientId);
      }
    }
  }

  {
    static auto t = Telemetry::timer("tick.server.prologue.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    dt = ServerGlobalTimestep * GlobalTimescale;
    m_worldServer->setFidelity(fidelity);
  }

  {
    // role=Detail, NOT Budget. This scope is the PARENT of the sixteen tick.server.compute.* parts
    // declared in StarWorldServer.cpp; counting both would double-count the dominant phase and put
    // closure far past 100%. Kept as a Detail so the compute aggregate is still readable in one row.
    static auto t = Telemetry::timer("tick.server.compute.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Detail});
    TelemetryScope s(t);
    // The gate has TWO arms and both are live: GlobalTimescale is settable to 0 from the console, and
    // m_pause is raised by the client's escape dialog. This is why the sixteen phases inside
    // WorldServer::update are cadence=Call and why their handles are declared at file scope there --
    // a block-scope static inside a function that is never entered never registers at all.
    if (dt > 0.0f && (!m_pause || *m_pause == false))
      m_worldServer->update(dt);
  }

  {
    static auto t = Telemetry::timer("tick.server.messages.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    List<Message> messages;
    {
      RecursiveMutexLocker messageLocker(m_messageMutex, false);
      {
        static auto tl = Telemetry::timer("tick.server.lock.message.us",
          MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Call, MetricRole::Detail});
        TelemetryScope q(tl);
        messageLocker.lock();
      }
      messages = std::move(m_messages);
    }
    for (auto& message : messages) {
      if (auto resp = m_worldServer->receiveMessage(ServerConnectionId, message.message, message.args))
        message.promise.fulfill(*resp);
      else
        message.promise.fail("Message not handled by world");
    }
  }

  {
    // Telemetry sync phase: collect + queue this tick's outgoing client packets.
    static auto t = Telemetry::timer("tick.server.sync.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    for (auto& clientId : unerroredClientIds) {
      auto outgoingPackets = m_worldServer->getOutgoingPackets(clientId);
      RecursiveMutexLocker queueLocker(m_queueMutex, false);
      { TelemetryScope q(blockedQueueTimer()); queueLocker.lock(); }
      m_outgoingPacketQueue[clientId].appendAll(std::move(outgoingPackets));
    }
  }

  {
    // The tail: expiry read plus the registered update action, which on a client-hosted server is how
    // packets actually reach the network. NOTE that start() precedes setUpdateAction() at all three
    // world-creation sites, so this records ~0 for the opening ticks of EVERY world -- a short capture
    // taken immediately after a warp under-reports dispatch for a real reason.
    static auto t = Telemetry::timer("tick.server.epilogue.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Tick, MetricRole::Budget});
    TelemetryScope s(t);
    m_shouldExpire = m_worldServer->shouldExpire();

    if (m_updateAction)
      m_updateAction(this, m_worldServer.get());
  }
}

void WorldServerThread::sync() {
  // The sixth lock. Called from run() with no lock held, inside tick.server.storage.us -- so without
  // this the storage phase would silently contain an unbounded wait.
  RecursiveMutexLocker locker(m_mutex, false);
  {
    static auto t = Telemetry::timer("tick.server.lock.sync.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Call, MetricRole::Detail});
    TelemetryScope s(t);
    locker.lock();
  }
  Logger::debug("WorldServer: periodic sync to disk of world {}", m_worldId);
  m_worldServer->sync();
}

}
