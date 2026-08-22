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


def unit_basis(keys, w):
    """-> (basis, assumed, declared) for a set of metrics that are about to be SUMMED together.

    THE DEFECT THIS EXISTS FOR. A closure adds Budget parts and divides by a whole. Addition is only
    meaningful between quantities in the same unit, and until now nothing checked: the arithmetic
    assumed microseconds throughout, INDEPENDENTLY of the schema, so a nanosecond metric declared
    correctly at its site would still be summed as microseconds and read 1000x wrong -- silently, with
    the version check passing. The spec names this in its own risk table ("the seam drifts: one side
    ns, the other us; a dimensionless check passes") and says the consumer change is part of the work
    rather than a consequence of the version bump.

    `basis` is the single declared unit the set agrees on, or None when nothing in the set declares
    one. `assumed` lists the members that declare nothing and are therefore being TAKEN as the basis
    -- named rather than absorbed, because an assumption nobody can see is the defect wearing a new
    field. `declared` is the full set of distinct declared units: more than one means the sum is
    meaningless and the caller must refuse rather than produce a number.

    UNDECLARED IS HOMOGENEOUS WITH ITSELF, and that is a deliberate weakness rather than an oversight.
    109 of the 146 metrics in the banked corpus declare no unit, so refusing every undeclared set would
    refuse most of the evidence this project reasons with and make the instrument useless on its own
    history. The residual hole -- a NEW nanosecond metric that also fails to declare joins the
    undeclared set invisibly -- is closed from the other end, by ratcheting the undeclared count down,
    not by a check here that would have to reject the past to protect the future.
    """
    declared = sorted({w[k]["unit"] for k in keys
                       if k in w and w[k].get("unit") and w[k]["unit"] != "undeclared"})
    assumed = sorted(k for k in keys
                     if k in w and (not w[k].get("unit") or w[k]["unit"] == "undeclared"))
    return (declared[0] if len(declared) == 1 else None), assumed, declared


def conflict_violations(w):
    """-> [str] for every windowed metric whose two registration sites disagreed.

    Kept as a function rather than an inline loop so the self-test can drive it: the conflict it
    reports has never occurred in 72,900 banked readings, so a fixture is the only way to exercise it,
    and a check that can only be reached through a full window() run is a check nobody writes an arm
    for."""
    out = []
    for k in sorted(w):
        if w[k].get("descConflict"):
            out.append(
                f"{k}: descConflict -- two registration sites declared this key with DIFFERENT "
                f"descriptors, so its domain/owner/cadence/role/unit depend on which site ran first. "
                f"Every figure derived from this metric is attributed by a coin toss.")
        if w[k].get("typeConflict"):
            out.append(
                f"{k}: typeConflict -- two registration sites disagree about this key's TYPE "
                f"(counter/gauge/timer). The windowing rule differs per type, so one of the two "
                f"readings is being differenced under the wrong rule.")
    return out


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
            #
            # EXCEPT WHEN IT IS A PEAK, AND THAT DISTINCTION IS WHY `boundedness` EXISTS. A
            # HighWaterMark gauge is written `s = max(s, x)` and never falls, so its latest reading is
            # the largest value seen since PROCESS START -- not since the window opened. Carrying it
            # into `value` beside a counter's honest window delta invites exactly the reading it
            # cannot support: "the peak during this leg". The header records this defect shipping
            # twice (StarMetricDesc.hpp:95-101) and the histogram `max` field as a third instance.
            #
            # So a peak is emitted under its OWN key. A reader that wants it must ask for it by a name
            # that says what it is, and a reader that sums or differences `value` cannot reach it by
            # accident. Undeclared gauges keep the old behaviour unchanged -- 146 of 146 metrics in
            # the banked corpus declare nothing, and rewriting their meaning retroactively would
            # invalidate the only re-readable evidence this project has.
            if mb.get("type") == "gauge" and mb.get("boundedness") == "high_water_mark":
                d["runLongMax"] = vb
                d["peakNotWindowed"] = True
            else:
                d["value"] = vb if mb.get("type") == "gauge" else vb - va
            if first_seen and mb.get("type") != "gauge":
                d["firstSeenInWindow"] = True
        out[name] = d
    return out


