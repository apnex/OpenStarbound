// RustBound M52 L8c-mech: what the fork's per-tick armour check COSTS, as the oracle side of a
// like-for-like comparison with RustBound's `Wardrobe::dress` (its own probe is
// crates/appearance/tests/dress_cost.rs in the RustBound tree).
//
//   STAR_ARMOR_COST=1 taskset -c 6-15 ./game_tests --gtest_filter='ArmorWearerCost.*'
//
// Skipped unless STAR_ARMOR_COST is set: a timing, run deliberately and pinned to the E-cores.
//
// Npc::tickShared calls ArmorWearer::setupHumanoid on EVERY tick, and Npc::update reaches
// tickShared on both its master and its slave branch. setupHumanoid is itself change-gated: every
// call scans all twenty ArmorWearer::m_armors, and only a slot flagged Armor::needsSync is rebuilt
// through Humanoid::setWearableFrom*. So two arms, both on the intro's NPC 279 as its npctype
// generates it:
//   - scan: setupHumanoid with nothing changed - the per-tick cost;
//   - sync: the same items alternated with a different colourIndex before each call, so both the
//     chest and the legs slots rebuild - the per-change cost.
//
// NOT CLAIMED: the call site's Npc::forceNude() (a StatusController::statPositive lookup) is not in
// either arm, so the per-tick number is a FLOOR of what a tick spends here. The return value is never
// read: on the no-sync path setupHumanoid returns its local movementParametersChanged uninitialised.

#include "StarRoot.hpp"
#include "StarNpcDatabase.hpp"
#include "StarItemDatabase.hpp"
#include "StarArmorWearer.hpp"
#include "StarArmors.hpp"
#include "StarHumanoid.hpp"

#include "gtest/gtest.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

using namespace Star;

namespace {

std::string cpusAllowed() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line))
    if (line.rfind("Cpus_allowed_list:", 0) == 0)
      return line;
  return "Cpus_allowed_list: unknown";
}

double nsPerCall(std::chrono::steady_clock::time_point start, size_t calls) {
  return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (double)calls;
}

}

TEST(ArmorWearerCost, ScanAndSyncOnTheIntroNpc) {
  if (!std::getenv("STAR_ARMOR_COST"))
    GTEST_SKIP() << "a timing measurement; run with STAR_ARMOR_COST=1, pinned to the E-cores";

  auto& root = Root::singleton();
  NpcVariant variant = root.npcDatabase()->generateNpcVariant("human", "protectoratelobbystudent2", 1.0f, 0, Json());
  Humanoid humanoid(variant.humanoidIdentity, variant.humanoidParameters, variant.uniqueHumanoidConfig ? variant.humanoidConfig : Json());

  auto itemDatabase = root.itemDatabase();
  auto chest = as<ChestArmor>(itemDatabase->item(variant.items.get("chest")));
  auto legs = as<LegsArmor>(itemDatabase->item(variant.items.get("legs")));
  ASSERT_TRUE(chest && legs) << "the npctype's chest and legs are armour";
  auto chestOther = as<ChestArmor>(itemDatabase->item(ItemDescriptor(chest->name(), 1, JsonObject{{"colorIndex", 3}})));
  auto legsOther = as<LegsArmor>(itemDatabase->item(ItemDescriptor(legs->name(), 1, JsonObject{{"colorIndex", 3}})));
  ASSERT_TRUE(chestOther && legsOther);

  ArmorWearer armor;
  armor.setChestItem(chest);
  armor.setLegsItem(legs);
  armor.setupHumanoid(humanoid, false);

  size_t const scanCalls = 200000;
  size_t const syncCalls = 20000;
  int const passes = 7;
  double scan = std::numeric_limits<double>::max();
  double sync = std::numeric_limits<double>::max();
  for (int pass = 0; pass != passes; ++pass) {
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i != scanCalls; ++i)
      armor.setupHumanoid(humanoid, false);
    scan = std::min(scan, nsPerCall(start, scanCalls));

    start = std::chrono::steady_clock::now();
    for (size_t i = 0; i != syncCalls; ++i) {
      bool odd = (i & 1) != 0;
      armor.setChestItem(odd ? chestOther : chest);
      armor.setLegsItem(odd ? legsOther : legs);
      armor.setupHumanoid(humanoid, false);
    }
    sync = std::min(sync, nsPerCall(start, syncCalls));
    // Leave the wearer as the scan arm expects it: the npctype's own items, synced.
    armor.setChestItem(chest);
    armor.setLegsItem(legs);
    armor.setupHumanoid(humanoid, false);
  }

  std::printf("armor_wearer_cost: %s\n", cpusAllowed().c_str());
  std::printf("armor_wearer_cost: scan %.1f ns/call, sync (chest+legs) %.1f ns/call\n", scan, sync);
  // The negative control: a rebuild that is not dearer than a scan rebuilt nothing.
  EXPECT_GT(sync, scan) << "the sync arm cost no more than the scan: it did not rebuild";
}
