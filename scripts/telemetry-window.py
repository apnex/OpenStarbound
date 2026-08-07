#!/usr/bin/env python3
"""Difference two cumulative telemetry snapshots into a per-frame cost table.

Every counter, timer and histogram bucket in a snapshot is CUMULATIVE since process start, so a single snapshot
reports the average over the whole run -- world load, shader compilation and atlas warm-up included. Those
dominate the early seconds and drag every per-pass number toward a value that describes no moment of play.
Differencing two snapshots taken inside the steady state gives the cost over THAT window and nothing else.

This consumer knows NOTHING about the engine. It reads `owners` for each budget's denominator and total, and
`metrics` for each metric's domain/owner/cadence/role. That is the point of schema v2: the previous version
discriminated GPU metrics by a filename suffix and hard-coded which metric counted frames, and got it wrong.

  telemetry-window.py <snapshot-dir> [--label NAME] [--first N] [--last N] [--json OUT]
"""
import argparse
import json
import os
import sys

SCHEMA = 4
BUCKETS = 64
# Tolerated skew between a timer's histogram sum and its count. The engine has four sampling threads at most
# (main, server, lighting, and the GL readback path), each able to be mid-record() at either snapshot endpoint;
# 16 is that with generous headroom, and still orders of magnitude below any real sample loss.
HIST_SKEW_SLACK = 16
# Tolerated overshoot of a cadence-declared count against its own tick counter. TWO, not one: a snapshot
# is taken with the sampling threads running, so the tick counter and a phase timer inside that tick are
# read at slightly different instants at BOTH endpoints of the window -- one boundary each. Measured: five
# server-tick timers read 301 against 300 ticks on a clean 20s capture, every time. Three or more is not a
# boundary artefact, it is a span opening twice per tick, which the assertion still catches.
CADENCE_BOUNDARY_SLACK = 2


def load(path):
    with open(path) as f:
        return json.load(f)


# THE CLOSURE BOUND, AND WHERE THE NUMBER CAME FROM.
#
# ROUND ONE OF THIS BOUND WAS AIMED AT THE WRONG TARGET, and the correction is the point. It was set at
# 4.0% from a ten-run null control of the gl/gpu closure, which spread -1.14%..+2.10% and went positive
# 6 times in 10. That measurement was real. What it measured was not a budget.
#
# The gpu "parts" were the 13 GL_TIME_ELAPSED pass timers, and source/client/StarClientApplication.cpp
# states plainly what they are: "The per-pass GL_TIME_ELAPSED timers are NOT ADDITIVE -- with 12 of them
# their sum overshot the real frame by 5ms, because each bracket serialises the pipeline and measures its
# own stall. They rank passes; they do not budget them." Three lines below that comment the same call site
# declared MetricRole::Budget -- "a part; sums with its siblings and must close against the owner's Total".
# The code stated the rule and violated it in the next statement. All 13 now declare Detail, so the gpu
# domain has no parts to sum and this bound no longer applies to it at all.
#
# THE CONTROL THAT PROVED IT. The same ten runs, same consumer, same windowing, measured on the CPU
# domains, which ARE a genuine partition:
#     frame/cpu     mean -0.01%  sd 0.01pp  range -0.03..-0.00%   positive  0/10
#     sim/cpu       mean -0.22%  sd 0.03pp  range -0.27..-0.19%   positive  0/10
#     lighting/cpu  mean -0.77%  sd 0.28pp  range -1.51..-0.53%   positive  0/10
#     gl/gpu        mean +0.36%  sd 0.99pp  range -1.14..+2.10%   positive  6/10
# Thirty CPU observations, not one positive, and thirty to a hundred times tighter than the gpu spread.
# So the wobble was never "how telemetry works here" -- it was specific to the non-additive parts.
#
# THE BOUND IS THEREFORE RE-DERIVED FROM CPU DATA, where a closure genuinely exists. Worst owner is
# lighting at mean+3sd = -0.77 + 3*0.28 = +0.07%. 1.0% clears that by an order of magnitude and still
# catches a real double count. 4.0% would have swallowed a hundred-fold defect on frame/cpu, whose true
# spread is 0.01pp.
CLOSURE_TOLERANCE = 0.01

# The measured spread itself, kept so the selftest asserts the bound against the DATA it was derived
# from rather than against a number retyped from a commit message.
NULL_CONTROL_PCTS = [-0.03, -0.00, -0.19, -0.27, -0.53, -1.51, -0.22, -0.77, -0.01, -0.09]


