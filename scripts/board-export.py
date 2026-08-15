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
# ONE GENERATOR, TWO RENDERINGS -- the rule matrix-prereq-ledger.py already follows. The markdown is
# the AUTHORITY: version-controlled, asserted by --check, and resolvable from a clone with no
# network. The page is a READING SURFACE for the same data, and it exists because 400KB of markdown
# is not something anyone reads. Neither can say something the other does not, because both are
# emitted from ONE load of ONE store in the same process. A hand-written page beside a generated
# doc is two descriptions of one claim, which is precisely the drift this file exists to prevent.
#
# The page is NOT the authority and must never become it: nothing can verify a published artefact,
# and a commit message ending [#240] has to resolve from a clone rather than from a URL.
TEMPLATE_PATH = REPO / "scripts" / "board.html.tmpl"
DEFAULT_STORE_ROOT = pathlib.Path.home() / ".claude" / "tasks"

# Our commit convention: the message CARRIES a [#<task>] stamp, conventionally last. The bracket form
# is unambiguous, which is why it is the only cross-reference this script trusts -- see the namespace
# warning below.
#
# ANYWHERE IN THE MESSAGE, NOT ONLY AT THE END, and that is load-bearing rather than lax. This comment
# used to say the message ENDS with the stamp; the regex has always matched anywhere, and the tree
# depends on the regex. Counted 2026-08-07 before touching it: tightening to a trailing-only match
# takes the corpus from 396 links to 327 and orphans 66 real work commits whose stamp is followed by
# more text. The DOC was the false claim, not the code -- so the doc changed.
#
# THE PRICE, and the convention that pays it: a commit message cannot MENTION a stamp without being
# linked by it. Combined with the fact that the board cannot contain the hash of the commit that
# creates it -- a fixed point, not a bug -- a board-only commit that stamps or quotes [#NNN] leaves
# --check RED the instant you finish using it, which is how a check trains you to ignore it. So a
# commit that only regenerates the board carries NO stamp and quotes none: refer to tasks in prose
# ("task 242") instead. Findings belong in the task description, which the board renders, not in a
# board-only commit message the board is structurally unable to cite.
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
# Not every hex run is a commit. The board also records BINARY MD5s (which deploy was running when a
# measurement was taken) and RENDER FRAME HASHES from the A/B harness. 12 of the first 40 ids this
# check flagged were of that kind and had never been commits.
#
# The obvious fix -- look for "md5"/"binary"/"hash" NEAR the hex -- was measured and REJECTED. Swept
# over every window from 0 to 95 characters there is no safe setting: at +/-10 it catches 4 of the 12,
# and every window that catches more also hides REAL dangling commits (8bf7777, cb1332ce, 8e05b0882
# each sit within a few words of an unrelated "binary" or "hash"). An integrity check that can
# silently hide a broken reference is worse than one that is merely noisy, so the heuristic is gone.
#
# Instead the CITATION FORMAT is normalised and matched exactly: a non-commit hash is written
# `md5:df159908` or `framehash:1b0f0923f5677022` in the task text. That is a rule over a format we
# control rather than a guess about prose, and it cannot mistake a commit for a checksum.
NON_COMMIT_PREFIX = re.compile(r"(?:md5|framehash|binary-md5)\s*[:=]\s*$", re.I)
# The board cites commits in TWO repositories: this one, and the notes/design repo where specs and
# plans live. Three ids were reported dangling purely because the check only ever looked in one of
# them. Silently skipped when absent -- on another machine this file is the copy that travels and
# the sibling repo may not exist.
RELATED_REPOS = [pathlib.Path("/root/kubebound")]
# Hand-curated, reviewed, version-controlled: what each cited hex string ACTUALLY is. Task
# descriptions are left untouched -- they are the historical record -- and this is the interpretation
# laid beside them. An id listed here is explained; the number that must stay at zero is UNEXPLAINED.
ANCHORS = REPO / "docs" / "board-anchors.json"

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
    # \x1e BEGINS each record and \x1f separates fields, so a multi-line body cannot be mistaken for
    # the next commit. The separator leads rather than trails because --name-only prints the touched
    # paths AFTER the format string: with a trailing \x1e the paths fall into the NEXT record and
    # corrupt its sha field. That is not hypothetical -- it is what the first version of this did,
    # and the before/after null control caught it by reporting 437 of 440 evidence rows changed.
    out = git("log", "--all", "--no-merges", "--name-only", "--format=%x1e%h\x1f%s\x1f%b")
    for record in out.split("\x1e"):
        record = record.strip("\n")
        if record.count("\x1f") < 2:
            continue
        sha, subject, rest = record.split("\x1f", 2)
        # --name-only puts the paths after the body, separated by a blank line. Splitting on the
        # LAST blank line would break a body that ends in one; instead take every trailing line that
        # names an existing-looking path, which is what git emits and nothing else here does.
        body, paths = _split_body_paths(rest)
        if board_only(paths):
            continue
        for tid in COMMIT_STAMP.findall(subject + "\n" + body):
            idx.setdefault(tid, [])
            if sha not in (s for s, _ in idx[tid]):
                idx[tid].append((sha, subject))
    return idx


