#include "StarWorldServer.hpp"
#include "StarNetElement.hpp"
#include "StarMovementController.hpp"
#include "StarPlant.hpp"
#include "StarLogging.hpp"
#include "StarIterator.hpp"
#include "StarDataStreamExtra.hpp"
#include "StarBiome.hpp"
#include "StarWireProcessor.hpp"
#include "StarWireEntity.hpp"
#include "StarWorldImpl.hpp"
#include "StarWorldGeneration.hpp"
#include "StarItemDescriptor.hpp"
#include "StarItemDrop.hpp"
#include "StarObjectDatabase.hpp"
#include "StarObject.hpp"
#include "StarItemDatabase.hpp"
#include "StarContainerEntity.hpp"
#include "StarItemBag.hpp"
#include "StarPhysicsEntity.hpp"
#include "StarProjectile.hpp"
#include "StarDamageManager.hpp"
#include "StarPlayer.hpp"
#include "StarEntityFactory.hpp"
#include "StarBiomeDatabase.hpp"
#include "StarLiquidTypes.hpp"
#include "StarFallingBlocksAgent.hpp"
#include "StarWarpTargetEntity.hpp"
#include "StarUniverseSettings.hpp"
#include "StarUniverseServerLuaBindings.hpp"
#include "StarTelemetry.hpp"

namespace Star {

EnumMap<WorldServerFidelity> const WorldServerFidelityNames{
  {WorldServerFidelity::Minimum, "minimum"},
  {WorldServerFidelity::Low, "low"},
  {WorldServerFidelity::Medium, "medium"},
  {WorldServerFidelity::High, "high"}
};

WorldServer::WorldServer(WorldTemplatePtr const& worldTemplate, IODevicePtr storage) {
  m_worldTemplate = worldTemplate;
  m_worldStorage = make_shared<WorldStorage>(m_worldTemplate->size(), storage, make_shared<WorldGenerator>(this));
  m_adjustPlayerStart = true;
  m_respawnInWorld = false;
  m_tileProtectionEnabled = true;
  m_universeSettings = make_shared<UniverseSettings>();
  m_worldId = worldTemplate->worldName();
  m_expiryTimer = GameTimer(0.0f);

  init(true);
  writeMetadata();
}

WorldServer::WorldServer(Vec2U const& size, IODevicePtr storage)
  : WorldServer(make_shared<WorldTemplate>(size), storage) {}

WorldServer::WorldServer(IODevicePtr const& storage) {
  m_worldStorage = make_shared<WorldStorage>(storage, make_shared<WorldGenerator>(this));
  m_tileProtectionEnabled = true;
  m_universeSettings = make_shared<UniverseSettings>();
  m_worldId = "Nowhere";

  readMetadata();
  init(false);
}

WorldServer::WorldServer(WorldChunks const& chunks) {
  m_worldStorage = make_shared<WorldStorage>(chunks, make_shared<WorldGenerator>(this));
  m_tileProtectionEnabled = true;
  m_universeSettings = make_shared<UniverseSettings>();
  m_worldId = "Nowhere";

  readMetadata();
  init(false);
}

WorldServer::~WorldServer() {
  for (auto& p : m_scriptContexts)
    p.second->uninit();

  m_scriptContexts.clear();
  m_spawner.uninit();
  // Option A dormancy: clear the awake-set / scheduled-wakes alongside the other
  // per-world transient state torn down here (they would auto-destruct with the
  // members regardless; kept explicit to mirror m_scriptContexts.clear()).
  m_awakeEntities.clear();
  m_scheduledWakes.clear();
  writeMetadata();
  m_worldStorage->unloadAll(true);
}

void WorldServer::setWorldId(String worldId) {
  m_worldId = std::move(worldId);
}

String const& WorldServer::worldId() const {
  return m_worldId;
}

void WorldServer::setUniverseSettings(UniverseSettingsPtr universeSettings) {
  m_universeSettings = std::move(universeSettings);
}

UniverseSettingsPtr WorldServer::universeSettings() const {
  return m_universeSettings;
}

void WorldServer::setReferenceClock(ClockPtr clock) {
  m_weather.setReferenceClock(clock);
  m_sky->setReferenceClock(clock);
  m_referenceClock = clock;
}

void WorldServer::setPause(bool pause) {
  if (m_referenceClock && pause)
    m_referenceClock->stop();
  else if (m_referenceClock)
    m_referenceClock->start();
}

void WorldServer::initLua(UniverseServer* universe) {
  m_luaRoot->addCallbacks("universe", LuaBindings::makeUniverseServerCallbacks(universe));
  auto assets = Root::singleton().assets();
  for (auto& p : assets->json("/worldserver.config:scriptContexts").toObject()) {
    auto scriptComponent = make_shared<ScriptComponent>();
    scriptComponent->setScripts(jsonToStringList(p.second.toArray()));

    m_scriptContexts.set(p.first, scriptComponent);
    scriptComponent->init(this);
  }
}

WorldStructure WorldServer::setCentralStructure(WorldStructure centralStructure) {
  removeCentralStructure();

  m_centralStructure = std::move(centralStructure);
  m_centralStructure.setAnchorPosition(Vec2I(m_geometry.size()) / 2);

  m_playerStart = Vec2F(m_centralStructure.flaggedBlocks("playerSpawn").first()) + Vec2F(0, 1);
  m_adjustPlayerStart = false;

  auto materialDatabase = Root::singleton().materialDatabase();
  for (auto const& foregroundBlock : m_centralStructure.foregroundBlocks()) {
    generateRegion(RectI::withSize(foregroundBlock.position, {1, 1}));
    if (auto tile = m_tileArray->modifyTile(foregroundBlock.position)) {
      if (tile->foreground == EmptyMaterialId) {
        tile->foreground = foregroundBlock.materialId;
        tile->foregroundColorVariant = foregroundBlock.materialColor;
        tile->foregroundHueShift = foregroundBlock.materialHue;
        tile->foregroundMod = foregroundBlock.materialMod;
        tile->updateCollision(materialDatabase->materialCollisionKind(foregroundBlock.materialId));
        queueTileUpdates(foregroundBlock.position);
        dirtyCollision(RectI::withSize(foregroundBlock.position, {1, 1}));
      }
    }
  }

  for (auto const& backgroundBlock : m_centralStructure.backgroundBlocks()) {
    generateRegion(RectI::withSize(backgroundBlock.position, {1, 1}));
    if (auto tile = m_tileArray->modifyTile(backgroundBlock.position)) {
      if (tile->background == EmptyMaterialId) {
        tile->background = backgroundBlock.materialId;
        tile->backgroundColorVariant = backgroundBlock.materialColor;
        tile->backgroundHueShift = backgroundBlock.materialHue;
        tile->backgroundMod = backgroundBlock.materialMod;
        queueTileUpdates(backgroundBlock.position);
      }
    }
  }

  auto objectDatabase = Root::singleton().objectDatabase();
  for (auto structureObject : m_centralStructure.objects()) {
    generateRegion(RectI::withSize(structureObject.position, {1, 1}));
    if (auto object = objectDatabase->createForPlacement(this, structureObject.name, structureObject.position, structureObject.direction, structureObject.parameters))
      addEntity(object);
  }

  for (auto const& pair : m_clientInfo)
    pair.second->outgoingPackets.append(make_shared<CentralStructureUpdatePacket>(m_centralStructure.store()));

  return m_centralStructure;
}

WorldStructure const& WorldServer::centralStructure() const {
  return m_centralStructure;
}

void WorldServer::removeCentralStructure() {
  for (auto const& structureObject : m_centralStructure.objects()) {
    if (!structureObject.residual) {
      generateRegion(RectI::withSize(structureObject.position, {1, 1}));
      for (auto const& objectEntity : atTile<Object>(structureObject.position)) {
        if (objectEntity->tilePosition() == structureObject.position && objectEntity->name() == structureObject.name)
          removeEntity(objectEntity->entityId(), false);
      }
    }
  }

  for (auto const& backgroundBlock : m_centralStructure.backgroundBlocks()) {
    if (!backgroundBlock.residual) {
      generateRegion(RectI::withSize(backgroundBlock.position, {1, 1}));
      if (auto tile = m_tileArray->modifyTile(backgroundBlock.position)) {
        if (tile->background == backgroundBlock.materialId) {
          tile->background = EmptyMaterialId;
          tile->backgroundMod = NoModId;
          queueTileUpdates(backgroundBlock.position);
        }
      }
    }
  }

  for (auto const& foregroundBlock : m_centralStructure.foregroundBlocks()) {
    if (!foregroundBlock.residual) {
      generateRegion(RectI::withSize(foregroundBlock.position, {1, 1}));
      if (auto tile = m_tileArray->modifyTile(foregroundBlock.position)) {
        if (tile->foreground == foregroundBlock.materialId) {
          tile->foreground = EmptyMaterialId;
          tile->foregroundMod = NoModId;
          tile->updateCollision(CollisionKind::None);
          dirtyCollision(RectI::withSize(foregroundBlock.position, {1, 1}));
          queueTileUpdates(foregroundBlock.position);
        }
      }
    }
  }
}

bool WorldServer::spawnTargetValid(SpawnTarget const& spawnTarget) const {
  if (spawnTarget.is<SpawnTargetUniqueEntity>())
    return (bool)m_entityMap->get<WarpTargetEntity>(m_worldStorage->loadUniqueEntity(spawnTarget.get<SpawnTargetUniqueEntity>()));
  return true;
}

bool WorldServer::addClient(ConnectionId clientId, SpawnTarget const& spawnTarget, bool isLocal, bool isAdmin, NetCompatibilityRules netRules) {
  if (m_clientInfo.contains(clientId))
    return false;

  Vec2F playerStart;
  if (spawnTarget.is<SpawnTargetPosition>()) {
    playerStart = spawnTarget.get<SpawnTargetPosition>();
  } else if (spawnTarget.is<SpawnTargetX>()) {
    auto targetX = spawnTarget.get<SpawnTargetX>();
    playerStart = findPlayerSpaceStart(targetX);
  } else if (spawnTarget.is<SpawnTargetUniqueEntity>()) {
    if (auto target = m_entityMap->get<WarpTargetEntity>(m_worldStorage->loadUniqueEntity(spawnTarget.get<SpawnTargetUniqueEntity>())))
      playerStart = target->position() + target->footPosition();
    else
      return false;
  } else {
    playerStart = m_playerStart;
    if (m_adjustPlayerStart) {
      m_playerStart = findPlayerStart(m_playerStart);
      playerStart = m_playerStart;
    }
  }
  RectF spawnRegion = RectF(playerStart, playerStart).padded(m_serverConfig.getInt("playerStartInitialGenRadius"));
  generateRegion(RectI::integral(spawnRegion));
  m_spawner.activateEmptyRegion(spawnRegion);

  InterpolationTracker tracker;
  if (isLocal)
    tracker = InterpolationTracker(m_serverConfig.query("interpolationSettings.local"));
  else
    tracker = InterpolationTracker(m_serverConfig.query("interpolationSettings.normal"));

  tracker.update(m_currentTime);

  auto& clientInfo = m_clientInfo.add(clientId, make_shared<ClientInfo>(clientId, tracker));
  clientInfo->local = isLocal;
  clientInfo->admin = isAdmin;
  clientInfo->clientState.setNetCompatibilityRules(netRules);

  auto worldStartPacket = make_shared<WorldStartPacket>();
  auto& templateData = worldStartPacket->templateData = m_worldTemplate->store();
  // this makes it possible to use custom InstanceWorlds without clients having the mod that adds their dungeon:
  if (templateData.optQueryString("worldParameters.primaryDungeon")
    && Root::singletonPtr()->configuration()->getPath("compatibility.customDungeonWorld").optBool().value(false))
    worldStartPacket->templateData = worldStartPacket->templateData.setPath("worldParameters.primaryDungeon", "testarena");

  tie(worldStartPacket->skyData, clientInfo->skyNetVersion) = m_sky->writeUpdate(0, netRules);
  tie(worldStartPacket->weatherData, clientInfo->weatherNetVersion) = m_weather.writeUpdate(0, netRules);
  worldStartPacket->playerStart = playerStart;
  worldStartPacket->playerRespawn = m_playerStart;
  worldStartPacket->respawnInWorld = m_respawnInWorld;
  worldStartPacket->worldProperties = m_worldProperties;
  worldStartPacket->dungeonIdGravity = m_dungeonIdGravity;
  worldStartPacket->dungeonIdBreathable = m_dungeonIdBreathable;
  worldStartPacket->protectedDungeonIds = m_protectedDungeonIds;
  worldStartPacket->clientId = clientId;
  worldStartPacket->localInterpolationMode = isLocal;
  clientInfo->outgoingPackets.append(worldStartPacket);

  clientInfo->outgoingPackets.append(make_shared<CentralStructureUpdatePacket>(m_centralStructure.store()));

  for (auto& p : m_scriptContexts)
    p.second->invoke("addClient", clientId, isLocal);

  return true;
}

List<PacketPtr> WorldServer::removeClient(ConnectionId clientId) {
  auto const& info = m_clientInfo.get(clientId);

  for (auto const& entityId : m_entityMap->entityIds()) {
    if (connectionForEntity(entityId) == clientId)
      removeEntity(entityId, false);
  }

  for (auto const& uuid : m_entityMessageResponses.keys()) {
    if (m_entityMessageResponses[uuid].first == clientId) {
      auto response = m_entityMessageResponses[uuid].second;
      if (response.is<ConnectionId>()) {
        if (auto clientInfo = m_clientInfo.value(response.get<ConnectionId>()))
          clientInfo->outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeLeft("Client disconnected"), uuid));
      } else {
        response.get<RpcPromiseKeeper<Json>>().fail("Client disconnected");
      }
      m_entityMessageResponses.remove(uuid);
    }
  }

  auto packets = std::move(info->outgoingPackets);
  m_clientInfo.remove(clientId);

  packets.append(make_shared<WorldStopPacket>("Removed"));

  for (auto& p : m_scriptContexts)
    p.second->invoke("removeClient", clientId);

  return packets;
}

List<ConnectionId> WorldServer::clientIds() const {
  return m_clientInfo.keys();
}

bool WorldServer::hasClient(ConnectionId clientId) const {
  return m_clientInfo.contains(clientId);
}

RectF WorldServer::clientWindow(ConnectionId clientId) const {
  auto i = m_clientInfo.find(clientId);
  if (i != m_clientInfo.end())
    return RectF(i->second->clientState.window());
  else
    return RectF::null();
}

PlayerPtr WorldServer::clientPlayer(ConnectionId clientId) const {
  auto i = m_clientInfo.find(clientId);
  if (i != m_clientInfo.end())
    return get<Player>(i->second->clientState.playerId());
  else
    return {};
}

List<EntityId> WorldServer::players() const {
  List<EntityId> playerIds;
  for (auto pair : m_clientInfo)
    playerIds.append(pair.second->clientState.playerId());
  return playerIds;
}

