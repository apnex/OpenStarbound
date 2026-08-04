#pragma once

#include "StarWorldClientState.hpp"
#include "StarNetPackets.hpp"
#include "StarWorldRenderData.hpp"
#include "StarAmbient.hpp"
#include "StarCellularLighting.hpp"
#include "StarListener.hpp"
#include "StarTemporalLightingGate.hpp"
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
  void forEachEntity(RectF const& boundBox, EntityCallback const& callback) const override;
  void forEachEntityLine(Vec2F const& begin, Vec2F const& end, EntityCallback const& callback) const override;
  void forEachEntityAtTile(Vec2I const& pos, EntityCallbackOf<TileEntity> const& entityCallback) const override;
  EntityPtr findEntity(RectF const& boundBox, EntityFilter const& entityFilter) const override;
  EntityPtr findEntityLine(Vec2F const& begin, Vec2F const& end, EntityFilter const& entityFilter) const override;
  EntityPtr findEntityAtTile(Vec2I const& pos, EntityFilterOf<TileEntity> const& entityFilter) const override;
  bool tileIsOccupied(Vec2I const& pos, TileLayer layer, bool includeEphemeral = false, bool checkCollision = false) const override;
  CollisionKind tileCollisionKind(Vec2I const& pos) const override;
  void forEachCollisionBlock(RectI const& region, function<void(CollisionBlock const&)> const& iterator) const override;
  void getCollisionBlocks(RectI const& region, List<CollisionBlockRef>& output) const override;
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

  // Render harness (P-0): pin the sky's clock so a golden-frame hash reproduces across runs. The universe
  // clock is WALL-CLOCK derived, so two runs load the same save at different real times and land on a
  // different epochTime -- which moves the stars, the orbiters, the day/night colour and the parallax drift.
  // Measured: with the world paused, camera / entity count / parallax-layer count were already bit-identical
  // across runs and epochTime was the SOLE remaining source of hash drift. Called every frame by the harness;
  // never called in normal play.
  void pinSkyEpochTime(double epochTime);

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

  // HEADLESS: stop producing the VIEW, keep simulating (#199). render() still runs and entities still emit
  // -- particles spawn, sounds play, tile previews update, the world evolves identically -- but the two
  // view sinks discard, so no drawables and no overhead bars are accumulated and renderData carries none.
  //
  // It is a property of the CLIENT, not of the harness: a bot, an integration test or a CI rig wants a
  // world that ticks and nothing to look at. Off by default; the shipped client never touches it.
  void setHeadless(bool headless);
  bool headless() const;

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

  // RENDERCALLBACK HAS SIX SINKS AND TWO DUTIES, and that is why the client has no headless expression
  // (#199). `entity->render(&callback)` is not a view function -- it is the entity's per-frame EMIT, and
  // drawables are one of four outputs:
  //
  //   addDrawable / addOverheadBar   VIEW    -- what this entity looks like this frame
  //   addLightSource                 VIEW    -- feeds the lightmap, which nothing but a renderer reads
  //   addParticle                    SIM     -- particles are simulated; skipping them changes the world
  //   addAudio                       AUDIO   -- a real side effect a headless client may still want
  //   addTilePreview                 UI      -- placement preview state
  //
  // So "skip render() when headless" is WRONG: entities would stop emitting particles and sounds. The
  // separable thing is not the CALL, it is the SINK -- which is why the gate lives here rather than at the
  // call site, and why it is expressed as which sinks accept rather than as a branch around the loop.
  //
  // `wantView` false makes the two view sinks discard. Everything else behaves exactly as before, so the
  // world evolves identically; only the description of how it LOOKS is dropped.
  struct ClientRenderCallback : RenderCallback {
    explicit ClientRenderCallback(bool wantView = true) : wantView(wantView) {}

    void addDrawable(Drawable drawable, EntityRenderLayer renderLayer) override;
    void addLightSource(LightSource lightSource) override;
    void addParticle(Particle particle) override;
    void addAudio(AudioInstancePtr audio) override;
    void addTilePreview(PreviewTile preview) override;
    void addOverheadBar(OverheadBar bar) override;

    bool wantView = true;

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

  // The one tile gather (#214). Walks `region` column-parallel, computing the per-frame-INVARIANT
  // inputs, and hands each finished column to
  //   sink(Vec2I const& pos, Vec3F* light, bool* obstacle, bool const* skyExposed, size_t ySize)
  // `light` is mutable so a sink can fold environmentLight in place.
  //
  // Do NOT fold environmentLight in here. Float addition is not associative, and both sinks add it
  // LAST; moving it earlier changes pixels.
  template <typename ColumnSink>
  void gatherColumns(RectI const& region, ColumnSink&& sink);
  void lightingTileGather();
  // A1: gather the per-frame-INVARIANT tile lighting (block+liquid+background emission + obstacle +
  // sky-exposed bit, EXCLUDING the per-frame environmentLight) into the reusable stable grid (full
  // calc region; zeroes first so unloaded-margin cells read as begin()'s {0, not-obstacle}).
  void lightingStableGather();
  // A1/A2: gather the stable tile lighting for the given world-tile sub-rect into m_gatherGrid
  // (indexed relative to the current calc region). Shared by the full gather and the A2 margin.
  void gatherStableColumns(RectI const& region);
  // A2: shift the stable grid by the integer-tile camera delta (dx, dy) via the scratch buffer, then
  // gather only the newly-exposed L-shaped margin. Caller guarantees same dims+epoch and |d| < dims.
  void shiftAndGatherMargin(int dx, int dy);
  // E04: rebuild the stable grid from scratch and compare it against what the cache just produced, on the
  // real world. Observe-only -- the cached grid is what ships -- and inert unless lightingGatherOracle.
  // `path` names the branch that produced the grid, so a mismatch says which one is wrong.
  void gatherOracleCompare(char const* path);
  // #225 MEASUREMENT: is this tile position outside the lighting thread's last published calculation
  // region? A statistic only -- the snapshot may be stale, so it must never gate a real invalidation.
  bool epochBumpOffRegion(Vec2I const& pos) const;
  // A1: write the calculator cells from the stable grid, re-applying the current-frame
  // environmentLight to sky-exposed cells. Cheap (no material DB lookups / tile traversal).
  void applyStableToCells();
  void lightingCalc();
  void lightingMain();

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
  bool m_headless = false;   // #199 -- see setHeadless()
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
  // Freshness of the published GPU-lighting inputs, explicit rather than inferred from emptiness.
  // It USED to be inferred: waitForLighting moved the buffers out, so !m_lightingEmission.empty() meant
  // "not yet consumed". That coupling is what forced the buffers to be emptied on every consume, which in
  // turn made every recompute re-allocate and zero-fill ~788 KB it was about to overwrite in full. With
  // the flag, consume can SWAP the buffers back instead of moving them away, and the ring keeps its
  // allocations. Written and read only under m_lightMapMutex.
  bool m_lightingInputsFresh = false;
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

  // Bumped at every writer of lighting-relevant ClientTile fields (block/mod/liquid); a monotonic
  // change-detector for the temporal lighting gate (StarTemporalLightingGate). Atomic: written on the
  // packet/update thread, read on the lighting thread.
  atomic<uint64_t> m_lightingTileEpoch{0};
  // #225 MEASUREMENT: the lighting thread's last calculation region, published for the packet thread so an
  // epoch bump can be classified as inside or outside the region the gather actually reads. Packed Vec2I
  // per word; relaxed. Not load-bearing -- remove with the counters if the epoch stays global.
  atomic<int64_t> m_lightingCalcMinPacked{0};
  atomic<int64_t> m_lightingCalcMaxPacked{0};
  // Temporal lighting decoupling (flag lightingTemporalDecouple, default on): the last computed frame's
  // activity baseline. lightingCalc skips the recompute (render reuses the prior lightmap) on calm
  // frames between the floor cadence. Lighting-thread private.
  TemporalLightingGate::Baseline m_temporalBaseline;

  // A1/A2: scroll-incremental cached tile-gather (flag lightingGatherCache, default on; kill-switch).
  // The "stable" grid holds the per-frame-INVARIANT part of the tile gather (block+liquid+background
  // emission + obstacle + sky-exposed bit), EXCLUDING the per-frame environmentLight (re-applied each
  // frame via the skyExposed bit in applyStableToCells). Reused across frames when the tile epoch +
  // calc anchor/dims are unchanged (cache hit); shifted + margin-gathered on camera scroll (A2).
  // Column-major (x*height+y), matching CellularLightingCalculator::baseIndexFor. Lighting-thread
  // private (touched only in lightingCalc / lightingStableGather / applyStableToCells).
  struct GatherCell {
    Vec3F stableLight;
    uint8_t obstacle;
    uint8_t skyExposed;
  };
  List<GatherCell> m_gatherGrid;
  List<GatherCell> m_gatherScratch; // A2 double-buffer: shift destination, swapped into m_gatherGrid
  Vec2I m_gatherAnchor = Vec2I();
  Vec2I m_gatherDims = Vec2I();
  uint64_t m_gatherEpoch = 0;
  bool m_gatherValid = false;

  // Cache for the calculator's parameter Json. The composed value depends only on newLighting and
  // monochrome, but it was rebuilt every recompute: an Assets::json lookup under the GLOBAL assets mutex
  // (plus a freshen() clock write under that lock), a Json::set that deep-copies the whole config object,
  // and 7 string-keyed lookups inside setParameters. Measured 7.8 us/recompute.
  //
  // m_lightingParamsValid is ATOMIC because it is the one field here that is NOT lighting-thread-private:
  // initWorld clears it from the main thread, and clearWorld does not stop or join the lighting thread, so
  // a world hop can land that store while lightingCalc is mid-flight. A lost store is not benign -- it
  // would leave the cache claiming valid while initWorld has already set the parameters WITHOUT the
  // "pointAdditive" override lightingCalc composes in, silently dropping pointAdditive until newLighting
  // or monochrome next changes. The other two need no synchronisation because they are LIGHTING-THREAD-
  // PRIVATE -- written and read only inside lightingCalc. The rule is thread ownership, not the guard:
  // this used to say they were "only ever touched under a valid==false guard", which blesses a
  // main-thread write that would be a real race (#218). Any other-thread access makes them atomic too.
  // (Relaxed is sufficient for the flag: it guards a recompute decision, not a data handoff.)
  atomic<bool> m_lightingParamsValid = false;
  bool m_lightingParamsNewLighting = false;
  bool m_lightingParamsMonochrome = false;
  // /reload (and hot-reload) re-reads /lighting.config from disk without changing newLighting or
  // monochrome, so the value comparison above cannot see it. Root fires this tracker on every reload;
  // lightingCalc pulls it (atomic exchange) and drops the cache. WorldClient has no other reload hook --
  // initWorld is a world-START handler, not a reload handler -- so without this a /reload would be
  // silently inert for lighting parameters, which the uncached code path did honour.
  TrackerListenerPtr m_lightingParamsReloadTracker;

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
