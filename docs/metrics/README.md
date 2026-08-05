# `source/metrics/` — measurement as a duty

This component measures GPU work from **outside** the process doing it, and it exists because three
numbers this project acted on were artefacts of their own meters.

`render.pass.parallax.gpu_us` appeared to step 4.04x and stay there, with a static camera and every
oracle green; the step was the instrument (a timer bracketing a gate it did not enter, read through a
magnitude-biased filter). A "96.8% GPU busy" figure used to validate the corrected timers was wrong by
4x, and announced itself only on the next run as 117.3% — an arithmetic impossibility for one engine.
A "+47% instrument cost" was one run of a fresh script against a cold shader cache.

The shape is the same each time: **a number was quoted with nothing in the system able to contradict
it.** Each was eventually caught by something outside the measurement — a histogram, an
impossibility, a replication — and never by the oracles, because a wrong number is a plausible
number. This component's job is to make that contradiction routine rather than lucky.

Design: [`docs/superpowers/specs/2026-08-05-sovereign-metrics-design.md`](../superpowers/specs/2026-08-05-sovereign-metrics-design.md).

---

## The dependency rule

**`metrics` is granted `core`, and nothing it measures.**

Not a style preference. `metrics --pid N` reads another process's kernel accounting; if it linked the
renderer it would be measuring a build of the thing it is part of, and every result would carry the
question of whether the instrument perturbed the subject. Because it links only `star_core` and
`star_metrics`, it can be pointed at the **shipped, uninstrumented** binary — which is both the
sovereignty proof and a capability the previous telemetry never had.

The rule is enforced by the layering gate, not by intent. The renderer does not link `metrics`,
cannot perturb it, and does not know it exists.

## Two readers, and why there are two

They measure the same physical quantity — render-engine busy nanoseconds — by different kernel
mechanisms, at **different scopes**. That difference is the entire point.

| | `ClientBusyReader` | `EngineBusyReader` |
|---|---|---|
| mechanism | `/proc/<pid>/fdinfo/*`, `drm-engine-*` lines | `perf_event_open` on the i915 PMU |
| scope | **one process**, summed over its DRM clients | **the whole device**, including work others caused |
| privilege | none | `perf_event_paranoid <= 0` or `CAP_PERFMON` |
| ordinarily | available | **unavailable** — this host sits at paranoid 2 |

With exactly one GPU client running, the two scopes coincide, so the readings must agree. Two
instruments that *can* disagree is the property the old GPU telemetry never had, and its absence is
why a 4x arithmetic error and a 4.04x sampling artefact each survived a full day of analysis. The
agreement is checked by `scripts/metrics-mutual-check.sh`; see below.

`EngineBusyReader` being unavailable on the ordinary path is not a portability footnote to apologise
for. A privileged instrument that silently reads zero is *worse* than an absent one, because absence
is visible.

## The dedup rule: one client, not one fd

`/proc/<pid>/fdinfo` holds one file per open fd. A process that dups the DRM device gets a file per
dup — and each of those files reports the **same `drm-client-id`** and the **same whole-client**
`drm-engine-render` total. Not a per-fd share of it. The same total, restated.

starbound holds four such fds. Every line below is one live client, read off this host:

```
/proc/638363/fdinfo/6:drm-client-id:      193
/proc/638363/fdinfo/6:drm-engine-render:  835027544 ns
/proc/638363/fdinfo/7:drm-client-id:      193
/proc/638363/fdinfo/7:drm-engine-render:  835027544 ns
/proc/638363/fdinfo/8:drm-client-id:      193
/proc/638363/fdinfo/8:drm-engine-render:  835027544 ns
/proc/638363/fdinfo/9:drm-client-id:      193
/proc/638363/fdinfo/9:drm-engine-render:  835027544 ns
```

One client, one total, four restatements of it.

Summing across fds therefore multiplies busy time by the dup count. That is exactly what happened:
the GPU was reported **96.8% busy against a truth of 24.2%**, and the error was caught only because
the next sample read 117.3% and one engine cannot exceed 100%. Had the load been lighter, 4x of 15%
is 60% — plausible, unfalsifiable, and it would still be in the record.

So every reading is keyed by `drm-client-id` and **assigned** per client, then summed over *distinct*
clients. Assigned, not accumulated: a second fd carrying client 158 is a restatement, and adding it
is the bug. This is the whole reason `ClientBusyReader` is a class rather than a loop over a
directory. Chrome does the same thing on this host right now.

## The never-return-zero rule

**A reader that cannot read says so. It never returns zero.**

`render.pass.compose.gpu_us` reported `0us` for its entire existence. It bracketed a conditional that
never ran, and an empty bracket looks exactly like free work. Nobody questioned it, because zero is
what a cheap pass is supposed to cost.

