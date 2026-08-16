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

# THE SUBSYSTEM KEYS (#273), mirroring obs-join's declared list -- they arrive on the axis as
# wall_fraction. Without these a matrix run analyses five owner-level aggregates and leaves every
# named subsystem cost unattributed, which is what it would have done today.
SUBSYSTEM = [
    "lighting.gpu.drive.repack.wall_fraction", "lighting.gpu.drive.upload.wall_fraction",
    "lighting.gpu.drive.spread.wall_fraction", "lighting.gpu.drive.point.wall_fraction",
    "lighting.gpu.drive.compose.wall_fraction", "lighting.gpu.drive.upscale.wall_fraction",
    "lighting.gpu.drive.flush.wall_fraction", "lighting.gpu.spread_scan.wall_fraction",
    # #272's residual keys. drive.point.switch and drive.point.gputimer are SUBSETS of
    # drive.point -- analysed per lever like any other key, never summed with their parent.
    "lighting.gpu.drive.switch.wall_fraction", "lighting.gpu.drive.quad.wall_fraction",
    "lighting.gpu.drive.gputimer.wall_fraction", "lighting.gpu.drive.bind.wall_fraction",
    "lighting.gpu.drive.point.switch.wall_fraction", "lighting.gpu.drive.point.gputimer.wall_fraction",
    "lighting.produce.entities.wall_fraction", "lighting.produce.prep.wall_fraction",
    "lighting.produce.particles.wall_fraction", "lighting.produce.adjust.wall_fraction",
    "lighting.gpu.cpu_cost.wall_fraction", "lighting.cpu.total.wall_fraction",
]

# The full analysed set, in report order. One list so the omnibus family and the loop cannot disagree
# -- a corrected alpha computed over a different set than the one tested is the shape of an
# unfalsifiable threshold.
ALL_KEYS = [WHOLE] + OWNERS + EXTRA + SUBSYSTEM


def declared_null_control():
    """The lever the table declares cannot touch THE CPU. Returns None when none does, which is today.

    IT USED TO IMPORT pmu-join's, AND THAT WAS THE DEFECT (#268). The reuse-not-restate reasoning was
    right -- two places knowing the control would drift -- but it reused the wrong QUANTITY's control.
    pmu-join reads `gpuNullControl`, which is null for the GPU and emphatically not for the CPU:
    scriptProtoCacheEnabled swaps Lua chunk compilation for a cache lookup, and on
    matrix-20260808-140430 it moves cpu.process.busy_cores by -0.0299 cores -- 63% of that key's floor.
    A control that is not null for the measured quantity inflates every floor and SUPPRESSES real
    levers, which is the opposite of what a control is for.

    NO LEVER IN THE TABLE QUALIFIES, and the table says why: `gpuNullControl` exists because some
    levers cannot reach the GPU, and there is no symmetric escape for the CPU -- every lever here IS a
    CPU code-path change. Returning None is therefore the CORRECT answer, not a missing one, and the
    caller must say so out loud rather than substituting.
    """
    try:
        table = json.load(open(os.path.join(HERE, "lever-table.json")))
    except (OSError, ValueError):
        return None
    for lever in table.get("levers", []):
        if lever.get("cpuNullControl"):
            return lever["key"]
    return None


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
# WIDENED FOR THE NUMBER OF KEYS TESTED (#273), and this is the price of putting the subsystem keys
# on the axis. The omnibus is the gate that OPENS a key: run it on 20 keys at a flat 0.05 and one key
# in every twenty opens by chance, after which the per-lever floor and MDE are being applied inside a
# key that resolves nothing. Fisher's protection is WITHIN a key; nothing was protecting the choice
# BETWEEN keys.
#
# Corrected here rather than by tightening the per-lever family, because these are different
# questions: "which keys resolve anything" is the omnibus's family, "which lever on this key" is the
# lever table's declared family. Conflating them would pay the multiplicity twice.
OMNIBUS_ALPHA_FAMILYWISE = 0.05


def omnibus_alpha(n_keys):
    """-> the per-key omnibus threshold once n_keys are being tested."""
    return OMNIBUS_ALPHA_FAMILYWISE / max(1, n_keys)


