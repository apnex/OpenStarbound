#!/usr/bin/env python3
"""banked_corpus_reread -- re-window the banked evidence and assert the consumer still agrees.

WHY THIS EXISTS. `telemetry-window.py` is the only thing that turns snapshot pairs into the numbers
this project reasons with, and [#245] is about to change how it computes. A change there cannot be
judged by the gate set: every other gate reads the SOURCE, and this one reads the ANSWERS. Without it,
"the consumer now honours the declared unit" and "the consumer now reports different numbers for the
same input" are indistinguishable.

WHAT IT COMPARES. For each leg of the pinned run it re-runs the consumer over that leg's own
`.snapshots` directory, with that leg's own `meta.intervals` and `label`, and diffs the result against
the banked leg JSON. Everything is compared: window, metrics, owners, zeroed, violations, label, and
meta minus a DECLARED exclusion set.

THE EXCLUSION SET IS FOUR KEYS AND IT IS NOT A TOLERANCE. gpuClockMhzStart/End and
packageTempCStart/End are hardware probes that `render-profile.sh` writes INTO the leg file after the
consumer has produced it -- the consumer never sets them, so they cannot round-trip and their absence
is not a disagreement. Every other meta key IS compared. The list is a closed literal rather than a
prefix or a regex, because a normaliser that can grow is how a comparison gate stops comparing: the
next field that fails to round-trip must fail loudly and be argued about, not be quietly absorbed.

WHY IT SKIPS RATHER THAN PASSES WITHOUT THE CORPUS. `harness/` is gitignored (.gitignore:53) and
holds ZERO tracked files, so this gate has no corpus on a fresh clone or in CI. That is exactly the
shape that has burned this project: a gate whose absent verdict is spelled the same as its passing
verdict reports green from a machine that never had the data. Absent is exit 77 = SKIP, and
run-gates.sh refuses to count a SKIP as green.

WHY THE RUN ID IS PINNED. Nine of the ten banked matrix runs retain no `.snapshots` directories at
all -- they banked the consumer's OUTPUT and discarded its INPUT, and can never be re-windowed. This
run is the only re-readable corpus that exists, its 599 files are hashed into
docs/evidence/matrix-20260808-140430.manifest.md, and a read-only copy sits outside the tree. Pinning
the id here means a second run appearing does not silently change what this gate certifies.
"""

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
WINDOW = REPO / "scripts" / "telemetry-window.py"

# The one run that kept its inputs. See the module docstring.
PINNED_RUN = "matrix-20260808-140430"
CORPUS = REPO / "harness" / "matrix" / PINNED_RUN

# Written by render-profile.sh AFTER the consumer produces the file; the consumer never sets them.
# A closed literal on purpose -- see the docstring.
META_NOT_ROUND_TRIPPED = ("gpuClockMhzStart", "gpuClockMhzEnd",
                          "packageTempCStart", "packageTempCEnd")


def legs():
    """-> [(label, snapshots_dir, banked_json)] for every leg that retains its inputs."""
    if not CORPUS.is_dir():
        return []
    out = []
    for snaps in sorted(CORPUS.glob("*.snapshots")):
        banked = snaps.with_suffix(".json")
        if banked.is_file():
            out.append((snaps.name[:-len(".snapshots")], snaps, banked))
    return out


def compare(banked, fresh):
    """-> [str] of disagreements. Empty means the consumer reproduced the banked answer."""
    bad = []
    for key in sorted(set(banked) | set(fresh)):
        if key == "meta":
            continue
        if banked.get(key) != fresh.get(key):
            bad.append("%s differs" % key)
    mb, mf = banked.get("meta", {}), fresh.get("meta", {})
    for key in sorted(set(mb) | set(mf)):
        if key in META_NOT_ROUND_TRIPPED:
            continue
        if mb.get(key) != mf.get(key):
            bad.append("meta.%s: banked %r, re-read %r" % (key, mb.get(key), mf.get(key)))
    return bad


def reread(label, snaps, intervals, tmp):
    """Run the consumer over one leg exactly as the harness ran it. -> parsed result or None."""
    out = pathlib.Path(tmp) / (label + ".json")
    r = subprocess.run([sys.executable, str(WINDOW), str(snaps),
                        "--intervals", str(intervals), "--label", label, "--json", str(out)],
                       capture_output=True, text=True)
    if r.returncode != 0 or not out.is_file():
        return None, (r.stderr or r.stdout).strip().splitlines()[-1:] or ["no output"]
    return json.loads(out.read_text()), None


