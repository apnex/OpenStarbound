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
  unsigned m_renderTestFrames = 0;
  unsigned m_renderTestWarmup = 120;
  String m_renderTestOut;
  unsigned m_renderTestSeen = 0;
  bool m_renderTestEntered = false;

  void renderTestCapture();
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
