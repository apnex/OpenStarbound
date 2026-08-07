#!/usr/bin/env python3
"""Join an i915 engine-busy series to a matrix run's legs, and refuse to quote what it cannot resolve.

THE DEFECT THIS EXISTS FOR. On 2026-08-07 a per-lever GPU cost table was produced by eyeballing spot
PMU samples against leg boundaries inferred from file mtimes. It looked like a result:

    off-renderVboOrphan          +4.24pp
    off-lightingTemporalDecouple +3.41pp
    off-envRefreshInterval       +2.78pp
    ...

It was not one. The baseline's OWN spread across its three repeats was 12.30 / 8.16 / 9.46 -- 4.13pp,
larger than most of the deltas. Nothing in the tooling objected, because nothing was checking. The
table was withdrawn by hand. This script exists so the next one is withdrawn by a program.

TWO RULES, both learned the same day:

  1. THE FLOOR IS MEASURED IN THIS RUN, NEVER BORROWED. An idle desktop read 0.213% at one moment and
     5.1% twenty minutes later, because "idle" depends on what else is on screen. A floor carried over
     from another run is a number about a different afternoon. The matrix declares a NULL-CONTROL
     lever -- one that cannot touch the GPU -- and its delta IS the floor for that run.
  2. A DELTA BELOW THE FLOOR IS NOT A SMALL EFFECT, IT IS NO MEASUREMENT. It is reported as
     UNRESOLVED, not as a number with a caveat, because a number with a caveat gets quoted without it.

Alignment is exact rather than inferred: every leg profile carries meta.windowStartEpoch and
meta.windowEndEpoch, stamped by telemetry-window.py from the mtimes of the two snapshots the window is
actually differenced over. Inferring boundaries from leg-JSON mtimes minus the nominal duration --
what was done by hand -- resolved 1 leg of 27 once the snapshots had been purged.

Usage:
    pmu-join.py <matrix-run-dir> <pmu.tsv> [--null-control <leverName>]
    pmu-join.py --selftest
Exit: 0 ok  1 nothing resolvable  2 usage
"""
import argparse
import collections
import glob
import json
import os
import statistics as st
import sys

RENDER_ENGINE = "rcs0-busy"
LEVER_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lever-table.json")


def declared_null_control():
    """The lever the TABLE declares cannot touch the GPU -- one source of truth, not a constant here.

    Hardcoding the name would make this script the second place that knows which lever is the control,
    and the two would drift the moment the table changed. Returns None if the table declares none, and
    the report then says so rather than inventing a floor.
    """
    try:
        table = json.load(open(LEVER_TABLE))
    except (OSError, ValueError):
        return None
    for lever in table.get("levers", []):
        if lever.get("gpuNullControl"):
            return lever.get("key")
    return None

EXIT_OK, EXIT_NOTHING, EXIT_USAGE = 0, 1, 2


def read_series(path):
    """(epoch, engine, pct) rows. UNAVAILABLE rows are DROPPED, never read as zero."""
    out, unavailable = [], 0
    with open(path) as fh:
        for line in fh.read().splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) != 4:
                continue
            if parts[3] == "UNAVAILABLE":
                unavailable += 1
                continue
            try:
                out.append((float(parts[0]), parts[2], float(parts[3])))
            except ValueError:
                continue
    return out, unavailable


def read_legs(run_dir):
    """{lever: {repeat: (start, end)}} from each leg profile's stamped window."""
    legs, unstamped = collections.defaultdict(dict), []
    for path in sorted(glob.glob(os.path.join(run_dir, "*.json"))):
        if os.path.basename(path) == "manifest.json":
            continue
        try:
            j = json.load(open(path))
        except (OSError, ValueError):
            continue
        meta = j.get("meta", {})
        s, e = meta.get("windowStartEpoch"), meta.get("windowEndEpoch")
        name = os.path.basename(path)[:-len(".json")]
        # <runid>-<repeat>-<lever>; the run id itself contains dashes, so split from the RIGHT of the
        # repeat marker rather than the left of the name.
        tail = name.split("-r", 1)[-1] if "-r" in name else name
        repeat, _, lever = tail.partition("-")
        lever = lever or "baseline"
        if s is None or e is None:
            unstamped.append(name)
            continue
        legs[lever][f"r{repeat}"] = (s, e)
    return legs, unstamped


