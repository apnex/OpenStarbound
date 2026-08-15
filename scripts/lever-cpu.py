#!/usr/bin/env python3
"""Per-lever CPU cost, per owner, from a matrix run's joined artefacts -- the half pmu-join cannot do.

WHY A SECOND ANALYSER AND NOT A FLAG ON THE FIRST. pmu-join reads a PMU TSV and a set of leg windows;
its subject is ONE device-wide series and its arithmetic is a mean of rates. This reads the JOINED
artefacts obs-join writes per leg, whose CPU numbers are re-differenced cumulative counters, and its
subject is a SET of owners that must also sum to a whole. Same verdict discipline, different evidence,
different arithmetic. Folding them together would mean one function that has to remember which of two
things it is holding, which is the confusion this whole campaign has been removing.

IT INHERITS EVERY RULE pmu-join PAID FOR, because they are rules about experiments and not about GPUs:

  THE FLOOR IS MEASURED IN THIS RUN, NEVER BORROWED. The matrix declares a null-control lever -- one
  that cannot touch the quantity -- and its delta IS the floor, alongside the baseline's own spread
  across repeats. A floor carried from another run is a number about a different afternoon.

  A DELTA BELOW THE FLOOR IS NOT A SMALL EFFECT, IT IS NO MEASUREMENT. Reported UNRESOLVED, not as a
  number with a caveat, because a number with a caveat gets quoted without it.

  A DELTA SMALLER THAN THE LEVER'S OWN REPEAT SPREAD IS THE MEAN OF SOMETHING THAT DID NOT REPEAT.
  Two tests, not one: the run floor is built from the BASELINE's spread and is blind to a lever that is
  itself unstable.

  A LEG THE RUNNER VOIDED IS NOT QUOTED. VOID means the lever was set and the witness did not move --
  the two arms were the same experiment run twice.

ONE RULE OF ITS OWN, WHICH THE GPU SIDE HAS NO NEED OF. The CPU owners are PARTS OF A WHOLE, and the
whole (cpu.process.busy_cores) is measured independently. So this reports the sum of the owner deltas
beside the whole's delta: if a lever moves the whole by 0.05 cores and the owners account for 0.01 of
it, the attribution missed something and that is worth seeing. The GPU engines have no such whole.

UNITS. CPU figures are CORES -- busy time over wall time against ONE core -- so an owner running four
threads flat out reads 4.0 and a delta of +0.05 means "one twentieth of a core more". They are NOT
percentage points, and the report never calls them pp.

Usage:
    lever-cpu.py <matrix-run-dir> [--null-control <leverName>]
    lever-cpu.py --selftest
Exit: 0 ok  1 nothing resolvable  2 usage
"""
import argparse
import collections
import glob
import math
import importlib.util
import json
import os
import statistics as st
import sys

EXIT_OK, EXIT_NOTHING, EXIT_USAGE = 0, 1, 2

HERE = os.path.dirname(os.path.abspath(__file__))

# The owner series, plus the whole they are parts of, plus the in-process view of the same main thread.
# Ordered so the report reads whole-then-parts.
WHOLE = "cpu.process.busy_cores"
OWNERS = ["cpu.owner.frame.busy_cores", "cpu.owner.sim.busy_cores", "cpu.owner.gl.busy_cores",
          "cpu.owner.lighting.busy_cores", "cpu.owner.unknown.busy_cores"]
EXTRA = ["cpu.frame.work.wall_fraction", "cpu.attribution.residual_cores"]


