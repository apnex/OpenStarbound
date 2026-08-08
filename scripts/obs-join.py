#!/usr/bin/env python3
"""Put the in-process telemetry and both sovereign readers on ONE epoch axis, per leg, per interval.

THE NORTH STAR'S CENTRAL CLAIM, and until this script it did not exist. Director, 2026-08-07: "both
GPU and CPU metrics, correctly attributed, as a stream of time series data that can be directly
plotted and compared against every single lever." Three artefacts held three quarters of that and
never met:

    IN-PROCESS   <label>.series.json    per-interval, 145 keys, stamped tStartEpoch/tEndEpoch
    SOVEREIGN    metrics.tsv            per-owner CPU + per-client GPU, cumulative counters
    PMU          pmu.tsv                device-wide engine busy, a rate per sample

This joins them into one file per leg. Epoch is the only clock all three share, which is why every
producer stamps it and why the join is exact rather than inferred.

TWO SOURCES, TWO METHODS, AND THE ARTEFACT SAYS WHICH. This is the honesty that makes the file
readable a month later:

  * SOVEREIGN COUNTERS ARE RE-DIFFERENCED. They are cumulative, so the busy time over an interval is
    the sum of consecutive-sample deltas inside it -- the exact quantity, with no averaging anywhere.
  * THE PMU IS A MEAN OF RATES. Each pmu.tsv row is already a ratio over its own sub-window, and a
    rate cannot be re-windowed, so the interval's figure is the mean of the samples that fall in it.
    Strictly weaker than the line above, and labelled so nobody quotes them as the same kind of number.

THE DENOMINATOR IS THE SPAN ACTUALLY COVERED, NEVER THE NOMINAL INTERVAL. Samples do not line up with
telemetry snapshot boundaries, so the first usable sample sits after the interval opens and the last
before it closes. Dividing busy time by the interval's declared duration would report a rate that is
too low by exactly the uncovered fraction -- silently, and worse at the edges. `coveredS` is the
denominator and is carried in the output beside `durationS` so the shortfall is visible.

THREE WAYS A COUNTER STOPS BEING DIFFERENCEABLE, all of them real here:
  * THE PID CHANGED. Each leg runs a new client and counters reset with it. Never differenced across.
  * THE COUNTER FELL. cpu.owner.*.busy_ns_total sums an owner's LIVE threads, so a thread exiting
    makes it drop. That sub-interval is DISCARDED, not counted as negative work, and the discard is
    reported -- cpu.process.busy_ns_total beside it retains exited threads and is what makes the loss
    a quantity rather than a silence.
  * FEWER THAN TWO SAMPLES. One sample cannot be differenced, so the interval reports UNAVAILABLE.
    Reporting 0 would be indistinguishable from an idle owner, which is the defect this whole
    component exists to end.

Usage:
    obs-join.py <label.series.json | run-dir> --sovereign metrics.tsv [--pmu pmu.tsv]
    obs-join.py --selftest
Writes <label>.joined.json beside each series file. Exit: 0 ok, 1 nothing joined, 2 usage.
"""
import argparse
import collections
import glob
import json
import os
import statistics as st
import sys

EXIT_OK, EXIT_NOTHING, EXIT_USAGE = 0, 1, 2

UNAVAILABLE = "UNAVAILABLE"
COUNTER_SUFFIX = "_total"

# How the two sovereign counter families are turned into a number on the shared axis. Both are
# busy-ns over covered-ns, and both are dimensionless -- which is what lets CPU and GPU sit on one Y
# axis at all. They are NOT the same quantity and the suffixes say so: a GPU engine ratio cannot
# exceed 1, an owner's cores can, because an owner with four busy threads reads 4.0.
DERIVED = [("gpu.engine.", ".busy_ns_total", ".busy_ratio"),
           ("cpu.owner.", ".busy_ns_total", ".busy_cores"),
           ("cpu.process.", ".busy_ns_total", ".busy_cores")]


