#pragma once

#include "StarWorldClientState.hpp"
#include "StarNetPackets.hpp"
#include "StarWorldRenderData.hpp"
#include "StarAmbient.hpp"
#include "StarCellularLighting.hpp"
#include "StarWeather.hpp"
#include "StarInterpolationTracker.hpp"
#include "StarWorldStructure.hpp"
#include "StarChatAction.hpp"
#include "StarWiring.hpp"
#include "StarEntityRendering.hpp"
#include "StarWorld.hpp"
#include "StarGameTimers.hpp"
#include "StarLuaRoot.hpp"
#include "StarTickRateMonitor.hpp"

namespace Star {

STAR_STRUCT(Biome);
STAR_CLASS(WorldTemplate);
STAR_CLASS(Sky);
STAR_CLASS(Parallax);
STAR_CLASS(LuaRoot);
STAR_CLASS(DamageManager);
STAR_CLASS(EntityMap);
STAR_CLASS(ParticleManager);
STAR_CLASS(WorldClient);
STAR_CLASS(Player);
STAR_CLASS(Item);
STAR_CLASS(CelestialLog);
STAR_CLASS(ClientContext);
STAR_CLASS(PlayerStorage);
STAR_STRUCT(OverheadBar);

STAR_EXCEPTION(WorldClientException, StarException);

class WorldClient : public World {
public:
  WorldClient(PlayerPtr mainPlayer, LuaRootPtr luaRoot);
  ~WorldClient();

