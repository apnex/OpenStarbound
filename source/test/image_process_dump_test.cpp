// The language-boundary oracle for RustBound's `M63` (the celestial textures): dump the reference's
// PROCESSED image for an asset path that carries directives - `?hueshift=..?addmask=<mask>[+<mask>];x;y`
// as CelestialGraphics::drawWorld and worldHorizonImages name every textured layer of a planet, a moon
// or a horizon - so a Rust port of the directives can be compared byte for byte.
//
// The image is read exactly the way the sky's drawables are: Root::singleton().assets()->image(path),
// which is Assets::loadImage - the base file, every reference the operations name loaded as an asset
// path of its own, then the operations applied in order (base/StarAssets.cpp loadImage). Nothing here
// re-implements an operation.
//
// Output: a raw file at STAR_IMAGE_OUT - a one-line ASCII header `IMGDUMP <width> <height> <path>\n`
// then width*height*4 bytes of RGBA, rows TOP-DOWN (the reference's Image stores rows bottom-up, so
// the rows are written from y = height-1 down to 0; a PNG decoder and RustBound's media::decode
// produce rows top-down, so the two sides compare without a flip on either). Nothing is asserted
// against a number: the test's job is to PRINT, and the comparison is the Rust side's.
//
// Run:  scripts/game-tests.sh --gtest_filter=ImageProcessDump.*
// Env:  STAR_IMAGE_PATH (the asset path with directives; default: the human bank's moon layer),
//       STAR_IMAGE_OUT (default /tmp/image-process-dump.rgba).
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarImage.hpp"
#include "StarFormat.hpp"
#include "gtest/gtest.h"
#include <cstdio>
#include <cstdlib>

using namespace Star;

namespace {
  String envOr(char const* name, char const* fallback) {
    char const* v = std::getenv(name);
    return String(v && *v ? v : fallback);
  }
}

TEST(ImageProcessDump, DumpsTheProcessedImageTopDown) {
  auto path = envOr("STAR_IMAGE_PATH",
      "/celestial/system/terrestrial/biomes/forest/maskie3.png?hueshift=-30?addmask=/celestial/system/terrestrial/dynamics/temperate/10.png");
  auto out = envOr("STAR_IMAGE_OUT", "/tmp/image-process-dump.rgba");
  auto assets = Root::singleton().assets();
  ImageConstPtr image = assets->image(path);
  ASSERT_TRUE(image) << "no image for " << path.utf8Ptr();
  ASSERT_EQ(image->pixelFormat(), PixelFormat::RGBA32) << "the dump is RGBA; the reference converts every asset image to RGBA32 on load";
  FILE* f = std::fopen(out.utf8Ptr(), "wb");
  ASSERT_TRUE(f != nullptr) << "cannot open " << out.utf8Ptr();
  std::string header = strf("IMGDUMP {} {} {}\n", image->width(), image->height(), path);
  std::fwrite(header.data(), 1, header.size(), f);
  for (unsigned y = image->height(); y-- > 0;) {
    for (unsigned x = 0; x < image->width(); ++x) {
      Vec4B p = image->get(x, y);
      unsigned char bytes[4] = {p[0], p[1], p[2], p[3]};
      std::fwrite(bytes, 1, 4, f);
    }
  }
  std::fclose(f);
  std::printf("IMGDUMP wrote %s %ux%u for %s\n", out.utf8Ptr(), image->width(), image->height(), path.utf8Ptr());
}