def per_leg(series, legs, engine=RENDER_ENGINE):
    out = collections.defaultdict(dict)
    for lever, reps in legs.items():
        for rep, (s, e) in reps.items():
            v = [pct for (t, eng, pct) in series if eng == engine and s <= t <= e]
            if v:
                out[lever][rep] = (st.mean(v), len(v))
    return out


def report(joined, null_control):
    if not joined:
        print("pmu-join: FAIL -- no leg window contained a single sample. Either the series and the "
              "run are from different sessions, or the sampler was not running.")
        return EXIT_NOTHING

    base = joined.get("baseline", {})
    if not base:
        print("pmu-join: FAIL -- no baseline leg resolved; every delta would be against nothing.")
        return EXIT_NOTHING
    base_means = [m for m, _ in base.values()]
    base_mean = st.mean(base_means)
    base_spread = max(base_means) - min(base_means)

    ctrl = joined.get(f"off-{null_control}") or joined.get(null_control)
    if ctrl:
        ctrl_delta = abs(st.mean([m for m, _ in ctrl.values()]) - base_mean)
        floor, floor_why = max(ctrl_delta, base_spread), "max(null-control delta, baseline spread)"
    else:
        ctrl_delta = None
        floor, floor_why = base_spread, "baseline spread only -- NO NULL CONTROL IN THIS RUN"

    print(f"  engine {RENDER_ENGINE}, {sum(len(r) for r in joined.values())} leg-windows resolved\n")
    print(f"  {'leg':34s} {'r1':>7} {'r2':>7} {'r3':>7} {'n':>4}  {'mean':>7} {'delta':>8}")
    for lever in sorted(joined):
        reps = joined[lever]
        cells = " ".join(f"{reps[f'r{i}'][0]:7.2f}" if f"r{i}" in reps else "      -" for i in (1, 2, 3))
        means = [m for m, _ in reps.values()]
        n = sum(c for _, c in reps.values())
        d = "" if lever == "baseline" else f"{st.mean(means) - base_mean:+8.2f}"
        print(f"  {lever:34s} {cells} {n:4d}  {st.mean(means):7.2f} {d}")

    print(f"\n  baseline {base_mean:.2f}%, spread across repeats {base_spread:.2f}pp")
    if ctrl_delta is not None:
        print(f"  null control `{null_control}` (cannot touch the GPU) reads {ctrl_delta:+.2f}pp")
    print(f"  RESOLUTION FLOOR {floor:.2f}pp -- {floor_why}")

    resolved, unresolved = [], []
    for lever in sorted(joined):
        if lever == "baseline":
            continue
        d = st.mean([m for m, _ in joined[lever].values()]) - base_mean
        (resolved if abs(d) > floor else unresolved).append((lever, d))

    print(f"\n  RESOLVED -- delta clears the floor ({len(resolved)}):")
    for lever, d in sorted(resolved, key=lambda x: -abs(x[1])):
        print(f"       {lever:34s} {d:+7.2f} pp")
    if not resolved:
        print("       (none)")
    print(f"\n  UNRESOLVED -- delta is inside the floor, so this run did not measure it ({len(unresolved)}):")
    for lever, d in sorted(unresolved, key=lambda x: -abs(x[1])):
        print(f"       {lever:34s} {d:+7.2f} pp")
    if unresolved:
        print("\n  An unresolved lever is NOT a lever that costs nothing. It is a lever this run could")
        print("  not separate from its own noise -- sample longer, or at a scene where it engages.")
    return EXIT_OK


