#!/usr/bin/env python3
"""Difference two sets of profile legs, and REFUSE when the difference is below the scene's noise.

    scripts/leg-diff.py --a legA1.json legA2.json --b legB1.json legB2.json [--key K ...]
    scripts/leg-diff.py --pair-group gputax          # legs tagged by render-ab.sh
    scripts/leg-diff.py --selftest

WHY THIS EXISTS (#279). On 2026-08-22, two comparisons ran on the same hardware within an hour:

    PAIRED   ([#276], on1-off1 and on2-off2)   the two paired differences agreed to 0.2%,
                                               against within-arm drift of 4.4-4.8%
    UNPAIRED (epoch-0, n=1 vs n=2 cross-run)   "content addition made the frame 18.9% FASTER"

The second is not a result. It is the noise floor wearing a percentage sign, and NOTHING STOPPED IT
BEING WRITTEN DOWN -- I wrote it down, in a report, before checking the control. Two legs subtracted
by hand look exactly like a designed A/B once they reach prose.

THE FLOOR IS MEASURED, NOT INVENTED. scripts/scene-floor.json carries per-scene per-key leg-to-leg CV
from a 12-leg probe (docs/evidence/scene-stability.md). Even the best scene, Desert-Town, sits at
~6.6% on lighting.produce.entities.us -- larger than most lever effects this project has confirmed.

PAIRING IS THE POINT, NOT A NICETY. An unpaired comparison must clear cv*sqrt(1/na + 1/nb) scaled by
1.96; a paired one differences legs that share their between-leg noise and is not bound by that floor
at all. This tool reports which mode it used and applies the corresponding bar, so "we paired it"
becomes a property of the artifact rather than a claim in a sentence.
"""
import argparse
import glob
import json
import os
import statistics as st
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLOORS = os.path.join(REPO, "scripts", "scene-floor.json")
Z95 = 1.959964


def load_floors(path=None):
    return json.load(open(path or FLOORS))


def leg(path):
    """-> (scene, per-frame metric dict, fingerprint, pair). Per FRAME, because a leg's window length
    is not guaranteed identical and a raw total silently encodes duration."""
    d = json.load(open(path))
    m = d.get("metrics", d)
    fr = m.get("cpu.frame.total.us", {}).get("count") or 0
    meta = d.get("meta", {})
    vals = {k: v["total"] / fr for k, v in m.items()
            if isinstance(v, dict) and isinstance(v.get("total"), (int, float)) and fr}
    return meta.get("scene"), vals, meta.get("assetFingerprint"), meta.get("pair")


def compare(a_paths, b_paths, keys=None, floors=None, paired=None, scene=None):
    """-> (rows, problems). Rows are dicts; problems abort attribution entirely."""
    fl = floors if floors is not None else load_floors()
    problems = []
    A = [leg(p) for p in a_paths]
    B = [leg(p) for p in b_paths]
    if not A or not B:
        return [], ["no legs on one side"]

    # THE CHAIN MUST MATCH. Differencing across asset chains compares two contents, not two code
    # paths -- the [#277] failure arriving through the consumer instead of the runner.
    fps = {x[2] for x in A + B}
    if len(fps) > 1:
        problems.append(f"legs span {len(fps)} asset chains {sorted(map(str, fps))} -- the difference "
                        f"is between contents, not between arms. Not attributable.")
    if None in fps:
        problems.append("at least one leg records NO asset chain (banked before #277). Its content "
                        "cannot be established, so it cannot be differenced against a leg that can.")

    scenes = {x[0] for x in A + B if x[0]}
    if scene is None:
        if len(scenes) == 1:
            scene = scenes.pop()
        elif len(scenes) > 1:
            problems.append(f"legs span {len(scenes)} scenes {sorted(scenes)} -- different fixtures "
                            f"have different noise and different content. Not attributable.")
        else:
            problems.append("no leg records its scene, so no floor can be selected. An unmeasured "
                            "floor and a floor of zero give opposite verdicts; refusing.")

    # PAIRING IS DECLARED, NOT GUESSED. Inferring it from equal counts would call two unrelated legs
    # a pair whenever the arms happened to be the same size -- exactly the mistake this tool exists
    # to prevent, automated.
    if paired is None:
        pa = [x[3] for x in A]
        pb = [x[3] for x in B]
        paired = (len(A) == len(B) and all(pa) and all(pb)
                  and [p.get("group") for p in pa] == [p.get("group") for p in pb]
                  and [p.get("index") for p in pa] == [p.get("index") for p in pb])

    if problems:
        return [], problems

    sfl = fl["scenes"].get(scene)
    if sfl is None:
        return [], [f"scene {scene!r} has no measured floor in scene-floor.json. Measure it "
                    f"(4 legs minimum) before differencing anything at it."]
    insensitive = fl.get("_insensitive", {})

    keys = keys or [k for k in sorted(set(A[0][1]) & set(B[0][1]))]
    rows = []
    for k in keys:
        av = [x[1].get(k) for x in A]
        bv = [x[1].get(k) for x in B]
        if any(v is None for v in av + bv):
            rows.append({"key": k, "verdict": "ABSENT", "note": "not present in every leg"})
            continue
        ma, mb = st.mean(av), st.mean(bv)
        if ma == 0:
            rows.append({"key": k, "verdict": "ABSENT", "note": "baseline arm is zero"})
            continue
        pct = 100.0 * (mb - ma) / ma
        if k in insensitive:
            rows.append({"key": k, "delta_pct": pct, "verdict": "INSENSITIVE",
                         "note": "pacing-bound; its tiny floor is insensitivity, not precision"})
            continue
        cv = sfl.get(k)
        if cv is None:
            rows.append({"key": k, "delta_pct": pct, "verdict": "NO FLOOR",
                         "note": f"no measured floor for this key at {scene}"})
            continue
        if paired:
            # Paired legs share their between-leg noise, so the floor does not apply. What bounds a
            # paired result is the SPREAD OF ITS OWN PAIRED DIFFERENCES -- [#276]'s agreed to 0.2%.
            diffs = [100.0 * (b - a) / a for a, b in zip(av, bv)]
            spread = (max(diffs) - min(diffs)) if len(diffs) > 1 else None
            rows.append({"key": k, "delta_pct": pct, "verdict": "PAIRED",
                         "pair_spread_pp": spread,
                         "note": "floor not applicable; read the pair spread"
                                 + ("" if spread is not None else " (n=1 pair: no spread to read)")})
        else:
            bar = Z95 * cv * (1.0 / len(av) + 1.0 / len(bv)) ** 0.5
            rows.append({"key": k, "delta_pct": pct, "bar_pct": bar,
                         "verdict": "RESOLVED" if abs(pct) > bar else "BELOW FLOOR",
                         "note": f"unpaired, cv {cv:.2f}% at {scene}, n={len(av)}v{len(bv)}"})
    return rows, []