def closure_verdict(owner, dom, parts, whole):
    """None, or the violation string. Pure so it can be exercised without a capture."""
    if not whole:
        return None
    un = whole - parts
    over = -un / whole              # positive when the parts exceed the whole
    if over > CLOSURE_TOLERANCE:
        return (f"{owner}/{dom}: parts exceed the whole by {-un} us ({100 * over:.2f}%), beyond the "
                f"{100 * CLOSURE_TOLERANCE:.1f}% measured agreement bound -- a Detail metric "
                f"declared as Budget, or a double-counted phase")
    if un / whole > 0.25:
        return (f"{owner}/{dom}: {100 * un / whole:.0f}% unattributed "
                f"-- the instrumentation is missing a phase")
    return None


def bucket_bounds(i):
    """Lower and upper microsecond bound of histogram bucket i (see Telemetry::histogramBucket).

    BOTH ENDS ARE CLAMPS, not ranges. Bucket 0 absorbs 0 and any negative sample; bucket 63 is UNBOUNDED
    ABOVE -- [57344, inf), not [57344, 65536). Treating 63 as a closed range is not a rounding error, it
    silently caps every hitch at 65 ms, which is precisely the case histograms were added to see: a 200 ms
    stall would be reported as ~65 ms and read as merely bad rather than catastrophic.
    """
    h, m = divmod(i, 4)
    lo = (2 ** h) * (1 + m / 4)
    if i == BUCKETS - 1:
        return lo, float("inf")
    return lo, (2 ** h) * (1 + (m + 1) / 4)


def percentile(buckets, q):
    """Estimate the q-th percentile (0..1) from windowed bucket counts, interpolating within the bucket.

    Returns (value, saturated). `saturated` is True when the percentile falls in the unbounded top bucket, in
    which case the value is a LOWER BOUND and must be rendered as such -- reporting a midpoint of an infinite
    interval would be inventing a number.
    """
    total = sum(buckets)
    if not total:
        return 0.0, False
    target, seen = q * total, 0
    for i, c in enumerate(buckets):
        if not c:
            continue
        if seen + c >= target:
            lo, hi = bucket_bounds(i)
            if hi == float("inf"):
                return lo, True
            return lo + (hi - lo) * ((target - seen) / c), False
        seen += c
    return bucket_bounds(BUCKETS - 1)[0], True


def window(a, b, zeroed=None):
    # `zeroed`, when a caller passes a list, collects the REGISTERED timers whose windowed count is zero.
    # They are dropped from the result -- thirty always-zero rows would bury the table -- and dropping them
    # silently is the same ABSENT-vs-ZERO defect one level out: a pass that ran on the baseline leg and not
    # on the off leg loses its row entirely, which reads as "infinitely cheaper" rather than "did not run".
    # Registering the key at the source (the engine now does) is only half the fix if the consumer then
    # hides the zero. So the drop is REPORTED rather than made invisible.
    """Delta between two snapshots, carrying each metric's descriptor forward."""
    out = {}
    for name, mb in b.get("metrics", {}).items():
        ma = a.get("metrics", {}).get(name, {})
        # A KEY ABSENT FROM SNAPSHOT A IS NOT A KEY THAT WAS ZERO. Every value here is cumulative
        # since process start, so differencing against a missing entry treats "not registered yet" as
        # "was 0" and hands back the metric's ENTIRE life -- world load, shader compilation, atlas
        # warm-up -- labelled as this window's cost. It is the ABSENT-vs-ZERO defect again, at the
        # consumer end this time, and it is silent by construction: the number looks like every other
        # number.
        #
        # Registration hoisting has made this rare rather than impossible: a static handle on a path
        # first reached mid-window still registers mid-window. So it is FLAGGED, not assumed away.
        first_seen = name not in a.get("metrics", {})
        # FORWARD THE WHOLE DESCRIPTOR, not a hardcoded five. This tuple used to be exactly
        # ("type","domain","owner","cadence","role"), which meant a schema-4 snapshot carrying
        # unit/clock/source/boundedness had those fields DROPPED here -- before any assertion could
        # read them. The version check would pass and the consumer would still window a nanosecond
        # metric as microseconds, silently, by a factor of 1000. "The schema bump is the migration" was
        # true for old-snapshot/new-reader and false for the direction that actually matters.
        #
        # Copying every key except the value payload means a field added to MetricDesc reaches this
        # consumer with no second edit, which is the same one-declaration rule the ratchet ceilings
        # already follow via --from-cmake.
        d = {k: v for k, v in mb.items() if k not in ("count", "total", "mean", "buckets", "value")}
        if mb.get("type") == "timer":
            dc = mb.get("count", 0) - ma.get("count", 0)
            if dc <= 0:
                if zeroed is not None:
                    zeroed.append(name)
                continue
            ba, bb = ma.get("buckets", []), mb.get("buckets", [])
            ba = ba + [0] * (BUCKETS - len(ba))
            bb = bb + [0] * (BUCKETS - len(bb))
            d.update(count=dc,
                     total=mb.get("total", 0) - ma.get("total", 0),
                     buckets=[y - x for x, y in zip(ba, bb)])
            d["mean"] = d["total"] / dc
            if first_seen:
                d["firstSeenInWindow"] = True
        elif mb.get("type") in ("counter", "gauge", "rate"):
            va, vb = ma.get("value", 0), mb.get("value", 0)
            # A gauge is a level, not an accumulation: its delta is meaningless, so carry the latest reading.
            d["value"] = vb if mb.get("type") == "gauge" else vb - va
            if first_seen and mb.get("type") != "gauge":
                d["firstSeenInWindow"] = True
        out[name] = d
    return out


