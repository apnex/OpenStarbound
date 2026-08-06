# Metric descriptor convergence: design

**Status:** proposed, revised after adversarial audit. Extends
`2026-08-05-sovereign-metrics-design.md` (GM-0).

**Goal.** One vocabulary for what a metric MEANS, shared by the in-process store and the
out-of-process readers, so that a number's name can no longer disagree with its value — and so CPU
can become first-class without inheriting three incompatible shapes.

**Revision note.** The first draft of this document was audited against the tree across seven
dimensions. Thirty-seven claims verified; ten did not. Two of its declines were wrong, its
transposition fix did not work at the call sites that matter, and its drift gate could never have
compared anything. Those corrections are folded in below rather than appended, and §8 records what
the audit changed so the reasoning is not silently rewritten.

---

## 1. The decision, and why it is not "merge the two"

**CONVERGE THE DESCRIPTOR. KEEP TWO STORES. PUT THE SEAM AT THE SUBJECT BOUNDARY.**

**Why not one store.** `Telemetry` is a process-global singleton with no subject parameter anywhere
in its contract, and `metrics/`'s defining property is measuring a *different* process. Merging means
either instantiating that singleton per subject — a large change to a lock-free registry whose raw
`MetricNode*` handles rest on two stability guarantees — or encoding the subject in key strings, at
which point the closure oracle silently sums across subjects. That is a new instance of the defect
class this whole effort exists to remove.

There is also a hard cost floor. `MetricSample` holds five `String`s and throws on an empty
`measures` or `validWhen`. It cannot go on a per-call bracket firing thousands of times a frame, and
softening it to fit would delete the constructor checks that are the type's entire reason to exist.

**Why not two independent models.** The two blocked CPU items are both TELEMETRY-side: #235 (a timer
must declare which clock it takes) and #171 (producer-side lighting CPU needs a second whole). A
sovereign `metrics/` discharges neither. And anything outside `telemetry-window.py`'s closure check
sits outside the only automatic contradiction detector the system has.

So: **one `MetricDesc`, two value types, one wire vocabulary.**

| | in-process | out-of-process |
|---|---|---|
| value | `MetricNode` — lock-free relaxed atomics | `MetricSample` — a value with its meaning attached |
| meaning | `MetricDesc`, paid once at registration | `MetricDesc`, carried per sample |
| subject | this process only | any pid |

Meaning is paid at REGISTRATION in-process, which preserves the lock-free path. The descriptor stays
bound to the recording call site the way #167 established — separating them is the reachability bug
class that task closed.

---

## 2. What the descriptor carries

Today: `domain`, `owner`, `cadence`, `role`. Seven fields are added. Each is earned by a defect that
actually happened, and for each the earning defect is stated in the form the tree supports — the
first draft twice described an existing free-text field as absent, which is the exact shape of the
defect this document exists to end.

### 2.1 `unit` — an enum, not a string

```
enum class MetricUnit : uint8_t { Undeclared, Nanoseconds, Microseconds, Bytes, KiB, Count, Ratio, Hertz };
```

**Earned by:** the two stores disagree about whether a unit exists at all. `MetricSample` carries a
free-text `unit` (`"ns"`, `"ratio"`); `MetricDesc` carries no unit field, so every in-process unit
lives in a key suffix, and the suffixes disagree in spelling (`cpu.frame.total.us` vs
`cpu.process.total_us`). One gauge encodes a scale factor in its name outright
(`lighting.gpu.spread.max_emission_x1000`). Meanwhile `telemetry-window.py` hardcodes microseconds in
its bucket bounds, its per-tick columns and its fps arithmetic — **a nanosecond timer entering that
shape is off by 1000 with no error** — and every kernel source we are about to add is nanoseconds.

**Enum, not string**, because the consumer must be able to CONVERT rather than assume, and because a
free-text unit on one side of a seam and no unit on the other is not a vocabulary. A string unit is a
second naming convention doing a type's job, which is the situation being repaired.

### 2.2 `measures` — the physical quantity, in words