def _split_body_paths(rest):
    """-> (body, [path]). git --name-only emits the paths as the trailing block of the record."""
    lines = rest.split("\n")
    i = len(lines)
    while i > 0 and (lines[i - 1].strip() == "" or _looks_like_path(lines[i - 1])):
        i -= 1
    return "\n".join(lines[:i]), [l.strip() for l in lines[i:] if l.strip()]


def _looks_like_path(line):
    """A --name-only path: no spaces, contains a separator or a suffix, not prose."""
    s = line.strip()
    return bool(s) and " " not in s and ("/" in s or "." in s)


def board_only(paths):
    """True when a commit touches nothing but the generated board.

    WHY THIS IS STRUCTURAL AND NOT A CONVENTION. The rule above -- a commit that only regenerates the
    board carries NO stamp -- was written down and then violated three times, most recently by me at
    30c0ed8d while closing task 245. Each time the board grew an evidence row citing a commit that
    contains no work, and a reader following that citation finds a regenerated table.

    It is also a LOOP, which is what finally forced the fix: regenerating after a stamped board
    commit produces a new evidence row, which dirties the board, which needs another commit. The
    board could never reach a fixed point. A convention that has been broken three times and cannot
    converge is not a convention; the exporter now cannot cite such a commit whatever its message
    says.
    """
    return bool(paths) and all(p in BOARD_ARTEFACTS for p in paths)


# The generated surfaces. A commit touching ONLY these carries no work to cite. Deliberately a
# closed literal rather than a docs/ prefix: a real finding written into docs/ must stay citable.
BOARD_ARTEFACTS = frozenset({"docs/board.md", "docs/board.html"})


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
    cited, noncommit = {}, {}
    for _, tasks in stores_data:
        for t in tasks:
            text = strip_uuids(str(t.get("subject", "")) + " " + str(t.get("description", "")))
            keep, skip = [], []
            for m in SHA_LIKE.finditer(text):
                prefix = text[max(0, m.start() - 24): m.start()]
                (skip if NON_COMMIT_PREFIX.search(prefix) else keep).append(m.group(0))
            if keep:
                cited[str(t["id"])] = list(dict.fromkeys(keep))
            if skip:
                noncommit[str(t["id"])] = list(dict.fromkeys(skip))

    candidates = sorted({s for v in cited.values() for s in v})
    if not candidates:
        return {}, 0, noncommit

    alive = set()
    for repo in [REPO, *RELATED_REPOS]:
        if not (repo / ".git").exists() and not repo.is_dir():
            continue
        todo = [s for s in candidates if s not in alive]
        if not todo:
            break
        try:
            proc = subprocess.run(["git", "-C", str(repo), "cat-file", "--batch-check"],
                                  input="".join(f"{s}^{{commit}}\n" for s in todo),
                                  capture_output=True, text=True)
        except FileNotFoundError:
            continue
        # One output line per input line, in order: "<sha> commit <size>" or "<query> missing".
        for s, line in zip(todo, proc.stdout.splitlines()):
            if " missing" not in line and " ambiguous" not in line:
                alive.add(s)

    anchors = {}
    if ANCHORS.exists():
        try:
            anchors = json.loads(ANCHORS.read_text()).get("anchors", {})
        except json.JSONDecodeError:
            anchors = {}

    dead = {tid: [s for s in shas if s not in alive] for tid, shas in cited.items()}
    dead = {k: v for k, v in dead.items() if v}
    unexplained = {tid: [s for s in shas if s not in anchors] for tid, shas in dead.items()}
    return ({k: v for k, v in unexplained.items() if v}, len(candidates), noncommit,
            dead, anchors)


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


