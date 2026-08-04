#include "StarGridScroll.hpp"
#include "gtest/gtest.h"

#include <vector>

// The stable-grid gather (#122, A2) scrolls a retained grid rather than re-reading every tile: it copies the
// overlap from the old grid and refills only the newly-exposed margin. That is four sign cases, half-open
// bounds and an intra-column slice copy, and until now it had no test at all -- it was verified only by
// looking at a running game, on a path that E02 measured at 5.5% of recomputes while walking and 0% standing
// still. A path that rare is precisely the one play-testing does not reach.
//
// The load-bearing test is ScrollMatchesFullRebuild: it does not check the rects against a restatement of the
// same arithmetic (which would pass on any consistently-wrong formula), it drives a real scroll and compares
// against a from-scratch rebuild. That is the property the cache actually owes its caller.
using namespace Star;

namespace {

constexpr int Width = 16;
constexpr int Height = 12;

// Unique per world position over the ranges used here, so a cell that ends up holding another position's
// value cannot coincide with the right answer. A hash would risk exactly that collision.
int worldValue(int wx, int wy) {
  return wx * 10000 + wy;
}

int at(std::vector<int> const& grid, int nx, int ny) {
  return grid[(size_t)nx * (size_t)Height + (size_t)ny];
}

void put(std::vector<int>& grid, int nx, int ny, int v) {
  grid[(size_t)nx * (size_t)Height + (size_t)ny] = v;
}

std::vector<int> fullRebuild(Vec2I const& anchor) {
  std::vector<int> g((size_t)Width * (size_t)Height, 0);
  for (int nx = 0; nx < Width; ++nx)
    for (int ny = 0; ny < Height; ++ny)
      put(g, nx, ny, worldValue(anchor[0] + nx, anchor[1] + ny));
  return g;
}

// Exactly what WorldClient::shiftAndGatherMargin does, with the tile read replaced by worldValue: retain the
// overlap by copy, refill each margin. Sentinel-filled first so a cell no one writes is detectable rather
// than accidentally correct.
std::vector<int> scrollRebuild(std::vector<int> const& old, GridScroll const& s, Vec2I const& newAnchor, int dx, int dy) {
  std::vector<int> g((size_t)Width * (size_t)Height, -1);
  if (!s.overlap.isEmpty()) {
    for (int nx = s.overlap.min()[0]; nx < s.overlap.max()[0]; ++nx)
      for (int ny = s.overlap.min()[1]; ny < s.overlap.max()[1]; ++ny)
        put(g, nx, ny, at(old, nx + dx, ny + dy));
  }
  for (RectI const& m : {s.marginX, s.marginY}) {
    if (m.isEmpty())
      continue;
    for (int nx = m.min()[0]; nx < m.max()[0]; ++nx)
      for (int ny = m.min()[1]; ny < m.max()[1]; ++ny)
        put(g, nx, ny, worldValue(newAnchor[0] + nx, newAnchor[1] + ny));
  }
  return g;
}

bool inRect(RectI const& r, int x, int y) {
  return !r.isEmpty() && x >= r.min()[0] && x < r.max()[0] && y >= r.min()[1] && y < r.max()[1];
}

// Deltas spanning both signs, zero, one-cell, mid-grid and beyond the grid extent on each axis -- the last of
// those is the no-overlap case the caller is expected to avoid but which must still answer coherently.
std::vector<int> const Deltas = {-20, -16, -12, -5, -1, 0, 1, 5, 12, 16, 20};

}

TEST(GridScrollTest, ScrollMatchesFullRebuild) {
  Vec2I const oldAnchor(1000, -500);
  auto before = fullRebuild(oldAnchor);
  for (int dx : Deltas) {
    for (int dy : Deltas) {
      Vec2I newAnchor = oldAnchor + Vec2I(dx, dy);
      auto s = gridScroll(dx, dy, Width, Height);
      auto scrolled = scrollRebuild(before, s, newAnchor, dx, dy);
      EXPECT_EQ(scrolled, fullRebuild(newAnchor)) << "dx=" << dx << " dy=" << dy;
    }
  }
}