def read_sovereign(path):
    """{key: [(epoch_s, pid, value)]} sorted, plus the count of UNAVAILABLE rows DROPPED not zeroed."""
    by_key, unavailable = collections.defaultdict(list), 0
    with open(path) as fh:
        for line in fh.read().splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) != 6:
                continue
            epoch_ns, _mono, pid, key, value, _detail = parts
            if value == UNAVAILABLE:
                unavailable += 1
                continue
            try:
                by_key[key].append((int(epoch_ns) / 1e9, int(pid), float(value)))
            except ValueError:
                continue
    for rows in by_key.values():
        rows.sort()
    return by_key, unavailable


def read_pmu(path):
    """{engine: [(epoch_s, pct)]}, UNAVAILABLE dropped. Same rule pmu-join.py keeps, same reason."""
    by_engine, unavailable = collections.defaultdict(list), 0
    with open(path) as fh:
        for line in fh.read().splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) != 4:
                continue
            if parts[3] == UNAVAILABLE:
                unavailable += 1
                continue
            try:
                by_engine[parts[2]].append((float(parts[0]), float(parts[3])))
            except ValueError:
                continue
    for rows in by_engine.values():
        rows.sort()
    return by_engine, unavailable


def rediff(rows, t0, t1):
    """Busy time over [t0, t1] by summing consecutive deltas. None if it cannot be differenced.

    Returns (busyNs, coveredS, samples, resets, pidChanges). `coveredS` is the span the accepted pairs
    actually spanned -- the denominator any rate must use, because the sample grid does not align with
    the interval's edges and a discarded pair removes its span from the numerator too.
    """
    inside = [r for r in rows if t0 <= r[0] <= t1]
    if len(inside) < 2:
        return None
    busy_ns = covered = 0.0
    resets = pid_changes = 0
    for (ta, pa, va), (tb, pb, vb) in zip(inside, inside[1:]):
        if pa != pb:
            pid_changes += 1
            continue
        if vb < va:
            resets += 1
            continue
        busy_ns += vb - va
        covered += tb - ta
    if covered <= 0:
        return None
    return busy_ns, covered, len(inside), resets, pid_changes


def gauge(rows, t0, t1):
    """A gauge is a level, not a counter: report the last reading inside the interval, never a sum."""
    inside = [r for r in rows if t0 <= r[0] <= t1]
    if not inside:
        return None
    return {"last": inside[-1][2], "samples": len(inside)}


def sovereign_for(by_key, t0, t1):
    """Every sovereign key resolved over one interval, with its method and its own denominator."""
    out = {"method": "counter re-differenced", "keys": {}, "unresolved": []}
    for key, rows in sorted(by_key.items()):
        if not key.endswith(COUNTER_SUFFIX):
            g = gauge(rows, t0, t1)
            if g is not None:
                out["keys"][key] = g
            continue
        d = rediff(rows, t0, t1)
        if d is None:
            # NAMED, NOT OMITTED. A key missing from the output reads as a metric that does not exist;
            # a key present and unresolved reads as one this interval could not measure.
            out["unresolved"].append(key)
            continue
        busy_ns, covered, samples, resets, pid_changes = d
        entry = {"busyNs": busy_ns, "coveredS": covered, "samples": samples}
        if resets:
            entry["resetsDiscarded"] = resets
        if pid_changes:
            entry["pidChangesDiscarded"] = pid_changes
        out["keys"][key] = entry
        for prefix, suffix, derived in DERIVED:
            if key.startswith(prefix) and key.endswith(suffix):
                out["keys"][key[:-len(suffix)] + derived] = busy_ns / (covered * 1e9)
                break
    out["available"] = bool(out["keys"])
    return out