def stamp_of(snap, mtime):
    """(epoch seconds, source) for one snapshot, preferring the stamp the engine wrote.

    THE FILE'S OWN STAMP BEATS ITS mtime, and the difference is not academic. Until schema 4 carried
    `tEpochNs` the only time a snapshot had was its mtime -- a property of the FILESYSTEM, not of the
    measurement: copy the file and the number changes. This consumer is now pointed at an ARCHIVE that
    render-profile.sh copies per leg, so mtime-as-truth would have started lying the moment the series
    became retainable. mtime stays as the fallback because every snapshot written before that field
    existed has nothing else.

    The SOURCE travels with the value, because a stamp that silently changed provenance between two legs
    is exactly the shape this file keeps paying for: a number that reads the same and means something else.
    """
    ns = snap.get("meta", {}).get("tEpochNs")
    if isinstance(ns, (int, float)) and ns > 0:
        return round(ns / 1e9, 3), "meta"
    if mtime is not None:
        return round(mtime, 3), "mtime"
    return None, "absent"


def series(snaps, stamps):
    """Per-interval deltas over CONSECUTIVE snapshot pairs -- the stream this consumer used to discard.

    Snapshots are cumulative, so differencing neighbours yields a reading for every declared metric over
    every interval, histogram buckets included, which is p99 over time. main() differences exactly
    files[lo] against files[hi] and drops everything between; its own output has said so all along --
    `"intervals": 15, "windowIndices": [1, 16]` counts fifteen and keeps two.

    REUSES window() rather than reimplementing the delta, so the gauge-is-a-level rule, the
    firstSeenInWindow flag and the descriptor forwarding are the SAME code the headline number uses. A
    series computed by a second, similar-looking differencer is two things that must be kept in step, and
    they would diverge at the first schema field only one of them learned about.

    EVERY interval is emitted, including the two main() trims. Trimming is a choice about which single
    window to QUOTE -- the first sits closest to load, the last may be cut short by the kill -- and a
    consumer holding individually stamped intervals can make that choice for itself. Dropping them here
    would be this function deciding what a plot is allowed to show.
    """
    out = []
    for i in range(len(snaps) - 1):
        a, b = snaps[i], snaps[i + 1]
        na = a.get("meta", {}).get("tMonotonicNs")
        nb = b.get("meta", {}).get("tMonotonicNs")
        # DURATION FROM THE MONOTONIC PAIR WHEREVER BOTH ENDS HAVE ONE. Epoch is the join axis; monotonic
        # is the ruler. An NTP step moves the epoch stamps without moving any work, and at one snapshot
        # every ~5s such a step lands INSIDE an interval rather than harmlessly between runs. Falling back
        # to epoch beats refusing to measure, but the fallback is NAMED in the output, so a rate computed
        # against a stepped clock is identifiable rather than merely wrong.
        if isinstance(na, (int, float)) and isinstance(nb, (int, float)) and nb > na:
            duration, dur_src = (nb - na) / 1e9, "monotonic"
        elif (stamps[i][0] is not None and stamps[i + 1][0] is not None
              and stamps[i + 1][0] > stamps[i][0]):
            duration, dur_src = round(stamps[i + 1][0] - stamps[i][0], 3), stamps[i][1]
        else:
            duration, dur_src = None, "absent"
        z = []
        out.append({
            "index": i,
            "tStartEpoch": stamps[i][0],
            "tEndEpoch": stamps[i + 1][0],
            "stampSource": stamps[i + 1][1],
            "durationS": duration,
            "durationSource": dur_src,
            "metrics": window(a, b, z),
            "zeroed": sorted(z),
        })
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