# Kept for the selftest arms that predate the correction and test a single key in isolation.
OMNIBUS_ALPHA = OMNIBUS_ALPHA_FAMILYWISE


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


# ---------------------------------------------------------------------------------------------
# THE MINIMUM DETECTABLE EFFECT, and the third verdict it forces.
#
# WHY THIS OUTRANKS THE CORRECTION ARGUMENT. Choosing between an uncorrected alpha and a Bonferroni
# one is choosing a threshold; it says nothing about whether the experiment can REACH that threshold.
# At n=3 per arm it cannot. On matrix-20260808-140430 the smallest effect this design finds at 80%
# power is 0.0668 cores on cpu.owner.sim (31% of that key's baseline) and 0.0859 on cpu.process
# (16%) -- while the two deltas being argued over are 0.0666 and 0.0534, i.e. AT OR BELOW the MDE at
# every correction including none. A p-value computed for a delta under the MDE is not evidence
# about the lever; it is a draw from a distribution the run cannot resolve.
#
# THE RULE: a delta below the MDE is NEVER quotable as a cost, whatever its p. That is stricter than
# any alpha argument and it does not require agreeing on one.
#
# HENCE THREE VERDICTS, because two could not express the state the data is actually in -- the same
# reason ABSENT had to stop being spelled like ZERO, and the same reason run-gates needed exit 77
# alongside pass and fail:
#   CONFIRMED  above the MDE and past the declared correction -- quotable as a measured cost
#   CANDIDATE  clears the floor and the omnibus but sits under the MDE -- quotable ONLY as "this
#              lever deserves a dedicated A/B", never as a number. The matrix is a SCREEN; treating
#              its output as a finding is what put four noise deltas on the board.
#   NULL       inside the floor, or the key has no between-lever structure at all
MDE_Z_POWER = {0.80: 0.8416, 0.90: 1.2816, 0.95: 1.6449}


def t_critical(alpha, df):
    """Two-sided critical t, by bisection on the CDF above. No table, no dependency."""
    lo, hi = 0.0, 200.0
    for _ in range(200):
        mid = (lo + hi) / 2.0
        if t_sf_two_sided(mid, df) > alpha:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2.0


def mde(pooled_sd, n_lever, n_base, df, alpha, power):
    """Smallest lever effect detectable at `power`, in the key's own units.

    (t_crit + z_power) * SE(difference). The normal shift is the standard approximation to the
    noncentral t and is well inside the precision this decides anything at.
    """
    if pooled_sd <= 0 or n_lever < 1 or n_base < 1 or df < 1:
        return None
    z = MDE_Z_POWER.get(round(power, 2))
    if z is None:
        return None
    se = pooled_sd * math.sqrt(1.0 / n_lever + 1.0 / n_base)
    return (t_critical(alpha, df) + z) * se


def load_statistics(path=None):
    """The declared family/alpha/power. DECLARED, never inferred -- see the lever table's comment.

    A missing block is not a default: it is refused, because silently correcting against a family
    this script chose for itself is the unfalsifiable version of the whole exercise.
    """
    path = path or os.path.join(os.path.dirname(os.path.abspath(__file__)), "lever-table.json")
    try:
        s = json.load(open(path)).get("statistics")
    except (OSError, ValueError):
        return None
    if not s or "family" not in s or "alpha" not in s or "power" not in s:
        return None
    return s


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