void WorldServer::handleIncomingPackets(ConnectionId clientId, List<PacketPtr> const& packets) {
  shared_ptr<ClientInfo> clientInfo = m_clientInfo.get(clientId);
  auto& root = Root::singleton();
  auto entityFactory = root.entityFactory();
  auto itemDatabase = root.itemDatabase();

  for (auto const& packet : packets) {
    if (auto worldStartAcknowledge = as<WorldStartAcknowledgePacket>(packet)) {
      clientInfo->started = true;

    } else if (!clientInfo->started) {
      // clients which have not sent a world start acknowledge are not sending packets intended for this world
      continue;

    } else if (auto heartbeat = as<StepUpdatePacket>(packet)) {
      clientInfo->interpolationTracker.receiveTimeUpdate(heartbeat->remoteTime);

    } else if (auto wcsPacket = as<WorldClientStateUpdatePacket>(packet)) {
      clientInfo->clientState.readDelta(wcsPacket->worldClientStateDelta);

      // Need to send all sectors that are now in the client window but were not
      // in the old
      HashSet<ServerTileSectorArray::Sector> oldSectors = take(clientInfo->activeSectors);

      for (auto const& monitoredRegion : clientInfo->monitoringRegions(m_entityMap))
        clientInfo->activeSectors.addAll(m_tileArray->validSectorsFor(monitoredRegion));

      clientInfo->pendingSectors.addAll(clientInfo->activeSectors.difference(oldSectors));

    } else if (auto mtpacket = as<ModifyTileListPacket>(packet)) {
      auto unappliedModifications = applyTileModifications(mtpacket->modifications, mtpacket->allowEntityOverlap);
      if (!unappliedModifications.empty())
        clientInfo->outgoingPackets.append(make_shared<TileModificationFailurePacket>(unappliedModifications));

    } else if (auto rtpacket = as<ReplaceTileListPacket>(packet)) {
      auto unappliedModifications = replaceTiles(rtpacket->modifications, rtpacket->tileDamage, rtpacket->applyDamage);
      if (!unappliedModifications.empty())
        clientInfo->outgoingPackets.append(make_shared<TileModificationFailurePacket>(unappliedModifications));

    } else if (auto dtgpacket = as<DamageTileGroupPacket>(packet)) {
      damageTiles(dtgpacket->tilePositions, dtgpacket->layer, dtgpacket->sourcePosition, dtgpacket->tileDamage, dtgpacket->sourceEntity);

    } else if (auto clpacket = as<CollectLiquidPacket>(packet)) {
      if (auto item = collectLiquid(clpacket->tilePositions, clpacket->liquidId))
        clientInfo->outgoingPackets.append(make_shared<GiveItemPacket>(item));

    } else if (auto sepacket = as<SpawnEntityPacket>(packet)) {
      auto netRules = clientInfo->clientState.netCompatibilityRules();
      auto entity = entityFactory->netLoadEntity(sepacket->entityType, std::move(sepacket->storeData), netRules);
      entity->readNetState(std::move(sepacket->firstNetState), 0.0f, netRules);
      addEntity(std::move(entity));

    } else if (auto rdpacket = as<RequestDropPacket>(packet)) {
      auto drop = m_entityMap->get<ItemDrop>(rdpacket->dropEntityId);
      if (drop && drop->isMaster() && drop->canTake()) {
        if (auto taken = drop->takeBy(clientInfo->clientState.playerId()))
          clientInfo->outgoingPackets.append(make_shared<GiveItemPacket>(taken->descriptor()));
      }

    } else if (auto hit = as<HitRequestPacket>(packet)) {
      if (hit->remoteHitRequest.destinationConnection() == ServerConnectionId)
        m_damageManager->pushRemoteHitRequest(hit->remoteHitRequest);
      else
        m_clientInfo.get(hit->remoteHitRequest.destinationConnection())->outgoingPackets.append(make_shared<HitRequestPacket>(hit->remoteHitRequest));

    } else if (auto damage = as<DamageRequestPacket>(packet)) {
      if (damage->remoteDamageRequest.destinationConnection() == ServerConnectionId)
        m_damageManager->pushRemoteDamageRequest(damage->remoteDamageRequest);
      else
        m_clientInfo.get(damage->remoteDamageRequest.destinationConnection())->outgoingPackets.append(make_shared<DamageRequestPacket>(damage->remoteDamageRequest));

    } else if (auto damage = as<DamageNotificationPacket>(packet)) {
      m_damageManager->pushRemoteDamageNotification(damage->remoteDamageNotification);
      for (auto const& pair : m_clientInfo) {
        if (pair.first != clientId && pair.second->needsDamageNotification(damage->remoteDamageNotification))
          pair.second->outgoingPackets.append(make_shared<DamageNotificationPacket>(damage->remoteDamageNotification));
      }

    } else if (auto entityInteract = as<EntityInteractPacket>(packet)) {
      auto targetEntityConnection = connectionForEntity(entityInteract->interactRequest.targetId);
      if (targetEntityConnection == ServerConnectionId) {
        auto interactResult = interact(entityInteract->interactRequest).result();
        clientInfo->outgoingPackets.append(make_shared<EntityInteractResultPacket>(interactResult.take(), entityInteract->requestId, entityInteract->interactRequest.sourceId));
      } else {
        auto const& forwardClientInfo = m_clientInfo.get(targetEntityConnection);
        forwardClientInfo->outgoingPackets.append(entityInteract);
      }

    } else if (auto interactResult = as<EntityInteractResultPacket>(packet)) {
      auto const& forwardClientInfo = m_clientInfo.get(connectionForEntity(interactResult->sourceEntityId));
      forwardClientInfo->outgoingPackets.append(interactResult);

    } else if (auto entityCreate = as<EntityCreatePacket>(packet)) {
      if (!entityIdInSpace(entityCreate->entityId, clientInfo->clientId)) {
        throw WorldServerException::format("WorldServer received entity create packet with illegal entity id {}.", entityCreate->entityId);
      } else {
        if (m_entityMap->entity(entityCreate->entityId)) {
          Logger::error("WorldServer received duplicate entity create packet from client, deleting old entity {}", entityCreate->entityId);
          removeEntity(entityCreate->entityId, false);
        }
        auto netRules = clientInfo->clientState.netCompatibilityRules();
        auto entity = entityFactory->netLoadEntity(entityCreate->entityType, entityCreate->storeData, netRules);
        entity->readNetState(entityCreate->firstNetState, 0.0f, netRules);
        entity->init(this, entityCreate->entityId, EntityMode::Slave);
        m_entityMap->addEntity(entity);

        if (clientInfo->interpolationTracker.interpolationEnabled())
          entity->enableInterpolation(clientInfo->interpolationTracker.extrapolationHint());
      }

    } else if (auto entityUpdateSet = as<EntityUpdateSetPacket>(packet)) {
      float interpolationLeadTime = clientInfo->interpolationTracker.interpolationLeadTime();
      m_entityMap->forAllEntities([&](EntityPtr const& entity) {
          EntityId entityId = entity->entityId();
          if (connectionForEntity(entityId) == clientId) {
            starAssert(entity->isSlave());
            entity->readNetState(entityUpdateSet->deltas.value(entityId), interpolationLeadTime, clientInfo->clientState.netCompatibilityRules());
          }
        });
      clientInfo->pendingForward = true;

    } else if (auto entityDestroy = as<EntityDestroyPacket>(packet)) {
      if (auto entity = m_entityMap->entity(entityDestroy->entityId)) {
        entity->readNetState(entityDestroy->finalNetState, clientInfo->interpolationTracker.interpolationLeadTime(), clientInfo->clientState.netCompatibilityRules());
        // Before destroying the entity, we should make sure that the entity is
        // using the absolute latest data, so we disable interpolation.
        entity->disableInterpolation();
        removeEntity(entityDestroy->entityId, entityDestroy->death);
      }

    } else if (auto disconnectWires = as<DisconnectAllWiresPacket>(packet)) {
      for (auto wireEntity : atTile<WireEntity>(disconnectWires->entityPosition)) {
        for (auto connection : wireEntity->connectionsForNode(disconnectWires->wireNode)) {
          wireEntity->removeNodeConnection(disconnectWires->wireNode, connection);
          for (auto connectedEntity : atTile<WireEntity>(connection.entityLocation))
            connectedEntity->removeNodeConnection({otherWireDirection(disconnectWires->wireNode.direction), connection.nodeIndex}, WireConnection{disconnectWires->entityPosition, disconnectWires->wireNode.nodeIndex});
        }
      }

    } else if (auto connectWire = as<ConnectWirePacket>(packet)) {
      for (auto source : atTile<WireEntity>(connectWire->inputConnection.entityLocation)) {
        for (auto target : atTile<WireEntity>(connectWire->outputConnection.entityLocation)) {
          source->addNodeConnection(WireNode{WireDirection::Input, connectWire->inputConnection.nodeIndex}, connectWire->outputConnection);
          target->addNodeConnection(WireNode{WireDirection::Output, connectWire->outputConnection.nodeIndex}, connectWire->inputConnection);
        }
      }

    } else if (auto findUniqueEntity = as<FindUniqueEntityPacket>(packet)) {
      clientInfo->outgoingPackets.append(make_shared<FindUniqueEntityResponsePacket>(findUniqueEntity->uniqueEntityId,
          m_worldStorage->findUniqueEntity(findUniqueEntity->uniqueEntityId)));

    } else if (auto entityMessagePacket = as<EntityMessagePacket>(packet)) {
      EntityPtr entity;
      if (entityMessagePacket->entityId.is<EntityId>())
        entity = m_entityMap->entity(entityMessagePacket->entityId.get<EntityId>());
      else
        entity = m_entityMap->entity(loadUniqueEntity(entityMessagePacket->entityId.get<String>()));

      if (!entity) {
        clientInfo->outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeLeft("Unknown entity"), entityMessagePacket->uuid));
      } else {
        if (entity->isMaster()) {
          auto response = entity->receiveMessage(clientId, entityMessagePacket->message, entityMessagePacket->args);
          if (response)
            clientInfo->outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeRight(response.take()), entityMessagePacket->uuid));
          else
            clientInfo->outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeLeft("Message not handled by entity"), entityMessagePacket->uuid));
        } else if (auto const& clientInfo = m_clientInfo.value(connectionForEntity(entity->entityId()))) {
          m_entityMessageResponses[entityMessagePacket->uuid] = {clientInfo->clientId, clientId};
          entityMessagePacket->fromConnection = clientId;
          clientInfo->outgoingPackets.append(std::move(entityMessagePacket));
        }
      }

    } else if (auto entityMessageResponsePacket = as<EntityMessageResponsePacket>(packet)) {
      if (!m_entityMessageResponses.contains(entityMessageResponsePacket->uuid))
        Logger::warn("EntityMessageResponse received for unknown context [{}]!", entityMessageResponsePacket->uuid.hex());
      else {
        auto response = m_entityMessageResponses.take(entityMessageResponsePacket->uuid).second;
        if (response.is<ConnectionId>()) {
          if (auto clientInfo = m_clientInfo.value(response.get<ConnectionId>()))
            clientInfo->outgoingPackets.append(std::move(entityMessageResponsePacket));
        } else {
          if (entityMessageResponsePacket->response.isRight())
            response.get<RpcPromiseKeeper<Json>>().fulfill(entityMessageResponsePacket->response.right());
          else
            response.get<RpcPromiseKeeper<Json>>().fail(entityMessageResponsePacket->response.left());
        }
      }
    } else if (auto pingPacket = as<PingPacket>(packet)) {
      clientInfo->outgoingPackets.append(make_shared<PongPacket>(pingPacket->time));

    } else if (auto updateWorldProperties = as<UpdateWorldPropertiesPacket>(packet)) {
      // Kae: Properties set to null (nil from Lua) should be erased instead of lingering around
      for (auto& pair : updateWorldProperties->updatedProperties) {
        if (pair.second.isNull())
          m_worldProperties.erase(pair.first);
        else
          m_worldProperties[pair.first] = pair.second;
      }
      for (auto const& pair : m_clientInfo)
        pair.second->outgoingPackets.append(make_shared<UpdateWorldPropertiesPacket>(updateWorldProperties->updatedProperties));

    } else if (auto updateWorldTemplate = as<UpdateWorldTemplatePacket>(packet)) {
      if (!clientInfo->admin)
        continue; // nuh-uh!

      auto newWorldTemplate = make_shared<WorldTemplate>(updateWorldTemplate->templateData);
      setTemplate(newWorldTemplate);
      // setTemplate re-adds all clients currently, update clientInfo
      clientInfo = m_clientInfo.get(clientId);
    } else {
      Logger::warn("UniverseServer: Dropping unexpected {} packet from client",
          PacketTypeNames.getRight(packet->type()));
    }
  }
}

List<PacketPtr> WorldServer::getOutgoingPackets(ConnectionId clientId) {
  auto const& clientInfo = m_clientInfo.get(clientId);
  return std::move(clientInfo->outgoingPackets);
}

bool WorldServer::sendPacket(ConnectionId clientId, PacketPtr const& packet) {
  if (auto clientInfo = m_clientInfo.ptr(clientId)) {
    clientInfo->get()->outgoingPackets.append(packet);
    return true;
  }
  return false;
}

Maybe<Json> WorldServer::receiveMessage(ConnectionId fromConnection, String const& message, JsonArray const& args) {
  Maybe<Json> result;
  for (auto& p : m_scriptContexts) {
    result = p.second->handleMessage(message, fromConnection == ServerConnectionId, args);
    if (result)
      break;
  }
  return result;
}

WorldServerFidelity WorldServer::fidelity() const {
  return m_fidelity;
}

void WorldServer::setFidelity(WorldServerFidelity fidelity) {
  m_fidelity = fidelity;
  m_fidelityConfig = m_serverConfig.get("fidelitySettings").get(WorldServerFidelityNames.getRight(m_fidelity));
}

bool WorldServer::shouldExpire() {
  if (!m_clientInfo.empty()) {
    m_expiryTimer.reset();
    return false;
  }

  return m_expiryTimer.ready();
}

void WorldServer::setExpiryTime(float expiryTime) {
  m_expiryTimer = GameTimer(expiryTime);
}

float WorldServer::expiryTime() {
  return m_expiryTimer.timer;
}

namespace {
  // OWNER `sim`'s COMPUTE PARTS -- the sixteen Budget phases that partition WorldServer::update.
  //
  // DECLARED AT FILE SCOPE, NOT AS BLOCK STATICS, and that is not a style preference. This function is
  // entered only when `dt > 0.0f && !paused` (StarWorldServerThread.cpp). A block-scope static inside
  // it does not merely go un-recorded on a paused tick -- it never REGISTERS AT ALL, so a capture
  // taken across a pause would find these metrics ABSENT from the snapshot rather than present with a
  // zero, and absent reads as "this phase does not exist" rather than "this phase did no work". File
  // scope registers them at static-init unconditionally. Safe to do at static-init: Telemetry's
  // registry is a function-local static, constructed on first use.
  //
  // cadence=Call, NOT Tick. They genuinely do not fire on a paused tick, and under a Tick declaration
  // the consumer reads that shortfall as SAMPLING loss and scales every one of them up -- at which
  // point the parts exceed the whole. The price is the count==denominator oracle, which cannot check
  // Call-cadence metrics; the closure percentage is what guards these instead.
  TelemetryTimer simComputePart(char const* key) {
    return Telemetry::timer(key,
      MetricDesc{MetricDomain::Cpu, MetricOwner::Sim, MetricCadence::Call, MetricRole::Budget});
  }
  static TelemetryTimer const tcPrologue       = simComputePart("tick.server.compute.prologue.us");
  static TelemetryTimer const tcWake           = simComputePart("tick.server.compute.wake.us");
  static TelemetryTimer const tcEntities       = simComputePart("tick.server.compute.entities.us");
  static TelemetryTimer const tcScripts        = simComputePart("tick.server.compute.scripts.us");
  static TelemetryTimer const tcDamage         = simComputePart("tick.server.compute.damage.us");
  static TelemetryTimer const tcWiring         = simComputePart("tick.server.compute.wiring.us");
  static TelemetryTimer const tcSky            = simComputePart("tick.server.compute.sky.us");
  static TelemetryTimer const tcClientRegions  = simComputePart("tick.server.compute.clientregions.us");
  static TelemetryTimer const tcWeather        = simComputePart("tick.server.compute.weather.us");
  static TelemetryTimer const tcLiquid         = simComputePart("tick.server.compute.liquid.us");
  static TelemetryTimer const tcFallingBlocks  = simComputePart("tick.server.compute.fallingblocks.us");
  static TelemetryTimer const tcBlockDamage    = simComputePart("tick.server.compute.blockdamage.us");
  static TelemetryTimer const tcStorage        = simComputePart("tick.server.compute.storage.us");
  static TelemetryTimer const tcCommit         = simComputePart("tick.server.commit.us");
  static TelemetryTimer const tcNetSync        = simComputePart("tick.server.compute.netsync.us");
  static TelemetryTimer const tcEpilogue       = simComputePart("tick.server.compute.epilogue.us");

  // THE SIM SCENE FINGERPRINT -- owner `sim`'s answer to `lighting.lights.sources`.
  //
  // An A/B on a sim phase is only valid if both arms saw the SAME WORLD, and until now nothing in the
  // sim budget tracked that. It bit immediately: the first attempt at the L-WIND-A measurement (#84)
  // produced a clean-looking +5.7% on tick.server.compute.entities.us that dissolved on inspection --
  // scene load varied 26% between runs and correlated +0.83 with the very phase under test, and the
  // single heaviest run happened to land in the lever-OFF arm. The apparent win was the scene, not the
  // lever.
  //
  // Same lesson as `calc.cells` vs `lights.sources` on the lighting side, and the same rule: PIN THE
  // LOCATION, THEN ASSERT THE FINGERPRINT MATCHES ACROSS THE PAIR BEFORE BELIEVING ANY DELTA. A phase
  // that moved when nothing touched it is the tell that the comparison changed, not the code.
  //
  // Published from the epilogue, which is where the count is already read for the LogMap line -- the
  // act that establishes it. It is inside the pause-gated function, so on a fully paused capture it
  // holds its last value; that is honest here in a way it was not for `lighting.cells`, because a
  // world that is not ticking is not gaining or losing entities either.
  static TelemetryGauge gEntitiesLive = Telemetry::gauge("sim.entities.live",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Sim,
               .cadence = MetricCadence::Tick, .role = MetricRole::Detail,
               .boundedness = MetricBoundedness::Level});
}