def no_whole_report(owner, dom, total_name, parts, denom):
    """(line, violation-or-None) for a domain whose declared total is missing from the window.

    SAID OUT LOUD EVEN WHEN THERE ARE NO PARTS (R16). This used to sit behind `if parts:`, so a domain
    with a missing TOTAL and no budget parts printed nothing at all and the whole owner/domain block
    simply vanished -- indistinguishable from an owner that has no metrics. That is not hypothetical
    for gpu: D01 demoted the 13 pass timers to Detail, so the gpu domain has ZERO budget parts, and
    render.frame.gpu_span_us registered four conditionals deep. Lose the total and the entire gl/gpu
    section disappears silently, which is the exact shape of a gate that skips and reports OK.

    A VIOLATION only when parts exist, and that asymmetry is deliberate: with parts, a real budget went
    unclosed and the leg's costs are not quotable. With none, nothing was mis-closed -- but the
    instrument still failed to produce its denominator, and that is a fact about the run.
    """
    why = "no total declared for this domain" if not total_name \
          else f"declared total '{total_name}' is absent from the window"
    if parts:
        return (f"{dom} accounted: {parts/denom:8.1f} us/tick of UNKNOWN -- {why}",
                f"{owner}/{dom}: {parts} us of budget parts with NO WHOLE -- {why}. "
                f"Nothing was closed; this is not a pass")
    return (f"{dom}: NO WHOLE and no budget parts -- {why}. Nothing was measured for this "
            f"owner/domain; read the absence as an instrument gap, not as an empty budget.", None)