def tick_count(m):
    """A metric's windowed tick count, regardless of whether it is a timer or a counter.

    Owner denominators are declared against whatever metric happens to count the ticks for that owner, and
    that metric is not always a timer: `sim`'s denominator is `tick.server.seq`, a plain sequence COUNTER
    (cadence=tick), and `lighting`'s is `lighting.temporal.recomputed`, also a counter (cadence=recompute).
    Only `frame`/`gl` happen to use a timer (`cpu.frame.total.us`) as their denominator. A timer's tick count
    is its windowed `count`; a counter/gauge/rate's tick count is its windowed `value`. Reading `count` off a
    counter entry (which has no such key) silently returns 0 via dict.get's default -- which reads as "no
    ticks in this window" and drops the whole owner table rather than raising, so this has to be type-aware
    rather than a blind `.get("count", 0)`.
    """
    if not m:
        return 0
    return m.get("count", 0) if m.get("type") == "timer" else m.get("value", 0)


def cadence_expectation_map(owners, w):
    """Map each cadence value to the metric that counts ticks at that cadence -- derived from the schema's
    own data, not from a hard-coded engine key name.

    An owner's declared `denominator` IS the tick-counting metric for whatever cadence its ticks are
    declared in -- `frame` denominates in frames, `sim` in server ticks, `lighting` in recomputes -- and each
    of those denominator metrics carries its OWN `cadence` field equal to the very unit it counts
    (cpu.frame.total.us is cadence=frame, tick.server.seq is cadence=tick, lighting.temporal.recomputed is
    cadence=recompute). Scanning owners for their denominators and reading each denominator's own cadence
    back out gives the cadence -> counter mapping without this file ever naming an engine metric. `call`
    cadence never appears here: nothing is denominated in raw calls, so it has no expectation to check
    against, by construction.
    """
    out = {}
    for spec in owners.values():
        dn = spec.get("denominator")
        m = w.get(dn) if dn else None
        c = m.get("cadence") if m else None
        if c and c not in out:
            out[c] = dn
    return out


def coverage_scale(v, cadence_ticks, w):
    """Coverage-scale a timer's windowed total up to what full observation would have summed to.

    A metric's count can fall short of its cadence's tick count for two different reasons that look
    identical from the count alone but need OPPOSITE arithmetic:
      - SAMPLING LOSS: the work happened, the instrument missed it (an async GPU timer query that has not
        resolved by readback time). The honest total is bigger than what got recorded -- scale it up.
      - GENUINE GATING: the work did not happen (a refresh-gated pass skipped this tick). The honest total
        IS what was recorded -- scaling it up would invent cost for ticks that had none.
    The tell is the metric's own declared `cadence`. frame/tick/recompute means the work is declared to
    happen at that cadence, so any shortfall against that cadence's tick count is sampling loss, corrected
    by total / coverage (a no-op when coverage is 1). `call` means there is no cadence to fall short of --
    an arbitrary number of calls per frame is not "coverage" of anything -- so it is returned unscaled.
    Returns (scaled_total, coverage, expected); coverage/expected are None when nothing was scaled.
    """
    cad = v.get("cadence")
    if cad == "call":
        return v["total"], None, None
    expected = tick_count(w.get(cadence_ticks.get(cad)))
    if not expected:
        return v["total"], None, None
    coverage = v["count"] / expected
    return v["total"] / coverage, coverage, expected