void WorldServer::update(float dt) {
  // Hoisted out of the phase scopes below purely so those scopes can exist. `dormancyActive` and
  // `dormancyEnabled` lose their `const` in the process -- the alternative was to leave their
  // initialisation outside every phase, which would open a contiguity gap, and an exhaustive
  // partition is worth more here than two const qualifiers.
  bool doBreakChecks = false;
  List<EntityId> toRemove;
  bool dormancyActive = false;
  bool dormancyEnabled = false;
  List<RectI> clientWindows;
  List<RectI> clientMonitoringRegions;

  {
    TelemetryScope s(tcPrologue);
    m_currentTime += dt;
    ++m_currentStep;
    for (auto const& pair : m_clientInfo)
      pair.second->interpolationTracker.update(m_currentTime);

    List<WorldAction> triggeredActions;
    eraseWhere(m_timers, [&triggeredActions, dt](pair<float, WorldAction>& timer) {
        if ((timer.first -= dt) <= 0) {
          triggeredActions.append(timer.second);
          return true;
        }
        return false;
      });
    for (auto const& action : triggeredActions)
      action(this);

    m_spawner.update(dt);
  }

  {
    TelemetryScope s(tcWake);
    doBreakChecks = m_tileEntityBreakCheckTimer.wrapTick(m_currentTime) && m_needsGlobalBreakCheck;
    if (doBreakChecks)
      m_needsGlobalBreakCheck = false;

    // Entity-dormancy (option A "skip update() only"): read the gates ONCE per tick,
    // hoisted out of the per-entity callback (M3). dormancyActive (== enabled ||
    // validate) drives the OFF/ON fork and all membership/scheduling bookkeeping;
    // dormancyEnabled gates the actual update() skip. Splitting them lets validate
    // mode (active but !enabled) run EVERY entity's update() while still tracking
    // exactly the membership enabled mode would have acted on.
    dormancyActive = EntityDormancy::active();
    dormancyEnabled = EntityDormancy::enabled.load(std::memory_order_relaxed);

    // Promote scheduled wakes due this step into the awake-set. maybeTake (NOT take —
    // take throws on a missing key, and most ticks have no bucket for the current step).
    if (dormancyActive) {
      if (auto due = m_scheduledWakes.maybeTake(m_currentStep))
        for (EntityId id : *due)
          if (m_entityMap->entity(id))        // <-- skip stale ids from removed entities
            m_awakeEntities.add(id);
    }
  }

  // OPTION A INVARIANTS (see also queueUpdatePackets / m_wireProcessor->process):
  //  (a) net-sync is DECOUPLED from update(): queueUpdatePackets iterates
  //      monitoredEntities independently, so dormant entities are still
  //      created/synced to clients regardless of the awake-set.
  //  (b) the wire processor (m_wireProcessor->process(), below) is a SEPARATE
  //      pass that keeps iterating ALL entities — intentionally NOT touched here
  //      so wired devices keep evaluating even while their update() is skipped.
  //  (c) the tile-entity break-check + shouldDestroy() reaping below run for
  //      EVERY entity, dormant or not — this is what preserves the reaping /
  //      break-check coupling for free, and why only update() is gated.
  // The dominant phase: expected to carry the large majority of the tick, and the reason `compute`
  // reading 98.4% was an attribution failure rather than an attribution.
  {
  TelemetryScope sEntities(tcEntities);
  m_entityMap->updateAllEntities([&](EntityPtr const& entity) {
      if (!dormancyActive) {
        entity->update(dt, m_currentStep); // OFF: byte-identical to the pre-dormancy path
      } else {
        EntityId id = entity->entityId();
        // Consume the wake flag on EVERY visit (never short-circuited behind
        // contains()), otherwise a set request would go stale and never fire.
        bool requested = entity->takeWakeRequested();
        bool awake = m_awakeEntities.contains(id);  // single membership lookup (M4)
        bool run = requested || awake;

        // Telemetry (Task 6): tally would-be-dormant (!run) vs would-run updates.
        // Counted identically in enabled AND validate mode, so the dormancy ratio
        // logged below measures the SAME set in both -> a clean A/B (Task 8).
        if (run)
          EntityDormancy::ran.fetch_add(1, std::memory_order_relaxed);
        else
          EntityDormancy::skipped.fetch_add(1, std::memory_order_relaxed);

        // Validate/shadow mode (Task 6), the primary safety net. When validating
        // (active but !enabled) update() runs for EVERY entity, so for a would-be-
        // dormant one (!run) we capture its net-version aggregate BEFORE the still-
        // run update() and assert below it did NOT advance after -> the slate that
        // enabled mode WOULD have skipped was a genuine no-op. {} = the type exposes
        // no aggregate (not validatable) -> shadow check skipped for it. See
        // Entity::netVersionLatestChange for the (bounded) net-only-oracle limit.
        Maybe<uint64_t> validateBefore;
        if (!dormancyEnabled && !run)
          validateBefore = entity->netVersionLatestChange();

        // validate-only (dormancyActive && !dormancyEnabled) NEVER skips: it runs
        // every entity's update() so the shadow assert later sees the real ticks.
        if (run || !dormancyEnabled)
          entity->update(dt, m_currentStep);

        // Shadow assertion: a skipped-in-enabled-mode slate that mutated net state
        // here is a missing-wake bug (the dormancy analogue of #4's MISMATCH).
        if (validateBefore) {
          // Flush deferred NetElement stores BEFORE the after-capture so their
          // markChanged() (which the real pump only fires later in
          // queueUpdatePackets, the client phase) is visible to the oracle NOW.
          // Without this, deferred-store net state written only at pump time via
          // setNetStates() — ContainerObject::m_itemsNetState (item contents),
          // Object::m_orientationIndexNetState — would mutate AFTER this capture
          // and before next tick's before-capture, so before == after every tick
          // and the missed-wake bug would never be flagged. Eager NetElements
          // (markChanged in set()) are already caught without this. Validate-mode
          // only: this branch is the !dormancyEnabled && !run path, so the OFF
          // path and the enabled path never reach here. The extra pump is
          // idempotent (re-stores the current values) and the real pump still
          // runs in queueUpdatePackets — an acceptable cost for a debug mode.
          entity->netStorePump();
          if (auto after = entity->netVersionLatestChange()) {
            if (*after != *validateBefore) {
              Logger::warn("DORMANCY MISMATCH type={} id={} latestChange {}->{}",
                  EntityTypeNames.getRight(entity->entityType()), id, *validateBefore, *after);
              EntityDormancy::mismatches.fetch_add(1, std::memory_order_relaxed);
            }
          }
        }

        // Membership/scheduling are gated on `run`, NOT on whether update() ran:
        // a would-be-dormant entity in validate mode has run==false but still
        // updates, and its membership must stay put so validate tracks exactly the
        // set enabled mode would have acted on. Mutate only on transition (M4).
        if (run) {
          auto horizon = entity->nextEngineWakeStep(m_currentStep);
          // Staggered max-sleep cap (Task 7 backstop): bound any indefinite ({}) or
          // over-cap sleep so a missed wake degrades to <= maxSleep latency, never a
          // permanent freeze. Stagger within a small window just below the cap (keyed
          // on entityId) so cap-wakes don't all land on one tick. cappedWake is always
          // > m_currentStep + 1 for any sane cap, so a capped entity genuinely sleeps.
          uint64_t const cap = m_dormancyMaxSleepSteps;
          uint64_t const staggerWindow = min<uint64_t>(cap, 64);
          uint64_t const cappedWake = m_currentStep + cap - ((uint64_t)id % staggerWindow); // in [cur+cap-(window-1), cur+cap]
          if (!horizon || *horizon > cappedWake) {
            // No self-scheduled work, or a horizon past the cap -> wake at the staggered cap.
            if (awake) m_awakeEntities.remove(id);
            m_scheduledWakes[cappedWake].append(id);
          } else if (*horizon > m_currentStep + 1) {
            // Sleep until the horizon step.
            if (awake) m_awakeEntities.remove(id);
            m_scheduledWakes[*horizon].append(id);
          } else {
            // horizon <= m_currentStep + 1 -> stay awake next tick (a buggy past
            // horizon lands here too, which is safe).
            if (!awake) m_awakeEntities.add(id);
          }
        }
      }

      // OPTION A: everything below runs for EVERY entity, dormant or not — this is
      // what preserves the reaping / break-check coupling for free. Do NOT gate it.
      if (auto* tileEntity = entity->asTileEntity()) {
        // Only do break checks on objects if all sectors the object touches
        // *and surrounding sectors* are active.  Objects that this object
        // rests on can be up to an entire sector large in any direction.
        if (doBreakChecks && regionActive(RectI::integral(tileEntity->metaBoundBox().translated(tileEntity->position())).padded(WorldSectorSize)))
          tileEntity->checkBroken();
        updateTileEntityTiles(tileEntity);
      }

      if (entity->shouldDestroy() && entity->entityMode() == EntityMode::Master)
        toRemove.append(entity->entityId());
    }, [](EntityPtr const& a, EntityPtr const& b) {
      return a->entityType() < b->entityType();
    });
  }

  // From here to the end of the function every phase WRAPS its `if` rather than sitting inside it.
  // shouldRunThisStep() is a fidelity-driven cadence gate, so a scope placed inside the branch would
  // fire on only a fraction of ticks; these are cadence=Call so they would not be scaled up, but they
  // WOULD silently vanish from any window in which the gate never fired -- and an absent row reads as
  // "no such phase", not "this phase did nothing". Outside the branch each records ~0 instead.
  {
    TelemetryScope s(tcScripts);
    for (auto& pair : m_scriptContexts)
      pair.second->update(pair.second->updateDt(dt));
  }

  {
    TelemetryScope s(tcDamage);
    updateDamage(dt);
  }

  {
    TelemetryScope s(tcWiring);
    if (shouldRunThisStep("wiringUpdate"))
      m_wireProcessor->process();
  }

  {
    TelemetryScope s(tcSky);
    m_sky->update(dt);
  }

  {
    TelemetryScope s(tcClientRegions);
    for (auto const& pair : m_clientInfo) {
      clientWindows.append(pair.second->clientState.window());
      for (auto const& region : pair.second->monitoringRegions(m_entityMap))
        clientMonitoringRegions.appendAll(m_geometry.splitRect(region));
    }
  }

  {
    TelemetryScope s(tcWeather);
    m_weather.setClientVisibleRegions(clientWindows);
    m_weather.update(dt);
    for (auto projectile : m_weather.pullNewProjectiles())
      addEntity(std::move(projectile));
  }

  {
    TelemetryScope s(tcLiquid);
    if (shouldRunThisStep("liquidUpdate")) {
      m_liquidEngine->setProcessingLimit(m_fidelityConfig.optUInt("liquidEngineBackgroundProcessingLimit"));
      m_liquidEngine->setNoProcessingLimitRegions(clientMonitoringRegions);
      m_liquidEngine->update();
    }
  }

  {
    TelemetryScope s(tcFallingBlocks);
    if (shouldRunThisStep("fallingBlocksUpdate"))
      m_fallingBlocksAgent->update();
  }

  {
    TelemetryScope s(tcBlockDamage);
    if (auto delta = shouldRunThisStep("blockDamageUpdate"))
      updateDamagedBlocks(*delta * dt);
  }

  {
  TelemetryScope sStorage(tcStorage);
  if (auto delta = shouldRunThisStep("worldStorageTick"))
    m_worldStorage->tick(*delta * GlobalTimestep, &m_worldId);

  if (auto delta = shouldRunThisStep("worldStorageGenerate")) {
    m_worldStorage->generateQueue(m_fidelityConfig.optUInt("worldStorageGenerationLevelLimit"), [this](WorldStorage::Sector a, WorldStorage::Sector b) {
        auto distanceToClosestPlayer = [this](WorldStorage::Sector sector) {
          Vec2F sectorCenter = RectF(*m_worldStorage->regionForSector(sector)).center();
          float distance = highest<float>();
          for (auto const& pair : m_clientInfo) {
            if (auto player = get<Player>(pair.second->clientState.playerId()))
              distance = min(vmag(sectorCenter - player->position()), distance);
          }
          return distance;
        };

        return distanceToClosestPlayer(a) < distanceToClosestPlayer(b);
      });
  }
  }

  {
    // Telemetry commit phase: master-entity destruction/removal for this tick.
    //
    // CADENCE CHANGED Tick -> Call, and the handle moved to file scope with its fifteen siblings. It
    // was declared Tick while living inside this runtime-gated function, which is the same
    // conditional-phase defect the lighting budget carried: on a window containing paused ticks the
    // consumer scaled it up against an unscaled Tick-cadence denominator. role stays Budget -- it is a
    // sibling of the other fifteen compute parts, not a parent of them.
    TelemetryScope s(tcCommit);
    for (EntityId entityId : toRemove)
      removeEntity(entityId, true);
  }

  {
  TelemetryScope sNetSync(tcNetSync);
  bool sendRemoteUpdates = m_entityUpdateTimer.wrapTick(dt);
  // Lever #4: pump the sky's deferred store once before the per-client loop
  // (Sky::writeUpdate has a setNeedsStoreCallback and shares the early-out gate).
  if (NetElementEarlyOut::active())
    m_sky->netStorePump();
  for (auto const& pair : m_clientInfo) {
    // Compute this client's monitoring regions once per tick and reuse them for both the
    // signalRegion pass and queueUpdatePackets (which used to recompute them internally).
    // Nothing between here and that use moves entities or changes the client's tracked set
    // (signalRegion only marks regions active), so the two computations are identical. (Lever #10)
    auto monitoringRegions = pair.second->monitoringRegions(m_entityMap);
    for (auto const& monitoredRegion : monitoringRegions)
      signalRegion(monitoredRegion.padded(jsonToVec2I(m_serverConfig.get("playerActiveRegionPad"))));
    queueUpdatePackets(pair.first, sendRemoteUpdates, monitoringRegions);
  }
  m_netStateCache.clear();
  m_netStorePumpedThisTick.clear();
  }

  {
  TelemetryScope sEpilogue(tcEpilogue);
  // Lever #4 measurement: while a gate is on, log the early-out coverage
  // (hits/(hits+walks)) roughly every ~10s and reset the window. Counts are
  // process-global (aggregate across all active server worlds); zero overhead
  // and no log line when both gates are OFF.
  if (NetElementEarlyOut::active() && ++m_netDeltaStatTick >= 600) {
    m_netDeltaStatTick = 0;
    uint64_t h = NetElementEarlyOut::hits.exchange(0, std::memory_order_relaxed);
    uint64_t total = h + NetElementEarlyOut::walks.exchange(0, std::memory_order_relaxed);
    if (total > 0)
      Logger::info("netDelta early-out coverage: {}/{} entity-writes skipped ({:.1f}%) over last window",
          h, total, 100.0 * (double)h / (double)total);
  }

  // Entity-dormancy measurement (Task 6), mirroring the Lever #4 log above: while a
  // dormancy gate is on, log the dormancy ratio (would-be-/actually-dormant updates
  // over the window) plus the live awake-set / total entity counts and the cumulative
  // validate mismatch tally, then reset the window. Process-global counts; zero cost
  // and no log line when dormancy is OFF.
  if (dormancyActive && ++m_dormancyStatTick >= 600) {
    m_dormancyStatTick = 0;
    // Prune stale ids: entities can leave via paths that bypass WorldServer::removeEntity
    // (WorldStorage sector-unload calls m_entityMap->removeEntity directly). Harmless to the
    // tick loop (it iterates live entities + only tests contains()), and the skipped/ran
    // ratio is unaffected (stale ids are never iterated/counted), but it's unbounded growth
    // and skews the awake/live telemetry count. O(awake) every ~600 ticks (only while a
    // dormancy gate is on). Robust to every current/future removal path.
    eraseWhere(m_awakeEntities, [this](EntityId id) { return !m_entityMap->entity(id); });
    uint64_t skipped = EntityDormancy::skipped.exchange(0, std::memory_order_relaxed);
    uint64_t total = skipped + EntityDormancy::ran.exchange(0, std::memory_order_relaxed);
    if (total > 0)
      Logger::info("entity dormancy: {}/{} entity-updates {} ({:.1f}%) over last window; awake {}/{} live; validate mismatches {}",
          skipped, total, dormancyEnabled ? "skipped" : "would-skip", 100.0 * (double)skipped / (double)total,
          m_awakeEntities.size(), m_entityMap->size(),
          EntityDormancy::mismatches.load(std::memory_order_relaxed));
  }

  for (auto& pair : m_clientInfo)
    pair.second->pendingForward = false;

  m_expiryTimer.tick(dt);

  gEntitiesLive.set((int64_t)m_entityMap->size());
  LogMap::set(strf("server_{}_entities", m_worldId), strf("{} in {} sectors", m_entityMap->size(), m_tileArray->loadedSectorCount()));
  LogMap::set(strf("server_{}_time", m_worldId), strf("age = {:4.2f}, day = {:4.2f}/{:4.2f}s", epochTime(), timeOfDay(), dayLength()));
  LogMap::set(strf("server_{}_active_liquid", m_worldId), m_liquidEngine->activeCells());
  LogMap::set(strf("server_{}_lua_mem", m_worldId), m_luaRoot->luaMemoryUsage());
  }
}

WorldGeometry WorldServer::geometry() const {
  return m_geometry;
}

uint64_t WorldServer::currentStep() const {
  return m_currentStep;
}

uint64_t WorldServer::dormancyMaxSleepSteps() const {
  return m_dormancyMaxSleepSteps;
}

size_t WorldServer::awakeEntityCount() const {
  return m_awakeEntities.size();
}

bool WorldServer::isEntityAwake(EntityId entityId) const {
  return m_awakeEntities.contains(entityId);
}

MaterialId WorldServer::material(Vec2I const& pos, TileLayer layer) const {
  return m_tileArray->tile(pos).material(layer);
}

MaterialHue WorldServer::materialHueShift(Vec2I const& position, TileLayer layer) const {
  auto const& tile = m_tileArray->tile(position);
  return layer == TileLayer::Foreground ? tile.foregroundHueShift : tile.backgroundHueShift;
}

