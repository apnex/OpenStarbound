#ifndef STAR_METRIC_DESC_HPP
#define STAR_METRIC_DESC_HPP

#include <stdint.h>

namespace Star {

// WHAT A METRIC IS, declared once at registration and never inferred at sample time.
//
// THIS IS THE SHARED VOCABULARY, and it lives in core rather than in the sovereign metrics/ component
// on purpose. Two components use it and they are two DUTIES, not one: Telemetry is the subject
// reporting on itself -- in-process, bound to the recording call site (#167), and perturbing by
// construction -- while metrics/ is an outsider measuring any pid and must never be a peer of its
// subject. What converges here is a vocabulary, not a duty, and a vocabulary two components share
// belongs where both are granted. See docs/superpowers/specs/
// 2026-08-06-metric-descriptor-convergence-design.md.
//
// EVERY FIELD IS A CHEAP VALUE, and that is a hard constraint rather than a preference. MetricDesc is
// constructed as a TEMPORARY, PER CALL, PER FRAME at the GPU timer sites --
//   gpuTimer().begin("render.pass.environment.gpu_us", MetricDesc{...})
// -- so a String member would add a heap allocation per timer per frame to the render path. Thirteen
// GPU timers over a three-hundred-frame window is thousands of allocations, on the exact path this
// system exists to measure without disturbing. The prose fields are therefore `char const*`: they are
// always literals at the registration site, and the registry copies them into a String ONCE when the
// metric is first registered, where cost does not matter.

// Inference is not merely inconvenient here, it is WRONG: GPU query results are read back and recorded by the
// MAIN thread several frames after the GPU did the work -- see OpenGlRenderer::GlGpuTimer, which polls a
// rotating query ring via glGetQueryObjectuiv(GL_QUERY_RESULT_AVAILABLE). Stamping the recording thread, the
// obvious design, would therefore label every GPU sample as CPU/main. Declaration is both correct and
// cheaper, costing nothing on the sampling path.
enum class MetricDomain : uint8_t { Unknown, Cpu, Gpu };

// The LOGICAL budget a sample belongs to -- deliberately not an OS thread. WorldClient::lightingCalc() runs on
// its own thread or inline on the main thread depending on WorldClient::m_asyncLighting (set by
// setAsyncLighting); it belongs to the `Lighting` budget either way. "Which budget does this cost land in"
// was always the question; "which thread ran it" never was.
// Count is a SENTINEL, not an owner. It exists so ownerName() can be a table with a static_assert on its
// size, which makes "added an owner, forgot to name it" a COMPILE ERROR rather than a silent one. The
// switch it replaced compiled clean with a case missing: -Wswitch warns, but this build has no -Werror and
// the warning drowns in the output. That mattered more here than for the other three descriptor enums,
// because ownerName's fallback was "unknown" -- which silently drops the metric out of its owner's budget
// and quietly under-reports the whole. (domain/cadence/role keep their switches on purpose: their
// fallbacks are deliberately SAFE -- cadence falls back to "call", which is unscaled and cannot inflate,
// and role to "detail", which is excluded from sums and so under-counts rather than over-counts.)
// Keep Count last; nothing may be added after it.
enum class MetricOwner : uint8_t { Unknown, Frame, Gl, Sim, Lighting, Process, Count };

// Which of the owner's tick counters this metric's count is checked against. NOT used to compute per-frame
// cost -- that is always total / frames. Cadence exists so a gated pass sampling 60% of frames is reported as
// 60% COVERAGE rather than mistaken for a metric that is 40% broken.
enum class MetricCadence : uint8_t { Call, Frame, Tick, Recompute };

// Whether this metric is a whole, a part of a whole, or neither.
//   Total  -- IS the owner's whole; excluded from the sum of parts.
//   Budget -- a part; sums with its siblings and must close against the owner's Total.
//   Detail -- nested inside a Budget part; never summed. render.frame.us and render.interface.us both nest
//             inside cpu.frame.render.us, so summing all three would double-count.
enum class MetricRole : uint8_t { Detail, Budget, Total };

// ---------------------------------------------------------------------------------------------------
// The fields below are the CONVERGENCE. Each is earned by a defect that actually happened; none is
// speculative. An `Undeclared` enumerator is always value 0, so a descriptor that says nothing says so
// EXPLICITLY rather than by asserting a default nobody checked.
// ---------------------------------------------------------------------------------------------------

// EARNED BY: the in-process descriptor has no unit at all, while the out-of-process MetricSample carries
// a free-text one ("ns", "ratio"). So the two stores cannot be compared, and every in-process unit lives
// in a key suffix instead -- suffixes that already disagree in spelling (cpu.frame.total.us vs
// cpu.process.total_us) and one gauge that encodes a scale factor in its NAME
// (lighting.gpu.spread.max_emission_x1000). Meanwhile scripts/telemetry-window.py hardcodes microseconds
// in its bucket bounds, its per-tick columns and its fps arithmetic, so a nanosecond metric entering that
// shape is wrong by a factor of 1000 with no error -- and every kernel CPU source is nanoseconds.
// An ENUM, not a string, because the consumer must CONVERT rather than assume.
enum class MetricUnit : uint8_t { Undeclared, Nanoseconds, Microseconds, Bytes, KiB, Count, Ratio, Hertz };

// EARNED BY #235: the cpu-domain timers record WALL time -- blocking, lock wait and preemption included --
// under names asserting work. A phase that looks expensive under contention is indistinguishable from one
// that is expensive, and a lever aimed at the wrong one measures null.
//
// IT HAS NO DEFAULT, DELIBERATELY. Defaulting to Wall would make every unmigrated timer assert a clock
// nobody checked, which is worse than silence: a field claiming otherwise is more convincing than an
// absent one. Defaulting to ThreadCpu would be flatly false for cpu.wait.lighting.us, which measures
// blocking ON PURPOSE. Undeclared means "nobody has said yet" and is counted by a ratchet.
enum class MetricClock : uint8_t { Undeclared, NotApplicable, Wall, ThreadCpu, ProcessCpu, GpuEngine, GpuTimeline };

// EARNED BY: the fdinfo reading of 96.8% against a truth of 24.2% -- and by the two stores recording
// provenance incomparably. MetricSample carries a free-text source ("fdinfo:drm-engine",
// "i915-pmu:rcs0-busy"); MetricDesc carries nothing. The distinction that decides whether a reading
// perturbs its subject is expressible on one side of the seam only.
// MIGRATION NOTE for the seam gate: the existing free-text values map onto these -- "fdinfo:*" to ProcFs,
// "i915-pmu:*" to PerfEvent.
enum class MetricSource : uint8_t { Undeclared, InProcess, ProcFs, SysFs, PerfEvent, GlQuery };

// EARNED BY A DEFECT THAT HAS ALREADY SHIPPED TWICE, and is not a memory concern. Two RUNNING-MAXIMA
// gauges ship today -- lighting.gpu.spread.max_emission_x1000 and lighting.lights.max_intensity_x1000 --
// and scripts/telemetry-window.py windows EVERY gauge as a level ("a gauge is a level, not an
// accumulation: its delta is meaningless, so carry the latest reading"). The consumer therefore cannot
// tell a level from a peak. Commit bf6fa6bf records the correction firing the second time; the histogram
// `max` field records a third instance in the type system. This is the field that lets a consumer REFUSE
// to difference a peak.
enum class MetricBoundedness : uint8_t { Undeclared, Monotonic, Level, HighWaterMark };

struct MetricDesc {
  MetricDomain domain = MetricDomain::Unknown;
  MetricOwner owner = MetricOwner::Unknown;
  MetricCadence cadence = MetricCadence::Call;
  MetricRole role = MetricRole::Detail;