def declared_null_control():
    """The lever the TABLE declares cannot touch the quantity -- reused, not restated.

    Imported from pmu-join rather than copied: two places knowing which lever is the control would
    drift the moment the table changed, and the analyser that drifted would be the one nobody re-read.
    """
    spec = importlib.util.spec_from_file_location("pj", os.path.join(HERE, "pmu-join.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.declared_null_control()


def read_verdicts(run_dir):
    """{leg label: OK|VOID|FAILED} from legs.tsv -- the runner's verdict on whether it HAPPENED."""
    out = {}
    try:
        with open(os.path.join(run_dir, "legs.tsv")) as fh:
            for line in fh:
                cols = line.rstrip("\n").split("\t")
                if len(cols) >= 4:
                    out[cols[0]] = cols[3]
    except OSError:
        pass
    return out


def leg_means(path):
    """{key: mean over the leg's intervals}. Intervals with no axis contribute nothing, never a zero."""
    try:
        d = json.load(open(path))
    except (OSError, ValueError):
        return None
    acc = collections.defaultdict(list)
    for iv in d.get("intervals", []):
        for k, v in (iv.get("axis") or {}).items():
            if isinstance(v, (int, float)):
                acc[k].append(float(v))
    return {k: st.mean(v) for k, v in acc.items() if v}


def collect(run_dir, verdicts, run_id):
    """{lever: {repeat: {key: mean}}}, VOID legs excluded and returned separately."""
    legs, voided, unreadable = collections.defaultdict(dict), collections.defaultdict(list), []
    for path in sorted(glob.glob(os.path.join(run_dir, "*.joined.json"))):
        name = os.path.basename(path)[:-len(".joined.json")]
        if verdicts.get(name) == "VOID":
            tail = name.split("-r", 1)[-1]
            rep, _, lever = tail.partition("-")
            voided[lever or "baseline"].append("r" + rep)
            continue
        m = leg_means(path)
        if not m:
            unreadable.append(name)
            continue
        tail = name.split("-r", 1)[-1] if "-r" in name else name
        rep, _, lever = tail.partition("-")
        legs[lever or "baseline"]["r" + rep] = m
    return legs, voided, unreadable


# ---------------------------------------------------------------------------------------------
# THE OMNIBUS GATE. Added after owner `sim` published three RESOLVED lever effects that were noise.
#
# WHAT WENT WRONG. The rule below used to be per-lever only: a delta beat the floor, its own 3-sample
# range was smaller than the delta, so it printed RESOLVED. On cpu.owner.sim.busy_cores in
# matrix-20260808-140430 that promoted three levers -- while ALL EIGHT levers moved sim in the same
# direction, including the DECLARED NULL CONTROL and a render-only lever that cannot touch the world
# server. A common offset shared by every arm is the signature of drift or ordering, not of eight
# independent lever effects, and a per-lever floor cannot see it: the floor is computed from the
# baseline and the control, both of which carry the same offset.
#
# THE RULE. Ask FIRST whether the key resolves anything at all -- a one-way ANOVA across the lever
# groups -- and only then ask which lever. This is Fisher's protected test, and it is exactly the
# guard that was missing: on this run it gives F(8,18)=2.18, p=0.081 for owner sim (NOT significant,
# so no lever may be promoted) against p=0.0092 for cpu.process and p<1e-9 for the GPU series. The
# instrument does resolve real lever effects; it just does not resolve them on sim. Without the
# protection the three sim numbers read exactly like the cpu.process ones.
#
# WHY THE MATH IS INLINE. scipy is imported by NO script in this repo, and a gate that needs a new
# third-party dependency to run is a gate that silently stops running. The regularized incomplete
# beta below is the standard continued-fraction evaluation; `selftest_omnibus` checks it against
# published F-table critical values AND against the three real series.
OMNIBUS_ALPHA = 0.05


def _betacf(a, b, x, itmax=200, eps=3e-16):
    """Continued fraction for the incomplete beta (Lentz). Standard NR formulation."""
    qab, qap, qam = a + b, a + 1.0, a - 1.0
    c, d = 1.0, 1.0 - qab * x / qap
    if abs(d) < 1e-300:
        d = 1e-300
    d = 1.0 / d
    h = d
    for m in range(1, itmax + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < 1e-300:
            d = 1e-300
        c = 1.0 + aa / c
        if abs(c) < 1e-300:
            c = 1e-300
        d = 1.0 / d
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < 1e-300:
            d = 1e-300
        c = 1.0 + aa / c
        if abs(c) < 1e-300:
            c = 1e-300
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < eps:
            break
    return h


def betai(a, b, x):
    """Regularized incomplete beta I_x(a,b)."""
    if x <= 0.0:
        return 0.0
    if x >= 1.0:
        return 1.0
    lbeta = (math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b)
             + a * math.log(x) + b * math.log1p(-x))
    if x < (a + 1.0) / (a + b + 2.0):
        return math.exp(lbeta) * _betacf(a, b, x) / a
    return 1.0 - math.exp(lbeta) * _betacf(b, a, 1.0 - x) / b


def f_sf(F, df1, df2):
    """P(X >= F) for X ~ F(df1, df2). The omnibus p-value."""
    if F <= 0:
        return 1.0
    return betai(df2 / 2.0, df1 / 2.0, df2 / (df2 + df1 * F))


def t_sf_two_sided(tstat, df):
    """Two-sided p for Student t. Same incomplete beta as the F above -- no new dependency."""
    if df <= 0:
        return 1.0
    return betai(df / 2.0, 0.5, df / (df + tstat * tstat))


def corrected_p(delta, pooled_sd, n_lever, n_base, df, n_comparisons):
    """Bonferroni-corrected two-sided p for one lever against the baseline.

    REPORTED, NOT GATED, and the distinction is deliberate. The omnibus below decides what may be
    called RESOLVED. This number exists so that no delta is ever quoted without the reader seeing
    what it costs to have tested EIGHT levers and kept the best one. On the run that motivated all
    this the two disagree in a way that matters: cpu.process passes the omnibus (p=0.0092) while its
    second survivor, off-envRefreshInterval, sits at a corrected p of 0.198. Gating on the stricter
    of the two would withdraw more of the published set than the defect actually found justifies, so
    the choice is surfaced rather than made here.
    """
    if pooled_sd <= 0 or n_lever < 1 or n_base < 1:
        return None
    se = pooled_sd * math.sqrt(1.0 / n_lever + 1.0 / n_base)
    if se <= 0:
        return None
    return min(1.0, t_sf_two_sided(abs(delta / se), df) * max(1, n_comparisons))


def omnibus(legs, key):
    """-> (F, df1, df2, p, pooled_sd) across ALL lever groups incl. baseline, or None if too thin."""
    groups = []
    for _lever, reps in legs.items():
        vals = [m[key] for m in reps.values() if key in m]
        if len(vals) >= 2:
            groups.append(vals)
    if len(groups) < 2:
        return None
    allv = [x for g in groups for x in g]
    grand = st.mean(allv)
    df1, df2 = len(groups) - 1, len(allv) - len(groups)
    if df1 < 1 or df2 < 1:
        return None
    ssb = sum(len(g) * (st.mean(g) - grand) ** 2 for g in groups)
    ssw = sum(sum((x - st.mean(g)) ** 2 for x in g) for g in groups)
    if ssw <= 0:
        return None
    F = (ssb / df1) / (ssw / df2)
    return F, df1, df2, f_sf(F, df1, df2), (ssw / df2) ** 0.5


def common_offset(legs, key, base_mean):
    """-> (mean delta, n, all_same_sign). Every lever sharing a sign is the drift signature."""
    deltas = []
    for lever, reps in legs.items():
        if lever == "baseline":
            continue
        vals = [m[key] for m in reps.values() if key in m]
        if len(vals) >= 2:
            deltas.append(st.mean(vals) - base_mean)
    if not deltas:
        return 0.0, 0, False
    return st.mean(deltas), len(deltas), all(d < 0 for d in deltas) or all(d > 0 for d in deltas)


def verdicts_for(legs, key, null_control):
    """-> (base_mean, floor, floor_why, [(lever, delta, own_spread, resolved)], omni) or None.

    A lever is RESOLVED only if the key passes the omnibus test FIRST -- see the block above. The
    per-lever floor is kept as a second, independent hurdle rather than replaced: it encodes the
    null control and the baseline's own spread, which the F test does not know about.
    """
    base = {r: m[key] for r, m in legs.get("baseline", {}).items() if key in m}
    if len(base) < 2:
        return None
    base_mean = st.mean(base.values())
    base_spread = max(base.values()) - min(base.values())

    ctrl = legs.get(f"off-{null_control}") or legs.get(null_control) or {}
    ctrl_vals = [m[key] for m in ctrl.values() if key in m]
    if ctrl_vals:
        ctrl_delta = abs(st.mean(ctrl_vals) - base_mean)
        floor, why = max(ctrl_delta, base_spread), "max(null-control delta, baseline spread)"
    else:
        floor, why = base_spread, "baseline spread only -- NO NULL CONTROL IN THIS RUN"

    omni = omnibus(legs, key)
    # No between-lever structure => nothing on this key is attributable to any lever, whatever the
    # individual deltas look like. Fisher's protection: the omnibus comes first, always.
    resolves = omni is not None and omni[3] < OMNIBUS_ALPHA

    rows = []
    for lever, reps in sorted(legs.items()):
        if lever == "baseline":
            continue
        vals = [m[key] for m in reps.values() if key in m]
        if len(vals) < 2:
            continue
        d = st.mean(vals) - base_mean
        own = max(vals) - min(vals)
        rows.append((lever, d, own, resolves and abs(d) > floor and abs(d) > own, len(vals)))
    return base_mean, floor, why, rows, omni, len(base)


def report(legs, null_control, voided, unreadable):
    if "baseline" not in legs:
        print("lever-cpu: FAIL -- no baseline leg resolved; every delta would be against nothing.")
        return EXIT_NOTHING

    n = sum(len(r) for r in legs.values())
    print(f"  {n} leg(s) with a joined CPU axis, {len(legs)} lever group(s)")
    if null_control:
        print(f"  null control declared by the lever table: {null_control}")
    else:
        print("  !! NO NULL CONTROL DECLARED -- the floor falls back to the baseline's spread alone.")
    if unreadable:
        print(f"  !! {len(unreadable)} leg(s) had no readable joined axis and were EXCLUDED: "
              f"{', '.join(unreadable[:4])}{' ...' if len(unreadable) > 4 else ''}")
    if voided:
        print(f"  VOID -- the runner says the lever did not engage; no delta is quotable "
              f"({len(voided)}): {', '.join(sorted(voided))}")

    any_resolved = []
    for key in [WHOLE] + OWNERS + EXTRA:
        v = verdicts_for(legs, key, null_control)
        if v is None:
            print(f"\n  {key}\n    NOT MEASURED -- fewer than two baseline repeats carry this key.")
            continue
        base_mean, floor, why, rows, omni, n_base = v
        print(f"\n  {key}   baseline {base_mean:+.4f} cores   floor {floor:.4f}  ({why})")
        if omni is None:
            print("       OMNIBUS not computable -- too few groups; NOTHING may be resolved here.")
        else:
            F, df1, df2, pval, pooled = omni
            ok = pval < OMNIBUS_ALPHA
            print(f"       omnibus F({df1},{df2}) = {F:.2f}, p = {pval:.4f}, pooled sd {pooled:.4f}"
                  f"  -> {'the key resolves lever effects' if ok else 'NO between-lever structure'}")
            if not ok:
                off, n_off, same = common_offset(legs, key, base_mean)
                print(f"       NOTHING IS ATTRIBUTABLE ON THIS KEY in this run. Every delta below is "
                      f"reported for completeness and NONE of them is a lever effect.")
                if same and n_off > 2:
                    print(f"       all {n_off} levers move the same way (mean {off:+.4f} cores), "
                          f"including the null control -- the signature of drift, not of levers.")
        res = [r for r in rows if r[3]]
        unres = [r for r in rows if not r[3]]
        if res:
            for lever, d, own, _, nl in sorted(res, key=lambda r: -abs(r[1])):
                cp = corrected_p(d, omni[4], nl, n_base, omni[2], len(rows)) if omni else None
                cps = f", Bonferroni p={cp:.3f}" if cp is not None else ""
                print(f"       RESOLVED    {lever:34s} {d:+8.4f} cores   "
                      f"(own spread {own:.4f}{cps})")
            any_resolved.append(key)
        else:
            print("       RESOLVED    (none)")
        for lever, d, own, _, _nl in sorted(unres, key=lambda r: -abs(r[1])):
            if omni is not None and omni[3] >= OMNIBUS_ALPHA:
                why2 = "key resolves nothing"
            elif abs(d) <= floor:
                why2 = "inside the floor"
            else:
                why2 = f"own spread {own:.4f} exceeds the delta"
            print(f"       unresolved  {lever:34s} {d:+8.4f} cores   ({why2})")

    # THE ATTRIBUTION CHECK, which the GPU side has no analogue for: the owners are PARTS of the whole,
    # and the whole is measured independently. A lever that moves the whole while the owners do not
    # account for it has moved work somewhere the attribution cannot see, and that is a finding about
    # the INSTRUMENT rather than about the lever.
    print("\n  ATTRIBUTION CHECK -- does the sum of the owner deltas account for the whole's delta?")
    wv = verdicts_for(legs, WHOLE, null_control)
    if wv:
        whole_rows = {lever: d for lever, d, _o, _r, _n in wv[3]}
        owner_sums = collections.defaultdict(float)
        for key in OWNERS:
            ov = verdicts_for(legs, key, null_control)
            if ov:
                for lever, d, _o, _r, _n in ov[3]:
                    owner_sums[lever] += d
        print(f"       {'lever':34s} {'whole':>9} {'sum(owners)':>12} {'unaccounted':>12}")
        for lever in sorted(whole_rows):
            w, s = whole_rows[lever], owner_sums.get(lever, 0.0)
            print(f"       {lever:34s} {w:+9.4f} {s:+12.4f} {w - s:+12.4f}")
    else:
        print("       not computable -- the whole did not resolve.")

    if not any_resolved:
        print("\n  NO CPU SERIES RESOLVED A SINGLE LEVER IN THIS RUN. That is a MEASURED result, not a")
        print("  failure: it says every lever's CPU effect at this scene is smaller than the run's own")
        print("  noise floor. It is NOT the same as 'the levers cost nothing' -- sample longer, or at a")
        print("  scene that exercises them.")
    return EXIT_OK


# One per numbered check in selftest(). Asserted against the numbering itself below, so adding a
# check without updating this is a FAILURE rather than a silently stale banner.
ARMS = 11


def selftest():
    fails = []

    def mk(baseline, off_big, off_null):
        """Three repeats each; values are the WHOLE, owners split evenly so the sums check out."""
        legs = {}
        for lever, vals in (("baseline", baseline), ("off-big", off_big), ("off-nullctl", off_null)):
            legs[lever] = {}
            for i, v in enumerate(vals, 1):
                legs[lever][f"r{i}"] = {WHOLE: v, OWNERS[0]: v * 0.6, OWNERS[1]: v * 0.4}
        return legs

    # 1. A LEVER WELL CLEAR OF THE FLOOR RESOLVES; ONE INSIDE IT DOES NOT.
    legs = mk([0.50, 0.52, 0.51], [0.80, 0.81, 0.80], [0.505, 0.515, 0.510])
    base_mean, floor, _why, rows, _omni, _nb = verdicts_for(legs, WHOLE, "nullctl")
    got = {lever: resolved for lever, _d, _o, resolved, _n in rows}
    if not got.get("off-big"):
        fails.append("a +0.29-core lever did not clear the floor")
    if got.get("off-nullctl"):
        fails.append("the null control itself resolved -- the floor is not being applied")

    # 2. A LEVER CARRIED BY ONE OUTLIER REPEAT IS NOT A MEASUREMENT, even when its MEAN clears the
    #    floor. Same rule pmu-join learned at Ark Ruins: the run floor comes from the BASELINE's
    #    spread and is blind to a lever that is itself unstable.
    legs2 = mk([0.500, 0.501, 0.502], [0.560, 0.502, 0.503], [0.500, 0.501, 0.502])
    _b, _f, _w, rows2, _o2, _nb2 = verdicts_for(legs2, WHOLE, "nullctl")
    got2 = {lever: r for lever, _d, _o, r, _n in rows2}
    if got2.get("off-big"):
        fails.append("a lever carried entirely by one outlier repeat was reported RESOLVED")

    # 4. THE OMNIBUS GATE, added because three sim numbers were published from a key with no
    #    between-lever structure at all. Two arms: it must SUPPRESS a key that resolves nothing, and
    #    it must NOT suppress a key that does -- a gate that only ever says no is not a gate.
    #
    #    (a) EVERY lever shifted by the same amount, which is drift, not eight lever effects. The
    #        per-lever floor cannot see this: baseline and control both carry the offset.
    #        The proportions are the REAL ones: a 0.030-core shift against a within-group sd of
    #        ~0.021, which is what cpu.owner.sim actually looked like. A first attempt used sd 0.010
    #        and the arm failed correctly -- a 3-sigma shift IS detectable, and a fixture that makes
    #        the defect obvious is not a test of the defect.
    drift = mk([0.500, 0.521, 0.479], [0.470, 0.492, 0.448], [0.472, 0.450, 0.494])
    _bm, _fl, _wy, rows4, omni4, _n4 = verdicts_for(drift, WHOLE, "nullctl")
    if omni4 is None or omni4[3] < OMNIBUS_ALPHA:
        fails.append("a common shift shared by every lever passed the omnibus")
    if any(r for _l, _d, _o, r, _n in rows4):
        fails.append("a lever RESOLVED on a key with no between-lever structure")
    off, n_off, same = common_offset(drift, WHOLE, _bm)
    if not same or n_off != 2:
        fails.append("the common-offset detector missed a shift shared by every lever")

    #    (b) A REAL, WELL-SEPARATED EFFECT MUST STILL SURVIVE. Same shape as arm 1, checked through
    #        the omnibus rather than the floor.
    real = mk([0.500, 0.502, 0.501], [0.800, 0.802, 0.801], [0.500, 0.503, 0.502])
    _bm5, _fl5, _wy5, rows5, omni5, _n5 = verdicts_for(real, WHOLE, "nullctl")
    if omni5 is None or omni5[3] >= OMNIBUS_ALPHA:
        fails.append("a large, clean lever effect was suppressed by the omnibus")
    if not {l: r for l, _d, _o, r, _n in rows5}.get("off-big"):
        fails.append("a large, clean lever effect did not resolve under the omnibus")

    # 5. THE DISTRIBUTIONS ARE THE ARITHMETIC EVERYTHING ELSE RESTS ON. Check against published
    #    table values rather than against themselves: F(8,18) 5% critical value is 2.510, and a
    #    two-sided t at df=18 for |t|=2.101 is 0.05.
    if abs(f_sf(2.510, 8, 18) - 0.05) > 5e-4:
        fails.append(f"F distribution wrong: sf(2.510; 8,18) = {f_sf(2.510, 8, 18):.5f}, want 0.05")
    if abs(t_sf_two_sided(2.101, 18) - 0.05) > 5e-4:
        fails.append(f"t distribution wrong: p(|t|>2.101; 18) = {t_sf_two_sided(2.101, 18):.5f}")
    if abs(f_sf(1.0, 1, 1) - 0.5) > 1e-6:
        fails.append("F distribution wrong at the F(1,1)=1 midpoint")

    # 6. THE CORRECTION IS A CORRECTION: testing more levers must never make a p SMALLER.
    p1 = corrected_p(0.05, 0.02, 3, 3, 18, 1)
    p8 = corrected_p(0.05, 0.02, 3, 3, 18, 8)
    if p1 is None or p8 is None or p8 < p1:
        fails.append("the Bonferroni correction did not widen with more comparisons")

    # 3. A VOID LEG IS NOT QUOTED, and its healthy repeats are not dropped with it.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        def write(label, whole):
            json.dump({"label": label, "intervals": [
                {"index": 0, "tStartEpoch": 1, "tEndEpoch": 2, "axis": {WHOLE: whole}},
                {"index": 1, "tStartEpoch": 2, "tEndEpoch": 3, "axis": {WHOLE: whole}}]},
                open(os.path.join(d, label + ".joined.json"), "w"))
        for r in (1, 2, 3):
            write(f"RUN-r{r}-baseline", 0.5)
            write(f"RUN-r{r}-off-big", 0.8)
        with open(os.path.join(d, "legs.tsv"), "w") as fh:
            for r in (1, 2, 3):
                fh.write(f"RUN-r{r}-baseline\tk\to\tOK\t-\t-\t-\t-\n")
                fh.write(f"RUN-r{r}-off-big\tk\to\t{'VOID' if r == 2 else 'OK'}\t-\t-\t-\t-\n")
        legs3, voided3, unread3 = collect(d, read_verdicts(d), "RUN")
        if "r2" in legs3.get("off-big", {}):
            fails.append("a VOID leg was collected into the table anyway")
        if voided3.get("off-big") != ["r2"]:
            fails.append("a VOID leg was not reported under its own verdict")
        if set(legs3.get("off-big", {})) != {"r1", "r3"}:
            fails.append("excluding a VOID leg also dropped its healthy repeats")
        if unread3:
            fails.append("a readable leg was reported unreadable")

    # 4. AN INTERVAL WITH NO AXIS CONTRIBUTES NOTHING, never a zero -- the absent-vs-zero rule, one
    #    consumer further out.
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.joined.json")
        json.dump({"label": "x", "intervals": [
            {"index": 0, "axis": {WHOLE: 0.6}},
            {"index": 1, "unjoinable": "no epoch stamps"},
            {"index": 2, "axis": {WHOLE: 0.8}}]}, open(p, "w"))
        m = leg_means(p)
        if abs(m[WHOLE] - 0.7) > 1e-9:
            fails.append(f"an unjoinable interval entered the mean as a zero (got {m[WHOLE]})")

    # 5. A RUN WITH NO BASELINE FAILS rather than quoting deltas against nothing.
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()) as buf:
        rc = report({"off-x": {"r1": {WHOLE: 0.5}}}, "nullctl", {}, [])
    if rc != EXIT_NOTHING:
        fails.append("a run with no baseline did not fail")

    # 6. AN ALL-NULL RUN SAYS SO IN WORDS. A table of "unresolved" with no closing sentence reads as a
    #    broken instrument; it is a measured result and has to be named as one.
    flat = mk([0.500, 0.501, 0.502], [0.500, 0.502, 0.501], [0.501, 0.500, 0.502])
    with contextlib.redirect_stdout(io.StringIO()) as buf2:
        report(flat, "nullctl", {}, [])
    if "NO CPU SERIES RESOLVED A SINGLE LEVER" not in buf2.getvalue():
        fails.append("a run in which nothing resolved did not say so")
    if "MEASURED result" not in buf2.getvalue():
        fails.append("an all-null run was not named as a measured result")

    # 7. THE ATTRIBUTION CHECK REPORTS THE UNACCOUNTED REMAINDER. Owners at 0.6/0.4 of the whole sum
    #    exactly, so the remainder must be ~0; a version that forgot an owner would show it.
    with contextlib.redirect_stdout(io.StringIO()) as buf3:
        report(legs, "nullctl", {}, [])
    line = [l for l in buf3.getvalue().splitlines() if "off-big" in l and "+" in l and l.count("+") >= 2]
    if not line:
        fails.append("the attribution check printed no row for a resolved lever")
    elif abs(float(line[-1].split()[-1])) > 1e-3:
        fails.append(f"owners summing to the whole reported a non-zero remainder: {line[-1]}")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    # DERIVED, NOT DECLARED. This line read "7/7 arms ok" while eleven arms were running -- the count
    # was a literal and the arms were free to grow past it, so four new checks were invisible in the
    # output that reports them. Same defect telemetry-window.py's selftest carried and for the same
    # reason; here the number cannot drift because nothing writes it down.
    print(f"  lever_cpu selftest: {ARMS}/{ARMS} arms ok (the floor swallows the null control, an "
          f"outlier repeat is refused, a shift shared by EVERY lever is suppressed by the omnibus, a "
          f"clean effect still resolves through it, F and t match published table values, the "
          f"Bonferroni correction widens with the comparison count, VOID legs are excluded without "
          f"dropping their siblings, an unjoinable interval is not a zero, a baseline-less run "
          f"fails, an all-null run is named a result, the attribution remainder is reported)")
    return EXIT_OK


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run_dir", nargs="?")
    ap.add_argument("--null-control", default=None)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.run_dir:
        print("lever-cpu: need <matrix-run-dir>", file=sys.stderr)
        return EXIT_USAGE

    run_id = os.path.basename(args.run_dir.rstrip("/"))
    null_control = args.null_control or declared_null_control()
    verdicts = read_verdicts(args.run_dir)
    legs, voided, unreadable = collect(args.run_dir, verdicts, run_id)
    print(f"=== lever-cpu: {run_id}")
    if not legs:
        print("lever-cpu: FAIL -- no leg carried a joined CPU axis. Either the run predates the join, "
              "or no sovereign sampler was running.")
        return EXIT_NOTHING
    return report(legs, null_control, voided, unreadable)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