def selftest():
    fails = []

    # A synthetic run: baseline ~10% with a 4pp spread, one lever clearly above it, one inside it,
    # and a null control that is pure noise. The floor must swallow the small one and pass the big one.
    legs = {"baseline": {"r1": (0, 10), "r2": (100, 110), "r3": (200, 210)},
            "off-big": {"r1": (10, 20), "r2": (110, 120), "r3": (210, 220)},
            "off-small": {"r1": (20, 30), "r2": (120, 130), "r3": (220, 230)},
            "off-nullctl": {"r1": (30, 40), "r2": (130, 140), "r3": (230, 240)}}
    series = []
    for t, v in ((5, 12.0), (105, 8.0), (205, 10.0),          # baseline: spread 4.0pp
                 (15, 30.0), (115, 30.0), (215, 30.0),        # big: +20pp, clears
                 (25, 11.0), (125, 11.0), (225, 11.0),        # small: +1pp, inside the floor
                 (35, 10.5), (135, 9.5), (235, 10.0)):        # null control: ~0
        series.append((t, RENDER_ENGINE, v))
    joined = per_leg(series, legs)
    if set(joined) != {"baseline", "off-big", "off-small", "off-nullctl"}:
        fails.append("the join lost or invented a leg")

    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        report(joined, "nullctl")
    out = buf.getvalue()
    big_line = [l for l in out.splitlines() if "off-big" in l and "pp" in l]
    if "RESOLVED" not in out or not any("off-big" in l for l in out.split("UNRESOLVED")[0].splitlines()):
        fails.append("a +20pp lever did not clear a 4pp floor")
    if not any("off-small" in l for l in out.split("UNRESOLVED")[1].splitlines()):
        fails.append("a +1pp lever inside a 4pp floor was reported as resolved")

    # An empty join must FAIL, not print an empty-but-cheerful table. A run with legs but no baseline
    # must fail too -- every delta would be against nothing. Both arms are run with stdout captured:
    # a selftest that prints FAIL lines while PASSING is how you learn to skim past FAIL lines.
    with contextlib.redirect_stdout(io.StringIO()):
        empty_rc = report({}, "nullctl")
        nobase_rc = report({"off-x": {"r1": (1.0, 1)}}, "nullctl")
    if empty_rc != EXIT_NOTHING:
        fails.append("an empty join did not fail")
    if nobase_rc != EXIT_NOTHING:
        fails.append("a run with no baseline did not fail")

    # UNAVAILABLE must never enter the arithmetic as a zero.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "s.tsv")
        open(p, "w").write("epoch\tutc\tengine\tbusy_pct\n"
                           "1.0\t00:00:01\trcs0-busy\tUNAVAILABLE\n"
                           "2.0\t00:00:02\trcs0-busy\t7.5\n")
        rows, unavail = read_series(p)
        if unavail != 1 or len(rows) != 1 or rows[0][2] != 7.5:
            fails.append("an UNAVAILABLE sample was not dropped from the series")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  pmu_join selftest: 5/5 arms ok (clears the floor, is swallowed by it, empty and "
          "baseline-less runs fail, UNAVAILABLE is not zero)")
    return EXIT_OK


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run_dir", nargs="?")
    ap.add_argument("series", nargs="?")
    ap.add_argument("--null-control", default=None,
                    help="override the lever the table declares with gpuNullControl")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not args.run_dir or not args.series:
        print("pmu-join: need <matrix-run-dir> <pmu.tsv>", file=sys.stderr)
        return EXIT_USAGE

    null_control = args.null_control or declared_null_control()
    series, unavailable = read_series(args.series)
    legs, unstamped = read_legs(args.run_dir)
    print(f"=== pmu-join: {os.path.basename(args.run_dir.rstrip('/'))} x {os.path.basename(args.series)}")
    print(f"  {len(series)} usable samples ({unavailable} UNAVAILABLE, dropped not zeroed), "
          f"{sum(len(r) for r in legs.values())} stamped legs")
    if unstamped:
        # LOUD. A leg with no stamped window is a leg this join silently omitted, and an omitted leg
        # reads exactly like a lever with no effect.
        print(f"  !! {len(unstamped)} leg(s) carry no windowStartEpoch and were EXCLUDED: "
              f"{', '.join(unstamped[:4])}{' ...' if len(unstamped) > 4 else ''}")
        print(f"     Those profiles predate the stamp; re-run, or join them by nothing at all.")
    if null_control:
        print(f"  null control declared by the lever table: {null_control}")
    else:
        print("  !! NO NULL CONTROL DECLARED -- no lever in scripts/lever-table.json carries "
              "gpuNullControl, so the floor falls back to the baseline's own spread alone.")
    return report(per_leg(series, legs), null_control)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