def selftest():
    """Prove the closure bound fires AND that it does not over-fire. Both ends measured (#194's rule).

    A tolerance nobody has watched fire is not known to fire -- that is #223, where a BOUNDED-diff
    oracle was read as a zero-diff one and reported green for weeks. This bound was introduced to stop
    a check crying wolf, so the risk it carries is the opposite one: silence mistaken for correctness.
    """
    fails = []
    ran = []

    def arm(name, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        ran.append(name)
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

    # 11-12. A MISSING TOTAL IS SAID OUT LOUD EVEN WITH NO PARTS (R16), and is a violation only when
    #        parts exist. The gpu domain has zero budget parts by declaration, so without this the
    #        whole gl/gpu block vanishes the moment its denominator fails to register.
    line_np, viol_np = no_whole_report("gl", "gpu", "render.frame.gpu_span_us", 0, 1)
    arm("a missing total with NO parts still prints", "NO WHOLE" in line_np and "absent" in line_np)
    arm("...and is not raised as a violation, because nothing was mis-closed", viol_np is None)
    line_p, viol_p = no_whole_report("frame", "cpu", "cpu.frame.total.us", 5000, 100)
    arm("a missing total WITH parts is still a violation", viol_p is not None and "NO WHOLE" in viol_p)

    # 14-16. THE STAMP'S PROVENANCE. The failure this guards is not a crash -- it is a stamp that keeps
    #        working while quietly changing what it means, which is what mtime does the moment a snapshot
    #        is copied. Each source is asserted BY NAME, so a future edit that flips the precedence fails
    #        here rather than in a plot nobody can reconcile six weeks later.
    arm("an in-file tEpochNs wins over mtime",
        stamp_of({"meta": {"tEpochNs": 1_700_000_000_000_000_000}}, 999.0) == (1.7e9, "meta"))
    arm("...mtime is used, and NAMED, when the snapshot predates the field",
        stamp_of({"meta": {"schema": 4}}, 1234.5678) == (1234.568, "mtime"))
    arm("...and absence is a third answer, never a zero",
        stamp_of({}, None) == (None, "absent"))

    # 21-25. UNIT HOMOGENEITY. No nanosecond metric exists anywhere in the tree, so a fixture is the
    #        ONLY way to exercise this -- which is the point rather than a weakness. The defect is that
    #        a correctly-declared ns metric would have been summed as us, silently, at 1000x; a check
    #        that could only be tested by first shipping such a metric would be a check nobody could
    #        write until after it was needed.
    U = {"a": {"unit": "us"}, "b": {"unit": "us"}, "c": {"unit": "ns"}, "d": {}, "e": {"unit": "undeclared"}}
    arm("one declared unit across the set IS the basis",
        unit_basis(["a", "b"], U) == ("us", [], ["us"]))
    arm("two declared units yield NO basis and both are reported",
        unit_basis(["a", "c"], U) == (None, [], ["ns", "us"]))
    arm("an undeclared member is NAMED, not silently absorbed",
        unit_basis(["a", "d"], U) == ("us", ["d"], ["us"]))
    arm("an explicitly 'undeclared' unit counts as undeclared, not as a third unit",
        unit_basis(["a", "e"], U) == ("us", ["e"], ["us"]))
    arm("an all-undeclared set is homogeneous with itself (the declared weakness)",
        unit_basis(["d", "e"], U) == (None, ["d", "e"], []))

    # 26-29. THE CONFLICT FLAGS. Never true in 72,900 banked readings, so a fixture is the only way
    #        to reach them -- which is exactly why they had no reader for so long. A flag that only
    #        fires in circumstances nobody has produced still has to be PROVEN to fire.
    arm("a clean metric raises no conflict violation", conflict_violations({"a": {}}) == [])
    arm("descConflict raises one, naming the key",
        [v.split(":")[0] for v in conflict_violations({"a": {"descConflict": True}})] == ["a"])
    arm("typeConflict raises one too", len(conflict_violations({"a": {"typeConflict": True}})) == 1)
    arm("both flags on one key raise BOTH -- they are different defects",
        len(conflict_violations({"a": {"descConflict": True, "typeConflict": True}})) == 2)
    arm("a falsy flag is not a conflict (the banked corpus is all false)",
        conflict_violations({"a": {"descConflict": False, "typeConflict": False}}) == [])

    # 31-35. THE PEAK. A HighWaterMark gauge is written `s = max(s, x)` and never falls, so its
    #        latest reading is the largest since PROCESS START. The banked corpus declares no
    #        boundedness at all (146 of 146), so this behaviour is unreachable from real data and a
    #        fixture is the only way to exercise it -- which is exactly why it needs one.
    def _g(kind, boundedness, val):
        m = {"type": "gauge", "owner": "lighting", "domain": "cpu", "value": val}
        if boundedness:
            m["boundedness"] = boundedness
        return {"meta": {"schema": SCHEMA, "tMonotonicNs": 0, "tEpochNs": 0}, "metrics": {kind: m}}

    peak = window(_g("p", "high_water_mark", 5), _g("p", "high_water_mark", 9))["p"]
    arm("a HighWaterMark gauge does NOT carry a window `value`", "value" not in peak)
    arm("...it carries runLongMax, named for what it is", peak.get("runLongMax") == 9)
    arm("...and says so, so a reader cannot mistake it", peak.get("peakNotWindowed") is True)
    lvl = window(_g("l", "level", 5), _g("l", "level", 9))["l"]
    arm("a Level gauge still carries the latest reading as `value`",
        lvl.get("value") == 9 and "runLongMax" not in lvl)
    und = window(_g("u", None, 5), _g("u", None, 9))["u"]
    arm("an UNDECLARED gauge is unchanged -- the banked corpus keeps its meaning",
        und.get("value") == 9 and "runLongMax" not in und)

    # 31-34. THE SERIES. Two snapshots -> one interval; three -> two. The arithmetic arm matters most:
    #        a cumulative counter differenced per interval must yield the PER-INTERVAL amount, and the
    #        way to get this wrong is to emit the cumulative value, which looks entirely plausible
    #        (monotonically rising, right units) and is the metric's whole life at every point.
    def _snap(mono, val):
        return {"meta": {"schema": SCHEMA, "tMonotonicNs": mono, "tEpochNs": mono},
                "metrics": {"k": {"type": "counter", "owner": "frame", "domain": "cpu", "value": val}}}
    s3 = series([_snap(0, 10), _snap(2_000_000_000, 30), _snap(5_000_000_000, 90)],
                [(0.0, "meta"), (2.0, "meta"), (5.0, "meta")])
    arm("three snapshots yield two intervals", len(s3) == 2)
    arm("each interval carries the DELTA, not the cumulative value",
        [iv["metrics"]["k"]["value"] for iv in s3] == [20, 60])
    arm("duration comes from the monotonic pair and is named as such",
        [(iv["durationS"], iv["durationSource"]) for iv in s3] == [(2.0, "monotonic"), (3.0, "monotonic")])
    # The fallback has to be exercised, not merely written: a branch that has never run is a branch
    # nobody has checked, and this one only fires on snapshots older than the field it prefers.
    s_fb = series([{"meta": {}, "metrics": {}}, {"meta": {}, "metrics": {}}],
                  [(100.0, "mtime"), (104.5, "mtime")])
    arm("with no monotonic pair it falls back to epoch and says so",
        s_fb[0]["durationS"] == 4.5 and s_fb[0]["durationSource"] == "mtime")

    print()
    if fails:
        print(f"telemetry-window selftest: FAILED -- {len(fails)} arm(s): {', '.join(fails)}")
        return 1
    # THE COUNT IS DERIVED, AND IT WAS NOT. This line read "20/20" as a literal while the arms above
    # it were free to grow, so the first commit to add one shipped an instrument whose own summary was
    # false -- the same shape as prose-claims.py's DANGLING_D drive, which named D14 as its
    # certainly-absent decision and went silent the day D14 was ratified. A count that cannot drift is
    # worth more than a count that reads tidily.
    print(f"telemetry-window selftest: {len(ran)}/{len(ran)} arms ok -- the bound fires, does not "
          "over-fire, its blind spot is asserted, the rank case is distinguished from a broken budget, "
          "a timer that recorded nothing is reported rather than silently dropped, every stamp names "
          "its own source, the series carries per-interval deltas rather than cumulative values, and a "
          "closure whose parts declare different units is REFUSED rather than summed, a run-long peak is "
          "never presented as a window value, and a descriptor conflict finally reaches a reader")
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
    ap.add_argument("--scene", default=None,
                    help="the --warp bookmark this leg measured. Recorded into meta so a consumer "
                         "can select the right noise floor: scene-floor.json shows Desert Town at "
                         "6.6%% CV and 03 Surface Outpost at 46%%, so the same delta means opposite "
                         "things at the two (#279).")
    ap.add_argument("--asset-fingerprint", default=None,
                    help="hash of the asset chain this leg ran against, from "
                         "scripts/asset-fingerprint.sh. Recorded into meta so a leg can say WHICH "
                         "content it measured; without it two legs from different content chains "
                         "are indistinguishable in the artifact (#277).")
    # Declared, not inferred. A window whose length depends on how many files happened to exist is a
    # window nobody chose.
    ap.add_argument("--intervals", type=int, default=1,
                    help="how many snapshot intervals the window spans (default 1)")
    # SEPARATE FLAG, NOT A SIDE EFFECT OF --json. The windowed number is a VERDICT about one chosen
    # interval span and the series is the RAW STREAM under it; folding the second into the first would put
    # ~15x the data behind a name that promises one window, and every existing consumer of --json reads
    # that name.
    ap.add_argument("--series", default=None,
                    help="write the per-interval series (every consecutive pair) as JSON to this path")
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

    # WHEN THE WINDOW WAS, in wall-clock epoch seconds, stamped from the mtimes of the two snapshots the
    # window is actually differenced over. Any out-of-process observer -- the i915 PMU sampler is the one
    # that motivated this -- has a time series and no way to say which of its samples belong to this leg.
    #
    # Stamped HERE and not in render-profile.sh's env sidecar, which brackets the whole measurement PHASE:
    # that is wider than the window by the snapshots --intervals trims, and aligning against the wider
    # bracket silently mixes in the legs' ragged edges. These two files ARE the window; nothing else is.
    #
    # PREFER THE STAMP THE ENGINE WROTE; mtime is now the FALLBACK, not the source. schema 4 carries
    # `tEpochNs` in every snapshot's meta, which survives being copied -- and the snapshots are copied now,
    # per leg, into an archive. An mtime does not survive that, so a rule that was merely fragile would
    # have become wrong the moment the series was retained. stamp_of() reports which it used.
    #
    # The reason this was ever a stat: render-profile.sh purges the snapshot directory at the start of the
    # NEXT leg, so a consumer trying to recover mtimes after a matrix run found one leg's files at most --
    # measured 2026-08-07, 1 of 27 resolvable. That purge still happens; the archive is what changed.
    def _mtime(name):
        try:
            return os.path.getmtime(os.path.join(args.snapdir, name))
        except OSError:
            return None
    (window_start, ws_src) = stamp_of(a, _mtime(files[lo]))
    (window_end, we_src) = stamp_of(b, _mtime(files[hi]))

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

    # DESCCONFLICT GETS ITS FIRST READER OUTSIDE A TEST, and that gap is the reason this exists.
    #
    # `Telemetry` raises descConflict when two call sites register one key with DIFFERENT descriptors,
    # and typeConflict when they disagree about the metric's TYPE. Five comments in the renderer state
    # that the design rests on it -- StarRenderer_opengl.cpp:1085 calls it "the only thing co-location
    # would have to give up", :1097 says a second site "must match the descriptor exactly or
    # descConflict fires", StarBackdropPass.cpp:28 says the same. It is serialised into every snapshot
    # at StarTelemetry.cpp:510.
    #
    # Nothing read it. `grep -rn descConflict source/ scripts/` finds exactly one assertion, at
    # source/test/telemetry_test.cpp:255, on a fixture the test itself constructs. So the flag fired
    # only where a test had already arranged for it to, and a real conflict in a real run reached the
    # snapshot, was written to disk, and was read by nobody. A claim with no instrument -- in the very
    # field [#245] is populating, which is when descriptor disagreement becomes likely rather than
    # theoretical.
    #
    # MEASURED BEFORE SHIPPING: 72,900 metric readings across all 486 banked snapshots carry
    # descConflict=false and typeConflict=false, so this refuses nothing on today's corpus and the
    # banked legs re-read byte-identically. It is exercised by fixtures, which is the honest place for
    # a check whose subject has never occurred.
    violations.extend(conflict_violations(w))
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
            part_keys = [k for k, v in rows if v["role"] == "budget" and v["domain"] == dom]
            total_name = totals.get(dom)
            # THE SUM MUST BE IN ONE UNIT. Checked before it is taken, because a number produced from
            # mixed units cannot be un-produced by a warning printed after it.
            basis, assumed, declared = unit_basis(
                part_keys + ([total_name] if total_name else []), w)
            if len(declared) > 1:
                violations.append(
                    f"{owner}/{dom}: closure REFUSED -- its parts and whole declare {len(declared)} "
                    f"different units ({', '.join(declared)}), so their sum is not a quantity. "
                    f"Reconcile the declarations at the registration sites; the consumer will not "
                    f"guess a conversion.")
                print(f"\n  {owner}/{dom}: closure refused, mixed units ({', '.join(declared)})")
                continue
            parts = sum(scaled[k][0] for k in part_keys)
            if not total_name or total_name not in w:
                # LOUD, never silent. A domain carrying budget parts with no whole to close them against is
                # unclosable, and an unclosable budget that prints nothing reads exactly like a closed one --
                # the same shape as a gate that skips and reports OK (see scripts/ci/run-gates.sh).
                line, viol = no_whole_report(owner, dom, total_name, parts, denom)
                print(f"\n  {line}")
                if viol:
                    violations.append(viol)
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
    work = w.get("cpu.frame.work.us")
    if tot:
        # PREFER THE MEASURED WORK SPAN OVER THE DERIVED ONE. This block subtracted idle from total for as
        # long as the engine had no metric for the frame's work, which made the right number available to a
        # reader of this output and not to the closure oracle -- the DECLARED model still said total was the
        # whole and idle was a part of it, and that is what the oracle reads. cpu.frame.work.us is now
        # recorded at the source and is owner frame's declared Total, so the two agree.
        #
        # The subtraction stays as the fallback, because a snapshot captured before that metric existed is
        # still a valid measurement and must not become unreadable. Which one was used is PRINTED: two ways
        # of getting a number that silently swap places is how a value changes meaning without changing
        # name. idle records ONLY on frames that slept, so the fallback normalises by the FRAME count and
        # not idle's own -- the denominator trap that has already fired three times in this file.
        if work and work.get("count"):
            busy, src = work["mean"], "measured"
        else:
            idle_pf = (idle["total"] / tot["count"]) if (idle and tot["count"]) else 0.0
            busy, src = tot["mean"] - idle_pf, "derived total-idle"
        print(f"  CPU work: {busy:.0f}us/frame of a {tot['mean']:.0f}us pace "
              f"({100.0 * busy / tot['mean']:.0f}% utilised, {src}) -- WORK is the A/B metric, total is "
              f"the pacer")
        # Not "busy": busy is what the CPU actually burned, which only the out-of-process thread reader can
        # say (cpu.owner.frame.busy_ns). This is WALL time with the deliberate sleep removed, and it still
        # contains lock waits and GPU stalls. Two numbers, two questions, and one name for both would have
        # made them look like one measurement taken twice.
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
                       # assetFingerprint: WHICH CONTENT THIS LEG MEASURED (#277). Absent means the
                       # caller did not pass one -- recorded as None rather than omitted, so "the
                       # runner did not know" and "an older schema" are different shapes on the wire,
                       # the ABSENT-vs-ZERO rule this file already applies to metrics.
                       "meta": dict(meta, intervals=hi - lo, windowIndices=[lo, hi],
                                    windowStartEpoch=window_start, windowEndEpoch=window_end,
                                    windowStampSource=[ws_src, we_src],
                                    assetFingerprint=args.asset_fingerprint,
                                    scene=args.scene),
                       "owners": owners,
                       "metrics": w, "zeroed": sorted(zeroed), "violations": violations}, f, indent=2)
        print(f"\n  wrote {args.json}")

    if args.series:
        # Loads every file, not just the two endpoints. ~15 files at ~70KB is nothing next to what the
        # capture already cost, and reading them here means the series and the headline window come from
        # ONE traversal of one directory rather than from two tools that could be pointed at different ones.
        snaps = [load(os.path.join(args.snapdir, f)) for f in files]
        stamps = [stamp_of(s, _mtime(f)) for s, f in zip(snaps, files)]
        ivals = series(snaps, stamps)
        with open(args.series, "w") as f:
            json.dump({"label": args.label, "schema": SCHEMA, "files": files,
                       # WHICH INTERVALS THE HEADLINE NUMBER USED, carried alongside rather than applied.
                       # The series is every interval; this says which of them --json quoted, so the two
                       # artefacts can be checked against each other instead of merely coexisting.
                       "quotedWindowIndices": [lo, hi],
                       "intervals": ivals}, f, indent=2)
        named = sum(1 for iv in ivals if iv["durationSource"] == "monotonic")
        print(f"  wrote {args.series} -- {len(ivals)} interval(s), "
              f"{named} timed by the monotonic pair, {len(ivals) - named} by fallback")

    return 3 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
