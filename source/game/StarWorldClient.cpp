#include "StarWorldClient.hpp"
#include "StarGridScroll.hpp"
// #225 MEASUREMENT. Vec2I through one relaxed atomic word, so the packet thread can test a tile position
// against the lighting thread's calculation region without a lock. Measurement scaffolding: if the epoch
// ever really becomes regional, the region needs publishing with an ordering that a verdict can rely on.
namespace {
int64_t packVec2I(Star::Vec2I const& v) {
  return ((int64_t)(uint32_t)v[0] << 32) | (int64_t)(uint32_t)v[1];
}
Star::Vec2I unpackVec2I(int64_t p) {
  return Star::Vec2I((int32_t)(uint32_t)(p >> 32), (int32_t)(uint32_t)(p & 0xffffffffu));
}
}
#include "StarIterator.hpp"
#include "StarLogging.hpp"
#include "StarTelemetry.hpp"
#include "StarBiome.hpp"
#include "StarMaterialRenderProfile.hpp"
#include "StarLiquidTypes.hpp"
#include "StarDamageDatabase.hpp"
#include "StarParticleDatabase.hpp"
#include "StarParticleManager.hpp"
#include "StarWorldImpl.hpp"
#include "StarPlayer.hpp"
#include "StarPlayerLog.hpp"
#include "StarAggressiveEntity.hpp"
#include "StarPhysicsEntity.hpp"
#include "StarItemDrop.hpp"
#include "StarItemDatabase.hpp"
#include "StarObjectDatabase.hpp"
#include "StarObject.hpp"
#include "StarEntityFactory.hpp"
#include "StarWorldTemplate.hpp"
#include "StarStoredFunctions.hpp"
#include "StarInspectableEntity.hpp"
#include "StarCurve25519.hpp"

namespace Star {

// IEEE-754 float32 -> float16 (half) with round-to-nearest. Used to pre-convert the GPU-lighting
// emission grid on the lighting thread so the render thread uploads RGB16F (half the bytes). Lighting
// values are non-negative and moderate, so the simple range handling (flush tiny to 0, clamp big to
// inf) is sufficient; the spread pipeline already runs at 16F precision.
//
// BRANCHLESS on purpose. This runs calcCells*3 times per recompute (~107,520 at the measured 35,840-cell
// calculation region). The two range tests become select arithmetic and the round-up becomes an
// unconditional add. Every result is bit-identical to the branching version -- this is a byte-identical
// change, verified exhaustively over all 2^32 float bit patterns, NOT a semantics change.
//
// What this actually bought, measured rather than assumed: clang -O3 (baseline x86-64, no -march) emits a
// loop body with ZERO conditional branches where the branching version emitted three; the range tests are
// now cmov and the round-up is bt/adc. It did NOT auto-vectorise -- the loop is still scalar, and is a few
// instructions LONGER per element. The win is branch-misprediction removal, not width: the emission grid is
// mostly dark cells (exp <= 0) speckled with lit ones, so the old range branches mispredicted at every lit
// region boundary. lighting.cpu.convert.us went 84.9 -> 64.0 us/recompute. Do not "restore" the branches on
// the theory that they predict well; they do not on this data.
//
// Deliberately NOT F16C (_mm_cvtps_ph): it rounds half-to-EVEN and emits proper subnormals, while this
// rounds half AWAY FROM ZERO and flushes subnormals to signed zero. Measured against a true IEEE half
// conversion, the two disagree on 1 in ~16,384 inputs across the non-negative normal-half range this grid
// actually occupies, and on 1 in ~23 across all finite floats (the subnormal-flush and overflow-clamp
// regions). So it is an output change requiring a quality argument rather than byte-identity. It is also
// unreachable without -march/-mf16c plus runtime dispatch plus an MSVC path, and this tree has no SIMD and
// has already taken one MSVC portability incident (__builtin_clzll). Tracked separately.
static uint16_t floatToHalf(float f) {
  uint32_t x;
  memcpy(&x, &f, sizeof(x));
  uint32_t sign = (x >> 16) & 0x8000u;
  int32_t exp = (int32_t)((x >> 23) & 0xffu) - 127 + 15;
  uint32_t mant = x & 0x7fffffu;

  // Normal-range result, computed unconditionally. The round-up is the dropped bits' MSB, ADDED rather
  // than branched on; a mantissa carry propagates into the exponent field exactly as `++h` did, because
  // the fields are contiguous.
  uint32_t normal = sign | ((uint32_t)exp << 10) | (mant >> 13);
  normal += (mant >> 12) & 1u;

  // Select without branching: underflow (exp <= 0) -> signed zero; overflow (exp >= 31) -> signed inf.
  // The two conditions are mutually exclusive, so the three-way blend needs no priority.
  uint32_t underflow = (uint32_t)(int32_t)(-(exp <= 0));
  uint32_t overflow = (uint32_t)(int32_t)(-(exp >= 31));
  uint32_t result = (normal & ~(underflow | overflow))
                  | (sign & underflow)
                  | ((sign | 0x7c00u) & overflow);
  return (uint16_t)result;
}

const std::string SECRET_BROADCAST_PUBLIC_KEY = "SecretBroadcastPublicKey";
const std::string SECRET_BROADCAST_PREFIX = "\0Broadcast\0"s;

const float WorldClient::DropDist = 6.0f;
WorldClient::WorldClient(PlayerPtr mainPlayer, LuaRootPtr luaRoot) {
  auto& root = Root::singleton();
  auto assets = root.assets();

  m_clientConfig = assets->json("/client.config");

  // lightingCalc caches the composed calculator parameters instead of re-reading /lighting.config every
  // recompute; this is how an asset reload reaches that cache. Root holds it weakly, so it dies with us.
  m_lightingParamsReloadTracker = make_shared<TrackerListener>();
  root.registerReloadListener(m_lightingParamsReloadTracker);

  m_currentStep = 0;
  m_currentTime = 0;
  m_fullBright = false;
  m_asyncLighting = false;
  m_worldDimTimer = GameTimer(m_clientConfig.getFloat("worldDimTime"));
  m_worldDimTimer.setDone();
  m_worldDimLevel = 0.0f;

  m_parallaxFadeTimer = GameTimer(m_clientConfig.getFloat("parallaxFadeTime"));
  m_parallaxFadeTimer.setDone();

  m_collisionDebug = false;
  m_inWorld = false;

  m_luaRoot = luaRoot;

  m_mainPlayer = mainPlayer;

  centerClientWindowOnPlayer(Vec2U(100, 100));

  m_collisionGenerator.init([this](int x, int y) {
    if (!m_predictedTiles.empty()) {
      if (auto p = m_predictedTiles.ptr({x, y})) {
        if (p->collision)
          return *p->collision;
      }
    }
    return m_tileArray->tile({x, y}).collision;
  });

  m_modifiedTilePredictionTimeout = (int)round(m_clientConfig.getFloat("modifiedTilePredictionTimeout") / GlobalTimestep);

  m_latency = 0.0;

  m_blockDamageParticle = Particle(m_clientConfig.getObject("blockDamageParticle"));
  m_blockDamageParticleVariance = Particle(m_clientConfig.getObject("blockDamageParticleVariance"));
  m_blockDamageParticleProbability = m_clientConfig.getFloat("blockDamageParticleProbability");

  m_blockDingParticle = Particle(m_clientConfig.getObject("blockDingParticle"));
  m_blockDingParticleVariance = Particle(m_clientConfig.getObject("blockDingParticleVariance"));
  m_blockDingParticleProbability = m_clientConfig.getFloat("blockDingParticleProbability");

  m_damageNotificationBatchDuration = m_clientConfig.getFloat("damageNotificationBatchDuration");

  m_ambientSounds.setTrackFadeInTime(assets->json("/interface.config:ambientTrackFadeInTime").toFloat());
  m_ambientSounds.setTrackSwitchGrace(assets->json("/interface.config:ambientTrackSwitchGrace").toFloat());

  m_musicTrack.setTrackSwitchGrace(assets->json("/interface.config:musicTrackSwitchGrace").toFloat());
  m_musicTrack.setTrackFadeInTime(assets->json("/interface.config:musicTrackFadeInTime").toFloat());

  m_altMusicTrack.setTrackFadeInTime(assets->json("/interface.config:musicTrackFadeInTime").toFloat());
  m_altMusicTrack.setTrackSwitchGrace(assets->json("/interface.config:musicTrackFadeInTime").toFloat());
  m_altMusicTrack.setVolume(0, 0, 0);
  m_altMusicActive = false;

  m_stopLightingThread = false;
  m_pendingLightReady = false;

  clearWorld();
}

WorldClient::~WorldClient() {
  if (m_lightingThread) {
    m_stopLightingThread = true;
    {
      MutexLocker locker(m_lightingMutex);
      m_lightingCond.broadcast();
    }

    m_lightingThread.finish();
  }
  clearWorld();
}

bool WorldClient::inWorld() const {
  return m_inWorld;
}

bool WorldClient::inSpace() const {
  if (!m_sky)
    return false;
  return m_sky->inSpace();
}

bool WorldClient::flying() const {
  if (!m_sky)
    return false;
  return m_sky->flying();
}

bool WorldClient::mainPlayerDead() const {
  if (inWorld())
    return !m_entityMap->get<Player>(m_mainPlayer->entityId());
  else
    return false;
}

void WorldClient::reviveMainPlayer() {
  if (inWorld() && mainPlayerDead()) {
    m_mainPlayer->revive(m_playerStart);
    m_mainPlayer->init(this, m_entityMap->reserveEntityId(), EntityMode::Master);
    m_entityMap->addEntity(m_mainPlayer);
  }
}

bool WorldClient::respawnInWorld() const {
  return m_respawnInWorld;
}

void WorldClient::setRespawnInWorld(bool respawnInWorld) {
  m_respawnInWorld = respawnInWorld;
}

int64_t WorldClient::latency() const {
  return m_latency;
}

void WorldClient::resendEntity(EntityId entityId) {
  auto entity = m_entityMap->entity(entityId);
  if (!entity || !entity->isMaster() || !m_masterEntitiesNetVersion.contains(entity->entityId()))
    return;

  auto fromVersion = m_masterEntitiesNetVersion.take(entity->entityId());
  auto netRules = m_clientState.netCompatibilityRules();
  // Lever #4: pump deferred stores so this final delta isn't dropped by an
  // early-out (mirrors the server-side WorldServer::removeEntity). The client
  // periodic-update loop pumps too (see below); this resend path is the same
  // master-entity delta write and was the second un-pumped client call site.
  if (NetElementEarlyOut::active())
    entity->netStorePump();
  ByteArray finalNetState = entity->writeNetState(fromVersion, netRules).first;
  m_outgoingPackets.append(make_shared<EntityDestroyPacket>(entity->entityId(), std::move(finalNetState), false));
  notifyEntityCreate(entity);
}

void WorldClient::removeEntity(EntityId entityId, bool andDie) {
  auto entity = m_entityMap->entity(entityId);
  if (!entity)
    return;

  if (andDie) {
    ClientRenderCallback renderCallback;
    entity->destroy(&renderCallback);

    const List<Directives>* directives = nullptr;
    if (auto& worldTemplate = m_worldTemplate) {
      if (const auto& parameters = worldTemplate->worldParameters())
        if (auto& globalDirectives = m_worldTemplate->worldParameters()->globalDirectives)
          directives = &globalDirectives.get();
    }
    if (directives) {
      int directiveIndex = unsigned(entity->entityId()) % directives->size();
      for (auto& p : renderCallback.particles)
        p.directives.append(directives->get(directiveIndex));
    }

    m_particles->addParticles(std::move(renderCallback.particles));
    m_samples.appendAll(std::move(renderCallback.audios));
  }

  if (auto version = m_masterEntitiesNetVersion.maybeTake(entity->entityId())) {
    auto netRules = m_clientState.netCompatibilityRules();
    // Lever #4: pump deferred stores so the final destroy delta isn't dropped by
    // an early-out (mirrors WorldServer::removeEntity:2268-2271). This was the
    // call site that produced the validate-live MISMATCH on player teardown at exit.
    if (NetElementEarlyOut::active())
      entity->netStorePump();
    ByteArray finalNetState = entity->writeNetState(*version, netRules).first;
    m_outgoingPackets.append(make_shared<EntityDestroyPacket>(entity->entityId(), std::move(finalNetState), andDie));
  }

  m_entityMap->removeEntity(entityId);
  entity->uninit();
}

WorldTemplateConstPtr WorldClient::currentTemplate() const {
  return m_worldTemplate;
}

void WorldClient::setTemplate(Json newTemplate) {
  m_outgoingPackets.push_back(make_shared<UpdateWorldTemplatePacket>(newTemplate));
}

SkyConstPtr WorldClient::currentSky() const {
  return m_sky;
}

void WorldClient::pinSkyEpochTime(double epochTime) {
  if (m_sky)
    m_sky->setEpochTime(epochTime);
}

void WorldClient::timer(float delay, WorldAction worldAction) {
  if (!inWorld())
    return;

  m_timers.append({delay, worldAction});
}

EntityPtr WorldClient::closestEntity(Vec2F const& center, float radius, EntityFilter selector) const {
  if (!inWorld())
    return {};

  return m_entityMap->closestEntity(center, radius, selector);
}

void WorldClient::forAllEntities(EntityCallback callback) const {
  m_entityMap->forAllEntities(callback);
}

void WorldClient::forEachEntity(RectF const& boundBox, EntityCallback const& callback) const {
  if (!inWorld())
    return;
  m_entityMap->forEachEntity(boundBox, callback);
}

void WorldClient::forEachEntityLine(Vec2F const& begin, Vec2F const& end, EntityCallback const& callback) const {
  if (!inWorld())
    return;
  m_entityMap->forEachEntityLine(begin, end, callback);
}

void WorldClient::forEachEntityAtTile(Vec2I const& pos, EntityCallbackOf<TileEntity> const& callback) const {
  if (!inWorld())
    return;
  m_entityMap->forEachEntityAtTile(pos, callback);
}

EntityPtr WorldClient::findEntity(RectF const& boundBox, EntityFilter const& entityFilter) const {
  if (!inWorld())
    return {};
  return m_entityMap->findEntity(boundBox, entityFilter);
}

EntityPtr WorldClient::findEntityLine(Vec2F const& begin, Vec2F const& end, EntityFilter const& entityFilter) const {
  if (!inWorld())
    return {};
  return m_entityMap->findEntityLine(begin, end, entityFilter);
}

EntityPtr WorldClient::findEntityAtTile(Vec2I const& pos, EntityFilterOf<TileEntity> const& entityFilter) const {
  if (!inWorld())
    return {};
  return m_entityMap->findEntityAtTile(pos, entityFilter);
}

bool WorldClient::tileIsOccupied(Vec2I const& pos, TileLayer layer, bool includeEphemeral, bool checkCollision) const {
  if (!inWorld())
    return false;
  return WorldImpl::tileIsOccupied(m_tileArray, m_entityMap, pos, layer, includeEphemeral, checkCollision);
}

CollisionKind WorldClient::tileCollisionKind(Vec2I const& pos) const {
  if (!inWorld())
    return CollisionKind::Null;
  return WorldImpl::tileCollisionKind(m_tileArray, m_entityMap, pos);
}

void WorldClient::forEachCollisionBlock(RectI const& region, function<void(CollisionBlock const&)> const& iterator) const {
  if (!inWorld())
    return;
  const_cast<WorldClient*>(this)->freshenCollision(region);
  WorldImpl::forEachCollisionBlock(m_tileArray, region, [&iterator](Vec2I const& pos, CollisionBlock const* block) {
      if (block) iterator(*block);
      else iterator(CollisionBlock::nullBlock(pos));
    });
}

void WorldClient::getCollisionBlocks(RectI const& region, List<CollisionBlockRef>& output) const {
  if (!inWorld())
    return;
  const_cast<WorldClient*>(this)->freshenCollision(region);
  WorldImpl::forEachCollisionBlock(m_tileArray, region, [&output](Vec2I const& pos, CollisionBlock const* block) {
      output.append(CollisionBlockRef{pos, block});
    });
}

bool WorldClient::isTileConnectable(Vec2I const& pos, TileLayer layer, bool tilesOnly) const {
  if (!inWorld())
    return false;

  return m_tileArray->tile(pos).isConnectable(layer, tilesOnly);
}

bool WorldClient::pointTileCollision(Vec2F const& point, CollisionSet const& collisionSet) const {
  if (!inWorld())
    return false;

  return m_tileArray->tile(Vec2I(point.floor())).isColliding(collisionSet);
}

bool WorldClient::lineTileCollision(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet) const {
  if (!inWorld())
    return false;

  return WorldImpl::lineTileCollision(m_geometry, m_tileArray, begin, end, collisionSet);
}

Maybe<pair<Vec2F, Vec2I>> WorldClient::lineTileCollisionPoint(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet) const {
  if (!inWorld())
    return {};

  return WorldImpl::lineTileCollisionPoint(m_geometry, m_tileArray, begin, end, collisionSet);
}

List<Vec2I> WorldClient::collidingTilesAlongLine(
    Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet, int maxSize, bool includeEdges) const {
  if (!inWorld())
    return {};

  return WorldImpl::collidingTilesAlongLine(m_geometry, m_tileArray, begin, end, collisionSet, maxSize, includeEdges);
}

bool WorldClient::rectTileCollision(RectI const& region, CollisionSet const& collisionSet) const {
  if (!inWorld())
    return false;

  return WorldImpl::rectTileCollision(m_tileArray, region, collisionSet);
}

LiquidLevel WorldClient::liquidLevel(Vec2I const& pos) const {
  if (!inWorld())
    return {};
  return m_tileArray->tile(pos).liquid;
}

LiquidLevel WorldClient::liquidLevel(RectF const& region) const {
  if (!inWorld())
    return {};
  return WorldImpl::liquidLevel(m_tileArray, region);
}

TileModificationList WorldClient::validTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) const {
  if (!inWorld())
    return {};

  return WorldImpl::splitTileModifications(m_entityMap, modificationList, allowEntityOverlap, m_tileGetterFunction, [this](Vec2I pos, TileModification) {
      return !isTileProtected(pos);
    }).first;
}

TileModificationList WorldClient::applyTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) {
  if (!inWorld())
    return {};
  
  // thanks to new prediction: do each one by one so that previous modifications affect placeability
  
  TileModificationList success, failures, temp;
  TileModificationList const* list = &modificationList;

  while (true) {
    bool yay = false;
    for (size_t i = 0; i != list->size(); ++i) {
      auto& pair = list->at(i);
      if (!isTileProtected(pair.first)) {
        auto result = WorldImpl::validateTileModification(m_entityMap, pair.first, pair.second, allowEntityOverlap, m_tileGetterFunction);

        if (result.first) {
          informTilePrediction(pair.first, pair.second);
          success.append(pair);
          yay = true;
          continue;
        }
      }
      failures.append(pair);
    }
    if (yay) {
      list = &(temp = std::move(failures));
      failures = {};
      continue;
    }
    else break;
  }

  if (!success.empty())
    m_outgoingPackets.append(make_shared<ModifyTileListPacket>(std::move(success), true));

  return failures;
}

TileModificationList WorldClient::replaceTiles(TileModificationList const& modificationList, TileDamage const& tileDamage, bool applyDamage) {
  if (!inWorld())
    return {};
  
  // Tell client it can't send a replace packet
  auto netRules = m_clientState.netCompatibilityRules();
  if (netRules.isLegacy() || netRules.version() <= 3)
    return modificationList;
  
  TileModificationList success, failures;
  for (auto pair : modificationList) {
    if (!isTileProtected(pair.first) && WorldImpl::validateTileReplacement(pair.second))
      success.append(pair);
    else
      failures.append(pair);
  }

  m_outgoingPackets.append(make_shared<ReplaceTileListPacket>(std::move(success), tileDamage, applyDamage));

  return failures;
}

bool WorldClient::damageWouldDestroy(Vec2I const& pos, TileLayer layer, TileDamage const& tileDamage) const {
  if (!inWorld())
    return false;
  return WorldImpl::damageWouldDestroy(m_tileArray, pos, layer, tileDamage);
}

