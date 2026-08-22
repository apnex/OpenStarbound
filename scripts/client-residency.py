#!/usr/bin/env python3
"""Ratchet the render harness's residency in STOCK client files (#237).

WHY THIS EXISTS. The harness is not a module; it is an occupant. It lives inside
source/client/StarClientApplication, which is upstream's file, and it therefore inherits state it never
asked for -- the player save's persisted position decides which scene gets measured, the settle criterion
is written against the client's world-load lifecycle, and a run mutates the save it measures. All three
produced wrong numbers on 2026-08-06 and all three are now guarded by ASSERTIONS, which is exactly the
tell: detection was needed because nothing prevented it.

#237 proposes the fix -- a measurement client as its own entrypoint. It is PARKED. #199 has the
boundary_ratchet keeping its decision revisitable from data; this one had nothing, so the number it turns
on would drift upward in silence and the eventual extraction would quietly get bigger. A parked decision
with no instrument is a decision that gets made by accretion.

TWO CEILINGS PER FILE, AND THE SECOND ONE IS THE INTERESTING ONE.

  harness lines  -- lines matching the harness vocabulary. Directly the quantity #237 is about.
  total lines    -- the whole file.

The total is not padding. A ratchet on a GREP is only as good as its word list, and this project has been
burned three times by a gate that stayed green while naming the wrong thing (oracle_vocabulary_trap,
gate_vocabulary_outlives_document). Harness code written with new vocabulary would leave the marker count
flat while the file grew -- invisible to the first ceiling and caught by the second. Neither number alone
is trustworthy; the pair is.

WHAT THIS DELIBERATELY DOES NOT DO. It does not diff against origin/main. That measures our TOTAL
divergence (957 lines across these two files today, vs 213 harness-marker lines) which is a superset
including every unrelated client change, and it needs a ref that a shallow CI checkout may not have -- a
gate that skips in CI is a gate nobody runs. The divergence is reported when the ref happens to exist,
as information, never as a verdict.
"""

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The harness vocabulary. Kept explicit and PRINTED on every run: a word list nobody can see is a word
# list nobody can notice is wrong.
MARKERS = re.compile(r"renderTest|RenderTest|rendertest|walkoracle")

# STOCK files that host harness code, with their ceilings. A file appears here because upstream owns it
# and we are resident in it; the fix is to leave, not to raise the numbers.
#   path: (max harness lines, max total lines)
#
# RAISED ONCE, 185 -> 186, to restore the SHIP as a nameable scene (#277). The ship is the only fixture
# where the parallax pass costs exactly zero, so it validates a pass the other four scenes cannot, and
# the five-scene matrix could not run without it. Recorded rather than absorbed: this is the second time
# #237 has been deferred, and the trade bought a whole scene for one line because the same commit made
# the two warp paths share one load-phase reset instead of keeping two copies of it.
BUDGET = {
    "source/client/StarClientApplication.cpp": (186, 2270),
    "source/client/StarClientApplication.hpp": (40, 310),
}


def measure(rel):
    path = os.path.join(REPO, rel)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    return sum(1 for ln in lines if MARKERS.search(ln)), len(lines)


def divergence(rel):
    """Our added lines vs origin/main, when that ref exists. INFORMATION, never a verdict."""
    try:
        out = subprocess.run(["git", "diff", "--numstat", "origin/main", "--", rel],
                             cwd=REPO, capture_output=True, text=True, timeout=30)
        if out.returncode != 0 or not out.stdout.strip():
            return None
        return int(out.stdout.split()[0])
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def main():
    print("client-residency: harness vocabulary /%s/" % MARKERS.pattern)
    failures = []
    for rel, (max_harness, max_total) in sorted(BUDGET.items()):
        m = measure(rel)
        if m is None:
            # A budgeted file that has vanished is not a pass. It moved, or it was renamed, and either
            # way the ceiling now guards nothing.
            print("  %-42s MISSING -- budgeted but not in the tree" % rel)
            failures.append("%s is missing" % rel)
            continue
        harness, total = m
        div = divergence(rel)
        div_s = ("  [+%d vs origin/main]" % div) if div is not None else "  [no origin/main -- not compared]"
        flag = ""
        if harness > max_harness:
            failures.append("%s: %d harness lines, ceiling %d" % (rel, harness, max_harness))
            flag = "  <-- HARNESS OVER"
        if total > max_total:
            failures.append("%s: %d total lines, ceiling %d" % (rel, total, max_total))
            flag += "  <-- TOTAL OVER"
        print("  %-42s harness %4d/%-4d  total %5d/%-5d%s%s"
              % (rel, harness, max_harness, total, max_total, div_s, flag))

    if failures:
        print()
        print("client-residency: FAIL -- the harness grew inside a stock client file.")
        for f in failures:
            print("    %s" % f)
        print()
        print("  Raising a ceiling is a decision, not a formality: it says the measurement client (#237)")
        print("  is worth deferring again. Do that deliberately, or move the code out.")
        return 1

    print("client-residency: OK -- %d stock file(s) within budget. #237 is parked, and now visibly so."
          % len(BUDGET))
    return 0


def selftest():
    """Both ceilings must FIRE, and the missing-file arm must not read as a pass."""
    global BUDGET
    saved = BUDGET
    arms = 0
    ok = 0

    def arm(desc, cond):
        nonlocal arms, ok
        arms += 1
        ok += 1 if cond else 0
        print("  %-4s %s" % ("ok" if cond else "FAIL", desc))

    real = sorted(saved)[0]
    h, t = measure(real)

    BUDGET = {real: (h, t)}
    arm("exactly at both ceilings passes", main() == 0)

    BUDGET = {real: (h - 1, t)}
    arm("one harness line over the ceiling fails", main() == 1)

    BUDGET = {real: (h, t - 1)}
    arm("one total line over the ceiling fails -- the vocabulary-blind backstop", main() == 1)

    BUDGET = {"source/client/NoSuchFile.cpp": (1, 1)}
    arm("a budgeted file that is missing fails rather than passing vacuously", main() == 1)

    BUDGET = saved
    print()
    print("client-residency selftest: %d/%d arms ok" % (ok, arms))
    return 0 if ok == arms else 1


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else main())