def verdicts_for(legs, key, null_control, n_keys=1):
    """-> (base_mean, floor, floor_why, [(lever, delta, own_spread, resolved)], omni) or None.

    A lever is RESOLVED only if the key passes the omnibus test FIRST -- see the block above. The
    per-lever floor is kept as a second, independent hurdle rather than replaced: it encodes the
    null control and the baseline's own spread, which the F test does not know about.
    """
    base = {r: m[key] for r, m in legs.get("baseline", {}).items() if key in m}
    if len(base) < 2:
        return None
    base_mean = st.mean(base.values())

    omni = omnibus(legs, key)

    # THE FLOOR HAS TWO TERMS AND USED TO HAVE A THIRD THAT DID NOT BELONG (#268).
    #
    # GONE: the null-control term. It read the lever flagged `gpuNullControl`, which is null for the
    # GPU and not for the CPU -- on matrix-20260808-140430 it moves cpu.process by -0.0299 cores, 63%
    # of that key's floor. A control that is not null for the measured quantity does not bound noise,
    # it ADDS a lever's real effect to the bar and suppresses smaller real levers. declared_null_control
    # now reads `cpuNullControl`, no lever sets it, and the table explains why none can.
    #
    # KEPT: the baseline's own spread across repeats. It is a poor SCALE estimate -- a range over
    # three points, 2 dof -- but it is the only term that sees DRIFT ACROSS LEGS, which is what [#265]
    # found moving every lever including the control. Dropping it for a within-group statistic would
    # lose exactly the signal that mattered.
    #
    # ADDED: the least significant difference from the POOLED within-group sd, which omnibus already
    # computes over every group rather than over the baseline's three points. It is the smallest
    # difference that would be significant at the declared alpha -- strictly below the MDE, which adds
    # the power term on top, so the two are a significance bar and a detectability bar rather than the
    # same test twice.
    #
    # max(), not sum: they bound the same quantity by different routes, and adding them would double-
    # count noise that is already in both.
    base_spread = max(base.values()) - min(base.values())
    lsd = None
    stats_for_floor = load_statistics()
    if omni is not None and stats_for_floor:
        se = omni[4] * math.sqrt(2.0 / max(2, len(base)))
        lsd = t_critical(stats_for_floor["alpha"] / max(1, stats_for_floor["family"]), omni[2]) * se
    if lsd is not None and lsd > base_spread:
        floor, why = lsd, f"pooled-sd LSD (baseline spread {base_spread:.4f} is smaller)"
    elif lsd is not None:
        floor, why = base_spread, f"baseline spread (pooled-sd LSD {lsd:.4f} is smaller)"
    else:
        floor, why = base_spread, "baseline spread only -- no pooled sd available"
    if null_control:
        why += f"; CPU null control declared: {null_control}"
    else:
        why += "; NO CPU NULL CONTROL EXISTS -- see lever-table.json"

    # No between-lever structure => nothing on this key is attributable to any lever, whatever the
    # individual deltas look like. Fisher's protection: the omnibus comes first, always.
    alpha_o = omnibus_alpha(n_keys)
    resolves = omni is not None and omni[3] < alpha_o

    stats = load_statistics()
    n_base = len(base)

    rows = []
    for lever, reps in sorted(legs.items()):
        if lever == "baseline":
            continue
        vals = [m[key] for m in reps.values() if key in m]
        if len(vals) < 2:
            continue
        d = st.mean(vals) - base_mean
        own = max(vals) - min(vals)
        clears = resolves and abs(d) > floor and abs(d) > own
        # THE THIRD VERDICT. Clearing the floor and the omnibus makes a lever a CANDIDATE for a
        # dedicated A/B; only clearing the MDE as well makes its number a measured cost. A delta
        # under the MDE is the largest draw from a distribution this design cannot resolve, and the
        # matrix is a screen, not a confirmation.
        m_val = (mde(omni[4], len(vals), n_base, omni[2],
                     stats["alpha"] / max(1, stats["family"]), stats["power"])
                 if (clears and omni and stats) else None)
        if not clears:
            verdict = "NULL"
        elif m_val is None or abs(d) >= m_val:
            verdict = "CONFIRMED"
        else:
            verdict = "CANDIDATE"
        rows.append((lever, d, own, verdict, len(vals)))
    return base_mean, floor, why, rows, omni, n_base