float WorldClient::gravity(Vec2F const& pos) const {
  if (!inWorld())
    return 0.0f;

  if (m_overrideGravity)
    return *m_overrideGravity;

  auto dungeonId = m_tileArray->tile(Vec2I::round(pos)).dungeonId;
  return m_dungeonIdGravity.maybe(dungeonId).value(currentTemplate()->gravity());
}

float WorldClient::windLevel(Vec2F const& pos) const {
  if (!inWorld())
    return 0.0f;

  return WorldImpl::windLevel(m_tileArray, pos, m_weather.wind());
}

void WorldClient::setClientWindow(RectI window) {
  m_clientState.setWindow(window);
}

void WorldClient::centerClientWindowOnPlayer(Vec2U const& windowSize) {
  setClientWindow(RectI::withCenter(Vec2I::floor(m_mainPlayer->position()), Vec2I(windowSize)));
}

void WorldClient::centerClientWindowOnPlayer() {
  centerClientWindowOnPlayer(Vec2U(clientWindow().size()));
}

RectI WorldClient::clientWindow() const {
  return m_clientState.window();
}

WorldClientState& WorldClient::clientState() {
  return m_clientState;
}

void WorldClient::render(WorldRenderData& renderData, unsigned bufferTiles) {
  if (!m_lightingThread && m_asyncLighting)
    m_lightingThread = Thread::invoke("WorldClient::lightingMain", mem_fn(&WorldClient::lightingMain), this);

  renderData.clear();
  if (!inWorld())
    return;

  // If we're dimming the world, then that takes priority
  m_worldDimTimer.tick();
  float dimRatio = m_worldDimTimer.percent();

  // Spends 80% of the time at pitch black with 10% ramp up and down

  m_worldDimColor = {}; // always reset this to prevent persistent dimming from other sources
  if (dimRatio) {
    if (dimRatio <= 0.1f)
      m_worldDimLevel = dimRatio / 0.1f;
    else if (dimRatio >= 0.9f)
      m_worldDimLevel = (1 - dimRatio) / (1 - 0.9f);
    else
      m_worldDimLevel = 1.0f;
  }

  List<LightSource> renderLightSources;
  m_previewTiles.clear();

  renderData.geometry = m_geometry;

  ClientRenderCallback lightingRenderCallback;
  m_entityMap->forAllEntities([&](EntityPtr const& entity) {
    if (m_startupHiddenEntities.contains(entity->entityId()))
      return;

    entity->renderLightSources(&lightingRenderCallback);
  });

  renderLightSources = std::move(lightingRenderCallback.lightSources);

  RectI window = m_clientState.window();
  RectI tileRange = window.padded(bufferTiles);
  renderData.tileMinPosition = tileRange.min();

  if (!m_fullBright) {
    {
      MutexLocker m_prepLocker(m_lightMapPrepMutex);
      m_pendingLights = std::move(renderLightSources);
      m_pendingParticleLights = m_particles->lightSources();
      // Stable lighting grid (#127): round the light-query size up to a bucket so the calc region --
      // and everything slaved to it (emission/obstacle grids, the lighting ping-pong FBOs, the upscale
      // FBO) -- holds a constant size across camera scroll instead of breathing +-1 tile. The breathe
      // made setRenderTarget re-spec the FBO textures (a fresh driver buffer object each time) at the
      // lighting cadence -- measured ~60% of the kernel texture-upload cluster. Bucket 1 = exact size
      // (kill-switch). Min corner is untouched: it scrolls with the camera, which the lighting gather
      // (and its A2 scroll-shift cache) already handles. Default 32, NOT 8: the window's real variance
      // is +-2+ tiles, which crossed the 112 boundary at bucket 8 (bucketed size flipped 112<->120,
      // re-spec churn persisted); 32 was measured to fully absorb the swing (zero re-specs in combat).
      RectI lightWindow = window.padded(1);
      unsigned gridBucket = (unsigned)Root::singleton().configuration()->get("lightingGridSizeBucket", 32).toUInt();
      if (gridBucket > 1) {
        Vec2I bucketed((lightWindow.width() + gridBucket - 1) / gridBucket * gridBucket,
            (lightWindow.height() + gridBucket - 1) / gridBucket * gridBucket);
        lightWindow = RectI::withSize(lightWindow.min(), bucketed);
      }
      m_pendingLightRange = lightWindow;
      m_pendingLightReady = true;
    } //Kae: Padded by one to fix light spread issues at the edges of the frame.

    if (m_asyncLighting)
      m_lightingCond.signal();
    else
      lightingCalc();
  }

  float pulseAmount = Root::singleton().assets()->json("/highlights.config:interactivePulseAmount").toFloat();
  float pulseRate = Root::singleton().assets()->json("/highlights.config:interactivePulseRate").toFloat();
  float pulseLevel = 1 - pulseAmount * 0.5 * (sin(2 * Constants::pi * pulseRate * Time::monotonicMilliseconds() / 1000.0) + 1);

  bool inspecting = m_mainPlayer->inspecting();
  float inspectionFlickerMultiplier = Random::randf(1 - Root::singleton().assets()->json("/highlights.config:inspectionFlickerAmount").toFloat(), 1);

  EntityId playerAimInteractive = NullEntityId;
  if (Root::singleton().configuration()->get("interactiveHighlight").toBool()) {
    if (auto entity = m_mainPlayer->bestInteractionEntity(false))
      playerAimInteractive = entity->entityId();
  }

  const List<Directives>* directives = nullptr;
  if (auto& worldTemplate = m_worldTemplate) {
    if (const auto& parameters = worldTemplate->worldParameters())
      if (auto& globalDirectives = parameters->globalDirectives)
        directives = &globalDirectives.get();
  }
  m_entityMap->forAllEntities([&](EntityPtr const& entity) {
      if (m_startupHiddenEntities.contains(entity->entityId()))
        return;

      ClientRenderCallback renderCallback(!m_headless);

      try { entity->render(&renderCallback); }
      catch (StarException const& e) {
        if (entity->isMaster()) // this is YOUR problem!!
          throw e; 
        else { // this is THEIR problem!!
          auto issue = printException(e, true);
          auto hash = hashOf(issue);
          if (!m_entityExceptionsLogged.contains(hash))
            m_entityExceptionsLogged.insert(hash);
          else
            issue = e.what();

          Logger::error("WorldClient: Exception caught in {}::render ({}): {}", EntityTypeNames.getRight(entity->entityType()), entity->entityId(), issue);
          auto toolUser = as<ToolUserEntity>(entity);
          String image = toolUser ? strf("/rendering/sprites/error_{}.png", DirectionNames.getRight(toolUser->facingDirection())) : "/rendering/sprites/error.png";
          Color color = Color::rgbf(0.8f + (float)sin(m_currentTime * Constants::pi * 2.0) * 0.2f, 0.0f, 0.0f);
          auto drawable = Drawable::makeImage(image, 1.0f / TilePixels, true, entity->position(), color);
          drawable.fullbright = true;
          renderCallback.addDrawable(std::move(drawable), RenderLayerMiddleParticle);
        }
      }
      

      // VIEW ASSEMBLY, skipped wholesale when headless (#199). With the view sinks discarding,
      // renderCallback.drawables is empty and this block would append an empty EntityDrawables per entity
      // and still run the interactive/inspection highlight queries -- work whose only consumer is a
      // renderer. The sink gate makes it EMPTY; this gate makes it ABSENT.
      if (!m_headless) {
        EntityDrawables ed;
        for (auto& p : renderCallback.drawables) {
          if (directives) {
            int directiveIndex = unsigned(entity->entityId()) % directives->size();
            for (auto& d : p.second) {
              if (d.isImage())
                d.imagePart().addDirectives(directives->at(directiveIndex), true);
            }
          }
          ed.layers[p.first] = std::move(p.second);
        }

        if (m_interactiveHighlightMode || (!inspecting && entity->entityId() == playerAimInteractive)) {
          if (auto interactive = entityCast<InteractiveEntity>(entity)) {
            if (interactive->isInteractive()) {
              ed.highlightEffect.type = EntityHighlightEffectType::Interactive;
              ed.highlightEffect.level = pulseLevel;
            }
          }
        } else if (inspecting) {
          if (auto inspectable = entityCast<InspectableEntity>(entity)) {
            ed.highlightEffect = m_mainPlayer->inspectionHighlight(inspectable);
            ed.highlightEffect.level *= inspectionFlickerMultiplier;
          }
        }
        renderData.entityDrawables.append(std::move(ed));
      }

      if (directives) {
        int directiveIndex = unsigned(entity->entityId()) % directives->size();
        for (auto& p : renderCallback.particles)
          p.directives.append(directives->get(directiveIndex));
      }
      
      m_particles->addParticles(std::move(renderCallback.particles));
      m_samples.appendAll(std::move(renderCallback.audios));
      m_previewTiles.appendAll(std::move(renderCallback.previewTiles));
      renderData.overheadBars.appendAll(std::move(renderCallback.overheadBars));

    }, [](EntityPtr const& a, EntityPtr const& b) {
      return a->entityId() < b->entityId();
    });

  m_tileArray->tileEachTo(renderData.tiles, tileRange, [&](RenderTile& renderTile, Vec2I const&, ClientTile const& clientTile) {
      renderTile.foreground = clientTile.foreground;
      renderTile.foregroundMod = clientTile.foregroundMod;

      renderTile.background = clientTile.background;
      renderTile.backgroundMod = clientTile.backgroundMod;

      renderTile.foregroundHueShift = clientTile.foregroundHueShift;
      renderTile.foregroundModHueShift = clientTile.foregroundModHueShift;
      renderTile.foregroundColorVariant = clientTile.foregroundColorVariant;
      renderTile.foregroundDamageType = clientTile.foregroundDamage.damageType();
      renderTile.foregroundDamageLevel = floatToByte(clientTile.foregroundDamage.damageEffectPercentage());

      renderTile.backgroundHueShift = clientTile.backgroundHueShift;
      renderTile.backgroundModHueShift = clientTile.backgroundModHueShift;
      renderTile.backgroundColorVariant = clientTile.backgroundColorVariant;
      renderTile.backgroundDamageType = clientTile.backgroundDamage.damageType();
      renderTile.backgroundDamageLevel = floatToByte(clientTile.backgroundDamage.damageEffectPercentage());

      renderTile.liquidId = clientTile.liquid.liquid;
      renderTile.liquidLevel = floatToByte(clientTile.liquid.level);
    });

  for (auto& pair : m_predictedTiles) {
    Vec2I tileArrayPos = m_geometry.diff(pair.first, renderData.tileMinPosition);
    if (tileArrayPos[0] >= 0 && tileArrayPos[0] < (int)renderData.tiles.size(0) && tileArrayPos[1] >= 0 && tileArrayPos[1] < (int)renderData.tiles.size(1)) {
      RenderTile& renderTile = renderData.tiles(tileArrayPos[0], tileArrayPos[1]);
      PredictedTile& p = pair.second;
      if (p.liquid) {
        auto& liquid = *p.liquid;
        if (liquid.liquid == renderTile.liquidId) {
          uint8_t added = floatToByte(liquid.level, true);
          renderTile.liquidLevel = (renderTile.liquidLevel > 255 - added) ? 255 : renderTile.liquidLevel + added;
        }
        else {
          renderTile.liquidId = liquid.liquid;
          renderTile.liquidLevel = floatToByte(liquid.level, true);
        }
      }

      pair.second.apply(renderTile);
    }
  }

  for (auto const& previewTile : m_previewTiles) {
    Vec2I tileArrayPos = m_geometry.diff(previewTile.position, renderData.tileMinPosition);
    if (tileArrayPos[0] >= 0 && tileArrayPos[0] < (int)renderData.tiles.size(0) && tileArrayPos[1] >= 0 && tileArrayPos[1] < (int)renderData.tiles.size(1)) {
      RenderTile& renderTile = renderData.tiles(tileArrayPos[0], tileArrayPos[1]);

      auto material = previewTile.matId;
      auto hueShift = previewTile.hueShift;
      auto colorVariant = previewTile.colorVariant;
      if (previewTile.updateMatId) {
        if (previewTile.foreground) {
          renderTile.foreground = material;
          renderTile.foregroundHueShift = hueShift;
          renderTile.foregroundColorVariant = colorVariant;
        } else {
          renderTile.background = material;
          renderTile.backgroundHueShift = hueShift;
          renderTile.backgroundColorVariant = colorVariant;
        }
      }

      if (previewTile.liqId != EmptyLiquidId) {
        renderTile.liquidId = previewTile.liqId;
        renderTile.liquidLevel = 255;
      }
    }
  }

  renderData.particles = &m_particles->particles();
  LogMap::set("client_render_particle_count", renderData.particles->size());
  // Durable telemetry mirror (R-F gate): particle count in the snapshot, not just the /debug HUD.
  static auto particleCountGauge = Telemetry::gauge("render.particle.count",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
  particleCountGauge.set((int64_t)renderData.particles->size());

  renderData.skyRenderData = m_sky->renderData();

  auto environmentBiome = mainEnvironmentBiome();

  m_parallaxFadeTimer.tick();
  if (m_parallaxFadeTimer.ready() && m_nextParallax) {
    m_currentParallax = m_nextParallax;
    m_nextParallax.reset();
  }

  if (environmentBiome)
    setParallax(environmentBiome->parallax);

  if (m_currentParallax) {
    if (m_parallaxFadeTimer.ready()) {
      renderData.parallaxLayers.appendAll(m_currentParallax->layers());
    } else {
      for (auto layer : m_currentParallax->layers()) {
        layer.alpha = min(1.0f, m_parallaxFadeTimer.percent() * 2);
        renderData.parallaxLayers.append(layer);
      }
    }
  }

  if (m_nextParallax) {
    for (auto layer : m_nextParallax->layers()) {
      layer.alpha = min(1.0f, (1.0f - m_parallaxFadeTimer.percent()) * 2);
      renderData.parallaxLayers.append(layer);
    }
  }

  auto functionDatabase = Root::singleton().functionDatabase();
  for (auto& layer : renderData.parallaxLayers) {
    if (!layer.timeOfDayCorrelation.empty())
      layer.alpha *= clamp((float)functionDatabase->function(layer.timeOfDayCorrelation)->evaluate(m_sky->timeOfDay() / m_sky->dayLength()), 0.0f, 1.0f);
  }

  stableSort(renderData.parallaxLayers, [](ParallaxLayer const& a, ParallaxLayer const& b) {
      return tie(a.zLevel, a.verticalOrigin) > tie(b.zLevel, b.verticalOrigin);
    });

  auto overlayToDrawable = [](WorldStructure::Overlay const& overlay) -> Drawable {
    Drawable drawable = Drawable::makeImage(overlay.image, 1.0f / TilePixels, false, overlay.min);
    drawable.fullbright = overlay.fullbright;
    return drawable;
  };

  renderData.backgroundOverlays = m_centralStructure.backgroundOverlays().transformed(overlayToDrawable);
  renderData.foregroundOverlays = m_centralStructure.foregroundOverlays().transformed(overlayToDrawable);

  renderData.isFullbright = m_fullBright;
  renderData.dimLevel = m_worldDimLevel;
  renderData.dimColor = m_worldDimColor;
}

List<AudioInstancePtr> WorldClient::pullPendingAudio() {
  return take(m_samples);
}

List<AudioInstancePtr> WorldClient::pullPendingMusic() {
  return take(m_music);
}

void WorldClient::dimWorld() {
  m_worldDimTimer.reset();
}

bool WorldClient::interactiveHighlightMode() const {
  return m_interactiveHighlightMode;
}

void WorldClient::setInteractiveHighlightMode(bool enabled) {
  m_interactiveHighlightMode = enabled;
}

void WorldClient::setParallax(ParallaxPtr newParallax) {
  if (newParallax) {
    if (!m_currentParallax) {
      m_currentParallax = newParallax;
    } else if (m_parallaxFadeTimer.ready() && newParallax != m_currentParallax) {
      m_nextParallax = newParallax;
      m_parallaxFadeTimer.reset();
    } else if (m_nextParallax && newParallax == m_currentParallax) {
      m_currentParallax = m_nextParallax;
      m_nextParallax = newParallax;
      m_parallaxFadeTimer.invert();
    }
  }
}

void WorldClient::overrideGravity(float gravity) {
  m_overrideGravity = gravity;
}

void WorldClient::resetGravity() {
  m_overrideGravity = {};
}

bool WorldClient::fullBright() const {
  return m_fullBright;
}

void WorldClient::setFullBright(bool fullBright) {
  m_fullBright = fullBright;
}

bool WorldClient::asyncLighting() const {
  return m_asyncLighting;
}

void WorldClient::setAsyncLighting(bool asyncLighting) {
  m_asyncLighting = asyncLighting;
}

bool WorldClient::collisionDebug() const {
  return m_collisionDebug;
}

void WorldClient::setCollisionDebug(bool collisionDebug) {
  m_collisionDebug = collisionDebug;
}

void WorldClient::handleIncomingPackets(List<PacketPtr> const& packets) {
  auto& root = Root::singleton();
  auto materialDatabase = root.materialDatabase();
  auto itemDatabase = root.itemDatabase();
  auto entityFactory = root.entityFactory();

  for (auto const& packet : packets) {
    if (!inWorld() && !is<WorldStartPacket>(packet))
      Logger::error("WorldClient received packet type {} while not in world", PacketTypeNames.getRight(packet->type()));

    if (auto worldStartPacket = as<WorldStartPacket>(packet)) {
      initWorld(*worldStartPacket);

    } else if (auto worldStopPacket = as<WorldStopPacket>(packet)) {
      Logger::info("Client received world stop packet, leaving: {}", worldStopPacket->reason);
      clearWorld();

    } else if (auto entityCreate = as<EntityCreatePacket>(packet)) {
      if (m_entityMap->entity(entityCreate->entityId)) {
        Logger::error("WorldClient received entity create packet with duplicate entity id {}, deleting old entity.", entityCreate->entityId);
        removeEntity(entityCreate->entityId, false);
      }

      auto netRules = m_clientState.netCompatibilityRules();
      auto entity = entityFactory->netLoadEntity(entityCreate->entityType, entityCreate->storeData, netRules);
      entity->readNetState(entityCreate->firstNetState, 0.0f, netRules);
      entity->init(this, entityCreate->entityId, EntityMode::Slave);
      m_entityMap->addEntity(entity);

      if (m_interpolationTracker.interpolationEnabled()) {
        entity->enableInterpolation(m_interpolationTracker.extrapolationHint());

        // Delay appearance of new slaved entities to match with interplation
        // state.
        m_startupHiddenEntities.add(entityCreate->entityId);
        timer(m_interpolationTracker.interpolationLeadTime(), [this, entityId = entityCreate->entityId](World*) {
            m_startupHiddenEntities.remove(entityId);
          });
      }

    } else if (auto entityUpdateSet = as<EntityUpdateSetPacket>(packet)) {
      float interpolationLeadTime = m_interpolationTracker.interpolationLeadTime();
      m_entityMap->forAllEntities([&](EntityPtr const& entity) {
          EntityId entityId = entity->entityId();
          if (connectionForEntity(entityId) == entityUpdateSet->forConnection) {
            starAssert(entity->isSlave());
            entity->readNetState(entityUpdateSet->deltas.value(entityId), interpolationLeadTime, m_clientState.netCompatibilityRules());
          }
        });

    } else if (auto entityDestroy = as<EntityDestroyPacket>(packet)) {
      if (auto entity = m_entityMap->entity(entityDestroy->entityId)) {
        entity->readNetState(entityDestroy->finalNetState, m_interpolationTracker.interpolationLeadTime(), m_clientState.netCompatibilityRules());

        // Before destroying the entity, we should make sure that the entity is
        // using the absolute latest data, so we disable interpolation.

        if (m_interpolationTracker.interpolationEnabled() && entityDestroy->death) {
          // Delay death packets by the interpolation step to give time for
          // interpolation to catch up.
          timer(m_interpolationTracker.interpolationLeadTime(), [this, entity, entityDestroy](World*) {
              entity->disableInterpolation();
              removeEntity(entityDestroy->entityId, entityDestroy->death);
            });
        } else {
          entity->disableInterpolation();
          removeEntity(entityDestroy->entityId, entityDestroy->death);
        }
      }

    } else if (auto structurePacket = as<CentralStructureUpdatePacket>(packet)) {
      m_centralStructure = WorldStructure(structurePacket->structureData);

    } else if (auto tileArrayUpdate = as<TileArrayUpdatePacket>(packet)) {
      RectI tileRegion = RectI::withSize(tileArrayUpdate->min, Vec2I(tileArrayUpdate->array.size()));

      // NOTE: We're creating client side sectors on tileArrayUpdate here, and
      // at no other time, and this is sort of a big assumption that
      // tileArrayUpdate happens for all valid client side sectors first before
      // any other tile updates.
      // LOADING A SECTOR CHANGES WHAT THE GATHER COVERS, NOT JUST WHAT IT READS (#226 H2), and until now
      // nothing here bumped. tileEvalColumnsParallel does not visit absent sectors at all -- it passes
      // evalEmpty=false, so the callback never runs and the stable grid keeps whatever those cells already
      // held. A newly loaded sector therefore moves cells from "skipped, stale value retained" to
      // "gathered", which is a real change even when every tile in it matches what was there.
      //
      // Unconditional and once per batch. The readNetTile loop below happens to bump for every tile today
      // and so incidentally covered this, but that is an accident of it never gating -- this must not
      // depend on that, and it is the hazard that has to be closed before any bump becomes conditional.
      bool loadedAnySector = false;
      for (auto const& sector : m_tileArray->validSectorsFor(tileRegion)) {
        m_tileArray->loadDefaultSector(sector);
        loadedAnySector = true;
      }
      if (loadedAnySector)
        m_lightingTileEpoch.fetch_add(1, std::memory_order_relaxed);

      for (int x = tileRegion.xMin(); x < tileRegion.xMax(); ++x) {
        for (int y = tileRegion.yMin(); y < tileRegion.yMax(); ++y)
          readNetTile({x, y}, tileArrayUpdate->array(x - tileRegion.xMin(), y - tileRegion.yMin()), false);
      }
      dirtyCollision(tileRegion);

    } else if (auto tileUpdate = as<TileUpdatePacket>(packet)) {
      readNetTile(tileUpdate->position, tileUpdate->tile);

    } else if (auto tileDamageUpdate = as<TileDamageUpdatePacket>(packet)) {
      if (ClientTile* tile = m_tileArray->modifyTile(tileDamageUpdate->position)) {
        if (tileDamageUpdate->layer == TileLayer::Foreground)
          tile->foregroundDamage = tileDamageUpdate->tileDamage;
        else
          tile->backgroundDamage = tileDamageUpdate->tileDamage;

        m_damagedBlocks.add(tileDamageUpdate->position);
      }

    } else if (auto tileModificationFailure = as<TileModificationFailurePacket>(packet)) {
      // TODO: Right now we assume that every tile modification was caused by a
      // player, but this may not be true in the future.  In the future, there
      // may be context hints with tile modifications to figure out what to do
      // with failures.
      for (auto& modification : tileModificationFailure->modifications) {
        auto findPrediction = m_predictedTiles.find(modification.first);
        if (findPrediction != m_predictedTiles.end()) {
          auto& p = findPrediction->second;
          if (auto placeMaterial = modification.second.ptr<PlaceMaterial>()) {
            if (placeMaterial->layer == TileLayer::Foreground) {
              p.foreground.reset();
              p.foregroundHueShift.reset();
              if (p.collision) {
                p.collision.reset();
                dirtyCollision(RectI::withSize(modification.first, { 1, 1 }));
              }
            }
            else {
              p.background.reset();
              p.backgroundHueShift.reset();
            }
          } else if (auto placeMod = modification.second.ptr<PlaceMod>()) {
            if (placeMod->layer == TileLayer::Foreground) {
              p.foregroundMod.reset();
              p.foregroundModHueShift.reset();
            }
            else {
              p.backgroundMod.reset();
              p.backgroundModHueShift.reset();
            }
          } else if (auto placeColor = modification.second.ptr<PlaceMaterialColor>()) {
            if (placeColor->layer == TileLayer::Foreground)
              p.foregroundColorVariant.reset();
            else
              p.backgroundColorVariant.reset();
          } else if (auto placeLiquid = modification.second.ptr<PlaceLiquid>()) {
            p.liquid.reset();
          }

          if (!p)
            m_predictedTiles.erase(findPrediction);
        }

        if (auto placeMaterial = modification.second.ptr<PlaceMaterial>()) {
          auto stack = materialDatabase->materialItemDrop(placeMaterial->material);
          tryGiveMainPlayerItem(itemDatabase->item(stack), true);
        } else if (auto placeMod = modification.second.ptr<PlaceMod>()) {
          auto stack = materialDatabase->modItemDrop(placeMod->mod);
          tryGiveMainPlayerItem(itemDatabase->item(stack), true);
        }
      }

    } else if (auto liquidUpdate = as<TileLiquidUpdatePacket>(packet)) {
      m_predictedTiles.remove(liquidUpdate->position);
      if (ClientTile* tile = m_tileArray->modifyTile(liquidUpdate->position)) {
        // #225 MEASUREMENT ONLY -- the bump stays unconditional and nothing here changes behaviour.
        //
        // TWO predicates, because they are not the same and the difference decides the design. `noop` is
        // the naive one: did the LiquidLevel change at all. `dark` is the exact one: the gather adds
        // liquidsDatabase->radiantLight(level), which is settings->radiantLightLevel * level -- so for any
        // liquid whose radiance is zero, EVERY level change leaves the gather's output bit-identical.
        // Flowing water invalidating the lighting grid would be entirely wasted; flowing lava would not.
        static auto bumpLiquid = Telemetry::counter("lighting.epoch.bump.liquid",
          MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
        static auto bumpLiquidNoop = Telemetry::counter("lighting.epoch.bump.liquid.noop",
          MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
        static auto bumpLiquidDark = Telemetry::counter("lighting.epoch.bump.liquid.dark",
          MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
        static auto bumpLiquidOffRegion = Telemetry::counter("lighting.epoch.bump.liquid.offregion",
          MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
        LiquidLevel wasLiquid = tile->liquid;
        tile->liquid = liquidUpdate->liquidUpdate.liquidLevel();
        bumpLiquid.inc();
        if (wasLiquid.liquid == tile->liquid.liquid && wasLiquid.level == tile->liquid.level)
          bumpLiquidNoop.inc();
        if (epochBumpOffRegion(liquidUpdate->position))
          bumpLiquidOffRegion.inc();

        // BUMP ONLY WHEN THE RADIANCE ACTUALLY CHANGES, which is what the line here always claimed to do
        // and never did. The old comment read "temporal gate: liquid radiance changed" while the bump was
        // unconditional -- and measurement made that a provably false claim: 8522 of 8522 liquid updates
        // left radiantLight bit-identical, only 116 of them because the level was unchanged.
        //
        // The predicate is the gather's own arithmetic, not a proxy for it. radiantLight is
        // settings->radiantLightLevel * level, so a liquid with zero radiance contributes nothing at ANY
        // level: comparing raw {liquid, level} would fire on all 8406 level changes that cannot alter a
        // single lighting output, while comparing the radiance is exact.
        //
        // Safe only because sector load now bumps on its own (#226 H2, above). These suppressed bumps were
        // incidentally covering that gap, and removing accidental cover without closing the gap first is
        // how a value gate ships stale lighting.
        auto liquidsDatabase = Root::singleton().liquidsDatabase();
        if (liquidsDatabase->radiantLight(wasLiquid) == liquidsDatabase->radiantLight(tile->liquid))
          bumpLiquidDark.inc();
        else
          m_lightingTileEpoch.fetch_add(1, std::memory_order_relaxed);
      }

    } else if (auto giveItem = as<GiveItemPacket>(packet)) {
      tryGiveMainPlayerItem(itemDatabase->item(giveItem->item));

    } else if (auto stepUpdate = as<StepUpdatePacket>(packet)) {
      m_interpolationTracker.receiveTimeUpdate(stepUpdate->remoteTime);

    } else if (auto environmentUpdatePacket = as<EnvironmentUpdatePacket>(packet)) {
      m_sky->readUpdate(environmentUpdatePacket->skyDelta, m_clientState.netCompatibilityRules());
      m_weather.readUpdate(environmentUpdatePacket->weatherDelta, m_clientState.netCompatibilityRules());

    } else if (auto hit = as<HitRequestPacket>(packet)) {
      m_damageManager->pushRemoteHitRequest(hit->remoteHitRequest);

    } else if (auto damage = as<DamageRequestPacket>(packet)) {
      m_damageManager->pushRemoteDamageRequest(damage->remoteDamageRequest);

    } else if (auto damage = as<DamageNotificationPacket>(packet)) {
      std::string_view view = damage->remoteDamageNotification.damageNotification.targetMaterialKind.utf8();
      static const size_t FULL_SIZE = SECRET_BROADCAST_PREFIX.size() + Curve25519::SignatureSize;
      static const std::string LEGACY_VOICE_PREFIX = "data\0voice\0"s;

      if (view.size() >= FULL_SIZE && view.rfind(SECRET_BROADCAST_PREFIX, 0) != NPos) {
        // this is actually a secret broadcast!!
        if (auto player = m_entityMap->get<Player>(damage->remoteDamageNotification.sourceEntityId)) {
          if (auto publicKey = player->getSecretPropertyView(SECRET_BROADCAST_PUBLIC_KEY)) {
            if (publicKey->utf8Size() == Curve25519::PublicKeySize) {
              auto signature = view.substr(SECRET_BROADCAST_PREFIX.size(), Curve25519::SignatureSize);

              auto rawBroadcast = view.substr(FULL_SIZE);
              if (Curve25519::verify(
                (uint8_t const*)signature.data(),
                (uint8_t const*)publicKey->utf8Ptr(),
                (void*)rawBroadcast.data(),
                       rawBroadcast.size()
              )) {
                handleSecretBroadcast(player, rawBroadcast);
              }
            }
          }
        }
      }
      else if (view.size() > 75 && view.rfind(LEGACY_VOICE_PREFIX, 0) != NPos) {
        // this is a StarExtensions voice packet
        // (remove this and stop transmitting like this once most SE features are ported over)
        if (auto player = m_entityMap->get<Player>(damage->remoteDamageNotification.sourceEntityId)) {
          if (auto publicKey = player->effectsAnimator()->globalTagPtr("\0SE_VOICE_SIGNING_KEY"s)) {
            auto raw = view.substr(75);
            if (m_broadcastCallback && Curve25519::verify(
              (uint8_t const*)view.data() + LEGACY_VOICE_PREFIX.size(),
              (uint8_t const*)publicKey->utf8Ptr(),
              (void*)raw.data(),
                     raw.size()
            )) {
              auto broadcastData = "Voice\0"s;
              broadcastData.append(raw.data(), raw.size());
              m_broadcastCallback(player, broadcastData);
            }
          }
        }
      }
      else {
        m_damageManager->pushRemoteDamageNotification(damage->remoteDamageNotification);
      }

    } else if (auto entityMessagePacket = as<EntityMessagePacket>(packet)) {
      EntityPtr entity;
      if (entityMessagePacket->entityId.is<EntityId>())
        entity = m_entityMap->entity(entityMessagePacket->entityId.get<EntityId>());
      else
        entity = m_entityMap->uniqueEntity(entityMessagePacket->entityId.get<String>());

      if (!entity) {
        m_outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeLeft("Unknown entity"), entityMessagePacket->uuid));

      } else if (!entity->isMaster()) {
        Logger::error("Server has sent a scripted entity response for a slave entity");
        m_outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeLeft("Entity delivery error"), entityMessagePacket->uuid));

      } else {
        ConnectionId fromConnection = entityMessagePacket->fromConnection;
        if (fromConnection == *m_clientId) // Kae: The server should not be able to forge entity messages that appear as if they're from us
          fromConnection = ServerConnectionId;

        auto response = entity->receiveMessage(entityMessagePacket->fromConnection, entityMessagePacket->message, entityMessagePacket->args);
        if (response)
          m_outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeRight(response.take()), entityMessagePacket->uuid));
        else
          m_outgoingPackets.append(make_shared<EntityMessageResponsePacket>(makeLeft("Message not handled by entity"), entityMessagePacket->uuid));
      }

    } else if (auto entityMessageResponsePacket = as<EntityMessageResponsePacket>(packet)) {
      if (!m_entityMessageResponses.contains(entityMessageResponsePacket->uuid))
        Logger::warn("EntityMessageResponse received for unknown context [{}]!", entityMessageResponsePacket->uuid.hex());
      else {
        auto response = m_entityMessageResponses.take(entityMessageResponsePacket->uuid);
        if (entityMessageResponsePacket->response.isRight())
          response.fulfill(entityMessageResponsePacket->response.right());
        else
          response.fail(entityMessageResponsePacket->response.left());
      }
    } else if (auto updateWorldProperties = as<UpdateWorldPropertiesPacket>(packet)) {
      // Kae: Properties set to null (nil from Lua) should be erased instead of lingering around
      for (auto& pair : updateWorldProperties->updatedProperties) {
        if (pair.second.isNull())
          m_worldProperties.erase(pair.first);
        else
          m_worldProperties[pair.first] = pair.second;
      }

    } else if (auto updateTileProtection = as<UpdateTileProtectionPacket>(packet)) {
      setTileProtection(updateTileProtection->dungeonId, updateTileProtection->isProtected);

    } else if (auto setDungeonGravity = as<SetDungeonGravityPacket>(packet)) {
      if (setDungeonGravity->gravity)
        m_dungeonIdGravity[setDungeonGravity->dungeonId] = *setDungeonGravity->gravity;
      else
        m_dungeonIdGravity.remove(setDungeonGravity->dungeonId);

    } else if (auto setDungeonBreathable = as<SetDungeonBreathablePacket>(packet)) {
      if (setDungeonBreathable->breathable.isValid())
        m_dungeonIdBreathable[setDungeonBreathable->dungeonId] = *setDungeonBreathable->breathable;
      else
        m_dungeonIdBreathable.remove(setDungeonBreathable->dungeonId);

    } else if (auto entityInteract = as<EntityInteractPacket>(packet)) {
      auto interactResult = interact(entityInteract->interactRequest).result();
      m_outgoingPackets.append(make_shared<EntityInteractResultPacket>(interactResult.value(), entityInteract->requestId, entityInteract->interactRequest.sourceId));

    } else if (auto interactResult = as<EntityInteractResultPacket>(packet)) {
      if (auto response = m_entityInteractionResponses.maybeTake(interactResult->requestId)) {
        if (interactResult->action)
          response->fulfill(interactResult->action);
        else
          response->fail("no interaction result");
      }
    } else if (auto setPlayerStart = as<SetPlayerStartPacket>(packet)) {
      m_playerStart = setPlayerStart->playerStart;
      m_respawnInWorld = setPlayerStart->respawnInWorld;

    } else if (auto findUniqueEntityResponse = as<FindUniqueEntityResponsePacket>(packet)) {
      for (auto& promise : take(m_findUniqueEntityResponses[findUniqueEntityResponse->uniqueEntityId])) {
        if (findUniqueEntityResponse->entityPosition)
          promise.fulfill(*findUniqueEntityResponse->entityPosition);
        else
          promise.fail("Unknown entity");
      }

    } else if (auto worldLayoutUpdate = as<WorldLayoutUpdatePacket>(packet)) {
      m_worldTemplate->setWorldLayout(make_shared<WorldLayout>(worldLayoutUpdate->layoutData));

    } else if (auto worldParametersUpdate = as<WorldParametersUpdatePacket>(packet)) {
      m_worldTemplate->setWorldParameters(netLoadVisitableWorldParameters(worldParametersUpdate->parametersData));
      // undergroundLevel IS A LIGHTING INPUT AND NOTHING USED TO INVALIDATE ON IT (#226 H1). gatherColumns
      // reads m_worldTemplate->undergroundLevel() once per gather and it is the sole non-tile term of
      // skyExposed -- `backgroundLightTransparent && pos[1] + y > undergroundLevel`. World parameters
      // resolve that value, so changing them can stale the skyExposed bit of the ENTIRE cached grid, and no
      // per-tile invalidation could ever catch it because the input is not a tile.
      //
      // The epoch, not m_gatherValid: this runs on the client thread while the cache state belongs to the
      // lighting thread, and the epoch is the atomic already built to carry exactly this signal across it.
      m_lightingTileEpoch.fetch_add(1, std::memory_order_relaxed);

    } else if (auto pongPacket = as<PongPacket>(packet)) {
      if (pongPacket->time)
        m_latency = Time::monotonicMilliseconds() - pongPacket->time;
      else if (m_pingTime) 
        m_latency = Time::monotonicMilliseconds() - m_pingTime.take();

    } else {
      Logger::error("Improper packet type {} received by client", (int)packet->type());
    }
  }
}

