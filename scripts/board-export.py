#!/usr/bin/env python3
"""Export the agent task board to a durable, version-controlled document.

WHY THIS EXISTS
---------------
The board lives in the Claude Code task store at ~/.claude/tasks/<session-uuid>/<id>.json --
one JSON file per task, outside the repo, in no git history, keyed by SESSION UUID. Meanwhile
the identifiers it hands out are load-bearing in two permanent places:

  * commit messages, which end with "[#131]" and are immutable once pushed;
  * the architecture docs, which cite "task #168" as the record of why a thing was built.

Both are pointers into a non-versioned, session-scoped directory. Lose it -- or start a session
under a new UUID -- and every one of those references dangles while still reading as if it
resolves. That is not hypothetical: ids #1-#63 are already absent from disk.

This script makes the referent durable. It reads every store it can find and emits docs/board.md:
one row per id, plus the full stored description of each, so a reader with nothing but the repo
can resolve any "[#NNN]" in the history.

DETERMINISM IS A FEATURE. The output carries no wall-clock timestamp and is sorted by store and
id, so regenerating produces a diff ONLY when the board actually changed. Git already records
when. A file that churns on every run trains you to ignore its diffs.

Usage:
    scripts/board-export.py                 # write docs/board.md
    scripts/board-export.py --check         # exit 1 if docs/board.md is stale
    scripts/board-export.py --store <dir>   # export one specific store

--check is for LOCAL use (a pre-commit habit), deliberately not wired into CI: a CI machine has no
task store, so the check could only ever report "no store found". The board is machine-local by
nature; this file is the copy that travels.

The output carries an Integrity section that self-checks two things the board cannot police on its
own: commit ids cited in task text that no longer resolve (the 2026-07-19 reorg rewrote history, so
content survived and ids did not), and completed tasks carrying no evidence at all.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
OUT = REPO / "docs" / "board.md"
DEFAULT_STORE_ROOT = pathlib.Path.home() / ".claude" / "tasks"

# Our commit convention: the message ENDS with [#<task>]. The bracket form is unambiguous, which
# is why it is the only cross-reference this script trusts -- see the namespace warning below.
COMMIT_STAMP = re.compile(r"\[#(\d+)\]")
# In prose the bare "#NNN" is NOT ours to claim: the same namespace carries upstream PR and issue
# numbers (#542, #561, #204, #285, #510), and at least one number -- #104 -- is BOTH a task here
# and an upstream issue cited in another task's description. Only the explicit "task #NNN" form is
# safe to resolve automatically.
DOC_CITATION = re.compile(r"task #(\d+)")

# A hex run that could be an abbreviated commit id. Requiring BOTH a digit and a letter rejects pure
# numbers ("1500 frames", "2026") and pure-alpha words ("added", "decade") that are otherwise valid
# hex. UUIDs are removed from the text before this runs -- see strip_uuids.
SHA_LIKE = re.compile(r"\b(?=[0-9a-f]*[0-9])(?=[0-9a-f]*[a-f])[0-9a-f]{7,40}\b")
# Session-store uuids appear in task text and their groups are hex of exactly the right length. The
# first attempt at this check reported two uuid fragments as dangling commits. Blank uuids out
# first: a rule, not a blocklist of the fragments that happened to bite.
UUID = re.compile(r"\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b")

STATUS_ORDER = {"in_progress": 0, "pending": 1, "completed": 2}


def find_stores(store_root):
    """Every task store on disk, newest-modified first. Each is one session's id-space."""
    if not store_root.is_dir():
        return []
    stores = [d for d in store_root.iterdir() if d.is_dir() and any(d.glob("*.json"))]
    return sorted(stores, key=lambda d: (-d.stat().st_mtime, d.name))


def load_store(store):
    tasks = []
    for f in sorted(store.glob("*.json")):
        try:
            d = json.loads(f.read_text())
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            print(f"  ! skipping unreadable {f.name}: {e}", file=sys.stderr)
            continue
        if "id" not in d:
            continue
        tasks.append(d)
    # Numeric sort where possible: "#100" must not sort before "#64".
    return sorted(tasks, key=lambda t: (int(t["id"]) if str(t["id"]).isdigit() else 1 << 30, str(t["id"])))