# The harness has, on 20 tasks going back to #126, appended the activeForm parameter INTO the
# description as literal markup. Stripped at RENDER time rather than by rewriting the store: it is
# non-destructive, it heals historical and future instances alike, and the store belongs to the
# harness. The count is REPORTED (see the Integrity section) rather than laundered silently -- a
# normalisation nobody can see is a normalisation nobody can notice is wrong. Anchored to end-of-
# string and required to be followed by the parameter tag, so it cannot eat real prose.
# TWO VARIANTS, both anchored to END OF STRING. The common one is a closing tag followed by the
# activeForm parameter; two tasks carry a bare closing tag with nothing after it. Neither can
# eat real prose: a mid-text mention of the tag is not at the end, and no legitimate
# description terminates with a closing tag for itself.
# FOUR VARIANTS observed across 26 tasks, all one accident: a closing tag from the harness's own
# parameter vocabulary, at END OF STRING, followed only by more markup or by nothing --
# `</description><parameter ...>`, `</description></invoke>`, `</parameter></invoke>`, or a bare
# closing tag. It is written as a RULE over that shape rather than a list of the forms that
# happened to bite, because the first three drafts each missed the next one.
#
# It cannot eat prose: a mid-text mention of a tag is neither followed by `<` nor at the end.
HARNESS_TAIL = re.compile(r"</(?:description|parameter)>\s*(?:<.*)?\Z", re.S)


def clean_desc(text):
    """(description with the harness tail removed, whether anything was removed)."""
    out = HARNESS_TAIL.sub("", text or "")
    return out, out != (text or "")


def harness_tail_count(stores_data):
    """How many descriptions this export had to normalise. REPORTED, never silent.

    The comment above clean_desc used to claim this was reported when nothing counted it -- the
    same claim-with-no-instrument shape as ledger rows R13/R14, committed the same day. Counted
    here so the claim is true.
    """
    return sum(1 for _, tasks in stores_data for t in tasks
               if clean_desc(t.get("description", ""))[1])


def ranked_tasks(stores_data):
    """Tasks carrying metadata.rank, in rank order — the NEXT list.

    THE RANK LIVES ON THE TASK, which is the whole point. board.md, this page and the live board all
    render from one store, so a reprioritisation cannot leave one of them saying something else.
    Set it with TaskUpdate(metadata={"rank": N}); clear it with {"rank": null}.

    Completed tasks are dropped: a rank is a statement about what comes next, and a finished item
    holding one is stale by construction. That is counted in Integrity, not hidden.
    """
    out, done_ranked = [], []
    for store, tasks in stores_data:
        for t in tasks:
            r = (t.get("metadata") or {}).get("rank")
            if r is None:
                continue
            (done_ranked if t.get("status") == "completed" else out).append((r, store, t))
    out.sort(key=lambda x: (x[0], str(x[1].name), str(x[2].get("id"))))
    return out, done_ranked