List<PacketPtr> WorldClient::getOutgoingPackets() {
  return std::move(m_outgoingPackets);
}

void WorldClient::update(float dt) {
  if (!inWorld())
    return;

  auto assets = Root::singleton().assets();

  float expireTime = min(float(m_latency + 800), 2000.f);
  auto now = Time::monotonicMilliseconds();
  eraseWhere(m_predictedTiles, [&](auto& pair) {
    float expiry = (float)(now - pair.second.time) / expireTime;
    auto center = Vec2F(pair.first) + Vec2F::filled(0.5f);
    auto size = Vec2F::filled(0.875f - expiry * 0.875f);
    auto poly = PolyF(RectF::withCenter(center, size));
    SpatialLogger::logPoly("world", poly, Color::Cyan.mix(Color::Red, expiry).toRgba());
    if (expiry >= 1.0f) {
      dirtyCollision(RectI::withSize(pair.first, { 1, 1 }));
      return true;
    } else {
      return false;
    }
  });

  // Secret broadcasts are transmitted through DamageNotifications for vanilla server compatibility.
  // Because DamageNotification packets are spoofable, we have to sign the data so other clients can validate that it is legitimate.
  auto& publicKey = Curve25519::publicKey();
  String publicKeyString((const char*)publicKey.data(), publicKey.size());
  m_mainPlayer->setSecretProperty(SECRET_BROADCAST_PUBLIC_KEY, publicKeyString);
  // Temporary: Backwards compatibility with StarExtensions
  m_mainPlayer->effectsAnimator()->setGlobalTag("\0SE_VOICE_SIGNING_KEY"s, publicKeyString);

  ++m_currentStep;
  m_currentTime += dt;
  m_interpolationTracker.update(m_currentTime);

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

  List<EntityId> toRemove;
  List<EntityId> clientPresenceEntities;
  m_entityMap->updateAllEntities([&](EntityPtr const& entity) {
      try { entity->update(dt, m_currentStep); }
      catch (StarException const& e) {
        if (entity->isMaster()) // this is YOUR problem!!
          throw e;
        else { // this is THEIR problem!!
          auto issue = printException(e, true);
          auto hash = hashOf(issue);
          if (!m_entityExceptionsLogged.contains(hash))
            m_entityExceptionsLogged.insert(hash);
          else
            issue = e.what();

          Logger::error("WorldClient: Exception caught in {}::update ({}): {}", EntityTypeNames.getRight(entity->entityType()), entity->entityId(), issue);
        }
      }

      if (entity->shouldDestroy() && entity->entityMode() == EntityMode::Master)
        toRemove.append(entity->entityId());
      if (entity->isMaster() && entity->clientEntityMode() == ClientEntityMode::ClientPresenceMaster)
        clientPresenceEntities.append(entity->entityId());
    }, [](EntityPtr const& a, EntityPtr const& b) {
      return a->entityType() < b->entityType();
    });

  m_clientState.setPlayer(m_mainPlayer->entityId());
  m_clientState.setClientPresenceEntities(std::move(clientPresenceEntities));

  m_damageManager->update(dt);
  handleDamageNotifications();

  m_sky->setAltitude(m_clientState.windowCenter()[1]);
  m_sky->update(dt);

  RectI particleRegion = m_clientState.window().padded(m_clientConfig.getInt("particleRegionPadding"));

  m_weather.setVisibleRegion(particleRegion);
  m_weather.update(dt);

  if (!m_mainPlayer->isDead()) {
    // Clear m_requestedDrops every so often in case of entity id reuse or
    // desyncs etc
    if (m_currentStep % m_clientConfig.getInt("itemRequestReset") == 0)
      m_requestedDrops.clear();

    Vec2F playerPos = m_mainPlayer->position();
    auto dropList = m_entityMap->query<ItemDrop>(RectF(playerPos - Vec2F::filled(DropDist / 2), playerPos + Vec2F::filled(DropDist / 2)));
    for (auto itemDrop : dropList) {
      auto distSquared = m_geometry.diff(itemDrop->position(), playerPos).magnitudeSquared();

      // If the drop is within DropDist and not owned, request it.
      if (itemDrop->canTake() && !m_requestedDrops.contains(itemDrop->entityId()) && distSquared < square(DropDist)) {
        m_requestedDrops.add(itemDrop->entityId());
        if (m_mainPlayer->itemsCanHold(itemDrop->item()) != 0) {
          m_startupHiddenEntities.erase(itemDrop->entityId());
          itemDrop->takeBy(m_mainPlayer->entityId(), (float)m_latency / 1000);
          m_outgoingPackets.append(make_shared<RequestDropPacket>(itemDrop->entityId()));
        }
      }
    }
  } else {
    m_requestedDrops.clear();
  }

  sparkDamagedBlocks();

  m_particles->addParticles(m_weather.pullNewParticles());
  m_particles->update(dt, RectF(particleRegion), m_weather.wind());

  if (auto audioSample = m_ambientSounds.updateAmbient(currentAmbientNoises(), m_sky->isDayTime()))
    m_samples.append(audioSample);
  if (auto audioSample = m_ambientSounds.updateWeather(currentWeatherNoises()))
    m_samples.append(audioSample);

  if (inSpace()) {
    m_samples.appendAll(m_sky->pullSounds());

    if (m_spaceSound && m_spaceSound->finished()) {
      m_spaceSound = {};
      m_activeSpaceSound = "";
    }

    auto skyAmbientNoise = m_sky->ambientNoise();
    if (skyAmbientNoise != m_activeSpaceSound) {
      if (m_spaceSound) {
        m_spaceSound->stop(skyAmbientNoise == "" ? 3.0 : 0.0);
      } else {
        m_activeSpaceSound = skyAmbientNoise;
        if (!m_activeSpaceSound.empty()) {
          m_spaceSound = make_shared<AudioInstance>(*assets->audio(m_activeSpaceSound));
          m_samples.append(m_spaceSound);
        }
      }
    }
  }

  if (auto newAltMusic = m_mainPlayer->pullPendingAltMusic()) {
    if (newAltMusic->first)
      playAltMusic(newAltMusic->first->first, newAltMusic->second, newAltMusic->first->second);
    else
      stopAltMusic(newAltMusic->second);
  }

  if (auto audioSample = m_altMusicTrack.updateAmbient(currentAltMusicTrack(), true))
    m_music.append(audioSample);

  if (auto audioSample = m_musicTrack.updateAmbient(currentMusicTrack(), m_sky->isDayTime()))
    m_music.append(audioSample);

  for (EntityId entityId : toRemove)
    removeEntity(entityId, true);

  queueUpdatePackets(m_entityUpdateTimer.wrapTick(dt));

  if ((!m_clientState.netCompatibilityRules().isLegacy() && m_currentStep % 3 == 0) || m_pingTime.isNothing()) {
    m_pingTime = Time::monotonicMilliseconds();
    m_outgoingPackets.append(make_shared<PingPacket>(*m_pingTime));
  }

  LogMap::set("client_ping", m_latency);

  // Remove active sectors that are outside of the current monitoring region
  Set<ClientTileSectorArray::Sector> neededSectors;
  auto monitoredRegions = m_clientState.monitoringRegions([this](EntityId entityId) -> Maybe<RectI> {
      if (auto entity = this->entity(entityId))
        return RectI::integral(entity->metaBoundBox().translated(entity->position()));
      return {};
    });
  for (auto monitoredRegion : monitoredRegions)
    neededSectors.addAll(m_tileArray->validSectorsFor(monitoredRegion.padded(WorldSectorSize)));

  // m_lightMapPrepMutex IS A LIFETIME LOCK HERE, NOT A LIGHTING LOCK (#210). The lighting thread's
  // gather holds raw `Array*` into sector storage (SectorArray2D::evalColumnsPrivPar) and
  // SectorArray2D has no internal synchronisation, so unloading without it is a use-after-free.
  // Any future cross-thread reader of m_tileArray must take it too; nothing enforces that, because
  // the real fix is for SectorArray2D to own its lifetime contract. Costs one gather of main-thread
  // stall, 93-276 us (#168). Do NOT add a `loadedSectors.size() > neededSectors.size()` guard: the
  // sets can differ with equal or smaller loaded size, and stale sectors would never unload.
  auto loadedSectors = m_tileArray->loadedSectors();
  {
    MutexLocker prepLocker(m_lightMapPrepMutex);
    for (auto sector : loadedSectors) {
      if (!neededSectors.contains(sector))
        m_tileArray->unloadSector(sector);
    }
  }

  if (m_collisionDebug)
    renderCollisionDebug();

  LogMap::set("client_entities", m_entityMap->size());
  LogMap::set("client_sectors", toString(loadedSectors.size()));
  LogMap::set("client_lua_mem", m_luaRoot->luaMemoryUsage());
}