def pmu_for(by_engine, t0, t1):
    out = {"method": "mean of sub-window rates", "keys": {}}
    for engine, rows in sorted(by_engine.items()):
        inside = [pct for (t, pct) in rows if t0 <= t <= t1]
        if inside:
            out["keys"][f"{engine}.busy_pct"] = st.mean(inside)
            out["keys"][f"{engine}.samples"] = len(inside)
    out["available"] = bool(out["keys"])
    return out


def in_process_axis(metrics, duration_s):
    """The engine's own view of its main thread, as a fraction of wall -- the fourth cell.

    NOT the same quantity as cpu.owner.frame.busy_cores beside it, and the name has to say so.
    cpu.frame.work.us is WALL time elapsed while the frame was not sleeping, so it includes time the
    thread spent blocked or descheduled; the sovereign figure is CPU time actually consumed. The
    relation that should hold is work_wall_fraction >= owner frame busy_cores, and having both on one
    axis is what makes that checkable at all -- two instruments, one thread, different mechanisms.
    """
    m = metrics.get("cpu.frame.work.us")
    if not isinstance(m, dict) or not duration_s:
        return {}
    total = m.get("total")
    if not isinstance(total, (int, float)):
        return {}
    return {"cpu.frame.work.wall_fraction": (total / 1e6) / duration_s}


def join_leg(series, by_key, sov_unavailable, by_engine, pmu_unavailable, sources):
    intervals, resolved = [], 0
    for iv in series.get("intervals", []):
        t0, t1 = iv.get("tStartEpoch"), iv.get("tEndEpoch")
        if t0 is None or t1 is None:
            # A leg profile predating the epoch stamps joins against nothing. Excluded LOUDLY: an
            # omitted interval reads exactly like one in which nothing happened.
            intervals.append({"index": iv.get("index"), "unjoinable": "interval carries no epoch stamps"})
            continue
        sov = sovereign_for(by_key, t0, t1) if by_key else {
            "available": False, "reason": "no sovereign series supplied"}
        pmu = pmu_for(by_engine, t0, t1) if by_engine else {
            "available": False, "reason": "no PMU series supplied"}

        axis = in_process_axis(iv.get("metrics", {}), iv.get("durationS"))
        for key, value in sov.get("keys", {}).items():
            if isinstance(value, float) and (key.endswith(".busy_ratio") or key.endswith(".busy_cores")):
                axis[key] = value
        for key, value in pmu.get("keys", {}).items():
            if key.endswith(".busy_pct"):
                # The PMU's engine vocabulary (rcs0) and the DRM reader's (render) name the same
                # hardware differently. Kept apart under an explicit suffix rather than reconciled:
                # merging two vocabularies on the assumption they mean the same thing is how a number
                # comes to mean something other than its name.
                axis[f"gpu.engine.{key[:-len('.busy_pct')]}.busy_ratio.pmu"] = value / 100.0

        resolved += 1 if sov.get("available") or pmu.get("available") else 0
        intervals.append({
            "index": iv.get("index"),
            "tStartEpoch": t0, "tEndEpoch": t1,
            "durationS": iv.get("durationS"), "durationSource": iv.get("durationSource"),
            "axis": axis,
            "sovereign": sov,
            "pmu": pmu,
            "inProcess": iv.get("metrics", {}),
        })

    return {
        "label": series.get("label"),
        "schema": series.get("schema"),
        "sources": sources,
        "dropped": {"sovereignUnavailableRows": sov_unavailable, "pmuUnavailableRows": pmu_unavailable},
        "quotedWindowIndices": series.get("quotedWindowIndices"),
        "intervalsJoined": resolved,
        "intervals": intervals,
    }, resolved


