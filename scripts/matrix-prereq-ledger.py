#!/usr/bin/env python3
"""Generate and CHECK the lever-matrix prerequisite ledger.

WHAT THIS IS. 49 findings from a five-agent read-only sweep, each one something that would make a
number produced by the lever matrix wrong or uninterpretable. The Director asked that every
prerequisite be closed by construction before a 94-minute measurement run, so the failure mode is a
MISSED prerequisite -- which makes an unwritten finding worse than a false one.

WHY IT IS NOT JUST THE TASK BOARD. The board is the burn-down mechanism and each row carries its task
id. But a task is prose: nothing checks whether its claim is still true. This ledger's rows carry a
SIGNATURE -- a predicate over the tree that is TRUE while the defect is present -- and --check asserts
each row's status against it.

THAT IS THE ONE THING THE PRIOR LEDGER DID NOT DO. scripts/pr570-ledger.py checks the table against
its decisions file, and the decisions file against the table: two views of the same claim agreeing
with each other. Ten rows once sat "accepted" while the change was already shipped, because nothing
ever compared a decision to the CODE. Internal consistency is not correspondence. So here:

    status open/doing  =>  the signature MUST still match     (else the row is stale and lying)
    status done        =>  the signature MUST NOT match       (else it regressed, or never landed)
    signature null     =>  UNVERIFIABLE, counted and printed loudly, never silently passed

A row with no signature is not a failure -- some findings are judgements, not greps -- but the COUNT
of them is the honest measure of how much of this ledger is checked by machine rather than asserted.

  matrix-prereq-ledger.py                 # regenerate the markdown
  matrix-prereq-ledger.py --check         # gate: statuses correspond to the tree
  matrix-prereq-ledger.py --selftest      # prove the checker fires in both directions
"""
import glob
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FINDINGS = os.path.join(REPO, "docs/superpowers/drafts/matrix-prereq-findings.json")
DECISIONS = os.path.join(REPO, "docs/superpowers/drafts/matrix-prereq-decisions.json")
LEDGER = os.path.join(REPO, "docs/superpowers/drafts/matrix-prereq-ledger.md")
# The page template lives beside this script rather than inside it, so the CSS stays editable and
# the data stays generated. One generator, two renderings.
TEMPLATE_PATH = os.path.join(REPO, "scripts/matrix-prereq-ledger.html.tmpl")

OPEN_STATES = ("open", "doing")
ALL_STATES = ("open", "doing", "done", "declined", "deferred")


def signature_matches(sig):
    """True when the defect the signature describes is still present in the tree.

    SOME DEFECTS ARE ABSENCES. A missing freshness assertion, an unregistered counter, a check nobody
    wrote -- for those the pattern describes the FIX, and its absence is the defect. `"absent": true`
    inverts the sense so both shapes can be expressed, rather than forcing a contorted regex that
    matches the old code and silently stops meaning anything once that code is edited for an
    unrelated reason.
    """
    pat = re.compile(sig["re"])
    pattern = os.path.join(REPO, sig["glob"])
    for path in glob.glob(pattern, recursive=True):
        if "/test/" in path:
            continue
        try:
            if pat.search(open(path, encoding="utf-8", errors="replace").read()):
                return False if sig.get("absent") else True
        except OSError:
            continue
    return True if sig.get("absent") else False


def load():
    rows = json.load(open(FINDINGS))["rows"]
    raw = json.load(open(DECISIONS)) if os.path.exists(DECISIONS) else {}
    # Underscore keys are documentation living in the same file, not decisions about a row. Without
    # this the checker reports "_comment names a row that is not in the findings" and its one real
    # correspondence failure is buried under a false one.
    decisions = {k: v for k, v in raw.items() if not k.startswith("_")}
    return rows, decisions