ConnectionId WorldClient::connection() const {
  return *m_clientId;
}

WorldGeometry WorldClient::geometry() const {
  return m_geometry;
}

uint64_t WorldClient::currentStep() const {
  return m_currentStep;
}

MaterialId WorldClient::material(Vec2I const& pos, TileLayer layer) const {
  if (!inWorld())
    return NullMaterialId;
  return m_tileArray->tile(pos).material(layer);
}

MaterialHue WorldClient::materialHueShift(Vec2I const& position, TileLayer layer) const {
  if (!inWorld())
    return MaterialHue();
  auto const& tile = m_tileArray->tile(position);
  return layer == TileLayer::Foreground ? tile.foregroundHueShift : tile.backgroundHueShift;
}

ModId WorldClient::mod(Vec2I const& pos, TileLayer layer) const {
  if (!inWorld())
    return NoModId;
  return m_tileArray->tile(pos).mod(layer);
}

MaterialHue WorldClient::modHueShift(Vec2I const& position, TileLayer layer) const {
  if (!inWorld())
    return MaterialHue();
  auto const& tile = m_tileArray->tile(position);
  return layer == TileLayer::Foreground ? tile.foregroundModHueShift : tile.backgroundModHueShift;
}

MaterialColorVariant WorldClient::colorVariant(Vec2I const& position, TileLayer layer) const {
  if (!inWorld())
    return MaterialColorVariant();
  auto const& tile = m_tileArray->tile(position);
  return layer == TileLayer::Foreground ? tile.foregroundColorVariant : tile.backgroundColorVariant;
}

EntityPtr WorldClient::entity(EntityId entityId) const {
  if (!inWorld())
    return {};

  return m_entityMap->entity(entityId);
}

void WorldClient::addEntity(EntityPtr const& entity, EntityId entityId) {
  if (!entity)
    return;

  if (!inWorld())
    return;

  if (entity->clientEntityMode() != ClientEntityMode::ClientSlaveOnly) {
    entity->init(this, m_entityMap->reserveEntityId(entityId), EntityMode::Master);
    m_entityMap->addEntity(entity);
    notifyEntityCreate(entity);
  } else {
    auto entityFactory = Root::singleton().entityFactory();
    auto netRules = m_clientState.netCompatibilityRules();
    m_outgoingPackets.append(make_shared<SpawnEntityPacket>(entity->entityType(), entityFactory->netStoreEntity(entity, netRules), entity->writeNetState(0, netRules).first));
  }
}

TileDamageResult WorldClient::damageTiles(List<Vec2I> const& pos, TileLayer layer, Vec2F const& sourcePosition, TileDamage const& tileDamage, Maybe<EntityId> sourceEntity) {
  if (!inWorld())
    return TileDamageResult::None;

  // Filter out any tiles that are not currently occupied or are protected
  auto occupied = pos.filtered([this, layer](Vec2I pos) { return tileIsOccupied(pos, layer, true); });
  auto toDamage = occupied.filtered([this](Vec2I pos) { return !isTileProtected(pos); });
  auto toDing = occupied.filtered([this](Vec2I pos) { return isTileProtected(pos); });

  if (toDamage.size() + toDing.size() == 0)
    return TileDamageResult::None;

  auto res = TileDamageResult::None;

  if (toDing.size()) {
    auto dingDamage = tileDamage;
    dingDamage.type = TileDamageType::Protected;
    m_outgoingPackets.append(make_shared<DamageTileGroupPacket>(std::move(toDing), layer, sourcePosition, dingDamage, Maybe<EntityId>()));
    res = TileDamageResult::Protected;
  }

  if (toDamage.size()) {
    m_outgoingPackets.append(make_shared<DamageTileGroupPacket>(std::move(toDamage), layer, sourcePosition, tileDamage, sourceEntity));
    res = TileDamageResult::Normal;
  }

  return res;
}

DungeonId WorldClient::dungeonId(Vec2I const& pos) const {
  if (!inWorld())
    return NoDungeonId;

  return m_tileArray->tile(pos).dungeonId;
}

void WorldClient::collectLiquid(List<Vec2I> const& tilePositions, LiquidId liquidId) {
  if (!inWorld())
    return;

  float bucketSize = Root::singleton().assets()->json("/items/defaultParameters.config:liquidItems.bucketSize").toFloat();
  float nextUnit = bucketSize;
  List<Vec2I> maybeDrainTiles;

  for (auto& pos : tilePositions) {
    if (isTileProtected(pos))
      continue;
    auto& p = m_predictedTiles[pos];
    auto const& tile = m_tileArray->tile(pos);
    if ((p.liquid ? p.liquid->liquid : tile.liquid.liquid) == liquidId) {
      if (!p.liquid)
        p.liquid.emplace(tile.liquid.liquid, tile.liquid.level);
      auto& liquid = *p.liquid;
      if (liquid.level >= nextUnit) {
        liquid.take(nextUnit);
        nextUnit = bucketSize;

        for (size_t i = 0; i < maybeDrainTiles.size(); ++i)
          m_predictedTiles[pos].liquid.emplace(EmptyLiquidId, 0.0f);

        maybeDrainTiles.clear();
      }

      if (liquid.level > 0) {
        nextUnit -= liquid.level;
        maybeDrainTiles.append(pos);
      }
    }
  }

  m_outgoingPackets.append(make_shared<CollectLiquidPacket>(tilePositions, liquidId));
}

bool WorldClient::waitForLighting(WorldRenderData* renderData) {
  MutexLocker prepLocker(m_lightMapPrepMutex);
  MutexLocker lightMapLocker(m_lightMapMutex);
  // Slice 4: a lighting frame is "ready to consume" when either the CPU lightMap is
  // fresh (CPU mode / first GPU frame / shadow-compare) OR fresh GPU inputs were
  // exported (skip-calculate GPU mode, where m_lightMap is intentionally empty).
  // Freshness of the GPU inputs is the explicit m_lightingInputsFresh flag (set on publish, cleared
  // below on consume, both under m_lightMapMutex) -- NOT the emptiness of m_lightingEmission. The
  // buffers are swapped back rather than moved out now, so they stay non-empty across a consume and
  // emptiness no longer carries any signal for them. (m_lightMap still uses the consume-once move.)
  if (renderData && (!m_lightMap.empty() || (m_lightingInputsValid && m_lightingInputsFresh))) {
    // Preview-tile light injection patches the CPU lightMap so client-predicted (pre-server-confirm)
    // block placement lights up instantly. Known limitation (Slice 4): in skip-calculate GPU mode
    // m_lightMap is intentionally empty, so this patch is unavailable -- a placed block's light
    // appears one server-confirm round-trip later (once it enters the tile gather -> export -> GPU).
    // The empty-guard makes that skip explicit (the bounds checks would no-op against width/height 0).
    if (!m_lightMap.empty()) {
      for (auto& previewTile : m_previewTiles) {
        if (previewTile.updateLight) {
          Vec2I lightArrayPos = m_geometry.diff(previewTile.position, m_lightMinPosition);
          if (lightArrayPos[0] >= 0 && lightArrayPos[0] < (int)m_lightMap.width()
           && lightArrayPos[1] >= 0 && lightArrayPos[1] < (int)m_lightMap.height())
            m_lightMap.set(lightArrayPos[0], lightArrayPos[1], Color::v3bToFloat(previewTile.light));
        }
      }
    }
    renderData->lightMap = std::move(m_lightMap);
    renderData->lightMinPosition = m_lightMinPosition;
    // Travel the GPU-spread inputs alongside the lightmap when present.
    renderData->lightingInputsValid = m_lightingInputsValid;
    if (m_lightingInputsValid) {
      // SWAP, not move. Moving emptied these, which is what forced lightingCalc to re-allocate and
      // zero-fill ~788 KB every recompute; swapping hands renderData's previous (correctly-sized)
      // allocations back so reset()/resize() hit their same-size early-outs. Safe because the lighting
      // thread rewrites every one of them in full before the next publish, and because the render thread
      // has finished with the buffers it is handing back -- they were last frame's renderData, already
      // consumed by the painter before this frame's waitForLighting runs.
      std::swap(renderData->lightingEmission, m_lightingEmission);
      std::swap(renderData->lightingObstacle, m_lightingObstacle);
      std::swap(renderData->lightingPointLights, m_lightingPointLights);
      std::swap(renderData->lightingEmissionHalf, m_lightingEmissionHalf);
      std::swap(renderData->lightingObstacleR8, m_lightingObstacleR8);
      renderData->lightMapBorder = m_lightingBorder;
    }
    m_lightingInputsFresh = false;
    return true;
  }
  return false;
}

void WorldClient::setGpuLightingActive(bool active) {
  m_gpuLightingActive.store(active, std::memory_order_relaxed);
}

WorldClient::BroadcastCallback& WorldClient::broadcastCallback() {
  return m_broadcastCallback;
}

bool WorldClient::isTileProtected(Vec2I const& pos) const {
  if (!inWorld())
    return true;

  auto const& tile = m_tileArray->tile(pos);
  return m_protectedDungeonIds.contains(tile.dungeonId);
}

void WorldClient::setTileProtection(DungeonId dungeonId, bool isProtected) {
  if (isProtected) {
    m_protectedDungeonIds.add(dungeonId);
  } else {
    m_protectedDungeonIds.remove(dungeonId);
  }
}

void WorldClient::queueUpdatePackets(bool sendEntityUpdates) {
  auto& root = Root::singleton();
  auto assets = root.assets();
  auto entityFactory = root.entityFactory();

  m_outgoingPackets.append(make_shared<StepUpdatePacket>(m_currentTime));

  if (m_currentStep % m_clientConfig.getInt("worldClientStateUpdateDelta") == 0)
    m_outgoingPackets.append(make_shared<WorldClientStateUpdatePacket>(m_clientState.writeDelta()));

  m_entityMap->forAllEntities([&](EntityPtr const& entity) { notifyEntityCreate(entity); });

  if (sendEntityUpdates) {
    auto entityUpdateSet = make_shared<EntityUpdateSetPacket>();
    entityUpdateSet->forConnection = *m_clientId;
    auto netRules = m_clientState.netCompatibilityRules();
    m_entityMap->forAllEntities([&](EntityPtr const& entity) {
        if (auto version = m_masterEntitiesNetVersion.ptr(entity->entityId())) {
          // Lever #4: pump deferred stores before the early-out — the client's
          // master entities (e.g. the player in single-player) are gated by the
          // same process-global flag set in WorldServer::init.
          if (NetElementEarlyOut::active())
            entity->netStorePump();
          auto updateAndVersion = entity->writeNetState(*version, netRules);
          if (!updateAndVersion.first.empty())
            entityUpdateSet->deltas[entity->entityId()] = std::move(updateAndVersion.first);
          *version = updateAndVersion.second;
        }
      });
    m_outgoingPackets.append(std::move(entityUpdateSet));
  }

  for (auto& remoteHitRequest : m_damageManager->pullRemoteHitRequests())
    m_outgoingPackets.append(make_shared<HitRequestPacket>(std::move(remoteHitRequest)));
  for (auto& remoteDamageRequest : m_damageManager->pullRemoteDamageRequests())
    m_outgoingPackets.append(make_shared<DamageRequestPacket>(std::move(remoteDamageRequest)));
  for (auto& remoteDamageNotification : m_damageManager->pullRemoteDamageNotifications())
    m_outgoingPackets.append(make_shared<DamageNotificationPacket>(std::move(remoteDamageNotification)));
}

void WorldClient::handleDamageNotifications() {
  if (!inWorld())
    return;

  auto renderParticle = [&](Vec2F position, float amount, String const& damageNumberParticleKind) {
    int displayValue = (int)ceil(amount - 0.1f);
    if (displayValue <= 0)
      return;
    Particle particle = Root::singleton().particleDatabase()->particle(damageNumberParticleKind);
    particle.position += position;
    particle.string = particle.string.replace("$dmg$", toString(displayValue));
    m_particles->add(particle);
  };

  eraseWhere(m_damageNumbers, [&](std::pair<DamageNumberKey, DamageNumber> const& entry) -> bool {
      if (Time::monotonicTime() - entry.second.timestamp > m_damageNotificationBatchDuration) {
        renderParticle(entry.second.position, entry.second.amount, entry.first.damageNumberParticleKind);
        return true;
      }
      return false;
    });

  for (auto const& damageNotification : m_damageManager->pullPendingNotifications()) {
    auto damageDatabase = Root::singleton().damageDatabase();
    DamageKind const& damageKind = damageDatabase->damageKind(damageNotification.damageSourceKind);
    ElementalType const& elementalType = damageDatabase->elementalType(damageKind.elementalType);

    auto damageNumberParticleKind = elementalType.damageNumberParticles.get(damageNotification.hitType);
    auto damageNumberKey = DamageNumberKey{ damageNumberParticleKind, damageNotification.sourceEntityId, damageNotification.targetEntityId};


    DamageNumber number;
    if (m_damageNumbers.contains(damageNumberKey)) {
      number = m_damageNumbers.take(damageNumberKey);

      if (damageNotification.hitType == HitType::Kill)
        renderParticle(damageNotification.position,
            damageNotification.damageDealt + number.amount,
            damageNumberKey.damageNumberParticleKind);
    } else {
      if (damageNotification.hitType == HitType::Kill)
        renderParticle(damageNotification.position, damageNotification.damageDealt, damageNumberParticleKind);
      number.amount = 0;
      number.timestamp = Time::monotonicTime();
    }

    if (damageNotification.hitType != HitType::Kill) {
      number.position = damageNotification.position;
      number.amount += damageNotification.damageDealt;
      m_damageNumbers[damageNumberKey] = number;
    }

    String material = damageNotification.targetMaterialKind;
    if (!material.empty() && damageKind.effects.contains(material)) {
      // default to normal hit
      HitType effectHitType = damageKind.effects.get(material).contains(damageNotification.hitType) ? damageNotification.hitType : HitType::Hit;
      m_samples.appendAll(soundsFromDefinition(damageKind.effects.get(material).get(effectHitType).sounds, damageNotification.position));
      
      auto hitParticles = particlesFromDefinition(damageKind.effects.get(material).get(effectHitType).particles, damageNotification.position);
      
      const List<Directives>* directives = nullptr;
      if (auto& worldTemplate = m_worldTemplate) {
        if (const auto& parameters = worldTemplate->worldParameters())
          if (auto& globalDirectives = parameters->globalDirectives)
            directives = &globalDirectives.get();
      }
      if (directives) {
        int directiveIndex = unsigned(damageNotification.targetEntityId) % directives->size();
        for (auto& p : hitParticles)
          p.directives.append(directives->get(directiveIndex));
      }
      
      m_particles->addParticles(hitParticles);
    }
  }
}

