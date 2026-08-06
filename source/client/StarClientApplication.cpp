#include "StarClientApplication.hpp"
#include "StarConfiguration.hpp"
#include "StarJsonExtra.hpp"
#include "StarFile.hpp"
#include "StarEncode.hpp"
#include "StarXXHash.hpp"   // [rendertest] golden-frame hash
#include "StarLogging.hpp"
#include "StarJsonExtra.hpp"
#include "StarRoot.hpp"
#include "StarVersion.hpp"
#include "StarPlayer.hpp"
#include "StarPlayerStorage.hpp"
#include "StarWarping.hpp"           // [rendertest] warp to a bookmark
#include "StarPlayerUniverseMap.hpp"  // [rendertest] teleport bookmarks
#include "StarPlayerLog.hpp"
#include "StarAssets.hpp"
#include "StarWorldTemplate.hpp"
#include "StarWorldClient.hpp"
#include "StarRootLoader.hpp"
#include "StarInput.hpp"
#include "StarVoice.hpp"
#include "StarCurve25519.hpp"
#include "StarInterpolation.hpp"

#include "StarCameraLuaBindings.hpp"
#include "StarCelestialLuaBindings.hpp"
#include "StarClipboardLuaBindings.hpp"
#include "StarInputLuaBindings.hpp"
#include "StarInterfaceLuaBindings.hpp"
#include "StarLuaHttpBindings.hpp"
#include "StarRenderingLuaBindings.hpp"
#include "StarTeamClientLuaBindings.hpp"
#include "StarVoiceLuaBindings.hpp"
#include "StarHttpTrustDialog.hpp"
#include "StarMainInterfaceTypes.hpp"
#include "StarTelemetry.hpp"
#include "StarTelemetryReporter.hpp"

#include "imgui.h"
#include "imgui_freetype.h"

#if defined STAR_SYSTEM_WINDOWS
#include <windows.h>
// graphics driver is told by these exports to default to the dedicated GPU
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;

// https://docs.kicad.org/doxygen/windows_2app_8cpp_source.html L45
extern "C" __declspec(dllexport) void NoHotPatch() { return; }
#endif 