  ConnectionId connection() const override;
  WorldGeometry geometry() const override;
  uint64_t currentStep() const override;
  MaterialId material(Vec2I const& position, TileLayer layer) const override;
  MaterialHue materialHueShift(Vec2I const& position, TileLayer layer) const override;
  ModId mod(Vec2I const& position, TileLayer layer) const override;
  MaterialHue modHueShift(Vec2I const& position, TileLayer layer) const override;
  MaterialColorVariant colorVariant(Vec2I const& position, TileLayer layer) const override;
  LiquidLevel liquidLevel(Vec2I const& pos) const override;
  LiquidLevel liquidLevel(RectF const& region) const override;
  TileModificationList validTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) const override;
  TileModificationList applyTileModifications(TileModificationList const& modificationList, bool allowEntityOverlap) override;
  TileModificationList replaceTiles(TileModificationList const& modificationList, TileDamage const& tileDamage, bool applyDamage = false) override;
  bool damageWouldDestroy(Vec2I const& pos, TileLayer layer, TileDamage const& tileDamage) const override;
  EntityPtr entity(EntityId entityId) const override;
  void addEntity(EntityPtr const& entity, EntityId entityId = NullEntityId) override;
  EntityPtr closestEntity(Vec2F const& center, float radius, EntityFilter selector = EntityFilter()) const override;
  void forAllEntities(EntityCallback entityCallback) const override;
  void forEachEntity(RectF const& boundBox, EntityCallback callback) const override;
  void forEachEntityLine(Vec2F const& begin, Vec2F const& end, EntityCallback callback) const override;
  void forEachEntityAtTile(Vec2I const& pos, EntityCallbackOf<TileEntity> entityCallback) const override;
  EntityPtr findEntity(RectF const& boundBox, EntityFilter entityFilter) const override;
  EntityPtr findEntityLine(Vec2F const& begin, Vec2F const& end, EntityFilter entityFilter) const override;
  EntityPtr findEntityAtTile(Vec2I const& pos, EntityFilterOf<TileEntity> entityFilter) const override;
  bool tileIsOccupied(Vec2I const& pos, TileLayer layer, bool includeEphemeral = false, bool checkCollision = false) const override;
  CollisionKind tileCollisionKind(Vec2I const& pos) const override;
  void forEachCollisionBlock(RectI const& region, function<void(CollisionBlock const&)> const& iterator) const override;
  bool isTileConnectable(Vec2I const& pos, TileLayer layer, bool tilesOnly = false) const override;
  bool pointTileCollision(Vec2F const& point, CollisionSet const& collisionSet = DefaultCollisionSet) const override;
  bool lineTileCollision(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet = DefaultCollisionSet) const override;
  Maybe<pair<Vec2F, Vec2I>> lineTileCollisionPoint(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet = DefaultCollisionSet) const override;
  List<Vec2I> collidingTilesAlongLine(Vec2F const& begin, Vec2F const& end, CollisionSet const& collisionSet = DefaultCollisionSet, int maxSize = -1, bool includeEdges = true) const override;
  bool rectTileCollision(RectI const& region, CollisionSet const& collisionSet = DefaultCollisionSet) const override;
  TileDamageResult damageTiles(List<Vec2I> const& pos, TileLayer layer, Vec2F const& sourcePosition, TileDamage const& tileDamage, Maybe<EntityId> sourceEntity = {}) override;
  InteractiveEntityPtr getInteractiveInRange(Vec2F const& targetPosition, Vec2F const& sourcePosition, float maxRange) const override;
  bool canReachEntity(Vec2F const& position, float radius, EntityId targetEntity, bool preferInteractive = true) const override;
  RpcPromise<InteractAction> interact(InteractRequest const& request) override;
  float gravity(Vec2F const& pos) const override;
  float windLevel(Vec2F const& pos) const override;
  float lightLevel(Vec2F const& pos) const override;
  bool breathable(Vec2F const& pos) const override;
  float threatLevel() const override;
  StringList environmentStatusEffects(Vec2F const& pos) const override;
  StringList weatherStatusEffects(Vec2F const& pos) const override;
  bool exposedToWeather(Vec2F const& pos) const override;
  bool isUnderground(Vec2F const& pos) const override;
  bool disableDeathDrops() const override;
  List<PhysicsForceRegion> forceRegions() const override;
  Json getProperty(String const& propertyName, Json const& def = Json()) const override;
  void setProperty(String const& propertyName, Json const& property) override;
  void timer(float delay, WorldAction worldAction) override;
  double epochTime() const override;
  uint32_t day() const override;
  float dayLength() const override;
  float timeOfDay() const override;
  LuaRootPtr luaRoot() override;
  RpcPromise<Vec2F> findUniqueEntity(String const& uniqueId) override;
  RpcPromise<Json> sendEntityMessage(Variant<EntityId, String> const& entity, String const& message, JsonArray const& args = {}) override;
  bool isTileProtected(Vec2I const& pos) const override;

  // Is this WorldClient properly initialized in a world
  bool inWorld() const;

  bool inSpace() const;
  bool flying() const;

  bool mainPlayerDead() const;
  void reviveMainPlayer();
  bool respawnInWorld() const;
  void setRespawnInWorld(bool respawnInWorld);

  int64_t latency() const;

  void resendEntity(EntityId entityId);
  void removeEntity(EntityId entityId, bool andDie);

  WorldTemplateConstPtr currentTemplate() const;
  void setTemplate(Json newTemplate);
  SkyConstPtr currentSky() const;

  void dimWorld();
  bool interactiveHighlightMode() const;
  void setInteractiveHighlightMode(bool enabled);
  void setParallax(ParallaxPtr newParallax);
  void overrideGravity(float gravity);
  void resetGravity();

  // Disable normal client-side lighting algorithm, everything full brightness.
  bool fullBright() const;
  void setFullBright(bool fullBright);
  // Disable asynchronous client-side lighting algorithm, run on main thread.
  bool asyncLighting() const;
  void setAsyncLighting(bool asyncLighting);
  // Spatial log generated collision geometry.
  bool collisionDebug() const;
  void setCollisionDebug(bool collisionDebug);

  void handleIncomingPackets(List<PacketPtr> const& packets);
  List<PacketPtr> getOutgoingPackets();