  MetricUnit unit = MetricUnit::Undeclared;
  MetricClock clock = MetricClock::Undeclared;
  MetricSource source = MetricSource::Undeclared;
  MetricBoundedness boundedness = MetricBoundedness::Undeclared;

  // THE ONLY SAME-TYPED ADJACENT PAIR IN THIS STRUCT, and therefore the only transposition hazard.
  // Every other field is a distinct enum type, so swapping domain and owner is ALREADY a compile error
  // -- which is why converting every registration site to designated initializers would buy nothing.
  // These two are both `char const*` (with `whole` behind them), so writing them in the wrong order
  // compiles, passes every check, and produces a descriptor whose stated meaning and stated condition
  // are swapped. It does not show: the strings are only ever printed.
  //
  // scripts/metric-desc-lint.py therefore requires DESIGNATED INITIALIZERS (.measures = / .validWhen =
  // / .whole =) at any site that sets one. A convention is not enforcement; the lint is.
  //
  // THAT SENTENCE WAS FALSE FOR AS LONG AS IT STOOD HERE. The lint was named, described and credited
  // with enforcing the rule, and the file did not exist -- so the paragraph asserting a check was in
  // place was the only thing holding the rule up. Written 2026-08-06, it fired on its first run: the
  // single site in the tree that sets these fields is telemetry_test's fixture FOR this hazard, and it
  // was built positionally. The gate reports how many sites it examined and how many set a prose field,
  // so a pass with nothing to check reads as one rather than as a clean bill of health.
  //
  // NOTE: an earlier draft proposed strong_typedef for these. It does not work. The macro's guard is
  // `explicit NewType(BaseType const&)` -- explicit only from a String -- while `using BaseType::BaseType`
  // inherits String's own non-explicit String(char const*). A bare literal therefore converts implicitly
  // to EITHER tag, and bare literals are exactly what the call sites pass.
  char const* measures = nullptr;   // the physical quantity, in words
  char const* validWhen = nullptr;  // the condition under which the value IS that quantity

  // The total this part closes against, when it is NOT the (owner, domain) total. Absent means "use the
  // (owner, domain) total", so this costs nothing for metrics already correctly denominated and is
  // declared only by the ones that need a different whole. EARNED BY #171: the (owner, domain) table
  // permits exactly ONE whole per pair and a second row silently overwrites the first, so producer-side
  // lighting CPU -- six untimed costs billed to cpu.frame.render.us under owner Frame -- has no whole it
  // can name. The board records the requirement verbatim: "needs a SECOND Total per owner, which is not
  // representable".
  char const* whole = nullptr;
};

inline bool operator==(MetricDesc const& a, MetricDesc const& b) {
  // char const* compared by POINTER, not by strcmp. Every one of these is a string literal from a single
  // registration site, so two descriptors for the same metric share the same pointer, and two different
  // literals are a genuine disagreement worth reporting even in the pathological case where their bytes
  // match. This keeps the comparison trivial on a path that runs per frame.
  return a.domain == b.domain && a.owner == b.owner && a.cadence == b.cadence && a.role == b.role
      && a.unit == b.unit && a.clock == b.clock && a.source == b.source
      && a.boundedness == b.boundedness
      && a.measures == b.measures && a.validWhen == b.validWhen && a.whole == b.whole;
}
inline bool operator!=(MetricDesc const& a, MetricDesc const& b) { return !(a == b); }

}

#endif