**Earned by:** `render.pass.parallax.gpu_us`, whose name said "GPU microseconds of the parallax pass"
and whose value meant "elapsed span of a bracket that may contain no work, sampled through a
magnitude-biased filter". This is the field whose absence IS the standing lesson.

### 2.3 `validWhen` — the condition under which the value IS that quantity

**Earned by:** `render.pass.compose.gpu_us` reported `0us` for its entire existence. The deep gate
makes a zero-count timer mean "deep off" and "never fired" indistinguishably. The i915 PMU's warm-up
and lazy publication make a window valid only under a stated settling condition.

### 2.4 `clock` — which clock a timer takes

```
enum class MetricClock : uint8_t { Undeclared, NotApplicable, Wall, ThreadCpu, ProcessCpu, GpuEngine, GpuTimeline };
```

**Earned by #235:** the cpu-domain timers record WALL time — blocking, lock wait and preemption
included — under names asserting work.

**It has no sensible default, so it does not get one.** Defaulting to `Wall` would make every
unmigrated timer assert a clock it was never checked against — worse than silence, because a field
claiming otherwise is more convincing than an absent one. Defaulting to `ThreadCpu` would be false
for `cpu.wait.lighting.us`, which measures blocking ON PURPOSE.

`Undeclared` is therefore a real value meaning *nobody has said yet*, counted by a ratchet. **The
ratchet's corpus and seed are derived by the gate, not written here** — see §7, which is where the
first draft got this wrong.

### 2.5 `source` — provenance

```
enum class MetricSource : uint8_t { Undeclared, InProcess, ProcFs, SysFs, PerfEvent, GlQuery };
```

**Earned by:** the fdinfo reading of 96.8% against a truth of 24.2% — and by the fact that the two
stores record provenance incomparably. `MetricSample` carries a free-text `source`
(`"fdinfo:drm-engine"`, `"i915-pmu:rcs0-busy"`); `MetricDesc` carries nothing at all. So no reader
can compare them, and the distinction that decides whether a reading perturbs its subject is
expressible on one side of the seam only.

**Migration note for the gate:** the existing free-text values must map onto the enumerators —
`"fdinfo:*"` to `ProcFs`, `"i915-pmu:*"` to `PerfEvent`. The first draft believed this field did not
exist, which would have had the two sides disagreeing on day one.

### 2.6 `whole` — the total this part closes against. ADOPTED (the first draft declined it, wrongly)

```
Maybe<String> whole;   // absent = use the (owner, domain) total
```

**Earned by #171.** GM-2a keyed the whole on `(owner, domain)`, which fixed a real defect — a GPU
whole denominating CPU parts. It did not fix this one: **that table permits exactly one whole per
pair**, and a second row for the same pair silently overwrites the first. #171 needs a second CPU
whole under owner `Frame`, for six untimed producer-side lighting costs currently billed to
`cpu.frame.render.us`. The board records the requirement verbatim: *"Doing it properly needs a SECOND
Total per owner, which is not representable."*

The first draft declined this field on two grounds, and both were wrong:

* *"the problem is fixed"* — true of GM-2a's defect, false of the one that remains.
* *"more references means more silent drops"* — that hazard is about the DENOMINATOR
  (`telemetry-window.py` skips an owner whose denominator windows to zero). An unresolved *whole* has
  been LOUD since the very commit cited: the consumer prints `of UNKNOWN -- declared total '<name>'
  is absent from the window`, appends a violation, and exits 3. GM-2a's own message says so: *"A
  DOMAIN WITH NO DECLARED WHOLE IS LOUD, NOT SKIPPED."*

**Optional, not universal**, which answers the original YAGNI objection properly. Absent means "use
the `(owner, domain)` total", so the field costs nothing for the metrics that are already correctly
denominated and is declared only by the ones that need a different whole. The reference count is
O(metrics that need one), not O(127).

### 2.7 `boundedness` — how the value accumulates. ADOPTED (the first draft deferred it, wrongly)

```
enum class MetricBoundedness : uint8_t { Undeclared, Monotonic, Level, HighWaterMark };
```