def rank_problems(ranked, stores_data):
    """Integrity of the ranking itself. Reported, never silently normalised."""
    by_id = {str(t.get("id")): t for _, tasks in stores_data for t in tasks}
    problems = []
    seen = {}
    for r, _, t in ranked:
        seen.setdefault(r, []).append(str(t.get("id")))
    for r, ids in sorted(seen.items()):
        if len(ids) > 1:
            problems.append(f"rank {r} is claimed by {len(ids)} tasks: {', '.join('#' + i for i in ids)}")
    # NO CONTIGUITY CHECK, deliberately, and it was here for one commit before firing on correct
    # behaviour. Completing the top item and clearing its rank leaves 2..N, which the check called
    # "a gap at 1" -- so it fired on every act of PROGRESS, and satisfying it would mean renumbering
    # every remaining task after each completion. A rank is an ORDER: it needs to be total, not
    # dense. 2 < 3 < 4 orders fine, and sparse numbering (1, 2, 50) is useful for inserting without
    # a renumber. Duplicates are the real defect, because two items at one position have no order
    # between them. A check that cries wolf on the normal path is how the real signal below gets
    # ignored -- the same over-fire hazard gputimer-brackets.py keeps dedicated arms for.
    for r, _, t in ranked:
        for b in t.get("blockedBy") or []:
            blocker = by_id.get(str(b))
            if blocker and blocker.get("status") != "completed":
                br = (blocker.get("metadata") or {}).get("rank")
                if br is not None and br > r:
                    problems.append(f"#{t.get('id')} (rank {r}) is blocked by #{b} (rank {br}) — "
                                    f"a blocker ranked LATER than the thing it blocks")
    return problems


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


def _ambiguous_ids(stores_data):
    """-> {task id: store count} for ids that exist in more than one store.

    A `[#NNN]` stamp names a number, never a store, so an id living in two stores makes every commit
    carrying it unattributable. There are no such ids today; this is here so that the day there is
    one, the board says so rather than handing the evidence to whichever store was written last."""
    seen = {}
    for _, tasks in stores_data:
        for t in tasks:
            seen[str(t["id"])] = seen.get(str(t["id"]), 0) + 1
    return {k: v for k, v in seen.items() if v > 1}