def check(rows, decisions):
    by_id = {r["id"]: r for r in rows}
    problems, unverifiable = [], []

    for rid in decisions:
        if rid not in by_id:
            problems.append(f"decision names row {rid}, which is not in the findings")

    for r in rows:
        d = decisions.get(r["id"], {})
        status = d.get("status", "open")
        if status not in ALL_STATES:
            problems.append(f"{r['id']}: unknown status {status!r}")
            continue
        # declined and deferred must carry their warrant, exactly as the PR-570 ledger requires.
        if status == "declined" and not d.get("reason"):
            problems.append(f"{r['id']}: declined without a reason")
        if status == "deferred" and not d.get("until"):
            problems.append(f"{r['id']}: deferred without a trigger")

        sig = r.get("signature")
        if not sig:
            unverifiable.append(r["id"])
            continue
        present = signature_matches(sig)
        if status in OPEN_STATES and not present:
            problems.append(f"{r['id']}: status {status}, but the defect signature NO LONGER matches "
                            f"({sig['desc']}). The row is stale -- either it was fixed without being "
                            f"recorded, or the signature is wrong.")
        if status == "done" and present:
            problems.append(f"{r['id']}: status done, but the defect signature STILL matches "
                            f"({sig['desc']}). It regressed, or it never landed.")
    return problems, unverifiable


def render(rows, decisions):
    by_state = {}
    for r in rows:
        st = decisions.get(r["id"], {}).get("status", "open")
        by_state.setdefault(st, []).append(r)
    sev_rank = {"BLOCKS_MATRIX": 0, "DEGRADES_MATRIX": 1, "COSMETIC": 2}

    checked = sum(1 for r in rows if r.get("signature"))
    out = ["# Lever-matrix prerequisites — findings ledger", "",
           "**Generated by `scripts/matrix-prereq-ledger.py`.** Do not hand-edit: the table is emitted",
           "from `matrix-prereq-findings.json` and `matrix-prereq-decisions.json`, and your edits will be",
           "lost. To record a decision, add an entry to the decisions file and regenerate.",
           "",
           "Every row is something that would make a number produced by the lever matrix wrong or",
           "uninterpretable. Source: a five-agent read-only sweep of the tree. Unlike the PR-570 ledger,",
           "the inputs live IN this repository, so this file rebuilds from a clean clone.",
           "",
           f"**{checked} of {len(rows)} rows carry a machine-checkable signature.** `--check` asserts an",
           "open row's defect is still present and a done row's is gone -- correspondence against the",
           "tree, not agreement between two files. The remainder are judgements; their count is printed",
           "rather than hidden, because an unchecked row is not a checked one.", ""]

    out += ["## Status", "", "| status | rows | meaning |", "|---|---:|---|"]
    for st, meaning in [("open", "not started"), ("doing", "in progress"),
                        ("done", "closed; `commit` says where"),
                        ("declined", "we will not do this; `reason` is mandatory"),
                        ("deferred", "not now; `until` names the trigger, and is mandatory")]:
        out.append(f"| **{st}** | {len(by_state.get(st, []))} | {meaning} |")
    out += ["", "## By severity, open rows only", "",
            "| id | sev | finding | closes by |", "|---|---|---|---|"]
    openish = [r for r in rows if decisions.get(r["id"], {}).get("status", "open") in OPEN_STATES]
    for r in sorted(openish, key=lambda r: (sev_rank.get(r["severity"], 3), r["id"])):
        sev = {"BLOCKS_MATRIX": "**BLOCK**", "DEGRADES_MATRIX": "degrade", "COSMETIC": "cosmetic"}[r["severity"]]
        out.append(f"| `{r['id']}` | {sev} | {r['title'][:150]} | {r['fixShape'][:120]} |")

    done = [r for r in rows if decisions.get(r["id"], {}).get("status") == "done"]
    if done:
        out += ["", "## Closed", "", "| id | finding | where |", "|---|---|---|"]
        for r in sorted(done, key=lambda r: r["id"]):
            d = decisions[r["id"]]
            out.append(f"| `{r['id']}` | {r['title'][:150]} | {d.get('commit', d.get('note', ''))[:80]} |")

    out += ["", "## Evidence", ""]
    for r in sorted(rows, key=lambda r: r["id"]):
        st = decisions.get(r["id"], {}).get("status", "open")
        out += [f"### `{r['id']}` — {r['severity']} — {st}", "",
                f"**{r['title']}**", "",
                f"*Evidence.* {r['evidence']}", "",
                f"*Why it corrupts a matrix number.* {r['impact']}", "",
                f"*Closes by.* {r['fixShape']}", ""]
        if r.get("signature"):
            out.append(f"*Signature.* `{r['signature']['glob']}` matching `{r['signature']['re']}` "
                       f"— present while open. ({r['signature']['desc']})")
            out.append("")
    return "\n".join(out) + "\n"



