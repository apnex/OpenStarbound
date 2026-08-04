#pragma once

#include "StarRect.hpp"

namespace Star {

// The geometry of scrolling a fixed-size grid by (dx, dy): what survives the shift, and what is newly
// exposed and must be refilled.
//
// GRID-INDEX SPACE, half-open, origin at the grid's own (0, 0) -- the caller adds its world anchor. Keeping
// world position out is what makes this pure integer geometry, and what lets the covering property below be
// stated at all.
//
// THE CONTRACT, and it is load-bearing rather than incidental: overlap, marginX and marginY COVER THE WHOLE
// GRID, and neither margin touches the overlap. The stable-grid gather relies on precisely that -- it
// retains the overlap by copy and refills only the margins, so a cell that fell through the middle would
// keep a value belonging to a different world position and nothing downstream would notice. The margins do
// overlap EACH OTHER at the corner, which costs one redundant refill and is not a defect.
struct GridScroll {
  RectI overlap;   // present in BOTH the old and new grid; null when the shift clears the grid entirely
  RectI marginX;   // the |dx| newly-exposed columns, full height; null when dx == 0
  RectI marginY;   // the |dy| newly-exposed rows, full width;     null when dy == 0
};

// New index (nx, ny) holds the world tile that old index (nx + dx, ny + dy) held.
//
// A shift of at least the grid extent on either axis leaves no overlap. The caller is expected to take a
// full refill instead of calling here, but this answers rather than asserts, because a degenerate answer
// that still satisfies the covering contract is safer than one that is undefined.
inline GridScroll gridScroll(int dx, int dy, int width, int height) {
  int adx = dx < 0 ? -dx : dx;
  int ady = dy < 0 ? -dy : dy;

  GridScroll s;
  if (adx >= width || ady >= height) {
    s.overlap = RectI::null();
    s.marginX = RectI::withSize(Vec2I(0, 0), Vec2I(width, height));
    s.marginY = RectI::null();
    return s;
  }

  // Shifting by +dx moves content toward lower indices, so the retained band starts at 0 and stops short of
  // the top; shifting by -dx does the mirror. Same on y.
  s.overlap = RectI(Vec2I(dx > 0 ? 0 : adx, dy > 0 ? 0 : ady),
                    Vec2I(dx > 0 ? width - dx : width, dy > 0 ? height - dy : height));
  s.marginX = dx == 0 ? RectI::null()
                      : RectI::withSize(Vec2I(dx > 0 ? width - dx : 0, 0), Vec2I(adx, height));
  s.marginY = dy == 0 ? RectI::null()
                      : RectI::withSize(Vec2I(0, dy > 0 ? height - dy : 0), Vec2I(width, ady));
  return s;
}

}