  // Set the rendering window for this client.
  void setClientWindow(RectI window);
  // Sets the client window around the position of the main player.
  void centerClientWindowOnPlayer(Vec2U const& windowSize);
  void centerClientWindowOnPlayer();
  RectI clientWindow() const;
  WorldClientState& clientState();

  void update(float dt);
  // borderTiles here should extend the client window for border tile
  // calculations.  It is not necessary on the light array.
  void render(WorldRenderData& renderData, unsigned borderTiles);
  List<AudioInstancePtr> pullPendingAudio();
  List<AudioInstancePtr> pullPendingMusic();

  bool playerCanReachEntity(EntityId entityId, bool preferInteractive = true) const;

  void disconnectAllWires(Vec2I wireEntityPosition, WireNode const& node);
  void wire(Vec2I const& outputPosition, size_t outputIndex, Vec2I const& inputPosition, size_t inputIndex);
  void connectWire(WireConnection const& output, WireConnection const& input);

  // Functions for sending broadcast messages to other players that can receive them,
  // on completely vanilla servers by smuggling it through a DamageNotification.
  // It's cursed as fuck, but it works.
  bool sendSecretBroadcast(StringView broadcast, bool raw = false, bool compress = true);
  bool handleSecretBroadcast(PlayerPtr player, StringView broadcast);

  List<ChatAction> pullPendingChatActions();

  WorldStructure const& centralStructure() const;

  DungeonId dungeonId(Vec2I const& pos) const;

  void collectLiquid(List<Vec2I> const& tilePositions, LiquidId liquidId);

  bool waitForLighting(WorldRenderData* renderData = nullptr);

  // Slice 4: render-thread feedback. The render thread (ClientApplication) reports
  // whether the GPU lightmap pass succeeded this frame; the lighting thread reads it
  // to decide whether the CPU calculate() is redundant. Latches off (CPU keeps running)
  // until a real GPU success, and re-arms the CPU path the moment a GPU frame fails.
  void setGpuLightingActive(bool active);

  typedef std::function<bool(PlayerPtr, StringView)> BroadcastCallback;
  BroadcastCallback& broadcastCallback();



private:
  static const float DropDist;

  struct ClientRenderCallback : RenderCallback {
    void addDrawable(Drawable drawable, EntityRenderLayer renderLayer) override;
    void addLightSource(LightSource lightSource) override;
    void addParticle(Particle particle) override;
    void addAudio(AudioInstancePtr audio) override;
    void addTilePreview(PreviewTile preview) override;
    void addOverheadBar(OverheadBar bar) override;

    Map<EntityRenderLayer, List<Drawable>> drawables;
    List<LightSource> lightSources;
    List<Particle> particles;
    List<AudioInstancePtr> audios;
    List<PreviewTile> previewTiles;
    List<OverheadBar> overheadBars;
  };

  struct DamageNumber {
    float amount;
    Vec2F position;
    double timestamp;
  };

  struct DamageNumberKey {
    String damageNumberParticleKind;
    EntityId sourceEntityId;
    EntityId targetEntityId;

    bool operator<(DamageNumberKey const& other) const;
  };

  typedef function<ClientTile const& (Vec2I)> ClientTileGetter;

  void lightingTileGather();
  void lightingCalc();
  void lightingMain();
  // Dirty-REGION (Stage 0): record a lighting-relevant tile write into the coalesced dirty bbox.
  void markLightDirtyTile(Vec2I const& pos);

  void initWorld(WorldStartPacket const& packet);
  void clearWorld();
  void tryGiveMainPlayerItem(ItemPtr item, bool silent = false);

  void notifyEntityCreate(EntityPtr const& entity);

  // Queues pending (step based) updates to server,
  void queueUpdatePackets(bool sendEntityUpdates);
  void handleDamageNotifications();

  void sparkDamagedBlocks();