**Earned by a defect that has already shipped twice.** The first draft deferred this as a future
memory concern — "nothing collects memory yet". That is true and irrelevant: two high-water-mark
gauges ship today and neither is memory. `lighting.gpu.spread.max_emission_x1000` and
`lighting.lights.max_intensity_x1000` are both RUNNING MAXIMA registered as `Gauge`, and
`telemetry-window.py` windows every gauge as a level — *"A gauge is a level, not an accumulation: its
delta is meaningless, so carry the latest reading"* — so **the consumer cannot tell a level from a
peak**. Commit `bf6fa6bf` records the correction firing the second time: *"lighting.lights.max_
intensity_x1000 already carries this exact correction … it would have shipped a number that could not
support the conclusion drawn from it."* The type system records a third instance: *"the `max` field
is a run-long high-water mark and is NOT windowable."*

This is the field that lets a consumer REFUSE to difference a peak. Deferring it would have meant
looking straight at a live, thrice-recorded defect and filing it as speculative.

---

## 3. The transposition hazard, and the fix that actually works

`MetricSample` takes four consecutive same-typed `String`s. Transposing `measures` and `validWhen`
compiles, passes both non-empty checks, and yields a sample whose stated meaning and stated condition
are swapped. **The failure mode is that it does not show.**

**The first draft's fix was `strong_typedef`, and it does not work.** The macro's guard is
`explicit NewType##Wrapper(BaseType const&)` — explicit only when constructing *from a `String`*. It
also carries `using BaseType::BaseType;`, which inherits `String`'s own constructors including the
non-explicit `String(char const*)`. So a **bare string literal converts implicitly to either tag**,
and the real call sites pass bare literals and `char const* const` constants positionally. The
protection would apply only where the argument is already a typed `String`, which is the minority of
sites and not the one the test uses.

Two further edges, both unbudgeted: `strf("{}", Measures(...))` fails fmt's static assert — and
`metrics_main.cpp` formats these fields today, so it would stop compiling — and a strong-typedef'd
`String` cannot be a `HashMap` key without an added `Star::hash` specialization, which is the natural
shape for §5's comparison.

**The fix is NAMED FIELDS.** After §2 the enums remove `unit`, `clock`, `source` and `boundedness`
from the same-typed run entirely, leaving exactly two adjacent `String`s: `measures` and `validWhen`.
Construct `MetricDesc` with designated initializers, so the call site reads:

```cpp
MetricDesc{ .domain = MetricDomain::Cpu, .owner = MetricOwner::Lighting,
            .cadence = MetricCadence::Recompute, .role = MetricRole::Budget,
            .unit = MetricUnit::Microseconds, .clock = MetricClock::ThreadCpu,
            .measures  = "CPU time in the tile gather",
            .validWhen = "always" }
```

A transposition is now visible at the call site rather than invisible in an argument list, and C++20
requires designated initializers to follow declaration order, so a *reordered* pair is a compile
error. **Enforced by a lint**, in the manner this repo already uses for `getOrDefault` declarations,
GPU-timer brackets and comment citations: constructing `MetricDesc` without designated initializers
is a build-time failure. Convention alone is what the first draft relied on and is not enforcement.

**And the test must not share the code's construction shape.** The first draft's remedy — "state the
expected `measures` and `validWhen` as literals" — is *already satisfied* by `metrics_test.cpp`, and
that test is still transposition-blind, because the assertion literals are copies of the constructor
literals in the same file. The property actually needed is an INDEPENDENT source of truth: assert
against the real emitted line from `metrics_main`, or a golden fixture not authored beside the
constructor.

---

## 4. Where it lives — and why not in `source/metrics/`

`MetricDesc` and its enums go to **`source/core/StarMetricDesc.hpp`**, included by both
`StarTelemetry.hpp` and `source/metrics/`.

The obvious objection is that a sovereign `metrics/` already exists, so the vocabulary of measurement
should live there. Three placements were considered.

**Vocabulary in `metrics/`, `Telemetry` stays in `core`.** Impossible: `core` would include
`metrics`, and `metrics -> core` is the grant. A literal cycle.

