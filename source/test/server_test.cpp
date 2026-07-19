#include "StarUniverseServer.hpp"
#include "StarRoot.hpp"
#include "StarWorldServer.hpp"
#include "StarWorldServerThread.hpp"
#include "StarWarping.hpp"
#include "StarFile.hpp"
#include "StarThread.hpp"

#include "StarTelemetry.hpp"
#include "StarJson.hpp"

#include "gtest/gtest.h"

using namespace Star;

TEST(ServerTest, Run) {
  UniverseServer server(Root::singleton().toStoragePath("universe"));
  server.start();
  server.stop();
  server.join();
}

// Task 4: prove the four-phase server-tick timers populate on a real running server tick loop.
// Timers are deep-gated, so deep tracing is enabled before any world ticks. We stand up a flat
// WorldServer (the same (size, ephemeralFile) construction the engine uses for client ship worlds)
// wrapped in a WorldServerThread, and let its run() loop drive WorldServerThread::update ->
// WorldServer::update for many ticks. We then assert each phase timer recorded at least one sample
// and the tick sequence advanced. No client/ship is involved, so the run logs no errors and the
// strict ErrorLogSink stays quiet (the UniverseServer/client path's default-species ship world is
// deliberately avoided here).
TEST(TelemetryServerTick, PhaseTimersPopulateUnderDeepTracing) {
  Telemetry::reset();
  Telemetry::setDeepEnabled(true);

  auto worldServer = make_shared<WorldServer>(Vec2U(2048, 2048), File::ephemeralFile());
  worldServer->setSpawningEnabled(false);

  auto worldThread = make_shared<WorldServerThread>(worldServer, WorldId(InstanceWorldId("telemetrytest")));
  worldThread->start();
  Thread::sleep(500); // let the server tick loop run for many ticks (~30 at 60Hz)
  worldThread->stop(); // signals stop and joins

  ASSERT_FALSE(worldThread->serverErrorOccurred());

  Json snap = Telemetry::snapshot();
  Json timers = snap.getObject("timers");
  EXPECT_GE(timers.get("tick.server.publish.us").getUInt("count"), 1u);
  EXPECT_GE(timers.get("tick.server.compute.us").getUInt("count"), 1u);
  EXPECT_GE(timers.get("tick.server.commit.us").getUInt("count"), 1u);
  EXPECT_GE(timers.get("tick.server.sync.us").getUInt("count"), 1u);
  EXPECT_GE(Telemetry::counter("tick.server.seq").value(), 1u);

  Telemetry::setDeepEnabled(false);
}