  Vec2I environmentBiomeTrackPosition() const;
  AmbientNoisesDescriptionPtr currentAmbientNoises() const;
  WeatherNoisesDescriptionPtr currentWeatherNoises() const;
  AmbientNoisesDescriptionPtr currentMusicTrack() const;
  AmbientNoisesDescriptionPtr currentAltMusicTrack() const;

  void playAltMusic(StringList const& newTracks, float fadeTime, int loops = -1);
  void stopAltMusic(float fadeTime);

  BiomeConstPtr mainEnvironmentBiome() const;

  // Populates foregroundTransparent / backgroundTransparent flag on ClientTile
  // based on transparency rules.
  bool readNetTile(Vec2I const& pos, NetTile const& netTile, bool updateCollision = true);
  void dirtyCollision(RectI const& region);
  void freshenCollision(RectI const& region);
  void renderCollisionDebug();

  void informTilePrediction(Vec2I const& pos, TileModification const& modification);

  void setTileProtection(DungeonId dungeonId, bool isProtected);

  void setupForceRegions();

  Json m_clientConfig;
  WorldTemplatePtr m_worldTemplate;
  WorldStructure m_centralStructure;
  Vec2F m_playerStart;
  bool m_respawnInWorld;
  JsonObject m_worldProperties;

  EntityMapPtr m_entityMap;
  ClientTileSectorArrayPtr m_tileArray;
  ClientTileGetter m_tileGetterFunction;
  DamageManagerPtr m_damageManager;
  LuaRootPtr m_luaRoot;

  WorldGeometry m_geometry;
  uint64_t m_currentStep;
  double m_currentTime;
  bool m_fullBright;
  bool m_asyncLighting;
  CellularLightingCalculator m_lightingCalculator;
  mutable CellularLightIntensityCalculator m_lightIntensityCalculator;
  ThreadFunction<void> m_lightingThread;
  
  Mutex m_lightingMutex;
  ConditionVariable m_lightingCond;
  atomic<bool> m_stopLightingThread;

  Mutex m_lightMapPrepMutex;
  Mutex m_lightMapMutex;

  Lightmap m_pendingLightMap;
  Lightmap m_lightMap;
  // GPU-spread inputs (Slice 2): exported alongside the lightmap when the
  // 'lightingGpu' flag is on. The pending pair is filled in lightingCalc outside
  // the map mutex, then published under m_lightMapMutex into the m_lighting* pair
  // (gated by m_lightingInputsValid) for waitForLighting to move into renderData.
  Image m_pendingLightingEmission;
  Image m_pendingLightingObstacle;
  Image m_lightingEmission;
  Image m_lightingObstacle;
  // The emission grid pre-converted to 16-bit half-floats (RGB, packed) on the lighting thread, so
  // the render thread uploads RGB16F (half the bytes) instead of RGB_F. Same pending->published handoff.
  List<uint16_t> m_pendingLightingEmissionHalf;
  List<uint16_t> m_lightingEmissionHalf;
  // The obstacle mask as single-channel R8 bytes (0/255), extracted on the lighting thread so the GPU
  // upload is R8 (a third the bytes of RGB24). Same pending->published handoff.
  List<uint8_t> m_pendingLightingObstacleR8;
  List<uint8_t> m_lightingObstacleR8;
  // The point-light list (Slice 3), exported/published alongside the emission +
  // obstacle grids for the GPU point pass; same pending->published handoff.
  List<ColoredCellularLightArray::PointLight> m_pendingLightingPointLights;
  List<ColoredCellularLightArray::PointLight> m_lightingPointLights;
  bool m_lightingInputsValid = false;
  // GPU lightmap border (cells) = calc-vs-query region padding, computed from the calculator's
  // geometry and published alongside the GPU inputs for waitForLighting to travel into renderData.
  int m_lightingBorder = 0;
  // Slice 4: set by the render thread via setGpuLightingActive(); read by the lighting
  // thread (lightingCalc) to skip the redundant CPU calculate() in confirmed GPU mode.
  atomic<bool> m_gpuLightingActive{false};
  List<LightSource> m_pendingLights;
  List<std::pair<Vec2F, Vec3F>> m_pendingParticleLights;
  RectI m_pendingLightRange;
  atomic<bool> m_pendingLightReady;
  Vec2I m_lightMinPosition;
  List<PreviewTile> m_previewTiles;

