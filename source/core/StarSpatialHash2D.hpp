#pragma once

#include "StarRect.hpp"
#include "StarMap.hpp"
#include "StarSet.hpp"
#include "StarBlockAllocator.hpp"

namespace Star {

// Dual-map based on key and 2 dimensional bounding rectangle.  Implements a 2d
// spatial hash for fast bounding box queries.  Each entry may have more than
// one bounding rectangle.
template <typename KeyT, typename ScalarT, typename ValueT, typename IntT = int, size_t AllocatorBlockSize = 4096>
class SpatialHash2D {
public:
  typedef KeyT Key;
  typedef ScalarT Scalar;
  typedef Box<ScalarT, 2> Rect;
  typedef typename Rect::Coord Coord;
  typedef ValueT Value;

  struct Entry {
    Entry();

    SmallList<Rect, 2> rects;
    Value value;
    // Lever #4: per-query dedup stamp. forEach stamps an entry with the current
    // query's id the first time it is collected, then skips it on the remaining
    // cells/rects it occupies -- this replaces the old per-query std::sort +
    // skip-equal dedup pass. The `mutable` write is safe because every
    // SpatialHash2D instance is queried single-threaded: the only instantiation
    // is EntityMap::m_spatialMap, and server worlds serialize all queries under
    // WorldServerThread::m_mutex while the client queries it only on the main
    // update/render thread (the lighting worker reads a pre-gathered light list,
    // never the map). Re-entrancy (a nested forEach from a callback) is safe
    // because forEach is two-phase: the gather finishes before any callback runs,
    // so a nested query cannot advance the counter mid-gather (forEach also reads
    // the counter into a local as defensive future-proofing -- see forEach).
    mutable uint64_t queryStamp = 0;
  };

  typedef StableHashMap<Key, Entry, hash<Key>, std::equal_to<Key>, BlockAllocator<pair<Key const, Entry>, AllocatorBlockSize>> EntryMap;

  SpatialHash2D(Scalar const& sectorSize);

  List<Key> keys() const;
  List<Value> values() const;
  EntryMap const& entries() const;

  size_t size() const;

  bool contains(Key const& key) const;

  Value const& get(Key const& key) const;
  Value& get(Key const& key);

  // Returns default constructed value if key not found
  Value value(Key const& key) const;

  // Query values from several bounding boxes at once with no duplicates.
  List<Value> queryValues(Rect const& rect) const;
  template <typename RectCollection>
  List<Value> queryValues(RectCollection const& rects) const;

  // Iterate over entries in the given bounding boxes without duplication.  It
  // is safe to modify rects or add entries from the given callback, but it is
  // not safe to remove entries from it.
  template <typename Function>
  void forEach(Rect const& rect, Function&& function) const;
  template <typename RectCollection, typename Function>
  void forEach(RectCollection const& rects, Function&& function) const;

  void set(Key const& key, Coord const& pos);
  void set(Key const& key, Rect const& rect);

  template <typename RectCollection>
  void set(Key const& key, RectCollection const& rects);

  void set(Key const& key, Coord const& pos, Value value);
  void set(Key const& key, Rect const& rect, Value value);

  template <typename RectCollection>
  void set(Key const& key, RectCollection const& rects, Value value);

  Maybe<Value> remove(Key const& key);

  // Recalculates every item in sector map
  void setSectorSize(Scalar const& sectorSize);

private:
  typedef Vector<IntT, 2> Sector;
  typedef Box<IntT, 2> SectorRange;
  // Lever #12a: flat heap-backed container, not a hash set. Sectors are 16-unit
  // cells holding 0-few entries (pruned the instant they empty), so a contiguous
  // vector iterates with full cache locality in forEach (no hash bucket-skip /
  // second-level pointer chase) and add is a hash-free O(1) append, remove a
  // small linear scan. forEach's per-query stamp dedup (Lever #4) also absorbs
  // the rare intra-sector duplicate from an entry whose two rects map to the
  // same sector. (List, not an inline SmallList: the SectorEntrySet is a
  // value in the relocating FlatHashMap<Sector,...>; a vector's heap pointer
  // survives the move, an inline buffer would not.)
  typedef List<Entry const*> SectorEntrySet;
  typedef HashMap<Sector, SectorEntrySet> SectorMap;

  SectorRange getSectors(Rect const& r) const;

  void addSpatial(Entry const* entry);
  void removeSpatial(Entry const* entry);

  template <typename RectCollection>
  void updateSpatial(Entry* entry, RectCollection const& rects);

