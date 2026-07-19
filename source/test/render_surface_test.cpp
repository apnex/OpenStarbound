#include "StarGlRenderSurface.hpp"
#include "StarGlTexturePrimitives.hpp"

#include "gtest/gtest.h"

using namespace Star;

// Unit tests for the CPU-side RB-1 / RB-6 invariants of the sovereign render-surface module. They run WITHOUT a
// GL context: recordStorage / setAllocatedSize (the storage descriptor, RB-6) and adopt / share / release (the
// borrow trio, RB-1) issue no GL. The only GL call within reach -- ~GlLoneTexture's glDeleteTextures -- is
// guarded on textureId != 0, so every fixture that fakes a non-zero name zeroes it again before the RefPtr drops.
//
// These are the first tests that exercise a Layer-1 component in isolation. They are possible only because the
// components were lifted into a sovereign module with a real OUTSIDE (docs/render/layer1-architecture.md).

namespace {
  // A GlLoneTexture carrying a fake, non-zero GL name so hasStorage() reports true, WITHOUT a real allocation.
  // The caller MUST zero textureId before this is destroyed (see the trailing tex->textureId = 0 in each test)
  // so the guarded destructor never calls glDeleteTextures with no context.
  RefPtr<GlLoneTexture> fakeStorage(GLuint id = 7) {
    auto tex = make_ref<GlLoneTexture>();
    tex->textureId = id;
    return tex;
  }
}

// RB-6: the storage descriptor is size AND format, written together by recordStorage.
TEST(RenderSurfaceTest, LoneTextureRecordsWholeDescriptor) {
  GlLoneTexture tex;   // textureId == 0, destructor is a no-op
  EXPECT_EQ(tex.format(), 0) << "internalFormat starts at the 0 sentinel";

  tex.recordStorage(Vec2U(640, 320), GL_RGBA16F);
  EXPECT_EQ(tex.glTextureSize(), Vec2U(640, 320));
  EXPECT_EQ(tex.format(), (GLint)GL_RGBA16F);
}

// RB-6: the allocator pre-stamp records only the size; internalFormat stays the 0 sentinel no SubImage guard
// can match, so a freshly-allocated-but-unspecified texture can never be mistaken for same-format.
TEST(RenderSurfaceTest, SetAllocatedSizeLeavesFormatAtSentinel) {
  GlLoneTexture tex;
  tex.setAllocatedSize(Vec2U(64, 64));
  EXPECT_EQ(tex.glTextureSize(), Vec2U(64, 64));
  EXPECT_EQ(tex.format(), 0) << "the allocator pre-stamp must leave internalFormat at the 0 sentinel";
}

TEST(RenderSurfaceTest, DefaultEffectTextureOwnsNothing) {
  EffectTexture et;
  EXPECT_FALSE(et.hasStorage());
  EXPECT_FALSE(et.ownsWritableStorage());
  EXPECT_FALSE(et.borrowed());
}

// RB-1: adopt() means "we allocated this; it is ours to write" -- writable, not a named borrow.
TEST(RenderSurfaceTest, AdoptIsWritable) {
  auto tex = fakeStorage();
  EffectTexture et;
  et.adopt(tex);
  EXPECT_TRUE(et.hasStorage());
  EXPECT_TRUE(et.ownsWritableStorage());
  EXPECT_FALSE(et.borrowed());
  ASSERT_TRUE((bool)et.texture());
  EXPECT_EQ(et.texture()->glTextureId(), 7u);
  tex->textureId = 0;   // disarm the guarded destructor (no GL context here)
}

// RB-1 / RB-5: share() of a NAMED target is not ours to write, and records the name to re-point on rebuild.
TEST(RenderSurfaceTest, ShareOfNamedTargetIsNotWritableAndRepoints) {
  auto tex = fakeStorage();
  EffectTexture et;
  et.share(tex, "worldSurface");
  EXPECT_TRUE(et.hasStorage());
  EXPECT_FALSE(et.ownsWritableStorage()) << "a borrowed target is not ours to write";
  EXPECT_TRUE(et.borrowed());
  EXPECT_EQ(et.borrowFrom(), "worldSurface");
  tex->textureId = 0;
}

// THE RB-1 SIBLING-ALIAS SEAL. share(tex, "") is shared-but-owned-elsewhere: empty name, so borrowed()==false,
// yet NOT writable -- writability comes from m_owned (set only by adopt), never derived from the name. Deriving
// it from !borrowed() (the old monolith bug) would call this writable and let an upload re-specify a live
// target's storage through the alias.
TEST(RenderSurfaceTest, EmptyNameShareIsNotWritable) {
  auto tex = fakeStorage();
  EffectTexture et;
  et.share(tex, "");
  EXPECT_TRUE(et.hasStorage());
  EXPECT_FALSE(et.borrowed()) << "an empty name is not a named-target borrow";
  EXPECT_FALSE(et.ownsWritableStorage())
      << "RB-1: an empty-name alias of a non-owned source must NOT be writable";
  tex->textureId = 0;
}

// RB-1: re-sharing storage we had adopted must clear the ownership bit -- adopt-then-share drops writability.
TEST(RenderSurfaceTest, AdoptThenShareDropsWritability) {
  auto tex = fakeStorage();
  EffectTexture et;
  et.adopt(tex);
  ASSERT_TRUE(et.ownsWritableStorage());
  et.share(tex, "");
  EXPECT_FALSE(et.ownsWritableStorage()) << "sharing must clear the ownership bit adopt set";
  tex->textureId = 0;
}

TEST(RenderSurfaceTest, ReleaseLetsGo) {
  auto tex = fakeStorage();
  EffectTexture et;
  et.adopt(tex);
  et.release();
  EXPECT_FALSE(et.hasStorage());
  EXPECT_FALSE(et.ownsWritableStorage());
  EXPECT_FALSE(et.borrowed());
  EXPECT_FALSE((bool)et.texture());
  tex->textureId = 0;
}

// hasStorage() requires a real GL name: a zero-id texture is "no storage" even when adopted.
TEST(RenderSurfaceTest, ZeroIdIsNotStorage) {
  auto tex = make_ref<GlLoneTexture>();   // textureId == 0
  EffectTexture et;
  et.adopt(tex);
  EXPECT_FALSE(et.hasStorage());
  EXPECT_FALSE(et.ownsWritableStorage());
}
