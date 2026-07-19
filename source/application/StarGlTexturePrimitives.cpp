#include "StarGlTexturePrimitives.hpp"

namespace Star {

GlLoneTexture::~GlLoneTexture() {
  if (textureId != 0)
    glDeleteTextures(1, &textureId);
}

Vec2U GlLoneTexture::size() const {
  return textureSize;
}

TextureFiltering GlLoneTexture::filtering() const {
  return textureFiltering;
}

TextureAddressing GlLoneTexture::addressing() const {
  return textureAddressing;
}

GLuint GlLoneTexture::glTextureId() const {
  return textureId;
}

Vec2U GlLoneTexture::glTextureSize() const {
  return textureSize;
}

Vec2U GlLoneTexture::glTextureCoordinateOffset() const {
  return Vec2U();
}

}