def report(rows, problems, paired_hint=""):
    if problems:
        print("leg-diff: NOT ATTRIBUTABLE")
        for p in problems:
            print(f"    {p}")
        return 1
    print(f"leg-diff:{paired_hint}")
    for r in rows:
        d = f"{r['delta_pct']:+8.2f}%" if "delta_pct" in r else " " * 9
        b = f" bar {r['bar_pct']:5.2f}%" if "bar_pct" in r else ""
        s = (f" spread {r['pair_spread_pp']:.2f}pp"
             if r.get("pair_spread_pp") is not None else "")
        print(f"  {r['verdict']:<12s} {r['key']:<34s} {d}{b}{s}")
        if r["verdict"] in ("BELOW FLOOR", "NO FLOOR", "INSENSITIVE", "ABSENT"):
            print(f"       {r['note']}")
    return 0


def selftest():
    import tempfile
    arms = []

    def arm(label, ok):
        arms.append((label, ok))

    FL = {"scenes": {"S": {"k": 10.0}}, "_insensitive": {"paced": "insensitive"}}

    with tempfile.TemporaryDirectory() as d:
        def mk(name, k=100.0, scene="S", fp="CHAIN", pair=None, extra=None):
            m = {"cpu.frame.total.us": {"total": 1000, "count": 10},
                 "k": {"total": k * 10, "count": 10}}
            if extra: m.update({n: {"total": v * 10, "count": 10} for n, v in extra.items()})
            meta = {"scene": scene, "assetFingerprint": fp}
            if pair: meta["pair"] = pair
            p = os.path.join(d, name + ".json")
            json.dump({"label": name, "meta": meta, "metrics": m}, open(p, "w"))
            return p

        # 1. A difference INSIDE the floor is refused. cv 10%, n=1v1 -> bar = 1.96*10*sqrt(2) = 27.7%.
        #    A 15% difference is real-looking and must NOT resolve.
        a, b = mk("a", 100.0), mk("b", 115.0)
        r, _ = compare([a], [b], ["k"], FL, paired=False)
        arm("a 15% difference at a 27.7% unpaired bar reads BELOW FLOOR", r[0]["verdict"] == "BELOW FLOOR")

        # 2. ...and a large one resolves. Without this the tool could refuse everything and look safe.
        r, _ = compare([a], [mk("b2", 200.0)], ["k"], FL, paired=False)
        arm("a 100% difference clears the bar", r[0]["verdict"] == "RESOLVED")

        # 3. MORE LEGS LOWER THE BAR. n=4v4 -> 1.96*10*sqrt(0.5) = 13.9%, so the same 15% now resolves.
        #    This is the property that makes repeats worth paying for.
        A = [mk(f"a{i}", 100.0) for i in range(4)]
        B = [mk(f"b{i}", 115.0) for i in range(4)]
        r, _ = compare(A, B, ["k"], FL, paired=False)
        arm("n=4v4 lowers the bar so the same 15% resolves", r[0]["verdict"] == "RESOLVED")

        # 4. PAIRING IS DECLARED, NOT INFERRED FROM EQUAL COUNTS. Arm 3 had 4 v 4 and no pair tags;
        #    it must NOT have been treated as paired, or every equal-sized comparison silently
        #    escapes the floor -- the failure this tool exists to prevent, automated.
        arm("equal arm sizes alone do not count as paired", r[0]["verdict"] != "PAIRED")

        # 5. Declared pairs are honoured and report their own spread instead of the floor.
        PA = [mk(f"pa{i}", 100.0, pair={"group": "g", "index": i, "arm": "a"}) for i in range(2)]
        PB = [mk(f"pb{i}", 115.0, pair={"group": "g", "index": i, "arm": "b"}) for i in range(2)]
        r, _ = compare(PA, PB, ["k"], FL)
        arm("declared pairs read PAIRED and carry a spread",
            r[0]["verdict"] == "PAIRED" and r[0]["pair_spread_pp"] is not None)

        # 6. A MIXED ASSET CHAIN ABORTS EVERYTHING. This is [#277] arriving through the consumer:
        #    differencing across contents compares two worlds, not two code paths.
        _, probs = compare([a], [mk("bx", 115.0, fp="OTHER")], ["k"], FL, paired=False)
        arm("a mixed asset chain is not attributable", bool(probs))

        # 7. A leg with NO chain cannot be differenced against one that has it -- 472 legs banked
        #    before #277 are in exactly this state and must not silently participate.
        _, probs = compare([a], [mk("bn", 115.0, fp=None)], ["k"], FL, paired=False)
        arm("a leg with no recorded chain is refused", bool(probs))

        # 8. Mixed scenes abort: different fixtures have different noise AND different content.
        _, probs = compare([a], [mk("bs", 115.0, scene="T")], ["k"], FL, paired=False)
        arm("mixed scenes are not attributable", bool(probs))

        # 9. An unmeasured scene refuses rather than assuming zero noise.
        _, probs = compare([mk("u1", 100.0, scene="U")], [mk("u2", 115.0, scene="U")], ["k"], FL, paired=False)
        arm("a scene with no measured floor refuses", bool(probs))

        # 10. A PACING-BOUND KEY IS NEVER ATTRIBUTED. cpu.frame.total.us measures 0.004% CV because it
        #     is mostly vsync sleep; a floor that small would confirm anything. Insensitivity must not
        #     read as precision.
        pa2 = mk("pk1", 100.0, extra={"paced": 100.0})
        pb2 = mk("pk2", 115.0, extra={"paced": 100.5})
        r, _ = compare([pa2], [pb2], ["paced"], FL, paired=False)
        arm("a pacing-bound key reads INSENSITIVE, never RESOLVED", r[0]["verdict"] == "INSENSITIVE")

        # 11. A key with no floor at a known scene is named, not silently passed.
        r, _ = compare([pa2], [pb2], ["nosuchkey"], FL, paired=False)
        arm("a key absent from the legs is reported ABSENT", r[0]["verdict"] == "ABSENT")

    bad = sum(1 for _, ok in arms if not ok)
    for label, ok in arms:
        print(f"  [{'ok' if ok else 'FAIL'}] {label}")
    if bad:
        print(f"leg_diff: SELFTEST FAIL -- {bad} of {len(arms)} arm(s)")
        return 1
    print(f"leg_diff selftest: {len(arms)}/{len(arms)} arms ok -- the floor refuses, repeats lower "
          f"it, pairing is declared not inferred, and chain/scene/insensitivity all abort")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", nargs="*", default=[])
    ap.add_argument("--b", nargs="*", default=[])
    ap.add_argument("--pair-group", default=None)
    ap.add_argument("--key", action="append", default=None)
    ap.add_argument("--scene", default=None,
                    help="name the scene for legs banked before meta.scene existed. It selects the "
                         "floor, so naming the WRONG one silently swaps a 6.6%% bar for a 46%% one "
                         "-- state it only when you know it.")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    a, b = args.a, args.b
    if args.pair_group:
        legs = sorted(p for p in glob.glob(os.path.join(REPO, "harness/profiles/*.json"))
                      if not p.endswith((".series.json", ".joined.json")))
        a = [p for p in legs if (leg(p)[3] or {}).get("group") == args.pair_group
             and (leg(p)[3] or {}).get("arm") == "a"]
        b = [p for p in legs if (leg(p)[3] or {}).get("group") == args.pair_group
             and (leg(p)[3] or {}).get("arm") == "b"]
    if not a or not b:
        print("leg-diff: need --a and --b legs, or a --pair-group that matches some")
        return 2
    rows, problems = compare(a, b, args.key, scene=args.scene)
    hint = "" if problems else (f"  {len(a)} vs {len(b)} leg(s)")
    return report(rows, problems, hint)


if __name__ == "__main__":
    sys.exit(main())