def persist_thresholds(run_dir, run_id, null_control, rows):
    """Write the thresholds a run's verdicts were judged against, INTO the run (#268 item 4).

    They existed only in stdout. manifest.json still says `"analysis": "NOT PERFORMED"` and the
    evidence manifest contains no "floor" string, so a past run's verdicts could not be audited
    against the bar that produced them -- the same defect that put sceneBoundPct and
    captureDeepTracing into the manifest, one instrument along. A threshold that lives only in a
    terminal is a threshold nobody can check a past conclusion against.

    Written BESIDE the run rather than into manifest.json, because the manifest is the RUNNER's
    record of what it did and this is the ANALYSER's record of how it judged -- one writer each.
    """
    out = os.path.join(run_dir, "lever-cpu-thresholds.json")
    try:
        with open(out, "w") as fh:
            json.dump({"runId": run_id,
                       "cpuNullControl": null_control,
                       "_cpuNullControlNote":
                           "null when no lever is CPU-null by construction, which is the case for the "
                           "shipped table -- see lever-table.json. NOT a missing input.",
                       "omnibusAlphaFamilywise": OMNIBUS_ALPHA_FAMILYWISE,
                       "omnibusAlphaPerKey": omnibus_alpha(len(ALL_KEYS)),
                       "keysTested": len(ALL_KEYS),
                       "statistics": load_statistics(),
                       "keys": rows}, fh, indent=2)
        return out
    except OSError:
        return None


