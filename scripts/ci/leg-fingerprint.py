#!/usr/bin/env python3
"""Every NEW profile leg must record which asset chain it measured (#277).

    scripts/ci/leg-fingerprint.py             # check
    scripts/ci/leg-fingerprint.py --selftest  # prove both arms fire

A RATCHET, NOT A REQUIREMENT, AND THE REASON IS NOT CONVENIENCE. 472 legs are banked without the
field, and their chain CANNOT BE RECOVERED -- the information was never written. Two dishonest
options were available and both are refused here:

  * Requiring the field outright makes a gate that can never pass until someone deletes 472 legs of
    real evidence to buy green. A check that cannot pass is worth no more than one that cannot fail
    (scripts/assert-binary-fresh.sh already carries that ruling).
  * Back-stamping the banked legs with today's hash asserts something known to be FALSE: Steam
    updated three mods inside harness/sbinit-perf.config at 08:34 on 2026-08-22, so the chain those
    legs ran on provably differs from the chain now on disk. That is not backfill, it is fabrication
    of provenance -- the exact failure [#263] found as 47 phantom evidence rows.

So the count of legs LACKING the field may not GROW. Old legs stay honestly unrecorded; a new leg
written without a fingerprint pushes the count over the ceiling and turns the gate red. The ceiling
falls only if someone deletes old legs, which is fine -- it is a ceiling, not a target.
"""
import glob
import json
import os
import sys

# Measured 2026-08-22, immediately after wiring render-profile.sh. Every leg banked before that
# commit; none can be repaired. Lower it freely, never raise it without saying which run added one.
CEILING = 472

LEGDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "harness", "profiles")


def legs(directory=None):
    """-> [path] of LEG jsons. .series.json and .joined.json are different artifacts with different
    shapes; counting them would ratchet a unit the thing this gate names cannot move."""
    d = directory or LEGDIR
    return sorted(f for f in glob.glob(os.path.join(d, "*.json"))
                  if not f.endswith((".series.json", ".joined.json")))


def unrecorded(paths):
    """-> [path] whose meta carries no usable assetFingerprint.

    MISSING counts as unrecorded on purpose. asset_fingerprint() prints MISSING:<paths> and exits
    non-zero when a source is absent, and render-profile.sh stores that verbatim -- which is a real
    and useful state to see in a leg, but it is not a chain identity and must not satisfy a gate
    asking which chain ran."""
    out = []
    for p in paths:
        try:
            fp = json.load(open(p)).get("meta", {}).get("assetFingerprint")
        except Exception:
            out.append(p)
            continue
        if not fp or str(fp).startswith("MISSING"):
            out.append(p)
    return out


def check(directory=None, ceiling=CEILING):
    paths = legs(directory)
    if not paths:
        print("leg_fingerprint: NOT RUN -- no profile legs on disk. harness/ is gitignored, so a "
              "fresh clone and a CI runner have nothing to check. Nothing was verified.")
        return 77
    miss = unrecorded(paths)
    if len(miss) > ceiling:
        print(f"leg_fingerprint: FAIL -- {len(miss)} of {len(paths)} leg(s) record no asset chain, "
              f"ceiling {ceiling}. A NEW leg was written without one, which means render-profile.sh "
              f"stopped passing --asset-fingerprint or telemetry-window.py stopped writing it. The "
              f"leg cannot say what content it measured and never will.")
        for p in sorted(miss, key=os.path.getmtime)[-5:]:
            print(f"    newest unrecorded: {os.path.basename(p)}")
        return 1
    print(f"leg_fingerprint: {len(paths) - len(miss)}/{len(paths)} leg(s) record their asset chain; "
          f"{len(miss)} unrecorded <= ceiling {ceiling} (banked before the field existed)")
    return 0


def selftest():
    import tempfile
    arms = []

    def arm(label, got, want):
        arms.append((label, got == want))

    with tempfile.TemporaryDirectory() as d:
        def write(name, meta):
            json.dump({"label": name, "meta": meta, "metrics": {}},
                      open(os.path.join(d, name + ".json"), "w"))

        # 1. An empty corpus SKIPS rather than passing. Without this the gate reads green on any
        #    machine that has never run a leg -- including CI, where it would be permanently green
        #    and permanently meaningless.
        arm("an empty corpus skips rather than passes", check(d, ceiling=0), 77)

        # 2. A recorded leg passes at ceiling 0. The positive control: without it the arms below
        #    prove only that the gate can fail.
        write("good", {"assetFingerprint": "82514b4583a94c9b"})
        arm("a leg carrying a fingerprint passes at ceiling 0", check(d, ceiling=0), 0)

        # 3. A leg with no fingerprint breaks a zero ceiling.
        write("bare", {})
        arm("an unrecorded leg exceeds a zero ceiling", check(d, ceiling=0), 1)

        # 4. ...and is tolerated below the ceiling, which is what lets 472 banked legs coexist with
        #    a gate that still catches the 473rd.
        arm("an unrecorded leg is tolerated under the ceiling", check(d, ceiling=1), 0)

        # 5. MISSING IS NOT AN IDENTITY. asset_fingerprint() emits MISSING:<paths> when a source is
        #    absent; storing that is honest but it does not say which chain ran, and a gate that
        #    accepted it would let a leg with a broken chain look recorded.
        write("missing", {"assetFingerprint": "MISSING:/gone/contents.pak"})
        arm("a MISSING sentinel does not count as recorded", check(d, ceiling=1), 1)

        # 6. Sibling artifacts must not be counted. .series.json and .joined.json have different
        #    shapes and no meta block; counting them would inflate the ratchet with a unit the thing
        #    it names cannot move -- and would have masked a real regression behind slack.
        for sib in ("x.series.json", "x.joined.json"):
            json.dump({"not": "a leg"}, open(os.path.join(d, sib), "w"))
        arm("series/joined siblings are not counted as legs", check(d, ceiling=1), 1)

    bad = sum(1 for _, ok in arms if not ok)
    for label, ok in arms:
        print(f"  [{'ok' if ok else 'FAIL'}] {label}")
    if bad:
        print(f"leg_fingerprint: SELFTEST FAIL -- {bad} of {len(arms)} arm(s)")
        return 1
    print(f"leg_fingerprint selftest: {len(arms)}/{len(arms)} arms ok -- empty skips, recorded "
          f"passes, unrecorded trips the ceiling, MISSING is not an identity, siblings excluded")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv[1:] else check())
