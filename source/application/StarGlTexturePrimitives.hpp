#pragma once

#include "StarRenderer.hpp"

#include "GL/glew.h"

namespace Star {

// GL TEXTURE PRIMITIVES -- the concrete GPU textures the renderer draws with, lifted OUT of OpenGlRenderer so
// the render-surface substrate (GlFrameBuffer / GlTargets / GlPass / GlEffects) can depend on GlLoneTexture
// without dragging in the whole renderer. This is Tier 1 of the §8.ii sovereignty extraction: GlLoneTexture
// is genuinely self-contained (it touches nothing in OpenGlRenderer and nothing in the texture atlas), so it
// comes out cleanly; the atlas-coupled GlGroupedTexture stays with the renderer and just implements the
// GlTexture interface declared here.
//
// GlTexture is the 3-method GL view a renderer consumer needs: the GL name, the storage size, and (for atlas
// pages) the offset of this logical texture within its physical page.

struct GlTexture : public Texture {
  virtual GLuint glTextureId() const = 0;
  virtual Vec2U glTextureSize() const = 0;
  virtual Vec2U glTextureCoordinateOffset() const = 0;
};

// A standalone GL texture: one glGenTextures name, owning its storage, deleting it on destruction. Everything
// that is NOT an atlas page -- framebuffer colour attachments, effect samplers, the white pixel -- is one of
// these. It is the type GlFrameBuffer's faces and an effect's samplers hold.
struct GlLoneTexture : public GlTexture {
  ~GlLoneTexture();

  Vec2U size() const override;
  TextureFiltering filtering() const override;
  TextureAddressing addressing() const override;

  GLuint glTextureId() const override;
  Vec2U glTextureSize() const override;
  Vec2U glTextureCoordinateOffset() const override;

  // THE STORAGE DESCRIPTOR -- size and internal format together, because storage is ONE act and describing half
  // of it is worse than describing none: a glTexSubImage2D fast path that consults a HALF-TRUE record writes
  // into storage whose format changed underneath it, and GL does not complain. It replaced an `uploadChannels`
  // field only ONE of the storage-spec paths maintained; the others left it stale, so emission storage went
  // RGBA16F -> RGB32F behind the guard's back and the pass wrote half-float RGBA into three-channel storage for
  // the rest of the run, dropping the obstacle-flag alpha (RB-6).
  //
  // The two fields are PRIVATE. They change ONLY through recordStorage() -- co-located with a glTex{Sub}Image2D
  // at every caller -- or setAllocatedSize() for the pure allocator (internalFormat stays the 0 sentinel, which
  // no SubImage guard can match). So no call site can poke half the descriptor and desync the pair. The
  // "recorded beside the spec" co-location is still by convention (glTex*Image2D are free globals a future path
  // could call directly), but the raw-field-poke seat of RB-6 is closed by the compiler.
  void recordStorage(Vec2U size, GLint format) { textureSize = size; internalFormat = format; }
  void setAllocatedSize(Vec2U size) { textureSize = size; }   // allocator pre-stamp; internalFormat stays 0
  GLint format() const { return internalFormat; }             // read by the SubImage same-format guard

  GLuint textureId = 0;
  TextureAddressing textureAddressing = TextureAddressing::Clamp;
  TextureFiltering textureFiltering = TextureFiltering::Nearest;

private:
  Vec2U textureSize;
  GLint internalFormat = 0;
};

}