  // --- Dirty-gated lighting (spike, flag lightingDirtyGate, default off) ---
  // A fingerprint of every input to the computed lightmap. If unchanged since the last
  // computed frame, lightingCalc skips the gather+dispatch+export+GPU recompute and the
  // existing consume-once path reuses the prior lightmap. Derived state -> cannot desync.
  struct LightFingerprint {
    RectI lightRange;
    uint64_t tileEpoch = 0;
    Vec3F environmentLight;
    float undergroundLevel = 0.0f;
    bool newLighting = false, monochrome = false, lightingGpu = false, shadowCompare = false, tonemap = false;
    float promoteFraction = 0.0f, gpuBrightness = 1.0f, promoteMinIntensity = 0.0f;
    unsigned spreadIterations = 0;
    List<LightSource> lights;
    List<std::pair<Vec2F, Vec3F>> particleLights;
    bool operator==(LightFingerprint const& o) const {
      return lightRange == o.lightRange && tileEpoch == o.tileEpoch && environmentLight == o.environmentLight
          && undergroundLevel == o.undergroundLevel && newLighting == o.newLighting && monochrome == o.monochrome
          && lightingGpu == o.lightingGpu && shadowCompare == o.shadowCompare && tonemap == o.tonemap
          && promoteFraction == o.promoteFraction && gpuBrightness == o.gpuBrightness
          && promoteMinIntensity == o.promoteMinIntensity
          && spreadIterations == o.spreadIterations && lights == o.lights && particleLights == o.particleLights;
    }
    bool operator!=(LightFingerprint const& o) const { return !(*this == o); }
  };
  // Bumped at every writer of lighting-relevant ClientTile fields (emission/obstacle); the
  // fingerprint compares it to detect any tile change without a per-tile dirty-rect intersect.
  atomic<uint64_t> m_lightingTileEpoch{0};
  LightFingerprint m_lightingFingerprint;
  bool m_lightingFingerprintValid = false;
  // Validate oracle (lightingDirtyGateValidate): reference copies of the tile-derived GPU
  // buffers; on a would-skip frame a diff vs these proves a missed invalidation.
  List<uint16_t> m_validateRefEmissionHalf;
  List<uint8_t> m_validateRefObstacleR8;
  bool m_validateRefValid = false;

  // --- Dirty-REGION tracker (Stage 0, flag lightingDirtyRegionValidate, default off) ---
  // A coalesced bounding rect (WORLD-tile coords) of all lighting-relevant tile writes since the
  // last lightmap recompute. Generalizes the coarse m_lightingTileEpoch (kept as a redundant
  // cross-check). Written by the packet/update thread at the tile writers (markLightDirtyTile),
  // snapshotted + cleared by the lighting thread at the start of lightingCalc -- via its OWN minimal
  // critical-section mutex, NOT m_lightMapPrepMutex (which is held across the whole gather). Stage 0
  // does not yet consume the rect for partial recompute; it only validates the tracker.
  Mutex m_lightDirtyMutex;
  RectI m_lightDirtyRect = RectI::null();
  // Validate oracle: the obstacle buffer is PURELY tile-derived, so every obstacle cell that differs
  // from the reference (prior recompute, SAME window) must lie inside the consumed dirty rect -- else
  // the tracker under-reported. Refs + the window/epoch they were captured at gate the comparison
  // (skip on window-scroll or a gather-racing tile write -> conservative, never a false positive).
  List<uint8_t> m_validateRegionRefObstacleR8;
  RectI m_validateRegionRefLightRange = RectI::null();
  bool m_validateRegionRefValid = false;
  uint64_t m_lightDirtyLastEpoch = 0;
  // Validate heartbeat: POSITIVE evidence the oracle actually exercised real edits (not merely that
  // no failure fired). checks = frames the containment check ran; edits = check-frames with a
  // non-empty dirty rect (a real tile change validated as contained); underReports = cells found
  // outside the rect. A periodic "VALIDATE OK" log proves the tracker contained real edits.
  uint64_t m_validateRegionChecks = 0;
  uint64_t m_validateRegionEdits = 0;
  uint64_t m_validateRegionUnderReports = 0;
  uint64_t m_validateRegionLastLogEdits = 0;

