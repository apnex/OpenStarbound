#pragma once

#include "StarDrawable.hpp"
#include "StarRenderer.hpp"
#include "StarAssetTextureGroup.hpp"

namespace Star {

STAR_CLASS(DrawablePainter);

class DrawablePainter {
public:
  DrawablePainter(RendererPtr renderer, AssetTextureGroupPtr textureGroup);

  void drawDrawable(Drawable const& drawable);

  void cleanup(int64_t textureTimeout);

  // The group every drawable's image is looked up in. Read by the harness census to name where in the atlas a
  // texture landed, which is state below the drawable and not otherwise observable.
  AssetTextureGroupPtr const& textureGroup() const { return m_textureGroup; }

private:
  RendererPtr m_renderer;
  AssetTextureGroupPtr m_textureGroup;
};

}
