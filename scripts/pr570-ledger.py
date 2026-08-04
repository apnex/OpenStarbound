#!/usr/bin/env python3
"""Generate the PR-570 findings ledger, merging in the decisions we have taken.

WHY THIS IS AN INSTRUMENT AND NOT A DOCUMENT. The ledger is 155 rows of verdicts produced by three
analysis workflows. Transcribing them by hand is how a table comes to disagree with the analysis it
claims to summarise -- the defect this project spent two commits deleting from the TSSA. So the
table is emitted from the raw outputs, and the only hand-written input is the DECISIONS file.

    scripts/pr570-ledger.py                  # write the ledger
    scripts/pr570-ledger.py --check          # exit 1 if it is stale or a decision is dangling

WHERE STATUS LIVES, AND WHY NOT IN THE TABLE. A status column typed into the generated markdown is
destroyed by the next regeneration. So decisions live beside the instrument, exactly as LOCAL_COUNT,
SINGLE_DUTY and ELEMENT_FREE do for the spec gates, and the generator merges them in.

A DANGLING DECISION IS A HARD FAILURE. If a decision names a row id that no longer exists -- because
the analysis was re-run and the ids shifted -- the entry is silently excusing nothing, which is the
`gate-vocabulary-outlives-document` defect this project has now hit five times. It fails loudly.

DECLINED AND DEFERRED MUST CARRY THEIR REASON. `declined` without a reason is indistinguishable from
forgotten; `deferred` without a trigger is a backlog entry pretending to be a decision. Both are
rejected at generation time rather than being allowed to accumulate.

THE RAW OUTPUTS ARE DELIBERATELY OUTSIDE THE REPOSITORY. They are agent reports about third-party
unlicensed code and quote it as evidence, so they stay at /root/analysis/osb-pr570/raw/ with the
snapshot. That means this script regenerates the ledger on THIS machine and not from a clean clone,
and the ledger says so rather than implying otherwise.
"""
import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
RAW = pathlib.Path("/root/analysis/osb-pr570/raw")
DECISIONS = REPO / "docs/superpowers/drafts/osb-pr570-decisions.json"
LEDGER = REPO / "docs/superpowers/drafts/2026-08-04-osb-pr570-findings-ledger.md"

# Absent from the decisions file means OPEN. Everything else must be one of these.
STATUS = {
    "accepted": "work is committed to; `task` names where it is tracked",
    "declined": "we will not do this; `reason` is mandatory",
    "deferred": "not now; `until` names the trigger, and is mandatory",
    "done": "finished; `task` or `commit` says where",
}
NEEDS = {"declined": "reason", "deferred": "until"}


def result(name):
    d = json.loads((RAW / f"{name}.json").read_text())
    r = d["result"]
    return (json.loads(r) if isinstance(r, str) else r), d.get("logs", [])


def cell(s, n=150):
    s = re.sub(r"\s+", " ", str(s or "")).replace("|", "/").strip()
    return (s[: n - 1] + "…") if len(s) > n else s


STEPS_FIX = {
    ("real", "we-still-have-it"): "FIX · DEFER · ACCEPT-RISK",
    ("overstated", "we-still-have-it"): "FIX · DEFER · ACCEPT-RISK",
    ("real", "we-already-fixed-it"): "CLOSE",
    ("real", "not-applicable-to-us"): "CLOSE",
    ("overstated", "not-applicable-to-us"): "CLOSE",
    ("cannot-determine", "not-applicable-to-us"): "CLOSE",
    ("not-dead-at-all", "we-still-have-it"): "RE-EXAMINE · CLOSE",
}
STEPS_OVERLAP = {
    "we-are-ahead": "NO ACTION · DOCUMENT", "equivalent": "NO ACTION",
    "different-tradeoff": "NO ACTION · REVISIT if assumptions change",
    "they-are-ahead": "ADOPT the idea · DECLINE · DEFER",
    "we-lack-entirely": "BUILD it · DECLINE · DEFER",
}
STEPS_AXIS = {"ours": "NO ACTION", "tie": "NO ACTION",
              "cannot-determine": "MEASURE · ACCEPT unknown",
              "theirs": "CLOSE THE GAP · DECLINE · DEFER"}