namespace Star {

Json const AdditionalAssetsSettings = Json::parseJson(R"JSON(
    {
      "missingImage" : "/assetmissing.png",
      "missingAudio" : "/assetmissing.wav"
    }
  )JSON");

Json const AdditionalDefaultConfiguration = Json::parseJson(R"JSON(
    {
      "configurationVersion" : {
        "client" : 8
      },

      "allowAssetsMismatch" : false,
      "vsync" : true,
      "limitTextureAtlasSize" : false,
      "useMultiTexturing" : true,
      "audioChannelSeparation" : [-25, 25],

      "sfxVol" : 100,
      "instrumentVol" : 100,
      "musicVol" : 70,
      "hardwareCursor" : true,
      "windowedResolution" : [1000, 600],
      "fullscreenResolution" : [1920, 1080],
      "fullscreen" : false,
      "borderless" : false,
      "maximized" : true,
      "hdr": true,
      "zoomLevel" : 3.0,
      "cameraSpeedFactor" : 1.0,
      "interfaceScale" : 0,
      "speechBubbles" : true,

      "title" : {
        "multiPlayerAddress" : "",
        "multiPlayerPort" : "",
        "multiPlayerAccount" : "",
        "multiPlayerForceLegacy" : false
      },

      "bindings" : {
        "PlayerUp" :  [ { "type" : "key", "value" : "W", "mods" : [] } ],
        "PlayerDown" :  [ { "type" : "key", "value" : "S", "mods" : [] } ],
        "PlayerLeft" :  [ { "type" : "key", "value" : "A", "mods" : [] } ],
        "PlayerRight" :  [ { "type" : "key", "value" : "D", "mods" : [] } ],
        "PlayerJump" :  [ { "type" : "key", "value" : "Space", "mods" : [] } ],
        "PlayerDropItem" :  [ { "type" : "key", "value" : "Q", "mods" : [] } ],
        "PlayerInteract" :  [ { "type" : "key", "value" : "E", "mods" : [] } ],
        "PlayerShifting" :  [ { "type" : "key", "value" : "RShift", "mods" : [] }, { "type" : "key", "value" : "LShift", "mods" : [] } ],
        "PlayerTechAction1" :  [ { "type" : "key", "value" : "F", "mods" : [] } ],
        "PlayerTechAction2" :  [],
        "PlayerTechAction3" :  [],
        "EmoteBlabbering" :  [ { "type" : "key", "value" : "Right", "mods" : ["LCtrl", "LShift"] } ],
        "EmoteShouting" :  [ { "type" : "key", "value" : "Up", "mods" : ["LCtrl", "LAlt"] } ],
        "EmoteHappy" :  [ { "type" : "key", "value" : "Up", "mods" : [] } ],
        "EmoteSad" :  [ { "type" : "key", "value" : "Down", "mods" : [] } ],
        "EmoteNeutral" :  [ { "type" : "key", "value" : "Left", "mods" : [] } ],
        "EmoteLaugh" :  [ { "type" : "key", "value" : "Left", "mods" : [ "LCtrl" ] } ],
        "EmoteAnnoyed" :  [ { "type" : "key", "value" : "Right", "mods" : [] } ],
        "EmoteOh" :  [ { "type" : "key", "value" : "Right", "mods" : [ "LCtrl" ] } ],
        "EmoteOooh" :  [ { "type" : "key", "value" : "Down", "mods" : [ "LCtrl" ] } ],
        "EmoteBlink" :  [ { "type" : "key", "value" : "Up", "mods" : [ "LCtrl" ] } ],
        "EmoteWink" :  [ { "type" : "key", "value" : "Up", "mods" : ["LCtrl", "LShift"] } ],
        "EmoteEat" :  [ { "type" : "key", "value" : "Down", "mods" : ["LCtrl", "LShift"] } ],
        "EmoteSleep" :  [ { "type" : "key", "value" : "Left", "mods" : ["LCtrl", "LShift"] } ],
        "ShowLabels" :  [ { "type" : "key", "value" : "RAlt", "mods" : [] }, { "type" : "key", "value" : "LAlt", "mods" : [] } ],
        "CameraShift" :  [ { "type" : "key", "value" : "RCtrl", "mods" : [] }, { "type" : "key", "value" : "LCtrl", "mods" : [] } ],
        "TitleBack" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "CinematicSkip" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "CinematicNext" :  [ { "type" : "key", "value" : "Right", "mods" : [] }, { "type" : "key", "value" : "Return", "mods" : [] } ],
        "GuiClose" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "GuiShifting" :  [ { "type" : "key", "value" : "RShift", "mods" : [] }, { "type" : "key", "value" : "LShift", "mods" : [] } ],
        "KeybindingCancel" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "KeybindingClear" :  [ { "type" : "key", "value" : "Del", "mods" : [] }, { "type" : "key", "value" : "Backspace", "mods" : [] } ],
        "ChatPageUp" :  [ { "type" : "key", "value" : "PageUp", "mods" : [] } ],
        "ChatPageDown" :  [ { "type" : "key", "value" : "PageDown", "mods" : [] } ],
        "ChatPreviousLine" :  [ { "type" : "key", "value" : "Up", "mods" : [] } ],
        "ChatNextLine" :  [ { "type" : "key", "value" : "Down", "mods" : [] } ],
        "ChatSendLine" :  [ { "type" : "key", "value" : "Return", "mods" : [] } ],
        "ChatBegin" :  [ { "type" : "key", "value" : "Return", "mods" : [] } ],
        "ChatBeginCommand" :  [ { "type" : "key", "value" : "/", "mods" : [] } ],
        "ChatStop" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "InterfaceHideHud" :  [ { "type" : "key", "value" : "F1", "mods" : [] } ],
        "InterfaceChangeBarGroup" :  [ { "type" : "key", "value" : "X", "mods" : [] } ],
        "InterfaceDeselectHands" :  [ { "type" : "key", "value" : "Z", "mods" : [] } ],
        "InterfaceBar1" :  [ { "type" : "key", "value" : "1", "mods" : [] } ],
        "InterfaceBar2" :  [ { "type" : "key", "value" : "2", "mods" : [] } ],
        "InterfaceBar3" :  [ { "type" : "key", "value" : "3", "mods" : [] } ],
        "InterfaceBar4" :  [ { "type" : "key", "value" : "4", "mods" : [] } ],
        "InterfaceBar5" :  [ { "type" : "key", "value" : "5", "mods" : [] } ],
        "InterfaceBar6" :  [ { "type" : "key", "value" : "6", "mods" : [] } ],
        "InterfaceBar7" :  [],
        "InterfaceBar8" :  [],
        "InterfaceBar9" :  [],
        "InterfaceBar10" :  [],
        "EssentialBar1" :  [ { "type" : "key", "value" : "R", "mods" : [] } ],
        "EssentialBar2" :  [ { "type" : "key", "value" : "T", "mods" : [] } ],
        "EssentialBar3" :  [ { "type" : "key", "value" : "Y", "mods" : [] } ],
        "EssentialBar4" :  [ { "type" : "key", "value" : "N", "mods" : [] } ],
        "InterfaceRepeatCommand" :  [ { "type" : "key", "value" : "P", "mods" : [] } ],
        "InterfaceToggleFullscreen" :  [ { "type" : "key", "value" : "F11", "mods" : [] } ],
        "InterfaceReload" :  [],
        "InterfaceEscapeMenu" :  [ { "type" : "key", "value" : "Esc", "mods" : [] } ],
        "InterfaceInventory" :  [ { "type" : "key", "value" : "I", "mods" : [] } ],
        "InterfaceCodex" :  [ { "type" : "key", "value" : "L", "mods" : [] } ],
        "InterfaceQuest" :  [ { "type" : "key", "value" : "J", "mods" : [] } ],
        "InterfaceCrafting" :  [ { "type" : "key", "value" : "C", "mods" : [] } ]
      }
    }
  )JSON");

// Fixed sky clock for the render harness (P-0). Any constant works; it only has to be the SAME constant in
// every run, so two binaries render the same sky. The universe clock is wall-clock derived, so without this
// two runs of the same save land on a different epochTime -- which moves stars, orbiters, day/night colour and
// parallax drift.
//
// IT WAS NOT THE SOLE SOURCE, and this comment said it was. Measured on the current binary: four settled
// runs at one location, IDENTICAL fingerprint (this epochTime, same camera, parallaxLayers=31,
// entities=189), four distinct frame hashes -- each self-consistent across its own 30 captured frames.
// Two more per-run terms live in the sun rays, and the constants below pin them; see
// EnvironmentPainter::pinRayAnimation for what they are and why neither is reachable from here.
static double const RenderTestEpochTime = 36714000.0;
// Same rule as the epoch: arbitrary, but the SAME in every run. The seed replaces a Random::randu64()
// draw and the timer replaces an accumulation of real load-phase frametimes.
static uint64_t const RenderTestRaySeed = 0x5EED5A4Bull;
static double const RenderTestRayTime = 1234.5;

void ClientApplication::startup(StringList const& cmdLineArgs) {
  RootLoader rootLoader({AdditionalAssetsSettings, AdditionalDefaultConfiguration, String("starbound.log"), LogLevel::Info, false, String("starbound.config")});
  m_root = rootLoader.initOrDie(cmdLineArgs).first;

  Logger::info("OpenStarbound Client v{} for v{} ({}) Source ID: {}", OpenStarVersionString, StarVersionString, StarArchitectureString, StarSourceIdentifierString);
  #ifdef __clang__
  Logger::info("Compiled with Clang {}", __clang_version__);
  #endif

  // Headless render harness (P-0). Environment-driven so the shipped CLI surface is untouched and the harness
  // costs exactly nothing when unset. See the block comment in StarClientApplication.hpp.
  if (char const* frames = getenv("STAR_RENDERTEST_FRAMES")) {
    m_renderTestFrames = (unsigned)strtoul(frames, nullptr, 10);
    if (char const* load = getenv("STAR_RENDERTEST_LOAD"))
      m_renderTestLoad = (unsigned)strtoul(load, nullptr, 10);
    if (char const* quiesce = getenv("STAR_RENDERTEST_QUIESCE"))
      m_renderTestQuiesce = (unsigned)strtoul(quiesce, nullptr, 10);
    if (char const* warmup = getenv("STAR_RENDERTEST_WARMUP"))
      m_renderTestWarmup = (unsigned)strtoul(warmup, nullptr, 10);
    if (char const* out = getenv("STAR_RENDERTEST_OUT"))
      m_renderTestOut = String(out);
    if (char const* warp = getenv("STAR_RENDERTEST_WARP"))
      m_renderTestWarp = String(warp);
    // MOTION KNOBS (#174). Frames per leg / period, never seconds.
    if (char const* walk = getenv("STAR_RENDERTEST_WALK"))
      m_renderTestWalk = (unsigned)strtoul(walk, nullptr, 10);
    // "<clientOptionKey>=<framesPerFlip>", e.g. antiAliasing=45 or hdr=60. Flipping either REALLOCATES every
    // framebuffer with undefined content -- the churn the GL-state audit (#139) and the cache generation guard
    // both exist for, and which no frozen run can produce.
    if (char const* tog = getenv("STAR_RENDERTEST_TOGGLE")) {
      String spec(tog);
      if (auto eq = spec.find('='); eq != NPos) {
        m_renderTestToggleKey = spec.substr(0, eq);
        m_renderTestTogglePeriod = (unsigned)strtoul(spec.substr(eq + 1).utf8Ptr(), nullptr, 10);
      } else {
        Logger::error("[rendertest] STAR_RENDERTEST_TOGGLE must look like '<key>=<framesPerFlip>' -- got '{}'", spec);
      }
    }
    // "<a>,<b>,...=<framesPerStep>", e.g. "2.0,3.0,4.0=45". Zoom changes the camera pixelRatio, which is a
    // term in the env cache's refresh key and resizes every screen-sized surface.
    if (char const* zoom = getenv("STAR_RENDERTEST_ZOOM")) {
      String spec(zoom);
      if (auto eq = spec.find('='); eq != NPos) {
        for (auto const& lv : spec.substr(0, eq).split(','))
          m_renderTestZoomLevels.append(strtof(lv.utf8Ptr(), nullptr));
        m_renderTestZoomPeriod = (unsigned)strtoul(spec.substr(eq + 1).utf8Ptr(), nullptr, 10);
      }
      if (m_renderTestZoomLevels.size() < 2 || !m_renderTestZoomPeriod) {
        Logger::error("[rendertest] STAR_RENDERTEST_ZOOM must look like '<a>,<b>[,...]=<framesPerStep>' with at "
                      "least two levels -- got '{}'; zoom disabled", spec);
        m_renderTestZoomLevels.clear();
        m_renderTestZoomPeriod = 0;
      }
    }
    // "<configKey>=<jsonA>|<jsonB>", e.g. envRefreshInterval=1|4  or  lightingGpu=true|false
    if (char const* ab = getenv("STAR_RENDERTEST_AB")) {
      String spec(ab);
      if (auto eq = spec.find('='); eq != NPos) {
        String key = spec.substr(0, eq);
        String vals = spec.substr(eq + 1);
        if (auto bar = vals.find('|'); bar != NPos) {
          try {
            m_renderTestAbKey = key;
            m_renderTestAbA = Json::parse(vals.substr(0, bar));
            m_renderTestAbB = Json::parse(vals.substr(bar + 1));
          } catch (std::exception const& e) {
            Logger::error("[rendertest] bad STAR_RENDERTEST_AB '{}': {}", spec, outputException(e, false));
            m_renderTestAbKey = "";
          }
        }
      }
      if (m_renderTestAbKey.empty())
        Logger::error("[rendertest] STAR_RENDERTEST_AB must look like 'key=jsonA|jsonB' -- got '{}'", spec);
    }
    if (m_renderTestFrames)
      Logger::info("[rendertest] ARMED load={} warmup={} frames={} warp='{}' ab='{}' out='{}'",
        m_renderTestLoad, m_renderTestWarmup, m_renderTestFrames, m_renderTestWarp, m_renderTestAbKey, m_renderTestOut);
  }
}

void ClientApplication::shutdown() {
  // Clear HTTP trust request callback
  LuaBindings::clearHttpTrustRequestCallback();

  m_mainInterface.reset();

  if (m_universeClient)
    m_universeClient->disconnect();

  if (m_universeServer) {
    m_universeServer->stop();
    m_universeServer->join();
    m_universeServer.reset();
  }

  if (m_statistics) {
    m_statistics->writeStatistics();
    m_statistics.reset();
  }

  m_universeClient.reset();
  m_statistics.reset();
}

void ClientApplication::applicationInit(ApplicationControllerPtr appController) {
  Application::applicationInit(appController);

  appController->setCursorVisible(true);

  auto configuration = m_root->configuration();
  bool vsync = configuration->get("vsync").toBool();
  Vec2U windowedSize = jsonToVec2U(configuration->get("windowedResolution"));
  Vec2U fullscreenSize = jsonToVec2U(configuration->get("fullscreenResolution"));
  bool fullscreen = configuration->get("fullscreen").toBool();
  bool borderless = configuration->get("borderless").toBool();
  bool maximized = configuration->get("maximized").toBool();
  m_controllerInput = configuration->get("controllerInput").optBool().value();
  
  #ifdef STAR_SYSTEM_WINDOWS
    appController->setBorderlessWorkaround(configuration->get("borderlessWorkaround", true).toBool());
  #endif

  if (fullscreen)
    appController->setFullscreenWindow(fullscreenSize);
  else if (borderless)
    appController->setBorderlessWindow();
  else if (maximized)
    appController->setMaximizedWindow();
  else
    appController->setNormalWindow(windowedSize);

  float updateRate = 1.0f / GlobalTimestep;
  if (auto jUpdateRate = configuration->get("updateRate")) {
    updateRate = jUpdateRate.toFloat();
    GlobalTimestep = 1.0f / updateRate;
  }

  if (auto jServerUpdateRate = configuration->get("serverUpdateRate"))
    ServerGlobalTimestep = 1.0f / jServerUpdateRate.toFloat();

  appController->setTargetUpdateRate(updateRate);
  appController->setVSyncEnabled(vsync);
  appController->setCursorHardware(configuration->get("hardwareCursor").optBool().value(true));

  // Must be called before anything that can invoke an asset load.
  loadMods();
  
  AudioFormat audioFormat = appController->enableAudio();
  m_mainMixer = make_shared<MainMixer>(audioFormat.sampleRate, audioFormat.channels);
  m_mainMixer->setVolume(0.5);
  
  m_worldPainter = make_shared<WorldPainter>();
  m_guiContext = make_shared<GuiContext>(m_mainMixer->mixer(), appController);
  m_input = make_shared<Input>();
  m_voice = make_shared<Voice>(appController);  

  auto assets = m_root->assets();

  {
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    m_immediateFont = *assets->bytes("/hobo.ttf");
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_ForceAutoHint;
    io.Fonts->AddFontFromMemoryTTF(m_immediateFont.ptr(), m_immediateFont.size(),
      16, &config, io.Fonts->GetGlyphRangesDefault());
  }

  m_minInterfaceScale = assets->json("/interface.config:minInterfaceScale").toFloat();
  m_maxInterfaceScale = assets->json("/interface.config:maxInterfaceScale").toFloat();
  m_crossoverRes = jsonToVec2F(assets->json("/interface.config:interfaceCrossoverRes"));
  
  appController->setApplicationTitle(assets->json("/client.config:windowTitle").toString());
  appController->setMaxFrameSkip(assets->json("/client.config:maxFrameSkip").toUInt());
  appController->setUpdateTrackWindow(assets->json("/client.config:updateTrackWindow").toFloat());
  
  if (auto jVoice = configuration->get("voice"))
    m_voice->loadJson(jVoice.toObject(), true);

  m_voice->init();
  m_voice->setLocalSpeaker(0);

  // Telemetry: apply configured gates (read-only; no game-state mutation).
  Telemetry::setEnabled(configuration->get("telemetryEnabled", true).toBool());
  Telemetry::setDeepEnabled(configuration->get("telemetryDeepTracing", false).toBool());
}

void ClientApplication::renderInit(RendererPtr renderer) {
  Application::renderInit(renderer);
  renderReload();
  m_root->registerReloadListener(m_reloadListener = make_shared<CallbackListener>([this]() { renderReload(); }));

  if (m_root->configuration()->get("limitTextureAtlasSize").optBool().value(false))
    renderer->setSizeLimitEnabled(true);

  renderer->setMultiTexturingEnabled(m_root->configuration()->get("useMultiTexturing").optBool().value(true));

  m_guiContext->renderInit(renderer);

  m_cinematicOverlay = make_shared<Cinematic>();
  m_errorScreen = make_shared<ErrorScreen>();

  if (m_titleScreen)
    m_titleScreen->renderInit(renderer);
  if (m_worldPainter)
    m_worldPainter->renderInit(renderer);

  #ifdef STAR_ENABLE_STEAM_INTEGRATION
  #ifdef STAR_SYSTEM_LINUX
  if (g_steamIsFlatpak) {
    auto config = m_root->configuration();
    if (!config->get("steamFlatpakWarningShown").optBool().value()) {
      config->set("steamFlatpakWarningShown", true);
      m_errorScreen->setMessage(m_root->assets()->json("/interface.config:steamFlatpakWarning").toString());
      changeState(MainAppState::SteamFlatpakWarning);
      return;
    }
  }
  #endif
  #endif

  changeState(MainAppState::Mods);
}

void ClientApplication::windowChanged(WindowMode windowMode, Vec2U screenSize) {
  auto config = m_root->configuration();
  if (windowMode == WindowMode::Fullscreen) {
    config->set("fullscreenResolution", jsonFromVec2U(screenSize));
    config->set("fullscreen", true);
    config->set("borderless", false);
  } else if (windowMode == WindowMode::Borderless) {
    config->set("borderless", true);
    config->set("fullscreen", false);
  } else if (windowMode == WindowMode::Maximized) {
    config->set("maximized", true);
    config->set("fullscreen", false);
    config->set("borderless", false);
  } else {
    config->set("maximized", false);
    config->set("fullscreen", false);
    config->set("borderless", false);
    config->set("windowedResolution", jsonFromVec2U(screenSize));
  }
}

void ClientApplication::processInput(InputEvent const& event) {
  if (auto keyDown = event.ptr<KeyDownEvent>()) {
    m_heldKeyEvents.append(*keyDown);
    m_edgeKeyEvents.append(*keyDown);
  } else if (auto keyUp = event.ptr<KeyUpEvent>()) {
    eraseWhere(m_heldKeyEvents, [&](auto& keyEvent) {
      return keyEvent.key == keyUp->key;
    });

    Maybe<KeyMod> modKey = KeyModNames.maybeLeft(KeyNames.getRight(keyUp->key));
    if (modKey)
      m_heldKeyEvents.transform([&](auto& keyEvent) {
        return KeyDownEvent{keyEvent.key, keyEvent.mods & ~*modKey};
      });
  }
  else if (auto cAxis = event.ptr<ControllerAxisEvent>()) {
    if (cAxis->controllerAxis == ControllerAxis::LeftX)
      m_controllerLeftStick[0] = cAxis->controllerAxisValue;
    else if (cAxis->controllerAxis == ControllerAxis::LeftY)
      m_controllerLeftStick[1] = cAxis->controllerAxisValue;
    else if (cAxis->controllerAxis == ControllerAxis::RightX)
      m_controllerRightStick[0] = cAxis->controllerAxisValue;
    else if (cAxis->controllerAxis == ControllerAxis::RightY)
      m_controllerRightStick[1] = cAxis->controllerAxisValue;
  }

  bool processed = !m_errorScreen->accepted() && m_errorScreen->handleInputEvent(event);

  if (!processed) {
    if (m_state == MainAppState::Splash) {
      processed = m_cinematicOverlay->handleInputEvent(event);
    } else if (m_state == MainAppState::Title) {
      if (!(processed = m_cinematicOverlay->handleInputEvent(event)))
        processed = m_titleScreen->handleInputEvent(event);

    } else if (m_state == MainAppState::SinglePlayer || m_state == MainAppState::MultiPlayer) {
      if (!(processed = m_cinematicOverlay->handleInputEvent(event)))
        processed = m_mainInterface->handleInputEvent(event);
    }
  }

  m_input->handleInput(event, processed);
}

void ClientApplication::update() {
  float dt = GlobalTimestep * GlobalTimescale;
  auto& app = appController();
  if (m_state >= MainAppState::Title) {
    if (auto p2pNetworkingService = app->p2pNetworkingService()) {
      if (auto join = p2pNetworkingService->pullPendingJoin()) {
        m_pendingMultiPlayerConnection = PendingMultiPlayerConnection{join.takeValue(), {}, {}, false};
        changeState(MainAppState::Title);
      }
      
      if (auto req = p2pNetworkingService->pullJoinRequest())
        m_mainInterface->queueJoinRequest(*req);

      p2pNetworkingService->update();
    }
  }

  if (!m_errorScreen->accepted())
    m_errorScreen->update(dt);

  // This warning is only applicable to Linux systems so no need to process it otherwise.
  #ifdef STAR_ENABLE_STEAM_INTEGRATION
  #ifdef STAR_SYSTEM_LINUX
  if (m_state == MainAppState::SteamFlatpakWarning)
    updateSteamFlatpakWarning(dt);
  else
  #endif
  #endif

  if (m_state == MainAppState::Mods)
    updateMods(dt);
  else if (m_state == MainAppState::ModsWarning)
    updateModsWarning(dt);

  if (m_state == MainAppState::Splash)
    updateSplash(dt);
  else if (m_state == MainAppState::Error)
    updateError(dt);
  else if (m_state == MainAppState::Title)
    updateTitle(dt);
  else if (m_state > MainAppState::Title)
    updateRunning(dt);
  
  // Swallow leftover encoded voice data if we aren't in-game to allow mic read to continue for settings.
  if (m_state <= MainAppState::Title) {
    DataStreamBuffer ext;
    m_voice->send(ext);
  } // TODO: directly disable encoding at menu so we don't have to do this

  m_guiContext->cleanup();
  m_edgeKeyEvents.clear();
  m_input->update();
  ++m_framesSkipped;

  // Telemetry: one tick mark per client update + interval-driven JSON snapshot (read-only).
  Telemetry::markTick("client");
  if (auto interval = m_root->configuration()->get("telemetryReportInterval", 0).toInt(); interval > 0) {
    m_telemetryReportTimer += dt;
    if (m_telemetryReportTimer >= (float)interval) {
      m_telemetryReportTimer = 0.0f;
      TelemetryReporter::writeSnapshot(m_root->toStoragePath(""), JsonObject{
        {"vsync", Json(m_root->configuration()->get("vsync", true).optBool().value(true))}
      });
    }
  }
}

void ClientApplication::render() {
  m_framesSkipped = 0;
  auto config = m_root->configuration();
  auto assets = m_root->assets();
  auto& renderer = Application::renderer();

  renderer->setMultiSampling(config->getOrDefault("antiAliasing").toBool() ? 4 : 0);
  renderer->setMainHDR(config->getOrDefault("hdr").toBool());
  renderer->setVboOrphan(config->getOrDefault("renderVboOrphan").toBool());
  // Like setMainHDR/setMultiSampling above, this reloads the whole framebuffer set when it changes, so it
  // belongs HERE -- before the frame starts -- and not at the point of use inside WorldPainter::render, which
  // would destroy and recreate every framebuffer (including the bound "main") in the middle of a frame.
  renderer->oracle().setEnabled(config->get("envOracle", false).optBool().value(false)
      || config->get("parallaxOracle", false).optBool().value(false)
      || config->get("lightingSpreadOracle", false).optBool().value(false));
  renderer->switchEffectConfig("interface");

  if (auto interfaceScale = config->get("interfaceScale").optFloat().value(); interfaceScale != 0)
    m_guiContext->setInterfaceScale(interfaceScale);
  else if (m_guiContext->windowWidth() >= m_crossoverRes[0] && m_guiContext->windowHeight() >= m_crossoverRes[1])
    m_guiContext->setInterfaceScale(m_maxInterfaceScale);
  else
    m_guiContext->setInterfaceScale(m_minInterfaceScale);

  if (m_state == MainAppState::Mods || m_state == MainAppState::Splash) {
    m_cinematicOverlay->render();

  } else if (m_state == MainAppState::Title) {
    m_titleScreen->render();
    m_cinematicOverlay->render();

  } else if (m_state > MainAppState::Title) {
    WorldClientPtr worldClient = m_universeClient->worldClient();
    if (worldClient) {
      auto totalStart = Time::monotonicMicroseconds();
      renderer->switchEffectConfig("world");
      auto clientStart = totalStart;
      // Render harness: pin the sky clock BEFORE the sky bakes its render data (star offsets, orbit angle,
      // day/night colour are all derived from epochTime, so overriding the field afterwards would be too late).
      // With the world paused this was the sole remaining source of cross-run hash drift.
      if (m_renderTestFrames) {
        worldClient->pinSkyEpochTime(RenderTestEpochTime);
        // The sky clock is not enough on its own -- the rays carry two terms epochTime does not reach.
        // Pinned every frame, like the epoch, so the value never depends on how long the load took.
        if (m_worldPainter)
          m_worldPainter->pinRayAnimation(RenderTestRaySeed, RenderTestRayTime);
      }
      worldClient->render(m_renderData, TilePainter::BorderTileSize);
      LogMap::set("client_render_world_client", strf(u8"{:05d}\u00b5s", Time::monotonicMicroseconds() - clientStart));

      auto paintStart = Time::monotonicMicroseconds();
      // Work versus wait. cpu.frame.render.us bills this block whether the thread was computing or blocked on
      // the lighting thread; that is correct for frame time and useless for choosing a lever. Detail, not
      // Budget: it is a slice of cpu.frame.render.us, not a sibling of it.
      static auto waitLighting = Telemetry::timer("cpu.wait.lighting.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
      m_worldPainter->render(m_renderData, [&]() -> bool {
        TelemetryScope s(waitLighting);
        return worldClient->waitForLighting(&m_renderData);
      });
      // Slice 4: report the GPU lightmap outcome to the lighting thread so it can drop the
      // redundant CPU calculate() once GPU lighting is confirmed (and re-arm it if GPU fails).
      worldClient->setGpuLightingActive(m_worldPainter->gpuLightingActive());
      auto painterUs = Time::monotonicMicroseconds() - paintStart;
      LogMap::set("client_render_world_painter", strf(u8"{:05d}\u00b5s", painterUs));
      // Durable telemetry mirror (R-F gate): render-thread paint cost in the snapshot, not just the /debug HUD.
      static auto painterTimer = Telemetry::timer("render.world.painter.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
      painterTimer.record(painterUs);
      auto worldRenderUs = Time::monotonicMicroseconds() - totalStart;
      LogMap::set("client_render_world_total", strf(u8"{:05d}\u00b5s", worldRenderUs));
      // Telemetry: route the already-computed render delta through a timer (no extra clock read).
      // Cache the handle in a static so the per-frame path stays lock-free (registration once).
      static auto renderFrameTimer = Telemetry::timer("render.frame.us",
        MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
      renderFrameTimer.record(worldRenderUs);

      // Golden-frame capture (P-0). HERE, and not later: "main" now holds the composed WORLD frame, before
      // post-process and before the GUI -- so chat, the FPS counter and the clock cannot poison the hash.
      if (m_renderTestFrames)
        renderTestCapture();

      auto size = Vec2F(renderer->screenSize());
      auto quad = renderFlatRect(RectF::withSize(size / -2, size), Vec4B::filled(0), 0.0f);
      for (auto& layer : m_postProcessLayers) {
        if (layer.group ? layer.group->enabled : true) {
          for (unsigned i = 0; i < layer.passes; i++) {
            for (auto& effect : layer.effects) {
              renderer->switchEffectConfig(effect);
              renderer->render(quad);
            }
          }
        }
      }
    }
    renderer->switchEffectConfig("interface");
    auto start = Time::monotonicMicroseconds();
    // GPU timer on the interface (task #141). This pass had a CPU timer but NO GPU timer, and it is a large
    // part of the 1.8-3.5ms/frame that the whole-frame span showed was unaccounted for -- at the Lava Refinery
    // the unattributed block is BIGGER THAN ALL THE LIGHTING COMBINED. On a static scene the entire GUI is
    // rebuilt and re-rasterised every single frame.
    // ABLATION HOOK (task #141). The per-pass GL_TIME_ELAPSED timers are NOT ADDITIVE -- with 12 of them their
    // sum overshot the real frame by 5ms, because each bracket serialises the pipeline and measures its own
    // stall. They rank passes; they do not budget them. The only trustworthy figure is the whole-frame span.
    // So to get a pass's TRUE cost, ABLATE it and measure the frame: (span_with - span_without).
    static bool const skipInterface = []() {
      char const* e = getenv("STAR_RENDERTEST_NO_INTERFACE");
      return e && *e && *e != '0';
    }();
    // Finer ablation, because "the GUI costs 75% of the frame" is too big a claim to leave unlocalised.
    static int const uiMask = []() {
      char const* e = getenv("STAR_RENDERTEST_UI_MASK");   // bit0 inWorld, bit1 mainInterface, bit2 cinematic
      return e && *e ? (int)strtol(e, nullptr, 10) : 7;
    }();
    if (!skipInterface) {
      renderer->gpuTimer().begin("render.pass.interface.gpu_us",
        MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});
      if (uiMask & 1) m_mainInterface->renderInWorldElements();
      if (uiMask & 2) m_mainInterface->render();
      if (uiMask & 4) m_cinematicOverlay->render();
      renderer->gpuTimer().end("render.pass.interface.gpu_us");
    }
    // Task #141: the GUI had a debug-HUD string but NO telemetry timer, so its CPU cost was invisible -- and
    // that is load-bearing right now. The whole-frame "GPU span" is a GL_TIMESTAMP delta, which includes GPU
    // IDLE. If the HUD is CPU-bound, the GPU sits waiting and the span measures LATENCY, not GPU WORK -- which
    // would make "the HUD costs 8.3ms of GPU" an artifact of the instrument rather than a fact about the game.
    auto interfaceUs = Time::monotonicMicroseconds() - start;
    static auto interfaceTimer = Telemetry::timer("render.interface.us",
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail});
    interfaceTimer.record(interfaceUs);
    LogMap::set("client_render_interface", strf(u8"{:05d}\u00b5s", interfaceUs));
  }

  // Telemetry HUD face: curated headline skip-rates (gated; reuses the /debug LogMap, cheap counter reads).
  if (config->get("telemetryHud", false).toBool()) {
    auto skipState = Telemetry::counter("animator.state.merge.skipped").value();
    auto perfState = Telemetry::counter("animator.state.merge.performed").value();
    uint64_t totalState = skipState + perfState;
    LogMap::set("telemetry_animator_state_skiprate",
      strf("{:.1f}% ({}/{})", 100.0 * skipState / (totalState ? totalState : 1), skipState, totalState));
    auto skipPart = Telemetry::counter("animator.part.merge.skipped").value();
    auto perfPart = Telemetry::counter("animator.part.merge.performed").value();
    uint64_t totalPart = skipPart + perfPart;
    LogMap::set("telemetry_animator_part_skiprate",
      strf("{:.1f}% ({}/{})", 100.0 * skipPart / (totalPart ? totalPart : 1), skipPart, totalPart));
  }

  if (!m_errorScreen->accepted())
    m_errorScreen->render();
}

void ClientApplication::getAudioData(int16_t* sampleData, size_t frameCount) {
  if (m_mainMixer) {
    m_mainMixer->read(sampleData, frameCount, [&](int16_t* buffer, size_t frames, unsigned channels) {
      if (m_voice)
        m_voice->mix(buffer, frames, channels);
    });
  }
}

auto postProcessGroupsRoot = "postProcessGroups";

void ClientApplication::renderReload() {
  auto assets = m_root->assets();
  auto renderer = Application::renderer();

  auto loadEffectConfig = [&](String const& name) {
    String path = strf("/rendering/effects/{}.config", name);
    if (assets->assetExists(path)) {
      StringMap<String> shaders;
      auto config = assets->json(path);
      auto shaderConfig = config.getObject("effectShaders");
      for (auto& entry : shaderConfig) {
        if (entry.second.isType(Json::Type::String)) {
          String shader = entry.second.toString();
          if (!shader.hasChar('\n')) {
            auto shaderBytes = assets->bytes(AssetPath::relativeTo(path, shader));
            shader = std::string(shaderBytes->ptr(), shaderBytes->size());
          }
          shaders[entry.first] = shader;
        }
      }

      renderer->loadEffectConfig(name, config, shaders);
    } else
      Logger::warn("No rendering config found for renderer with id '{}'", renderer->rendererId());
  };

  renderer->loadConfig(assets->json("/rendering/opengl.config"));
  
  loadEffectConfig("world");
  loadEffectConfig("lightingPassthrough");
  loadEffectConfig("lightingSpread");
  loadEffectConfig("lightingPoint");
  loadEffectConfig("lightingUpscale");   // R-A Form 2: bicubic upscale pass (must be registered; switchEffectConfig silently no-ops on an unregistered effect)
  loadEffectConfig("backdropCompose");   // CM-1: merged env+parallax compose (two input textures -> one full-screen pass into "main")

  // define post process groups and set them to be enabled/disabled based on config
  
  auto config = m_root->configuration();
  if (!config->get(postProcessGroupsRoot).isType(Json::Type::Object))
    config->set(postProcessGroupsRoot, JsonObject());
  auto groupsConfig = config->get(postProcessGroupsRoot);
  
  m_postProcessGroups.clear();
  auto postProcessGroups = assets->json("/client.config:postProcessGroups").toObject();
  for (auto& pair : postProcessGroups) {
    auto name = pair.first;
    auto groupConfig = groupsConfig.opt(name);
    auto def = pair.second.getBool("enabledDefault",true);
    if (!groupConfig)
      config->setPath(strf("{}.{}", postProcessGroupsRoot, name),JsonObject());
    m_postProcessGroups.add(name,PostProcessGroup{ groupConfig ? groupConfig.value().getBool("enabled", def) : def });
  }
  
  // define post process layers and optionally assign them to groups
  m_postProcessLayers.clear();
  m_labelledPostProcessLayers.clear();
  auto postProcessLayers = assets->json("/client.config:postProcessLayers").toArray();
  for (auto& layer : postProcessLayers) {
    auto effects = jsonToStringList(layer.getArray("effects"));
    for (auto& effect : effects)
      loadEffectConfig(effect);
    PostProcessGroup* group = nullptr;
    auto gname = layer.optString("group");
    if (gname) {
      group = &m_postProcessGroups.get(gname.value());
    }
    // I'd think a string map for all of these would be better, but the order does matter here, and does make sense to depend on mod priority, so...
    // guess a string map of indices works
    // I tried pointers but for whatever reason the behaviour was highly inconsistent and only worked after reload...
    auto label = layer.optString("name");
    if (label) {
      m_labelledPostProcessLayers.add(label.value(),m_postProcessLayers.count());
    }
    m_postProcessLayers.append(PostProcessLayer{ std::move(effects), (unsigned)layer.getUInt("passes", 1), group });
  }

  loadEffectConfig("interface");
}

void ClientApplication::setPostProcessLayerPasses(String const& layer, unsigned const& passes) {
  m_postProcessLayers.at(m_labelledPostProcessLayers.get(layer)).passes = passes;
}
void ClientApplication::setPostProcessGroupEnabled(String const& group, bool const& enabled, Maybe<bool> const& save) {
  m_postProcessGroups.get(group).enabled = enabled;
  if (save && save.value())
    m_root->configuration()->setPath(strf("{}.{}.enabled", postProcessGroupsRoot, group),enabled);
}
bool ClientApplication::postProcessGroupEnabled(String const& group) {
  return m_postProcessGroups.get(group).enabled;
}

Json ClientApplication::postProcessGroups() {
  return m_root->assets()->json("/client.config:postProcessGroups");
}

unsigned ClientApplication::framesSkipped() const {
  return m_framesSkipped;
}

void ClientApplication::changeState(MainAppState newState) {
  MainAppState oldState = m_state;
  m_state = newState;
  auto& app = appController();

  if (m_state == MainAppState::Quit)
    app->quit();

  if (newState == MainAppState::Mods)
    m_cinematicOverlay->load(m_root->assets()->json("/cinematics/mods/modloading.cinematic"));

  if (newState == MainAppState::Splash) {
    m_cinematicOverlay->load(m_root->assets()->json("/cinematics/splash.cinematic"));
    m_rootLoader = Thread::invoke("Async root loader", [this]() {
        m_root->fullyLoad();
      });
  }

  if (oldState > MainAppState::Title && m_state <= MainAppState::Title) {
    if (m_universeClient)
      m_universeClient->disconnect();

    if (m_universeServer) {
      m_universeServer->stop();
      m_universeServer->join();
      m_universeServer.reset();
    }
    m_cinematicOverlay->stop();

    // Clear HTTP trust request callback
    LuaBindings::clearHttpTrustRequestCallback();

    m_mainInterface.reset();

    m_voice->clearSpeakers();

    if (auto p2pNetworkingService = app->p2pNetworkingService()) {
      p2pNetworkingService->setJoinUnavailable();
      p2pNetworkingService->setAcceptingP2PConnections(false);
    }
  }

  if (oldState > MainAppState::Title && m_state == MainAppState::Title) {
    m_titleScreen->resetState();
    m_mainMixer->setUniverseClient({});
  }
  if (oldState >= MainAppState::Title && m_state < MainAppState::Title) {
    m_playerStorage.reset();

    if (m_statistics) {
      m_statistics->writeStatistics();
      m_statistics.reset();
    }

    m_universeClient.reset();
    m_mainMixer->setUniverseClient({});
    m_titleScreen.reset();
  }

  if (oldState < MainAppState::Title && m_state >= MainAppState::Title) {
    if (m_rootLoader)
      m_rootLoader.finish();

    m_cinematicOverlay->stop();

    m_playerStorage = make_shared<PlayerStorage>(m_root->toStoragePath("player"));
    m_statistics = make_shared<Statistics>(m_root->toStoragePath("player"), app->statisticsService());
    m_universeClient = make_shared<UniverseClient>(m_playerStorage, m_statistics);

    m_universeClient->setLuaCallbacks("input", LuaBindings::makeInputCallbacks());
    m_universeClient->setLuaCallbacks("voice", LuaBindings::makeVoiceCallbacks());
    m_universeClient->setLuaCallbacks("camera", LuaBindings::makeCameraCallbacks(&m_worldPainter->camera()));
    m_universeClient->setLuaCallbacks("renderer", LuaBindings::makeRenderingCallbacks(this));

    Json alwaysAllow = m_root->configuration()->getPath("safe.alwaysAllowClipboard");
    m_universeClient->setLuaCallbacks("clipboard", LuaBindings::makeClipboardCallbacks(app, alwaysAllow && alwaysAllow.toBool()));
    const bool luaHttpEnabled = m_root->configuration()->getPath("safe.luaHttp.enabled").optBool().value(false);

    m_universeClient->setLuaCallbacks("http", LuaBindings::makeHttpCallbacks(luaHttpEnabled));

    auto heldScriptPanes = make_shared<List<MainInterface::ScriptPaneInfo>>();

    m_universeClient->playerReloadPreCallback() = [&, heldScriptPanes](bool resetInterface) {
      if (!resetInterface)
        return;

      m_mainInterface->takeScriptPanes(*heldScriptPanes);
    };

    m_universeClient->playerReloadCallback() = [&, heldScriptPanes](bool resetInterface) {
      auto paneManager = m_mainInterface->paneManager();
      if (auto inventory = paneManager->registeredPane<InventoryPane>(MainInterfacePanes::Inventory))
        inventory->clearChangedSlots();

      if (resetInterface) {
        m_mainInterface->reviveScriptPanes(*heldScriptPanes);
        heldScriptPanes->clear();
      }
    };

    m_mainMixer->setUniverseClient(m_universeClient);
    m_titleScreen = make_shared<TitleScreen>(m_playerStorage, m_mainMixer->mixer(), m_universeClient);
    if (auto renderer = Application::renderer())
      m_titleScreen->renderInit(renderer);
  }

  if (m_state == MainAppState::Title) {
    auto configuration = m_root->configuration();

    if (m_pendingMultiPlayerConnection) {
      if (auto address = m_pendingMultiPlayerConnection->server.ptr<HostAddressWithPort>()) {
        m_titleScreen->setMultiPlayerAddress(toString(address->address()));
        m_titleScreen->setMultiPlayerPort(toString(address->port()));
        m_titleScreen->setMultiPlayerAccount(configuration->getPath("title.multiPlayerAccount").toString());
        m_titleScreen->setMultiPlayerForceLegacy(configuration->getPath("title.multiPlayerForceLegacy").optBool().value(false));
        m_titleScreen->goToMultiPlayerSelectCharacter(false);
      } else {
        m_titleScreen->goToMultiPlayerSelectCharacter(true);
      }
    } else {
      m_titleScreen->setMultiPlayerAddress(configuration->getPath("title.multiPlayerAddress").toString());
      m_titleScreen->setMultiPlayerPort(configuration->getPath("title.multiPlayerPort").toString());
      m_titleScreen->setMultiPlayerAccount(configuration->getPath("title.multiPlayerAccount").toString());
      m_titleScreen->setMultiPlayerForceLegacy(configuration->getPath("title.multiPlayerForceLegacy").optBool().value(false));
    }
  }

  if (m_state > MainAppState::Title) {
    if (m_titleScreen->currentlySelectedPlayer()) {
      m_player = m_titleScreen->currentlySelectedPlayer();
    } else {
      if (auto uuid = m_playerStorage->playerUuidAt(0))
        m_player = m_playerStorage->loadPlayer(*uuid);

      if (!m_player) {
        setError("Error loading player!");
        return;
      }
    }

    m_mainMixer->setUniverseClient(m_universeClient);
    m_universeClient->setMainPlayer(m_player);
    m_cinematicOverlay->setPlayer(m_player);
    m_timeSinceJoin = (int64_t)Time::millisecondsSinceEpoch() / 1000;

    auto assets = m_root->assets();
    String loadingCinematic = assets->json("/client.config:loadingCinematic").toString();
    m_cinematicOverlay->load(assets->json(loadingCinematic));
    if (!m_player->log()->introComplete()) {
      String introCinematic = assets->json("/client.config:introCinematic").toString();
      introCinematic = introCinematic.replaceTags(StringMap<String>{{"species", m_player->species()}});
      m_player->setPendingCinematic(Json(introCinematic));
    } else {
      m_player->setPendingCinematic(Json());
    }

    if (m_state == MainAppState::MultiPlayer) {
      PacketSocketUPtr packetSocket;

      auto multiPlayerConnection = m_pendingMultiPlayerConnection.take();

      if (auto address = multiPlayerConnection.server.ptr<HostAddressWithPort>()) {
        try {
          packetSocket = TcpPacketSocket::open(TcpSocket::connectTo(*address));
        } catch (StarException const& e) {
          setError(strf("Join failed! Error connecting to '{}'", *address), e);
          return;
        }

      } else {
        auto p2pPeerId = multiPlayerConnection.server.ptr<P2PNetworkingPeerId>();

        if (auto p2pNetworkingService = app->p2pNetworkingService()) {
          auto result = p2pNetworkingService->connectToPeer(*p2pPeerId);
          if (result.isLeft()) {
            setError(strf("Cannot join peer: {}", result.left()));
            return;
          } else {
            packetSocket = P2PPacketSocket::open(std::move(result.right()));
          }
        } else {
          setError("Internal error, no p2p networking service when joining p2p networking peer");
          return;
        }
      }

      bool allowAssetsMismatch = m_root->configuration()->get("allowAssetsMismatch").toBool();
      if (auto errorMessage = m_universeClient->connect(UniverseConnection(std::move(packetSocket)), allowAssetsMismatch,
            multiPlayerConnection.account, multiPlayerConnection.password, multiPlayerConnection.forceLegacy)) {
        setError(*errorMessage);
        return;
      }

      if (auto address = multiPlayerConnection.server.ptr<HostAddressWithPort>())
        m_currentRemoteJoin = *address;
      else
        m_currentRemoteJoin.reset();

    } else {
      if (!m_universeServer) {
        try {
          m_universeServer = make_shared<UniverseServer>(m_root->toStoragePath("universe"));
          m_universeServer->start();
        } catch (StarException const& e) {
          setError("Unable to start local server", e);
          return;
        }
      }

      if (auto errorMessage = m_universeClient->connect(m_universeServer->addLocalClient(), "", "")) {
        setError(strf("Error connecting locally: {}", *errorMessage));
        return;
      }
    }

    m_titleScreen->stopMusic();

    m_universeClient->restartLua();
    m_mainInterface = make_shared<MainInterface>(m_universeClient, m_worldPainter, m_cinematicOverlay);
    m_universeClient->setLuaCallbacks("interface", LuaBindings::makeInterfaceCallbacks(m_mainInterface.get()));
    m_universeClient->setLuaCallbacks("chat", LuaBindings::makeChatCallbacks(m_mainInterface.get(), m_universeClient.get()));
    m_universeClient->setLuaCallbacks("celestial", LuaBindings::makeCelestialCallbacks(m_universeClient.get()));
    m_universeClient->setLuaCallbacks("team", LuaBindings::makeTeamClientCallbacks(m_universeClient->teamClient().get()));
    m_universeClient->setLuaCallbacks("world", LuaBindings::makeWorldCallbacks(m_universeClient->worldClient().get()));

    LuaBindings::setHttpTrustRequestCallback([mainInterface = m_mainInterface.get()](String const& domain) {
      const auto paneManager = mainInterface->paneManager();
      const auto httpTrustDialog = paneManager->registeredPane<HttpTrustDialog>(MainInterfacePanes::HttpTrustDialog);

      httpTrustDialog->displayRequest(domain, [domain](const HttpTrustReply reply, bool remember) {
        const bool allowed = (reply == HttpTrustReply::Allow);
        LuaBindings::handleHttpTrustReply(domain, allowed);
      });
      paneManager->displayRegisteredPane(MainInterfacePanes::HttpTrustDialog);
    });


    m_mainInterface->displayDefaultPanes();
    m_universeClient->startLuaScripts();

    m_mainMixer->setWorldPainter(m_worldPainter);

    if (auto renderer = Application::renderer()) {
      m_worldPainter->renderInit(renderer);
    }
  }
}

void ClientApplication::setError(String const& error) {
  Logger::error(error.utf8Ptr());
  m_errorScreen->setMessage(error);
  m_titleScreen->resetState();
  changeState(MainAppState::Title);
}

void ClientApplication::setError(String const& error, std::exception const& e) {
  Logger::error("{}\n{}", error, outputException(e, true));
  m_errorScreen->setMessage(strf("{}\n{}", error, outputException(e, false)));
  m_titleScreen->resetState();
  changeState(MainAppState::Title);
}

void ClientApplication::loadMods() {
  auto ugcService = appController()->userGeneratedContentService();
  auto configuration = m_root->configuration();
  bool includeUGC = configuration->get("includeUGC", m_root->settings().includeUGC).toBool();
  if (ugcService && includeUGC) {
    StringList modDirectories;
    Logger::info("Checking for user generated content...");
    for (auto& contentId : ugcService->subscribedContentIds()) {
      if (auto contentDirectory = ugcService->contentDownloadDirectory(contentId)) {
        Logger::info("Loading mods from user generated content with id '{}' from directory '{}'", contentId, *contentDirectory);
        modDirectories.append(*contentDirectory);
      } else {
        Logger::warn("User generated content with id '{}' is not available", contentId);
      }
    }

    if (modDirectories.empty()) {
      Logger::info("No subscribed user generated content");
    } else {
      Root::singleton().loadMods(modDirectories, false);
      auto assets = m_root->assets();
    }
  }
}

void ClientApplication::updateSteamFlatpakWarning(float) {
  if (m_errorScreen->accepted())
    changeState(MainAppState::Mods);
}

void ClientApplication::updateMods(float dt) {
  m_cinematicOverlay->update(dt);
  auto ugcService = appController()->userGeneratedContentService();
  auto configuration = m_root->configuration();
  bool includeUGC = configuration->get("includeUGC", m_root->settings().includeUGC).toBool();
  if (ugcService && includeUGC) {
    // Prevent unnecessary log spam when UGC needs to be downloaded
    if (!m_loggedUGCCheck) {
      Logger::info("Checking for user generated content updates...");
      m_loggedUGCCheck = true;
    }
    
    if (ugcService->triggerContentDownload() == UserGeneratedContentService::UGCState::NoDownload) {
      changeState(MainAppState::Splash);
    } else {
      if (ugcService->triggerContentDownload() == UserGeneratedContentService::UGCState::Finished) {
        Logger::info("Loading updated user generated content...");
        StringList modDirectories;
        for (auto& contentId : ugcService->subscribedContentIds()) {
          if (auto contentDirectory = ugcService->contentDownloadDirectory(contentId)) {
            Logger::info("Loading mods from user generated content with id '{}' from directory '{}'", contentId, *contentDirectory);
            modDirectories.append(*contentDirectory);
          } else {
            Logger::warn("User generated content with id '{}' is not available", contentId);
          }
        }

        if (modDirectories.empty()) {
          changeState(MainAppState::Splash);
        } else {
          Logger::info("Reloading to include updated user generated content");
          Root::singleton().loadMods(modDirectories);

          // We've just reloaded, so make sure to grab our config again!
          // If we don't do this, we'll be able to read modsWarningShown
          // just fine, but we won't be able to write it back to the file.
          configuration = m_root->configuration();
        }

        auto assets = m_root->assets();

        if (configuration->get("modsWarningShown").optBool().value()) {
          changeState(MainAppState::Splash);
        } else {
          configuration->set("modsWarningShown", true);
          m_errorScreen->setMessage(assets->json("/interface.config:modsWarningMessage").toString());
          changeState(MainAppState::ModsWarning);
        }
      }
    }
  } else {
    changeState(MainAppState::Splash);
  }
}

void ClientApplication::updateModsWarning(float) {
  if (m_errorScreen->accepted())
    changeState(MainAppState::Splash);
}

void ClientApplication::updateSplash(float dt) {
  m_cinematicOverlay->update(dt);
  if (!m_rootLoader.isRunning() && (m_cinematicOverlay->completable() || m_cinematicOverlay->completed()))
    changeState(MainAppState::Title);
}

void ClientApplication::updateError(float) {
  if (m_errorScreen->accepted())
    changeState(MainAppState::Title);
}

uint64_t ClientApplication::renderTestHash(Image const& frame, double* meanLuminance) const {
  XXHash64 hasher;
  hasher.push((char const*)frame.data(), (size_t)frame.width() * frame.height() * frame.bytesPerPixel());
  if (meanLuminance) {
    // A uniformly-black frame hashes perfectly stably -- and is exactly what the AA bug produced. Report
    // luminance too, so a STABLE hash can never be mistaken for a CORRECT one.
    double lum = 0.0;
    auto const* px = (float const*)frame.data();
    size_t n = (size_t)frame.width() * frame.height() * 3;
    for (size_t i = 0; i < n; ++i)
      lum += px[i];
    *meanLuminance = lum / (double)n;
  }
  return hasher.digest();
}

void ClientApplication::renderTestWritePng(Image const& frame, String const& path) const {
  try {
    // NO Y FLIP, AND THE FLIP THAT USED TO BE HERE MADE EVERY DUMPED FRAME UPSIDE DOWN. Both sides of this
    // copy are already bottom-up -- glReadPixels fills that way, and Image::set counts rows from the bottom
    // too -- so writing row y to row y is the identity, while the h-1-y that stood here inverted it. It went
    // unnoticed because nobody had cause to check which way up a diagnostic frame was until a diff had to be
    // located against the scene. Clamping float to 8-bit is lossy and fine: every verdict is taken on the
    // raw float data, never on a PNG.
    Image out(frame.size(), PixelFormat::RGB24);
    unsigned w = frame.width(), h = frame.height();
    auto const* px = (float const*)frame.data();
    for (unsigned y = 0; y < h; ++y) {
      for (unsigned x = 0; x < w; ++x) {
        float const* p = px + ((size_t)y * w + x) * 3;
        auto enc = [](float v) -> uint8_t {
          v = v <= 0.0f ? 0.0f : (v >= 1.0f ? 1.0f : v);
          return (uint8_t)(v * 255.0f + 0.5f);
        };
        out.set(x, y, Vec3B(enc(p[0]), enc(p[1]), enc(p[2])));
      }
    }
    out.writePng(File::open(path, IOMode::Write));
  } catch (std::exception const& e) {
    Logger::warn("[rendertest] could not write '{}': {}", path, outputException(e, false));
  }
}

void ClientApplication::renderTestDiffMap(Image const& a, Image const& b) const {
  unsigned w = a.width(), h = a.height();
  if (!w || !h || b.width() != w || b.height() != h)
    return;
  auto const* pa = (float const*)a.data();
  auto const* pb = (float const*)b.data();

  // WHERE, not how much. A lighting region is the visible window grown by a border, so an artefact from
  // the region BOUNDARY can only enter the frame from its perimeter -- it is dense at the edges and absent
  // in the middle. A scene-wide difference is flat across both of these breakdowns. The count-and-maxAbs
  // line cannot tell those two apart, and they call for opposite responses.
  static constexpr unsigned GX = 16, GY = 9;
  size_t gridDiff[GY][GX] = {};
  size_t gridTotal[GY][GX] = {};

  // Bands measure DENSITY, never raw count: the outer bands contain far fewer pixels than the inner ones,
  // so counts alone make a perfect edge band look like a minor contributor.
  static constexpr unsigned BAND_EDGE[] = {8, 16, 32, 64, 128, 256};
  static constexpr size_t NBANDS = sizeof(BAND_EDGE) / sizeof(BAND_EDGE[0]) + 1;
  size_t bandDiff[NBANDS] = {};
  size_t bandTotal[NBANDS] = {};

  for (unsigned y = 0; y < h; ++y) {
    unsigned sy = h - 1 - y;   // bottom-up buffer reported in screen order
    for (unsigned x = 0; x < w; ++x) {
      size_t i = ((size_t)y * w + x) * 3;
      bool diff = pa[i] != pb[i] || pa[i + 1] != pb[i + 1] || pa[i + 2] != pb[i + 2];

      unsigned gx = x * GX / w, gy = sy * GY / h;
      ++gridTotal[gy][gx];
      unsigned dx = x < w - 1 - x ? x : w - 1 - x;
      unsigned dy = sy < h - 1 - sy ? sy : h - 1 - sy;
      unsigned edge = dx < dy ? dx : dy;
      size_t band = NBANDS - 1;
      for (size_t k = 0; k + 1 < NBANDS; ++k) {
        if (edge < BAND_EDGE[k]) {
          band = k;
          break;
        }
      }
      ++bandTotal[band];
      if (diff) {
        ++gridDiff[gy][gx];
        ++bandDiff[band];
      }
    }
  }

  Logger::info("[rendertest] A/B diff by distance from nearest frame edge (density within each band):");
  for (size_t k = 0; k < NBANDS; ++k) {
    unsigned lo = k ? BAND_EDGE[k - 1] : 0;
    String range = k + 1 < NBANDS ? strf("{}-{}", lo, BAND_EDGE[k]) : strf("{}+", lo);
    Logger::info("[rendertest]   edge {:>9}px  {:7.3f}%  ({} / {})",
      range, bandTotal[k] ? 100.0 * (double)bandDiff[k] / (double)bandTotal[k] : 0.0, bandDiff[k], bandTotal[k]);
  }

  Logger::info("[rendertest] A/B diff map {}x{} (% of each cell differing, screen order, top row first):", GX, GY);
  for (unsigned gy = 0; gy < GY; ++gy) {
    String row;
    for (unsigned gx = 0; gx < GX; ++gx)
      row += strf("{:6.1f}", gridTotal[gy][gx] ? 100.0 * (double)gridDiff[gy][gx] / (double)gridTotal[gy][gx] : 0.0);
    Logger::info("[rendertest]  |{}", row);
  }

  if (m_renderTestOut.empty())
    return;
  renderTestWritePng(a, strf("{}/ab_legA.png", m_renderTestOut));
  renderTestWritePng(b, strf("{}/ab_legB.png", m_renderTestOut));
  try {
    // Amplified 20x: the differences this is built to inspect peak around 0.05, which is invisible at 1:1.
    Image d(a.size(), PixelFormat::RGB24);
    auto amp = [](float v) -> uint8_t {
      float m = (v < 0.0f ? -v : v) * 20.0f;
      return (uint8_t)((m >= 1.0f ? 1.0f : m) * 255.0f + 0.5f);
    };
    for (unsigned y = 0; y < h; ++y) {
      for (unsigned x = 0; x < w; ++x) {
        size_t i = ((size_t)y * w + x) * 3;
        d.set(x, y, Vec3B(amp(pa[i] - pb[i]), amp(pa[i + 1] - pb[i + 1]), amp(pa[i + 2] - pb[i + 2])));
      }
    }
    String path = strf("{}/ab_diff.png", m_renderTestOut);
    d.writePng(File::open(path, IOMode::Write));
    Logger::info("[rendertest] wrote {}/ab_{{legA,legB,diff}}.png (diff amplified 20x)", m_renderTestOut);
  } catch (std::exception const& e) {
    Logger::warn("[rendertest] could not write the diff PNG: {}", outputException(e, false));
  }
}

void ClientApplication::renderTestCapture() {
  auto& renderer = Application::renderer();

  // PHASE 1 -- LOAD, unpaused. The world must actually stream in; a paused world never populates.
  //
  // WE FREEZE ON QUIESCENCE, NOT ON A FRAME COUNT. This used to run a fixed number of frames and then freeze,
  // which sounds deterministic and is not: chunks and entities arrive ASYNCHRONOUSLY on the server thread, so
  // how much had loaded after N frames depended on wall-clock and thread scheduling. Two runs of the SAME
  // binary froze with 217 and 214 entities -- which is why the frozen-world frame hash "legitimately differed"
  // between runs, and why we could only ever certify a refactor against the in-frame oracles (env, parallax,
  // lighting) and never against the world pass, the entities, or the interface.
  //
  // Waiting for the entity count to HOLD STILL makes the settle point machine-speed-independent, which is
  // what the in-process A/B needs. IT DOES NOT CONVERGE THE WORLD, and this comment used to claim it made
  // the frozen hash a valid cross-binary golden. Measured against that claim: five settled runs at one
  // location froze at 268/268/271/267/270 entities, and the two that agreed on the COUNT still rendered
  // frames differing in 18.78% of pixels. A converged count is not a converged world.
  if (m_renderTestLoading) {
    ++m_renderTestFrame;

    size_t entities = m_renderData.entityDrawables.size();
    if (entities > 0 && entities == m_renderTestLastEntities)
      ++m_renderTestStable;
    else
      m_renderTestStable = 0;   // still arriving -- restart the count
    m_renderTestLastEntities = entities;

    // A PENDING WARP IS NOT A SETTLED WORLD. The DEPARTURE world is usually the quieter of the two -- the
    // ship holds a handful of entities and never moves -- so the stability counter fills to 90 while the
    // transition is still in flight, and quiescence fires on the world we are leaving. Measured: a warp
    // issued at t+0.1s and a load that "ended" 2.8s later still aboard the ship, with the destination
    // bookmark present and perfectly valid. Every earlier arrival was a race this happened to win.
    bool arrived = m_renderTestWarpWorldId.empty()
        || printWorldId(m_universeClient->playerWorld()) == m_renderTestWarpWorldId;
    if (!arrived)
      m_renderTestStable = 0;

    bool quiesced = arrived && m_renderTestStable >= m_renderTestQuiesce;
    bool timedOut = m_renderTestFrame >= m_renderTestLoad;

    if (quiesced || timedOut) {
      // STAR_RENDERTEST_NOFREEZE=1: leave the sim RUNNING. The frozen scene is required for the byte-identity
      // gate (it needs deterministic input), but it measures a FLOOR, not real play -- no entity animation,
      // no particles, no liquid motion, no lighting recomputes. For TIMING we do not need determinism, only
      // averages, so an unfrozen run is the honest number to compare against the Director's live HUD reading.
      static bool const noFreezeEnv = []() {
        char const* e = getenv("STAR_RENDERTEST_NOFREEZE");
        return e && *e && *e != '0';
      }();
      // A WALK IMPLIES NOFREEZE, and is not merely compatible with it: a frozen world does not tick, so the
      // player cannot move in it at all. Requiring the caller to remember both would make the failure silent --
      // the run would complete, report zero motion, and look like a renderer that never bypasses.
      bool const noFreeze = noFreezeEnv || m_renderTestWalk > 0;

      // ARRIVAL, NOT DEPARTURE. The warp was issued long ago; this is the first moment the load is over
      // and the question "are we actually there?" has an answer. A dead bookmark issues a warp that never
      // completes, and every downstream number then describes a location nobody asked for -- silently,
      // because the fingerprint's camera and entity count are perfectly self-consistent at the wrong place.
      if (!m_renderTestWarpWorldId.empty()) {
        String here = printWorldId(m_universeClient->playerWorld());
        if (here != m_renderTestWarpWorldId) {
          Logger::error("[rendertest] FAIL: asked for world {} but the load ended in {}. The warp was issued "
                        "and never arrived -- a dead teleport bookmark, or a destination that would not load. "
                        "Refusing to measure a location nobody chose.",
            m_renderTestWarpWorldId, here);
          appController()->quit();
          return;
        }
        Logger::info("[rendertest] arrived in {} (the world the bookmark named)", here);
      }

      m_renderTestLoading = false;
      m_renderTestFrozen = !noFreeze;
      if (m_renderTestWalk && !noFreezeEnv)
        Logger::info("[walk] STAR_RENDERTEST_WALK implies NOFREEZE -- the sim stays live so the player can move");

      // STAR_RENDERTEST_FULLBRIGHT=1 -- the RB-1 probe. The GPU lighting pass points the world effect's
      // `lightMap` sampler AT lightingGpuUpscaled's own colour attachment (setEffectTextureFromTarget).
      // Fullbright then uploads a 1x1 white image into that same sampler, and before the ownership fix the
      // upload setter's "reuse the texture I already have" branch re-specified the RENDER TARGET's storage.
      //
      // The pixel oracles are structurally blind to this: they run with GPU lighting ON and fullbright OFF, so
      // the alias and the upload never collide in a gated frame. This knob makes them collide, on purpose.
      static bool const fullbright = []() {
        char const* e = getenv("STAR_RENDERTEST_FULLBRIGHT");
        return e && *e && *e != '0';
      }();
      // STAR_RENDERTEST_HEADLESS=1 (#199). Turn OFF the client's view production while the sim keeps
      // running: entities still emit particles, audio and tile previews, and only drawables and overhead
      // bars are discarded. Wired into the harness rather than left as an unexercised API -- a capability
      // nothing runs is a capability nobody knows works.
      static bool const headless = []() {
        char const* e = getenv("STAR_RENDERTEST_HEADLESS");
        return e && *e && *e != '0';
      }();
      if (headless && m_universeClient && m_universeClient->worldClient())
        m_universeClient->worldClient()->setHeadless(true);

      if (fullbright && m_universeClient && m_universeClient->worldClient()) {
        m_universeClient->worldClient()->setFullBright(true);
        Logger::info("[texowner] fullbright FORCED -- the lightMap sampler aliases a live render target and is "
                     "about to be uploaded into");
      }

      if (quiesced)
        Logger::info("[rendertest] world QUIESCED after {} frames -- {} entities, stable for {} -- sim {}",
          m_renderTestFrame, entities, m_renderTestQuiesce, m_renderTestFrozen ? "FROZEN" : "RUNNING (nofreeze)");
      else
        // LOUD, because a hash taken from an unsettled world is a number that looks like a result and is not
        // one. Raise STAR_RENDERTEST_LOAD; do not quietly accept the frame.
        Logger::error("[rendertest] world did NOT settle: hit the {}-frame cap with {} entities (stable for only "
                      "{} of {} required). The frozen state is NOT reproducible and any cross-binary hash "
                      "comparison from this run is INVALID.",
          m_renderTestLoad, entities, m_renderTestStable, m_renderTestQuiesce);
    }
    return;
  }

  // THE RB-1 OBSERVABLE. RenderOracle::read() sizes the image it returns from the target's OWN recorded size
  // (writeFace().texture->textureSize) -- the very field the corruption overwrites. So asking the oracle how
  // big lightingGpuUpscaled is asks the target what it thinks it is, which is exactly the question.
  //
  // Report it every frozen frame while the probe is armed: RED is a target that has silently become 1x1.
  if (getenv("STAR_RENDERTEST_FULLBRIGHT") && m_renderTestSeen % 30 == 0) {
    auto renderer = Application::renderer();
    if (renderer->hasFrameBuffer("lightingGpuUpscaled")) {
      auto size = renderer->oracle().read("lightingGpuUpscaled").size();
      Logger::info("[texowner] lightingGpuUpscaled is {}x{}", size[0], size[1]);
    }
  }

  // PHASE 2 -- SETTLE, frozen. Caches, atlases and the lighting pipeline quiesce against a static world.
  if (m_renderTestSeen < m_renderTestWarmup) {
    ++m_renderTestSeen;
    if (m_renderTestSeen == m_renderTestWarmup) {
      Logger::info("[rendertest] settled ({} frozen frames)", m_renderTestWarmup);
      if (!m_renderTestAbKey.empty()) {
        m_renderTestAbPhase = 0;
        // Remember the shipped value. Configuration::set PERSISTS to storage/starbound.config on exit, so an
        // A/B run would otherwise PIN leg B's value into the harness config and silently poison every later
        // run. That is exactly the config-pinning trap this campaign has already been bitten by twice -- and it
        // bit the harness itself: a `lightingGpu=true|false` A/B left CPU lighting pinned on, and the next run
        // rendered a black world that looked exactly like the AA bug under investigation.
        //
        // getOrDefault, NOT get (#185). `get` returns a NULL Json for an absent key, and writing null back
        // through Configuration::set does not restore the key -- it ERASES it, and the erase persists on
        // exit. So an A/B on any key the tree failed to declare would silently strip that key from the
        // harness config forever, after which whatever literal the call site typed became the authority
        // for every later run. Not hypothetical: newLighting was undeclared until this change, and an
        // enable/disable knob is exactly what an A/B targets. Reading the declared default means the
        // restore always writes a real value back.
        m_renderTestAbOriginal = m_root->configuration()->getOrDefault(m_renderTestAbKey);
        m_root->configuration()->set(m_renderTestAbKey, m_renderTestAbA);
        Logger::info("[rendertest] A/B leg A: {} = {} (will restore {} on exit)",
          m_renderTestAbKey, m_renderTestAbA.repr(), m_renderTestAbOriginal.repr());
      }
    }
    return;
  }

  // PHASE 3a -- A/B GATE. Against the frozen world, render leg A, then leg B, and compare byte-for-byte.
  // Each leg gets m_renderTestWarmup frames to settle so retained caches actually refresh under the new
  // setting (otherwise leg B would be scored on leg A's stale cache contents).
  if (m_renderTestAbPhase >= 0) {
    unsigned legFrame = (m_renderTestSeen - m_renderTestWarmup) % (m_renderTestWarmup + 1);
    ++m_renderTestSeen;
    if (legFrame < m_renderTestWarmup)
      return;   // still settling this leg

    Image frame = renderer->oracle().read("main");
    if (frame.empty()) {
      Logger::error("[rendertest] FAIL: oracle().read('main') returned nothing (unsized? GL error?)");
      appController()->quit();
      return;
    }
    double lum = 0.0;
    uint64_t hash = renderTestHash(frame, &lum);

    if (m_renderTestAbPhase == 0) {
      m_renderTestAbHashA = hash;
      m_renderTestAbFrameA = frame;
      Logger::info("[rendertest] legA {} = {} -> hash={:016x} meanLuminance={:.6f}",
        m_renderTestAbKey, m_renderTestAbA.repr(), hash, lum);
      m_renderTestAbPhase = 1;
      Logger::info("[rendertest] A/B null control: re-rendering leg A, config untouched");
      return;
    }

    // THE NULL CONTROL, AND THIS INSTRUMENT SHIPPED WITHOUT ONE FOR WEEKS. An A/B reports that two legs
    // differ; it cannot, alone, tell you whether the LEVER did that or whether the harness would have
    // differed from itself anyway. Re-rendering leg A unchanged measures exactly that floor. Run against
    // a daylit surface scene it came back at 7.4% of pixels -- so the 13.2% once attributed to the
    // adaptive lighting border was the harness reading its own sun-ray animation. Any A/B verdict taken
    // below this floor is not evidence, and the gate must refuse it rather than report it.
    if (m_renderTestAbPhase == 1) {
      m_renderTestAbPhase = 2;
      if (hash == m_renderTestAbHashA) {
        Logger::info("[rendertest] ===== A/B NULL OK: leg A reproduced byte-identically =====");
      } else {
        Logger::error("[rendertest] ===== A/B NULL FAILED: leg A did not reproduce ({:016x} then {:016x}) =====",
          m_renderTestAbHashA, hash);
        Logger::error("[rendertest] the frozen scene is not deterministic across legs -- no A/B verdict from"
                      " this run means anything. The map below is the harness's own noise, not a lever.");
        renderTestDiffMap(m_renderTestAbFrameA, frame);
      }
      m_root->configuration()->set(m_renderTestAbKey, m_renderTestAbB);
      Logger::info("[rendertest] A/B leg B: {} = {}", m_renderTestAbKey, m_renderTestAbB.repr());
      return;
    }

    Logger::info("[rendertest] legB {} = {} -> hash={:016x} meanLuminance={:.6f}",
      m_renderTestAbKey, m_renderTestAbB.repr(), hash, lum);

    if (hash == m_renderTestAbHashA) {
      Logger::info("[rendertest] ===== A/B MATCH: byte-identical ({} : {} vs {}) =====",
        m_renderTestAbKey, m_renderTestAbA.repr(), m_renderTestAbB.repr());
    } else {
      // Quantify the divergence. A hash mismatch alone cannot distinguish a 1-LSB rounding difference on a
      // few soft-edge texels (expected for e.g. the premultiplied parallax cache) from a real corruption.
      size_t differing = 0;
      float maxAbs = 0.0f;
      auto const* a = (float const*)m_renderTestAbFrameA.data();
      auto const* b = (float const*)frame.data();
      size_t px = (size_t)frame.width() * frame.height();
      for (size_t i = 0; i < px; ++i) {
        bool diff = false;
        for (int c = 0; c < 3; ++c) {
          float d = a[i * 3 + c] - b[i * 3 + c];
          if (d != 0.0f) {
            diff = true;
            float ad = d < 0.0f ? -d : d;
            if (ad > maxAbs)
              maxAbs = ad;
          }
        }
        if (diff)
          ++differing;
      }
      Logger::error("[rendertest] ===== A/B DIFF: {} px ({:.4f}%) maxAbs={:.6f} ({} : {} vs {}) =====",
        differing, 100.0 * (double)differing / (double)px, maxAbs,
        m_renderTestAbKey, m_renderTestAbA.repr(), m_renderTestAbB.repr());
      renderTestDiffMap(m_renderTestAbFrameA, frame);
    }
    m_root->configuration()->set(m_renderTestAbKey, m_renderTestAbOriginal);   // never leave the pin behind
    renderTestMotionVerdict();
    renderTestRestoreConfig();
    appController()->quit();
    return;
  }

  // PHASE 3b -- plain golden capture (no A/B configured).
  unsigned index = m_renderTestSeen - m_renderTestWarmup;
  ++m_renderTestSeen;

  Image frame = renderer->oracle().read("main");
  if (frame.empty()) {
    // RenderOracle::read returns EMPTY (never zero-filled) on every unreadable condition, precisely so this
    // cannot silently pass. A zero-filled frame would hash consistently and report a stable false green.
    Logger::error("[rendertest] FAIL frame={} oracle().read('main') returned nothing (multisample? unsized? GL error?)", index);
    appController()->quit();
    return;
  }

  double lum = 0.0;
  uint64_t hash = renderTestHash(frame, &lum);

  // State fingerprint alongside the pixel hash. If two runs disagree on the HASH, this says WHICH input
  // drifted -- pixels alone cannot tell you whether the renderer changed or the world did.
  auto const& sky = m_renderData.skyRenderData;
  // dayLevel and skyAlpha are here because without them the fingerprint cannot say whether the SUN is
  // up -- and the sun rays are the backdrop's only animated term, so a determinism result taken at
  // night says nothing about them. A fingerprint that cannot distinguish "the term was pinned" from
  // "the term was never drawn" is not a fingerprint for this question.
  // WORLD FIRST. Every other field is self-consistent at the WRONG PLACE, so without the world id a run
  // that warped somewhere it did not intend reads as clean data -- which is how a dead bookmark cost a
  // whole sweep before anyone noticed, and it took dayLength archaeology to see it.
  Logger::info("[rendertest] frame={} hash={:016x} size={}x{} meanLuminance={:.6f}"
               " | world={} epochTime={:.4f} dayLength={:.2f} dayLevel={:.4f} skyAlpha={:.4f}"
               " camera=({:.4f},{:.4f}) parallaxLayers={} entities={}",
    index, hash, frame.width(), frame.height(), lum,
    printWorldId(m_universeClient->playerWorld()),
    sky.epochTime, sky.dayLength, sky.dayLevel, sky.skyAlpha,
    m_worldPainter->camera().centerWorldPosition()[0], m_worldPainter->camera().centerWorldPosition()[1],
    m_renderData.parallaxLayers.size(), m_renderData.entityDrawables.size());

  if (!m_renderTestOut.empty())
    renderTestWritePng(frame, strf("{}/frame_{:04d}.png", m_renderTestOut, index));

  if (index + 1 >= m_renderTestFrames) {
    Logger::info("[rendertest] DONE captured={} frames", m_renderTestFrames);
    renderTestMotionVerdict();
    renderTestRestoreConfig();
    appController()->quit();
  }
}

void ClientApplication::updateTitle(float dt) {
  // Render harness: skip the menus and drop straight into the world, exactly as TitleState::StartSinglePlayer
  // does. changeState(SinglePlayer) is what LOADS the player -- with no title-screen selection it falls back to
  // playerUuidAt(0) and calls setError() if the storage is empty -- so it must be called unconditionally rather
  // than gated on m_player, which is still null at this point.
  if (m_renderTestFrames && !m_renderTestEntered) {
    m_renderTestEntered = true;
    Logger::info("[rendertest] entering SinglePlayer");
    changeState(MainAppState::SinglePlayer);
    if (m_player) {
      Logger::info("[rendertest] player '{}' loaded", m_player->name());
      // Enumerate the player's teleport bookmarks so a measurement can be aimed at a REAL location (a base,
      // a planet surface) rather than only wherever the character happens to be parked. Without this the
      // harness can only ever measure the ship -- where, as it turns out, the parallax pass costs ZERO.
      for (auto const& b : m_player->universeMap()->teleportBookmarks())
        Logger::info("[rendertest] bookmark: '{}'  (world={})", b.bookmarkName, printWorldId(b.target.first));
    }
    return;
  }

  m_cinematicOverlay->update(dt);

  m_titleScreen->update(dt);
  m_mainMixer->update(dt);
  m_mainMixer->setSpeed(GlobalTimescale);

  auto& app = appController();
  bool inputActive = m_titleScreen->textInputActive();
  m_input->setTextInputActive(inputActive);
  if (inputActive)
    app->setTextArea(m_titleScreen->paneManager()->keyboardCapturedWidget()->keyboardCaptureArea());
  else
    app->setTextArea();
  app->setAcceptingTextInput(inputActive);

  auto p2pNetworkingService = app->p2pNetworkingService();
  if (p2pNetworkingService) {
    auto getStateString = [](TitleState state) -> const char* {
      switch (state) {
        case TitleState::Main:
          return "In Main Menu";
        case TitleState::Options:
          return "In Options";
        case TitleState::Mods:
          return "In Mods";
        case TitleState::SinglePlayerSelectCharacter:
          return "Selecting a character for singleplayer";
        case TitleState::SinglePlayerCreateCharacter:
          return "Creating a character for singleplayer";
        case TitleState::MultiPlayerSelectCharacter:
          return "Selecting a character for multiplayer";
        case TitleState::MultiPlayerCreateCharacter:
          return "Creating a character for multiplayer";
        case TitleState::MultiPlayerConnect:
          return "Awaiting multiplayer connection info";
        case TitleState::StartSinglePlayer:
          return "Loading Singleplayer";
        case TitleState::StartMultiPlayer:
          return "Connecting to Multiplayer";
        default:
          return "";
      }
    };

    p2pNetworkingService->setActivityData("Not In Game", getStateString(m_titleScreen->currentState()), 0, {});
  }

  if (m_titleScreen->currentState() == TitleState::StartSinglePlayer) {
    changeState(MainAppState::SinglePlayer);

  } else if (m_titleScreen->currentState() == TitleState::StartMultiPlayer) {
    if (!m_pendingMultiPlayerConnection || m_pendingMultiPlayerConnection->server.is<HostAddressWithPort>()) {
      auto addressString = m_titleScreen->multiPlayerAddress().trim();
      auto portString = m_titleScreen->multiPlayerPort().trim();
      portString = portString.empty() ? toString(m_root->configuration()->get("gameServerPort").toUInt()) : portString;
      if (auto port = maybeLexicalCast<uint16_t>(portString)) {
        auto address = HostAddressWithPort::lookup(addressString, *port);
        if (address.isLeft()) {
          setError(address.left());
        } else {
          m_pendingMultiPlayerConnection = PendingMultiPlayerConnection{
            address.right(),
            m_titleScreen->multiPlayerAccount(),
            m_titleScreen->multiPlayerPassword(),
            m_titleScreen->multiPlayerForceLegacy()
          };

          auto configuration = m_root->configuration();
          configuration->setPath("title.multiPlayerAddress", m_titleScreen->multiPlayerAddress());
          configuration->setPath("title.multiPlayerPort", m_titleScreen->multiPlayerPort());
          configuration->setPath("title.multiPlayerAccount", m_titleScreen->multiPlayerAccount());
          configuration->setPath("title.multiPlayerForceLegacy", m_titleScreen->multiPlayerForceLegacy());

          changeState(MainAppState::MultiPlayer);
        }
      } else {
        setError(strf("invalid port: {}", portString));
      }
    } else {
      changeState(MainAppState::MultiPlayer);
    }

  } else if (m_titleScreen->currentState() == TitleState::Quit) {
    changeState(MainAppState::Quit);
  }
}

// THE MOTION DRIVER (#174). A deterministic, frame-counted cycle: walk right, pause, walk left, pause.
//
// Why scripted rather than "the Director walks around": the same reason STAR_RENDERTEST_WARP exists. A human
// remembering to move produces a different path every run, and two runs that took different paths cannot be
// compared -- which is the entire product of a harness. Frame-counted legs give the same path every time on
// any machine, busy or idle.
//
// WHAT IT UNLOCKS, none of which any frozen run can reach: the parallax moving-camera BYPASS, the two
// mutually-exclusive compose arms, ParkFrames hysteresis and the still<->moving transition, the retained
// cache's scroll-shift path, adaptive-N (which derives from drift), and the whole of #177's env motion term.
// The audit priced that blindness from evidence rather than assertion: one wrong measurement (owner `gl` at
// 118.4%, parts exceeding the whole by 1279 us/tick) and one defect that reached the Director in play.
void ClientApplication::renderTestDriveMotion() {
  if (!m_player)
    return;

  unsigned t = m_renderTestWalkFrame++;

  if (m_renderTestWalk) {
    // Four legs of equal length. moveLeft/moveRight are the same calls the real input path makes
    // (StarClientApplication.cpp's binding handlers), so this drives the player exactly as a key would --
    // it is not a teleport, and the movement controller, collision and camera all see an ordinary walk.
    unsigned leg = (t / m_renderTestWalk) % 4;
    if (leg == 0)
      m_player->moveRight();
    else if (leg == 2)
      m_player->moveLeft();
    // legs 1 and 3 issue nothing: the pauses are what produce the still<->moving TRANSITION, and the
    // transition is where ParkFrames hysteresis and the bypass/compose arm-switch actually live. A harness
    // that only ever walked would exercise one arm and call it coverage.

    // SAY WHERE THE PLAYER ACTUALLY IS at each leg boundary. A walk driver that issues moves the world
    // ignores looks identical, in every counter, to a renderer that has stopped bypassing -- and the whole
    // point of this instrument is to tell those two apart. One line per leg, so the position is in the log
    // whether the run passes or fails.
    if ((t % m_renderTestWalk) == 0) {
      auto pos = m_player->position();
      Logger::info("[walk] frame={} leg={} ({}) player=({:.3f},{:.3f})", t, leg,
        leg == 0 ? "right" : (leg == 2 ? "left" : "pause"), pos[0], pos[1]);
    }
  }

  if (m_renderTestTogglePeriod && (t % m_renderTestTogglePeriod) == 0) {
    auto cfg = m_root->configuration();
    if (!m_renderTestConfigOriginals.contains(m_renderTestToggleKey))
      m_renderTestConfigOriginals[m_renderTestToggleKey] = cfg->getOrDefault(m_renderTestToggleKey);
    bool now = cfg->getOrDefault(m_renderTestToggleKey).optBool().value(false);
    cfg->set(m_renderTestToggleKey, !now);
    Logger::info("[walk] frame={} toggled {} -> {}", t, m_renderTestToggleKey, !now);
  }

  if (m_renderTestZoomPeriod && (t % m_renderTestZoomPeriod) == 0) {
    auto cfg = m_root->configuration();
    if (!m_renderTestConfigOriginals.contains("zoomLevel"))
      m_renderTestConfigOriginals["zoomLevel"] = cfg->getOrDefault("zoomLevel");
    float lv = m_renderTestZoomLevels[(t / m_renderTestZoomPeriod) % m_renderTestZoomLevels.size()];
    cfg->set("zoomLevel", lv);
    Logger::info("[walk] frame={} zoomLevel -> {:.2f}", t, lv);
  }
}

// Configuration::set PERSISTS to storage/starbound.config on exit. A motion run that left antiAliasing or
// zoomLevel flipped would silently poison every later run from the same install -- the config-pinning trap
// this campaign has been bitten by twice, once in the harness itself (a lightingGpu A/B left CPU lighting
// pinned on and the next run rendered a black world that looked exactly like the bug under investigation).
void ClientApplication::renderTestRestoreConfig() {
  auto cfg = m_root->configuration();
  for (auto const& kv : m_renderTestConfigOriginals) {
    cfg->set(kv.first, kv.second);
    Logger::info("[walk] restored {} = {}", kv.first, kv.second.repr());
  }
  m_renderTestConfigOriginals.clear();
}

// THE COUNTERS ORACLE -- the actual deliverable of #174, not the movement.
//
// Movement alone only EXERCISES the motion paths; it does not GATE them. What turns exercise into a gate is
// an invariant over the counters the motion path maintains:
//
//   (1) refreshed + skipped + bypassed_moving == the frames the pass actually ran. Every frame takes exactly
//       one of the three arms, so the three must partition the frames. A shortfall means an arm was taken
//       that nobody counts -- which is precisely the defect class that drove owner `gl` to 118.4%.
//   (2) bypassed_moving > 0. This is the one that could never be asserted before: the bypass only engages
//       when the camera MOVES, so on a frozen run it is structurally unreachable and its absence is
//       indistinguishable from a renderer that has stopped bypassing at all.
//
// The Director's live session already showed the partition holding exactly (1448 + 690 + 70 = 2208), so the
// invariant is known-true, not hoped-for. This makes it checkable without him. It also retires #136 item (f),
// which parked a counter-reading on the Director's eyes -- a counter-reading is something the substrate does.
void ClientApplication::renderTestMotionVerdict() {
  if (!m_renderTestWalk)
    return;

  auto counter = [](char const* key) {
    return Telemetry::counter(key,
      MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Frame, MetricRole::Detail}).value();
  };
  uint64_t refreshed = counter("render.cache.parallax.refreshed");
  uint64_t skipped   = counter("render.cache.parallax.skipped");
  uint64_t bypassed  = counter("render.cache.parallax.bypassed_moving");
  uint64_t total     = refreshed + skipped + bypassed;

  Logger::info("[walkoracle] parallax arms: refreshed={} skipped={} bypassed_moving={} total={} walkFrames={}",
    refreshed, skipped, bypassed, total, m_renderTestWalkFrame);

  if (bypassed == 0)
    Logger::error("[walkoracle] FAIL: bypassed_moving is ZERO after a scripted walk. Either the camera never "
                  "moved (the walk did not drive the player) or the moving-camera bypass has stopped engaging. "
                  "Both are defects, and neither is visible to any frozen run.");
  else if (refreshed == 0 && skipped == 0)
    Logger::error("[walkoracle] FAIL: every frame bypassed -- the parked arm never ran, so the pauses in the "
                  "walk cycle did not produce a still camera and the still<->moving transition is untested.");
  else
    Logger::info("[walkoracle] PASS: the moving-camera bypass engaged ({} frames) AND the parked arms ran "
                 "({} refreshed + {} skipped) -- both sides of the transition were exercised.",
      bypassed, refreshed, skipped);
}

void ClientApplication::updateRunning(float dt) {
  // Render harness: warp to a named teleport bookmark before measuring. Without this the harness can only ever
  // measure wherever the character is parked -- which was the SHIP, where the parallax pass costs exactly ZERO.
  // A whole day of parallax optimization was aimed at a pass that is not in the scene being complained about.
  if (m_renderTestFrames && !m_renderTestWarp.empty() && !m_renderTestWarped && m_player
      && m_universeClient && m_universeClient->worldClient() && m_universeClient->worldClient()->inWorld()) {
    String want = m_renderTestWarp.toLower();
    for (auto const& b : m_player->universeMap()->teleportBookmarks()) {
      if (b.bookmarkName.toLower().contains(want)) {
        Logger::info("[rendertest] WARPING to bookmark '{}' (world={})", b.bookmarkName, printWorldId(b.target.first));
        m_universeClient->warpPlayer(WarpToWorld(b.target.first, b.target.second), false);
        m_renderTestWarped = true;
        m_renderTestWarpWorldId = printWorldId(b.target.first);   // asserted on arrival, below
        // The destination world has to stream in from scratch, so restart the LOAD phase from here. Otherwise
        // the freeze would land mid-load and we would measure a half-built world.
        m_renderTestFrame = 0;
        break;
      }
    }
    if (!m_renderTestWarped) {
      Logger::error("[rendertest] FAIL: no teleport bookmark matching '{}'", m_renderTestWarp);
      appController()->quit();
      return;
    }
  }

  // Drive the scripted motion only AFTER the world has settled. Walking during the load phase would move the
  // player while chunks are still streaming, which changes what quiesces and makes the settle point itself
  // depend on the walk -- the harness would be measuring its own driver.
  if (m_renderTestFrames && !m_renderTestLoading
      && (m_renderTestWalk || m_renderTestTogglePeriod || m_renderTestZoomPeriod))
    renderTestDriveMotion();

  try {
    auto& app = appController();
    auto worldClient = m_universeClient->worldClient();
    auto p2pNetworkingService = app->p2pNetworkingService();
    bool clientIPJoinable = m_root->configuration()->get("clientIPJoinable").toBool();
    bool clientP2PJoinable = m_root->configuration()->get("clientP2PJoinable").toBool();
    Maybe<pair<uint16_t, uint16_t>> party = make_pair(m_universeClient->players(), m_universeClient->maxPlayers());

    if (m_state == MainAppState::MultiPlayer) {
      if (p2pNetworkingService) {
        p2pNetworkingService->setAcceptingP2PConnections(false);
        if (clientP2PJoinable && m_currentRemoteJoin)
          p2pNetworkingService->setJoinRemote(*m_currentRemoteJoin);
        else
          p2pNetworkingService->setJoinUnavailable();
      }
    } else {
      m_universeServer->setListeningTcp(clientIPJoinable);
      if (p2pNetworkingService) {
        p2pNetworkingService->setAcceptingP2PConnections(clientP2PJoinable);
        if (clientP2PJoinable) {
          p2pNetworkingService->setJoinLocal(m_universeServer->maxClients());
        } else {
          p2pNetworkingService->setJoinUnavailable();
          party = {};
        }
      }
    }
    
    if (p2pNetworkingService) {
      auto getActivityDetail = [&](String const& tag) -> String {
        if (tag == "playerName")
          return Text::stripEscapeCodes(m_player->name());
        if (tag == "playerHealth")
          return toString(m_player->health());
        if (tag == "playerMaxHealth")
          return toString(m_player->maxHealth());
        if (tag == "playerEnergy")
          return toString(m_player->energy());
        if (tag == "playerMaxEnergy")
          return toString(m_player->maxEnergy());
        if (tag == "playerBreath")
          return toString(m_player->breath());
        if (tag == "playerMaxBreath")
          return toString(m_player->maxBreath());
        if (tag == "playerXPos")
          return toString(round(m_player->position().x()));
        if (tag == "playerYPos")
          return toString(round(m_player->position().y()));
        if (tag == "worldName") {
          if (m_universeClient->clientContext()->playerWorldId().is<ClientShipWorldId>())
            return "Player Ship";
          else if (WorldTemplate const* worldTemplate = worldClient ? worldClient->currentTemplate().get() : nullptr) {
            auto worldName = worldTemplate->worldName();
            if (worldName.empty())
              return "In World";
            else
              return Text::stripEscapeCodes(worldName);
          }
          else
            return "Nowhere";
        }
        return "";
      };

      String finalDetails = "";
      Json activityDetails = m_root->configuration()->getPath("discord.activityDetails");
      if (activityDetails.isType(Json::Type::Array)) {
        StringList detailsList;
        for (auto& detail : activityDetails.iterateArray())
          detailsList.append(getActivityDetail(*detail.stringPtr()));
        finalDetails = detailsList.join("\n");
      } else if (activityDetails.isType(Json::Type::String))
        finalDetails = activityDetails.toString().lookupTags(getActivityDetail);

      p2pNetworkingService->setActivityData("In Game", finalDetails.utf8Ptr(), m_timeSinceJoin, party);
    }

    if (!m_mainInterface->inputFocus() && !m_cinematicOverlay->suppressInput()) {
      m_player->setShifting(isActionTaken(InterfaceAction::PlayerShifting));

      if (isActionTaken(InterfaceAction::PlayerRight))
        m_player->moveRight();
      if (isActionTaken(InterfaceAction::PlayerLeft))
        m_player->moveLeft();
      if (isActionTaken(InterfaceAction::PlayerUp))
        m_player->moveUp();
      if (isActionTaken(InterfaceAction::PlayerDown))
        m_player->moveDown();
      if (isActionTaken(InterfaceAction::PlayerJump))
        m_player->jump();

      if (isActionTaken(InterfaceAction::PlayerTechAction1))
        m_player->special(1);
      if (isActionTaken(InterfaceAction::PlayerTechAction2))
        m_player->special(2);
      if (isActionTaken(InterfaceAction::PlayerTechAction3))
        m_player->special(3);

      if (isActionTakenEdge(InterfaceAction::PlayerInteract))
        m_player->beginTrigger();
      else if (!isActionTaken(InterfaceAction::PlayerInteract))
        m_player->endTrigger();

      if (isActionTakenEdge(InterfaceAction::PlayerDropItem))
        m_player->dropItem();

      if (isActionTakenEdge(InterfaceAction::EmoteBlabbering))
        m_player->addEmote(HumanoidEmote::Blabbering);
      if (isActionTakenEdge(InterfaceAction::EmoteShouting))
        m_player->addEmote(HumanoidEmote::Shouting);
      if (isActionTakenEdge(InterfaceAction::EmoteHappy))
        m_player->addEmote(HumanoidEmote::Happy);
      if (isActionTakenEdge(InterfaceAction::EmoteSad))
        m_player->addEmote(HumanoidEmote::Sad);
      if (isActionTakenEdge(InterfaceAction::EmoteNeutral))
        m_player->addEmote(HumanoidEmote::NEUTRAL);
      if (isActionTakenEdge(InterfaceAction::EmoteLaugh))
        m_player->addEmote(HumanoidEmote::Laugh);
      if (isActionTakenEdge(InterfaceAction::EmoteAnnoyed))
        m_player->addEmote(HumanoidEmote::Annoyed);
      if (isActionTakenEdge(InterfaceAction::EmoteOh))
        m_player->addEmote(HumanoidEmote::Oh);
      if (isActionTakenEdge(InterfaceAction::EmoteOooh))
        m_player->addEmote(HumanoidEmote::OOOH);
      if (isActionTakenEdge(InterfaceAction::EmoteBlink))
        m_player->addEmote(HumanoidEmote::Blink);
      if (isActionTakenEdge(InterfaceAction::EmoteWink))
        m_player->addEmote(HumanoidEmote::Wink);
      if (isActionTakenEdge(InterfaceAction::EmoteEat))
        m_player->addEmote(HumanoidEmote::Eat);
      if (isActionTakenEdge(InterfaceAction::EmoteSleep))
        m_player->addEmote(HumanoidEmote::Sleep);

      if (int newZoomDirection = (int)m_input->bindHeld("opensb", "zoomIn") - (int)m_input->bindHeld("opensb", "zoomOut"))
        m_cameraZoomDirection = newZoomDirection;
    }
    if (m_cameraZoomDirection != 0) {
      const float threshold = 0.01f;
      bool goingIn = m_cameraZoomDirection == 1;
      auto config = m_root->configuration();
      float curZoom = config->get("zoomLevel").toFloat(),
            newZoom = max(1.f, curZoom * powf(1.f + (float)m_cameraZoomDirection * 0.5f, min(1.f, dt * 5.f))),
            intZoom = max(1.f, (goingIn ? floor(curZoom) : ceil(curZoom)) + m_cameraZoomDirection);
      bool pastInt = goingIn ? newZoom + threshold > intZoom
                             : newZoom - threshold < intZoom;
      if (pastInt) {
        float intNewZoom = goingIn ? ceil(newZoom) : floor(newZoom);
        newZoom = lerp(clamp(abs(intZoom - newZoom) - 1.f, 0.f, 1.f), intZoom, intNewZoom);
        m_cameraZoomDirection = 0;
      }
      config->set("zoomLevel", min(1000000.f, newZoom));
    }

    if (m_controllerInput && m_controllerLeftStick.magnitudeSquared() > 0.01f)
      m_player->setMoveVector(m_controllerLeftStick);
    else
      m_player->setMoveVector(Vec2F());

    m_voice->setInput(m_input->bindHeld("opensb", "pushToTalk"));
    DataStreamBuffer voiceData;
    voiceData.setByteOrder(ByteOrder::LittleEndian);
    //voiceData.writeBytes(VoiceBroadcastPrefix.utf8Bytes()); transmitting with SE compat for now
    bool needstoSendVoice = m_voice->send(voiceData, 5000);

    auto checkDisconnection = [this]() {
      if (!m_universeClient->isConnected()) {
        m_cinematicOverlay->stop();
        String errMessage;
        if (auto disconnectReason = m_universeClient->disconnectReason())
          errMessage = strf("You were disconnected from the server for the following reason:\n{}", *disconnectReason);
        else
          errMessage = "Client-server connection no longer valid!";
        setError(errMessage);
        changeState(MainAppState::Title);
        return true;
      }

      return false;
    };

    if (checkDisconnection())
      return;

    m_mainInterface->preUpdate(dt);
    m_universeClient->update(dt);

    if (checkDisconnection())
      return;

    if (worldClient) {
      // THE HARNESS FREEZES THE SIM, NOT THE CLOCK, AND THAT INVALIDATED EVERY A/B RUN IN DAYLIGHT.
      // m_universeServer->setPause stops the world; this call is on the RENDER side and kept running, so
      // EnvironmentPainter::update went on advancing the sun-ray timer that feeds its per-ray alpha. The
      // A/B renders its legs seconds apart, so the rays had moved between them: an A/B holding IDENTICAL
      // config measured 7.4% of pixels differing at maxAbs 0.055 -- larger than the "cost" it was being
      // used to attribute to a lighting lever. A frozen scene has to be frozen to the renderer too.
      m_worldPainter->update(m_renderTestFrozen ? 0.0f : dt);
      auto& broadcastCallback = worldClient->broadcastCallback();
      if (!broadcastCallback) {
        broadcastCallback = [&](PlayerPtr player, StringView broadcast) -> bool {
          auto& view = broadcast.utf8();
          if (view.rfind(VoiceBroadcastPrefix.utf8(), 0) != NPos) {
            auto entityId = player->entityId();
            auto speaker = m_voice->speaker(connectionForEntity(entityId));
            speaker->entityId = entityId;
            speaker->name = player->name();
            speaker->position = player->mouthPosition();
            m_voice->receive(speaker, view.substr(VoiceBroadcastPrefix.utf8Size()));
          }
          return true;
        };
      }

      if (worldClient->inWorld()) {
        if (needstoSendVoice) {
          auto signature = Curve25519::sign(voiceData.ptr(), voiceData.size());
          std::string_view signatureView((char*)signature.data(), signature.size());
          std::string_view audioDataView(voiceData.ptr(), voiceData.size());
          auto broadcast = strf("data\0voice\0{}{}"s, signatureView, audioDataView);
          worldClient->sendSecretBroadcast(broadcast, true, false); // Already compressed by Opus.
        }
        if (auto mainPlayer = m_universeClient->mainPlayer()) {
          auto localSpeaker = m_voice->localSpeaker();
          localSpeaker->position = mainPlayer->position();
          localSpeaker->entityId = mainPlayer->entityId();
          localSpeaker->name = mainPlayer->name();
        }
        m_voice->setLocalSpeaker(worldClient->connection());
      }
      worldClient->setInteractiveHighlightMode(isActionTaken(InterfaceAction::ShowLabels));
    }
    updateCamera(dt);

    m_cinematicOverlay->update(dt);
    m_mainInterface->update(dt);
    m_mainMixer->update(dt, m_cinematicOverlay->muteSfx(), m_cinematicOverlay->muteMusic());
    m_mainMixer->setSpeed(GlobalTimescale);

    bool inputActive = m_mainInterface->textInputActive();
    m_input->setTextInputActive(inputActive);
    if (inputActive)
      app->setTextArea(m_mainInterface->paneManager()->keyboardCapturedWidget()->keyboardCaptureArea());
    else
      app->setTextArea();
    app->setAcceptingTextInput(inputActive);

    for (auto const& interactAction : m_player->pullInteractActions())
      m_mainInterface->handleInteractAction(interactAction);

    if (m_universeServer) {
      if (auto p2pNetworkingService = app->p2pNetworkingService()) {
        for (auto& p2pClient : p2pNetworkingService->acceptP2PConnections())
          m_universeServer->addClient(UniverseConnection(P2PPacketSocket::open(std::move(p2pClient))));
      }

      // Render harness: LOAD FIRST, THEN FREEZE. The world sim advances on wall-clock, so without a freeze two
      // runs reach a capture frame having ticked a different number of times and the golden hash is worthless.
      // But pausing from frame 0 does NOT work: the world never streams in, and the capture is the player alone
      // in empty space with the entire ship missing (observed). setPause stops the world POPULATING, not just
      // ticking. So run LOAD frames unpaused to let the world arrive, then freeze it for the rest of the run.
      m_universeServer->setPause(m_renderTestFrames ? m_renderTestFrozen : m_mainInterface->escapeDialogOpen());
    }

    Vec2F aimPosition = m_player->aimPosition();
    float fps = app->renderFps();
    LogMap::set("client_render_rate", strf("{:4.2f} FPS ({:4.2f}ms)", fps, (1.0f / app->renderFps()) * 1000.0f));
    LogMap::set("client_update_rate", strf("{:4.2f}Hz", app->updateRate()));
    LogMap::set("player_pos", strf("[ ^#f45;{:4.2f}^reset;, ^#49f;{:4.2f}^reset; ]", m_player->position()[0], m_player->position()[1]));
    LogMap::set("player_vel", strf("[ ^#f45;{:4.2f}^reset;, ^#49f;{:4.2f}^reset; ]", m_player->velocity()[0], m_player->velocity()[1]));
    LogMap::set("player_aim", strf("[ ^#f45;{:4.2f}^reset;, ^#49f;{:4.2f}^reset; ]", aimPosition[0], aimPosition[1]));
    if (auto world = m_universeClient->worldClient()) {
      auto aim = Vec2I::floor(aimPosition);
      LogMap::set("tile_liquid_level", toString(world->liquidLevel(aim).level));
      LogMap::set("tile_dungeon_id", world->isTileProtected(aim) ? strf("^red;{}", world->dungeonId(aim)) : toString(world->dungeonId(aim)));
    }

    if (m_mainInterface->currentState() == MainInterface::ReturnToTitle)
      changeState(MainAppState::Title);

  } catch (std::exception& e) {
    setError("Exception caught in client main-loop", e);
  }
}

bool ClientApplication::isActionTaken(InterfaceAction action) const {
  for (auto keyEvent : m_heldKeyEvents) {
    if (m_guiContext->actions(keyEvent).contains(action))
      return true;
  }

  return false;
}

bool ClientApplication::isActionTakenEdge(InterfaceAction action) const {
  for (auto keyEvent : m_edgeKeyEvents) {
    if (m_guiContext->actions(keyEvent).contains(action))
      return true;
  }

  return false;
}

void ClientApplication::updateCamera(float dt) {
  if (!m_universeClient->worldClient())
    return;

  WorldCamera& camera = m_worldPainter->camera();
  camera.update(dt);

  if (m_mainInterface->fixedCamera())
    return;

  auto assets = m_root->assets();

  const float triggerRadius = 100.0f;
  const float deadzone = 0.1f;
  const float panFactor = 1.5f;
  float cameraSpeedFactor = 30.0f / m_root->configuration()->get("cameraSpeedFactor").toFloat();
  cameraSpeedFactor /= (dt * 60.f);

  auto playerCameraPosition = m_player->cameraPosition();

  if (isActionTaken(InterfaceAction::CameraShift)) {
    m_snapBackCameraOffset = false;
    m_cameraOffsetDownTime += dt;
    Vec2F aim = m_universeClient->worldClient()->geometry().diff(m_mainInterface->cursorWorldPosition(), playerCameraPosition);

    float magnitude = aim.magnitude() / (triggerRadius / camera.pixelRatio());
    if (magnitude > deadzone) {
      float cameraXOffset = aim.x() / magnitude;
      float cameraYOffset = aim.y() / magnitude;
      magnitude = (magnitude - deadzone) / (1.0 - deadzone);
      if (magnitude > 1)
        magnitude = 1;
      cameraXOffset *= magnitude * 0.5f * camera.pixelRatio() * panFactor;
      cameraYOffset *= magnitude * 0.5f * camera.pixelRatio() * panFactor;
      m_cameraXOffset = (m_cameraXOffset * (cameraSpeedFactor - 1.0) + cameraXOffset) / cameraSpeedFactor;
      m_cameraYOffset = (m_cameraYOffset * (cameraSpeedFactor - 1.0) + cameraYOffset) / cameraSpeedFactor;
    }
  } else {
    if (m_cameraOffsetDownTime > 0.0f && m_cameraOffsetDownTime < 0.333333f)
      m_snapBackCameraOffset = true;
    if (m_snapBackCameraOffset) {
      m_cameraXOffset = (m_cameraXOffset * (cameraSpeedFactor - 1.0)) / cameraSpeedFactor;
      m_cameraYOffset = (m_cameraYOffset * (cameraSpeedFactor - 1.0)) / cameraSpeedFactor;
    }
    m_cameraOffsetDownTime = 0.f;
  }
  Vec2F newCameraPosition;

  newCameraPosition.setX(playerCameraPosition.x());
  newCameraPosition.setY(playerCameraPosition.y());

  auto baseCamera = newCameraPosition;

  const float cameraSmoothRadius = assets->json("/interface.config:cameraSmoothRadius").toFloat();
  const float cameraSmoothFactor = assets->json("/interface.config:cameraSmoothFactor").toFloat();

  auto cameraSmoothDistance = m_universeClient->worldClient()->geometry().diff(m_cameraPositionSmoother, newCameraPosition).magnitude();
  if (cameraSmoothDistance > cameraSmoothRadius) {
    auto cameraDelta = m_universeClient->worldClient()->geometry().diff(m_cameraPositionSmoother, newCameraPosition);
    m_cameraPositionSmoother = newCameraPosition + cameraDelta.normalized() * cameraSmoothRadius;
    m_cameraSmoothDelta = {};
  }

  auto cameraDelta = m_universeClient->worldClient()->geometry().diff(m_cameraPositionSmoother, newCameraPosition);
  if (cameraDelta.magnitude() > assets->json("/interface.config:cameraSmoothDeadzone").toFloat())
    newCameraPosition = newCameraPosition + cameraDelta * (cameraSmoothFactor - 1.0) / cameraSmoothFactor;
  m_cameraPositionSmoother = newCameraPosition;

  newCameraPosition.setX(newCameraPosition.x() + m_cameraXOffset / camera.pixelRatio());
  newCameraPosition.setY(newCameraPosition.y() + m_cameraYOffset / camera.pixelRatio());

  auto smoothDelta = newCameraPosition - baseCamera;

  m_worldPainter->setCameraPosition(m_universeClient->worldClient()->geometry(), baseCamera + (smoothDelta + m_cameraSmoothDelta) * 0.5f);
  m_cameraSmoothDelta = smoothDelta;

  m_universeClient->worldClient()->setClientWindow(camera.worldTileRect());
}

}

STAR_MAIN_APPLICATION(Star::ClientApplication);