void WorldClient::sparkDamagedBlocks() {
  if (!inWorld())
    return;

  auto materialDatabase = Root::singleton().materialDatabase();

  for (auto pos : m_damagedBlocks.values()) {
    if (auto tile = m_tileArray->modifyTile(pos)) {
      if (tile->backgroundDamage.healthy() && tile->foregroundDamage.healthy())
        m_damagedBlocks.remove(pos);

      if (isRealMaterial(tile->foreground) && tile->foregroundDamage.damageEffectPercentage() - Random::randf() > 0.0f
          && (Random::randf() < m_blockDamageParticleProbability)) {
        auto particle = m_blockDamageParticle;
        particle.color = materialDatabase->materialParticleColor(tile->foreground, tile->foregroundHueShift);

        if (isTileProtected(pos))
          particle = m_blockDingParticle;

        particle.position += centerOfTile(pos);
        particle.velocity = particle.velocity.magnitude()
            * vnorm(m_geometry.diff(tile->foregroundDamage.sourcePosition(), particle.position));
        particle.applyVariance(m_blockDamageParticleVariance);
        m_particles->add(particle);
      }

      if (isRealMaterial(tile->background) && tile->backgroundDamage.damageEffectPercentage() - Random::randf() > 0.0f
          && (Random::randf() < m_blockDamageParticleProbability)) {
        auto particle = m_blockDamageParticle;
        particle.color = materialDatabase->materialParticleColor(tile->background, tile->backgroundHueShift);

        if (isTileProtected(pos))
          particle = m_blockDingParticle;

        particle.position += centerOfTile(pos);
        particle.velocity = particle.velocity.magnitude()
            * vnorm(m_geometry.diff(tile->backgroundDamage.sourcePosition(), particle.position));
        particle.applyVariance(m_blockDamageParticleVariance);
        m_particles->add(particle);
      }
    }
  }
}

InteractiveEntityPtr WorldClient::getInteractiveInRange(Vec2F const& targetPosition, Vec2F const& sourcePosition, float maxRange) const {
  if (!inWorld())
    return {};
  return WorldImpl::getInteractiveInRange(m_geometry, m_entityMap, targetPosition, sourcePosition, maxRange);
}

bool WorldClient::canReachEntity(Vec2F const& position, float radius, EntityId targetEntity, bool preferInteractive) const {
  if (!inWorld())
    return false;
  return WorldImpl::canReachEntity(m_geometry, m_tileArray, m_entityMap, position, radius, targetEntity, preferInteractive);
}

RpcPromise<InteractAction> WorldClient::interact(InteractRequest const& request) {
  if (!inWorld())
    return RpcPromise<InteractAction>::createFailed("not initialized in world");

  if (auto targetEntity = m_entityMap->entity(request.targetId)) {
    if (targetEntity->isMaster()) {
      // client-side-master entities need to be handled here rather than over network
      if (auto interactiveTarget = as<InteractiveEntity>(targetEntity))
        return RpcPromise<InteractAction>::createFulfilled(interactiveTarget->interact(request));
      else
        return RpcPromise<InteractAction>::createFulfilled(InteractAction());
    }
  }

  auto pair = RpcPromise<InteractAction>::createPair();
  Uuid requestId;
  m_entityInteractionResponses[requestId] = pair.second;
  m_outgoingPackets.append(make_shared<EntityInteractPacket>(request, requestId));

  return pair.first;
}

template <typename ColumnSink>
void WorldClient::gatherColumns(RectI const& region, ColumnSink&& sink) {
  float undergroundLevel = m_worldTemplate->undergroundLevel();
  auto liquidsDatabase = Root::singleton().liquidsDatabase();
  auto materialDatabase = Root::singleton().materialDatabase();

  // Each column is guaranteed no larger than the sector size, which is what lets the staging arrays
  // be fixed-size stack storage.
  m_tileArray->tileEvalColumnsParallel(region, [&](Vec2I const& pos, ClientTile const* column, size_t ySize) {
    Vec3F colLight[WorldSectorSize];
    bool colObstacle[WorldSectorSize];
    bool colSkyExposed[WorldSectorSize];
    // Memoize radiantLight across vertical runs of identical material/mod (stone columns, open sky).
    // It is a pure function of (id, mod), so reuse is byte-identical. The sentinel is the "no
    // material" state, which never passes the emission guard, so the first emitting tile recomputes.
    MaterialId fgMat = EmptyMaterialId; ModId fgMod = NoModId; Vec3F fgLight;
    MaterialId bgMat = EmptyMaterialId; ModId bgMod = NoModId; Vec3F bgLight;
    for (size_t y = 0; y < ySize; ++y) {
      auto& tile = column[y];
      Vec3F light;
      if (tile.foreground != EmptyMaterialId || tile.foregroundMod != NoModId) {
        if (tile.foreground != fgMat || tile.foregroundMod != fgMod) {
          fgMat = tile.foreground; fgMod = tile.foregroundMod;
          fgLight = materialDatabase->radiantLight(fgMat, fgMod);
        }
        light += fgLight;
      }

      if (tile.liquid.liquid != EmptyLiquidId && tile.liquid.level != 0.0f)
        light += liquidsDatabase->radiantLight(tile.liquid);
      bool skyExposed = false;
      if (tile.foregroundLightTransparent) {
        if (tile.background != EmptyMaterialId || tile.backgroundMod != NoModId) {
          if (tile.background != bgMat || tile.backgroundMod != bgMod) {
            bgMat = tile.background; bgMod = tile.backgroundMod;
            bgLight = materialDatabase->radiantLight(bgMat, bgMod);
          }
          light += bgLight;
        }
        if (tile.backgroundLightTransparent && pos[1] + y > undergroundLevel)
          skyExposed = true;
      }
      colLight[y] = light;
      colObstacle[y] = !tile.foregroundLightTransparent;
      colSkyExposed[y] = skyExposed;
    }
    sink(pos, colLight, colObstacle, colSkyExposed, ySize);
  });
}

void WorldClient::lightingTileGather() {
  int64_t start = Time::monotonicMicroseconds();
  Vec3F environmentLight = m_sky->environmentLight().toRgbF();
  // One setCellColumn per column resolves the monochrome/Either branch once per column, not per tile.
  gatherColumns(m_lightingCalculator.calculationRegion(),
    [&](Vec2I const& pos, Vec3F* light, bool* obstacle, bool const* skyExposed, size_t ySize) {
      for (size_t y = 0; y < ySize; ++y) {
        if (skyExposed[y])
          light[y] += environmentLight;
      }
      m_lightingCalculator.setCellColumn(m_lightingCalculator.baseIndexFor(pos), light, obstacle, ySize);
    });
  LogMap::set("client_render_world_async_light_gather", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - start));
}

void WorldClient::lightingStableGather() {
  RectI calcRegion = m_lightingCalculator.calculationRegion();
  // Zero the whole grid first so cells outside the loaded sectors (which tileEvalColumnsParallel
  // clamps away) read as {0 light, not-obstacle, not-sky} -- exactly what begin() leaves them as in
  // the direct gather. Then gather the full calc region over the top.
  m_gatherGrid.assign((size_t)calcRegion.width() * (size_t)calcRegion.height(), GatherCell{});
  gatherStableColumns(calcRegion);
}

// THE GATHER ORACLE (E04). The stable-grid cache claims that a grid retained across frames and patched at
// its edges equals one gathered from scratch. Nothing checked that claim. The render gate cannot: it freezes
// the world, so a frozen camera only ever exercises the cache-HIT path, and no unit test can model sector
// residency faithfully enough to be worth believing. This rebuilds the grid in full against the real world
// and compares, on whichever path just ran.
//
// OBSERVE-ONLY: the cached grid is what ships. Arming this changes cost and nothing else, so a run with it on
// renders exactly what a run with it off renders -- which is the only way its verdict means anything about
// the shipping path.
//
// It is also the only instrument that can see the known residual: the cache key is (epoch, dims, anchor) and
// does NOT track sector load/unload -- unloadSector bumps no epoch -- so a cell retained through a scroll
// whose sector has since unloaded keeps its last-gathered value while a fresh gather reads it as absent.
// #225 MEASUREMENT: was this epoch bump for a tile the lighting even reads? The region is published by the
// lighting thread (relaxed), so this is a snapshot that may be a frame or two old -- fine for a statistic,
// and NOT a basis for skipping a bump, because a stale region would silently drop a real invalidation.
//
// Before the first gather the published region is degenerate (both words zero); that reads as "off region"
// for every position except the origin, so the early samples are counted rather than silently dropped, and
// the counts are read over a settled window where they are a rounding error.
bool WorldClient::epochBumpOffRegion(Vec2I const& pos) const {
  Vec2I mn = unpackVec2I(m_lightingCalcMinPacked.load(std::memory_order_relaxed));
  Vec2I mx = unpackVec2I(m_lightingCalcMaxPacked.load(std::memory_order_relaxed));
  // The world wraps in x, and the calc region is expressed in the calculator's UNWRAPPED coordinates while
  // a packet position is wrapped. Comparing them directly would misfile every sample near the seam, so ask
  // the geometry rather than the raw ints.
  return !m_geometry.rectContains(RectF(Vec2F(mn[0], mn[1]), Vec2F(mx[0], mx[1])), Vec2F(pos[0], pos[1]));
}

void WorldClient::gatherOracleCompare(char const* path) {
  auto configuration = Root::singleton().configuration();
  if (!configuration->getOrDefault("lightingGatherOracle").optBool().value(false))
    return;

  List<GatherCell> cached = m_gatherGrid;
  lightingStableGather();   // m_gatherGrid := the reference, gathered from scratch over the same region

  if (cached.size() != m_gatherGrid.size()) {
    Logger::error("[gatheroracle] diff={} maxAbs=99.0 size {} vs {} path={}",
      cached.size(), cached.size(), m_gatherGrid.size(), path);
    m_gatherGrid = std::move(cached);
    return;
  }

  size_t differing = 0;
  size_t firstIndex = NPos;
  float maxAbs = 0.0f;
  for (size_t i = 0; i < cached.size(); ++i) {
    GatherCell const& a = cached[i];
    GatherCell const& b = m_gatherGrid[i];
    float worst = 0.0f;
    for (int c = 0; c < 3; ++c) {
      float d = a.stableLight[c] - b.stableLight[c];
      d = d < 0.0f ? -d : d;
      if (d > worst)
        worst = d;
    }
    // The flags are compared too, and separately: a cell can carry identical light while disagreeing about
    // obstacle or sky exposure, and both feed the solve. A light-only comparison would miss that entirely.
    if (worst != 0.0f || a.obstacle != b.obstacle || a.skyExposed != b.skyExposed) {
      if (firstIndex == NPos)
        firstIndex = i;
      ++differing;
      if (worst > maxAbs)
        maxAbs = worst;
    }
  }

  if (differing == 0) {
    Logger::info("[gatheroracle] EXACT (0 diff) path={} cells={}", path, cached.size());
  } else {
    RectI calcRegion = m_lightingCalculator.calculationRegion();
    int height = calcRegion.height();
    Logger::error("[gatheroracle] diff={} maxAbs={:.6f} first=({},{}) path={} cells={}",
      differing, maxAbs,
      calcRegion.min()[0] + (int)(firstIndex / (size_t)height),
      calcRegion.min()[1] + (int)(firstIndex % (size_t)height), path, cached.size());
  }
  m_gatherGrid = std::move(cached);   // ship the cached grid: observe, never correct
}

void WorldClient::gatherStableColumns(RectI const& region) {
  RectI calcRegion = m_lightingCalculator.calculationRegion();
  int height = calcRegion.height();
  Vec2I calcMin = calcRegion.min();
  // Indexing is relative to the calc region, matching baseIndexFor, so this serves both the full
  // gather and the A2 margin -- the grid is always aligned to calcMin. environmentLight is EXCLUDED
  // here and carried as the skyExposed bit, which is what makes the grid reusable across frames;
  // applyStableToCells re-applies it per frame.
  gatherColumns(region,
    [&](Vec2I const& pos, Vec3F const* light, bool const* obstacle, bool const* skyExposed, size_t ySize) {
      size_t baseIndex = (size_t)(pos[0] - calcMin[0]) * (size_t)height + (size_t)(pos[1] - calcMin[1]);
      for (size_t y = 0; y < ySize; ++y) {
        GatherCell& gc = m_gatherGrid[baseIndex + y];
        gc.stableLight = light[y];
        gc.obstacle = obstacle[y] ? 1 : 0;
        gc.skyExposed = skyExposed[y] ? 1 : 0;
      }
    });
}