ModId WorldServer::mod(Vec2I const& pos, TileLayer layer) const {
  return m_tileArray->tile(pos).mod(layer);
}

MaterialHue WorldServer::modHueShift(Vec2I const& position, TileLayer layer) const {
  auto const& tile = m_tileArray->tile(position);
  return layer == TileLayer::Foreground ? tile.foregroundModHueShift : tile.backgroundModHueShift;
}

MaterialColorVariant WorldServer::colorVariant(Vec2I const& position, TileLayer layer) const {
  auto const& tile = m_tileArray->tile(position);
  return layer == TileLayer::Foreground ? tile.foregroundColorVariant : tile.backgroundColorVariant;
}

EntityPtr WorldServer::entity(EntityId entityId) const {
  return m_entityMap->entity(entityId);
}

void WorldServer::addEntity(EntityPtr const& entity, EntityId entityId) {
  if (!entity)
    return;

  entity->init(this, m_entityMap->reserveEntityId(entityId), EntityMode::Master);
  m_entityMap->addEntity(entity);

  // Option A dormancy: seed the awake-set so a freshly added entity runs its first
  // tick. Belt-and-suspenders — the load-bearing guarantee is Entity::m_wakeRequested
  // (default true), which makes takeWakeRequested() fire on first appearance. Gated
  // on active() so OFF builds never populate a set nothing reads (M5).
  if (EntityDormancy::active())
    m_awakeEntities.add(entity->entityId());

  if (auto* tileEntity = entity->asTileEntity())
    updateTileEntityTiles(tileEntity);
}

EntityPtr WorldServer::closestEntity(Vec2F const& center, float radius, EntityFilter selector) const {
  return m_entityMap->closestEntity(center, radius, selector);
}

void WorldServer::forAllEntities(EntityCallback callback) const {
  m_entityMap->forAllEntities(callback);
}

void WorldServer::forEachEntity(RectF const& boundBox, EntityCallback const& callback) const {
  m_entityMap->forEachEntity(boundBox, callback);
}

void WorldServer::forEachEntityLine(Vec2F const& begin, Vec2F const& end, EntityCallback const& callback) const {
  m_entityMap->forEachEntityLine(begin, end, callback);
}

void WorldServer::forEachEntityAtTile(Vec2I const& pos, EntityCallbackOf<TileEntity> const& callback) const {
  m_entityMap->forEachEntityAtTile(pos, callback);
}

EntityPtr WorldServer::findEntity(RectF const& boundBox, EntityFilter const& entityFilter) const {
  return m_entityMap->findEntity(boundBox, entityFilter);
}

EntityPtr WorldServer::findEntityLine(Vec2F const& begin, Vec2F const& end, EntityFilter const& entityFilter) const {
  return m_entityMap->findEntityLine(begin, end, entityFilter);
}

EntityPtr WorldServer::findEntityAtTile(Vec2I const& pos, EntityFilterOf<TileEntity> const& entityFilter) const {
  return m_entityMap->findEntityAtTile(pos, entityFilter);
}

bool WorldServer::tileIsOccupied(Vec2I const& pos, TileLayer layer, bool includeEphemeral, bool checkCollision) const {
  return WorldImpl::tileIsOccupied(m_tileArray, m_entityMap, pos, layer, includeEphemeral, checkCollision);
}

CollisionKind WorldServer::tileCollisionKind(Vec2I const& pos) const {
  return WorldImpl::tileCollisionKind(m_tileArray, m_entityMap, pos);
}


void WorldServer::forEachCollisionBlock(RectI const& region, function<void(CollisionBlock const&)> const& iterator) const {
  const_cast<WorldServer*>(this)->freshenCollision(region);
  WorldImpl::forEachCollisionBlock(m_tileArray, region, [&iterator](Vec2I const& pos, CollisionBlock const* block) {
      if (block) iterator(*block);
      else iterator(CollisionBlock::nullBlock(pos));
    });
}

void WorldServer::getCollisionBlocks(RectI const& region, List<CollisionBlockRef>& output) const {
  const_cast<WorldServer*>(this)->freshenCollision(region);
  WorldImpl::forEachCollisionBlock(m_tileArray, region, [&output](Vec2I const& pos, CollisionBlock const* block) {
      output.append(CollisionBlockRef{pos, block});
    });
}

bool WorldServer::isTileConnectable(Vec2I const& pos, TileLayer layer, bool tilesOnly) const {
  return m_tileArray->tile(pos).isConnectable(layer, tilesOnly);
}

bool WorldServer::pointTileCollision(Vec2F const& point, CollisionSet const& collisionSet) const {
  return m_tileArray->tile(Vec2I(point.floor())).isColliding(collisionSet);
}

bool WorldServer::lineTileCollision(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet) const {
  return WorldImpl::lineTileCollision(m_geometry, m_tileArray, begin, end, collisionSet);
}

Maybe<pair<Vec2F, Vec2I>> WorldServer::lineTileCollisionPoint(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet) const {
  return WorldImpl::lineTileCollisionPoint(m_geometry, m_tileArray, begin, end, collisionSet);
}

List<Vec2I> WorldServer::collidingTilesAlongLine(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet, int maxSize, bool includeEdges) const {
  return WorldImpl::collidingTilesAlongLine(m_geometry, m_tileArray, begin, end, collisionSet, maxSize, includeEdges);
}

bool WorldServer::rectTileCollision(RectI const& region, CollisionSet const& collisionSet) const {
  return WorldImpl::rectTileCollision(m_tileArray, region, collisionSet);
}

LiquidLevel WorldServer::liquidLevel(Vec2I const& pos) const {
  return m_tileArray->tile(pos).liquid;
}

LiquidLevel WorldServer::liquidLevel(RectF const& region) const {
  return WorldImpl::liquidLevel(m_tileArray, region);
}

void WorldServer::activateLiquidRegion(RectI const& region) {
  m_liquidEngine->visitRegion(region);
}

void WorldServer::activateLiquidLocation(Vec2I const& location) {
  m_liquidEngine->visitLocation(location);
}

void WorldServer::requestGlobalBreakCheck() {
  m_needsGlobalBreakCheck = true;
}

void WorldServer::setSpawningEnabled(bool spawningEnabled) {
  m_spawner.setActive(spawningEnabled);
}

void WorldServer::setPropertyListener(String const& propertyName, WorldPropertyListener listener) {
  m_worldPropertyListeners[propertyName] = listener;
}

TileModificationList WorldServer::validTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) const {
  return WorldImpl::splitTileModifications(m_entityMap, modificationList, allowEntityOverlap, m_tileGetterFunction, [this](Vec2I pos, TileModification) {
      return !isTileProtected(pos);
    }).first;
}

TileModificationList WorldServer::applyTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) {
  return doApplyTileModifications(modificationList, allowEntityOverlap);
}

bool WorldServer::forceModifyTile(Vec2I const& pos, TileModification const& modification, bool allowEntityOverlap) {
  return forceApplyTileModifications({{pos, modification}}, allowEntityOverlap).empty();
}

TileModificationList WorldServer::forceApplyTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) {
  return doApplyTileModifications(modificationList, allowEntityOverlap, true);
}

bool WorldServer::replaceTile(Vec2I const& pos, TileModification const& modification, TileDamage const& tileDamage) {
  if (isTileProtected(pos))
    return false;

  if (!WorldImpl::validateTileReplacement(modification))
    return false;
  
  if (auto placeMaterial = modification.ptr<PlaceMaterial>()) {
    if (!isTileConnectable(pos, placeMaterial->layer, true))
      return false;

    if (auto tile = m_tileArray->modifyTile(pos)) {
      auto damageParameters = WorldImpl::tileDamageParameters(tile, placeMaterial->layer, tileDamage);
      bool harvested = tileDamage.amount >= 0 && tileDamage.harvestLevel >= damageParameters.requiredHarvestLevel();
      auto damage = placeMaterial->layer == TileLayer::Foreground ? tile->foregroundDamage : tile->backgroundDamage;
      Vec2F dropPosition = centerOfTile(pos);

      for (auto drop : destroyBlock(placeMaterial->layer, pos, harvested, !tileDamageIsPenetrating(damage.damageType()), false))
        addEntity(ItemDrop::createRandomizedDrop(drop, dropPosition));
      
      return true;
    }
  }

  return false;
}

TileModificationList WorldServer::replaceTiles(TileModificationList const& modificationList, TileDamage const& tileDamage, bool applyDamage) {
  TileModificationList success, failures;

  if (applyDamage) {
    List<Vec2I> toDamage;
    TileLayer layer;

    for (auto pair : modificationList) {
      if (auto placeMaterial = pair.second.ptr<PlaceMaterial>()) {
        layer = placeMaterial->layer;

        if (placeMaterial->material == material(pair.first, layer)) {
          failures.append(pair);
          continue;
        }

        if (damageWouldDestroy(pair.first, layer, tileDamage)) {
          if (replaceTile(pair.first, pair.second, tileDamage))
            success.append(pair);
          else
            failures.append(pair);
          continue;
        }

        toDamage.append(pair.first);
        success.append(pair);
        continue;
      }
      
      failures.append(pair);
    }

    if (!toDamage.empty())
      damageTiles(toDamage, layer, Vec2F(), tileDamage, Maybe<EntityId>());
    
  } else {
    for (auto pair : modificationList) {
      if (replaceTile(pair.first, pair.second, tileDamage))
        success.append(pair);
      else
        failures.append(pair);
    }
  }

  failures.appendAll(doApplyTileModifications(success, true, false, false));

  for (auto pair : success) {
    checkEntityBreaks(RectF::withSize(Vec2F(pair.first), Vec2F(1, 1)));
    m_liquidEngine->visitLocation(pair.first);
    m_fallingBlocksAgent->visitLocation(pair.first);
  }

  return failures;
}

bool WorldServer::damageWouldDestroy(Vec2I const& pos, TileLayer layer, TileDamage const& tileDamage) const {
  return WorldImpl::damageWouldDestroy(m_tileArray, pos, layer, tileDamage);
}

TileDamageResult WorldServer::damageTiles(List<Vec2I> const& positions, TileLayer layer, Vec2F const& sourcePosition, TileDamage const& damage, Maybe<EntityId> sourceEntity) {
  Set<Vec2I> positionSet;
  for (auto const& pos : positions)
    positionSet.add(m_geometry.xwrap(pos));

  Set<EntityPtr> damagedEntities;
  auto res = TileDamageResult::None;

  for (auto const& pos : positionSet) {
    if (auto tile = m_tileArray->modifyTile(pos)) {
      auto tileDamage = damage;
      if (isTileProtected(pos))
        tileDamage.type = TileDamageType::Protected;

      auto tileRes = TileDamageResult::None;
      if (layer == TileLayer::Foreground) {
        Vec2I entityDamagePos = pos;
        Set<Vec2I> damagePositionSet = Set<Vec2I>(positionSet);
        if (tile->rootSource) {
          entityDamagePos = tile->rootSource.value();
          damagePositionSet.add(entityDamagePos);
        }

        for (auto entity : m_entityMap->entitiesAtTile(entityDamagePos)) {
          if (!damagedEntities.contains(entity)) {
            Set<Vec2I> entitySpacesSet;
            for (auto const& space : entity->spaces())
              entitySpacesSet.add(m_geometry.xwrap(entity->tilePosition() + space));

            // Dormancy wake (audit Rule 3): caller-loop placement so the
            // FarmableObject::damageTiles virtual override cannot dodge the wake.
            entity->requestWake();
            bool broken = entity->damageTiles(entitySpacesSet.intersection(damagePositionSet).values(), sourcePosition, tileDamage);
            if (sourceEntity.isValid() && broken) {
              Maybe<String> name;
              if (auto object = as<Object>(entity))
                name = object->name();
              sendEntityMessage(*sourceEntity, "tileEntityBroken", {
                  jsonFromVec2I(pos),
                  EntityTypeNames.getRight(entity->entityType()),
                  jsonFromMaybe(name),
                });
            }

            if (tileDamage.type == TileDamageType::Protected)
              tileRes = TileDamageResult::Protected;
            else if (broken || entity->canBeDamaged()) {
              tileRes = TileDamageResult::Normal;
              damagedEntities.add(entity);
            }
          }
        }
      }

      // Penetrating damage should carry through to the blocks behind this
      // entity.
      if (tileRes == TileDamageResult::None || tileDamageIsPenetrating(tileDamage.type)) {
        auto damageParameters = WorldImpl::tileDamageParameters(tile, layer, tileDamage);

        if (layer == TileLayer::Foreground && isRealMaterial(tile->foreground)) {
          if (!tile->rootSource || damagedEntities.empty()) {
            tile->foregroundDamage.damage(damageParameters, sourcePosition, tileDamage);

            // if the tile is broken, send a message back to the source entity with position, layer, dungeonId, and whether the tile was harvested
            if (sourceEntity.isValid() && tile->foregroundDamage.dead()) {
              sendEntityMessage(*sourceEntity, "tileBroken", {
                  jsonFromVec2I(pos),
                  TileLayerNames.getRight(TileLayer::Foreground),
                  tile->foreground,
                  tile->dungeonId,
                  tile->foregroundDamage.harvested(),
                });
            }

            queueTileDamageUpdates(pos, TileLayer::Foreground);
            m_damagedBlocks.add(pos);

            if (tileDamage.type == TileDamageType::Protected)
              tileRes = TileDamageResult::Protected;
            else
              tileRes = TileDamageResult::Normal;
          }
        } else if (layer == TileLayer::Background && isRealMaterial(tile->background)) {
          tile->backgroundDamage.damage(damageParameters, sourcePosition, tileDamage);
          
          // if the tile is broken, send a message back to the source entity with position and whether the tile was harvested
            if (sourceEntity.isValid() && tile->backgroundDamage.dead()) {
              sendEntityMessage(*sourceEntity, "tileBroken", {
                  jsonFromVec2I(pos),
                  TileLayerNames.getRight(TileLayer::Background),
                  tile->background,
                  tile->dungeonId,
                  tile->backgroundDamage.harvested(),
                });
            }

          queueTileDamageUpdates(pos, TileLayer::Background);
          m_damagedBlocks.add(pos);

          if (tileDamage.type == TileDamageType::Protected)
            tileRes = TileDamageResult::Protected;
          else
            tileRes = TileDamageResult::Normal;
        }
      }

      res = max<TileDamageResult>(res, tileRes);
    }
  }

  return res;
}

DungeonId WorldServer::dungeonId(Vec2I const& pos) const {
  return m_tileArray->tile(pos).dungeonId;
}

bool WorldServer::isPlayerModified(RectI const& region) const {
  return m_tileArray->tileSatisfies(region, [](Vec2I const&, ServerTile const& tile) {
      return tile.dungeonId == ConstructionDungeonId || tile.dungeonId == DestroyedBlockDungeonId;
    });
}

ItemDescriptor WorldServer::collectLiquid(List<Vec2I> const& tilePositions, LiquidId liquidId) {
  float bucketSize = Root::singleton().assets()->json("/items/defaultParameters.config:liquidItems.bucketSize").toFloat();
  unsigned drainedUnits = 0;
  float nextUnit = bucketSize;
  List<ServerTile*> maybeDrainTiles;

  for (auto pos : tilePositions) {
    ServerTile* tile = m_tileArray->modifyTile(pos);
    if (tile->liquid.liquid == liquidId && !isTileProtected(pos)) {
      if (tile->liquid.level >= nextUnit) {
        tile->liquid.take(nextUnit);
        nextUnit = bucketSize;
        drainedUnits++;

        for (auto previousTile : maybeDrainTiles)
          previousTile->liquid.take(previousTile->liquid.level);

        maybeDrainTiles.clear();
      }

      if (tile->liquid.level > 0) {
        nextUnit -= tile->liquid.level;
        maybeDrainTiles.append(tile);
      }

      for (auto const& pair : m_clientInfo) {
        if (pair.second->activeSectors.contains(m_tileArray->sectorFor(pos)))
          pair.second->pendingLiquidUpdates.add(pos);
      }
      m_liquidEngine->visitLocation(pos);
    }
  }

  if (drainedUnits > 0) {
    auto liquidConfig = Root::singleton().liquidsDatabase()->liquidSettings(liquidId);
    if (liquidConfig && liquidConfig->itemDrop)
      return liquidConfig->itemDrop.multiply(drainedUnits);
  }

  return ItemDescriptor();
}

bool WorldServer::placeDungeon(String const& dungeonName, Vec2I const& position, Maybe<DungeonId> dungeonId, bool forcePlacement) {
  m_generatingDungeon = true;
  m_tileProtectionEnabled = false;

  auto seed = worldTemplate()->seedFor(position[0], position[1]);
  auto facade = make_shared<DungeonGeneratorWorld>(this, true);
  bool placed = false;
  DungeonGenerator dungeonGenerator(dungeonName, seed, m_worldTemplate->threatLevel(), dungeonId);
  if (auto generateResult = dungeonGenerator.generate(facade, position, false, forcePlacement)) {
    auto worldGenerator = make_shared<WorldGenerator>(this);
    for (auto position : generateResult->second) {
      if (ServerTile* tile = modifyServerTile(position))
        worldGenerator->replaceBiomeBlocks(tile);
    }
    placed = true;
  }

  m_tileProtectionEnabled = true;
  m_generatingDungeon = false;

  return placed;
}

