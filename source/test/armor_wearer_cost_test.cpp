// RustBound M52 L8c-mech: what the fork spends keeping an NPC's wardrobe current, as the oracle side of a
// like-for-like comparison with RustBound's `Wardrobe::dress` (its own probe is
// crates/appearance/tests/dress_cost.rs in the RustBound tree).
//
//   STAR_ARMOR_COST=1 taskset -c 6-13 ./game_tests --gtest_filter='ArmorWearerCost.*'
//
// Skipped unless STAR_ARMOR_COST is set: a timing, run deliberately and pinned to the 4.5 GHz E-cores
// (6-13; 14-15 are the slower LP E-cores, where both arms read about twice as long).
//
// Npc::tickShared calls ArmorWearer::setupHumanoid on EVERY tick, and Npc::update reaches tickShared on
// both its master and its slave branch. setupHumanoid looks change-gated - it rebuilds only when some
// slot is flagged Armor::needsSync - but a flag is cleared only for a slot holding an allowed item. An
// EMPTY slot keeps its flag forever, so a wearer with any empty slot (NPC 279 fills 2 of 20) runs the
// whole sync block on every tick: removeWearable for each empty slot, the humanoidConfig merge,
// Humanoid::loadConfig, setBodyHidden. Two arms, both on the intro's NPC 279 as its npctype generates it:
//   - tick: setupHumanoid with nothing changed - what every tick costs;
//   - rebuild: the same items alternated with a different colourIndex before each call, so the chest
//     and legs wearables are rebuilt too - what a change costs.
//
// NOT CLAIMED: the call site's Npc::forceNude() (a StatusController::statPositive lookup) is not in
// either arm, so both numbers are FLOORS of what a tick spends here. The return value is never read;
// setupHumanoid leaves it uninitialised when no slot is flagged, which this wearer never reaches.

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

TEST(ArmorWearerCost, TickAndRebuildOnTheIntroNpc) {
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

  size_t const tickCalls = 200000;
  size_t const rebuildCalls = 20000;
  int const passes = 7;
  double tick = std::numeric_limits<double>::max();
  double rebuild = std::numeric_limits<double>::max();
  for (int pass = 0; pass != passes; ++pass) {
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i != tickCalls; ++i)
      armor.setupHumanoid(humanoid, false);
    tick = std::min(tick, nsPerCall(start, tickCalls));

    start = std::chrono::steady_clock::now();
    for (size_t i = 0; i != rebuildCalls; ++i) {
      bool odd = (i & 1) != 0;
      armor.setChestItem(odd ? chestOther : chest);
      armor.setLegsItem(odd ? legsOther : legs);
      armor.setupHumanoid(humanoid, false);
    }
    rebuild = std::min(rebuild, nsPerCall(start, rebuildCalls));
    // Leave the wearer as the tick arm expects it: the npctype's own items, synced.
    armor.setChestItem(chest);
    armor.setLegsItem(legs);
    armor.setupHumanoid(humanoid, false);
  }

  std::printf("armor_wearer_cost: %s\n", cpusAllowed().c_str());
  std::printf("armor_wearer_cost: tick %.1f ns/call, rebuild (chest+legs) %.1f ns/call\n", tick, rebuild);

  // The rebuild arm really rebuilds: the chest wearable's directives follow the alternated item. A
  // cost comparison could not show this - the rebuild loop also times two setItem calls.
  auto chestDirectives = [&]() {
    auto worn = humanoid.getLastWearableOfType<Humanoid::WornChest>();
    return worn ? worn->directives.string() : String("<no chest>");
  };
  String asGenerated = chestDirectives();
  armor.setChestItem(chestOther);
  armor.setupHumanoid(humanoid, false);
  String alternated = chestDirectives();
  EXPECT_NE(asGenerated, alternated) << "the alternate colourIndex did not reach the chest wearable";
  EXPECT_NE(alternated, String("<no chest>"));
}