**Move `Telemetry` into `metrics/`.** Genuinely available, and worth stating so, because the reason
to decline it is not a dependency: nothing in `core` uses `Telemetry` except the four
`StarTelemetry*` files themselves, so no cycle blocks the move. It is declined because it would
**cost the link-graph proof**. `source/metrics/CMakeLists.txt` records the module's defining
property — the CLI links `star_core` and `star_metrics` and *nothing of the game*, so it can be run
against a shipped uninstrumented binary without perturbing what it reads — and the TSSA grant warrant
records the same thing: `metrics` names nothing it measures, because a grant to `rendering` or `game`
would make the instrument a peer of its subject. `Telemetry` is that peer BY CONSTRUCTION: it is
bound to the recording call site (#167), it lives inside the subject, and it perturbs. Moving the
registry into `star_metrics` degrades a structural property into an argument ("linked but unused") —
and it gets worse as the system grows, since every in-process collector added under the north star
would then land inside the component whose whole claim is to be outside.

**Vocabulary in `core`. Chosen.** These are two duties, not one:

| | `Telemetry` | `metrics` |
|---|---|---|
| stance | the subject reports on itself | an outsider measures the subject |
| perturbs | yes, by design | never — that is the duty |
| subject | this process only | any pid |

What converges is a VOCABULARY, not a duty. A vocabulary two components share belongs in the
component both are granted, which is what `core` is for. The grant `metrics -> core` is unchanged,
and `metrics` keeps its readers, its CLI, and its sampling.

---

## 5. Gating the seam — at the QUANTITY, not the key

Two stores emitting one vocabulary will diverge, and the divergence will be silent because nothing
today reads both.

**The first draft proposed comparing descriptors key-by-key. That gate could never have compared
anything**, for two independent reasons, and both are worth recording because the shape recurs:

1. **Nothing to run it on.** The CI gates job is checkout-only — no compiler, no build — so the
   `metrics` CLI does not exist there, and the in-process snapshot needs a running client with a
   world loaded. It would have taken its SKIP path forever.
2. **Nothing to compare even where it can run.** The two stores share ZERO keys. The CLI emits only
   `gpu.engine.{engine}.busy_ns` and `.busy_ratio`; the engine registers no `gpu.engine.*` key at
   all, and 426 captured snapshots have a 129-key union whose prefixes are exactly {animator, cpu,
   lighting, render, sim, tick}. "Any key appearing in both" is the empty set — so the gate would
   run, compare nothing, and exit 0. That is precisely the failure GATE-SKIP-1 was written for,
   caught one beat before shipping it again.

**`metrics_seam_agreement`** therefore compares the same PHYSICAL QUANTITY measured both ways, in the
manner `scripts/metrics-mutual-check.sh` already proves out: take a quantity both stores can express,
convert each through its declared `unit`, and assert they agree within a measured bound. A unit
disagreement — the headline risk, one side `ns` and the other `us` — surfaces as a value
disagreement, which is a stronger check than comparing two spellings of a descriptor.

Three properties are contractual:

* It asserts it RAN. A comparison with nothing to compare exits 77 (SKIP), never 0.
* **An empty comparison set is a SKIP, not a pass.** The first draft's phrasing — "a comparison with
  nothing to compare must skip" — reads naturally as being about a missing subject, and would have
  let an empty intersection read as "compared everything, found no disagreement".
* It lives in the dev-host gate set. In CI it takes the 77 path and `run-gates.sh` reports it as
  UNVERIFIED rather than counting it green.

---

## 6. What this does NOT do

Stated so the boundary is a decision rather than an omission.

* It does not merge the stores, add a subject parameter to `Telemetry`, or make `MetricSample`
  cheap enough for a per-call bracket.
* It does not fix #235 — it makes the fix EXPRESSIBLE. Declaring `clock` per metric is the
  precondition; deciding the right clock for each key is separate work.
* It does not collect anything new. No CPU reader, no memory, no energy. GM-1c follows.
* It does not change `MetricNode`'s storage or the lock-free recording path. It DOES enlarge
  `MetricDesc`, which is paid once per metric at registration — the first draft claimed it touched
  neither, which was false for the descriptor.
* **#171 becomes unblocked but is not closed.** §2.6 supplies the mechanism (a second whole under one
  owner); naming the whole for each producer-side lighting cost, and timing those six costs, is
  separate work. The first draft said nothing about #171 here at all, which let a reader finish this
  section believing both blocked items were addressed.

