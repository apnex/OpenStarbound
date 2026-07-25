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

SCHEMA = 2
BUCKETS = 64
# Tolerated skew between a timer's histogram sum and its count. The engine has four sampling threads at most
# (main, server, lighting, and the GL readback path), each able to be mid-record() at either snapshot endpoint;
# 16 is that with generous headroom, and still orders of magnitude below any real sample loss.
HIST_SKEW_SLACK = 16


def load(path):
    with open(path) as f:
        return json.load(f)


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


def window(a, b):
    """Delta between two snapshots, carrying each metric's descriptor forward."""
    out = {}
    for name, mb in b.get("metrics", {}).items():
        ma = a.get("metrics", {}).get(name, {})
        d = {k: mb.get(k) for k in ("type", "domain", "owner", "cadence", "role")}
        if mb.get("type") == "timer":
            dc = mb.get("count", 0) - ma.get("count", 0)
            if dc <= 0:
                continue
            ba, bb = ma.get("buckets", []), mb.get("buckets", [])
            ba = ba + [0] * (BUCKETS - len(ba))
            bb = bb + [0] * (BUCKETS - len(bb))
            d.update(count=dc,
                     total=mb.get("total", 0) - ma.get("total", 0),
                     buckets=[y - x for x, y in zip(ba, bb)])
            d["mean"] = d["total"] / dc
        elif mb.get("type") in ("counter", "gauge", "rate"):
            va, vb = ma.get("value", 0), mb.get("value", 0)
            # A gauge is a level, not an accumulation: its delta is meaningless, so carry the latest reading.
            d["value"] = vb if mb.get("type") == "gauge" else vb - va
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("snapdir")
    ap.add_argument("--label", default="profile")
    ap.add_argument("--first", type=int, default=None)
    ap.add_argument("--last", type=int, default=None)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    files = sorted(f for f in os.listdir(args.snapdir) if f.endswith(".json"))
    if len(files) < 2:
        print(f"need >=2 snapshots in {args.snapdir}, found {len(files)}", file=sys.stderr)
        return 1

    # Trim the ends when affordable: the first snapshot sits closest to load, the last may be a partial
    # interval cut short by the kill.
    lo = args.first if args.first is not None else (1 if len(files) >= 4 else 0)
    hi = args.last if args.last is not None else (len(files) - 2 if len(files) >= 4 else len(files) - 1)
    a, b = load(os.path.join(args.snapdir, files[lo])), load(os.path.join(args.snapdir, files[hi]))

    schema = b.get("meta", {}).get("schema", 0)
    if schema != SCHEMA:
        # Refuse rather than mis-window. A pre-v2 snapshot has no descriptors, and guessing them is how the
        # 119% error happened in the first place.
        print(f"snapshot schema {schema}, expected {SCHEMA} -- rebuild and re-capture", file=sys.stderr)
        return 2

    owners, w = b.get("owners", {}), window(a, b)
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
    for owner in sorted({m["owner"] for m in w.values() if m.get("owner") not in (None, "unknown")}):
        spec = owners.get(owner, {})
        denom_name, total_name = spec.get("denominator"), spec.get("total")
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

        for k, v in sorted(rows, key=lambda kv: -kv[1]["total"]):
            per_tick = v["total"] / denom
            cover = 100.0 * v["count"] / denom
            p50, p50sat = percentile(v["buckets"], 0.50)
            p99, p99sat = percentile(v["buckets"], 0.99)
            # A saturated percentile landed in the unbounded top bucket, so the figure is a LOWER BOUND. Render
            # it as ">=" rather than as a number: a hitch past 65 ms printed as a plain value reads as merely
            # bad instead of unbounded, and that is exactly the case the histogram was added to expose.
            print(f"  {k:<40} {v['domain']:>4} {v['role']:>7} {v['count']:>8} {cover:>5.0f}% "
                  f"{per_tick:>9.1f} {('>=' if p50sat else '') + f'{p50:.1f}':>8} "
                  f"{('>=' if p99sat else '') + f'{p99:.1f}':>9}")
            # ASSERTION 2 (cadence bound): under is legitimate -- a gated pass or an async readback samples
            # only some ticks, which the coverage column reports. Over means the span opened twice per tick.
            if v.get("cadence") in ("frame", "tick", "recompute") and v["count"] > denom:
                violations.append(f"{k}: count {v['count']} > denominator {denom} "
                                  f"(declared cadence={v['cadence']}; should it be 'call'?)")
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

        # ASSERTION 1 (budget closure).
        if total_name and total_name in w:
            whole = w[total_name]["total"]
            for dom in sorted({v["domain"] for _, v in rows}):
                parts = sum(v["total"] for _, v in rows if v["role"] == "budget" and v["domain"] == dom)
                un = whole - parts
                pct = 100.0 * parts / whole if whole else 0.0
                print(f"\n  {dom} accounted: {parts/denom:8.1f} us/tick of {whole/denom:.1f} "
                      f"({pct:.1f}%) -- unattributed {un/denom:.1f} us/tick")
                if un < 0:
                    violations.append(f"{owner}/{dom}: parts exceed the whole by {-un} us "
                                      f"-- a Detail metric declared as Budget, or a double-counted phase")
                elif whole and un / whole > 0.25:
                    violations.append(f"{owner}/{dom}: {100*un/whole:.0f}% unattributed "
                                      f"-- the instrumentation is missing a phase")
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

    if violations:
        print("\n  !! ORACLE VIOLATIONS -- do not quote these numbers:")
        for v in violations:
            print(f"     {v}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"label": args.label, "window": [files[lo], files[hi]],
                       "meta": meta, "owners": owners, "metrics": w, "violations": violations}, f, indent=2)
        print(f"\n  wrote {args.json}")

    return 3 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