TEST(GridScrollTest, EveryCellIsCovered) {
  for (int dx : Deltas) {
    for (int dy : Deltas) {
      auto s = gridScroll(dx, dy, Width, Height);
      for (int nx = 0; nx < Width; ++nx) {
        for (int ny = 0; ny < Height; ++ny) {
          bool covered = inRect(s.overlap, nx, ny) || inRect(s.marginX, nx, ny) || inRect(s.marginY, nx, ny);
          EXPECT_TRUE(covered) << "uncovered cell (" << nx << "," << ny << ") at dx=" << dx << " dy=" << dy;
        }
      }
    }
  }
}

TEST(GridScrollTest, MarginsNeverTouchTheOverlap) {
  for (int dx : Deltas) {
    for (int dy : Deltas) {
      auto s = gridScroll(dx, dy, Width, Height);
      for (int nx = 0; nx < Width; ++nx) {
        for (int ny = 0; ny < Height; ++ny) {
          if (!inRect(s.overlap, nx, ny))
            continue;
          EXPECT_FALSE(inRect(s.marginX, nx, ny)) << "marginX clobbers retained (" << nx << "," << ny << ")";
          EXPECT_FALSE(inRect(s.marginY, nx, ny)) << "marginY clobbers retained (" << nx << "," << ny << ")";
        }
      }
    }
  }
}

TEST(GridScrollTest, OverlapSourceStaysInsideTheOldGrid) {
  for (int dx : Deltas) {
    for (int dy : Deltas) {
      auto s = gridScroll(dx, dy, Width, Height);
      if (s.overlap.isEmpty())
        continue;
      for (int nx = s.overlap.min()[0]; nx < s.overlap.max()[0]; ++nx) {
        for (int ny = s.overlap.min()[1]; ny < s.overlap.max()[1]; ++ny) {
          int sx = nx + dx, sy = ny + dy;
          EXPECT_TRUE(sx >= 0 && sx < Width && sy >= 0 && sy < Height)
            << "overlap reads outside the old grid at dx=" << dx << " dy=" << dy;
        }
      }
    }
  }
}

// WHY shiftAndGatherMargin MAY CLEAR ONLY THE VACATED L (E03), including the case that makes it subtle.
//
// The gather does NOT write every cell it is handed: tileEvalColumnsParallel clamps away cells in unloaded
// sectors, which is why lightingStableGather zeroes before gathering at all. So the margin pre-clear is
// load-bearing -- a margin cell whose sector is absent must read as {0 light, not-obstacle, not-sky} -- while
// the overlap pre-clear is dead, because the copy writes every overlap cell unconditionally.
//
// This models both strategies rather than calling production, so it certifies the ARGUMENT, not the code.
// It earns its place because the render gate cannot certify either: the gate freezes the world and a frozen
// camera never scrolls. The scratch starts holding a previous grid's contents, which is the actual risk --
// skip the clear in the wrong place and stale values from another world position survive.
TEST(GridScrollTest, ClearingOnlyTheMarginMatchesClearingEverything) {
  Vec2I const oldAnchor(1000, -500);
  auto before = fullRebuild(oldAnchor);
  auto resident = [](int wx, int wy) { return ((wx / 7) + (wy / 5)) % 3 != 0; };

  for (int dx : Deltas) {
    for (int dy : Deltas) {
      int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
      if (adx >= Width || ady >= Height)
        continue;   // no overlap: the caller takes a full gather, which clears everything regardless
      Vec2I newAnchor = oldAnchor + Vec2I(dx, dy);
      auto s = gridScroll(dx, dy, Width, Height);

      std::vector<int> clearAll((size_t)Width * (size_t)Height, 0);
      std::vector<int> clearL((size_t)Width * (size_t)Height, 0);
      for (size_t i = 0; i < clearL.size(); ++i)
        clearL[i] = -999 - (int)i;   // a previous grid's contents, still resident in the scratch buffer
      for (RectI const& m : {s.marginX, s.marginY}) {
        if (m.isEmpty())
          continue;
        for (int nx = m.min()[0]; nx < m.max()[0]; ++nx)
          for (int ny = m.min()[1]; ny < m.max()[1]; ++ny)
            put(clearL, nx, ny, 0);
      }

      for (std::vector<int>* g : {&clearAll, &clearL}) {
        for (int nx = s.overlap.min()[0]; nx < s.overlap.max()[0]; ++nx)
          for (int ny = s.overlap.min()[1]; ny < s.overlap.max()[1]; ++ny)
            put(*g, nx, ny, at(before, nx + dx, ny + dy));
        for (RectI const& m : {s.marginX, s.marginY}) {
          if (m.isEmpty())
            continue;
          for (int nx = m.min()[0]; nx < m.max()[0]; ++nx)
            for (int ny = m.min()[1]; ny < m.max()[1]; ++ny)
              if (resident(newAnchor[0] + nx, newAnchor[1] + ny))
                put(*g, nx, ny, worldValue(newAnchor[0] + nx, newAnchor[1] + ny));
        }
      }
      EXPECT_EQ(clearAll, clearL) << "dx=" << dx << " dy=" << dy;
    }
  }
}