void WorldServer::addBiomeRegion(Vec2I const& position, String const& biomeName, String const& subBlockSelector, int width) {
  width = std::min(width, (int)m_worldTemplate->size()[0]);

  auto regions = m_worldTemplate->previewAddBiomeRegion(position, width);

  if (regions.empty()) {
    Logger::info("Aborting addBiomeRegion as it would have no result!");
    return;
  }

  for (auto region : regions) {
    for (auto sector : m_worldStorage->sectorsForRegion(region))
      m_worldStorage->triggerTerraformSector(sector);
  }

  m_worldTemplate->addBiomeRegion(position, biomeName, subBlockSelector, width);
}

void WorldServer::expandBiomeRegion(Vec2I const& position, int newWidth) {
  newWidth = std::min(newWidth, (int)m_worldTemplate->size()[0]);

  auto regions = m_worldTemplate->previewExpandBiomeRegion(position, newWidth);

  if (regions.empty()) {
    Logger::info("Aborting expandBiomeRegion as it would have no result!");
    return;
  }

  for (auto region : regions) {
    for (auto sector : m_worldStorage->sectorsForRegion(region))
      m_worldStorage->triggerTerraformSector(sector);
  }

  m_worldTemplate->expandBiomeRegion(position, newWidth);
}

bool WorldServer::pregenerateAddBiome(Vec2I const& position, int width) {
  auto regions = m_worldTemplate->previewAddBiomeRegion(position, width);

  bool generationComplete = true;
  for (auto region : regions)
    generationComplete = generationComplete && signalRegion(region);

  return generationComplete;
}

bool WorldServer::pregenerateExpandBiome(Vec2I const& position, int newWidth) {
  auto regions = m_worldTemplate->previewExpandBiomeRegion(position, newWidth);

  bool generationComplete = true;
  for (auto region : regions)
    generationComplete = generationComplete && signalRegion(region);

  return generationComplete;
}

void WorldServer::setLayerEnvironmentBiome(Vec2I const& position) {
  auto biomeName = m_worldTemplate->worldLayout()->setLayerEnvironmentBiome(position);

  auto layoutJson = m_worldTemplate->worldLayout()->toJson();
  for (auto const& pair : m_clientInfo)
    pair.second->outgoingPackets.append(make_shared<WorldLayoutUpdatePacket>(layoutJson));
}

void WorldServer::setPlanetType(String const& planetType, String const& primaryBiomeName) {
  if (auto terrestrialParameters = as<TerrestrialWorldParameters>(m_worldTemplate->worldParameters())) {
    if (terrestrialParameters->typeName != planetType) {
      auto newTerrestrialParameters = make_shared<TerrestrialWorldParameters>(*terrestrialParameters);

      newTerrestrialParameters->typeName = planetType;
      newTerrestrialParameters->primaryBiome = primaryBiomeName;

      auto biomeDatabase = Root::singleton().biomeDatabase();
      auto newWeatherPool = biomeDatabase->biomeWeathers(primaryBiomeName, m_worldTemplate->worldSeed(), m_worldTemplate->threatLevel());
      newTerrestrialParameters->weatherPool = newWeatherPool;

      auto newSkyColors = biomeDatabase->biomeSkyColoring(primaryBiomeName, m_worldTemplate->worldSeed());
      newTerrestrialParameters->skyColoring = newSkyColors;

      newTerrestrialParameters->airless = biomeDatabase->biomeIsAirless(primaryBiomeName);
      newTerrestrialParameters->environmentStatusEffects = {};

      newTerrestrialParameters->terraformed = true;

      m_worldTemplate->setWorldParameters(newTerrestrialParameters);

      for (auto const& pair : m_clientInfo)
        pair.second->outgoingPackets.append(make_shared<WorldParametersUpdatePacket>(netStoreVisitableWorldParameters(newTerrestrialParameters)));

      auto newSkyParameters = SkyParameters(m_worldTemplate->skyParameters(), newTerrestrialParameters);
      m_worldTemplate->setSkyParameters(newSkyParameters);

      auto referenceClock = m_sky->referenceClock();
      m_sky = make_shared<Sky>(m_worldTemplate->skyParameters(), false);
      m_sky->setReferenceClock(referenceClock);

      m_weather.setup(m_worldTemplate->weathers(), m_worldTemplate->undergroundLevel(), m_geometry, [this](Vec2I const& pos) {
        auto const& tile = m_tileArray->tile(pos);
        return !isRealMaterial(tile.background);
      });

      m_newPlanetType = pair<String, String>{planetType, primaryBiomeName};
    }
  }
}


void WorldServer::setWeatherIndex(size_t weatherIndex, bool force) {
  m_weather.setWeatherIndex(weatherIndex, force);
}

void WorldServer::setWeather(String const& weatherName, bool force) {
  m_weather.setWeather(weatherName, force);
}

StringList WorldServer::weatherList() const {
  return m_weather.weatherList();
}

Maybe<pair<String, String>> WorldServer::pullNewPlanetType() {
  if (m_newPlanetType)
    return m_newPlanetType.take();
  return {};
}

bool WorldServer::isTileProtected(Vec2I const& pos) const {
  if (!m_tileProtectionEnabled)
    return false;

  auto const& tile = m_tileArray->tile(pos);
  return m_protectedDungeonIds.contains(tile.dungeonId);
}

bool WorldServer::getTileProtection(DungeonId dungeonId) const {
  return m_protectedDungeonIds.contains(dungeonId);
}

void WorldServer::setTileProtection(DungeonId dungeonId, bool isProtected) {
  bool updated = false;
  if (isProtected) {
    updated = m_protectedDungeonIds.add(dungeonId);
  } else {
    updated = m_protectedDungeonIds.remove(dungeonId);
  }

  if (updated) {
    for (auto const& pair : m_clientInfo)
      pair.second->outgoingPackets.append(make_shared<UpdateTileProtectionPacket>(dungeonId, isProtected));
  
    Logger::info("Protected dungeonIds for world set to {}", m_protectedDungeonIds);
  }
}

size_t WorldServer::setTileProtection(List<DungeonId> const& dungeonIds, bool isProtected) {
  List<PacketPtr> updates;
  updates.reserve(dungeonIds.size());
  for (auto const& dungeonId : dungeonIds)
    if (isProtected ? m_protectedDungeonIds.add(dungeonId) : m_protectedDungeonIds.remove(dungeonId))
      updates.append(make_shared<UpdateTileProtectionPacket>(dungeonId, isProtected));

  if (updates.empty())
    return 0;

  for (auto const& pair : m_clientInfo)
    pair.second->outgoingPackets.appendAll(updates);

  auto newDungeonIds = m_protectedDungeonIds.values();
  sort(newDungeonIds);
  Logger::info("Protected dungeonIds for world set to {}", newDungeonIds);
  return updates.size();
}

void WorldServer::setTileProtectionEnabled(bool enabled) {
  m_tileProtectionEnabled = enabled;
}

void WorldServer::setDungeonId(RectI const& tileArea, DungeonId dungeonId) {
  for (int x = tileArea.xMin(); x < tileArea.xMax(); ++x) {
    for (int y = tileArea.yMin(); y < tileArea.yMax(); ++y) {
      auto pos = Vec2I{x, y};
      if (auto tile = m_tileArray->modifyTile(pos)) {
        tile->dungeonId = dungeonId;
        queueTileUpdates(pos);
      }
    }
  }
}

void WorldServer::setDungeonGravity(DungeonId dungeonId, Maybe<float> gravity) {
  Maybe<float> current = m_dungeonIdGravity.maybe(dungeonId);
  if (gravity != current) {
    if (gravity)
      m_dungeonIdGravity[dungeonId] = *gravity;
    else
      m_dungeonIdGravity.remove(dungeonId);

    for (auto const& p : m_clientInfo)
      p.second->outgoingPackets.append(make_shared<SetDungeonGravityPacket>(dungeonId, gravity));
  }
}

float WorldServer::gravity(Vec2F const& pos) const {
  return gravityFromTile(m_tileArray->tile(Vec2I::round(pos)));
}

float WorldServer::gravityFromTile(ServerTile const& tile) const {
  return m_dungeonIdGravity.maybe(tile.dungeonId).value(m_worldTemplate->gravity());
}

bool WorldServer::isFloatingDungeonWorld() const {
  return m_worldTemplate && m_worldTemplate->worldParameters()
      && m_worldTemplate->worldParameters()->type() == WorldParametersType::FloatingDungeonWorldParameters;
}

void WorldServer::init(bool firstTime) {
  auto& root = Root::singleton();
  auto assets = root.assets();
  auto liquidsDatabase = root.liquidsDatabase();

  m_serverConfig = assets->json("/worldserver.config");
  // Lever #4: load the net-delta dirty-version early-out gates (process-global,
  // default OFF). Idempotent across worlds (every world loads the same asset).
  // Validated levers default ON (the fallback matches the canonical worldserver.config
  // default); the validate debug-oracles stay OFF.
  NetElementEarlyOut::enabled.store(m_serverConfig.getBool("netDeltaDirtyVersionEarlyOut", true), std::memory_order_relaxed);
  NetElementEarlyOut::validate.store(m_serverConfig.getBool("netDeltaDirtyVersionValidate", false), std::memory_order_relaxed);
  // Arc-A Rung 1: load the entity-dormancy awake-set gates (process-global, default ON).
  EntityDormancy::enabled.store(m_serverConfig.getBool("entityDormancyEnabled", true), std::memory_order_relaxed);
  EntityDormancy::validate.store(m_serverConfig.getBool("entityDormancyValidate", false), std::memory_order_relaxed);
  // Lever L2: collision-poly arena reuse (process-global, default ON; byte-identical
  // to OFF, kill-switch only). MovementController has no config access, so set the
  // process-global here.
  CollisionArena::enabled.store(m_serverConfig.getBool("collisionArenaEnabled", true), std::memory_order_relaxed);
  // Lever L-WIND-A: skip the render-only plant-wind dead store on the server
  // (process-global, default ON; byte-identical, kill-switch + A/B toggle).
  PlantWind::serverSkip.store(m_serverConfig.getBool("plantWindServerSkip", true), std::memory_order_relaxed);
  // Lever L-DMG-SKIP-0: skip the per-tick DamageManager damageSources() query for
  // provably-empty entities (process-global, default ON; byte-equivalent, kill-switch
  // + live A/B toggle).
  DamageSourceSkip::enabled.store(m_serverConfig.getBool("damageSourceSkipEnabled", true), std::memory_order_relaxed);
  // Task 7: staggered max-sleep cap. Convert the configured seconds to steps via the
  // per-step timestep (default 1/60s -> 10s == 600 steps); clamp to >= 1 so the cap is
  // always a genuine future step. Read here (not process-global) since it's a per-world
  // scheduler bound, not a global gate; every world loads the same asset so it's stable.
  m_dormancyMaxSleepSteps = max<uint64_t>(1, (uint64_t)round(m_serverConfig.getDouble("entityDormancyMaxSleepSeconds", 10.0) / ServerGlobalTimestep));
  setFidelity(WorldServerFidelity::Medium);

  m_worldStorage->setFloatingDungeonWorld(isFloatingDungeonWorld());

  m_currentTime = 0;
  m_currentStep = 0;
  m_generatingDungeon = false;
  m_geometry = WorldGeometry(m_worldTemplate->size());
  m_entityMap = m_worldStorage->entityMap();
  m_tileArray = m_worldStorage->tileArray();
  m_tileGetterFunction = [&](Vec2I pos) -> ServerTile const& { return m_tileArray->tile(pos); };
  m_damageManager = make_shared<DamageManager>(this, ServerConnectionId);
  m_wireProcessor = make_shared<WireProcessor>(m_worldStorage);
  m_luaRoot = make_shared<LuaRoot>();
  m_luaRoot->luaEngine().setNullTerminated(false);
  m_luaRoot->tuneAutoGarbageCollection(m_serverConfig.getFloat("luaGcPause"), m_serverConfig.getFloat("luaGcStepMultiplier"));

  m_sky = make_shared<Sky>(m_worldTemplate->skyParameters(), false);

  m_lightIntensityCalculator.setParameters(assets->json("/lighting.config:intensity"));

  m_entityMessageResponses = {};

  m_collisionGenerator.init([=](int x, int y) {
      return m_tileArray->tile({x, y}).getCollision();
    });

  m_entityUpdateTimer = GameTimer(m_serverConfig.query("interpolationSettings.normal").getFloat("entityUpdateDelta") / 60.f);
  m_tileEntityBreakCheckTimer = GameTimer(m_serverConfig.getFloat("tileEntityBreakCheckInterval"));

  m_liquidEngine = make_shared<LiquidCellEngine<LiquidId>>(liquidsDatabase->liquidEngineParameters(), make_shared<LiquidWorld>(this));
  for (auto liquidSettings : liquidsDatabase->allLiquidSettings())
    m_liquidEngine->setLiquidTickDelta(liquidSettings->id, liquidSettings->tickDelta);

  m_fallingBlocksAgent = make_shared<FallingBlocksAgent>(make_shared<FallingBlocksWorld>(this));

  setupForceRegions();

  setTileProtection(ProtectedZeroGDungeonId, true);

  try {
    m_spawner.init(make_shared<SpawnerWorld>(this));

    RandomSource rnd = RandomSource(m_worldTemplate->worldSeed());

    if (firstTime) {
      m_generatingDungeon = true;
      DungeonId currentDungeonId = 0;

      for (auto const& dungeon : m_worldTemplate->dungeons()) {
        Logger::info("Placing dungeon {}", dungeon.dungeon);
        int retryCounter = m_serverConfig.getInt("spawnDungeonRetries");
        while (retryCounter > 0) {
          --retryCounter;
          auto dungeonFacade = make_shared<DungeonGeneratorWorld>(this, true);
          Vec2I position = Vec2I((dungeon.baseX + rnd.randInt(0, dungeon.xVariance)) % m_geometry.width(), dungeon.baseHeight);
          DungeonGenerator dungeonGenerator(dungeon.dungeon, m_worldTemplate->worldSeed(), m_worldTemplate->threatLevel(), currentDungeonId);
          if (auto generateResult = dungeonGenerator.generate(dungeonFacade, position, dungeon.blendWithTerrain, dungeon.force)) {
            if (dungeonGenerator.definition()->isProtected())
              setTileProtection(currentDungeonId, true);

            if (auto gravity = dungeonGenerator.definition()->gravity())
              m_dungeonIdGravity[currentDungeonId] = *gravity;

            if (auto breathable = dungeonGenerator.definition()->breathable())
              m_dungeonIdBreathable[currentDungeonId] = *breathable;

            currentDungeonId++;

            // floating dungeon worlds should force immediate generation (since there won't be terrain) to avoid
            // bottlenecking "generation" of empty generation levels during loading
            if (isFloatingDungeonWorld()) {
              for (auto region : generateResult->first)
                generateRegion(region);
            }

            break;
          }
        }
      }

      m_dungeonIdGravity[ZeroGDungeonId] = 0.0;
      m_dungeonIdGravity[ProtectedZeroGDungeonId] = 0.0;

      m_generatingDungeon = false;
    }

    if (m_adjustPlayerStart)
      m_playerStart = findPlayerStart(firstTime ? Maybe<Vec2F>() : m_playerStart);

    generateRegion(RectI::integral(RectF(m_playerStart, m_playerStart)).padded(m_serverConfig.getInt("playerStartInitialGenRadius")));

    m_weather.setup(m_worldTemplate->weathers(), m_worldTemplate->undergroundLevel(), m_geometry, [this](Vec2I const& pos) {
        auto const& tile = m_tileArray->tile(pos);
        return !isRealMaterial(tile.background);
      });
  } catch (std::exception const& e) {
    m_worldStorage->unloadAll(true);
    throw WorldServerException("Exception encountered initializing world", e);
  }
}

Maybe<unsigned> WorldServer::shouldRunThisStep(String const& timingConfiguration) {
  Vec2U timing = jsonToVec2U(m_fidelityConfig.get(timingConfiguration));
  if ((m_currentStep + timing[1]) % timing[0] == 0)
    return timing[0];
  return {};
}

