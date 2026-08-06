#pragma once

#include "StarUniverseServer.hpp"
#include "StarUniverseClient.hpp"
#include "StarWorldPainter.hpp"
#include "StarGameTypes.hpp"
#include "StarMainInterface.hpp"
#include "StarMainMixer.hpp"
#include "StarTitleScreen.hpp"
#include "StarErrorScreen.hpp"
#include "StarCinematic.hpp"
#include "StarKeyBindings.hpp"
#include "StarMainApplication.hpp"

namespace Star {

STAR_CLASS(Input);
STAR_CLASS(Voice);

class ClientApplication : public Application {
public:
  void setPostProcessLayerPasses(String const& layer, unsigned const& passes);
  void setPostProcessGroupEnabled(String const& group, bool const& enabled, Maybe<bool> const& save);
  bool postProcessGroupEnabled(String const& group);
  Json postProcessGroups();
  virtual unsigned framesSkipped() const override;

protected:
  virtual void startup(StringList const& cmdLineArgs) override;
  virtual void shutdown() override;

  virtual void applicationInit(ApplicationControllerPtr appController) override;
  virtual void renderInit(RendererPtr renderer) override;

  virtual void windowChanged(WindowMode windowMode, Vec2U screenSize) override;

  virtual void processInput(InputEvent const& event) override;

  virtual void update() override;
  virtual void render() override;

  virtual void getAudioData(int16_t* stream, size_t len) override;

private:
  enum class MainAppState {
    Quit,
    Startup,
    SteamFlatpakWarning,
    Mods,
    ModsWarning,
    Splash,
    Error,
    Title,
    SinglePlayer,
    MultiPlayer
  };

  struct PendingMultiPlayerConnection {
    Variant<P2PNetworkingPeerId, HostAddressWithPort> server;
    String account;
    String password;
    bool forceLegacy;
  };
  
  struct PostProcessGroup {
    bool enabled;
  };
  
  struct PostProcessLayer {
    List<String> effects;
    unsigned passes;
    PostProcessGroup* group;
  };

  void renderReload();

  void changeState(MainAppState newState);
  void setError(String const& error);
  void setError(String const& error, std::exception const& e);

  void loadMods();
  void updateSteamFlatpakWarning(float dt);
  void updateMods(float dt);
  void updateModsWarning(float dt);
  void updateSplash(float dt);
  void updateError(float dt);
  void updateTitle(float dt);
  void updateRunning(float dt);

  bool isActionTaken(InterfaceAction action) const;
  bool isActionTakenEdge(InterfaceAction action) const;

  void updateCamera(float dt);

  RootUPtr m_root;
  ThreadFunction<void> m_rootLoader;
  CallbackListenerPtr m_reloadListener;

  MainAppState m_state = MainAppState::Startup;

  // Valid after applicationInit is called
  MainMixerPtr m_mainMixer;
  GuiContextPtr m_guiContext;
  InputPtr m_input;
  VoicePtr m_voice;

  // Valid after renderInit is called the first time
  CinematicPtr m_cinematicOverlay;
  ErrorScreenPtr m_errorScreen;

  // Valid if main app state >= Title
  PlayerStoragePtr m_playerStorage;
  StatisticsPtr m_statistics;
  UniverseClientPtr m_universeClient;
  TitleScreenPtr m_titleScreen;

  // Valid if main app state > Title
  PlayerPtr m_player;
  WorldPainterPtr m_worldPainter;
  WorldRenderData m_renderData;
  MainInterfacePtr m_mainInterface;
  
  StringMap<PostProcessGroup> m_postProcessGroups;
  List<PostProcessLayer> m_postProcessLayers;
  StringMap<size_t> m_labelledPostProcessLayers;

  // Valid if main app state == SinglePlayer
  UniverseServerPtr m_universeServer;