def selftest():
    """Prove the closure bound fires AND that it does not over-fire. Both ends measured (#194's rule).

    A tolerance nobody has watched fire is not known to fire -- that is #223, where a BOUNDED-diff
    oracle was read as a zero-diff one and reported green for weeks. This bound was introduced to stop
    a check crying wolf, so the risk it carries is the opposite one: silence mistaken for correctness.
    """
    fails = []

    def arm(name, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            fails.append(name)

    W = 1_000_000

    # 1. The exact spread the bound was derived from must NOT fire. This is the arm that would catch
    #    someone lowering the bound below the noise it exists to absorb.
    quiet = [p for p in NULL_CONTROL_PCTS
             if closure_verdict("frame", "cpu", W * (1 + p / 100.0), W) is None]
    arm(f"all {len(NULL_CONTROL_PCTS)} cpu null-control observations quiet "
        f"(worst {max(NULL_CONTROL_PCTS):+.2f}%)", len(quiet) == len(NULL_CONTROL_PCTS))

    # 2. ...and a breach beyond the bound DOES fire. Without this the bound could be infinity.
    over = closure_verdict("gl", "gpu", W * (1 + CLOSURE_TOLERANCE + 0.01), W)
    arm(f"excess of {100*CLOSURE_TOLERANCE + 1:.0f}% fires", over is not None and "exceed" in over)

    # 3. The bound is not vacuous: a DOUBLED pass -- the defect it is meant to catch -- trips it. The
    #    smallest gpu part that could be duplicated and still caught is one worth 4% of the frame; at
    #    the measured location render.pass.parallax is 7.6%, so a duplicate of it is caught.
    arm("the +2.10% mis-declaration that started #236 would now fire",
        closure_verdict("frame", "cpu", W * 1.0210, W) is not None)

    # 4. THE HONEST LIMIT, asserted rather than merely admitted in a comment: a duplicated part smaller
    #    than the bound is NOT caught. If someone later tightens the bound, this arm flips and forces
    #    them to re-read the trade instead of silently changing what the check covers.
    arm(f"a duplicated part below {100 * CLOSURE_TOLERANCE:.1f}% is NOT caught (known blind spot, #236)",
        closure_verdict("frame", "cpu", W * 1.005, W) is None)

    # 5. The unattributed-ceiling rule still fires -- it shares the function and must not have been
    #    disarmed by the rewrite.
    arm("30% unattributed still fires",
        (closure_verdict("gl", "gpu", W * 0.70, W) or "").find("unattributed") >= 0)

    # 6. A perfectly closed budget is silent, so arm 5 is not passing because everything fires.
    arm("an exactly-closed budget is silent", closure_verdict("gl", "gpu", W, W) is None)

    # 7. A zero whole must not divide. It is the no-data case and belongs to the NO WHOLE branch.
    arm("zero whole does not raise", closure_verdict("gl", "gpu", 0, 0) is None)

    # 8. A RANK DOMAIN IS NOT A BROKEN BUDGET. With the 13 gpu pass timers demoted to Detail the gpu
    #    domain has a whole and no parts. Asking closure_verdict would report "100% unattributed -- the
    #    instrumentation is missing a phase", which is exactly backwards: the instrumentation is right
    #    and the SUM was the thing that was wrong. So the caller must not ask, and this arm pins that.
    arm("a whole with no parts would be mis-reported if asked -- so the caller must not ask",
        (closure_verdict("gl", "gpu", 0, W) or "").find("unattributed") >= 0)

    # 9. A REGISTERED TIMER THAT RECORDED NOTHING MUST REACH THE CONSUMER, and must not reach it as a
    #    metric. This is R14's consumer half: the engine now registers every GPU pass key at zero, but
    #    window() drops zero-delta timers, so without `zeroed` the --json artifact was identical whether
    #    a key was registered-at-zero or absent from the binary -- and the three distinct key sets that
    #    R14 measured across 27 legs would have persisted unchanged after the engine fix. The second half
    #    of the arm is load-bearing too: a count=0 row inside `metrics` would reach coverage_scale.
    snap = lambda c: {"metrics": {"render.pass.x.gpu_us": {"type": "timer", "count": c, "total": 0,
                                                           "domain": "gpu", "owner": "gl",
                                                           "cadence": "call", "role": "detail"}}}
    z = []
    m = window(snap(0), snap(0), z)
    arm("a timer that recorded nothing lands in `zeroed`", z == ["render.pass.x.gpu_us"])
    arm("...and is kept OUT of `metrics`, where it would reach coverage_scale", m == {})

    print()
    if fails:
        print(f"telemetry-window selftest: FAILED -- {len(fails)} arm(s): {', '.join(fails)}")
        return 1
    print("telemetry-window selftest: 10/10 arms ok -- the bound fires, does not over-fire, its blind "
          "spot is asserted, the rank case is distinguished from a broken budget, and a timer that "
          "recorded nothing is reported rather than silently dropped")
    return 0


def main():
    if "--selftest" in sys.argv[1:]:
        return selftest()
    ap = argparse.ArgumentParser()
    ap.add_argument("snapdir")
    ap.add_argument("--label", default="profile")
    ap.add_argument("--first", type=int, default=None)
    ap.add_argument("--last", type=int, default=None)
    ap.add_argument("--json", default=None)
    # Declared, not inferred. A window whose length depends on how many files happened to exist is a
    # window nobody chose.
    ap.add_argument("--intervals", type=int, default=1,
                    help="how many snapshot intervals the window spans (default 1)")
    args = ap.parse_args()

    files = sorted(f for f in os.listdir(args.snapdir) if f.endswith(".json"))
    if len(files) < 2:
        print(f"need >=2 snapshots in {args.snapdir}, found {len(files)}", file=sys.stderr)
        return 1

    # Trim the ends when affordable: the first snapshot sits closest to load, the last may be a partial
    # interval cut short by the kill.
    # THE WINDOW LENGTH WAS A SNAPSHOT-COUNT LOTTERY. The rule used to be "drop the first and last IF
    # there are at least 4 files, otherwise drop neither" -- so N=4 gave a ONE-interval window and N=5
    # gave TWO, i.e. exactly twice the duration, with nothing recording which you got. That is the
    # observed 600-vs-300-frame capture: same command, same location, a window twice as long because
    # one more snapshot happened to land.
    #
    # Now the interval COUNT is explicit and the leg is refused if the files cannot supply it. The
    # first file is still dropped (closest to load) and so is the last (may be a partial interval cut
    # short by the kill); the window is then the LAST `intervals` intervals of what remains, so a run
    # that produced extra snapshots yields the same window shape rather than a longer one.
    if args.first is not None or args.last is not None:
        lo = args.first if args.first is not None else 0
        hi = args.last if args.last is not None else len(files) - 1
    else:
        hi = len(files) - 2
        lo = hi - args.intervals
        if lo < 1:
            print(f"need >={args.intervals + 3} snapshots for a {args.intervals}-interval window "
                  f"(first and last are trimmed); found {len(files)}", file=sys.stderr)
            return 1
    a, b = load(os.path.join(args.snapdir, files[lo])), load(os.path.join(args.snapdir, files[hi]))

    schema = b.get("meta", {}).get("schema", 0)
    if schema != SCHEMA:
        # Refuse rather than mis-window. A pre-v2 snapshot has no descriptors, and guessing them is how the
        # 119% error happened in the first place.
        print(f"snapshot schema {schema}, expected {SCHEMA} -- rebuild and re-capture", file=sys.stderr)
        return 2

    zeroed = []
    owners, w = b.get("owners", {}), window(a, b, zeroed)
    cadence_ticks = cadence_expectation_map(owners, w)
    meta = dict(b.get("meta", {}))
    # Environment facts the engine deliberately does not read (driver- and platform-specific sysfs paths).
    sidecar = os.path.join(args.snapdir, os.pardir, "env-sidecar.json")
    if os.path.exists(sidecar):
        try:
            meta.update(load(sidecar))
        except (OSError, ValueError):
            pass  # a missing or malformed sidecar must never invalidate a real measurement

    print(f"\n=== {args.label} === window: {files[lo]} -> {files[hi]}  ({hi - lo} intervals)")
    print(f"    vsync={meta.get('vsync')}  "
          f"gpuMHz={meta.get('gpuClockMhzStart')}->{meta.get('gpuClockMhzEnd')}  "
          f"pkgTempC={meta.get('packageTempCStart')}->{meta.get('packageTempCEnd')}\n")

    violations = []
    # UNDECLARED, SAID OUT LOUD. The owner loop below skips owner=="unknown", and skipping is all it did:
    # a metric registered with an empty MetricDesc left the tables with no mention, so "nobody declared
    # this" and "this owner has no metrics" were the same output. StarRenderDiagnostics.hpp cites this line
    # as what catches a deliberate `{}` at a GPU timer -- it is cited by the design, so it has to exist.
    # Not a violation: an undeclared metric is under-described, not wrong.
    undeclared = sorted(k for k, m in w.items() if m.get("owner") in (None, "unknown"))
    if undeclared:
        print(f"  UNDECLARED ({len(undeclared)}) -- no owner declared, so these are in no budget and appear "
              f"in no table below:")
        for k in undeclared:
            print(f"       {k}")
        print()
    for owner in sorted({m["owner"] for m in w.values() if m.get("owner") not in (None, "unknown")}):
        spec = owners.get(owner, {})
        denom_name, totals = spec.get("denominator"), spec.get("totals", {})
        denom = tick_count(w.get(denom_name)) if denom_name else 0
        if not denom:
            continue
        rows = [(k, v) for k, v in w.items() if v.get("owner") == owner and v.get("type") == "timer"]
        if not rows:
            continue

        print(f"  [owner: {owner}]  {denom} ticks ({denom_name})")
        print(f"  {'metric':<40} {'dom':>4} {'role':>7} {'calls':>8} {'cover':>6} "
              f"{'us/tick':>9} {'p50':>8} {'p99':>9}")
        print(f"  {'-'*40} {'-'*4} {'-'*7} {'-'*8} {'-'*6} {'-'*9} {'-'*8} {'-'*9}")

        # Coverage-scale every row's total ONCE, up front, so the same scaled figure backs both the printed
        # us/tick column and the closure sum below -- computing it twice would risk the two disagreeing.
        scaled = {k: coverage_scale(v, cadence_ticks, w) for k, v in rows}

        for k, v in sorted(rows, key=lambda kv: -kv[1]["total"]):
            s_total, coverage, expected = scaled[k]
            per_tick = s_total / denom
            # `cover` is the RAW observed fraction (count / this metric's OWN cadence expectation), never the
            # scaled one -- it exists specifically so a reader can see that a 67%-coverage figure was scaled
            # up, not to be hidden by the scaling. `call`-cadence metrics (no expectation) print "n/a".
            cover_str = f"{100.0 * coverage:.0f}%" if coverage is not None else "n/a"
            p50, p50sat = percentile(v["buckets"], 0.50)
            p99, p99sat = percentile(v["buckets"], 0.99)
            # A saturated percentile landed in the unbounded top bucket, so the figure is a LOWER BOUND. Render
            # it as ">=" rather than as a number: a hitch past 65 ms printed as a plain value reads as merely
            # bad instead of unbounded, and that is exactly the case the histogram was added to expose.
            print(f"  {k:<40} {v['domain']:>4} {v['role']:>7} {v['count']:>8} {cover_str:>6} "
                  f"{per_tick:>9.1f} {('>=' if p50sat else '') + f'{p50:.1f}':>8} "
                  f"{('>=' if p99sat else '') + f'{p99:.1f}':>9}")
            # ASSERTION 2 (cadence bound): checked against THIS METRIC'S OWN cadence expectation, not the
            # owner's ticks -- an owner's table can legitimately mix cadences (lighting.cpu.total.us is
            # frame-cadence inside the recompute-denominated `lighting` owner), so the owner tick count is the
            # wrong yardstick for this check. Under is legitimate -- a gated pass or an async readback samples
            # only some ticks, which the coverage column reports. Over means the span opened twice per tick.
            # SLACK ONE, not exact -- for the reason ASSERTION 3 below already gives about histograms, and
            # by the same mechanism. A snapshot is taken while the sampling threads are running, so the
            # tick counter and a phase timer inside that tick are read at slightly different instants at
            # BOTH endpoints. One phase sample landing on the far side of a boundary makes count exceed
            # the tick count by exactly one, and it did: five server timers read 301 against 300 ticks on
            # a clean 20s capture -- 0.33%, at every window, on an unchanged system.
            #
            # That mattered more than it looks. The runner marks a leg's costs unquotable when this
            # consumer exits non-zero, so a permanent boundary artefact would have marked EVERY leg of
            # the matrix unquotable and left the campaign with no usable numbers at all.
            #
            # The slack is CADENCE_BOUNDARY_SLACK, deliberately small: one boundary per endpoint. Two or
            # more is not a boundary, it is a span opening twice per tick, which is the defect this
            # assertion exists to catch and which it still catches.
            if (v.get("cadence") in ("frame", "tick", "recompute") and expected
                    and v["count"] > expected + CADENCE_BOUNDARY_SLACK):
                violations.append(f"{k}: count {v['count']} > expected {expected} (+{CADENCE_BOUNDARY_SLACK} "
                                  f"boundary slack) for its own cadence={v['cadence']} (should it be 'call'?)")
            # ASSERTION 3 (histogram consistency): the buckets are the instrument's own checksum -- but a
            # SLACK one, not an exact one. record() bumps count first and buckets last, both relaxed and with
            # no fence, so a snapshot taken while a sampling thread is mid-record() observes a skew of up to
            # one per in-flight thread, in EITHER direction (relaxed stores to different locations may be
            # observed out of order). Two snapshot endpoints double that. Asserting exact equality here would
            # fire on correct data and train the reader to ignore the oracle -- which is the failure this
            # whole subsystem exists to prevent. A real loss is orders of magnitude larger than the slack.
            skew = abs(sum(v["buckets"]) - v["count"])
            if skew > HIST_SKEW_SLACK:
                violations.append(f"{k}: histogram sum {sum(v['buckets'])} vs count {v['count']} "
                                  f"(skew {skew} > {HIST_SKEW_SLACK})")

        # ASSERTION 1 (budget closure). Both the whole and its parts are the COVERAGE-SCALED totals: comparing
        # raw totals directly is exactly the mistake that produced the original 119%-of-whole error, just one
        # level removed -- the whole and its parts do not all share the same coverage (the whole's own GPU
        # query typically resolves on FEWER frames than its constituent per-pass queries, since it cannot
        # complete until every part has), so summing raw parts against a raw, more-suppressed whole overstates
        # the closure even when nothing is double-counted.
        # THE WHOLE IS PER (OWNER, DOMAIN). It used to be per owner, while these parts were already summed
        # per domain -- so a cpu-domain Budget metric under owner `gl` was divided by a GPU span and printed
        # as "cpu accounted: N us/tick of <gpu span>". Labelled by one domain, denominated by another.
        for dom in sorted({v["domain"] for _, v in rows}):
            parts = sum(scaled[k][0] for k, v in rows if v["role"] == "budget" and v["domain"] == dom)
            total_name = totals.get(dom)
            if not total_name or total_name not in w:
                # LOUD, never silent. A domain carrying budget parts with no whole to close them against is
                # unclosable, and an unclosable budget that prints nothing reads exactly like a closed one --
                # the same shape as a gate that skips and reports OK (see scripts/ci/run-gates.sh).
                if parts:
                    why = "no total declared for this domain" if not total_name \
                          else f"declared total '{total_name}' is absent from the window"
                    print(f"\n  {dom} accounted: {parts/denom:8.1f} us/tick of UNKNOWN -- {why}")
                    violations.append(f"{owner}/{dom}: {parts} us of budget parts with NO WHOLE -- "
                                      f"{why}. Nothing was closed; this is not a pass")
                continue
            whole = scaled[total_name][0] if total_name in scaled else coverage_scale(w[total_name], cadence_ticks, w)[0]
            if not any(v["role"] == "budget" and v["domain"] == dom for _, v in rows):
                # A DOMAIN WITH A WHOLE AND NO PARTS IS A RANK TABLE, NOT A BROKEN BUDGET. The 13 GPU
                # pass timers are Detail by declaration because they are NOT ADDITIVE -- each
                # GL_TIME_ELAPSED bracket serialises the pipeline and charges its own stall to itself,
                # so with 12 of them the sum overshot the real frame by 5ms. They rank passes; they do
                # not close them, and the whole-frame span is the only trustworthy figure.
                print(f"\n  {dom}: whole {whole/denom:8.1f} us/tick  [{total_name}] -- RANK ONLY, no "
                      f"closure claimed. The per-pass timers are Detail by declaration (not additive); "
                      f"read them as a ranking, and the whole as the budget.")
                continue
            un = whole - parts
            pct = 100.0 * parts / whole if whole else 0.0
            print(f"\n  {dom} accounted: {parts/denom:8.1f} us/tick of {whole/denom:.1f} "
                  f"({pct:.1f}%) -- unattributed {un/denom:.1f} us/tick  [whole: {total_name}]")
            if un < 0:
                # PRINTED EVEN WHEN WITHIN TOLERANCE. A bound that silently absorbs everything under it
                # is how a slow drift becomes invisible: the excess would have to cross 4% in one step
                # to ever be seen again. This line makes the magnitude readable on every capture, so a
                # drift toward the bound is noticeable before it trips.
                print(f"     parts exceed the whole by {-un} us ({-100.0*un/whole:.2f}%) "
                      f"-- bound is {100*CLOSURE_TOLERANCE:.1f}%, measured (#236)")
            v = closure_verdict(owner, dom, parts, whole)
            if v:
                violations.append(v)
        print()

    # THE HEADLINE CPU NUMBER IS BUSY, NOT TOTAL.
    #
    # The main loop is PACED: Thread::sleepPrecise(m_updateTicker.spareTime()) runs even with vsync off, so
    # cpu.frame.total.us measures the frame PACE, not the frame COST. Measured 2026-07-25 on the first real
    # budget capture: total 16393 us/frame of which idle was 10619 -- 65% of the frame is sleep.
    #
    # That makes total useless as an A/B metric until a regression exceeds the entire idle reserve. A +1 ms CPU
    # regression moves busy 5774 -> 6774 and idle 10619 -> 9619, and total reads 16393 BOTH TIMES: the A/B
    # reports NO CHANGE for a real, shipped regression. Quote busy. Report total only as the pace it is.
    tot, swap, idle = w.get("cpu.frame.total.us"), w.get("cpu.frame.swap.us"), w.get("cpu.frame.idle.us")
    if tot:
        # idle records ONLY on frames that actually slept, so its count < total's. Normalise by the frame
        # count, not idle's own count -- the same denominator trap that has already fired three times here.
        idle_pf = (idle["total"] / tot["count"]) if (idle and tot["count"]) else 0.0
        busy = tot["mean"] - idle_pf
        print(f"  CPU busy: {busy:.0f}us/frame of a {tot['mean']:.0f}us pace "
              f"({100.0 * busy / tot['mean']:.0f}% utilised) -- BUSY is the A/B metric, total is the pacer")
    if tot and swap:
        sp, ip = swap["mean"], (idle["mean"] if idle else 0.0)
        if meta.get("vsync"):
            verdict = "vsync ON -- swap measures frame PACING, not backpressure; bound is not determinable"
        elif sp > 0.20 * tot["mean"]:
            verdict = f"GPU-BOUND (swap {sp:.0f}us = {100*sp/tot['mean']:.0f}% of frame)"
        elif ip < 0.05 * tot["mean"]:
            verdict = f"CPU-BOUND (idle {ip:.0f}us, swap {sp:.0f}us -- no headroom, not waiting on GPU)"
        else:
            verdict = f"HEADROOM ({100*ip/tot['mean']:.0f}% idle)"
        p50, p50sat = percentile(tot["buckets"], 0.50)
        p99, p99sat = percentile(tot["buckets"], 0.99)
        print(f"  VERDICT: {verdict}")
        print(f"  frame: mean {tot['mean']:.0f}us  p50 {'>=' if p50sat else ''}{p50:.0f}us  "
              f"p99 {'>=' if p99sat else ''}{p99:.0f}us  -> {1e6/tot['mean']:.0f} fps mean")
        if p99sat:
            print("         !! p99 is in the unbounded top bucket (>=57ms): the frame is hitching, and the "
                  "histogram cannot say how badly. Capture a per-frame trace if this persists.")

    firsts = sorted(k for k, v in w.items() if v.get("firstSeenInWindow"))
    if firsts:
        # LOUD, and it takes the leg's costs with it. A metric whose first appearance is inside the
        # window carries its whole cumulative history in that number; nothing downstream can tell.
        print(f"\n  !! {len(firsts)} metric(s) FIRST REGISTERED inside this window -- their values are"
              f" cumulative since process start, not windowed:")
        for k in firsts[:8]:
            print(f"       {k}")
        violations.append(f"{len(firsts)} metric(s) first registered inside the window "
                          f"({', '.join(firsts[:4])}{' ...' if len(firsts) > 4 else ''}) -- their "
                          f"values are lifetime totals, not window deltas")

    if zeroed:
        # NOT a violation -- a registered timer that recorded nothing in the window is a legitimate and
        # often correct state (a mutually-exclusive compose arm, a fallback path the config never takes).
        # It is REPORTED because the row is dropped from the tables above, and a row present on one leg and
        # dropped on the next reads as "this pass got infinitely cheaper" rather than "it did not run".
        # The engine registers these keys at zero precisely so the distinction exists; hiding it here would
        # spend that -- and until R14 this was printed to STDOUT ONLY, so every automated consumer saw the
        # same `metrics` dict whether a key was registered-at-zero or absent from the binary entirely. It
        # now travels in --json BESIDE `metrics`, never inside it: a count=0 row placed among the metrics
        # would reach coverage_scale and the cadence over-count check, both of which are sound only
        # because zero-delta rows never get that far.
        print(f"\n  registered timers that recorded NOTHING in this window ({len(sorted(zeroed))}) -- "
              f"dropped from the tables above, listed so their absence is a fact rather than a gap:")
        for k in sorted(zeroed):
            print(f"       {k}")

    if violations:
        print("\n  !! ORACLE VIOLATIONS -- do not quote these numbers:")
        for v in violations:
            print(f"     {v}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"label": args.label, "window": [files[lo], files[hi]],
                       "meta": dict(meta, intervals=hi - lo, windowIndices=[lo, hi]), "owners": owners,
                       "metrics": w, "zeroed": sorted(zeroed), "violations": violations}, f, indent=2)
        print(f"\n  wrote {args.json}")

    return 3 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