TileModificationList WorldServer::doApplyTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap, bool ignoreTileProtection, bool updateNeighbors) {
  auto materialDatabase = Root::singleton().materialDatabase();

  TileModificationList unapplied = modificationList;
  size_t unappliedSize = unapplied.size();
  auto it = makeSMutableIterator(unapplied);
  while (it.hasNext()) {
    Vec2I pos;
    TileModification modification;
    std::tie(pos, modification) = it.next();

    if (!ignoreTileProtection && isTileProtected(pos))
      continue;

    if (auto placeMaterial = modification.ptr<PlaceMaterial>()) {
      bool allowTileOverlap = placeMaterial->collisionOverride != TileCollisionOverride::None && collisionKindFromOverride(placeMaterial->collisionOverride) < CollisionKind::Dynamic;
      if (!WorldImpl::canPlaceMaterial(m_entityMap, pos, placeMaterial->layer, placeMaterial->material, allowEntityOverlap, allowTileOverlap, m_tileGetterFunction))
        continue;

      ServerTile* tile = m_tileArray->modifyTile(pos);
      if (!tile)
        continue;

      if (placeMaterial->layer == TileLayer::Background) {
        tile->background = placeMaterial->material;
        if (placeMaterial->materialHueShift)
          tile->backgroundHueShift = *placeMaterial->materialHueShift;
        else
          tile->backgroundHueShift = m_worldTemplate->biomeMaterialHueShift(tile->blockBiomeIndex, placeMaterial->material);

        tile->backgroundColorVariant = DefaultMaterialColorVariant;
        if (tile->background == EmptyMaterialId) {
          // Remove the background mod if removing the background.
          tile->backgroundMod = NoModId;
        } else if (tile->liquid.source) {
          tile->liquid.pressure = 1.0f;
          tile->liquid.source = false;
        }
      } else {
        tile->foreground = placeMaterial->material;
        if (placeMaterial->materialHueShift)
          tile->foregroundHueShift = *placeMaterial->materialHueShift;
        else
          tile->foregroundHueShift = m_worldTemplate->biomeMaterialHueShift(tile->blockBiomeIndex, placeMaterial->material);

        tile->foregroundColorVariant = DefaultMaterialColorVariant;
        if (placeMaterial->collisionOverride != TileCollisionOverride::None)
          tile->updateCollision(collisionKindFromOverride(placeMaterial->collisionOverride));
        else
          tile->updateCollision(materialDatabase->materialCollisionKind(tile->foreground));
        if (tile->foreground == EmptyMaterialId) {
          // Remove the foreground mod if removing the foreground.
          tile->foregroundMod = NoModId;
        }
        if (materialDatabase->blocksLiquidFlow(tile->foreground))
          tile->liquid = LiquidStore();
      }

      tile->dungeonId = ConstructionDungeonId;

      if (updateNeighbors) {
        checkEntityBreaks(RectF::withSize(Vec2F(pos), Vec2F(1, 1)));
        m_liquidEngine->visitLocation(pos);
        m_fallingBlocksAgent->visitLocation(pos);
      }

      if (placeMaterial->layer == TileLayer::Foreground)
        dirtyCollision(RectI::withSize(pos, {1, 1}));
      queueTileUpdates(pos);

    } else if (auto placeMod = modification.ptr<PlaceMod>()) {
      if (!WorldImpl::canPlaceMod(pos, placeMod->layer, placeMod->mod, m_tileGetterFunction))
        continue;

      ServerTile* tile = m_tileArray->modifyTile(pos);
      if (!tile)
        continue;

      if (placeMod->layer == TileLayer::Background) {
        tile->backgroundMod = placeMod->mod;
        if (placeMod->modHueShift)
          tile->backgroundModHueShift = *placeMod->modHueShift;
        else
          tile->backgroundModHueShift = m_worldTemplate->biomeModHueShift(tile->blockBiomeIndex, placeMod->mod);
      } else {
        tile->foregroundMod = placeMod->mod;
        if (placeMod->modHueShift)
          tile->foregroundModHueShift = *placeMod->modHueShift;
        else
          tile->foregroundModHueShift = m_worldTemplate->biomeModHueShift(tile->blockBiomeIndex, placeMod->mod);
      }

      m_liquidEngine->visitLocation(pos);
      queueTileUpdates(pos);

    } else if (auto placeMaterialColor = modification.ptr<PlaceMaterialColor>()) {
      if (!WorldImpl::canPlaceMaterialColorVariant(pos, placeMaterialColor->layer, placeMaterialColor->color, m_tileGetterFunction))
        continue;

      WorldTile* tile = m_tileArray->modifyTile(pos);
      if (!tile)
        continue;

      if (placeMaterialColor->layer == TileLayer::Background) {
        tile->backgroundHueShift = 0;
        if (!materialDatabase->isMultiColor(tile->background))
          continue;
        tile->backgroundColorVariant = placeMaterialColor->color;
      } else {
        tile->foregroundHueShift = 0;
        if (!materialDatabase->isMultiColor(tile->foreground))
          continue;
        tile->foregroundColorVariant = placeMaterialColor->color;
      }

      queueTileUpdates(pos);

    } else if (auto plpacket = modification.ptr<PlaceLiquid>()) {
      modifyLiquid(pos, plpacket->liquid, plpacket->liquidLevel, true);
      m_liquidEngine->visitLocation(pos);
      m_fallingBlocksAgent->visitLocation(pos);
    }

    it.remove();

    if (!it.hasNext()) {
      // If we are at the end, but have made progress by applying at least one
      // modification, then start over at the beginning and keep trying more
      // modifications until we can't make any more progress.
      if (unapplied.size() != unappliedSize) {
        unappliedSize = unapplied.size();
        it.toFront();
      }
    }
  }

  return unapplied;
}

void WorldServer::updateTileEntityTiles(TileEntity* entity, bool removing, bool checkBreaks) {
  // This method of updating tile entity collision only works if each tile
  // entity's collision spaces are a subset of their normal spaces, and thus no
  // two tile entities can have collision spaces that overlap.
  // NOTE: Some entities may violate this; it's an odd thing to rely on policy
  // for and maybe we shouldn't allow tile entity configurations to specify
  // material spaces outside of their spaces

  auto& spaces = m_tileEntitySpaces[entity->entityId()];

  // L-OBJ-2: bind by const& (both arms lvalues) so the common non-removing path is a
  // zero-copy reference into the entity's stored material spaces. Read-only below
  // (compared at :==, iterated into a separate passedSpaces; never mutated/moved).
  static List<MaterialSpace> const emptyMaterialSpaces;
  List<MaterialSpace> const& newMaterialSpaces = removing ? emptyMaterialSpaces : entity->materialSpaces();
  List<Vec2I> newRoots = removing || entity->ephemeral() ? List<Vec2I>() : entity->roots();

  if (!removing && spaces.materials == newMaterialSpaces && spaces.roots == newRoots)
    return;

  auto materialDatabase = Root::singleton().materialDatabase();

  // remove all old roots
  for (auto const& rootPos : spaces.roots) {
    if (auto tile = m_tileArray->modifyTile(rootPos + entity->tilePosition()))
      tile->rootSource = {};
  }

  // remove all old material spaces
  for (auto const& materialSpace : spaces.materials) {
    Vec2I pos = materialSpace.space + entity->tilePosition();

    ServerTile* tile = m_tileArray->modifyTile(pos);
    if (tile) {
      tile->rootSource = {};
      bool updatedTile = false;
      bool updatedCollision = false;
      if (isBiomeMaterial(materialSpace.material) || tile->foreground == materialSpace.material) {
        // if the world is old, the materialSpace's collision may still be in the tile
        tile->foreground = EmptyMaterialId;
        tile->foregroundMod = NoModId;
        updatedTile = true;
        updatedCollision = tile->updateCollision(CollisionKind::None);
      }
      if (tile->updateObjectCollision(CollisionKind::None))
        updatedTile = updatedCollision = true;
      if (updatedCollision) {
        m_liquidEngine->visitLocation(pos);
        m_fallingBlocksAgent->visitLocation(pos);
        dirtyCollision(RectI::withSize(pos, { 1, 1 }));
      }
      if (updatedTile)
        queueTileUpdates(pos);
    }
  }

  if (removing) {
    m_tileEntitySpaces.remove(entity->entityId());
  } else {
    // add new material spaces and update the known material spaces entry
    List<MaterialSpace> passedSpaces;
    for (auto const& materialSpace : newMaterialSpaces) {
      Vec2I pos = materialSpace.space + entity->tilePosition();

      bool updatedTile = false;
      bool updatedCollision = false;
      ServerTile* tile = m_tileArray->modifyTile(pos);
      if (tile) {
        if (tile->foreground == EmptyMaterialId) {
          tile->foreground = materialSpace.material;
          tile->foregroundMod = NoModId;
          updatedTile = true;
        }
        if (isRealMaterial(materialSpace.material))
          tile->rootSource = entity->tilePosition();
        passedSpaces.emplaceAppend(materialSpace);
        updatedTile |= updatedCollision = tile->updateObjectCollision(materialDatabase->materialCollisionKind(materialSpace.material));
      }
      if (updatedCollision) {
        m_liquidEngine->visitLocation(pos);
        m_fallingBlocksAgent->visitLocation(pos);
        dirtyCollision(RectI::withSize(pos, { 1, 1 }));
      }
      if (updatedTile)
        queueTileUpdates(pos);
    }
    spaces.materials = std::move(passedSpaces);

    // add new roots and update known roots entry
    for (auto const& rootPos : newRoots) {
      if (auto tile = m_tileArray->modifyTile(rootPos + entity->tilePosition()))
        tile->rootSource = entity->tilePosition();
    }
    spaces.roots = std::move(newRoots);
  }

  // check whether we've broken any other nearby entities
  if (checkBreaks)
    checkEntityBreaks(entity->metaBoundBox().translated(entity->position()));
}

ConnectionId WorldServer::connection() const {
  return ServerConnectionId;
}

bool WorldServer::signalRegion(RectI const& region) {
  auto sectors = m_worldStorage->sectorsForRegion(region);
  if (m_generatingDungeon) {
    // When generating a dungeon, all sector activations should immediately
    // load whatever is available and make the sector active for writing, but
    // should trigger no generation (for world generation speed).
    for (auto const& sector : sectors)
      m_worldStorage->loadSector(sector);
  } else {
    for (auto const& sector : sectors)
      m_worldStorage->queueSectorActivation(sector);
  }
  for (auto const& sector : sectors) {
    if (!m_worldStorage->sectorActive(sector))
      return false;
  }
  return true;
}

void WorldServer::generateRegion(RectI const& region) {
  for (auto sector : m_worldStorage->sectorsForRegion(region))
    m_worldStorage->activateSector(sector);
}

bool WorldServer::regionActive(RectI const& region) {
  for (auto const& sector : m_worldStorage->sectorsForRegion(region)) {
    if (!m_worldStorage->sectorActive(sector))
      return false;
  }
  return true;
}

WorldServer::ScriptComponentPtr WorldServer::scriptContext(String const& contextName) {
  if (auto context = m_scriptContexts.ptr(contextName))
    return *context;
  else
    return nullptr;
}

RpcPromise<Vec2I> WorldServer::enqueuePlacement(List<BiomeItemDistribution> distributions, Maybe<DungeonId> id) {
  return m_worldStorage->enqueuePlacement(std::move(distributions), id);
}

ServerTile const& WorldServer::getServerTile(Vec2I const& position, bool withSignal) {
  if (withSignal)
    signalRegion(RectI::withSize(position, {1, 1}));

  return m_tileArray->tile(position);
}

ServerTile* WorldServer::modifyServerTile(Vec2I const& position, bool withSignal) {
  if (withSignal)
    signalRegion(RectI::withSize(position, {1, 1}));

  auto tile = m_tileArray->modifyTile(position);
  if (tile) {
    dirtyCollision(RectI::withSize(position, {1, 1}));
    m_liquidEngine->visitLocation(position);
    queueTileUpdates(position);
  }
  return tile;
}

EntityId WorldServer::loadUniqueEntity(String const& uniqueId) {
  return m_worldStorage->loadUniqueEntity(uniqueId);
}

WorldTemplatePtr WorldServer::worldTemplate() const {
  return m_worldTemplate;
}

SkyPtr WorldServer::sky() const {
  return m_sky;
}

void WorldServer::modifyLiquid(Vec2I const& pos, LiquidId liquid, float quantity, bool additive) {
  if (liquid == EmptyLiquidId)
    quantity = 0;

  if (ServerTile* tile = m_tileArray->modifyTile(pos)) {
    auto materialDatabase = Root::singleton().materialDatabase();
    if (tile->foreground == EmptyMaterialId || !isSolidColliding(materialDatabase->materialCollisionKind(tile->foreground))) {
      if (additive && liquid == tile->liquid.liquid)
        quantity += tile->liquid.level;

      setLiquid(pos, liquid, quantity, tile->liquid.pressure);
      m_liquidEngine->visitLocation(pos);
    }
  }
}

void WorldServer::setLiquid(Vec2I const& pos, LiquidId liquid, float level, float pressure) {
  if (ServerTile* tile = m_tileArray->modifyTile(pos)) {
    if (liquid == EmptyLiquidId)
      level = 0;

    if (auto netUpdate = tile->liquid.update(liquid, level, pressure)) {
      for (auto const& pair : m_clientInfo) {
        if (pair.second->activeSectors.contains(m_tileArray->sectorFor(pos)))
          pair.second->pendingLiquidUpdates.add(pos);
      }
    }
  }
}

List<ItemDescriptor> WorldServer::destroyBlock(TileLayer layer, Vec2I const& pos, bool genItems, bool destroyModFirst, bool updateNeighbors) {
  auto materialDatabase = Root::singleton().materialDatabase();

  auto* tile = m_tileArray->modifyTile(pos);
  if (!tile)
    return {};

  List<ItemDescriptor> drops;

  if (layer == TileLayer::Background) {
    if (isRealMod(tile->backgroundMod) && destroyModFirst
        && !materialDatabase->modBreaksWithTile(tile->backgroundMod)) {
      if (genItems) {
        if (auto drop = materialDatabase->modItemDrop(tile->backgroundMod))
          drops.append(drop);
      }
      tile->backgroundMod = NoModId;
    } else {
      if (genItems) {
        if (auto drop = materialDatabase->materialItemDrop(tile->background))
          drops.append(drop);
        if (auto drop = materialDatabase->modItemDrop(tile->backgroundMod))
          drops.append(drop);
      }
      tile->background = EmptyMaterialId;
      tile->backgroundColorVariant = DefaultMaterialColorVariant;
      tile->backgroundHueShift = 0;
      tile->backgroundMod = NoModId;
    }

    tile->backgroundDamage.reset();

  } else {
    if (isRealMod(tile->foregroundMod) && destroyModFirst
        && !materialDatabase->modBreaksWithTile(tile->foregroundMod)) {
      if (genItems) {
        if (auto drop = materialDatabase->modItemDrop(tile->foregroundMod))
          drops.append(drop);
      }
      tile->foregroundMod = NoModId;
    } else {
      if (genItems) {
        if (auto drop = materialDatabase->materialItemDrop(tile->foreground))
          drops.append(drop);
        if (auto drop = materialDatabase->modItemDrop(tile->foregroundMod))
          drops.append(drop);
      }
      tile->foreground = EmptyMaterialId;
      tile->foregroundColorVariant = DefaultMaterialColorVariant;
      tile->foregroundHueShift = 0;
      tile->foregroundMod = NoModId;
      tile->updateCollision(CollisionKind::None);
      dirtyCollision(RectI::withSize(pos, {1, 1}));
    }

    tile->foregroundDamage.reset();
  }

  if (tile->background == EmptyMaterialId && tile->foreground == EmptyMaterialId) {
    auto blockInfo = m_worldTemplate->blockInfo(pos[0], pos[1]);
    if (blockInfo.oceanLiquid != EmptyLiquidId && !blockInfo.encloseLiquids && pos[1] < blockInfo.oceanLiquidLevel)
      tile->liquid = LiquidStore::endless(blockInfo.oceanLiquid, blockInfo.oceanLiquidLevel - pos[1]);
  }

  tile->dungeonId = DestroyedBlockDungeonId;

  if (updateNeighbors) {
    checkEntityBreaks(RectF::withSize(Vec2F(pos), Vec2F(1, 1)));
    m_liquidEngine->visitLocation(pos);
    m_fallingBlocksAgent->visitLocation(pos);
  }
  queueTileUpdates(pos);
  queueTileDamageUpdates(pos, layer);

  return drops;
}

