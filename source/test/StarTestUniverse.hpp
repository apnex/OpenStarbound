#include "StarPlayer.hpp"
#include "StarRoot.hpp"
#include "StarInput.hpp"
#include "StarUniverseClient.hpp"
#include "StarUniverseServer.hpp"

namespace Star {

class TestUniverse {
public:
  TestUniverse(Vec2U clientWindowSize);
  ~TestUniverse();

  void warpPlayer(WorldId worldId);
  WorldId currentPlayerWorld() const;

  void update(unsigned times = 1);

  List<Drawable> currentClientDrawables();

private:
  Vec2U m_clientWindowSize;
  String m_storagePath;
  // The player Lua scripts shipped in assets/opensb (copy_paste.lua and friends) call input.bindDown() on
  // EVERY update, but the "input" callback table is installed by ClientApplication -- the GUI app -- and
  // nothing else. A UniverseClient built directly (this class; equally any headless or bot client) therefore
  // hands those scripts a nil `input` and they throw once per frame. Input's constructor is headless-safe (it
  // touches no SDL, only the binding configs), so own one and present the same Lua environment a real client
  // does, rather than teaching the scripts to tolerate a half-built world.
  Input m_input;
  UniverseServerPtr m_server;
  UniverseClientPtr m_client;
  PlayerPtr m_mainPlayer;
};

}