// EXTRACTION FIDELITY, and the only thing that checks it. These are the expressions that stood inline in
// shiftAndGatherMargin before the helper existed, restated independently, so the extraction is measured
// against what it replaced rather than against itself. The render gate structurally cannot do this job: it
// freezes the world, a frozen camera never scrolls, and the scroll path therefore never executes under it.
//
// Retire this one once the extraction is no longer the change under review -- ScrollMatchesFullRebuild
// certifies correctness on its own merits, while this pins a historical formula and would obstruct a later
// deliberate change to the geometry.
TEST(GridScrollTest, MatchesTheInlineArithmeticItReplaced) {
  for (int dx : Deltas) {
    for (int dy : Deltas) {
      int adx = dx < 0 ? -dx : dx;
      int ady = dy < 0 ? -dy : dy;
      if (adx >= Width || ady >= Height)
        continue;   // the inline code was never reached here -- the caller took a full gather instead
      auto s = gridScroll(dx, dy, Width, Height);

      EXPECT_EQ(s.overlap.min(), Vec2I(dx > 0 ? 0 : adx, dy > 0 ? 0 : ady)) << "dx=" << dx << " dy=" << dy;
      EXPECT_EQ(s.overlap.max(), Vec2I(dx > 0 ? Width - dx : Width, dy > 0 ? Height - dy : Height))
        << "dx=" << dx << " dy=" << dy;

      if (dx == 0) {
        EXPECT_TRUE(s.marginX.isEmpty()) << "dx=0 must expose no columns";
      } else {
        RectI wasX = RectI::withSize(Vec2I(dx > 0 ? Width - dx : 0, 0), Vec2I(adx, Height));
        EXPECT_EQ(s.marginX.min(), wasX.min()) << "dx=" << dx;
        EXPECT_EQ(s.marginX.max(), wasX.max()) << "dx=" << dx;
      }
      if (dy == 0) {
        EXPECT_TRUE(s.marginY.isEmpty()) << "dy=0 must expose no rows";
      } else {
        RectI wasY = RectI::withSize(Vec2I(0, dy > 0 ? Height - dy : 0), Vec2I(Width, ady));
        EXPECT_EQ(s.marginY.min(), wasY.min()) << "dy=" << dy;
        EXPECT_EQ(s.marginY.max(), wasY.max()) << "dy=" << dy;
      }
    }
  }
}

// A shift of at least the grid extent retains nothing. Asserted so the degenerate branch stays a deliberate
// answer rather than something a later edit can quietly make partial.
TEST(GridScrollTest, ShiftBeyondTheGridRetainsNothing) {
  for (int d : {Width, -Width, Width + 7, -Width - 7})
    EXPECT_TRUE(gridScroll(d, 0, Width, Height).overlap.isEmpty()) << "dx=" << d;
  for (int d : {Height, -Height, Height + 7, -Height - 7})
    EXPECT_TRUE(gridScroll(0, d, Width, Height).overlap.isEmpty()) << "dy=" << d;
}