void WorldClient::shiftAndGatherMargin(int dx, int dy) {
  RectI calcRegion = m_lightingCalculator.calculationRegion();
  int width = calcRegion.width();
  int height = calcRegion.height();
  Vec2I calcMin = calcRegion.min();
  // WHAT SURVIVES THE SHIFT AND WHAT IS NEWLY EXPOSED comes from gridScroll (StarGridScroll.hpp) -- pure
  // integer geometry, unit-tested in core_tests over both signs, zero, one-cell, mid-grid and beyond-extent
  // deltas, against a from-scratch rebuild. It was inline here and untested, on a path E02 then measured at
  // 5.5% of recomputes while walking and none while standing still: rare enough that a sign error would have
  // reached the Director long before it reached a test.
  GridScroll scroll = gridScroll(dx, dy, width, height);

  // ONLY THE VACATED L IS CLEARED, and the asymmetry is the point. The copy below writes every cell of the
  // overlap unconditionally, so clearing that part first is dead work -- E02 measured the scale: 163 margin
  // cells of a 35,840-cell grid, meaning the assign() this replaces was clearing ~200x the region that
  // needed it.
  //
  // The MARGIN clear is not dead and must stay. tileEvalColumnsParallel clamps away cells in unloaded
  // sectors, so a margin cell whose sector is not resident is never written by the gather below and has to
  // read as {0 light, not-obstacle, not-sky} -- the value begin() leaves it in the direct gather, and the
  // reason lightingStableGather zeroes before gathering too.
  //
  // resize, not assign: the scratch holds the previous grid and only the covered cells are rewritten. That
  // is safe because overlap plus the two margins cover the grid, which is asserted in grid_scroll_test
  // rather than argued here -- and it is what lets a dims change (which resizes) still leave no stale cell.
  //
  // Not a performance change. The scroll path runs 5.5% of recomputes while walking and none standing
  // still; expect no measurable frame effect.
  m_gatherScratch.resize((size_t)width * (size_t)height);
  auto clear = [&](RectI const& r) {
    if (r.isEmpty())
      return;
    for (int nx = r.min()[0]; nx < r.max()[0]; ++nx) {
      GatherCell* col = m_gatherScratch.ptr() + (size_t)nx * (size_t)height;
      for (int ny = r.min()[1]; ny < r.max()[1]; ++ny)
        col[ny] = GatherCell{};
    }
  };
  clear(scroll.marginX);
  clear(scroll.marginY);

  // Double-buffer shift: copy the overlap (cells present in BOTH the old and new grid) from
  // m_gatherGrid into the scratch at the shifted position, then swap. Disjoint buffers, so
  // any column order is safe (no in-place memmove ordering hazard, and the dy intra-column move is a
  // plain slice copy). World tile at new index (nx, ny) was at old index (nx + dx, ny + dy).
  if (!scroll.overlap.isEmpty()) {
    int nyStart = scroll.overlap.min()[1];
    int copyLen = scroll.overlap.height();
    GatherCell const* gridPtr = m_gatherGrid.ptr();
    GatherCell* scratchPtr = m_gatherScratch.ptr();
    for (int nx = scroll.overlap.min()[0]; nx < scroll.overlap.max()[0]; ++nx) {
      GatherCell const* src = gridPtr + (size_t)(nx + dx) * (size_t)height + (size_t)(nyStart + dy);
      GatherCell* dst = scratchPtr + (size_t)nx * (size_t)height + (size_t)nyStart;
      for (int k = 0; k < copyLen; ++k)
        dst[k] = src[k]; // trivially-copyable GatherCell -> the compiler lowers this to a memcpy
    }
  }
  std::swap(m_gatherGrid, m_gatherScratch); // O(1) buffer-pointer swap (List::swap is element-swap)

  // Gather the L-shaped margin (cells new in x OR new in y) into the shifted grid. Rect A = the |dx|
  // newly-exposed columns (full height); Rect B = the |dy| newly-exposed rows (full width). Their
  // union is exactly the margin; they overlap only in the corner (gathered twice, identical value).
  // Neither rect touches the shifted overlap, so no retained cell is clobbered.
  //
  // HOW MUCH of the grid a scroll re-gathers. lighting.gather.scroll says this path RAN; against
  // lighting.calc.cells this says whether it was a thin strip or nearly a full re-gather. Counted as WORK
  // DONE, so the shared corner counts twice -- exactly as the two gathers below process it. Measured from
  // the rects themselves rather than recomputed from the deltas, so there is one source for the geometry.
  static auto marginCells = Telemetry::counter("lighting.gather.margin_cells",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  auto area = [](RectI const& r) -> uint64_t {
    return r.isEmpty() ? 0 : (uint64_t)r.width() * (uint64_t)r.height();
  };
  marginCells.inc(area(scroll.marginX) + area(scroll.marginY));

  // Grid indices to world tiles. The two rects meet only at the corner, which is gathered twice with an
  // identical result, and neither touches the retained overlap -- both properties are asserted in
  // grid_scroll_test rather than argued here.
  if (!scroll.marginX.isEmpty())
    gatherStableColumns(RectI(scroll.marginX.min() + calcMin, scroll.marginX.max() + calcMin));
  if (!scroll.marginY.isEmpty())
    gatherStableColumns(RectI(scroll.marginY.min() + calcMin, scroll.marginY.max() + calcMin));
}

void WorldClient::applyStableToCells() {
  Vec3F environmentLight = m_sky->environmentLight().toRgbF();
  RectI calcRegion = m_lightingCalculator.calculationRegion();
  int width = calcRegion.width();
  int height = calcRegion.height();
  // Write the calc cells from the stable grid, re-applying the current-frame environmentLight to
  // sky-exposed cells. Reconstructs exactly the light the direct gather would have produced this
  // frame. Process WorldSectorSize-tall chunks so setCellColumn resolves the monochrome/Either
  // branch once per chunk (a grid column may exceed the sector size).
  Vec3F colLight[WorldSectorSize];
  bool colObstacle[WorldSectorSize];
  for (int x = 0; x < width; ++x) {
    size_t colBase = (size_t)x * (size_t)height;
    for (int y0 = 0; y0 < height; y0 += (int)WorldSectorSize) {
      int n = height - y0 < (int)WorldSectorSize ? height - y0 : (int)WorldSectorSize;
      for (int k = 0; k < n; ++k) {
        GatherCell const& gc = m_gatherGrid[colBase + (size_t)(y0 + k)];
        colLight[k] = gc.skyExposed ? gc.stableLight + environmentLight : gc.stableLight;
        colObstacle[k] = gc.obstacle != 0;
      }
      m_lightingCalculator.setCellColumn(colBase + (size_t)y0, colLight, colObstacle, (size_t)n);
    }
  }
}

void WorldClient::lightingCalc() {
  // Phase timers (deep-gated; TelemetryScope records only under deep tracing). The total
  // scope begins AFTER the early-out so no-op wakeups (no pending light) are not timed.
  // totalScope opens after the m_pendingLightReady early-out below, but that guard passes on nearly every
  // frame in a live sim -- the real gate (recompute vs skip) is the temporal-decoupling check further down,
  // AFTER this scope has already started timing. So this metric genuinely measures a per-FRAME cost (the
  // whole lightingCalc call, including frames the temporal gate skips the expensive part on), not a
  // per-recompute one. gatherTimer below stays Recompute: it sits inside the gate and only fires when a
  // recompute actually happens (confirmed 2026-07-25: its windowed count matched lighting.temporal.recomputed
  // exactly, while totalTimer's matched cpu.frame.total.us's frame count exactly).
  static auto totalTimer = Telemetry::timer("lighting.cpu.total.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Frame, MetricRole::Total});
  static auto gatherTimer = Telemetry::timer("lighting.cpu.gather.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // The phases below are CONTIGUOUS and EXHAUSTIVE across the body of this function: every microsecond
  // between totalScope opening and the closing brace lands in exactly one of them. That is what makes the
  // closure exact rather than approximate. If you add a statement here, it goes INSIDE a phase.
  //
  // prologue is the only Frame-cadence part. It covers the work paid on EVERY frame, including the ones
  // the temporal gate skips -- which is precisely why lighting.cpu.total.us can stay Frame-cadence while
  // everything else is Recompute: the consumer scales each part against its OWN cadence, so a mixed-cadence
  // parts list closes against a frame-cadence whole exactly. (Two Totals per owner are not representable:
  // StarTelemetry.cpp's owner table is keyed by owner name and the last row wins.)
  static auto prologueTimer = Telemetry::timer("lighting.cpu.prologue.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Frame, MetricRole::Budget});
  static auto paramsTimer = Telemetry::timer("lighting.cpu.params.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto beginTimer = Telemetry::timer("lighting.cpu.begin.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto lightsTimer = Telemetry::timer("lighting.cpu.lights.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto exportTimer = Telemetry::timer("lighting.cpu.export.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  static auto convertTimer = Telemetry::timer("lighting.cpu.convert.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // Named 'calculate', not 'calc': lighting.cpu.calc.{ran,skipped} already exist as counters and the
  // prefix collision reads as a type conflict even though it is not one.
  static auto calculateTimer = Telemetry::timer("lighting.cpu.calculate.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // Includes the m_lightMapMutex acquisition, not just the moves. waitForLighting() holds that mutex
  // across a preview-tile patch loop on the render thread, so the wait is real and can stall -- but it is
  // genuine wall-clock cost on the lighting thread and must be inside a part for closure to hold. A fat
  // number here means CONTENTION, which is a different lever from anything else in this budget.
  static auto publishTimer = Telemetry::timer("lighting.cpu.publish.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Budget});
  // The true O(lights) denominator. lighting.lights.{spread,point} count ADDS, and a promoted light adds
  // one of each, so neither is the source count.
  static auto lightSourceCounter = Telemetry::counter("lighting.lights.sources",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  static auto temporalRecomputed = Telemetry::counter("lighting.temporal.recomputed",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  static auto temporalSkipped = Telemetry::counter("lighting.temporal.skipped",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  // UP HERE WITH THE REST, above BOTH early returns. These two used to register beside the calculate
  // phase, which two returns dominate -- the m_pendingLightReady check just below, and the temporal gate's
  // calm-scene return. A process whose temporal gate skipped every recompute never reached them, so the one
  // pair that says whether the CPU solve was actually skipped under GPU lighting read ABSENT, and a
  // consumer could not tell "no recomputes happened" from "the CPU path never ran". Every other timer and
  // counter in this function was already hoisted here for exactly that reason; these two were missed.
  static auto calcRan = Telemetry::counter("lighting.cpu.calc.ran",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
  static auto calcSkipped = Telemetry::counter("lighting.cpu.calc.skipped",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});

  MutexLocker prepLocker(m_lightMapPrepMutex);
  if (!m_pendingLightReady.load())
    return;
  auto& root = Root::singleton();
  TelemetryScope totalScope(totalTimer);

  // Declared out here because they must outlive the prologue block; assigned inside it so the prologue
  // phase actually covers the moves and the config fetch.
  RectI lightRange;
  List<LightSource> lights;
  List<std::pair<Vec2F, Vec3F>> particleLights;
  ConfigurationPtr configuration;
  {
    TelemetryScope prologueScope(prologueTimer);
    m_pendingLightReady = false;
    lightRange = m_pendingLightRange;
    lights = std::move(m_pendingLights);
    particleLights = std::move(m_pendingParticleLights);
    configuration = root.configuration();

    // --- Temporal lighting decoupling: skip this recompute when the scene is calm (only flicker /
    // particle-motion / ambient changed) and we are between the floor cadence; nothing is republished, so
    // the render thread reuses the previously-published lightmap. Off (flag off / floorMs<=0) => recompute
    // every frame (byte-identical). Flicker-tolerant: the activity signature ignores light colour.
    // NOTE this whole block, including signatureOf's allocate-and-std::sort over every light, runs on
    // EVERY frame -- it is the cost the prologue phase exists to measure. ---
    bool temporalEnabled = configuration->get("lightingTemporalDecouple").optBool().value(true);
    double temporalFloorMs = configuration->get("lightingTemporalFloorMs", 33.0).toDouble();
    int64_t nowMs = Time::monotonicMilliseconds();
    uint64_t epoch = m_lightingTileEpoch.load(std::memory_order_relaxed);
    auto sig = TemporalLightingGate::signatureOf(lights);
    if (!TemporalLightingGate::shouldRecompute(
            m_temporalBaseline, temporalEnabled, temporalFloorMs, epoch, lightRange, sig, nowMs)) {
      temporalSkipped.inc(1);
      return; // calm -> reuse the previously-published lightmap (both scopes close via RAII)
    }
    temporalRecomputed.inc(1);
    m_temporalBaseline = {true, epoch, lightRange, std::move(sig), nowMs};
  }

  // lightingGpu and lightingGpuShadowCompare are read HERE, once, rather than again further down. Two
  // benefits: it closes the gap between the lights and export phases (contiguity), and it removes a real
  // latent inconsistency -- lightingGpu was read at two points with the whole gather between them, so a
  // mid-function /command-set flip could pair a promote decision with the opposite export decision.
  bool newLighting = false;
  bool lightingGpu = false;
  bool shadowCompare = false;
  {
    TelemetryScope paramsScope(paramsTimer);
    newLighting = configuration->getOrDefault("newLighting").toBool();
    bool monochrome = configuration->getOrDefault("monochromeLighting").toBool();
    lightingGpu = configuration->getOrDefault("lightingGpu").toBool();
    shadowCompare = configuration->getOrDefault("lightingGpuShadowCompare").toBool();
    // An asset reload re-reads /lighting.config without touching newLighting or monochrome, so the value
    // comparison below cannot see it. Pull the reload tracker (atomic exchange) and drop the cache.
    if (m_lightingParamsReloadTracker && m_lightingParamsReloadTracker->pullTriggered())
      m_lightingParamsValid.store(false, std::memory_order_relaxed);
    // Recompose only when an input actually changed. setParameters/setMonochrome are idempotent, so
    // skipping them when nothing changed is byte-identical.
    if (!m_lightingParamsValid.load(std::memory_order_relaxed) || newLighting != m_lightingParamsNewLighting
        || monochrome != m_lightingParamsMonochrome) {
      m_lightingCalculator.setParameters(root.assets()->json("/lighting.config:lighting").set("pointAdditive", newLighting));
      m_lightingCalculator.setMonochrome(monochrome);
      m_lightingParamsValid.store(true, std::memory_order_relaxed);
      m_lightingParamsNewLighting = newLighting;
      m_lightingParamsMonochrome = monochrome;
    }
  }
  {
    TelemetryScope beginScope(beginTimer);

    // ADAPTIVE BORDER (#170). The calculation region is padded by ceil(max(spreadMaxAir, pointMaxAir))
    // = 48, which sizes it for a point light at intensity 1.0 -- the worst case the CONFIG can express.
    // What a scene actually needs is set by the lights it CONTAINS, and only by those OUTSIDE the query
    // region: a light at the centre needs no border at all, because the border exists precisely so that
    // off-region lights can reach in. Measured across four bookmarks the requirement was 20-28 of 48;
    // the clamp inside begin() then holds it at the spread floor of 32, which is -31.4% of the cells
    // every O(cells) lighting phase walks.
    //
    // Recomputed every recompute, so a bright distant light appearing pushes the border straight back
    // up on the next one. An OVER-estimate is free (begin() clamps it to the historic 48); an UNDER-
    // estimate deletes point lights, because the border is what decides whether an off-region light is
    // in the grid at all. That asymmetry is why the per-light arithmetic belongs to the calculator --
    // this loop used to compute reach from the channel mean while the engine used the channel max, and
    // silently dropped saturated lights 32-48 cells out (#217).
    //
    // Kill-switch, default ON: lightingAdaptiveBorder.
    Maybe<unsigned> adaptiveBorder;
    if (configuration->get("lightingAdaptiveBorder", true).optBool().value(true)) {
      float pointMaxAir = root.assets()->json("/lighting.config:lighting").getFloat("pointMaxAir");
      unsigned needed = 0;
      for (auto const& l : lights) {
        // Wrap first, as the add loop below does. On an x-wrapping world a seam-adjacent light has an
        // enormous raw distance, fails the reach test, and never constrains the border -- yet the add
        // loop wraps it to within a few cells and expects it to be in the grid. Anchor on the query
        // region because the calculation region does not exist until begin() runs, and the two differ
        // by at most the border, far below the half-world distance that could change which image is
        // nearest.
        Vec2F position = m_geometry.nearestTo(Vec2F(lightRange.min()), l.position);
        needed = max(needed, CellularLightingCalculator::pointBorderFor(lightRange, position, l.color, pointMaxAir));
      }
      adaptiveBorder = needed;
    }
    m_lightingCalculator.begin(lightRange, adaptiveBorder);
  }
  {
    TelemetryScope gatherScope(gatherTimer);
    // A1: when lightingGatherCache is on, reuse the per-frame-invariant stable grid across frames --
    // a cache HIT (tile epoch + calc anchor/dims unchanged) skips the tile gather entirely and only
    // re-applies the current-frame environmentLight. On a MISS we re-gather the stable grid. The
    // entity-light add-loop + exportSpreadInputs below run every frame regardless, so moving/flickering
    // lights are never cached. Flag OFF = the original direct gather (B1+B2) -- the clean A/B baseline.
    //
    // REGISTERED ABOVE THE BRANCH, NOT INSIDE IT. These six used to be declared inside the cached arm,
    // so with lightingGatherCache OFF none of them registered and every key read ABSENT rather than
    // ZERO -- indistinguishable, to a consumer differencing two snapshots, from "no such metric". The
    // lever matrix caught it: it could not compare the off leg to its baseline and correctly VOIDed the
    // leg rather than reporting a delta from a lever it could not prove had engaged. Same bug class
    // StarBackdropPass.hpp already names, and the reason its counters are constructor members.
    //
    // The six still partition the CACHED path by construction -- every arm of the chain below
    // increments exactly one, and the chain has no other exit -- so with the cache on they sum to the
    // recompute count, and with it off they are all zero. That is the witness.
    auto outcome = [](char const* key) {
      return Telemetry::counter(key,
        MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
    };
    static auto gatherHit       = outcome("lighting.gather.hit");
    static auto gatherScroll    = outcome("lighting.gather.scroll");
    static auto gatherFullFirst = outcome("lighting.gather.full.first");
    static auto gatherFullDims  = outcome("lighting.gather.full.dims");
    static auto gatherFullEpoch = outcome("lighting.gather.full.epoch");
    static auto gatherFullJump  = outcome("lighting.gather.full.jump");
    // TWO MORE THAT LIVE DEEPER THAN THIS BRANCH. margin_cells is declared inside
    // shiftAndGatherMargin, which only the SCROLL arm calls -- doubly gated, so a window with the
    // cache on but no scroll still reads ABSENT. calc_outside_loaded is the safety counter licensing
    // the gather cache's sector assumption, and a safety counter that cannot report zero is one that
    // cannot report anything.
    static auto marginCellsReg = outcome("lighting.gather.margin_cells");
    static auto calcOutsideReg = outcome("lighting.gather.calc_outside_loaded");
    (void)marginCellsReg; (void)calcOutsideReg;

    if (configuration->get("lightingGatherCache").optBool().value(true)) {
      int64_t gatherStart = Time::monotonicMicroseconds();
      RectI calcRegion = m_lightingCalculator.calculationRegion();
      Vec2I calcMin = calcRegion.min();
      Vec2I calcDims = Vec2I(calcRegion.width(), calcRegion.height());
      // #225 MEASUREMENT: publish the region so the packet thread can ask whether an epoch bump was for a
      // tile the lighting even reads. lightingCalc runs on the LIGHTING thread (m_lightingCond) while
      // packets are handled on the client thread, so the calculator cannot be read directly from there.
      // Relaxed, and deliberately two independent words: a torn read gives a region that never quite
      // existed, which for a statistic costs at most a misfiled sample and is not worth a lock on this path.
      m_lightingCalcMinPacked.store(packVec2I(calcMin), std::memory_order_relaxed);
      m_lightingCalcMaxPacked.store(packVec2I(calcRegion.max()), std::memory_order_relaxed);
      // The cache key is (tile epoch, anchor, dims) and deliberately does NOT track sector
      // load/unload. That is safe only while the calc region stays inside the loaded-sector region,
      // which this comment used to assert as a fact. It is not one (#210):
      //
      //     loaded    validSectorsFor(window.padded(32).padded(WorldSectorSize=32))  => +64 .. +95
      //     calc min  window -1 -border(<=48)                                        => -49   SAFE
      //     calc max  window +1 +bucketSlack(<=31) +border(<=48)                     => +80   NOT
      //
      // The +31 is #127's grid-size bucket, which rounds the light-window SIZE up to a multiple of
      // 32 anchored at the min corner -- so it grows the region on the MAX SIDE ONLY, while
      // neededSectors stays derived from the UNBUCKETED window.
      //
      // The unload loop now holds m_lightMapPrepMutex, so a breach reads as an absent sector rather
      // than a freed one. This counter says whether it happens at all: zero in play means alignment
      // has been covering us, non-zero means the padding must be derived from the calc region.
      static auto calcOutsideLoaded = Telemetry::counter("lighting.gather.calc_outside_loaded",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
      if (Telemetry::enabled()) {
        for (auto const& s : m_tileArray->validSectorsFor(calcRegion)) {
          if (!m_tileArray->sectorLoaded(s)) {
            calcOutsideLoaded.inc(1);
            break;
          }
        }
      }
      uint64_t tileEpoch = m_lightingTileEpoch.load(std::memory_order_relaxed);
      // WHICH PATH THE CACHE TOOK, AND WHY. A1/A2 were built on the premise that scrolling dominates and that
      // premise has never been measured. The six partition the cached path by construction -- every arm of the
      // chain below increments exactly one and the chain has no other exit -- so the split is verifiable by
      // reading it. Split by reason: "fell back" is not actionable, "fell back because the tile epoch moved" is.

      // Same grid layout (size + tile epoch) as last frame? Then we can reuse it: a HIT (same anchor)
      // skips the gather entirely; a scroll (anchor moved, A2) shifts the overlap + gathers only the
      // newly-exposed margin. Anything else (first frame, zoom/resize/size-breathe, tile edit, or a
      // jump >= the grid size) falls back to a full stable gather.
      char const* gatherPath = "?";   // named for the oracle below, so a mismatch says which path produced it
      bool sameGrid = m_gatherValid && m_gatherDims == calcDims && m_gatherEpoch == tileEpoch;
      if (sameGrid && m_gatherAnchor == calcMin) {
        gatherHit.inc();
        gatherPath = "hit";
        // cache hit: nothing to gather; applyStableToCells re-applies the current env-light below.
      } else if (sameGrid) {
        int dx = calcMin[0] - m_gatherAnchor[0];
        int dy = calcMin[1] - m_gatherAnchor[1];
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        if (adx < calcDims[0] && ady < calcDims[1]) {
          gatherScroll.inc();
          gatherPath = "scroll";
          shiftAndGatherMargin(dx, dy); // A2: scroll -- shift the overlap + gather only the margin
        } else {
          gatherFullJump.inc();
          gatherPath = "full.jump";
          lightingStableGather();       // jump >= grid size: no overlap, full gather
        }
      } else {
        // Ordered, so the six stay mutually exclusive: an invalid grid makes the dims and epoch
        // comparisons meaningless, and a resize makes the epoch one uninteresting.
        if (!m_gatherValid) {
          gatherFullFirst.inc();
          gatherPath = "full.first";
        } else if (m_gatherDims != calcDims) {
          gatherFullDims.inc();
          gatherPath = "full.dims";
        } else {
          gatherFullEpoch.inc();
          gatherPath = "full.epoch";
        }
        lightingStableGather();         // first frame / zoom / resize / size-breathe / tile edit
      }
      m_gatherAnchor = calcMin;
      m_gatherDims = calcDims;
      m_gatherEpoch = tileEpoch;
      m_gatherValid = true;
      gatherOracleCompare(gatherPath);
      applyStableToCells();
      // Mirror lightingTileGather's HUD timer so the gather cost shows in /debug whether the cache is on or off.
      LogMap::set("client_render_world_async_light_gather", strf(u8"{:05d}µs", Time::monotonicMicroseconds() - gatherStart));
    } else {
      lightingTileGather();
      m_gatherValid = false; // re-enabling the cache later must force a fresh gather
    }
  }

  {
    TelemetryScope lightsScope(lightsTimer);
    prepLocker.unlock();
    lightSourceCounter.inc(lights.size());

    // #170 MEASUREMENT PROBE -- how much of the border is actually used.
    //
    // CellularLightArray::borderCells() pads the calculation region by ceil(max(spreadMaxAir,
    // pointMaxAir)) = ceil(max(32, 48)) = 48 tiles on every side. That turns a 128x64 query into a
    // 224x160 calculation, and EVERY O(cells) phase in this budget pays the 4.375x.
    //
    // But 48 is the reach of a light at FULL intensity 1.0 -- the worst case the config can express, not
    // the worst case a scene contains. A light of intensity i reaches i*pointMaxAir. So the border that
    // would actually suffice is ceil(maxIntensity * pointMaxAir), and the gap between that and 48 is the
    // size of this lever. Publish the max so the gap can be MEASURED rather than assumed; the arithmetic
    // stays outside the engine so no config read is added to the hot path.
    //
    // x1000 because gauges are integers. Measured BEFORE the promote step below, so this is the source
    // truth: promotion converts Spread->Point without changing any intensity.
    // TWO gauges, because the naive one answers the wrong question. Max intensity alone measured 0.823
    // at three different worlds -- identical to three decimals, i.e. one ubiquitous source (the player's
    // own light) setting it everywhere. But a light at the CENTRE of the query region needs no border at
    // all: the border exists so lights OUTSIDE the region can reach in, and so off-region geometry can
    // shadow inward. What actually drives the requirement is, for each light that is outside, how far
    // outside it is -- and only for those whose reach still carries them in.
    //
    //   required border = max{ d_i : d_i < i_i * pointMaxAir }
    //     d_i = Chebyshev distance from the query rect out to light i (0 when inside)
    //     i_i = that light's intensity
    static auto maxIntensityGauge = Telemetry::gauge("lighting.lights.max_intensity_x1000",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
    static auto borderNeededGauge = Telemetry::gauge("lighting.border.needed",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Recompute, MetricRole::Detail});
    float maxIntensity = 0.0f;
    int borderNeeded = 0;
    float const pointMaxAir = 48.0f;   // probe-local mirror of /lighting.config:lighting.pointMaxAir
    // Shares pointBorderFor with the live path deliberately. This probe used to carry its own copy of
    // the arithmetic, so when that arithmetic was wrong the gauge reported the same wrong number and
    // could never have revealed it (#217) -- an instrument that duplicates its subject measures nothing.
    for (auto const& l : lights) {
      maxIntensity = max(maxIntensity, l.color.max());
      Vec2F position = m_geometry.nearestTo(Vec2F(lightRange.min()), l.position);
      borderNeeded = max(borderNeeded,
          (int)CellularLightingCalculator::pointBorderFor(lightRange, position, l.color, pointMaxAir));
    }
    // RUNNING MAXIMA, not last-value. A gauge holds whatever was written last, so reading one after a
    // window gives the FINAL recompute's requirement -- which is not a bound and cannot size a border
    // that must never under-serve. The first pass of this probe made exactly that mistake. Track the
    // high-water mark instead; single-threaded on the lighting path, so plain statics suffice.
    static float s_maxIntensitySeen = 0.0f;
    static int s_borderNeededSeen = 0;
    s_maxIntensitySeen = max(s_maxIntensitySeen, maxIntensity);
    s_borderNeededSeen = max(s_borderNeededSeen, borderNeeded);
    maxIntensityGauge.set((int64_t)(s_maxIntensitySeen * 1000.0f));
    borderNeededGauge.set((int64_t)s_borderNeededSeen);

    // CDL (lightingPromoteDynamic): promote static fill (Spread) lights to dynamic by a fraction
    // p in [0,1] -- (1-p) soft spread + p full directional point. p=0 off (Spread unchanged,
    // byte-identical); p~0.15 ~= the old hybrid; p=1 full Point (the mod's look). Gated on lightingGpu:
    // in confirmed GPU mode the CPU calculate() below is skipped, so this feeds the GPU point pass
    // without flooding the CPU raycast. Non-Spread lights (already Point/PointAsSpread, incl. mod-set)
    // are untouched -- no double-promote.
    float promoteFraction = 0.0f;
    float promoteMinIntensity = 0.0f;
    if (lightingGpu) {
      // Read defensively: an interim build persisted this key as a bool, so coerce bool->fraction
      // (true=>0.5, false=>0) rather than throwing toFloat() on a type-mismatched persisted value.
      Json pd = configuration->get("lightingPromoteDynamic");
      promoteFraction = pd.isType(Json::Type::Bool) ? (pd.toBool() ? 0.5f : 0.0f) : pd.optFloat().value(0.0f);
      // Floor below which a Spread light is NOT promoted to a dynamic point: ultra-dim fill lights
      // (e.g. item drops at 20/255 ~= 0.078) gain nothing from sharp point rendering and flicker on a
      // jittery emitter -> keep them soft spreads. 0 disables the floor (promote everything).
      promoteMinIntensity = configuration->get("lightingPromoteMinIntensity", 0.1f).toFloat();
    }
    promoteFraction = promoteFraction < 0.0f ? 0.0f : (promoteFraction > 1.0f ? 1.0f : promoteFraction);

    for (auto const& light : lights) {
      Vec2F position = m_geometry.nearestTo(Vec2F(m_lightingCalculator.calculationRegion().min()), light.position);
      // Promote only "feature" Spread lights: skip the floor (item-drop-class fill) -> pure spread.
      bool promote = promoteFraction > 0.0f && light.color.max() >= promoteMinIntensity;
      if (light.type == LightType::Spread && promote) {
        if (promoteFraction < 1.0f)
          m_lightingCalculator.addSpreadLight(position, light.color * (1.0f - promoteFraction));
        m_lightingCalculator.addPointLight(position, light.color * promoteFraction, light.pointBeam, light.beamAngle, light.beamAmbience);
      } else if (light.type == LightType::Spread) {
        m_lightingCalculator.addSpreadLight(position, light.color);
      } else {
        if (light.type == LightType::PointAsSpread) {
          if (!newLighting)
            m_lightingCalculator.addSpreadLight(position, light.color);
          else { // hybrid (used for auto-converted object lights) - 85% spread, 15% point (* .15 is applied in the calculation code)
            m_lightingCalculator.addSpreadLight(position, light.color * 0.85f);
            m_lightingCalculator.addPointLight(position, light.color, light.pointBeam, light.beamAngle, light.beamAmbience, true);
          }
        } else {
          m_lightingCalculator.addPointLight(position, light.color, light.pointBeam, light.beamAngle, light.beamAmbience);
        }
      }
    }

    for (auto const& lightPair : particleLights) {
      Vec2F position = m_geometry.nearestTo(Vec2F(m_lightingCalculator.calculationRegion().min()), lightPair.first);
      m_lightingCalculator.addSpreadLight(position, lightPair.second);
    }
  }

  // GPU lighting (Slice 2/3): when the lightingGpu flag is on, export the seeded emission + obstacle
  // grids and the point-light list for the GPU passes. exportSpreadInputs must run BEFORE calculate(),
  // which overwrites the cells with the spread result.
  //
  // Both scopes ENCLOSE their `if` rather than sitting inside it. A scope inside the branch would have to
  // be Cadence::Call to stop coverage_scale inflating it when GPU lighting is off; enclosing it means the
  // phase fires on every recompute, reports 100% coverage, and simply records ~0 when the branch is not
  // taken. That keeps the whole owner free of coverage scaling, which is where this campaign's arithmetic
  // errors have repeatedly come from. Cost: one predictable branch test.
  //
  // export and convert are separate phases because they have DIFFERENT scaling laws and different levers:
  // export is a cache-hostile transposing scatter over the calc region, convert is a compute-bound
  // per-element conversion over 3x that count.
  int lightMapBorder = 0;
  {
    TelemetryScope exportScope(exportTimer);
    if (lightingGpu) {
      m_lightingCalculator.exportSpreadInputs(m_pendingLightingEmission, m_pendingLightingObstacle);
      m_lightingCalculator.exportPointLights(m_pendingLightingPointLights);
      // Border (cells) between the calc-region-sized GPU result and the query region the world shader
      // samples. calculationRegion == queryRegion(lightRange).padded(borderCells), so this is exactly
      // borderCells. Computed here from the calculator's geometry and carried in renderData; WorldPainter
      // must NOT reverse-derive it from the CPU lightMap width, which is empty when the CPU calc is skipped.
      lightMapBorder = ((int)m_lightingCalculator.calculationRegion().width() - (int)lightRange.width()) / 2;
    }
  }
  {
    TelemetryScope convertScope(convertTimer);
    if (lightingGpu) {
      // Convert the RGB_F emission grid to 16-bit half-floats HERE (lighting thread, idle) so the render
      // thread uploads RGB16F -- half the per-frame transfer/store. No precision loss (the spread FBOs
      // are already 16F). The RGB_F emission is still kept for the auto-K scan + shadow-compare reference.
      {
        float const* ef = (float const*)m_pendingLightingEmission.data();
        size_t n = (size_t)m_pendingLightingEmission.size()[0] * m_pendingLightingEmission.size()[1] * 3;
        m_pendingLightingEmissionHalf.resize(n);
        uint16_t* hf = m_pendingLightingEmissionHalf.ptr();
        for (size_t i = 0; i < n; ++i)
          hf[i] = floatToHalf(ef[i]);
      }
      // Extract the obstacle mask's R channel (RGB24 0/255) into a single-channel R8 buffer so the GPU
      // upload is R8 (a third the bytes); the shaders already read obstacle as .r.
      {
        uint8_t const* ob = (uint8_t const*)m_pendingLightingObstacle.data();
        size_t cells = (size_t)m_pendingLightingObstacle.size()[0] * m_pendingLightingObstacle.size()[1];
        m_pendingLightingObstacleR8.resize(cells);
        uint8_t* r8 = m_pendingLightingObstacleR8.ptr();
        for (size_t i = 0; i < cells; ++i)
          r8[i] = ob[i * 3];   // R channel of each RGB24 texel
      }
    }
  }

  // Slice 4: in confirmed GPU mode the GPU produces the COMPLETE lightmap from the
  // exported grids, so the CPU calculate() (spread sweep + point raycast, the bulk of
  // the CPU lighting cost) is pure redundant work -- skip it. Guards:
  //  - m_gpuLightingActive: only skip once the render thread has confirmed a successful
  //    GPU pass. The first lighting frame (latch off) runs the CPU path so a valid
  //    m_lightMap exists for the fallback; if a later GPU frame fails, the render thread
  //    reports false and the CPU path re-arms within ~1 frame (self-healing).
  //  - !shadowCompare: shadow-compare keeps the CPU lightMap as the parity reference.
  // calcRan / calcSkipped are registered at the top of this function, above both early returns.
  {
    TelemetryScope calculateScope(calculateTimer);
    bool skipCpuCalc = lightingGpu && !shadowCompare && m_gpuLightingActive.load(std::memory_order_relaxed);
    if (skipCpuCalc) {
      calcSkipped.inc(1);
    } else {
      m_lightingCalculator.calculate(m_pendingLightMap);
      calcRan.inc(1);
    }
  }
  {
    TelemetryScope publishScope(publishTimer);
    MutexLocker mapLocker(m_lightMapMutex);
    // SWAP the five GPU buffers, not move. Moving leaves the pending ones empty (Image::operator=(Image&&)
    // takes the source's data AND its dimensions; vector move leaves capacity 0), so next recompute every
    // reset()/resize() below misses its same-size early-out and re-allocates + ZERO-FILLS ~788 KB that is
    // then immediately overwritten in full. Swapping hands last frame's correctly-sized buffers back.
    //
    // m_lightMap KEEPS its move -- do NOT swap it. It is the one buffer that is not rewritten every
    // recompute: calculate() is skipped in GPU mode, so m_pendingLightMap stays empty, and moving is what
    // makes m_lightMap empty too. That emptiness is load-bearing -- see the lightMapBorder note in the
    // export phase: WorldPainter must not reverse-derive the border from a CPU lightMap width, and an
    // empty map is how the GPU path signals "there is no CPU lightmap this frame". A swap would hand it
    // stale non-empty data from two frames ago and the world would render against the wrong geometry.
    m_lightMinPosition = lightRange.min();
    m_lightMap = std::move(m_pendingLightMap);
    m_lightingInputsValid = lightingGpu;
    if (lightingGpu) {
      std::swap(m_lightingEmission, m_pendingLightingEmission);
      std::swap(m_lightingObstacle, m_pendingLightingObstacle);
      std::swap(m_lightingPointLights, m_pendingLightingPointLights);
      std::swap(m_lightingEmissionHalf, m_pendingLightingEmissionHalf);
      std::swap(m_lightingObstacleR8, m_pendingLightingObstacleR8);
      m_lightingBorder = lightMapBorder;
      m_lightingInputsFresh = true;
    }
  }
}

void WorldClient::lightingMain() {
  MutexLocker condLocker(m_lightingMutex);
  while (true) {
    m_lightingCond.wait(m_lightingMutex);
    if (m_stopLightingThread)
      return;

    int64_t start = Time::monotonicMicroseconds();
    lightingCalc();
    LogMap::set("client_render_world_async_light_calc", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - start));
  }
}

void WorldClient::initWorld(WorldStartPacket const& startPacket) {
  clearWorld();
  m_outgoingPackets.append(make_shared<WorldStartAcknowledgePacket>());

  auto assets = Root::singleton().assets();
  if (startPacket.localInterpolationMode)
    m_interpolationTracker = InterpolationTracker(m_clientConfig.query("interpolationSettings.local"));
  else
    m_interpolationTracker = InterpolationTracker(m_clientConfig.query("interpolationSettings.normal"));

  m_entityUpdateTimer = GameTimer(m_interpolationTracker.entityUpdateDelta());

  m_clientId = startPacket.clientId;
  m_mainPlayer->clientContext()->setConnectionId(startPacket.clientId);
  auto entitySpace = connectionEntitySpace(startPacket.clientId);
  m_worldTemplate = make_shared<WorldTemplate>(startPacket.templateData);
  m_entityMap = make_shared<EntityMap>(m_worldTemplate->size(), entitySpace.first, entitySpace.second);
  m_tileArray = make_shared<ClientTileSectorArray>(m_worldTemplate->size());
  m_tileGetterFunction = [&, tile = ClientTile()](Vec2I pos) mutable -> ClientTile const& {
    if (!m_predictedTiles.empty()) {
      if (auto p = m_predictedTiles.ptr(pos)) {
        p->apply(tile = m_tileArray->tile(pos));
        if (p->liquid) {
          if (p->liquid->liquid == tile.liquid.liquid)
            tile.liquid.level += p->liquid->level;
          else {
            tile.liquid.liquid = p->liquid->liquid;
            tile.liquid.level = p->liquid->level;
          }
        }
        return tile;
      }
    }
    return m_tileArray->tile(pos);
  };
  m_damageManager = make_shared<DamageManager>(this, startPacket.clientId);
  m_playerStart = startPacket.playerRespawn;
  m_respawnInWorld = startPacket.respawnInWorld;
  m_worldProperties = startPacket.worldProperties.optObject().value();
  m_dungeonIdGravity = startPacket.dungeonIdGravity;
  m_dungeonIdBreathable = startPacket.dungeonIdBreathable;
  m_protectedDungeonIds = startPacket.protectedDungeonIds;

  m_geometry = WorldGeometry(m_worldTemplate->size());

  m_particles = make_shared<ParticleManager>(m_geometry, m_tileArray);
  m_particles->setUndergroundLevel(m_worldTemplate->undergroundLevel());

  setupForceRegions();

  m_sky = make_shared<Sky>();
  m_sky->readUpdate(startPacket.skyData, m_clientState.netCompatibilityRules());

  m_weather.setup(m_geometry, [this](Vec2I const& pos) {
      auto const& tile = m_tileArray->tile(pos);
      return !isRealMaterial(tile.background) && !isSolidColliding(tile.getCollision());
    });
  m_weather.readUpdate(startPacket.weatherData, m_clientState.netCompatibilityRules());

  // These two calls reconfigure the calculator behind lightingCalc's parameter cache -- and note they set
  // the parameters WITHOUT the "pointAdditive" override lightingCalc composes in. Drop the cache so the
  // first recompute in the new world re-composes rather than trusting a stale hit from the previous one.
  // This is the MAIN thread and clearWorld does not stop the lighting thread, hence the atomic.
  m_lightingParamsValid.store(false, std::memory_order_relaxed);
  m_lightingCalculator.setMonochrome(Root::singleton().configuration()->get("monochromeLighting").toBool());
  m_lightingCalculator.setParameters(assets->json("/lighting.config:lighting"));
  m_lightIntensityCalculator.setParameters(assets->json("/lighting.config:intensity"));

  m_inWorld = true;
  
  if (!m_mainPlayer->isDead()) {
    m_mainPlayer->init(this, m_entityMap->reserveEntityId(), EntityMode::Master);
    m_entityMap->addEntity(m_mainPlayer);
  }
  m_mainPlayer->moveTo(startPacket.playerStart);
  if (const auto& parameters = m_worldTemplate->worldParameters())
    m_mainPlayer->overrideTech(parameters->overrideTech);
  else
    m_mainPlayer->overrideTech({});

  // Auto reposition the client window on the player when the main player
  // changes position.
  centerClientWindowOnPlayer();
}

void WorldClient::clearWorld() {
  if (m_entityMap) {
    while (m_entityMap->size() > 0) {
      for (auto entityId : m_entityMap->entityIds())
        removeEntity(entityId, false);
    }
  }

  waitForLighting();

  m_currentStep = 0;
  m_currentTime = 0;
  m_inWorld = false;
  m_clientId.reset();

  m_interpolationTracker = InterpolationTracker();

  m_masterEntitiesNetVersion.clear();
  m_outgoingPackets.clear();

  m_pingTime.reset();

  m_entityMap.reset();
  m_worldTemplate.reset();
  m_worldProperties.clear();

  m_tileArray.reset();
  m_gatherValid = false; // A1/A2: drop the stable tile-gather cache so a reused WorldClient (world hop) can't reapply the previous world's lighting for a frame

  m_damageManager.reset();

  m_particles.reset();

  m_sky.reset();

  m_currentParallax.reset();
  m_nextParallax.reset();
  m_parallaxFadeTimer.setDone();

  m_clientState.reset();
  m_ambientSounds.cancelAll();
  m_musicTrack.cancelAll();
  m_musicTrack.setVolume(1, 0, 0);
  m_altMusicTrack.cancelAll();
  m_altMusicTrack.setVolume(0, 0, 0);
  m_altMusicActive = false;

  if (m_spaceSound) {
    m_spaceSound->stop();
    m_spaceSound = {};
  }

  m_entityMessageResponses = {};
  m_findUniqueEntityResponses = {};

  m_forceRegions.clear();
}

void WorldClient::tryGiveMainPlayerItem(ItemPtr item, bool silent) {
  if (auto spill = m_mainPlayer->pickupItems(item, silent))
    addEntity(ItemDrop::createRandomizedDrop(spill->descriptor(), m_mainPlayer->position()));
}

void WorldClient::notifyEntityCreate(EntityPtr const& entity) {
  if (entity->isMaster() && !m_masterEntitiesNetVersion.contains(entity->entityId())) {
    // Server was unaware of this entity until now
    auto netRules = m_clientState.netCompatibilityRules();
    auto firstNetState = entity->writeNetState(0, netRules);
    m_masterEntitiesNetVersion[entity->entityId()] = firstNetState.second;
    m_outgoingPackets.append(make_shared<EntityCreatePacket>(entity->entityType(),
      Root::singleton().entityFactory()->netStoreEntity(entity, netRules), std::move(firstNetState.first), entity->entityId()));
  }
}

Vec2I WorldClient::environmentBiomeTrackPosition() const {
  if (!inWorld())
    return {};

  auto pos = Vec2I::floor(m_clientState.windowCenter());
  return {m_geometry.xwrap(pos[0]), pos[1]};
}

AmbientNoisesDescriptionPtr WorldClient::currentAmbientNoises() const {
  if (!inWorld())
    return {};

  Vec2I pos = environmentBiomeTrackPosition();
  return m_worldTemplate->ambientNoises(pos[0], pos[1]);
}

WeatherNoisesDescriptionPtr WorldClient::currentWeatherNoises() const {
  if (!inWorld())
    return {};

  auto trackOptions = m_weather.weatherTrackOptions();
  if (trackOptions.empty())
    return {};
  else
    return make_shared<WeatherNoisesDescription>(std::move(trackOptions));
}

AmbientNoisesDescriptionPtr WorldClient::currentMusicTrack() const {
  if (!inWorld())
    return {};

  Vec2I pos = environmentBiomeTrackPosition();
  return m_worldTemplate->musicTrack(pos[0], pos[1]);
}

AmbientNoisesDescriptionPtr WorldClient::currentAltMusicTrack() const {
  if (!inWorld())
    return {};

  return m_altMusicTrackDescription;
}

void WorldClient::playAltMusic(StringList const& newTracks, float fadeTime, int loops) {
  auto newTrackGroup = AmbientTrackGroup(newTracks);
  m_altMusicTrackDescription = make_shared<AmbientNoisesDescription>(AmbientTrackGroup(newTracks), AmbientTrackGroup(), loops);
  if (!m_altMusicActive) {
    m_musicTrack.setVolume(0.0, 0.0, fadeTime);
    m_altMusicTrack.setVolume(1.0, 0.0, fadeTime);
    m_altMusicActive = true;
  }
}

void WorldClient::stopAltMusic(float fadeTime) {
  if (m_altMusicActive) {
    m_musicTrack.setVolume(1.0, 0.0, fadeTime);
    m_altMusicTrack.setVolume(0.0, 0.0, fadeTime);
    m_altMusicActive = false;
  }
}

BiomeConstPtr WorldClient::mainEnvironmentBiome() const {
  if (!inWorld())
    return {};

  Vec2I pos = environmentBiomeTrackPosition();
  return m_worldTemplate->environmentBiome(pos[0], pos[1]);
}

bool WorldClient::readNetTile(Vec2I const& pos, NetTile const& netTile, bool updateCollision) {
  ClientTile* tile = m_tileArray->modifyTile(pos);
  if (!tile)
    return false;

  if (!m_predictedTiles.empty()) {
    auto findPrediction = m_predictedTiles.find(pos);
    if (findPrediction != m_predictedTiles.end()) {
      auto& p = findPrediction->second;

      if (p.collision && *p.collision == netTile.collision)
        p.collision.reset();
      if (p.foreground && (*p.foreground == StructureMaterialId || *p.foreground == netTile.foreground))
        p.foreground.reset();
      if (p.foregroundMod && *p.foregroundMod == netTile.foregroundMod)
        p.foregroundMod.reset();
      if (p.foregroundHueShift && *p.foregroundHueShift == netTile.foregroundHueShift)
        p.foregroundHueShift.reset();
      if (p.foregroundModHueShift && *p.foregroundModHueShift == netTile.foregroundModHueShift)
        p.foregroundModHueShift.reset();

      if (p.background && *p.background == netTile.background)
        p.background.reset();
      if (p.backgroundMod && *p.backgroundMod == netTile.backgroundMod)
        p.backgroundMod.reset();
      if (p.backgroundHueShift && *p.backgroundHueShift == netTile.backgroundHueShift)
        p.backgroundHueShift.reset();
      if (p.backgroundModHueShift && *p.backgroundModHueShift == netTile.backgroundModHueShift)
        p.backgroundModHueShift.reset();

      if (!p)
        m_predictedTiles.erase(findPrediction);
    }
  }

  // #225 MEASUREMENT ONLY -- the bump below stays unconditional and nothing here changes behaviour.
  //
  // Snapshot exactly the members a read-only trace proved can alter what gatherColumns produces:
  // foreground/foregroundMod and background/backgroundMod feed stableLight through radiantLight, liquid
  // feeds it through the liquids database, collision is a term of foregroundLightTransparent, and the two
  // transparency flags carry ALL of obstacle and skyExposed. Everything else this function assigns --
  // both hue shifts, both mod hue shifts, both colour variants, the two biome indices, dungeonId -- was
  // proven unreachable from the gather, so a packet that changes only those is a pure no-op invalidation.
  MaterialId wasForeground = tile->foreground, wasBackground = tile->background;
  ModId wasForegroundMod = tile->foregroundMod, wasBackgroundMod = tile->backgroundMod;
  LiquidLevel wasLiquid = tile->liquid;
  CollisionKind wasCollision = tile->collision;
  bool wasBgTransparent = tile->backgroundLightTransparent;
  bool wasFgTransparent = tile->foregroundLightTransparent;

  tile->background = netTile.background;
  tile->backgroundHueShift = netTile.backgroundHueShift;
  tile->backgroundColorVariant = netTile.backgroundColorVariant;
  tile->backgroundMod = netTile.backgroundMod;
  tile->backgroundModHueShift = netTile.backgroundModHueShift;
  tile->foreground = netTile.foreground;
  tile->foregroundHueShift = netTile.foregroundHueShift;
  tile->foregroundColorVariant = netTile.foregroundColorVariant;
  tile->foregroundMod = netTile.foregroundMod;
  tile->foregroundModHueShift = netTile.foregroundModHueShift;
  tile->collision = netTile.collision;
  tile->blockBiomeIndex = netTile.blockBiomeIndex;
  tile->environmentBiomeIndex = netTile.environmentBiomeIndex;
  tile->liquid = netTile.liquid.liquidLevel();
  tile->dungeonId = netTile.dungeonId;

  auto materialDatabase = Root::singleton().materialDatabase();
  tile->backgroundLightTransparent = materialDatabase->backgroundLightTransparent(tile->background);
  tile->foregroundLightTransparent =
      materialDatabase->foregroundLightTransparent(tile->foreground) && tile->collision != CollisionKind::Dynamic;
  static auto bumpNetTile = Telemetry::counter("lighting.epoch.bump.nettile",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
  static auto bumpNetTileNoop = Telemetry::counter("lighting.epoch.bump.nettile.noop",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
  static auto bumpNetTileOffRegion = Telemetry::counter("lighting.epoch.bump.nettile.offregion",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Lighting, MetricCadence::Call, MetricRole::Detail});
  bumpNetTile.inc();
  if (epochBumpOffRegion(pos))
    bumpNetTileOffRegion.inc();
  if (wasForeground == tile->foreground && wasForegroundMod == tile->foregroundMod
      && wasBackground == tile->background && wasBackgroundMod == tile->backgroundMod
      && wasLiquid.liquid == tile->liquid.liquid && wasLiquid.level == tile->liquid.level
      && wasCollision == tile->collision
      && wasBgTransparent == tile->backgroundLightTransparent
      && wasFgTransparent == tile->foregroundLightTransparent)
    bumpNetTileNoop.inc();

  m_lightingTileEpoch.fetch_add(1, std::memory_order_relaxed); // temporal gate: tile light input changed

  if (updateCollision)
    dirtyCollision(RectI::withSize(pos, {1, 1}));

  return true;
}

void WorldClient::dirtyCollision(RectI const& region) {
  if (!inWorld())
    return;

  auto dirtyRegion = region.padded(CollisionGenerator::BlockInfluenceRadius);
  for (int x = dirtyRegion.xMin(); x < dirtyRegion.xMax(); ++x) {
    for (int y = dirtyRegion.yMin(); y < dirtyRegion.yMax(); ++y) {
      if (auto tile = m_tileArray->modifyTile({x, y}))
        tile->collisionCacheDirty = true;
    }
  }
}

void WorldClient::freshenCollision(RectI const& region) {
  if (!inWorld())
    return;

  // Lever L4 (mirror of WorldServer::freshenCollision -- keep both identical for
  // master/slave parity): read-only, column-amortized dirty scan via tileEachColumns,
  // which skips invalid/unloaded/out-of-y-range positions exactly like the old
  // per-tile modifyTile guard (so it visits the same dirty set; NOT the const tileEach,
  // whose dirty-by-default m_default would balloon freshenRegion). Byte-identical;
  // pass 2 below still mutates via modifyTile.
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

    for (auto& collisionBlock : m_collisionGenerator.getBlocks(freshenRegion)) {
      if (auto tile = m_tileArray->modifyTile(collisionBlock.space))
        tile->collisionCache.append(std::move(collisionBlock));
    }
  }
}

float WorldClient::lightLevel(Vec2F const& pos) const {
  if (!inWorld())
    return 0.0f;
  return WorldImpl::lightLevel(m_tileArray, m_entityMap, m_geometry, m_worldTemplate, m_sky, m_lightIntensityCalculator, pos);
}

bool WorldClient::breathable(Vec2F const& pos) const {
  if (!inWorld())
    return true;

  return WorldImpl::breathable(this, m_tileArray, m_dungeonIdBreathable, m_worldTemplate, pos);
}

float WorldClient::threatLevel() const {
  if (!inWorld())
    return 0.0f;
  return m_worldTemplate->threatLevel();
}

StringList WorldClient::environmentStatusEffects(Vec2F const& pos) const {
  if (!inWorld())
    return {};

  return m_worldTemplate->environmentStatusEffects(floor(pos[0]), floor(pos[1]));
}

StringList WorldClient::weatherStatusEffects(Vec2F const& pos) const {
  if (!inWorld())
    return {};

  if (!m_weather.statusEffects().empty()) {
     if (exposedToWeather(pos))
      return m_weather.statusEffects();
  }

  return {};
}

bool WorldClient::exposedToWeather(Vec2F const& pos) const {
  if (!inWorld())
    return false;

  if (!isUnderground(pos) && liquidLevel(Vec2I::floor(pos)).liquid == EmptyLiquidId) {
    auto assets = Root::singleton().assets();
    float weatherRayCheckDistance = assets->json("/weather.config:weatherRayCheckDistance").toFloat();
    float weatherRayCheckWindInfluence = assets->json("/weather.config:weatherRayCheckWindInfluence").toFloat();

    auto offset = Vec2F(-m_weather.wind() * weatherRayCheckWindInfluence, weatherRayCheckDistance).normalized() * weatherRayCheckDistance;

    return !lineCollision({pos, pos + offset});
  }

  return false;
}

bool WorldClient::isUnderground(Vec2F const& pos) const {
  if (!inWorld())
    return true;
  return m_worldTemplate->undergroundLevel() >= pos[1];
}

bool WorldClient::disableDeathDrops() const {
  if (const auto& parameters = m_worldTemplate->worldParameters())
    return parameters->disableDeathDrops;
  return false;
}

List<PhysicsForceRegion> WorldClient::forceRegions() const {
  return m_forceRegions;
}

Json WorldClient::getProperty(String const& propertyName, Json const& def) const {
  if (!inWorld())
    return {};

  return m_worldProperties.value(propertyName, def);
}

void WorldClient::setProperty(String const& propertyName, Json const& property) {
  if (!inWorld())
    return;

  if (m_worldProperties[propertyName] == property)
    return;

  m_outgoingPackets.append(make_shared<UpdateWorldPropertiesPacket>(JsonObject{{propertyName, property}}));
}

bool WorldClient::playerCanReachEntity(EntityId entityId, bool preferInteractive) const {
  return (entityId == m_mainPlayer->entityId()) || m_mainPlayer->isAdmin()
    || canReachEntity(m_mainPlayer->position(), m_mainPlayer->interactRadius(), entityId, preferInteractive);
}

void WorldClient::disconnectAllWires(Vec2I wireEntityPosition, WireNode const& node) {
  m_outgoingPackets.append(make_shared<DisconnectAllWiresPacket>(wireEntityPosition, node));
}

void WorldClient::wire(Vec2I const& outputPosition, size_t outputIndex, Vec2I const& inputPosition, size_t inputIndex) {
  WireConnection output = {outputPosition, outputIndex};
  WireConnection input = {inputPosition, inputIndex};
  connectWire(output, input);
}

void WorldClient::connectWire(WireConnection const& output, WireConnection const& input) {
  m_outgoingPackets.append(make_shared<ConnectWirePacket>(output, input));
}

bool WorldClient::sendSecretBroadcast(StringView broadcast, bool raw, bool compress) {
  if (!inWorld() || !m_mainPlayer || !m_mainPlayer->getSecretPropertyView(SECRET_BROADCAST_PUBLIC_KEY))
    return false;

  auto signature = Curve25519::sign((void*)broadcast.utf8Ptr(), broadcast.utf8Size());

  auto damageNotification = make_shared<DamageNotificationPacket>();
  auto& remDmg = damageNotification->remoteDamageNotification;
  auto& dmg = remDmg.damageNotification;

  dmg.targetEntityId = dmg.sourceEntityId = remDmg.sourceEntityId = m_mainPlayer->entityId();
  dmg.damageDealt = dmg.healthLost = 0.0f;
  dmg.hitType = HitType::Hit;
  dmg.damageSourceKind = "nodamage";
  dmg.targetMaterialKind = raw ? broadcast : strf("{}{}{}", SECRET_BROADCAST_PREFIX, StringView((char*)&signature, sizeof(signature)), broadcast);
  dmg.position = m_mainPlayer->position();

  if (!compress)
    damageNotification->setCompressionMode(PacketCompressionMode::Disabled);

  m_outgoingPackets.emplace_back(std::move(damageNotification));
  return true;
}

bool WorldClient::handleSecretBroadcast(PlayerPtr player, StringView broadcast) {
  if (m_broadcastCallback)
    return m_broadcastCallback(player, broadcast);
  else
    return false;
}


void WorldClient::ClientRenderCallback::addDrawable(Drawable drawable, EntityRenderLayer renderLayer) {
  // THE VIEW SINK, and the only place the headless decision is expressed for drawables. The entity still
  // BUILT this drawable -- that work lives inside the entity and is not separable without changing every
  // entity type -- but nothing downstream keeps it. See ClientRenderCallback in the header for why the gate
  // is at the SINK and not around the render() call: that call also emits particles, audio and tile
  // previews, and skipping it would change the world rather than just stop describing it.
  if (!wantView)
    return;
  drawables[renderLayer].append(std::move(drawable));
}

void WorldClient::ClientRenderCallback::addLightSource(LightSource lightSource) {
  lightSources.append(std::move(lightSource));
}

void WorldClient::ClientRenderCallback::addParticle(Particle particle) {
  particles.append(std::move(particle));
}

void WorldClient::ClientRenderCallback::addAudio(AudioInstancePtr audio) {
  audios.append(std::move(audio));
}

void WorldClient::ClientRenderCallback::addTilePreview(PreviewTile preview) {
  previewTiles.append(std::move(preview));
}

void WorldClient::ClientRenderCallback::addOverheadBar(OverheadBar bar) {
  if (!wantView)
    return;
  overheadBars.append(std::move(bar));
}

void WorldClient::setHeadless(bool headless) {
  if (m_headless == headless)
    return;
  m_headless = headless;
  Logger::info("WorldClient: view production {} -- the world still simulates; entities still emit particles, "
               "audio and tile previews, and only drawables and overhead bars are discarded",
    headless ? "OFF (headless)" : "ON");
}

bool WorldClient::headless() const {
  return m_headless;
}

double WorldClient::epochTime() const {
  if (!inWorld())
    return 0;
  return m_sky->epochTime();
}

uint32_t WorldClient::day() const {
  if (!inWorld())
    return 0;
  return m_sky->day();
}

float WorldClient::dayLength() const {
  if (!inWorld())
    return 0;
  return m_sky->dayLength();
}

float WorldClient::timeOfDay() const {
  if (!inWorld())
    return 0;
  return m_sky->timeOfDay();
}

LuaRootPtr WorldClient::luaRoot() {
  return m_luaRoot;
}

RpcPromise<Vec2F> WorldClient::findUniqueEntity(String const& uniqueId) {
  if (!inWorld())
    return RpcPromise<Vec2F>::createFailed("Not currently in a world");

  if (auto entity = m_entityMap->uniqueEntity(uniqueId))
    return RpcPromise<Vec2F>::createFulfilled(entity->position());

  auto pair = RpcPromise<Vec2F>::createPair();
  auto& rpcPromises = m_findUniqueEntityResponses[uniqueId];
  if (rpcPromises.empty())
    m_outgoingPackets.append(make_shared<FindUniqueEntityPacket>(uniqueId));
  rpcPromises.append(pair.second);

  return pair.first;
}

RpcPromise<Json> WorldClient::sendEntityMessage(Variant<EntityId, String> const& entityId, String const& message, JsonArray const& args) {
  if (!inWorld())
    return RpcPromise<Json>::createFailed("Not currently in a world");

  EntityPtr entity;
  if (entityId.is<EntityId>())
    entity = m_entityMap->entity(entityId.get<EntityId>());
  else
    entity = m_entityMap->uniqueEntity(entityId.get<String>());

  // Only fail with "unknown entity" if we know this entity should exist on the
  // client, because it's entity id indicates it is master here.
  if (entityId.is<EntityId>() && !entity && m_clientId == connectionForEntity(entityId.get<EntityId>())) {
    return RpcPromise<Json>::createFailed("Unknown entity");
  } else if (entity && entity->isMaster()) {
    if (auto resp = entity->receiveMessage(*m_clientId, message, args))
      return RpcPromise<Json>::createFulfilled(resp.take());
    else
      return RpcPromise<Json>::createFailed("Message not handled by entity");
  } else {
    auto pair = RpcPromise<Json>::createPair();
    Uuid uuid;
    m_entityMessageResponses[uuid] = pair.second;
    m_outgoingPackets.append(make_shared<EntityMessagePacket>(entityId, message, args, uuid));
    return pair.first;
  }
}

List<ChatAction> WorldClient::pullPendingChatActions() {
  List<ChatAction> result;
  if (m_entityMap) {
    for (auto const& entity : m_entityMap->all<ChattyEntity>())
      result.appendAll(entity->pullPendingChatActions());
  }
  return result;
}

WorldStructure const& WorldClient::centralStructure() const {
  return m_centralStructure;
}

bool WorldClient::DamageNumberKey::operator<(DamageNumberKey const& other) const {
  return tie(sourceEntityId, targetEntityId, damageNumberParticleKind)
      < tie(other.sourceEntityId, other.targetEntityId, other.damageNumberParticleKind);
}

void WorldClient::renderCollisionDebug() {
  RectI clientWindow = m_clientState.window();
  if (clientWindow.isEmpty())
    return;

  auto logPoly = [](PolyF poly, Vec2F position, float r, float g, float b) {
    poly.translate(position);
    SpatialLogger::logPoly("world", poly, {floatToByte(r, true), floatToByte(g, true), floatToByte(b, true), 255});
  };

  forEachCollisionBlock(clientWindow, [&](auto const& block) {
      logPoly(block.poly, Vec2F{}, 1.0f, 0.0f, 0.0f);
    });

  for (auto const& object : query<TileEntity>(RectF(clientWindow))) {
    for (auto const& space : object->spaces())
      logPoly(PolyF(RectF(Vec2F(space), Vec2F(space) + Vec2F(1, 1))), Vec2F(object->tilePosition()), 0., 1., 0.);
  }

  for (auto const& physics : query<PhysicsEntity>(RectF(clientWindow))) {
    for (auto const& forceRegion : physics->forceRegions()) {
      if (auto dfr = forceRegion.ptr<DirectionalForceRegion>())
        logPoly(dfr->region, {}, 1.0f, 1.0f, 0.0f);
      else if (auto rfr = forceRegion.ptr<RadialForceRegion>())
        logPoly(PolyF(rfr->boundBox()), {}, 0.0f, 1.0f, 1.0f);
    }

    for (size_t i = 0; i < physics->movingCollisionCount(); ++i) {
      if (auto pmc = physics->movingCollision(i)) {
        logPoly(pmc->collision, pmc->position, 1.0f, 1.0f, 1.0f);
      }
    }
  }
}

void WorldClient::informTilePrediction(Vec2I const& pos, TileModification const& modification) {
  auto now = Time::monotonicMilliseconds();
  auto& p = m_predictedTiles[pos];
  p.time = now;
  if (auto placeMaterial = modification.ptr<PlaceMaterial>()) {
    if (placeMaterial->layer == TileLayer::Foreground) {
      auto materialDatabase = Root::singleton().materialDatabase();
      if (!materialDatabase->isCascadingFallingMaterial(placeMaterial->material)
       && !materialDatabase->         isFallingMaterial(placeMaterial->material)) {
        p.foreground = placeMaterial->material;
        p.foregroundHueShift = placeMaterial->materialHueShift;
      }
      else
        p.foreground = StructureMaterialId;
      if (placeMaterial->collisionOverride != TileCollisionOverride::None)
        p.collision = collisionKindFromOverride(placeMaterial->collisionOverride);
      else
        p.collision = materialDatabase->materialCollisionKind(placeMaterial->material);
      dirtyCollision(RectI::withSize(pos, { 1, 1 }));
    } else {
      p.background = placeMaterial->material;
      p.backgroundHueShift = placeMaterial->materialHueShift;
    }
  }
  else if (auto placeMod = modification.ptr<PlaceMod>()) {
    if (placeMod->layer == TileLayer::Foreground)
      p.foregroundMod = placeMod->mod;
    else
      p.backgroundMod = placeMod->mod;
  }
  else if (auto placeColor = modification.ptr<PlaceMaterialColor>()) {
    if (placeColor->layer == TileLayer::Foreground)
      p.foregroundColorVariant = placeColor->color;
    else
      p.backgroundColorVariant = placeColor->color;
  }
  else if (auto placeLiquid = modification.ptr<PlaceLiquid>()) {
    if (!p.liquid || p.liquid->liquid != placeLiquid->liquid)
      p.liquid = LiquidLevel(placeLiquid->liquid, placeLiquid->liquidLevel);
    else
      p.liquid->level += placeLiquid->liquidLevel;
  }
}

void WorldClient::setupForceRegions() {
  m_forceRegions.clear();

  if (!currentTemplate() || !currentTemplate()->worldParameters())
    return;

  auto forceRegionType = currentTemplate()->worldParameters()->worldEdgeForceRegions;

  if (forceRegionType == WorldEdgeForceRegionType::None)
    return;

  bool addTopRegion = forceRegionType == WorldEdgeForceRegionType::Top || forceRegionType == WorldEdgeForceRegionType::TopAndBottom;
  bool addBottomRegion = forceRegionType == WorldEdgeForceRegionType::Bottom || forceRegionType == WorldEdgeForceRegionType::TopAndBottom;

  auto worldServerConfig = Root::singleton().assets()->json("/worldserver.config");

  auto regionHeight = worldServerConfig.getFloat("worldEdgeForceRegionHeight");
  auto regionForce = worldServerConfig.getFloat("worldEdgeForceRegionForce");
  auto regionVelocity = worldServerConfig.getFloat("worldEdgeForceRegionVelocity");
  auto regionCategoryFilter = PhysicsCategoryFilter::whitelist({"player", "monster", "npc", "vehicle"});
  auto worldSize = Vec2F(currentTemplate()->size());

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

}