`BusyReading` therefore carries `available` and a non-empty `unavailableReason` — the difference
between "measured nothing" and "could not measure", which no caller may conflate. Every failure path
names itself: no fdinfo, no `drm-engine-*` lines, PMU absent, event absent, privilege absent, counter
moved backwards because the client restarted. The CLI exits **3** for unavailability and prints
**nothing at all** on stdout, so a caller parsing stdout finds no number rather than a zero.

`MetricSample` enforces the same rule one level up: `measures` (the physical quantity, in words) and
`validWhen` (the condition under which the value *is* that quantity) are constructor arguments and
must be non-empty. The parallax defect was precisely a gap between a name and a meaning, and nothing
in the old model was capable of holding that gap. Here it cannot be reintroduced by omission — only
by writing something false on purpose.

---

## Running it

### The CLI

```
dist/metrics --pid <N> [--for <SECS>] [--json]
```

It samples the target's fdinfo, sleeps, samples again, and reports per-engine busy nanoseconds and
busy ratio. `measures` and `validWhen` print by default, not behind a verbose flag: a tool that
prints a bare number teaches its reader to supply the meaning from memory, which is how
`render.pass.parallax.gpu_us` came to mean something other than its name.

```
$ dist/metrics --pid 638363 --for 8
metrics: pid 638363, window 8.001s, 1 DRM client(s)
  ...
  gpu.engine.render.busy_ns                2899527124 ns     measures: render engine busy time, summed over this process's DRM clients | valid when: always
  gpu.engine.render.busy_ratio               0.362415 ratio  measures: render engine busy time as a fraction of wall clock | valid when: always
```

Engines the process never touched report `0` — and that is a *measurement* of zero, distinguishable
from unavailability by the exit code and by stdout being empty when the reader could not read.

Exit codes are the contract: `0` measured, `3` could not measure, `64` usage error.

### The mutual check

```
scripts/metrics-mutual-check.sh [pid]        # default target: pgrep -x starbound
scripts/metrics-mutual-check.sh --selftest   # verdict arms only: no GPU, no target, no root
```

It runs `dist/metrics` and `scripts/pmu-render-busy.py` **concurrently over one window** and compares
their render busy ratios. Concurrently, because two adjacent windows of a live scene differ by more
than the quantity under test, and comparing them charges the workload's variance to the instruments.

The PMU reader is a standalone `ctypes` implementation against `perf_event_open`, and it is separate
on purpose: running the C++ `EngineBusyReader` against the C++ `ClientBusyReader` would share the
parser, the syscall wrapper, the arithmetic and the author — an instrument checked against itself.

It needs a GPU client under real load, and root or `CAP_PERFMON`. To get one:

```
taskset -c 6-15 nice -n 19 scripts/render-profile.sh 600 mutual --warp "03-Surface Outpost"
```

**A run that could not compare exits 0 and says so in words that are not a pass.** There are four such
conditions, each with its own message: the PMU is unreadable, another process was using the render
engine (so device-wide and per-client are not the same quantity), a reader became unavailable
mid-window, or the GPU was near idle — at 0.005% busy even a 4x error stays inside the bound, so
agreement there would prove nothing. A check that cannot run must never read as a check that passed.
A missing target exits **2**: a comparison with no subject is a caller error, not a result.

## What the bound is, and what it is not

The tolerance is 1.00 percentage point, measured rather than chosen. Forty concurrent 8-second
windows against a live client at 20.9–25.5% render busy gave a median difference of 0.076pp and a
worst of 0.286pp; the bound is 3.5x that worst. What it exists to catch is a **factor, not a
fraction** — the fdinfo dup bug was 4x, and the smallest mis-scaling worth a name, 2x, is +23pp at
this load. Nothing between 0.3pp and 1.0pp is a defect this comparison could name.

A tolerance nobody has watched fire is not known to fire, so `--selftest` drives every arm in both
directions, including the exact 4x and 2x errors above.

## A property of the PMU you will otherwise misread

The i915 busy counter is **published lazily**. Read it only at the two ends of a window and you will
sometimes read it wrong: sampling every 0.5s under a steady 35% load, four intervals in forty
advanced by *exactly zero nanoseconds*, and the interval after each one advanced by double. The time
is not lost, it is delivered late — so a two-read window that ends inside a stale interval
under-reports by whatever has not been published yet. Two-read 8-second windows measured **0.0% and
50%** of the truth on 2 of 20 runs.

The staleness is relative to the reader, not a fixed kernel period, so `scripts/pmu-render-busy.py`
samples throughout the window and bounds the endpoint error at roughly one poll interval. It also
discards a warm-up: the first publication after `perf_event_open` delivers everything that
accumulated while nobody held the counter, which was **2.74 seconds of busy time** in one trace.

`EngineBusyReader::open()` returns its first sample immediately and `sample()` is called only by its
caller, so the C++ reader carries both exposures. It has no consumer today — the CLI uses the fdinfo
reader — but anything that adopts it must poll, not bracket.
