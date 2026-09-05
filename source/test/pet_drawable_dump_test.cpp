// The language-boundary oracle for RustBound's `M49` (the pet): dump the reference's drawables
// for a ship pet's animator at a declared state, so a Rust port can be compared item for item.
//
// Construction is exactly `MonsterDatabase::monsterPortrait`'s (StarMonsterDatabase.cpp): the
// variant the seed selects, its animator config, `partImage` tags per part, the zoom, and the
// colour swap when the type has one. The state is set the way a slave would see it after a
// `setState` on the master: `setState(type, state, true, reverse)`, then a fixed `update(dt)`
// ladder at the server's step. Drawables come from `drawablesWithZLevelRebuild`, the golden path
// both render caches are asserted equal to, so no cache mode is a variable here.
//
// Output, one line per drawable, in `drawablesWithZLevelRebuild` order, at a declared precision:
//   zLevel image r00 r01 r02 r10 r11 r12 r20 r21 r22 px py cr cg cb ca fullbright
// with `image` as `AssetPath::join` prints it and every float as `%.9g`. Nothing here is asserted
// against a number: the test's job is to PRINT, and the comparison is the Rust side's.
//
// Run:  scripts/game-tests.sh --gtest_filter=PetDrawableDump.*
// Env:  STAR_PET_TYPE (default petcat), STAR_PET_SEED (hex, default 43abdeae98af00b2 - the seed
//       the banked create in kubebound's evidence/m49/wire carried), STAR_PET_STATE (default idle),
//       STAR_PET_FLIPPED (0/1), STAR_PET_STEPS (updates of 1/60 s before the dump, default 0).
#include "StarMonsterDatabase.hpp"
#include "StarNetworkedAnimator.hpp"
#include "StarRoot.hpp"
#include "StarAssetPath.hpp"
#include "StarDrawable.hpp"
#include "StarLexicalCast.hpp"
#include "StarImageProcessing.hpp"
#include "StarFormat.hpp"
#include "gtest/gtest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Star;

namespace {
  String envOr(char const* name, char const* fallback) {
    char const* v = std::getenv(name);
    return String(v && *v ? v : fallback);
  }

  void dump(List<pair<Drawable, float>> const& drawables, String const& label) {
    std::printf("PETDUMP begin %s count=%zu\n", label.utf8Ptr(), (size_t)drawables.size());
    for (auto const& entry : drawables) {
      auto const& d = entry.first;
      auto image = d.part.ptr<Drawable::ImagePart>();
      if (!image) {
        std::printf("PETDUMP skip non-image drawable\n");
        continue;
      }
      auto const& m = image->transformation;
      std::printf("PETDUMP %.9g %s %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d\n",
          entry.second, AssetPath::join(image->image).utf8Ptr(),
          m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2],
          d.position[0], d.position[1],
          d.color.redF(), d.color.greenF(), d.color.blueF(), d.color.alphaF(),
          d.fullbright ? 1 : 0);
    }
    std::printf("PETDUMP end %s\n", label.utf8Ptr());
  }
}

TEST(PetDrawableDump, DumpsTheVariantAtAState) {
  auto type = envOr("STAR_PET_TYPE", "petcat");
  auto seedText = envOr("STAR_PET_SEED", "43abdeae98af00b2");
  auto state = envOr("STAR_PET_STATE", "idle");
  bool flipped = envOr("STAR_PET_FLIPPED", "0") == "1";
  unsigned steps = lexicalCast<unsigned>(envOr("STAR_PET_STEPS", "0"));
  uint64_t seed = std::strtoull(seedText.utf8Ptr(), nullptr, 16);

  auto db = Root::singleton().monsterDatabase();
  auto variant = db->monsterVariant(type, seed, JsonObject());

  NetworkedAnimator animator(variant.animatorConfig);
  for (auto const& pair : variant.animatorPartTags)
    animator.setPartTag(pair.first, "partImage", pair.second);
  animator.setZoom(variant.animatorZoom);
  auto colorSwap = variant.colorSwap.value(db->colorSwap(variant.parameters.getString("colors", "default"), variant.seed));
  if (!colorSwap.empty())
    animator.setProcessingDirectives(imageOperationToString(ColorReplaceImageOperation{colorSwap}));

  std::printf("PETDUMP variant type=%s seed=%s zoom=%.9g parts=%zu colorSwap=%zu\n",
      type.utf8Ptr(), seedText.utf8Ptr(), variant.animatorZoom, (size_t)variant.animatorPartTags.size(), (size_t)colorSwap.size());
  for (auto const& pair : variant.animatorPartTags)
    std::printf("PETDUMP partImage %s %s\n", pair.first.utf8Ptr(), pair.second.utf8Ptr());

  ASSERT_TRUE(animator.setState("movement", state, true, false)) << "the state must exist: " << state.utf8Ptr();
  animator.setFlipped(flipped, 0.0f);
  for (unsigned i = 0; i < steps; ++i)
    animator.update(1.0f / 60.0f, nullptr);

  auto label = strf("{} {} {} flipped={} steps={}", type, seedText, state, flipped ? 1 : 0, steps);
  dump(animator.drawablesWithZLevelRebuild(Vec2F()), label);
}