  Scalar m_sectorSize;
  EntryMap m_entryMap;
  SectorMap m_sectorMap;
  // Lever #4: monotonically increasing per-query id, bumped once per forEach and
  // written onto each collected Entry::queryStamp to dedup multi-cell/multi-rect
  // entries without sorting. mutable: forEach is const but must advance it.
  // uint64_t does not wrap in any realistic session.
  mutable uint64_t m_queryCounter = 0;
};

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::Entry::Entry()
  : value() {}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::SpatialHash2D(Scalar const& sectorSize)
  : m_sectorSize(sectorSize) {}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
List<KeyT> SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::keys() const {
  return m_entryMap.keys();
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
List<typename SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::Value> SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::values() const {
  List<Value> values;
  for (auto const& pair : m_entryMap)
    values.append(pair.second.value);

  return values;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
typename SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::EntryMap const&
SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::entries() const {
  return m_entryMap;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
size_t SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::size() const {
  return m_entryMap.size();
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
bool SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::contains(Key const& key) const {
  return m_entryMap.contains(key);
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
typename SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::Value const& SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::get(
    Key const& key) const {
  return m_entryMap.get(key).value;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
typename SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::Value& SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::get(
    Key const& key) {
  return m_entryMap.get(key).value;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
typename SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::Value SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::value(
    Key const& key) const {
  auto iter = m_entryMap.find(key);
  if (iter == m_entryMap.end())
    return Value();
  else
    return iter->second.value;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
List<ValueT> SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::queryValues(Rect const& rect) const {
  return queryValues(initializer_list<Rect>{rect});
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
template <typename RectCollection>
List<ValueT> SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::queryValues(RectCollection const& rects) const {
  List<Value> values;
  forEach(rects, [&values](Value const& value) {
      values.append(value);
    });
  return values;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
template <typename Function>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::forEach(Rect const& rect, Function&& function) const {
  return forEach(initializer_list<Rect>{rect}, forward<Function>(function));
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
template <typename RectCollection, typename Function>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::forEach(RectCollection const& rects, Function&& function) const {
  // Lever #4: stamp-based dedup. An entry is appended only the first time it is
  // seen this query (queryStamp != stamp), then stamped; subsequent cells/rects
  // that hold the same entry skip it. This removes the old unconditional
  // std::sort + skip-equal dedup pass and shrinks foundEntries to UNIQUE entries
  // (far less inline-buffer spilling). The per-query id is read into a LOCAL
  // `stamp` rather than re-read from m_queryCounter while gathering. NOTE: the
  // dedup's correctness under re-entrancy (a forEach issued from within
  // `function`, which bumps m_queryCounter) is actually guaranteed by the
  // two-phase collect-then-dispatch structure below -- the gather completes
  // before ANY callback runs, so m_queryCounter cannot change mid-gather. The
  // local snapshot is sound defensive future-proofing: it keeps THIS query's
  // stamp stable even if a later refactor were to interleave gather and
  // dispatch (single-phase), so it is not the sole guard, just belt-and-braces.
  uint64_t const stamp = ++m_queryCounter;

  // Two-phase: the gather completes before any callback runs, so adding entries
  // from `function` remains safe (it cannot perturb an in-flight gather) -- and,
  // as noted above, this is what guarantees the stamp dedup under re-entrancy.
  SmallList<Entry const*, 32> foundEntries;

  for (Rect const& rect : rects) {
    if (rect.isNull())
      continue;

    auto sectorResult = getSectors(rect);

    // Lever #3: skip the per-entry box test for fully-contained INTERIOR cells.
    // A cell strictly inside the query's sector span -- x in [xMin+1, xMax-1),
    // y in [yMin+1, yMax-1) -- has its entire world extent inside the query box,
    // so EVERY entry registered there provably intersects the query (no test
    // needed). Only the PERIMETER ring (the query edge may cut through those
    // cells) keeps the r.intersects test. Proof: an entry is registered in cell
    // cx (via getSectors in addSpatial) iff floor(r.xMin/s) <= cx < ceil(r.xMax/s),
    // i.e. fl(r.xMin/s) < cx+1 and fl(r.xMax/s) > cx. For an interior cx
    // (xMin+1 <= cx <= xMax-2 with xMin=floor(Q.xMin/s), xMax=ceil(Q.xMax/s)):
    // fl(r.xMax/s) > cx >= xMin+1 > fl(Q.xMin/s), and float division by a fixed
    // s is monotonic, so r.xMax > Q.xMin; symmetrically r.xMin < Q.xMax. Both
    // dims => r.intersects(Q) with includeEdges=true. The boundary cells (xMin,
    // xMax-1) are conservatively kept in the perimeter (the query edge can land
    // anywhere inside them), so we NEVER skip a test that was actually needed --
    // under-testing would emit a non-intersecting entry, over-testing is merely
    // slower. Small queries (span <= 2 cells in a dim) have an empty interior
    // range, so every cell is perimeter -- exactly today's behavior.
    IntT const xMin = sectorResult.xMin(), xMax = sectorResult.xMax();
    IntT const yMin = sectorResult.yMin(), yMax = sectorResult.yMax();

    for (IntT x = xMin; x < xMax; ++x) {
      bool const xInterior = (x > xMin && x < xMax - 1);
      for (IntT y = yMin; y < yMax; ++y) {
        auto i = m_sectorMap.find(Sector{x, y});
        if (i == m_sectorMap.end())
          continue;

        bool const interior = xInterior && (y > yMin && y < yMax - 1);
        if (interior) {
          // Fully contained: append every not-yet-stamped entry, no box test.
          for (auto e : i->second) {
            if (e->queryStamp == stamp)
              continue;  // already collected this query (multi-cell / multi-rect / shared-sector dup)
            e->queryStamp = stamp;
            foundEntries.append(e);
          }
        } else {
          // Perimeter cell: the query edge may cut through it -> keep the test.
          for (auto e : i->second) {
            if (e->queryStamp == stamp)
              continue;
            for (Rect const& r : e->rects) {
              if (r.intersects(rect)) {
                e->queryStamp = stamp;
                foundEntries.append(e);
                break;
              }
            }
          }
        }
      }
    }
  }

  // foundEntries is already unique (stamp dedup above) -- dispatch directly.
  for (auto const& entry : foundEntries)
    function(entry->value);
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::set(Key const& key, Coord const& pos) {
  // initializer_list<Rect>{...}, not a bare {...}: a braced-init-list can't
  // deduce the RectCollection template param, so {...} would re-select this
  // single-arg overload -> infinite recursion. Mirrors queryValues()/forEach().
  set(key, initializer_list<Rect>{Rect(pos, pos)});
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::set(Key const& key, Rect const& rect) {
  set(key, initializer_list<Rect>{rect});  // explicit type; bare {rect} would infinite-recurse
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
template <typename RectCollection>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::set(Key const& key, RectCollection const& rects) {
  updateSpatial(&m_entryMap.get(key), rects);
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::set(Key const& key, Coord const& pos, Value value) {
  set(key, initializer_list<Rect>{Rect(pos, pos)}, std::move(value));  // explicit type; bare {...} would infinite-recurse
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::set(Key const& key, Rect const& rect, Value value) {
  set(key, initializer_list<Rect>{rect}, std::move(value));  // explicit type; bare {rect} would infinite-recurse
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
template <typename RectCollection>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::set(Key const& key, RectCollection const& rects, Value value) {
  Entry& entry = m_entryMap[key];
  entry.value = std::move(value);
  updateSpatial(&entry, rects);
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
auto SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::remove(Key const& key) -> Maybe<Value> {
  auto iter = m_entryMap.find(key);
  if (iter == m_entryMap.end())
    return {};

  removeSpatial(&iter->second);
  Maybe<Value> val = std::move(iter->second.value);
  m_entryMap.erase(iter);
  return val;
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::setSectorSize(Scalar const& sectorSize) {
  m_sectorSize = sectorSize;
  m_sectorMap.clear();
  for (auto const& pair : m_entryMap)
    addSpatial(pair.first, pair.second);
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
typename SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::SectorRange SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::getSectors(Rect const& r) const {
  return SectorRange(
      floor(r.xMin() / m_sectorSize),
      floor(r.yMin() / m_sectorSize),
      ceil(r.xMax() / m_sectorSize),
      ceil(r.yMax() / m_sectorSize));
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::addSpatial(Entry const* entry) {
  for (Rect const& rect : entry->rects) {
    if (rect.isNull())
      continue;

    auto sectorResult = getSectors(rect);
    for (IntT x = sectorResult.xMin(); x < sectorResult.xMax(); ++x) {
      for (IntT y = sectorResult.yMin(); y < sectorResult.yMax(); ++y) {
        Sector sector(x, y);
        SectorEntrySet* p = m_sectorMap.ptr(sector);
        if (!p)
          p = &m_sectorMap.add(sector, SectorEntrySet());
        p->append(entry);  // Lever #12a: flat append; forEach stamp dedup (#4) collapses any intra-sector dup
      }
    }
  }
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::removeSpatial(Entry const* entry) {
  for (Rect const& rect : entry->rects) {
    if (rect.isNull())
      continue;

    auto sectorResult = getSectors(rect);
    for (IntT x = sectorResult.xMin(); x < sectorResult.xMax(); ++x) {
      for (IntT y = sectorResult.yMin(); y < sectorResult.yMax(); ++y) {
        auto i = m_sectorMap.find(Sector{x, y});
        if (i != m_sectorMap.end()) {
          i->second.remove(entry);
          if (i->second.empty())
            m_sectorMap.erase(i);
        }
      }
    }
  }
}

template <typename KeyT, typename ScalarT, typename ValueT, typename IntT, size_t AllocatorBlockSize>
template <typename RectCollection>
void SpatialHash2D<KeyT, ScalarT, ValueT, IntT, AllocatorBlockSize>::updateSpatial(Entry* entry, RectCollection const& rects) {
  removeSpatial(entry);
  entry->rects.clear();
  entry->rects.appendAll(rects);
  addSpatial(entry);
}

}
