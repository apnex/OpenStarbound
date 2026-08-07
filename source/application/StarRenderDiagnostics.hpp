#pragma once

#include "StarImage.hpp"
#include "StarMaybe.hpp"
#include "StarString.hpp"
#include "StarTelemetry.hpp"
#include "StarVector.hpp"

namespace Star {

// The renderer's INSTRUMENTS -- the things that observe the renderer rather than draw with it.
//
// These were seven virtuals on the Renderer contract itself, which meant every backend had to implement a
// pixel differ to draw a triangle, and every one of the twelve consumers could name a debug surface it had no
// business touching. Worse, it set the slope: each new diagnostic widened the contract that every consumer and
// every future backend depends on. Off the contract, the next one costs it nothing.
//
// The renderer OWNS these and hands out references; consumers reach them through the declared accessors
// (Renderer::gpuTimer(), Renderer::oracle()) and never see the GL behind them.

// Measure GPU-elapsed wall time for a named, non-overlapping span of draws.
//
// SHIPPED, DEFAULT-ON telemetry -- the per-pass numbers behind /telemetry and the debug HUD. This one is
// permanent. Note the spans cannot nest (GL_TIME_ELAPSED cannot), so begin() while another is open is a
// no-op, not a lie.
//
// begin() TAKES THE DESCRIPTOR. GPU values are recorded generically, three frames later, by code
// (OpenGlRenderer) that cannot know what a pass means -- so unlike a CPU Telemetry::timer() handle, there is
// no declaration site that naturally dominates the recording site. The previous design put a free-floating
// Telemetry::declare() near each begin(), which made declaration a STATEMENT IN PER-FRAME CONTROL FLOW: it
// produced three undeclared metrics in one task, twice because the declare sat in a branch a config flag
// switches off (backdropComposeMerge, parallaxOracle) and so never ran.
//
// Requiring the desc here closes that structurally. A timing CANNOT BE STARTED without one, so a metric can no
// longer be left undeclared by a branch that does not run, and a multi-site key just repeats the same desc at
// each call rather than needing one declaration proven to dominate every site. (It is still possible to pass
// `{}` on purpose and get owner=Unknown -- omission is impossible, deliberate vagueness is not. The profile
// check's UNDECLARED line reports that.)
//
// It also cannot register a pass that NEVER RUNS, because begin() is never reached for it -- so each key is
// ALSO registered eagerly at namespace scope in the file that begins it, and gpu_pass_keys asserts the two
// sets are equal in both directions (R14). The descriptor still lives here, at the call.
//
// The desc is spelled out in full at every call rather than hidden behind shape constants like
// `GpuFrameBudget`. Deliberate: cadence and role are exactly the fields that have been mis-set historically,
// and reading `Recompute` in place tells you the count is checked against recomputes rather than frames. The
// cost is that two sites of one key could drift apart -- that is DETECTED (descConflict), not silent.
class GpuTimer {
public:
  virtual ~GpuTimer() = default;

  virtual void begin(String const& name, MetricDesc const& desc) = 0;
  virtual void end(String const& name) = 0;
  // Last value read back for `name`, or nothing if none has landed yet. Results arrive ~3 frames late by
  // design: reading them sooner would stall the pipeline, and an instrument that changes what it measures is
  // not an instrument.
  virtual Maybe<int64_t> lastMicros(String const& name) const = 0;
};

// Read the renderer's pixels back, so one render path can be certified BIT-IDENTICAL to a reference path.
//
// This is DEV SCAFFOLDING and it is meant to die. A8 (Gated Ascension) says a gate exists to certify a layer,
// and a gate that has passed comes down: once the retained-surface identity is certified, this whole type and
// its devOnly reference surfaces should be DELETED, not carried. It lives behind its own accessor precisely so
// that deletion is a two-file operation rather than surgery on the renderer's contract.
class RenderOracle {
public:
  virtual ~RenderOracle() = default;

  // Arm/disarm allocation of framebuffers marked "devOnly" in config -- surfaces that exist only to be
  // compared against. They are screen-sized, so unarmed they would be pure wasted VRAM; keeping them
  // unallocated is the difference between a surface that is earned and one that is merely speculative.
  // Flipping this reloads the framebuffer set, so call it only when the arming changes, and never mid-frame.
  virtual void setEnabled(bool enabled) = 0;
  // Count of differing texels between two framebuffers, with the first differing texel's position and
  // (optionally) the largest absolute channel difference. {0, _} means bit-identical.
  virtual pair<size_t, Vec2U> compare(String const& a, String const& b, float* maxAbsDiff = nullptr) = 0;
  virtual Image read(String const& frameBufferId) = 0;
};

}