def summarise(joined):
    """One readable block per leg. A JSON file nobody opens is not a result anyone acts on."""
    label = joined["label"]
    ivs = [iv for iv in joined["intervals"] if "axis" in iv]
    print(f"=== {label}: {joined['intervalsJoined']} of {len(joined['intervals'])} intervals joined")
    if not ivs:
        print("      nothing resolved -- the series and the run are from different sessions, or the "
              "sampler was not running")
        return
    keys = sorted({k for iv in ivs for k in iv["axis"]})
    for key in keys:
        vals = [iv["axis"][key] for iv in ivs if key in iv["axis"]]
        print(f"  {key:46s} mean {st.mean(vals):7.4f}  min {min(vals):7.4f}  max {max(vals):7.4f}  "
              f"n {len(vals):3d}")
    thin = [iv["index"] for iv in ivs
            if iv["sovereign"].get("unresolved") or not iv["sovereign"].get("available")]
    if thin:
        print(f"  !! {len(thin)} interval(s) with an unresolved sovereign key: {thin[:8]}"
              f"{' ...' if len(thin) > 8 else ''}")
        print("     An unresolved key is NOT a zero. Sample faster than the telemetry cadence, or "
              "widen the interval.")


def selftest():
    import io, contextlib, tempfile
    fails = []

    def sov_rows(rows):
        """rows: (epoch_s, pid, key, value). Written and re-read, so the parser is in the loop."""
        d = tempfile.mkdtemp()
        p = os.path.join(d, "s.tsv")
        with open(p, "w") as fh:
            fh.write("epoch_ns\tmonotonic_ns\tpid\tkey\tvalue\tdetail\n")
            for t, pid, key, value in rows:
                fh.write(f"{int(t * 1e9)}\t0\t{pid}\t{key}\t{value}\t-\n")
        return read_sovereign(p)

    K = "cpu.owner.frame.busy_ns_total"

    # 1. RE-DIFFERENCED, NOT AVERAGED. A counter climbing 1e9 ns per second over a 4-second interval
    #    is exactly 1.0 busy cores, and must come out that way from samples of uneven spacing --
    #    which a mean of per-sample rates would only reach by luck.
    by_key, _ = sov_rows([(100.0, 7, K, 0), (100.5, 7, K, 0.5e9),
                          (102.0, 7, K, 2.0e9), (104.0, 7, K, 4.0e9)])
    got = sovereign_for(by_key, 100.0, 104.0)
    if abs(got["keys"]["cpu.owner.frame.busy_cores"] - 1.0) > 1e-9:
        fails.append(f"a 1.0-core counter re-differenced to {got['keys']['cpu.owner.frame.busy_cores']}")
    if abs(got["keys"][K]["coveredS"] - 4.0) > 1e-9:
        fails.append("the covered span is not the span the accepted pairs actually covered")

    # 2. THE DENOMINATOR IS THE COVERED SPAN, NOT THE NOMINAL INTERVAL. Same counter, but the samples
    #    only cover the middle 2s of a 10s interval. The answer is still 1.0 cores; dividing by 10
    #    would report 0.2 and look like a quiet lever.
    by_key, _ = sov_rows([(105.0, 7, K, 5.0e9), (107.0, 7, K, 7.0e9)])
    got = sovereign_for(by_key, 100.0, 110.0)
    if abs(got["keys"]["cpu.owner.frame.busy_cores"] - 1.0) > 1e-9:
        fails.append("the rate was divided by the nominal interval rather than the covered span")

    # 3. A COUNTER THAT FALLS IS A RESET, NOT NEGATIVE WORK. An owner's live-thread sum drops when a
    #    thread exits. That pair is discarded and SAID SO; summing it as a negative would report the
    #    owner as having given work back.
    by_key, _ = sov_rows([(100.0, 7, K, 0), (101.0, 7, K, 1.0e9),
                          (102.0, 7, K, 0.2e9), (103.0, 7, K, 1.2e9)])
    got = sovereign_for(by_key, 100.0, 103.0)
    if got["keys"][K]["busyNs"] != 2.0e9:
        fails.append(f"a falling counter did not discard its pair (busyNs {got['keys'][K]['busyNs']})")
    if got["keys"][K].get("resetsDiscarded") != 1:
        fails.append("a falling counter was discarded silently")
    if abs(got["keys"]["cpu.owner.frame.busy_cores"] - 1.0) > 1e-9:
        fails.append("the discarded pair's span was left in the denominator")

    # 4. NEVER DIFFERENCED ACROSS A PID CHANGE. Each leg runs a new client and the counters reset with
    #    it; the difference across that boundary is a number about nothing, and it is a LARGE number.
    by_key, _ = sov_rows([(100.0, 7, K, 9.0e9), (101.0, 8, K, 0.0),
                          (102.0, 8, K, 1.0e9)])
    got = sovereign_for(by_key, 100.0, 102.0)
    if got["keys"][K]["busyNs"] != 1.0e9:
        fails.append("a pid change was differenced across")
    if got["keys"][K].get("pidChangesDiscarded") != 1:
        fails.append("a pid change was discarded silently")

    # 5. ONE SAMPLE IS NOT A MEASUREMENT. An interval holding a single sample cannot be differenced,
    #    and must report the key as UNRESOLVED rather than 0 -- which would be indistinguishable from
    #    an idle owner, the exact absent-vs-zero defect this component exists to end.
    by_key, _ = sov_rows([(100.0, 7, K, 5.0e9)])
    got = sovereign_for(by_key, 99.0, 101.0)
    if K in got["keys"]:
        fails.append("a single sample was reported as a measurement")
    if K not in got["unresolved"]:
        fails.append("an unresolvable key was omitted rather than named as unresolved")

    # 6. UNAVAILABLE NEVER ENTERS THE ARITHMETIC AS A ZERO, in either reader's series.
    d = tempfile.mkdtemp()
    p = os.path.join(d, "u.tsv")
    open(p, "w").write("epoch_ns\tmonotonic_ns\tpid\tkey\tvalue\tdetail\n"
                       f"100000000000\t0\t7\t{K}\t{UNAVAILABLE}\tgone\n"
                       f"101000000000\t0\t7\t{K}\t1000000000\t-\n"
                       f"102000000000\t0\t7\t{K}\t2000000000\t-\n")
    by_key, unavail = read_sovereign(p)
    if unavail != 1 or len(by_key[K]) != 2:
        fails.append("an UNAVAILABLE sovereign row was not dropped from the series")
    pp = os.path.join(d, "p.tsv")
    open(pp, "w").write("epoch\tutc\tengine\tbusy_pct\n"
                        "100.0\t00:00\trcs0-busy\tUNAVAILABLE\n"
                        "101.0\t00:00\trcs0-busy\t8.0\n")
    by_engine, punavail = read_pmu(pp)
    if punavail != 1 or by_engine["rcs0-busy"] != [(101.0, 8.0)]:
        fails.append("an UNAVAILABLE PMU row was not dropped from the series")

    # 7. THE TWO METHODS ARE LABELLED, AND DIFFERENTLY. A reader who cannot tell a re-differenced
    #    counter from a mean of rates will quote them as the same kind of number, and one of them
    #    survives re-windowing while the other does not.
    by_key, _ = sov_rows([(100.0, 7, K, 0), (102.0, 7, K, 2.0e9)])
    by_engine, _ = read_pmu(pp)
    series = {"label": "t", "intervals": [{"index": 0, "tStartEpoch": 99.0, "tEndEpoch": 103.0,
                                           "durationS": 4.0, "metrics": {}}]}
    joined, resolved = join_leg(series, by_key, 0, by_engine, 0, {})
    iv = joined["intervals"][0]
    if iv["sovereign"]["method"] == iv["pmu"]["method"]:
        fails.append("the two sources claim the same method")
    if "re-differenced" not in iv["sovereign"]["method"] or "mean" not in iv["pmu"]["method"]:
        fails.append("a source's method is not named for what it actually did")
    if resolved != 1:
        fails.append("an interval with both sources present did not count as resolved")

    # 8. THE TWO GPU VOCABULARIES ARE NOT MERGED. The PMU says rcs0, the DRM reader says render, and
    #    they name the same hardware; putting the PMU figure under the DRM name would be a claim that
    #    two instruments measured the same quantity, which is exactly what metrics-mutual-check
    #    exists to TEST rather than assume.
    if "gpu.engine.rcs0-busy.busy_ratio.pmu" not in iv["axis"]:
        fails.append("the PMU figure did not reach the axis under its own vocabulary")
    if abs(iv["axis"]["cpu.owner.frame.busy_cores"] - 1.0) > 1e-9:
        fails.append("the sovereign figure did not reach the shared axis")

    # 9. AN INTERVAL WITH NO EPOCH STAMPS IS UNJOINABLE AND SAYS SO. Profiles predating the stamps
    #    join against nothing, and an omitted interval reads exactly like one in which nothing
    #    happened -- the failure mode that resolved 1 leg of 27 by hand on 2026-08-07.
    unstamped = {"label": "t", "intervals": [{"index": 0, "durationS": 4.0, "metrics": {}}]}
    j2, r2 = join_leg(unstamped, by_key, 0, by_engine, 0, {})
    if len(j2["intervals"]) != 1:
        fails.append("an interval with no epoch stamps vanished from the output entirely")
    elif r2 != 0 or "unjoinable" not in j2["intervals"][0]:
        fails.append("an interval with no epoch stamps was joined anyway, or dropped without a word")

    # 10. AN EMPTY JOIN MUST NOT PRINT A CHEERFUL SUMMARY. Captured, because a selftest that prints
    #     its own FAIL-shaped lines while passing teaches its reader to skim past them.
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        summarise(j2)
    if "nothing resolved" not in buf.getvalue():
        fails.append("a join that resolved nothing did not say so")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  obs_join selftest: 10/10 arms ok (re-differenced not averaged, the denominator is the "
          "covered span, resets and pid changes are discarded and reported, one sample is not a "
          "measurement, UNAVAILABLE is not zero, the two methods and the two GPU vocabularies stay "
          "apart, an unstamped interval says so, an empty join says so)")
    return EXIT_OK


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("target", nargs="?", help="a <label>.series.json, or a directory holding several")
    ap.add_argument("--sovereign", help="TSV from scripts/metrics-sample.py")
    ap.add_argument("--pmu", help="TSV from scripts/pmu-engine-sample.py")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.target:
        print("obs-join: need a series file or a run directory", file=sys.stderr)
        return EXIT_USAGE
    if not args.sovereign and not args.pmu:
        # A join of one source against nothing is a copy with extra steps, and would write an
        # artefact whose name promises three sources.
        print("obs-join: need at least one of --sovereign / --pmu; joining nothing is not a join",
              file=sys.stderr)
        return EXIT_USAGE

    by_key, sov_unavailable = (read_sovereign(args.sovereign) if args.sovereign
                               else ({}, 0))
    by_engine, pmu_unavailable = (read_pmu(args.pmu) if args.pmu else ({}, 0))
    sources = {"sovereign": args.sovereign, "pmu": args.pmu}

    targets = (sorted(glob.glob(os.path.join(args.target, "*.series.json")))
               if os.path.isdir(args.target) else [args.target])
    if not targets:
        print(f"obs-join: no *.series.json under {args.target}", file=sys.stderr)
        return EXIT_NOTHING

    total = 0
    for path in targets:
        series = json.load(open(path))
        sources_here = dict(sources, series=path)
        joined, resolved = join_leg(series, by_key, sov_unavailable, by_engine, pmu_unavailable,
                                    sources_here)
        out = path[:-len(".series.json")] + ".joined.json"
        with open(out, "w") as fh:
            json.dump(joined, fh, indent=2)
        summarise(joined)
        print(f"  wrote {out}\n")
        total += resolved

    if not total:
        print("obs-join: FAIL -- not one interval resolved against either sovereign source. Either "
              "the series and the samples are from different sessions, or no sampler was running.")
        return EXIT_NOTHING
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