def report(legs, null_control, voided, unreadable, run_dir=None, run_id=None):
    if "baseline" not in legs:
        print("lever-cpu: FAIL -- no baseline leg resolved; every delta would be against nothing.")
        return EXIT_NOTHING

    n = sum(len(r) for r in legs.values())
    print(f"  {n} leg(s) with a joined CPU axis, {len(legs)} lever group(s)")
    if null_control:
        print(f"  CPU null control declared by the lever table: {null_control}")
    else:
        # STATED AS A STRUCTURAL FACT, not as a missing input (#268). `gpuNullControl` works because
        # some levers cannot reach the GPU; every lever in this table is a CPU code-path change, so no
        # symmetric control can exist. This used to silently borrow the GPU one, which is not null for
        # the CPU and therefore ADDED a real lever effect to the bar.
        print("  NO CPU NULL CONTROL EXISTS, and none can from this lever set -- every lever here is a")
        print("  CPU code-path change. The floor is max(baseline spread, pooled-sd LSD); it does NOT")
        print("  borrow gpuNullControl, which moves cpu.process by 63% of that key's own floor.")
    if unreadable:
        print(f"  !! {len(unreadable)} leg(s) had no readable joined axis and were EXCLUDED: "
              f"{', '.join(unreadable[:4])}{' ...' if len(unreadable) > 4 else ''}")
    if voided:
        print(f"  VOID -- the runner says the lever did not engage; no delta is quotable "
              f"({len(voided)}): {', '.join(sorted(voided))}")

    any_resolved = []
    threshold_rows = {}
    # COUNT THE KEYS THAT ACTUALLY HAVE DATA, not the keys in the list. A run predating [#171]/[#271]
    # carries none of the subsystem keys, and dividing the omnibus alpha by keys that are ABSENT would
    # tighten the threshold for the keys that are PRESENT -- punishing a run for instrumentation it
    # could not have had. That is ABSENT-vs-ZERO wearing a multiplicity correction's clothes, and this
    # project has paid for that shape eleven times. Caught by running the banked corpus through it: the
    # 27-leg run has 6 of these 20 keys.
    n_keys_tested = sum(1 for k in ALL_KEYS if verdicts_for(legs, k, null_control, 1) is not None)
    if n_keys_tested != len(ALL_KEYS):
        print(f"  {n_keys_tested} of {len(ALL_KEYS)} analysed keys carry data in this run; the omnibus "
              f"family is the {n_keys_tested} PRESENT, not the {len(ALL_KEYS)} declared")
    for key in ALL_KEYS:
        v = verdicts_for(legs, key, null_control, n_keys_tested)
        if v is None:
            print(f"\n  {key}\n    NOT MEASURED -- fewer than two baseline repeats carry this key.")
            continue
        base_mean, floor, why, rows, omni, n_base = v
        print(f"\n  {key}   baseline {base_mean:+.4f} cores   floor {floor:.4f}  ({why})")
        if omni is None:
            print("       OMNIBUS not computable -- too few groups; NOTHING may be resolved here.")
        else:
            F, df1, df2, pval, pooled = omni
            ok = pval < omnibus_alpha(n_keys_tested)
            print(f"       omnibus F({df1},{df2}) = {F:.2f}, p = {pval:.4f}, pooled sd {pooled:.4f}"
                  f"  -> {'the key resolves lever effects' if ok else 'NO between-lever structure'}")
            if not ok:
                off, n_off, same = common_offset(legs, key, base_mean)
                print(f"       NOTHING IS ATTRIBUTABLE ON THIS KEY in this run. Every delta below is "
                      f"reported for completeness and NONE of them is a lever effect.")
                if same and n_off > 2:
                    print(f"       all {n_off} levers move the same way (mean {off:+.4f} cores), "
                          f"including the null control -- the signature of drift, not of levers.")
        stats = load_statistics()
        if stats is None:
            print("       !! NO `statistics` BLOCK IN lever-table.json -- family/alpha/power are "
                  "undeclared, so no MDE can be computed and nothing here is quotable as a cost.")
        elif omni is not None:
            mv = mde(omni[4], 3, n_base, omni[2],
                     stats["alpha"] / max(1, stats["family"]), stats["power"])
            if mv:
                pct = 100.0 * mv / abs(base_mean) if base_mean else float("nan")
                print(f"       MDE {mv:.4f} cores ({pct:.1f}% of baseline) at {int(stats['power']*100)}% "
                      f"power, alpha {stats['alpha']}/{stats['family']} ({stats['correction']}) -- "
                      f"a smaller delta is NOT quotable as a cost")
        if omni is not None:
            threshold_rows[key] = {
                "baseline": round(base_mean, 6), "floor": round(floor, 6), "floorWhy": why,
                "omnibusF": round(omni[0], 4), "omnibusP": round(omni[3], 6),
                "pooledSd": round(omni[4], 6), "df1": omni[1], "df2": omni[2],
                "mde": (round(mv, 6) if (stats and omni and (mv := mde(
                    omni[4], 3, n_base, omni[2],
                    stats["alpha"] / max(1, stats["family"]), stats["power"]))) else None),
                "verdicts": {lever: v for lever, _d, _o, v, _n in rows},
            }
        res = [r for r in rows if r[3] == "CONFIRMED"]
        cand = [r for r in rows if r[3] == "CANDIDATE"]
        unres = [r for r in rows if r[3] == "NULL"]
        if res:
            for lever, d, own, _, nl in sorted(res, key=lambda r: -abs(r[1])):
                cp = corrected_p(d, omni[4], nl, n_base, omni[2], len(rows)) if omni else None
                cps = f", corrected p={cp:.3f}" if cp is not None else ""
                print(f"       CONFIRMED   {lever:34s} {d:+8.4f} cores   "
                      f"(own spread {own:.4f}{cps})")
            any_resolved.append(key)
        else:
            print("       CONFIRMED   (none)")
        for lever, d, own, _, nl in sorted(cand, key=lambda r: -abs(r[1])):
            cp = corrected_p(d, omni[4], nl, n_base, omni[2], len(rows)) if omni else None
            cps = f", corrected p={cp:.3f}" if cp is not None else ""
            print(f"       CANDIDATE   {lever:34s} {d:+8.4f} cores   (under the MDE -- worth a "
                  f"dedicated A/B, NOT a cost{cps})")
        for lever, d, own, _, _nl in sorted(unres, key=lambda r: -abs(r[1])):
            if omni is not None and omni[3] >= omnibus_alpha(n_keys_tested):
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
    wv = verdicts_for(legs, WHOLE, null_control, n_keys_tested)
    if wv:
        whole_rows = {lever: d for lever, d, _o, _r, _n in wv[3]}
        owner_sums = collections.defaultdict(float)
        for key in OWNERS:
            ov = verdicts_for(legs, key, null_control, n_keys_tested)
            if ov:
                for lever, d, _o, _r, _n in ov[3]:
                    owner_sums[lever] += d
        print(f"       {'lever':34s} {'whole':>9} {'sum(owners)':>12} {'unaccounted':>12}")
        for lever in sorted(whole_rows):
            w, s = whole_rows[lever], owner_sums.get(lever, 0.0)
            print(f"       {lever:34s} {w:+9.4f} {s:+12.4f} {w - s:+12.4f}")
    else:
        print("       not computable -- the whole did not resolve.")

    if run_dir and run_id:
        written = persist_thresholds(run_dir, run_id, null_control, threshold_rows)
        if written:
            print(f"\n  thresholds written to {os.path.basename(written)} -- the bar these verdicts were "
                  f"judged against now travels with the run, not just this terminal")

    if not any_resolved:
        print("\n  NO CPU SERIES RESOLVED A SINGLE LEVER IN THIS RUN. That is a MEASURED result, not a")
        print("  failure: it says every lever's CPU effect at this scene is smaller than the run's own")
        print("  noise floor. It is NOT the same as 'the levers cost nothing' -- sample longer, or at a")
        print("  scene that exercises them.")
    return EXIT_OK