def build():
    RF, RF_LOGS = result("wtdd932ww")
    H2, _ = result("w59rxgngb")
    OV, _ = result("wer36jcl5")

    dropped = []
    for l in RF_LOGS:
        if l.startswith("CAPPED:"):
            dropped = [x.strip() for x in l.split("dropped", 1)[1].split(",") if x.strip()]

    dec = json.loads(DECISIONS.read_text()) if DECISIONS.exists() else {}
    dec = {k: v for k, v in dec.items() if not k.startswith("_")}

    rows = []      # (sec, id, open?, item, verdict1, verdict2, steps)
    for i, c in enumerate(RF["checks"], 1):
        rows.append(("A", "A%02d" % i, c["our_status"] == "we-still-have-it",
                     "`%s` — %s" % (c["id"], cell(c["summary"], 170)),
                     c["was_it_real"], c["our_status"],
                     STEPS_FIX.get((c["was_it_real"], c["our_status"]), "DECIDE")))
    for i, s in enumerate(dropped, 1):
        rows.append(("B", "B%02d" % i, True, "`%s`" % cell(s, 80),
                     "not examined", "unknown", "CHECK · DROP"))
    n = 0
    for p in H2["pairs"]:
        lab = cell(p["pair"].split("—")[0], 60)
        for a in p.get("axes", []):
            n += 1
            rows.append(("C", "C%02d" % n, a["winner"] == "theirs",
                         "%s — %s" % (lab, a["axis"]), a["winner"], cell(a["why"], 200),
                         STEPS_AXIS.get(a["winner"], "DECIDE")))
    n = 0
    for t in OV["themes"]:
        th = cell(t["theme"].split("—")[0], 40)
        for o in t["overlap"]:
            n += 1
            rows.append(("D", "D%02d" % n, o["verdict"] in ("they-are-ahead", "we-lack-entirely"),
                         "%s — %s" % (th, cell(o["topic"], 100)), o["verdict"], "",
                         STEPS_OVERLAP.get(o["verdict"], "DECIDE")))
    for i, c in enumerate(OV["confirmed"], 1):
        rows.append(("E", "E%02d" % i, True, cell(c["idea"], 200), "survived", "",
                     "ADOPT · DEFER · DECLINE"))
    for i, c in enumerate(OV["refuted"], len(OV["confirmed"]) + 1):
        rows.append(("E", "E%02d" % i, False, cell(c["idea"], 160), "refuted",
                     cell(c.get("reasoning"), 190), "CLOSE · CHALLENGE the refutation"))

    ids = {r[1] for r in rows}
    dangling = sorted(set(dec) - ids)
    if dangling:
        raise SystemExit("pr570-ledger: FAIL -- decisions name %d row(s) that do not exist: %s. "
                         "The analysis was re-run and the ids shifted, or the key is a typo. Either "
                         "way the entry is excusing nothing." % (len(dangling), ", ".join(dangling)))
    for k, v in sorted(dec.items()):
        st = v.get("status")
        if st not in STATUS:
            raise SystemExit("pr570-ledger: FAIL -- %s has status %r, not one of %s"
                             % (k, st, ", ".join(sorted(STATUS))))
        need = NEEDS.get(st)
        if need and not v.get(need):
            raise SystemExit("pr570-ledger: FAIL -- %s is %r and must carry a %r. %s"
                             % (k, st, need, STATUS[st]))

    def status_of(rid, is_open):
        v = dec.get(rid)
        if not v:
            return ("open" if is_open else "closed"), ""
        st = v["status"]
        note = v.get("task") or v.get("commit") or v.get("reason") or v.get("until") or ""
        return st, cell(note, 90)

    live = [r for r in rows if r[2]]
    tally = {}
    for r in rows:
        st, _ = status_of(r[1], r[2])
        tally[st] = tally.get(st, 0) + 1

    L = []
    W = L.append
    W("# PR 570 — findings ledger")
    W("")
    W("**Generated by `scripts/pr570-ledger.py`.** Do not hand-edit: the table is emitted from the")
    W("three workflow outputs and your edits will be lost. To record a decision, add an entry to")
    W("`docs/superpowers/drafts/osb-pr570-decisions.json` and regenerate.")
    W("")
    W("**Regeneration is machine-local.** The raw outputs live at `/root/analysis/osb-pr570/raw/`,")
    W("deliberately outside this repository — they are agent reports that quote third-party")
    W("unlicensed code as evidence. Without them this file cannot be rebuilt from a clean clone, and")
    W("saying so is the difference between a limitation and a lie.")
    W("")
    W("Subject: OpenStarbound PR 570 (`psychosomat`), compared against our fork at `566fce3d`.")
    W("Ideas only, never their code — see `/root/analysis/osb-pr570/PROVENANCE.txt`.")
    W("")
    W("## Status")
    W("")
    W("| status | rows | meaning |")
    W("|---|---:|---|")
    W("| **open** | %d | needs a call |" % tally.get("open", 0))
    for k in ("accepted", "deferred", "declined", "done"):
        if tally.get(k):
            W("| **%s** | %d | %s |" % (k, tally[k], STATUS[k]))
    W("| closed | %d | no action, by verdict |" % tally.get("closed", 0))
    W("")
    W("`declined` must carry a reason and `deferred` a trigger; the generator rejects either without")
    W("one, and hard-fails on a decision naming a row that no longer exists.")
    W("")
    W("---")
    W("")
    W("## Decided")
    W("")
    if dec:
        W("| row | status | item | where |")
        W("|---|---|---|---|")
        for r in rows:
            if r[1] not in dec:
                continue
            st, note = status_of(r[1], r[2])
            W("| **%s** | **%s** | %s | %s |" % (r[1], st, r[3], note or "—"))
    else:
        W("*Nothing decided yet.*")
    W("")
    W("## Still open")
    W("")
    W("| row | item | verdict | next steps |")
    W("|---|---|---|---|")
    for r in rows:
        st, _ = status_of(r[1], r[2])
        if st != "open":
            continue
        W("| **%s** | %s | %s | %s |" % (r[1], r[3], r[4], r[6]))
    W("")
    W("---")
    W("")
    W("## Full table")
    W("")
    W("| row | status | item | verdict | detail | next steps |")
    W("|---|---|---|---|---|---|")
    for sec, rid, is_open, item, v1, v2, steps in rows:
        st, note = status_of(rid, is_open)
        W("| **%s** | %s%s | %s | %s | %s | %s |"
          % (rid, st, (" · " + note) if note else "", item, v1, v2 or "—", steps))
    W("")
    return "\n".join(L) + "\n", len(rows), len(live), tally


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 if the ledger is stale")
    args = ap.parse_args(argv)

    text, n, live, tally = build()
    if args.check:
        if not LEDGER.exists() or LEDGER.read_text() != text:
            print("pr570-ledger: STALE -- rerun `scripts/pr570-ledger.py`")
            return 1
        print("pr570-ledger: OK -- %d rows, %d open" % (n, tally.get("open", 0)))
        return 0
    LEDGER.write_text(text, encoding="utf-8")
    print("pr570-ledger: wrote %s -- %d rows, %s"
          % (LEDGER.relative_to(REPO), n,
             ", ".join("%d %s" % (v, k) for k, v in sorted(tally.items()))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