def git(*args):
    try:
        return subprocess.run(["git", "-C", str(REPO), *args],
                              capture_output=True, text=True, check=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def commit_index():
    """task id -> [(short sha, subject)], across ALL branches.

    --all matters: the 2026-07-19 branch reorg rewrote history, so work can be reachable from a
    branch that is not the one checked out.

    Scans the WHOLE message, not just the subject. The convention is that the message *ends* with
    the stamp, and on a commit with a long body that puts it on the last line -- 32b8f849 is exactly
    that shape, and a subject-only scan silently dropped it from #131.
    """
    idx = {}
    # \x1e ends the record, \x1f separates fields, so a multi-line body cannot be mistaken for the
    # next commit.
    out = git("log", "--all", "--no-merges", "--format=%h\x1f%s\x1f%b\x1e")
    for record in out.split("\x1e"):
        record = record.strip("\n")
        if record.count("\x1f") < 2:
            continue
        sha, subject, body = record.split("\x1f", 2)
        for tid in COMMIT_STAMP.findall(subject + "\n" + body):
            idx.setdefault(tid, [])
            if sha not in (s for s, _ in idx[tid]):
                idx[tid].append((sha, subject))
    return idx


def strip_uuids(text):
    return UUID.sub(" ", text)


def dangling_index(stores_data):
    """task id -> [sha, ...] that the task cites but that resolve to no commit in any branch.

    WHY THIS IS A REAL CHECK AND NOT PEDANTRY. The 2026-07-19 reorg rewrote history: the fork was
    reconstructed as clean branches off a clean upstream base. Content survived, ids did not. A task
    description saying "SHIPPED (4fcf71033)" then reads as though it resolves while pointing at
    nothing -- and acting on such a citation has already produced two wrong conclusions here, one of
    them "implementation lost, rebuild it" about work that had in fact shipped.

    Resolution is one `git cat-file --batch-check` for every candidate at once, not one subprocess
    per sha.
    """
    cited = {}
    for _, tasks in stores_data:
        for t in tasks:
            text = strip_uuids(str(t.get("subject", "")) + " " + str(t.get("description", "")))
            found = list(dict.fromkeys(SHA_LIKE.findall(text)))
            if found:
                cited[str(t["id"])] = found

    candidates = sorted({s for v in cited.values() for s in v})
    if not candidates:
        return {}, 0

    try:
        proc = subprocess.run(["git", "-C", str(REPO), "cat-file", "--batch-check"],
                              input="".join(f"{s}^{{commit}}\n" for s in candidates),
                              capture_output=True, text=True)
    except FileNotFoundError:
        return {}, 0
    # One output line per input line, in order: "<sha> commit <size>" or "<query> missing".
    alive = set()
    for s, line in zip(candidates, proc.stdout.splitlines()):
        if " missing" not in line and " ambiguous" not in line:
            alive.add(s)

    dead = {tid: [s for s in shas if s not in alive] for tid, shas in cited.items()}
    return {k: v for k, v in dead.items() if v}, len(candidates)


def doc_index():
    """task id -> sorted list of repo-relative doc paths using the explicit 'task #NNN' form."""
    idx = {}
    docs = REPO / "docs"
    if not docs.is_dir():
        return idx
    for f in sorted(docs.rglob("*.md")):
        try:
            text = f.read_text(errors="replace")
        except OSError:
            continue
        rel = f.relative_to(REPO).as_posix()
        for tid in set(DOC_CITATION.findall(text)):
            idx.setdefault(tid, set()).add(rel)
    return {k: sorted(v) for k, v in idx.items()}


def cell(s, limit=None):
    """Make arbitrary stored text safe inside a markdown table cell.

    Some stored subjects contain newlines and literal XML-ish fragments -- #132 and #134 have their
    ENTIRE description swallowed into the subject field by a malformed task creation. They are real
    data and are preserved verbatim in the detail section; here they only have to not break the
    table.

    Angle brackets are escaped, not stripped: a markdown renderer treats `<parameter name="...">` as
    raw HTML and SWALLOWS it along with everything up to the next tag. Silent text loss in the one
    document whose whole job is to not lose text.
    """
    s = re.sub(r"\s+", " ", str(s)).strip()
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    s = s.replace("|", "\\|")
    if limit and len(s) > limit:
        s = s[: limit - 1].rstrip() + "…"
    return s


def fence_for(text):
    """A fence long enough to survive a description that itself contains backtick fences."""
    longest = max((len(m) for m in re.findall(r"`+", text)), default=0)
    return "`" * max(3, longest + 1)


def render(stores_data, commits, doccites, dangling, cited_total):
    L = []
    A = L.append

    total = sum(len(t) for _, t in stores_data)
    counts = {}
    for _, tasks in stores_data:
        for t in tasks:
            counts[t.get("status", "unknown")] = counts.get(t.get("status", "unknown"), 0) + 1

    A("# Board — the durable task index")
    A("")
    A("**Generated. Do not hand-edit.** Regenerate with `scripts/board-export.py`; the script is the")
    A("mechanism, this file is the artefact. It carries no timestamp on purpose, so a regeneration")
    A("diffs only when the board actually changed — git already records when.")
    A("")
    A("## Why this file exists")
    A("")
    A("The live board is the Claude Code task store at `~/.claude/tasks/<session-uuid>/<id>.json` —")
    A("outside the repo, in no git history, keyed by **session UUID**. But the identifiers it hands")
    A("out are permanent: commit messages end with `[#131]`, and the architecture docs cite")
    A("`task #168` as the record of why something was built. Without this file every one of those is")
    A("a pointer into a directory that is not versioned, not pushed, and not backed up — dangling but")
    A("still reading as though it resolves. Ids **#1–#63 are already absent from disk**.")
    A("")
    A("## How to read it")
    A("")
    A("- **Ids are per-store, not global.** Each session store has its own id-space. Two stores can")
    A("  both hold a `#4`. The `Store` column disambiguates; `[#NNN]` commit stamps refer to the")
    A("  primary store.")
    A("- **A bare `#NNN` in prose is not necessarily one of these.** The same namespace carries")
    A("  upstream PR and issue numbers (`#542`, `#561`, `#204`). `#104` is *both* a task here and an")
    A("  upstream issue cited elsewhere. Only the explicit `task #NNN` form is auto-resolved into the")
    A("  Cited column; treat everything else as ambiguous.")
    A("- **Commits are matched on the `[#NNN]` stamp across all branches** (`git log --all`), because")
    A("  the 2026-07-19 reorg rewrote history and work can be reachable only from another branch.")
    A("- **Status is what the store says**, which is not always what the tree says. A content audit on")
    A("  2026-07-25 found three statuses wrong in both directions. Verify against tree content before")
    A("  trusting a status to mean work did or did not ship.")
    A("")
    A(f"**{total} tasks** across {len(stores_data)} store(s): "
      + ", ".join(f"{n} {s}" for s, n in sorted(counts.items(), key=lambda kv: STATUS_ORDER.get(kv[0], 9))))
    A("")

    for store, tasks in stores_data:
        A(f"- `{store.name}` — {len(tasks)} tasks, ids "
          f"{min(int(t['id']) for t in tasks if str(t['id']).isdigit())}"
          f"–{max(int(t['id']) for t in tasks if str(t['id']).isdigit())}")
    A("")
    A("---")
    A("")
    A("## Integrity")
    A("")
    A("A self-check, so the drift this file exists to prevent is *visible* rather than something")
    A("someone has to go and discover. It is the same discipline as the render oracles: a check that")
    A("reports but does not surface is not a check.")
    A("")
    dead_n = sum(len(v) for v in dangling.values())
    A(f"**Commit ids cited in task text:** {cited_total} — "
      + (f"**{dead_n} dangling** across {len(dangling)} tasks." if dead_n else "all resolve. ✅"))
    A("")
    if dangling:
        A("A dangling id is one the task cites that resolves to no commit on any branch. These are")
        A("almost entirely pre-2026-07-19: the branch reorg rewrote history, so content survived and")
        A("ids did not. The citation still *reads* as though it resolves, which is the hazard — acting")
        A("on one produced the \"implementation lost, rebuild it\" conclusion about work that had")
        A("shipped. Re-anchor them to the live equivalent, found by commit *message*, not by id.")
        A("")
        A("| Task | Dangling ids |")
        A("|-----:|:-------------|")
        for tid in sorted(dangling, key=lambda x: int(x) if x.isdigit() else 1 << 30):
            A(f"| [#{tid}](#c29c1332-{tid}) | " + " ".join(f"`{s}`" for s in dangling[tid]) + " |")
        A("")

    comp = [t for _, ts in stores_data for t in ts if t.get("status") == "completed"]
    noev = [t for t in comp if not commits.get(str(t["id"])) and not doccites.get(str(t["id"]))]
    A(f"**Completed tasks citing no commit and no doc:** {len(noev)} of {len(comp)}.")
    A("")
    A("Not a defect count. Much of this campaign's completed work was *investigation* whose")
    A("deliverable was a conclusion — \"determinism-locked, DEFER\" is a finished task that correctly")
    A("touches no code. The number is worth watching only for tasks whose text claims code shipped;")
    A("those should carry a `[#NNN]` stamp, and from the stamping convention onward they do.")
    A("")
    A("---")
    A("")
    A("## The table")
    A("")
    A("| Id | Store | Status | Subject | Commits | Cited in |")
    A("|---:|:------|:-------|:--------|:--------|:---------|")

    for store, tasks in stores_data:
        tag = store.name.split("-")[0]
        for t in tasks:
            tid = str(t["id"])
            anchor = f"{tag}-{tid}"
            status = t.get("status", "unknown")
            mark = {"completed": "done", "in_progress": "**active**", "pending": "open"}.get(status, status)
            shas = commits.get(tid, []) if store is stores_data[0][0] else []
            sha_cell = " ".join(f"`{s}`" for s, _ in shas) if shas else "—"
            cites = doccites.get(tid, []) if store is stores_data[0][0] else []
            cite_cell = " ".join(f"`{c.rsplit('/', 1)[-1]}`" for c in cites) if cites else "—"
            A(f"| [#{tid}](#{anchor}) | `{tag}` | {mark} | {cell(t.get('subject', ''), 120)} "
              f"| {sha_cell} | {cite_cell} |")

    A("")
    A("---")
    A("")
    A("## The record")
    A("")
    A("The stored description of every task, verbatim. This is the part that makes a `[#NNN]` in the")
    A("git history resolvable from the repo alone. Descriptions are fenced rather than inlined because")
    A("some records contain literal markup from malformed task creation, and losing bytes to a")
    A("renderer would defeat the purpose.")
    A("")

    for store, tasks in stores_data:
        tag = store.name.split("-")[0]
        A(f"### Store `{store.name}`")
        A("")
        for t in tasks:
            tid = str(t["id"])
            raw_subject = str(t.get("subject", ""))
            A(f'<a id="{tag}-{tid}"></a>')
            A("")
            A(f"#### #{tid} — {cell(raw_subject, 140)}")
            A("")
            # A truncated heading is a display convenience, never a loss. If the stored subject did
            # not fit, it is reproduced byte-for-byte below -- #132 and #134 carry their entire
            # description in this field.
            if cell(raw_subject, 140) != cell(raw_subject):
                A("_Stored subject exceeds the heading; reproduced verbatim:_")
                A("")
                f = fence_for(raw_subject)
                A(f)
                A(raw_subject)
                A(f)
                A("")
            meta = [f"status: **{t.get('status', 'unknown')}**"]
            if t.get("blockedBy"):
                meta.append("blocked by: " + ", ".join(f"#{b}" for b in t["blockedBy"]))
            if t.get("blocks"):
                meta.append("blocks: " + ", ".join(f"#{b}" for b in t["blocks"]))
            if t.get("metadata"):
                meta.append("metadata: `" + cell(json.dumps(t["metadata"])) + "`")
            A(" · ".join(meta))
            A("")
            if store is stores_data[0][0]:
                for sha, subject in commits.get(tid, []):
                    A(f"- `{sha}` {cell(subject)}")
                for c in doccites.get(tid, []):
                    A(f"- cited in `{c}`")
                if commits.get(tid) or doccites.get(tid):
                    A("")
            desc = (t.get("description") or "").rstrip()
            if desc:
                f = fence_for(desc)
                A(f)
                A(desc)
                A(f)
            else:
                A("_(no description recorded)_")
            A("")

    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--store", type=pathlib.Path, action="append",
                    help="a specific task-store directory (repeatable). Default: every store found.")
    ap.add_argument("--store-root", type=pathlib.Path, default=DEFAULT_STORE_ROOT)
    ap.add_argument("--check", action="store_true", help="exit 1 if the output is stale; write nothing")
    args = ap.parse_args()

    stores = args.store if args.store else find_stores(args.store_root)
    if not stores:
        print(f"No task store found under {args.store_root}.", file=sys.stderr)
        print("The board is machine-local; on a machine without one, docs/board.md is the only copy",
              file=sys.stderr)
        print("and must not be regenerated (it would be emptied).", file=sys.stderr)
        return 2

    stores_data = [(s, load_store(s)) for s in stores]
    stores_data = [(s, t) for s, t in stores_data if t]
    if not stores_data:
        print("Every store was empty; refusing to write an empty board.", file=sys.stderr)
        return 2

    dangling, cited_total = dangling_index(stores_data)
    text = render(stores_data, commit_index(), doc_index(), dangling, cited_total)

    if args.check:
        current = OUT.read_text() if OUT.exists() else ""
        if current != text:
            print(f"STALE: {OUT.relative_to(REPO)} does not match the task store. "
                  f"Run scripts/board-export.py.", file=sys.stderr)
            return 1
        print(f"{OUT.relative_to(REPO)} is up to date.")
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text)
    n = sum(len(t) for _, t in stores_data)
    print(f"wrote {OUT.relative_to(REPO)} — {n} tasks from {len(stores_data)} store(s), {len(text)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
