#!/usr/bin/env python3
"""floor_reproduces -- re-derive every recorded noise floor from its own banked legs, on the
consumer's basis, and fail if any of them disagrees.

WHY THIS EXISTS. scripts/scene-floor.json is the threshold every A/B verdict is measured against:
leg-diff.py refuses to attribute a difference that does not clear it. But the floors were TYPED IN
from an analysis run elsewhere, and nothing connected a number to the legs that produced it. [#285]
is what that costs. Four floors were derived PER CALL (total/count) while leg-diff applies them PER
FRAME (total/frames) -- and because CV does not survive that change of denominator, four thresholds
were 2-3% too permissive for months. Nobody could see it, because the file recorded a number and a
sentence of prose, and prose does not get re-run.

WHAT IT CHECKS. For every (scene, key) in `scenes` and every (fixture, scene, key) in `fixtures`, it
loads the legs named in `_banks`, computes CV of total/frames across them, and compares to the
recorded figure at the precision the file stores (three decimals). It also asserts, for banks whose
legs carry meta.scene, that the legs agree with the key they are filed under -- the [#242] relabel
found two floors that were correctly measured and could never be found, because the scene key and
the bookmark name disagreed.

THE BASIS IS THE POINT, AND IT IS NOT CONFIGURABLE. total/frames is spelled out once here and once in
scene-floor.json's `_basis`, and both must match leg-diff.leg(). If a future change makes leg-diff
compute something else, this gate goes red -- which is the entire reason to have it, because the
alternative is what already happened: two derivations, one file, no record of which.

WHY IT SKIPS RATHER THAN PASSES WITHOUT THE LEGS. harness/ is gitignored (.gitignore:53) and holds no
tracked files, so on a fresh clone or in CI there is nothing to re-derive from. An absent verdict
spelled the same as a passing one is how a gate reports green from a machine that never had the data
-- the defect this project has hit repeatedly. Absent is exit 77 = SKIP, and run-gates.sh refuses to
count a SKIP as green.
"""

import importlib.util
import json
import pathlib
import statistics
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLOOR = ROOT / "scripts" / "scene-floor.json"
PROFILES = ROOT / "harness" / "profiles"

# The recorded figures are stored to three decimals, so agreement is agreement AT THAT PRECISION.
# This is a rounding allowance, NOT a tolerance: 0.0011 is half a unit in the last recorded place
# plus a hair for float noise. A real basis error moves these numbers by 2-4%, i.e. tens of units in
# the last place, so nothing that matters can hide under it.
EPS = 0.0011