void WorldServer::queueUpdatePackets(ConnectionId clientId, bool sendRemoteUpdates, List<RectI> const& monitoringRegions) {
  auto const& clientInfo = m_clientInfo.get(clientId);
  clientInfo->outgoingPackets.append(make_shared<StepUpdatePacket>(m_currentTime));

  if (shouldRunThisStep("environmentUpdate")) {
    ByteArray skyDelta;
    tie(skyDelta, clientInfo->skyNetVersion) = m_sky->writeUpdate(clientInfo->skyNetVersion, clientInfo->clientState.netCompatibilityRules());

    ByteArray weatherDelta;
    tie(weatherDelta, clientInfo->weatherNetVersion) = m_weather.writeUpdate(clientInfo->weatherNetVersion, clientInfo->clientState.netCompatibilityRules());

    if (!skyDelta.empty() || !weatherDelta.empty())
      clientInfo->outgoingPackets.append(make_shared<EnvironmentUpdatePacket>(std::move(skyDelta), std::move(weatherDelta)));
  }

  for (auto sector : clientInfo->pendingSectors.values()) {
    if (!m_worldStorage->sectorActive(sector))
      continue;

    auto tileArrayUpdate = make_shared<TileArrayUpdatePacket>();
    auto sectorTiles = m_tileArray->sectorRegion(sector);
    tileArrayUpdate->min = sectorTiles.min();
    tileArrayUpdate->array.resize(Vec2S(sectorTiles.width(), sectorTiles.height()));
    for (int x = sectorTiles.xMin(); x < sectorTiles.xMax(); ++x) {
      for (int y = sectorTiles.yMin(); y < sectorTiles.yMax(); ++y)
        writeNetTile({x, y}, tileArrayUpdate->array(x - sectorTiles.xMin(), y - sectorTiles.yMin()));
    }

    clientInfo->outgoingPackets.append(tileArrayUpdate);
    clientInfo->pendingSectors.remove(sector);
  }

  for (auto pos : clientInfo->pendingTileUpdates) {
    auto tileUpdate = make_shared<TileUpdatePacket>();
    tileUpdate->position = pos;
    writeNetTile(pos, tileUpdate->tile);

    clientInfo->outgoingPackets.append(tileUpdate);
  }
  clientInfo->pendingTileUpdates.clear();

  for (auto pair : clientInfo->pendingTileDamageUpdates) {
    auto tile = m_tileArray->tile(pair.first);
    if (pair.second == TileLayer::Foreground)
      clientInfo->outgoingPackets.append(
          make_shared<TileDamageUpdatePacket>(pair.first, TileLayer::Foreground, tile.foregroundDamage));
    else
      clientInfo->outgoingPackets.append(
          make_shared<TileDamageUpdatePacket>(pair.first, TileLayer::Background, tile.backgroundDamage));
  }
  clientInfo->pendingTileDamageUpdates.clear();

  for (auto pos : clientInfo->pendingLiquidUpdates) {
    auto tile = m_tileArray->tile(pos);
    clientInfo->outgoingPackets.append(make_shared<TileLiquidUpdatePacket>(pos, tile.liquid.netUpdate()));
  }
  clientInfo->pendingLiquidUpdates.clear();

  // Lever #12b: collect monitored entities as raw Entity* (no shared_ptr atomic
  // refcount churn vs the old HashSet<EntityPtr>), deduped by sort + skip-equal.
  // Safe because nothing for the rest of this function adds or removes entities
  // (only serialization getters + keyed-map/packet mutations below), so the raw
  // pointers stay valid; m_entityMap owns the entities for the whole tick.
  List<Entity*> monitoredEntities;
  for (auto const& monitoredRegion : monitoringRegions)
    m_entityMap->forEachEntity(RectF(monitoredRegion), [&](EntityPtr const& entity) {
        monitoredEntities.append(entity.get());
      });
  monitoredEntities.sort();
  size_t uniqueCount = 0;
  for (size_t i = 0; i < monitoredEntities.size(); ++i) {
    if (uniqueCount == 0 || monitoredEntities[i] != monitoredEntities[uniqueCount - 1])
      monitoredEntities[uniqueCount++] = monitoredEntities[i];
  }
  monitoredEntities.resize(uniqueCount);

  auto entityFactory = Root::singleton().entityFactory();
  auto outOfMonitoredRegionsEntities = HashSet<EntityId>::from(clientInfo->clientSlavesNetVersion.keys());
  for (auto const& monitoredEntity : monitoredEntities)
    outOfMonitoredRegionsEntities.remove(monitoredEntity->entityId());
  for (auto entityId : outOfMonitoredRegionsEntities) {
    clientInfo->outgoingPackets.append(make_shared<EntityDestroyPacket>(entityId, ByteArray(), false));
    clientInfo->clientSlavesNetVersion.remove(entityId);
  }

  HashMap<ConnectionId, shared_ptr<EntityUpdateSetPacket>> updateSetPackets;
  if (sendRemoteUpdates || clientInfo->local)
    updateSetPackets.add(ServerConnectionId, make_shared<EntityUpdateSetPacket>(ServerConnectionId));
  for (auto const& p : m_clientInfo) {
    if (p.first != clientId && p.second->pendingForward)
      updateSetPackets.add(p.first, make_shared<EntityUpdateSetPacket>(p.first));
  }

  for (auto const& monitoredEntity : monitoredEntities) {
    EntityId entityId = monitoredEntity->entityId();
    ConnectionId connectionId = connectionForEntity(entityId);
    if (connectionId != clientId) {
      // Lever #4: run the deferred-store pump once per master entity per tick
      // (before its first writeNetState, across all clients/netRules buckets) so
      // the latestChange aggregate is authoritative for the early-out.
      if (NetElementEarlyOut::active() && m_netStorePumpedThisTick.add(entityId))
        monitoredEntity->netStorePump();
      auto netRules = clientInfo->clientState.netCompatibilityRules();
      if (auto version = clientInfo->clientSlavesNetVersion.ptr(entityId)) {
        if (auto updateSetPacket = updateSetPackets.value(connectionId)) {
          auto pair = make_pair(entityId, *version);
          auto& cache = m_netStateCache[netRules];
          auto i = cache.find(pair);
          if (i == cache.end())
            i = cache.insert(pair, monitoredEntity->writeNetState(*version, netRules)).first;
          const auto& netState = i->second;
          if (!netState.first.empty())
            updateSetPacket->deltas[entityId] = netState.first;
          *version = netState.second;
        }
      } else if (!monitoredEntity->masterOnly()) {
        // Client was unaware of this entity until now
        auto firstUpdate = monitoredEntity->writeNetState(0, netRules);
        clientInfo->clientSlavesNetVersion.add(entityId, firstUpdate.second);
        clientInfo->outgoingPackets.append(make_shared<EntityCreatePacket>(monitoredEntity->entityType(),
              entityFactory->netStoreEntity(m_entityMap->entity(entityId), netRules), std::move(firstUpdate.first), entityId));
      }
    }
  }

  for (auto& p : updateSetPackets)
    clientInfo->outgoingPackets.append(std::move(p.second));
}

void WorldServer::updateDamage(float dt) {
  m_damageManager->update(dt);

  // Do nothing with damage notifications at the moment.
  m_damageManager->pullPendingNotifications();

  for (auto const& remoteHitRequest : m_damageManager->pullRemoteHitRequests())
    m_clientInfo.get(remoteHitRequest.destinationConnection())
        ->outgoingPackets.append(make_shared<HitRequestPacket>(remoteHitRequest));

  for (auto const& remoteDamageRequest : m_damageManager->pullRemoteDamageRequests())
    m_clientInfo.get(remoteDamageRequest.destinationConnection())
        ->outgoingPackets.append(make_shared<DamageRequestPacket>(remoteDamageRequest));

  for (auto const& remoteDamageNotification : m_damageManager->pullRemoteDamageNotifications()) {
    for (auto const& pair : m_clientInfo) {
      if (pair.second->needsDamageNotification(remoteDamageNotification))
        pair.second->outgoingPackets.append(make_shared<DamageNotificationPacket>(remoteDamageNotification));
    }
  }
}

void WorldServer::sync() {
  writeMetadata();
  m_worldStorage->sync();
}

void WorldServer::unloadAll(bool force) {
  m_worldStorage->unloadAll(force);
}

WorldChunks WorldServer::readChunks() {
  writeMetadata();
  return m_worldStorage->readChunks();
}

void WorldServer::updateDamagedBlocks(float dt) {
  auto materialDatabase = Root::singleton().materialDatabase();

  for (auto pos : m_damagedBlocks.values()) {
    auto tile = m_tileArray->modifyTile(pos);
    if (!tile) {
      m_damagedBlocks.remove(pos);
      continue;
    }

    Vec2F dropPosition = centerOfTile(pos);
    if (tile->foregroundDamage.dead()) {
      bool harvested = tile->foregroundDamage.harvested();
      for (auto drop : destroyBlock(TileLayer::Foreground, pos, harvested, !tileDamageIsPenetrating(tile->foregroundDamage.damageType())))
        addEntity(ItemDrop::createRandomizedDrop(drop, dropPosition));

    } else if (tile->foregroundDamage.damaged()) {
      if (isRealMaterial(tile->foreground)) {
        if (isRealMod(tile->foregroundMod)) {
          if (tileDamageIsPenetrating(tile->foregroundDamage.damageType()))
            tile->foregroundDamage.recover(materialDatabase->materialDamageParameters(tile->foreground), dt);
          else if (materialDatabase->modBreaksWithTile(tile->foregroundMod))
            tile->foregroundDamage.recover(materialDatabase->modDamageParameters(tile->foregroundMod).sum(materialDatabase->materialDamageParameters(tile->foreground)), dt);
          else
            tile->foregroundDamage.recover(materialDatabase->modDamageParameters(tile->foregroundMod), dt);
        } else
          tile->foregroundDamage.recover(materialDatabase->materialDamageParameters(tile->foreground), dt);
      } else
        tile->foregroundDamage.reset();

      queueTileDamageUpdates(pos, TileLayer::Foreground);
    }

    if (tile->backgroundDamage.dead()) {
      bool harvested = tile->backgroundDamage.harvested();
      for (auto drop : destroyBlock(TileLayer::Background, pos, harvested, !tileDamageIsPenetrating(tile->backgroundDamage.damageType())))
        addEntity(ItemDrop::createRandomizedDrop(drop, dropPosition));

    } else if (tile->backgroundDamage.damaged()) {
      if (isRealMaterial(tile->background)) {
        if (isRealMod(tile->backgroundMod)) {
          if (tileDamageIsPenetrating(tile->backgroundDamage.damageType()))
            tile->backgroundDamage.recover(materialDatabase->materialDamageParameters(tile->background), dt);
          else if (materialDatabase->modBreaksWithTile(tile->backgroundMod))
            tile->backgroundDamage.recover(materialDatabase->modDamageParameters(tile->backgroundMod).sum(materialDatabase->materialDamageParameters(tile->background)), dt);
          else
            tile->backgroundDamage.recover(materialDatabase->modDamageParameters(tile->backgroundMod), dt);
        } else {
          tile->backgroundDamage.recover(materialDatabase->materialDamageParameters(tile->background), dt);
        }
      } else {
        tile->backgroundDamage.reset();
      }

      queueTileDamageUpdates(pos, TileLayer::Background);
    }

    if (tile->backgroundDamage.healthy() && tile->foregroundDamage.healthy())
      m_damagedBlocks.remove(pos);
  }
}

void WorldServer::checkEntityBreaks(RectF const& rect) {
  for (auto tileEntity : m_entityMap->query<TileEntity>(rect)) {
    // Dormancy wake (audit Rule 4): a neighbor tile changed; wake so an anchor
    // invalidation (m_broken -> shouldDestroy) is seen by the reap path.
    tileEntity->requestWake();
    tileEntity->checkBroken();
  }
}

void WorldServer::queueTileUpdates(Vec2I const& pos) {
  for (auto const& pair : m_clientInfo) {
    if (pair.second->activeSectors.contains(m_tileArray->sectorFor(pos)))
      pair.second->pendingTileUpdates.add(pos);
  }
}

void WorldServer::queueTileDamageUpdates(Vec2I const& pos, TileLayer layer) {
  for (auto const& pair : m_clientInfo) {
    if (pair.second->activeSectors.contains(m_tileArray->sectorFor(pos)))
      pair.second->pendingTileDamageUpdates.add({pos, layer});
  }
}

void WorldServer::writeNetTile(Vec2I const& pos, NetTile& netTile) const {
  auto const& tile = m_tileArray->tile(pos);
  netTile.foreground = tile.foreground;
  netTile.foregroundHueShift = tile.foregroundHueShift;
  netTile.foregroundColorVariant = tile.foregroundColorVariant;
  netTile.foregroundMod = tile.foregroundMod;
  netTile.foregroundModHueShift = tile.foregroundModHueShift;
  netTile.background = tile.background;
  netTile.backgroundHueShift = tile.backgroundHueShift;
  netTile.backgroundColorVariant = tile.backgroundColorVariant;
  netTile.backgroundMod = tile.backgroundMod;
  netTile.backgroundModHueShift = tile.backgroundModHueShift;
  netTile.liquid = tile.liquid.netUpdate();
  netTile.collision = tile.getCollision();
  netTile.blockBiomeIndex = tile.blockBiomeIndex;
  netTile.environmentBiomeIndex = tile.environmentBiomeIndex;
  netTile.dungeonId = tile.dungeonId;
}

void WorldServer::dirtyCollision(RectI const& region) {
  auto dirtyRegion = region.padded(CollisionGenerator::BlockInfluenceRadius);
  for (int x = dirtyRegion.xMin(); x < dirtyRegion.xMax(); ++x) {
    for (int y = dirtyRegion.yMin(); y < dirtyRegion.yMax(); ++y) {
      if (auto tile = m_tileArray->modifyTile({x, y}))
        tile->collisionCacheDirty = true;
    }
  }
}

void WorldServer::freshenCollision(RectI const& region) {
  // Lever L4: the pass-1 dirty scan only READS collisionCacheDirty, so use the
  // read-only, column-amortized tileEachColumns instead of a per-tile modifyTile
  // (which does a full pmod + sector-hashmap lookup PER tile, ~2.34% of the exploring
  // WST). tileEachColumns walks valid loaded columns over contiguous tile memory and
  // -- exactly like modifyTile's null guard -- SKIPS invalid/unloaded/out-of-y-range
  // positions (NOT the const tileEach, which substitutes the dirty-by-default
  // m_default for invalid positions and would balloon freshenRegion). So it visits the
  // SAME tile set; the dirty subset is identical and RectI::combine is order-
  // independent, so freshenRegion is byte-identical. Pass 2 below still mutates via
  // modifyTile.
  RectI freshenRegion = RectI::null();
  m_tileArray->tileEachColumns(region, [&freshenRegion](Vec2I const& pos, auto const* column, size_t columnSize) {
      for (size_t i = 0; i < columnSize; ++i) {
        if (column[i].collisionCacheDirty)
          freshenRegion.combine(RectI(pos[0], pos[1] + (int)i, pos[0] + 1, pos[1] + (int)i + 1));
      }
    });

  if (!freshenRegion.isNull()) {
    for (int x = freshenRegion.xMin(); x < freshenRegion.xMax(); ++x) {
      for (int y = freshenRegion.yMin(); y < freshenRegion.yMax(); ++y) {
        if (auto tile = m_tileArray->modifyTile({x, y})) {
          tile->collisionCacheDirty = false;
          tile->collisionCache.clear();
        }
      }
    }

    for (auto collisionBlock : m_collisionGenerator.getBlocks(freshenRegion)) {
      if (auto tile = m_tileArray->modifyTile(collisionBlock.space))
        tile->collisionCache.append(std::move(collisionBlock));
    }
  }
}

void WorldServer::removeEntity(EntityId entityId, bool andDie) {
  auto entity = m_entityMap->entity(entityId);
  if (!entity)
    return;

  if (auto* tileEntity = entity->asTileEntity())
    updateTileEntityTiles(tileEntity, true);

  if (andDie)
    entity->destroy(nullptr);

  // Lever #4: pump deferred stores so the final delta below isn't dropped by an
  // early-out (entity is processed once here then removed, so no dedup needed).
  if (NetElementEarlyOut::active())
    entity->netStorePump();

  for (auto const& pair : m_clientInfo) {
    auto& clientInfo = pair.second;
    if (auto version = clientInfo->clientSlavesNetVersion.maybeTake(entity->entityId())) {
      auto netRules = clientInfo->clientState.netCompatibilityRules();
      ByteArray finalDelta = entity->writeNetState(*version, netRules).first;
      clientInfo->outgoingPackets.append(make_shared<EntityDestroyPacket>(entity->entityId(), std::move(finalDelta), andDie));
    }
  }

  m_entityMap->removeEntity(entityId);
  entity->uninit();

  // Option A dormancy: drop the id from the awake-set (O(1)). Both structures are
  // inert unless EntityDormancy is active, so this is a no-op in the default-OFF
  // build (m_awakeEntities stays empty, the remove is a no-op).
  m_awakeEntities.remove(entityId);
  // NOTE: we deliberately do NOT scrub m_scheduledWakes here (it would be O(all
  // scheduled entries) per removal). A removed entity's stale scheduled-bucket entry
  // is filtered out at promotion time (see the tick loop) — entity() returns null for
  // it, so it never enters m_awakeEntities. Buckets self-clean when their step is taken.
}

float WorldServer::windLevel(Vec2F const& pos) const {
  return WorldImpl::windLevel(m_tileArray, pos, m_weather.wind());
}

float WorldServer::lightLevel(Vec2F const& pos) const {
  return WorldImpl::lightLevel(m_tileArray, m_entityMap, m_geometry, m_worldTemplate, m_sky, m_lightIntensityCalculator, pos);
}

void WorldServer::setDungeonBreathable(DungeonId dungeonId, Maybe<bool> breathable) {
  Maybe<bool> current = m_dungeonIdBreathable.maybe(dungeonId);
  if (breathable != current) {
    if (breathable)
      m_dungeonIdBreathable[dungeonId] = *breathable;
    else
      m_dungeonIdBreathable.remove(dungeonId);

    for (auto const& p : m_clientInfo)
      p.second->outgoingPackets.append(make_shared<SetDungeonBreathablePacket>(dungeonId, breathable));
  }
}



bool WorldServer::breathable(Vec2F const& pos) const {
  return WorldImpl::breathable(this, m_tileArray, m_dungeonIdBreathable, m_worldTemplate, pos);
}

float WorldServer::threatLevel() const {
  return m_worldTemplate->threatLevel();
}

StringList WorldServer::environmentStatusEffects(Vec2F const& pos) const {
  return m_worldTemplate->environmentStatusEffects(floor(pos[0]), floor(pos[1]));
}

