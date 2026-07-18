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

  GLuint textureId = 0;

  // THE STORAGE DESCRIPTOR. Size and internal format together, because storage is ONE act and describing
  // half of it is worse than describing none: a glTexSubImage2D fast path that consults a HALF-TRUE record
  // will happily write into storage whose format has changed underneath it, and GL will not complain.
  //
  // internalFormat is 0 until storage has been specified. WHOEVER SPECIFIES STORAGE RECORDS WHAT IT
  // SPECIFIED -- in the same breath, or the next reader is consulting a lie.
  //
  // This replaced an `uploadChannels` field that only ONE of the four storage-spec paths maintained. The
  // other three re-specified the texture and left it stale, so the SubImage guard passed on a format that no
  // longer existed. Proven: emission storage went RGBA16F -> RGB32F behind the guard's back and the pass
  // wrote half-float RGBA into three-channel storage for the rest of the run, silently dropping the alpha
  // that carries the obstacle flag.
  Vec2U textureSize;
  GLint internalFormat = 0;
  TextureAddressing textureAddressing = TextureAddressing::Clamp;
  TextureFiltering textureFiltering = TextureFiltering::Nearest;
};

}