  // --- HEADLESS RENDER HARNESS (P-0) -------------------------------------------------------------
  // A golden-frame regression gate. Boots straight into single-player, renders N frames offscreen, hashes
  // the composed world frame ("main", captured BEFORE post-process and the GUI so chat/FPS/clock churn
  // cannot poison the hash), optionally dumps PNGs, and exits.
  //
  // WHY IT EXISTS: the two retained-cache oracles are DIFFERENTIAL -- they build their reference by invoking
  // the SAME draw lambda at the SAME point in the frame under the SAME ambient GL state, so they certify only
  // "path A == path B GIVEN identical ordering and state". Every reordering, blend/target/viewport change and
  // FBO-lifecycle event applies to BOTH sides and CANCELS EXACTLY. Dismantling the painter's algorithm (#139)
  // IS a reordering, so it lives entirely in their null space. This hash is ABSOLUTE, not differential: it is
  // the only gate that can see that class of failure. All five render bugs shipped on 2026-07-13 were found by
  // human eyes or adversarial reading -- none by an automated gate. This is that gate.
  //
  // Driven by environment variables (deliberately NOT command-line flags: the shipped option parser dies on
  // unknown args, and a test harness has no business widening the game's public CLI surface):
  //   STAR_RENDERTEST_FRAMES  -- capture this many frames, then quit. Unset/0 = harness off (zero cost).
  //   STAR_RENDERTEST_WARMUP  -- render (but do not capture) this many frames first, so world chunks, texture
  //                              atlases and the lighting pipeline have settled. Default 120.
  //   STAR_RENDERTEST_OUT     -- directory for PNG dumps. Unset = hash only, no images.
  // Run offscreen with SDL_VIDEO_DRIVER=offscreen (verified: yields a real GL 4.6 core context on the actual
  // Intel Arc GPU via Mesa/EGL, NOT a software rasterizer -- so hashes and GPU timings are both meaningful).
  //   STAR_RENDERTEST_LOAD    -- frames rendered UNPAUSED first, so the world actually streams in. Pausing
  //                              from frame 0 does NOT let chunks load (verified: the capture showed the player
  //                              alone in empty space, with the whole ship missing) -- setPause stops the world
  //                              from populating, not merely from ticking. Default 240.
  //   STAR_RENDERTEST_WARMUP  -- frames rendered AFTER the freeze, to let caches/atlases settle. Default 60.
  unsigned m_renderTestFrames = 0;
  unsigned m_renderTestLoad = 240;
  unsigned m_renderTestWarmup = 60;
  String m_renderTestOut;
  //   STAR_RENDERTEST_AB      -- "<configKey>=<jsonA>|<jsonB>". THE GATE. Against a FROZEN world, render with
  //                              the key set to A, then to B, and compare the two frames byte-for-byte. Both
  //                              renders see bit-identical world input, so any difference is attributable to
  //                              the CODE PATH and nothing else.
  //
  //                              This is why it beats a cross-run golden hash: the world must be loaded
  //                              UNPAUSED (a paused world never populates), and the number of sim ticks that
  //                              takes depends on wall-clock -- so two RUNS freeze in slightly different
  //                              animation states and their hashes legitimately differ. Within ONE run, frozen,
  //                              frames are bit-identical (verified). So the comparison must live inside one
  //                              process. Example, proving the env cache is byte-identical to the direct path:
  //                                STAR_RENDERTEST_AB='envRefreshInterval=1|4'
  unsigned m_renderTestFrame = 0;    // total frames since entering SinglePlayer
  unsigned m_renderTestSeen = 0;     // frames since the freeze
  bool m_renderTestEntered = false;
  bool m_renderTestFrozen = false;

  // THE LOAD ENDS ON QUIESCENCE, NOT ON A FRAME COUNT.
  //
  // It used to freeze after a fixed number of frames. But the world streams in ASYNCHRONOUSLY -- chunks and
  // entities arrive on the server thread -- so how much had loaded after N frames depended on wall-clock and
  // thread scheduling. Two runs of the SAME binary froze with 217 and 214 entities. That is why the cross-run
  // frame hash "legitimately differed", and it is why we could only ever certify a refactor against the
  // in-frame oracles, which cover env/parallax/lighting and NOT the world pass.
  //
  // Freezing when the entity count has been STABLE for N consecutive frames makes the SETTLE POINT
  // machine-speed-independent, which is what the in-process A/B below needs: both legs run against one
  // freeze, so a settle point that wanders no longer wanders BETWEEN the legs.
  //
  // IT DOES NOT MAKE THE FRAME HASH A CROSS-BINARY GOLDEN, and this comment claimed for twelve days that it
  // did. #191 tested the claim on its own terms and refuted it: three runs of one binary, frozen, at an
  // IDENTICAL state fingerprint -- epochTime, dayLength, camera, parallaxLayers=1, and entities=213 in all
  // three -- produced three distinct hashes. Stable within a run, different across runs, and not ASLR (three
  // runs under setarch -R gave three distinct hashes). So quiescence delivered exactly the observable it
  // converges, the entity COUNT, and a converged count is not a converged world: the residual is per-entity
  // animation phase, which nothing here pins. A cross-run golden needs that pinning and is its own project.
  //
  // What survives is the in-process A/B, and it is stronger than the golden would have been -- it holds the
  // world fixed by construction instead of hoping two runs agree. See #153.
  //
  //   STAR_RENDERTEST_QUIESCE -- consecutive frames the entity count must hold before freezing. Default 90.
  //   STAR_RENDERTEST_LOAD    -- now a HARD CAP, not a target: if the world has not settled by then, we freeze
  //                              anyway and say so LOUDLY, because a hash taken from an unsettled world is a
  //                              number that looks like a result and is not one.
  unsigned m_renderTestQuiesce = 90;
  unsigned m_renderTestStable = 0;      // consecutive frames the entity count has held
  size_t m_renderTestLastEntities = 0;
  bool m_renderTestLoading = true;
  String m_renderTestWarp;      // STAR_RENDERTEST_WARP=<substring of a teleport bookmark name>
  bool m_renderTestWarped = false;
  String m_renderTestAbKey;
  Json m_renderTestAbA;
  Json m_renderTestAbB;
  Json m_renderTestAbOriginal;   // shipped value, restored on exit so the A/B never PINS a setting
  int m_renderTestAbPhase = -1;      // -1 = no A/B; 0 = leg A settling; 1 = leg B settling
  uint64_t m_renderTestAbHashA = 0;
  Image m_renderTestAbFrameA;

