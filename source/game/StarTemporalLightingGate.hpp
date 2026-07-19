#pragma once

#include "StarRect.hpp"
#include "StarList.hpp"
#include "StarLightSource.hpp"

#include <cmath>

namespace Star {

// Flicker-tolerant temporal lighting gate (pure logic; unit-testable). Decides whether the lightmap
// must be recomputed this frame or can be skipped (reusing the prior lightmap) because the scene is
// "calm" -- only flicker / particle-motion / ambient changed, none of which it treats as activity.
// See /root/kubebound/2026-06-27-temporal-lighting-decoupling-design.md.
struct TemporalLightingGate {
  // Compact, flicker-tolerant, ORDER-INDEPENDENT signature of the entity light set: sorted
  // (tile-position, type) per light. Colour is intentionally EXCLUDED so flicker (a colour-only
  // change) does not register as activity.
  typedef List<std::pair<Vec2I, uint8_t>> LightSignature;

  static LightSignature signatureOf(List<LightSource> const& lights) {
    LightSignature sig;
    sig.reserve(lights.size());
    for (auto const& l : lights)
      sig.append({Vec2I((int)std::floor(l.position[0]), (int)std::floor(l.position[1])), (uint8_t)l.type});
    sig.sort([](std::pair<Vec2I, uint8_t> const& a, std::pair<Vec2I, uint8_t> const& b) {
      if (a.first[0] != b.first[0]) return a.first[0] < b.first[0];
      if (a.first[1] != b.first[1]) return a.first[1] < b.first[1];
      return a.second < b.second;
    });
    return sig;
  }

  // The last computed frame's activity baseline (held by WorldClient, lighting-thread-private).
  struct Baseline {
    bool valid = false;
    uint64_t tileEpoch = 0;
    RectI lightRange;
    LightSignature lightSig;
    int64_t lastRecomputeMs = 0;
  };

  // Recompute this frame? enabled=false or floorMs<=0 => always recompute (feature off => byte-identical).
  // Otherwise recompute on real activity (tile edit / scroll / light moved-added-removed) or when the
  // floor interval has elapsed; else skip (calm -> reuse the prior lightmap).
  static bool shouldRecompute(Baseline const& prev, bool enabled, double floorMs,
      uint64_t tileEpoch, RectI const& lightRange, LightSignature const& lightSig, int64_t nowMs) {
    if (!enabled || floorMs <= 0.0)
      return true;
    if (!prev.valid)
      return true;
    if ((double)(nowMs - prev.lastRecomputeMs) >= floorMs)
      return true;
    if (tileEpoch != prev.tileEpoch)
      return true;
    if (lightRange != prev.lightRange)
      return true;
    if (lightSig != prev.lightSig)
      return true;
    return false;
  }
};

}