def render_html(rows, decisions):
    """The same rows as the markdown, as a scannable page. ONE GENERATOR, TWO RENDERINGS -- a page
    hand-written beside a generated doc is two descriptions of one claim, which is the drift this
    ledger exists to prevent."""
    import html as _h
    TEMPLATE = open(TEMPLATE_PATH).read()

    sev_meta = {"BLOCKS_MATRIX": ("BLOCK", "block"), "DEGRADES_MATRIX": ("degrade", "degrade"),
                "COSMETIC": ("cosmetic", "cosmetic")}
    n = len(rows)
    openish = [r for r in rows if decisions.get(r["id"], {}).get("status", "open") in OPEN_STATES]
    blocks = [r for r in openish if r["severity"] == "BLOCKS_MATRIX"]
    verified = [r for r in rows if r.get("signature")]
    pct = round(100 * len(verified) / n)

    cards = []
    for r in sorted(rows, key=lambda r: (list(sev_meta).index(r["severity"]), r["id"])):
        st = decisions.get(r["id"], {}).get("status", "open")
        label, cls = sev_meta[r["severity"]]
        sig = r.get("signature")
        vb = ('<span class="tag ok" title="a predicate over the tree decides this row\'s status">'
              'verified</span>') if sig else \
             ('<span class="tag warn" title="no predicate; this row\'s status is asserted, not checked">'
              'asserted</span>')
        d = decisions.get(r["id"], {})
        where = _h.escape(d.get("commit", d.get("note", "")))
        cards.append(f'''<article class="row {cls}" data-sev="{cls}" data-status="{st}"
   data-checked="{"y" if sig else "n"}">
  <header>
    <code class="rid">{r["id"]}</code>
    <span class="tag sev-{cls}">{label}</span>
    <span class="tag st-{st}">{st}</span>
    {vb}
    {f'<span class="where">{where}</span>' if where else ''}
  </header>
  <h3>{_h.escape(r["title"])}</h3>
  <dl>
    <dt>Evidence</dt><dd>{_h.escape(r["evidence"])}</dd>
    <dt>Why it corrupts a matrix number</dt><dd>{_h.escape(r["impact"])}</dd>
    <dt>Closes by</dt><dd>{_h.escape(r["fixShape"])}</dd>
    {f'<dt>Signature</dt><dd class="mono">{_h.escape(sig["glob"])} &nbsp;·&nbsp; {_h.escape(sig["re"])}<br><span class="muted">present while open — {_h.escape(sig["desc"])}</span></dd>' if sig else ''}
  </dl>
</article>''')

    return TEMPLATE.replace("__CARDS__", "\n".join(cards)) \
                   .replace("__N__", str(n)).replace("__OPEN__", str(len(openish))) \
                   .replace("__BLOCKS__", str(len(blocks))) \
                   .replace("__VERIFIED__", str(len(verified))) \
                   .replace("__ASSERTED__", str(n - len(verified))) \
                   .replace("__PCT__", str(pct))