  // MOTION-DRIVEN HARNESS (#174, guardrail G9). Everything below is FRAME-COUNTED, never wall-clock: a
  // wall-clock cycle makes two runs incomparable the moment the machine is busy, and comparability is the
  // whole product. Same discipline as pinSkyEpochTime.
  //
  // IT SERVES THE NOFREEZE INSTRUMENT, NOT THE FROZEN GATE, and that was the decision this task demanded be
  // made deliberately. The byte-identity gate freezes the world so its input is deterministic -- and a frozen
  // world does not tick, so the player cannot move in it at all. Motion is therefore only expressible in the
  // unfrozen run, and what it is gated ON is a COUNTER invariant rather than a pixel hash. STAR_RENDERTEST_WALK
  // sets NOFREEZE itself rather than asking the caller to remember.
  unsigned m_renderTestWalk = 0;           // frames per leg (right / pause / left / pause); 0 = off
  unsigned m_renderTestWalkFrame = 0;      // frames since the world settled -- the walk's own clock
  unsigned m_renderTestTogglePeriod = 0;   // flip m_renderTestToggleKey every N frames; 0 = off
  String m_renderTestToggleKey;            // a client option whose change REALLOCATES every framebuffer
  unsigned m_renderTestZoomPeriod = 0;     // step zoomLevel every N frames; 0 = off
  List<float> m_renderTestZoomLevels;
  // Both knobs write a config key, and Configuration::set PERSISTS on exit -- the trap that has bitten this
  // campaign twice and the harness itself once. Captured before the first write, restored at the terminal
  // point, exactly as the A/B does.
  StringMap<Json> m_renderTestConfigOriginals;

  void renderTestDriveMotion();     // per frame, once the world has settled
  void renderTestMotionVerdict();   // once, at the run's terminal point: the counters oracle
  void renderTestRestoreConfig();   // put back every key the motion knobs wrote

  void renderTestCapture();
  uint64_t renderTestHash(Image const& frame, double* meanLuminance) const;
  // A differing hash says two legs disagree; it cannot say WHERE, and where is what separates a boundary
  // artefact from a whole-scene regression. Reports the spatial distribution and, with STAR_RENDERTEST_OUT
  // set, writes legA/legB/diff PNGs.
  void renderTestDiffMap(Image const& a, Image const& b) const;
  void renderTestWritePng(Image const& frame, String const& path) const;
  // -----------------------------------------------------------------------------------------------

  float m_cameraXOffset = 0.0f;
  float m_cameraYOffset = 0.0f;
  bool m_snapBackCameraOffset = false;
  float m_cameraOffsetDownTime = 0.f;
  Vec2F m_cameraPositionSmoother;
  Vec2F m_cameraSmoothDelta;
  int m_cameraZoomDirection = 0;

  unsigned m_framesSkipped = 0;
  float m_telemetryReportTimer = 0.0f;
  float m_minInterfaceScale = 2;
  float m_maxInterfaceScale = 3;
  Vec2F m_crossoverRes;

  bool m_controllerInput;
  Vec2F m_controllerLeftStick;
  Vec2F m_controllerRightStick;
  List<KeyDownEvent> m_heldKeyEvents;
  List<KeyDownEvent> m_edgeKeyEvents;

  Maybe<PendingMultiPlayerConnection> m_pendingMultiPlayerConnection;
  Maybe<HostAddressWithPort> m_currentRemoteJoin;
  int64_t m_timeSinceJoin = 0;

  ByteArray m_immediateFont;

  bool m_loggedUGCCheck;
};

}