StringList WorldServer::weatherStatusEffects(Vec2F const& pos) const {
  if (!m_weather.statusEffects().empty()) {
     if (exposedToWeather(pos))
      return m_weather.statusEffects();
  }

  return {};
}

bool WorldServer::exposedToWeather(Vec2F const& pos) const {
  if (!isUnderground(pos) && liquidLevel(Vec2I::floor(pos)).liquid == EmptyLiquidId) {
    auto assets = Root::singleton().assets();
    float weatherRayCheckDistance = assets->json("/weather.config:weatherRayCheckDistance").toFloat();
    float weatherRayCheckWindInfluence = assets->json("/weather.config:weatherRayCheckWindInfluence").toFloat();

    auto offset = Vec2F(-m_weather.wind() * weatherRayCheckWindInfluence, weatherRayCheckDistance).normalized() * weatherRayCheckDistance;

    return !lineCollision({pos, pos + offset});
  }

  return false;
}

bool WorldServer::isUnderground(Vec2F const& pos) const {
  return m_worldTemplate->undergroundLevel() >= pos[1];
}

bool WorldServer::disableDeathDrops() const {
  if (m_worldTemplate->worldParameters())
    return m_worldTemplate->worldParameters()->disableDeathDrops;
  return false;
}

List<PhysicsForceRegion> WorldServer::forceRegions() const {
  return m_forceRegions;
}

Json WorldServer::getProperty(String const& propertyName, Json const& def) const {
  return m_worldProperties.value(propertyName, def);
}

void WorldServer::setProperty(String const& propertyName, Json const& property) {
  // Kae: Properties set to null (nil from Lua) should be erased instead of lingering around
  auto entry = m_worldProperties.find(propertyName);
  bool missing = entry == m_worldProperties.end();
  if (missing ? !property.isNull() : property != entry->second) {
    if (missing) // property can't be null if we're doing this when missing is true
      m_worldProperties.emplace(propertyName, property);
    else if (property.isNull())
      m_worldProperties.erase(entry);
    else
      entry->second = property;
    for (auto const& pair : m_clientInfo)
      pair.second->outgoingPackets.append(make_shared<UpdateWorldPropertiesPacket>(JsonObject{ {propertyName, property} }));
  }
  auto listener = m_worldPropertyListeners.find(propertyName);
  if (listener != m_worldPropertyListeners.end())
    listener->second(property);
}

void WorldServer::timer(float delay, WorldAction worldAction) {
  m_timers.append({delay, worldAction});
}

void WorldServer::startFlyingSky(bool enterHyperspace, bool startInWarp, Json settings) {
  m_sky->startFlying(enterHyperspace, startInWarp, settings);
}

void WorldServer::stopFlyingSkyAt(SkyParameters const& destination) {
  m_sky->stopFlyingAt(destination);
  m_sky->setType(SkyType::Orbital);
}

void WorldServer::setOrbitalSky(SkyParameters const& destination) {
  m_sky->jumpTo(destination);
  m_sky->setType(SkyType::Orbital);
}

double WorldServer::epochTime() const {
  return m_sky->epochTime();
}

uint32_t WorldServer::day() const {
  return m_sky->day();
}

float WorldServer::dayLength() const {
  return m_sky->dayLength();
}

float WorldServer::timeOfDay() const {
  return m_sky->timeOfDay();
}

LuaRootPtr WorldServer::luaRoot() {
  return m_luaRoot;
}

RpcPromise<Vec2F> WorldServer::findUniqueEntity(String const& uniqueId) {
  if (auto pos = m_worldStorage->findUniqueEntity(uniqueId))
    return RpcPromise<Vec2F>::createFulfilled(*pos);
  else
    return RpcPromise<Vec2F>::createFailed("Unknown entity");
}

RpcPromise<Json> WorldServer::sendEntityMessage(Variant<EntityId, String> const& entityId, String const& message, JsonArray const& args) {
  EntityPtr entity;
  if (entityId.is<EntityId>())
    entity = m_entityMap->entity(entityId.get<EntityId>());
  else
    entity = m_entityMap->entity(loadUniqueEntity(entityId.get<String>()));

  if (!entity) {
    return RpcPromise<Json>::createFailed("Unknown entity");
  } else if (entity->isMaster()) {
    if (auto resp = entity->receiveMessage(ServerConnectionId, message, args))
      return RpcPromise<Json>::createFulfilled(resp.take());
    else
      return RpcPromise<Json>::createFailed("Message not handled by entity");
  } else {
    auto pair = RpcPromise<Json>::createPair();
    auto clientInfo = m_clientInfo.get(connectionForEntity(entity->entityId()));
    Uuid uuid;
    m_entityMessageResponses[uuid] = {clientInfo->clientId, pair.second};
    clientInfo->outgoingPackets.append(make_shared<EntityMessagePacket>(entity->entityId(), message, args, uuid));
    return pair.first;
  }
}

void WorldServer::setPlayerStart(Vec2F const& startPosition, bool respawnInWorld) {
  m_playerStart = startPosition;
  m_respawnInWorld = respawnInWorld;
  m_adjustPlayerStart = false;
  for (auto pair : m_clientInfo)
    pair.second->outgoingPackets.append(make_shared<SetPlayerStartPacket>(m_playerStart, m_respawnInWorld));
}

Vec2F WorldServer::findPlayerStart(Maybe<Vec2F> firstTry) {
  Vec2F spawnRectSize = jsonToVec2F(m_serverConfig.get("playerStartRegionSize"));
  auto maximumVerticalSearch = m_serverConfig.getInt("playerStartRegionMaximumVerticalSearch");
  auto maximumTries = m_serverConfig.getInt("playerStartRegionMaximumTries");

  static const Set<DungeonId> allowedSpawnDungeonIds = {NoDungeonId, SpawnDungeonId, ConstructionDungeonId, DestroyedBlockDungeonId};

  Vec2F pos;
  if (firstTry)
    pos = *firstTry;
  else
    pos = Vec2F(m_worldTemplate->findSensiblePlayerStart().value(Vec2I(0, m_worldTemplate->surfaceLevel())));

  CollisionSet collideWithAnything{CollisionKind::Null, CollisionKind::Block, CollisionKind::Dynamic, CollisionKind::Platform, CollisionKind::Slippery};
  for (int t = 0; t < maximumTries; ++t) {
    bool foundGround = false;
    // First go downward until we collide with terrain
    for (int i = 0; i < maximumVerticalSearch; ++i) {
      RectF spawnRect = RectF(pos[0] - spawnRectSize[0] / 2, pos[1], pos[0] + spawnRectSize[0] / 2, pos[1] + spawnRectSize[1]);
      generateRegion(RectI::integral(spawnRect));
      if (rectTileCollision(RectI::integral(spawnRect), collideWithAnything)) {
        foundGround = true;
        break;
      }
      --pos[1];
    }

    if (foundGround) {
      // Then go up until our spawn region is no longer in the terrain, but bail
      // out and try again if we can't signal the region or we are stuck in a
      // dungeon.
      for (int i = 0; i < maximumVerticalSearch; ++i) {
        if (m_tileArray->tile(Vec2I::floor(pos)).liquid.liquid != EmptyLiquidId)
          break;

        RectF spawnRect = RectF(pos[0] - spawnRectSize[0] / 2, pos[1], pos[0] + spawnRectSize[0] / 2, pos[1] + spawnRectSize[1]);

        generateRegion(RectI::integral(spawnRect));

        auto tileDungeonId = getServerTile(Vec2I::floor(pos)).dungeonId;

        if (!allowedSpawnDungeonIds.contains(tileDungeonId))
          break;

        if (!rectTileCollision(RectI::integral(spawnRect), collideWithAnything) && spawnRect.yMax() < m_geometry.height())
          return pos;

        ++pos[1];
      }
    }

    pos = Vec2F(m_worldTemplate->findSensiblePlayerStart().value(Vec2I(0, m_worldTemplate->surfaceLevel())));
  }

  return pos;
}

Vec2F WorldServer::findPlayerSpaceStart(float targetX) {
  Vec2F testRectSize = jsonToVec2F(m_serverConfig.get("playerSpaceStartRegionSize"));
  auto distanceIncrement = m_serverConfig.getFloat("playerSpaceStartDistanceIncrement");
  auto maximumTries = m_serverConfig.getInt("playerSpaceStartMaximumTries");

  Vec2F basePos = Vec2F(targetX, m_geometry.height() * 0.5);

  CollisionSet collideWithAnything{CollisionKind::Null, CollisionKind::Block, CollisionKind::Dynamic, CollisionKind::Platform, CollisionKind::Slippery};
  for (int t = 0; t < maximumTries; ++t) {
    Vec2F testPos = m_geometry.limit(basePos + Vec2F::withAngle(Random::randf() * 2 * Constants::pi, t * distanceIncrement));
    RectF testRect = RectF::withCenter(testPos, testRectSize);
    generateRegion(RectI::integral(testRect));
    if (!rectTileCollision(RectI::integral(testRect), collideWithAnything))
      return testPos;
  }

  return basePos;
}

void WorldServer::readMetadata() {
  auto dungeonDefinitions = Root::singleton().dungeonDefinitions();
  auto versioningDatabase = Root::singleton().versioningDatabase();

  auto metadata = versioningDatabase->loadVersionedJson(m_worldStorage->worldMetadata(), "WorldMetadata");

  m_playerStart = jsonToVec2F(metadata.get("playerStart"));
  m_respawnInWorld = metadata.getBool("respawnInWorld");
  m_adjustPlayerStart = metadata.getBool("adjustPlayerStart");
  m_worldTemplate = make_shared<WorldTemplate>(metadata.get("worldTemplate"));
  m_centralStructure = WorldStructure(metadata.get("centralStructure"));
  m_protectedDungeonIds = jsonToSet<StableHashSet<DungeonId>>(metadata.get("protectedDungeonIds"), mem_fn(&Json::toUInt));
  m_worldProperties = metadata.getObject("worldProperties");
  m_spawner.setActive(metadata.getBool("spawningEnabled"));

  m_dungeonIdGravity = transform<HashMap<DungeonId, float>>(metadata.getArray("dungeonIdGravity"), [](Json const& p) {
      return make_pair(p.getInt(0), p.getFloat(1));
    });

  m_dungeonIdBreathable = transform<HashMap<DungeonId, bool>>(metadata.getArray("dungeonIdBreathable"), [](Json const& p) {
      return make_pair(p.getInt(0), p.getBool(1));
    });
}

void WorldServer::writeMetadata() {
  auto versioningDatabase = Root::singleton().versioningDatabase();

  Json metadata = JsonObject{
    {"playerStart", jsonFromVec2F(m_playerStart)},
    {"respawnInWorld", m_respawnInWorld},
    {"adjustPlayerStart", m_adjustPlayerStart},
    {"worldTemplate", m_worldTemplate->store()},
    {"centralStructure", m_centralStructure.store()},
    {"protectedDungeonIds", jsonFromSet(m_protectedDungeonIds)},
    {"worldProperties", m_worldProperties},
    {"spawningEnabled", m_spawner.active()},
    {"dungeonIdGravity", m_dungeonIdGravity.pairs().transformed([](auto const& p) -> Json {
        return JsonArray{p.first, p.second};
      })},
    {"dungeonIdBreathable", m_dungeonIdBreathable.pairs().transformed([](auto const& p) -> Json {
        return JsonArray{p.first, p.second};
      })}
  };

  m_worldStorage->setWorldMetadata(versioningDatabase->makeCurrentVersionedJson("WorldMetadata", metadata));
}

bool WorldServer::isVisibleToPlayer(RectF const& region) const {
  for (auto const& p : m_clientInfo) {
    for (auto playerRegion : p.second->monitoringRegions(m_entityMap)) {
      if (m_geometry.rectIntersectsRect(RectF(playerRegion), region))
        return true;
    }
  }
  return false;
}

WorldServer::ClientInfo::ClientInfo(ConnectionId clientId, InterpolationTracker const trackerInit)
  : clientId(clientId), skyNetVersion(0), weatherNetVersion(0), pendingForward(false), started(false), local(false), admin(false), interpolationTracker(trackerInit) {}

List<RectI> WorldServer::ClientInfo::monitoringRegions(EntityMapPtr const& entityMap) const {
  return clientState.monitoringRegions([entityMap](EntityId entityId) -> Maybe<RectI> {
    if (auto entity = entityMap->entity(entityId))
      return RectI::integral(entity->metaBoundBox().translated(entity->position()));
    return {};
  });
}

bool WorldServer::ClientInfo::needsDamageNotification(RemoteDamageNotification const& rdn) const {
  if (clientId == connectionForEntity(rdn.sourceEntityId) || clientId == connectionForEntity(rdn.damageNotification.targetEntityId))
    return true;

  if (clientSlavesNetVersion.contains(rdn.damageNotification.targetEntityId))
    return true;

  if (clientState.window().contains(Vec2I::floor(rdn.damageNotification.position)))
    return true;

  return false;
}

InteractiveEntityPtr WorldServer::getInteractiveInRange(Vec2F const& targetPosition, Vec2F const& sourcePosition, float maxRange) const {
  return WorldImpl::getInteractiveInRange(m_geometry, m_entityMap, targetPosition, sourcePosition, maxRange);
}

bool WorldServer::canReachEntity(Vec2F const& position, float radius, EntityId targetEntity, bool preferInteractive) const {
  return WorldImpl::canReachEntity(m_geometry, m_tileArray, m_entityMap, position, radius, targetEntity, preferInteractive);
}

RpcPromise<InteractAction> WorldServer::interact(InteractRequest const& request) {
  if (auto entity = as<InteractiveEntity>(m_entityMap->entity(request.targetId)))
    return RpcPromise<InteractAction>::createFulfilled(entity->interact(request));
  else
    return RpcPromise<InteractAction>::createFulfilled(InteractAction());
}

void WorldServer::setupForceRegions() {
  m_forceRegions.clear();

  if (!worldTemplate() || !worldTemplate()->worldParameters())
    return;

  auto forceRegionType = worldTemplate()->worldParameters()->worldEdgeForceRegions;

  if (forceRegionType == WorldEdgeForceRegionType::None)
    return;

  bool addTopRegion = forceRegionType == WorldEdgeForceRegionType::Top || forceRegionType == WorldEdgeForceRegionType::TopAndBottom;
  bool addBottomRegion = forceRegionType == WorldEdgeForceRegionType::Bottom || forceRegionType == WorldEdgeForceRegionType::TopAndBottom;

  auto regionHeight = m_serverConfig.getFloat("worldEdgeForceRegionHeight");
  auto regionForce = m_serverConfig.getFloat("worldEdgeForceRegionForce");
  auto regionVelocity = m_serverConfig.getFloat("worldEdgeForceRegionVelocity");
  auto regionCategoryFilter = PhysicsCategoryFilter::whitelist({"player", "monster", "npc", "vehicle", "itemdrop"});
  auto worldSize = Vec2F(worldTemplate()->size());

  if (addTopRegion) {
    auto topForceRegion = GradientForceRegion();
    topForceRegion.region = PolyF({
        {0, worldSize[1] - regionHeight},
        {worldSize[0], worldSize[1] - regionHeight},
        (worldSize),
        {0, worldSize[1]}});
    topForceRegion.gradient = Line2F({0, worldSize[1]}, {0, worldSize[1] - regionHeight});
    topForceRegion.baseTargetVelocity = regionVelocity;
    topForceRegion.baseControlForce = regionForce;
    topForceRegion.categoryFilter = regionCategoryFilter;
    m_forceRegions.append(topForceRegion);
  }

  if (addBottomRegion) {
    auto bottomForceRegion = GradientForceRegion();
    bottomForceRegion.region = PolyF({
        {0, 0},
        {worldSize[0], 0},
        {worldSize[0], regionHeight},
        {0, regionHeight}});
    bottomForceRegion.gradient = Line2F({0, 0}, {0, regionHeight});
    bottomForceRegion.baseTargetVelocity = regionVelocity;
    bottomForceRegion.baseControlForce = regionForce;
    bottomForceRegion.categoryFilter = regionCategoryFilter;
    m_forceRegions.append(bottomForceRegion);
  }
}

void WorldServer::setTemplate(WorldTemplatePtr newTemplate) {
  m_worldTemplate = std::move(newTemplate);
  for (auto& client : clientIds()) {
    auto& info = m_clientInfo.get(client);
    bool local = info->local;
    bool isAdmin = info->admin;
    auto netRules = info->clientState.netCompatibilityRules();
    SpawnTarget spawnTarget;
    if (auto player = clientPlayer(client))
      spawnTarget = SpawnTargetPosition(player->position() + player->feetOffset());
    removeClient(client);
    addClient(client, spawnTarget, local, isAdmin, netRules);
  }
}

void WorldServer::wire(Vec2I const& outputPosition, size_t outputIndex, Vec2I const& inputPosition, size_t inputIndex) {
  WireConnection output = {outputPosition, outputIndex};
  WireConnection input = {inputPosition, inputIndex};
  for (auto source : atTile<WireEntity>(input.entityLocation)) {
    for (auto target : atTile<WireEntity>(output.entityLocation)) {
      source->addNodeConnection(WireNode{WireDirection::Input, input.nodeIndex}, output);
      target->addNodeConnection(WireNode{WireDirection::Output, output.nodeIndex}, input);
    }
  }
}

}