def selftest():
    """Both directions, because a checker that only ever passes is not known to work."""
    fails = []

    def arm(name, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            fails.append(name)

    # THE ABSENT PATTERN IS BUILT BY CONCATENATION, and that is not fussiness. Written as one literal
    # it appears in THIS FILE, which is the file the arm searches -- so the "no match" arm matched, and
    # the checker looked broken in three arms at once. A self-referential test that greps its own source
    # has to keep its needle out of its own haystack.
    real = {"glob": "scripts/matrix-prereq-ledger.py", "re": r"def selftest", "desc": "this file"}
    fake = {"glob": "scripts/matrix-prereq-ledger.py", "re": "no" + "_such" + "_token_here",
            "desc": "nothing"}
    arm("a signature that matches is detected as present", signature_matches(real) is True)
    arm("a signature that does not match is detected as absent", signature_matches(fake) is False)

    inv = {"glob": "scripts/matrix-prereq-ledger.py", "re": r"def selftest", "absent": True,
           "desc": "the fix is missing"}
    arm("an ABSENT-sense signature reads present-fix as defect-gone", signature_matches(inv) is False)
    inv2 = dict(inv, re="no" + "_such" + "_token_here")
    arm("an ABSENT-sense signature reads missing-fix as defect-present", signature_matches(inv2) is True)

    rows = [{"id": "X01", "severity": "BLOCKS_MATRIX", "title": "t", "evidence": "e", "impact": "i",
             "fixShape": "f", "signature": real}]
    p, u = check(rows, {"X01": {"status": "done"}})
    arm("a DONE row whose defect is still present FAILS", any("regressed" in x for x in p))
    p, u = check(rows, {"X01": {"status": "open"}})
    arm("an OPEN row whose defect is still present passes", not p)

    rows[0]["signature"] = fake
    p, u = check(rows, {"X01": {"status": "open"}})
    arm("an OPEN row whose defect is GONE fails as stale", any("stale" in x for x in p))
    p, u = check(rows, {"X01": {"status": "done"}})
    arm("a DONE row whose defect is gone passes", not p)

    rows[0]["signature"] = None
    p, u = check(rows, {"X01": {"status": "done"}})
    arm("a row with no signature is UNVERIFIABLE, not a silent pass", u == ["X01"] and not p)

    p, _ = check(rows, {"X01": {"status": "declined"}})
    arm("declined without a reason fails", any("without a reason" in x for x in p))
    p, _ = check(rows, {"NOPE": {"status": "done"}})
    arm("a decision naming a row that does not exist fails", any("not in the findings" in x for x in p))

    print()
    if fails:
        print(f"matrix-prereq-ledger selftest: FAILED -- {len(fails)}: {', '.join(fails)}")
        return 1
    print("matrix-prereq-ledger selftest: 11/11 arms ok -- the checker fires in BOTH directions and "
          "refuses to pass an unverifiable row silently")
    return 0


def main():
    if "--selftest" in sys.argv[1:]:
        return selftest()
    rows, decisions = load()
    problems, unverifiable = check(rows, decisions)
    if "--html" in sys.argv[1:]:
        out = sys.argv[sys.argv.index("--html") + 1]
        open(out, "w").write(render_html(rows, decisions))
        print(f"wrote {out}")
        return 0
    if "--check" in sys.argv[1:]:
        for p in problems:
            print(f"matrix-prereq-ledger: {p}", file=sys.stderr)
        openish = sum(1 for r in rows
                      if decisions.get(r["id"], {}).get("status", "open") in OPEN_STATES)
        blocks = sum(1 for r in rows if r["severity"] == "BLOCKS_MATRIX"
                     and decisions.get(r["id"], {}).get("status", "open") in OPEN_STATES)
        if problems:
            print(f"matrix-prereq-ledger: {len(problems)} row(s) do not correspond to the tree")
            return 1
        print(f"matrix-prereq-ledger: {len(rows)} rows, {openish} open ({blocks} BLOCK the matrix), "
              f"{len(rows) - len(unverifiable)} verified against the tree, "
              f"{len(unverifiable)} UNVERIFIABLE (no signature): {' '.join(unverifiable[:8])}"
              f"{' ...' if len(unverifiable) > 8 else ''}")
        return 0
    open(LEDGER, "w").write(render(rows, decisions))
    print(f"wrote {os.path.relpath(LEDGER, REPO)} -- {len(rows)} rows, "
          f"{len(unverifiable)} without a signature")
    return 0


if __name__ == "__main__":
    sys.exit(main())