def render(stores_data, commits, doccites, dangling, cited_total, noncommit, dead_all, anchors):
    ambiguous_ids = _ambiguous_ids(stores_data)
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

    # THE NEXT LIST, IN THE AUTHORITY. The page renders this same block from the same load; if it
    # only existed on the page, the page would be asserting a priority the repo cannot confirm.
    ranked, done_ranked = ranked_tasks(stores_data)
    by_id = {str(t.get("id")): t for _, tasks in stores_data for t in tasks}
    A("## Next")
    A("")
    A("Ranked work, in order. **The rank lives on the task** (`metadata.rank`), not in this file and")
    A("not in the page — one source, three renderings, so a reprioritisation cannot leave any of them")
    A("disagreeing. Set with `TaskUpdate(metadata={\"rank\": N})`; clear with `{\"rank\": null}`.")
    A("")
    if not ranked:
        A("*Nothing is ranked.*")
    else:
        # BOTH, because they are different facts and this file is the authority. `#` is the position
        # in the queue and always starts at 1; `rank` is the stored metadata.rank sort key, sparse by
        # design, and the value you set to reprioritise.
        A("| # | rank | id | task | startable |")
        A("|---:|---:|---|---|---|")
        for pos, (r, _store, t) in enumerate(ranked, 1):
            live = [str(b) for b in (t.get("blockedBy") or [])
                    if by_id.get(str(b), {}).get("status") != "completed"]
            state = "ready" if not live else "blocked by " + ", ".join("#" + b for b in live)
            A(f"| {pos} | {r} | `#{t.get('id')}` | {cell(t.get('subject', ''), 110)} | {state} |")
    A("")
    for p in rank_problems(ranked, stores_data):
        A(f"> **Ranking integrity:** {p}")
        A("")
    if done_ranked:
        A(f"> **{len(done_ranked)} completed task(s) still carry a rank** "
          f"({', '.join('#' + str(t.get('id')) for _, _, t in done_ranked)}). A rank is a claim about")
        A("> what comes next, so a finished item holding one is stale — clear it with `{\"rank\": null}`.")
        A("")
    A("---")
    A("")
    A("## Integrity")
    A("")
    A("A self-check, so the drift this file exists to prevent is *visible* rather than something")
    A("someone has to go and discover. It is the same discipline as the render oracles: a check that")
    A("reports but does not surface is not a check.")
    A("")
    dead_n = sum(len(v) for v in dead_all.values())
    unex_n = sum(len(v) for v in dangling.values())
    kinds = {}
    for sha in {s for v in dead_all.values() for s in v}:
        k = anchors.get(sha, {}).get("kind", "unexplained")
        kinds[k] = kinds.get(k, 0) + 1

    A(f"**Commit ids cited in task text:** {cited_total}, of which **{dead_n} resolve to nothing** "
      f"in either repository.")
    A("")
    tails = harness_tail_count(stores_data)
    if tails:
        A(f"**Descriptions normalised on export: {tails}.** The task harness has, on these, appended its")
        A("own closing markup (`</description>`, `</parameter>`, `<parameter name=\"activeForm\">…`) into")
        A("the stored description. It is stripped at render time rather than by rewriting the store —")
        A("non-destructive, self-healing, and the store belongs to the harness. Counted here rather than")
        A("laundered silently: a normalisation nobody can see is one nobody can notice is wrong.")
        A("")
    if dead_n:
        A("That is expected and mostly harmless: TWO history rewrites destroyed these ids while "
          "preserving every byte of content — the 2026-07-19 whole-fork reorg, and an earlier one "
          "around 2026-07-18 that rebuilt the 2026-07-14 stretch of `dev/upstream-merge`. What "
          "matters is not that an id is dead but whether anyone can still say what it *was*. "
          "`docs/board-anchors.json` answers that, id by id:")
        A("")
        for k, label in (("commit", "re-anchored to a live commit"),
                         ("md5", "a deployed-binary MD5, never a commit"),
                         ("framehash", "an A/B render frame hash, never a commit"),
                         ("unresolvable", "dead, with no live equivalent that could be defended"),
                         ("unexplained", "NOT YET INVESTIGATED")):
            if kinds.get(k):
                A(f"- **{kinds[k]}** — {label}")
        A("")
    A(f"**Unexplained ids: {unex_n}.**" + ("  ✅ Every dead id has a recorded meaning."
      if not unex_n else "  ← investigate these; they are citations nobody can resolve."))
    A("")
    if dangling:
        A("| Task | Unexplained ids |")
        A("|-----:|:----------------|")
        for tid in sorted(dangling, key=lambda x: int(x) if x.isdigit() else 1 << 30):
            A(f"| [#{tid}](#c29c1332-{tid}) | " + " ".join(f"`{s}`" for s in dangling[tid]) + " |")
        A("")

    offint = sum(1 for v in anchors.values() if v.get("kind") == "commit" and v.get("onIntegration") is False)
    if offint:
        A(f"**A caveat the anchors carry, and the reason they are not just a lookup table:** {offint} of "
          "the re-anchored commits are *not ancestors of* `integration`. They survive only on "
          "`dev/upstream-merge` / `reorg/tooling`. On `integration` the whole Layer-1 arc is one "
          "squashed commit, `083c6340`. So citing the fine-grained commit alone is misleading in a "
          "second way, and each anchor records the HEAD carrier as well.")
        A("")
    aud = sum(1 for v in anchors.values() if v.get("audited"))
    over = sum(1 for v in anchors.values() if v.get("upheld") is False)
    if aud:
        A(f"Mappings resting on message-matching rather than a direct id link were sent to an "
          f"adversarial auditor instructed to refute them: **{aud} audited, {over} overturned** to "
          "`unresolvable`. A wrong anchor is worse than an absent one — it is authoritative-looking "
          "and points at the wrong commit, which is the exact failure this file exists to remove.")
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
            # EVIDENCE RENDERS FOR EVERY STORE, AND AMBIGUITY REFUSES RATHER THAN GUESSES.
            #
            # This read `if store is stores_data[0][0]` -- evidence for the FIRST store only. Stores
            # sort by mtime, newest first (find_stores), so which store that is depends on which one
            # was touched last. Restoring a second store from this very file demoted the main one and
            # silently stripped the evidence lines from 203 tasks: 95 records shrank, ~34k characters
            # of citation vanished, and the board still said "204 tasks" and reported itself up to
            # date. A board that quietly forgets what proves its own rows is worse than one that
            # never claimed to.
            #
            # The guard was not baseless: a commit stamped `[#4]` cannot be attributed when two
            # stores each hold a task 4, because the stamp carries no store. So the ambiguity is
            # named instead of being resolved by an accident of mtime -- the same rule the metrics
            # sampler follows when two clients match.
            if tid in ambiguous_ids:
                A(f"- evidence NOT ATTRIBUTED: id #{tid} exists in {ambiguous_ids[tid]} stores and a "
                  f"`[#{tid}]` stamp names no store, so any commit carrying it could belong to "
                  f"either. Disambiguate by renumbering, not by guessing.")
            else:
                for sha, subject in commits.get(tid, []):
                    A(f"- `{sha}` {cell(subject)}")
                for c in doccites.get(tid, []):
                    A(f"- cited in `{c}`")
            if not (tid in ambiguous_ids):
                if commits.get(tid) or doccites.get(tid):
                    A("")
            # SAME normalisation as the page -- see clean_desc. If only one of the two
            # renderings stripped it, the page would be showing a description the authority
            # does not have, which is precisely the drift this file exists to prevent.
            desc, _ = clean_desc(t.get("description") or "")
            desc = desc.rstrip()
            if desc:
                f = fence_for(desc)
                A(f)
                A(desc)
                A(f)
            else:
                A("_(no description recorded)_")
            A("")

    return "\n".join(L) + "\n"