# One per numbered check in selftest(). Asserted against the numbering itself below, so adding a
# check without updating this is a FAILURE rather than a silently stale banner.
ARMS = 17


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
    # The verdict is a STRING now, and "NULL" is truthy -- comparing it as a boolean is how these
    # three arms started passing junk. Compare the verdict, never its truthiness.
    got = {lever: verdict for lever, _d, _o, verdict, _n in rows}
    if got.get("off-big") == "NULL":
        fails.append("a +0.29-core lever did not clear the floor")
    # RENAMED WITH THE RULE (#268). This arm used to be called "the floor swallows the null control",
    # and after the CPU control was removed that name described a mechanism that no longer exists --
    # the vocabulary outliving the rule, for the fourth time in this project. What it actually tests,
    # and always did, is that a delta far below the floor does not resolve.
    if got.get("off-nullctl") != "NULL":
        fails.append("a delta far below the floor resolved anyway")

    # 2. A LEVER CARRIED BY ONE OUTLIER REPEAT IS NOT A MEASUREMENT, even when its MEAN clears the
    #    floor. Same rule pmu-join learned at Ark Ruins: the run floor comes from the BASELINE's
    #    spread and is blind to a lever that is itself unstable.
    legs2 = mk([0.500, 0.501, 0.502], [0.560, 0.502, 0.503], [0.500, 0.501, 0.502])
    _b, _f, _w, rows2, _o2, _nb2 = verdicts_for(legs2, WHOLE, "nullctl")
    got2 = {lever: r for lever, _d, _o, r, _n in rows2}
    if got2.get("off-big") != "NULL":
        fails.append("a lever carried entirely by one outlier repeat was not refused")

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
    if any(r != "NULL" for _l, _d, _o, r, _n in rows4):
        fails.append("a lever resolved on a key with no between-lever structure")
    off, n_off, same = common_offset(drift, WHOLE, _bm)
    if not same or n_off != 2:
        fails.append("the common-offset detector missed a shift shared by every lever")

    #    (b) A REAL, WELL-SEPARATED EFFECT MUST STILL SURVIVE. Same shape as arm 1, checked through
    #        the omnibus rather than the floor.
    real = mk([0.500, 0.502, 0.501], [0.800, 0.802, 0.801], [0.500, 0.503, 0.502])
    _bm5, _fl5, _wy5, rows5, omni5, _n5 = verdicts_for(real, WHOLE, "nullctl")
    if omni5 is None or omni5[3] >= OMNIBUS_ALPHA:
        fails.append("a large, clean lever effect was suppressed by the omnibus")
    if {l: r for l, _d, _o, r, _n in rows5}.get("off-big") == "NULL":
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

    # 8. THE MDE RULE AND THE THIRD VERDICT. A screen that reports its largest draw as a measured
    #    cost is how four noise deltas reached the board; the MDE is the bar that makes the
    #    distinction mechanical rather than editorial.
    if abs(t_critical(0.05, 18) - 2.101) > 5e-4:
        fails.append(f"t_critical(0.05,18) = {t_critical(0.05, 18):.4f}, published table says 2.101")
    if abs(t_critical(0.01, 18) - 2.878) > 5e-3:
        fails.append(f"t_critical(0.01,18) = {t_critical(0.01, 18):.4f}, published table says 2.878")

    #    (a) CORRECTING FOR MORE COMPARISONS MUST RAISE THE BAR, and more repeats must lower it.
    wide = mde(0.02, 3, 3, 18, 0.05, 0.80)
    tight = mde(0.02, 3, 3, 18, 0.05 / 8, 0.80)
    if not (tight > wide > 0):
        fails.append("a stricter alpha did not raise the minimum detectable effect")
    if not (mde(0.02, 10, 10, 81, 0.05 / 8, 0.80) < tight):
        fails.append("more repeats did not lower the minimum detectable effect")
    if mde(0.02, 3, 3, 18, 0.05, 0.99) is not None:
        fails.append("an undeclared power target was silently accepted")

    #    (b) A LEVER UNDER THE MDE IS A CANDIDATE, NOT A COST -- even though it clears the floor and
    #        the omnibus. Built by scaling a clean, well-separated effect down until it is real but
    #        unresolvable at n=3.
    big = mk([0.5000, 0.5010, 0.5005], [0.6000, 0.6010, 0.6005], [0.5000, 0.5012, 0.5006])
    _b8, _f8, _w8, rows8, _o8, _n8 = verdicts_for(big, WHOLE, "nullctl")
    v8 = {l: v for l, _d, _o, v, _n in rows8}
    if v8.get("off-big") != "CONFIRMED":
        fails.append(f"a 0.10-core effect at sd~0.0005 was not CONFIRMED (got {v8.get('off-big')})")

    small = mk([0.5000, 0.5300, 0.4700], [0.4600, 0.4900, 0.4300], [0.5010, 0.5310, 0.4710])
    _b9, _f9, _w9, rows9, _o9, _n9 = verdicts_for(small, WHOLE, "nullctl")
    v9 = {l: v for l, _d, _o, v, _n in rows9}
    if v9.get("off-big") == "CONFIRMED":
        fails.append("a delta below the MDE was reported CONFIRMED rather than CANDIDATE")

    # 9. THE FAMILY IS DECLARED, NOT INFERRED. A missing block must refuse rather than default:
    #    silently correcting against a family this script picked for itself is the unfalsifiable
    #    version of the whole exercise.
    import tempfile as _tf
    with _tf.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
        json.dump({"levers": []}, fh)
        empty_table = fh.name
    if load_statistics(empty_table) is not None:
        fails.append("a lever table with no statistics block did not refuse")
    if load_statistics() is None:
        fails.append("the real lever table declares no statistics block")
    else:
        s = load_statistics()
        if s["family"] < 1 or not (0 < s["alpha"] < 1) or not (0 < s["power"] < 1):
            fails.append(f"the declared statistics block is out of range: {s}")
    os.unlink(empty_table)

    # 10. THE CPU NULL CONTROL IS ABSENT BY CONSTRUCTION, AND MUST NOT BE THE GPU ONE (#268).
    #     lever-cpu used to import pmu-join's declared_null_control, which reads `gpuNullControl` --
    #     null for the GPU, and worth 63% of cpu.process's floor. The two arms below are the ones that
    #     would have caught that: the table's GPU control must not come back as the CPU answer.
    table_path = os.path.join(HERE, "lever-table.json")
    try:
        table = json.load(open(table_path))
    except (OSError, ValueError):
        table = None
    if table is None:
        fails.append("lever-table.json is unreadable, so the control declaration cannot be checked")
    else:
        gpu_ctl = next((l["key"] for l in table.get("levers", []) if l.get("gpuNullControl")), None)
        cpu_ctl = declared_null_control()
        if gpu_ctl and cpu_ctl == gpu_ctl:
            fails.append(f"the CPU null control resolved to the GPU one ({gpu_ctl}) -- #268 regressed")
        declared = [l["key"] for l in table.get("levers", []) if l.get("cpuNullControl")]
        if cpu_ctl != (declared[0] if declared else None):
            fails.append(f"declared_null_control() = {cpu_ctl!r} but the table declares {declared!r}")

    # 11. THE FLOOR TAKES THE POOLED LSD WHEN IT BINDS, and the baseline range when THAT binds. Both
    #     directions, because a floor that only ever grows suppresses real levers as happily as noise.
    import contextlib as _cl, io as _io
    tight_base = mk([0.5000, 0.5001, 0.5002], [0.9000, 0.9001, 0.9002], [0.5000, 0.5002, 0.5001])
    _b, floor_tight, why_tight, _r, _o, _n = verdicts_for(tight_base, WHOLE, None)
    if "LSD" not in why_tight:
        fails.append(f"a near-zero baseline range did not defer to the pooled LSD: {why_tight}")
    wide_base = mk([0.30, 0.70, 0.50], [0.9000, 0.9001, 0.9002], [0.5000, 0.5002, 0.5001])
    _b2, floor_wide, why_wide, _r2, _o2, _n2 = verdicts_for(wide_base, WHOLE, None)
    if "baseline spread" not in why_wide or floor_wide <= floor_tight:
        fails.append(f"a wide baseline range did not raise the floor above the LSD case: {why_wide}")

    # 12. THE OMNIBUS FAMILY COUNTS PRESENT KEYS, NOT DECLARED ONES (#273). Adding the subsystem keys
    #     to the analysed set widens the family that gates which KEYS open -- but a run predating the
    #     instrumentation carries none of them, and dividing alpha by ABSENT keys would tighten the
    #     bar for the present ones. That is ABSENT-vs-ZERO wearing a correction's clothes, and it is
    #     the bug this arm exists to keep out.
    if omnibus_alpha(1) != OMNIBUS_ALPHA_FAMILYWISE:
        fails.append("omnibus_alpha(1) must be the familywise alpha itself")
    if not (omnibus_alpha(8) > omnibus_alpha(22) > 0):
        fails.append("the omnibus alpha did not tighten as more keys are tested")
    if abs(omnibus_alpha(8) - OMNIBUS_ALPHA_FAMILYWISE / 8) > 1e-12:
        fails.append("omnibus_alpha is not the familywise alpha divided by the key count")

    # 13. THE ANALYSED SET AND obs-join's DECLARED SET MUST NOT DRIFT. Two lists naming the same keys
    #     is exactly the shape that rots; assert they agree rather than trusting they do.
    import importlib.util as _iu
    _s = _iu.spec_from_file_location("oj", os.path.join(HERE, "obs-join.py"))
    _oj = _iu.module_from_spec(_s)
    try:
        _s.loader.exec_module(_oj)
        joined = {k[:-len(".us")] + ".wall_fraction" for k in _oj.SUBSYSTEM_KEYS}
        if joined != set(SUBSYSTEM):
            missing = sorted(joined - set(SUBSYSTEM)); extra = sorted(set(SUBSYSTEM) - joined)
            fails.append(f"obs-join and lever-cpu disagree on the subsystem set: "
                         f"only-in-join {missing}, only-in-lever-cpu {extra}")
    except Exception as e:
        fails.append(f"could not cross-check obs-join's SUBSYSTEM_KEYS: {e}")

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
    print(f"  lever_cpu selftest: {ARMS}/{ARMS} arms ok (a delta far below the floor is refused, an "
          f"outlier repeat is refused, the CPU null control is absent by construction and is never the "
          f"GPU one, the floor takes the pooled LSD or the baseline range whichever binds, a shift "
          f"shared by EVERY lever is suppressed by the omnibus, a "
          f"clean effect still resolves through it, F and t match published table values, the "
          f"correction widens with the comparison count, a delta under the MDE is a CANDIDATE and "
          f"not a cost, the multiplicity family is declared rather than inferred, the omnibus family "
          f"counts PRESENT keys not declared ones, obs-join and lever-cpu agree on the subsystem "
          f"set, VOID legs are "
          f"excluded without "
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
    return report(legs, null_control, voided, unreadable, args.run_dir, run_id)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