def run():
    found = legs()
    if not found:
        print("banked_corpus_reread: NOT RUN -- no re-readable corpus at %s" % CORPUS)
        print("banked_corpus_reread: harness/ is gitignored and holds no tracked files, so this gate")
        print("banked_corpus_reread: has no inputs here. Nothing was compared; this is NOT a pass.")
        return 77

    bad_legs, checked = [], 0
    with tempfile.TemporaryDirectory() as tmp:
        for label, snaps, banked_path in found:
            banked = json.loads(banked_path.read_text())
            intervals = banked.get("meta", {}).get("intervals")
            if intervals is None:
                bad_legs.append((label, ["banked leg states no meta.intervals -- cannot re-run it "
                                         "the way it was run"]))
                continue
            fresh, err = reread(label, snaps, intervals, tmp)
            if fresh is None:
                bad_legs.append((label, ["consumer failed: " + "; ".join(err)]))
                continue
            checked += 1
            diffs = compare(banked, fresh)
            if diffs:
                bad_legs.append((label, diffs))

    for label, diffs in bad_legs:
        print("  %s" % label)
        for d in diffs[:6]:
            print("      %s" % d)
        if len(diffs) > 6:
            print("      ... and %d more" % (len(diffs) - 6))

    print("banked_corpus_reread: %d leg(s) re-read from %s, %d disagreed"
          % (checked, PINNED_RUN, len(bad_legs)))
    if bad_legs:
        print("banked_corpus_reread: FAIL -- the consumer no longer reproduces banked evidence. If "
              "that is INTENDED, the change is a re-measurement and the corpus must be re-captured; "
              "it is not a refactor.")
        return 1
    print("banked_corpus_reread: OK -- every banked leg reproduces, excluding only the four "
          "hardware-probe meta keys the consumer never writes")
    return 0


def selftest():
    """Prove the comparison FIRES, and that the exclusion set cannot absorb a real difference."""
    arms, bad = [], 0

    def arm(name, ok):
        nonlocal bad
        arms.append((name, ok))
        if not ok:
            bad += 1

    base = {"label": "L", "window": {"seconds": 1.0}, "metrics": {"a": {"count": 1}},
            "owners": {}, "zeroed": [], "violations": [],
            "meta": {"schema": 4, "intervals": 15, "gpuClockMhzStart": 900}}

    import copy
    arm("identical compares clean", compare(base, copy.deepcopy(base)) == [])

    m = copy.deepcopy(base); m["metrics"]["a"]["count"] = 2
    arm("a changed metric FIRES", compare(base, m) != [])

    o = copy.deepcopy(base); o["owners"] = {"frame": {"totals": {}}}
    arm("a changed owner FIRES", compare(base, o) != [])

    v = copy.deepcopy(base); v["violations"] = ["something"]
    arm("a new violation FIRES", compare(base, v) != [])

    z = copy.deepcopy(base); z["zeroed"] = ["a"]
    arm("a changed zeroed set FIRES", compare(base, z) != [])

    e = copy.deepcopy(base); e["meta"]["gpuClockMhzStart"] = 2350
    arm("an EXCLUDED meta key is absorbed", compare(base, e) == [])

    # The arm that matters most: the exclusion set is closed, so a NEIGHBOURING meta key that also
    # fails to round-trip must fail loudly rather than be quietly covered by a prefix match.
    n = copy.deepcopy(base); n["meta"]["intervals"] = 14
    arm("a NON-excluded meta key FIRES", compare(base, n) != [])
    n2 = copy.deepcopy(base); n2["meta"]["gpuClockMhzMiddle"] = 1
    arm("an unlisted meta key that merely LOOKS excluded still FIRES", compare(base, n2) != [])

    for name, ok in arms:
        print("  %-58s %s" % (name, "ok" if ok else "FAILED"))
    if bad:
        print("banked_corpus_reread: SELFTEST FAIL -- %d arm(s)" % bad)
        return 1
    print("banked_corpus_reread: selftest OK -- %d arms; a real difference fires and only the four "
          "declared keys are absorbed" % len(arms))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="prove the comparison fires; needs no corpus")
    args = ap.parse_args(argv)
    return selftest() if args.selftest else run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