def render_html(stores_data):
    """The same tasks as the markdown, as a page that can be scanned and filtered.

    DETERMINISTIC, like the markdown: no wall-clock stamp, sorted by store then id, so republishing
    an unchanged board produces an identical file. Every task field is HTML-ESCAPED -- descriptions
    are free text containing angle brackets, ampersands and shell snippets, and one unescaped `<`
    would silently eat the rest of a card.
    """
    import html as _h

    counts = {}
    for _, tasks in stores_data:
        for t in tasks:
            counts[t.get("status", "unknown")] = counts.get(t.get("status", "unknown"), 0) + 1
    total = sum(len(t) for _, t in stores_data)

    label = {"in_progress": "in progress", "pending": "pending", "completed": "completed"}
    stats = [f'<div class="stat"><b>{total}</b><span>tasks</span></div>']
    for st, n in sorted(counts.items(), key=lambda kv: STATUS_ORDER.get(kv[0], 9)):
        stats.append(f'<div class="stat"><b>{n}</b><span>{_h.escape(label.get(st, st))}</span></div>')
    stats.append(f'<div class="stat"><b>{len(stores_data)}</b><span>stores</span></div>')

    ranked, _done_ranked = ranked_tasks(stores_data)
    rank_of = {str(t.get("id")): r for r, _, t in ranked}
    by_id = {str(t.get("id")): t for _, tasks in stores_data for t in tasks}

    def blocker_note(t):
        """Whether this item is actually startable, which is the thing a Next list must not lie about."""
        live = [str(b) for b in (t.get("blockedBy") or [])
                if by_id.get(str(b), {}).get("status") != "completed"]
        if not live:
            return '<span class="tag t-ready">ready</span>'
        return ('<span class="tag t-blocked">blocked by '
                + ", ".join("#" + _h.escape(b) for b in live) + '</span>')

    # POSITION, NOT THE STORED RANK. The reader's question is "what do I do next, and what after
    # that" -- that is a position in a queue, and it starts at 1 whatever the keys happen to be.
    # The stored rank is a stable SORT KEY: it deliberately does NOT renumber when something
    # completes, because renumbering is churn and it invalidates any reference made to "rank 3".
    # Two different facts; showing the key where the position belongs made a correct list read as
    # broken. The key is still surfaced, quietly, so reprioritising does not require leaving the
    # page to discover what value to set.
    nexts = []
    for pos, (r, store, t) in enumerate(ranked, 1):
        tid = str(t.get("id", "?"))
        desc, _ = clean_desc(t.get("description", ""))
        first = next((ln.strip() for ln in desc.splitlines() if ln.strip()), "")
        nexts.append(
            f'<li class="nx"><span class="nrank">{pos}</span>'
            f'<span class="nkey" title="stored metadata.rank -- the sort key, not the position">'
            f'r{r}</span>'
            f'<a class="nid" href="#t{_h.escape(tid)}">#{_h.escape(tid)}</a>'
            f'<span class="nsub">{_h.escape(t.get("subject", "") or "")}'
            f'<em>{_h.escape(first[:150])}</em></span>'
            f'{blocker_note(t)}</li>')

    rows = []
    for store, tasks in stores_data:
        for t in tasks:
            tid = str(t.get("id", "?"))
            st = t.get("status", "unknown")
            subj = t.get("subject", "") or ""
            desc, _ = clean_desc(t.get("description", ""))
            r = rank_of.get(tid)
            # The haystack is lowercased ONCE here rather than per keystroke in the browser.
            hay = _h.escape(f"#{tid} {subj} {desc}".lower(), quote=True)
            body = (f'<pre>{_h.escape(desc)}</pre>' if desc.strip()
                    else '<p class="none">No description recorded.</p>')
            # --r drives CSS `order` when the Next facet is active, so that view is in RANK order
            # rather than id order. Pure CSS on the existing flex column; no DOM reordering.
            rows.append(
                f'<details class="row" id="t{_h.escape(tid)}" data-status="{_h.escape(st)}" '
                f'data-rank="{r if r is not None else ""}" data-hay="{hay}" '
                f'style="--r:{r if r is not None else 999}">'
                f'<summary><span class="rid">#{_h.escape(tid)}</span>'
                f'<span class="subj">{_h.escape(subj)}</span>'
                + (f'<span class="tag t-rank">next {r}</span>' if r is not None else '')
                + f'<span class="tag t-{_h.escape(st)}">{_h.escape(label.get(st, st))}</span>'
                f'<span class="store">{_h.escape(store.name[:8])}</span></summary>'
                f'<div class="body">{body}</div></details>')

    ranges = ", ".join(
        f"{s.name[:8]} ({len(t)} tasks)" for s, t in stores_data)
    footer = (f"generated by scripts/board-export.py from {len(stores_data)} store(s): {_h.escape(ranges)} "
              f"&middot; docs/board.md is the authority; this page is the same render "
              f"&middot; ids are per-store, not global "
              f"&middot; rank lives in the task's metadata.rank, so this list cannot drift from the board")

    problems = rank_problems(ranked, stores_data)
    warn = ("" if not problems else
            '<div class="rankwarn"><b>Ranking integrity</b><ul>'
            + "".join(f"<li>{_h.escape(p)}</li>" for p in problems) + "</ul></div>")

    nextblock = ("" if not nexts else
                 f'<section class="nextsec"><h2 class="sec">Next '
                 f'<small>{len(nexts)} ranked · set with <code>metadata.rank</code></small></h2>'
                 f'{warn}<ol class="nextlist">{"".join(nexts)}</ol></section>')

    return (TEMPLATE_PATH.read_text()
            .replace("__STATS__", "\n".join(stats))
            .replace("__NEXT__", nextblock)
            .replace("__ROWS__", "\n".join(rows))
            .replace("__FOOTER__", footer))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--store", type=pathlib.Path, action="append",
                    help="a specific task-store directory (repeatable). Default: every store found.")
    ap.add_argument("--store-root", type=pathlib.Path, default=DEFAULT_STORE_ROOT)
    ap.add_argument("--check", action="store_true", help="exit 1 if the output is stale; write nothing")
    ap.add_argument("--html", type=pathlib.Path, metavar="OUT",
                    help="also write the page rendering to OUT (same data, same load, one generator)")
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

    dangling, cited_total, noncommit, dead_all, anchors = dangling_index(stores_data)
    text = render(stores_data, commit_index(), doc_index(), dangling, cited_total, noncommit,
                  dead_all, anchors)

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

    # Written AFTER the markdown and from the SAME stores_data, so the page cannot be a rendering of
    # a different load. If this ever moves before the authority is written, a failure here would
    # leave a published page describing a board the repo does not have.
    if args.html:
        page = render_html(stores_data)
        args.html.parent.mkdir(parents=True, exist_ok=True)
        args.html.write_text(page)
        print(f"wrote {args.html} — same {n} tasks, {len(page)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
