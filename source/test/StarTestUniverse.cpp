#include "StarTestUniverse.hpp"
#include "StarFile.hpp"
#include "StarQuests.hpp"
#include "StarPlayerFactory.hpp"
#include "StarPlayerStorage.hpp"
#include "StarStatistics.hpp"
#include "StarStatisticsService.hpp"
#include "StarPlayer.hpp"
#include "StarAssets.hpp"
#include "StarWorldClient.hpp"
#include "StarInputLuaBindings.hpp"

namespace Star {

TestUniverse::TestUniverse(Vec2U clientWindowSize) {
  auto& root = Root::singleton();

  m_clientWindowSize = clientWindowSize;

  m_storagePath = File::temporaryDirectory();
  auto playerStorage = make_shared<PlayerStorage>(File::relativeTo(m_storagePath, "player"));
  auto statistics = make_shared<Statistics>(File::relativeTo(m_storagePath, "statistics"));
  m_server = make_shared<UniverseServer>(File::relativeTo(m_storagePath, "universe"));
  m_client = make_shared<UniverseClient>(playerStorage, statistics);

  m_server->start();

  // Same Lua environment the real client installs (ClientApplication::run does this). Without it, the shipped
  // player scripts hit a nil `input` on their first update. See the m_input comment in the header.
  m_client->setLuaCallbacks("input", LuaBindings::makeInputCallbacks());

  m_mainPlayer = root.playerFactory()->create();
  m_mainPlayer->finalizeCreation();
  m_mainPlayer->setAdmin(true);
  m_mainPlayer->setModeType(PlayerMode::Survival);
  m_client->setMainPlayer(m_mainPlayer);
  m_client->connect(m_server->addLocalClient(), "test", "");
}

TestUniverse::~TestUniverse() {
  m_client = {};
  m_server = {};
  m_mainPlayer = {};
  File::removeDirectoryRecursive(m_storagePath);
}

void TestUniverse::warpPlayer(WorldId worldId) {
  // BOUNDED. This wait used to be `while (teleporting || playerWorld().empty())` with no exit -- so when the
  // warp could never complete (UniverseServer::triggerWorldCreation permanently returns nullptr once the world
  // promise has thrown), the test did not fail, it HUNG. A suite that hangs cannot return a verdict, and a gate
  // that cannot return a verdict certifies nothing -- it took the whole game_tests suite down with it.
  //
  // Time out and throw, so the underlying failure surfaces as a diagnosable red instead of an infinite wait.
  // The world-creation error itself is logged by UniverseServer ("error during world create").
  unsigned const MaxFrames = 60 * 30;   // 30s at the 16ms step below

  m_client->warpPlayer(WarpToWorld(worldId), true);
  for (unsigned frame = 0; m_mainPlayer->isTeleporting() || m_client->playerWorld().empty(); ++frame) {
    if (frame >= MaxFrames)
      throw StarException::format(
          "TestUniverse::warpPlayer timed out after {} frames warping to '{}' (teleporting={} playerWorld='{}'). "
          "The world never came up -- check the log for 'error during world create'.",
          MaxFrames, worldId, m_mainPlayer->isTeleporting(), m_client->playerWorld());
    m_client->update(0.016f);
    Thread::sleep(16);
  }
}

WorldId TestUniverse::currentPlayerWorld() const {
  return m_client->clientContext()->playerWorldId();
}

void TestUniverse::update(unsigned times) {
  for (unsigned i = 0; i < times; ++i) {
    m_client->update(0.016f);
    Thread::sleep(16);
  }
}

List<Drawable> TestUniverse::currentClientDrawables() {
  WorldRenderData renderData;
  auto worldClient = m_client->worldClient();
  worldClient->centerClientWindowOnPlayer(m_clientWindowSize);
  worldClient->render(renderData, 0);

  List<Drawable> drawables;
  for (auto& ed : renderData.entityDrawables) {
    for (auto& p : ed.layers)
      drawables.appendAll(std::move(p.second));
  }

  return drawables;
}

}