  SkyPtr m_sky;

  CollisionGenerator m_collisionGenerator;

  WorldClientState m_clientState;
  Maybe<ConnectionId> m_clientId;

  PlayerPtr m_mainPlayer;

  bool m_collisionDebug;

  // Client side entity updates are not done until m_inWorld is true, which is
  // set to true after we have entered a world *and* the first batch of updates
  // are received.
  bool m_inWorld;

  GameTimer m_worldDimTimer;
  float m_worldDimLevel;
  Vec3B m_worldDimColor;

  bool m_interactiveHighlightMode;

  GameTimer m_parallaxFadeTimer;
  ParallaxPtr m_currentParallax;
  ParallaxPtr m_nextParallax;

  Maybe<float> m_overrideGravity;

  ClientWeather m_weather;
  ParticleManagerPtr m_particles;

  List<AudioInstancePtr> m_samples;
  List<AudioInstancePtr> m_music;

  HashMap<EntityId, uint64_t> m_masterEntitiesNetVersion;

  InterpolationTracker m_interpolationTracker;
  GameTimer m_entityUpdateTimer;

  List<PacketPtr> m_outgoingPackets;
  Maybe<int64_t> m_pingTime;
  int64_t m_latency;

  Set<EntityId> m_requestedDrops;

  Particle m_blockDamageParticle;
  Particle m_blockDamageParticleVariance;
  float m_blockDamageParticleProbability;

  Particle m_blockDingParticle;
  Particle m_blockDingParticleVariance;
  float m_blockDingParticleProbability;

  HashSet<Vec2I> m_damagedBlocks;

  AmbientManager m_ambientSounds;
  AmbientManager m_musicTrack;
  AmbientManager m_altMusicTrack;

  List<pair<float, WorldAction>> m_timers;

  Map<DamageNumberKey, DamageNumber> m_damageNumbers;
  float m_damageNotificationBatchDuration;

  AudioInstancePtr m_spaceSound;
  String m_activeSpaceSound;

  AmbientNoisesDescriptionPtr m_altMusicTrackDescription;
  bool m_altMusicActive;

  int m_modifiedTilePredictionTimeout;
  HashMap<Vec2I, PredictedTile> m_predictedTiles;
  HashSet<EntityId> m_startupHiddenEntities;

  HashMap<DungeonId, float> m_dungeonIdGravity;
  HashMap<DungeonId, bool> m_dungeonIdBreathable;
  StableHashSet<DungeonId> m_protectedDungeonIds;

  HashMap<String, List<RpcPromiseKeeper<Vec2F>>> m_findUniqueEntityResponses;
  HashMap<Uuid, RpcPromiseKeeper<Json>> m_entityMessageResponses;
  HashMap<Uuid, RpcPromiseKeeper<InteractAction>> m_entityInteractionResponses;

  List<PhysicsForceRegion> m_forceRegions;

  BroadcastCallback m_broadcastCallback;

  // used to keep track of already-printed stack traces caused by remote entities, so they don't clog the log
  HashSet<uint64_t> m_entityExceptionsLogged;
};

}