---

## 7. Migration and risk

**Schema 3 -> 4.** The version check is real — `telemetry-window.py` refuses an unrecognised schema
rather than mis-windowing. But **the bump is not the migration**, and the first draft's claim that it
was is false in the direction that matters: the consumer hardcodes the descriptor keys it forwards
(`type, domain, owner, cadence, role`), so schema-4 fields would be dropped before any assertion sees
them, and its microsecond arithmetic is hardcoded independently of the schema. A nanosecond metric at
schema 4 would still read 1000× wrong, silently. **The consumer change is part of this work**, not a
consequence of the version bump.

**The ratchet's corpus and seed are derived by the gate**, and stated in the gate rather than in
prose. The first draft wrote "58 timers" into the text, and 58 is the wrong number for this purpose
three ways:

* it counts CPU-domain timers only, while the `clock` enum has `GpuEngine` and `GpuTimeline`, so the
  real population is 72;
* the 13 GPU pass timers register through a RUNTIME key, so a ratchet grepping
  `Telemetry::timer("literal")` is structurally blind to exactly the timers §2.2/§2.3 cite as the
  defect source;
* counted without excluding `source/test/`, it includes 15 test fixtures — making "the count may only
  fall" satisfiable by deleting a fixture, a route to green that measures nothing. This project's own
  ratchet log already records one scoring "a perfect result for a net change of zero" because it
  metered only part of its population.

| risk | how it shows | mitigation |
|---|---|---|
| the seam drifts | one side ns, the other us; a dimensionless check passes | §5 quantity-level gate, which must assert it ran AND that its comparison set was non-empty |
| `clock` defaults to a lie | a phase looks expensive under contention; a lever aimed at it measures null | no default; `Undeclared` + a ratchet that may only fall |
| the ratchet meters part of its population | the count falls to zero while GPU timers stay Undeclared | the gate derives the corpus, covers runtime-keyed registrations, and excludes `source/test/` explicitly |
| the consumer ignores the new fields | a ns metric passes the version check and still windows as µs | the `telemetry-window.py` forwarding tuple and its µs arithmetic change in this work, not later |
| a peak gets differenced | a high-water gauge windows as an accumulation, third occurrence | `boundedness`; the consumer refuses to difference `HighWaterMark` |

---

## 8. What the audit changed

Recorded so the revision is visible rather than silent. Seven dimensions, 83 claims, 37 verified.

| § | first draft | now |
|---|---|---|
| 2.1 | "there is no unit field anywhere" | false — `MetricSample` has a free-text `unit`. Restated as the two stores being incomparable, which earns the enum at least as well |
| 2.5 | "nothing records provenance" | false — `MetricSample` has a free-text `source`. Restated, plus the enumerator mapping the gate needs |
| 2.6 | declined the per-metric whole | ADOPTED, optional. The decline was wrong on both grounds and foreclosed #171 |
| 2.7 | deferred `boundedness` as a future memory concern | ADOPTED. It is a live defect that has shipped twice in non-memory gauges |
| 3 | `strong_typedef` | does not work — a bare literal converts implicitly to either tag. Replaced with designated initializers plus a lint |
| 3 | "assert the literals" | already satisfied by the existing test, which is still blind. Needs an independent source of truth |
| 5 | key-by-key descriptor gate | could never compare anything: no build in CI, and zero shared keys. Replaced with a quantity-level gate; empty set is explicitly a SKIP |
| 6 | "does not touch storage"; silent on #171 | corrected; #171's status stated |
| 7 | "the bump is the migration"; 58 in prose | consumer change is in scope; corpus and seed derived by the gate, population 72 |