def _leg_loader():
    spec = importlib.util.spec_from_file_location("_ld", ROOT / "scripts" / "leg-diff.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _banks(fl):
    """Every (label, scene, expected_floors, leg_names) the file declares."""
    b = fl["_banks"]
    out = [(f"scenes/{s}", s, fl["scenes"][s], names) for s, names in b["scenes"].items()]
    for fixture, scenes in b.get("fixtures", {}).items():
        for s, names in scenes.items():
            out.append((f"fixtures/{fixture}/{s}", s, fl["fixtures"][fixture][s], names))
    return out


def check(verbose=True):
    fl = json.loads(FLOOR.read_text())
    ld = _leg_loader()
    problems, checked = [], 0

    # EVERY RECORDED ENTRY MUST HAVE A BANK. A floor with no legs behind it cannot be re-derived, and
    # silently skipping it is how an unverifiable number stays in a verified file.
    declared = {f"scenes/{s}" for s in fl["scenes"]}
    declared |= {f"fixtures/{f}/{s}" for f, sc in fl.get("fixtures", {}).items() for s in sc}
    banked = {label for label, _, _, _ in _banks(fl)}
    for missing in sorted(declared - banked):
        problems.append(f"{missing}: recorded floors with NO bank in _banks -- cannot be re-derived")

    for label, scene, expected, names in _banks(fl):
        paths = [PROFILES / f"{n}.json" for n in names]
        absent = [p.name for p in paths if not p.exists()]
        if absent:
            print(f"floor_reproduces: SKIP -- {label} bank is absent: {' '.join(absent)}")
            print("floor_reproduces: harness/ is gitignored and holds no tracked files; nothing was")
            print("                  re-derived. This is NOT a pass.")
            return 77

        legs = [ld.leg(str(p)) for p in paths]

        # THE LEGS MUST AGREE WITH THE KEY THEY ARE FILED UNDER, where they can say so at all.
        recorded_scenes = {l[0] for l in legs if l[0] is not None}
        if recorded_scenes and recorded_scenes != {scene}:
            problems.append(f"{label}: legs record scene {sorted(recorded_scenes)}, filed under {scene!r}")

        # AND THEY MUST AGREE ON THE FIXTURE, for the same reason: a walking leg under a still key
        # would hand a moving bar to standing comparisons ([#286]).
        want_fixture = label.split("/")[1] if label.startswith("fixtures/") else "still"
        got_fixtures = {l[4] for l in legs}
        if got_fixtures != {want_fixture}:
            problems.append(f"{label}: legs are {sorted(got_fixtures)} fixtures, filed under {want_fixture!r}")

        for key, rec in expected.items():
            vals = [l[1][key] for l in legs if key in l[1]]
            if len(vals) != len(legs):
                problems.append(f"{label} {key}: present in {len(vals)}/{len(legs)} legs -- not re-derivable")
                continue
            got = round(statistics.stdev(vals) / statistics.mean(vals) * 100, 3)
            checked += 1
            if abs(got - rec) >= EPS:
                problems.append(f"{label} {key}: recorded {rec}, re-derives to {got} "
                                f"({(got - rec) / rec * 100:+.1f}%)")
            elif verbose:
                print(f"  ok  {label:<34s} {key:<32s} {got:8.3f}%")

    if problems:
        print(f"\nfloor_reproduces: FAIL -- {len(problems)} floor(s) do not reproduce from their own legs")
        for p in problems:
            print(f"  {p}")
        print("\nThe floors are the thresholds every A/B verdict clears. A floor that cannot be")
        print("re-derived from the legs it names is a bar of unknown provenance. Re-derive it on the")
        print("basis declared at scene-floor.json's `_basis`, or correct the bank.")
        return 1

    print(f"floor_reproduces: {checked} floors re-derive exactly from their own banked legs")
    return 0


def selftest():
    """The gate must FAIL on a floor that does not reproduce, and on a bank that is not the legs it
    claims. A gate nobody has watched fail is a gate whose green means nothing ([#194], [#223])."""
    import copy
    import tempfile
    arms = []

    def arm(label, ok):
        arms.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    fl = json.loads(FLOOR.read_text())
    ld = _leg_loader()
    names = fl["_banks"]["scenes"]["04-Ocean-Factory"]
    if not all((PROFILES / f"{n}.json").exists() for n in names):
        print("floor_reproduces --selftest: SKIP -- no banked legs to build the injection from")
        return 77

    legs = [ld.leg(str(PROFILES / f"{n}.json")) for n in names]
    K = "lighting.gpu.cpu_cost.us"
    vals = [l[1][K] for l in legs]
    perframe = round(statistics.stdev(vals) / statistics.mean(vals) * 100, 3)

    # 1. THE BASIS DISTINCTION IS REAL, AND THIS IS THE ARM THAT PROVES IT. If per-call and per-frame
    #    agreed, [#285] would have been unfalsifiable and this gate would be theatre.
    ms = [json.loads((PROFILES / f"{n}.json").read_text())["metrics"] for n in names]
    percall = [m[K]["total"] / m[K]["count"] for m in ms]
    percall_cv = round(statistics.stdev(percall) / statistics.mean(percall) * 100, 3)
    arm(f"per-call ({percall_cv}%) and per-frame ({perframe}%) are different numbers",
        abs(percall_cv - perframe) >= EPS)

    # 2. AND THE PER-CALL VALUE IS THE ONE THAT WAS RECORDED -- the finding, re-asserted as a test so
    #    that a future edit "correcting" 3.735 back to 3.613 goes red instead of looking tidier.
    arm("the pre-fix recorded figure 3.613 is the per-call CV, not the per-frame one",
        abs(percall_cv - 3.613) < EPS and abs(perframe - 3.735) < EPS)

    # 3. A WRONG FLOOR MUST FAIL. Injected via a temporary copy of the file, so the check under test
    #    is the real one reading a real (wrong) input.
    bad = copy.deepcopy(fl)
    bad["scenes"]["04-Ocean-Factory"][K] = round(perframe + 0.5, 3)
    orig = FLOOR.read_text()
    try:
        FLOOR.write_text(json.dumps(bad, indent=2))
        arm("a floor off by 0.5pp is REFUSED", check(verbose=False) == 1)
    finally:
        FLOOR.write_text(orig)

    # 4. A FLOOR WITH NO BANK MUST FAIL, rather than being quietly not-checked.
    bad2 = copy.deepcopy(fl)
    bad2["_banks"]["scenes"].pop("ship")
    try:
        FLOOR.write_text(json.dumps(bad2, indent=2))
        arm("a recorded scene with no bank is REFUSED, not skipped", check(verbose=False) == 1)
    finally:
        FLOOR.write_text(orig)

    # 5. AND THE UNMODIFIED FILE MUST PASS -- the null control. Arms 3 and 4 only mean something if
    #    the gate is not simply always red.
    arm("the file as committed passes", check(verbose=False) == 0)

    print(f"floor_reproduces selftest: {sum(arms)}/{len(arms)} arms ok")
    return 0 if all(arms) else 1


if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] == "--selftest":
            sys.exit(selftest())
        print(f"unknown argument {sys.argv[1]!r}; expected --selftest or nothing", file=sys.stderr)
        sys.exit(2)
    sys.exit(check())
