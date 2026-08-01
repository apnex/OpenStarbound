#!/usr/bin/env python3
"""Measure the tree facts the design's ARGUMENTS rest on, and generate them into the spec.

WHY THIS EXISTS. Seven of the twenty-seven confirmed review findings were one defect wearing seven
hats: a number measured from the codebase, written into prose as a word, and never measured again.

  "the thirteen `render()` bodies"          -- there are 17
  "`ClientApplication` pushes in exactly four" -- it pushes 11
  "43 direct crossings across 10 edges"     -- grant-sweep says 55 across 12

Each was true the day it was written. Each then became load-bearing: the "thirteen" number is tier 2's
headline, the "exactly four" measurement is the whole basis of D4, and the "43 crossings" figure is
the delta's completion criterion. A design argument resting on a number nobody re-measures is an
argument resting on a memory.

WHAT THIS DOES AND DOES NOT COVER. The document contains dozens of tree measurements. This instrument
deliberately owns only the ones a DESIGN DECISION rests on -- the set below, each with the decision it
supports named in its row. Measuring all of them would be slower, flakier, and would quietly convert
every incidental figure into a gate. Numbers outside this set stay prose and stay the author's
responsibility; saying so is the difference between a bounded instrument and a hole.

THE DIVISION OF LABOUR, and it is what keeps `prose_claims` honest about its own limit:

  spec-measures  measures the TREE and writes the block.  Fails when the block is stale.
  prose_claims   compares the PROSE to the block.         Never touches the tree.

`prose_claims` states that it checks the document against itself and not against the world. That stays
true: the generated block IS part of the document. This script is the only thing here that greps C++.
"""
import argparse
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SPEC = REPO / "docs/superpowers/specs/2026-08-01-sovereign-headless-client-design.md"

BEGIN = "<!-- BEGIN GENERATED: scripts/spec-measures.py -->"
END = "<!-- END GENERATED: spec-measures -->"


def _files_matching(pattern, root, suffixes=(".hpp", ".cpp")):
    out = []
    for p in sorted((REPO / root).rglob("*")):
        if p.suffix not in suffixes or not p.is_file():
            continue
        try:
            if re.search(pattern, p.read_text(encoding="utf-8", errors="replace")):
                out.append(p.relative_to(REPO))
        except OSError:
            continue
    return out


def render_bodies():
    """Entity `render(RenderCallback...)` implementations -- pure-virtual declarations excluded.

    The distinction is the measurement: `StarRenderableItem.hpp` declares the hook and implements
    nothing, so counting declarations overstates the move by one and counting types understates it.
    """
    hits = []
    for p in _files_matching(r'void render\(RenderCallback', "source/game", (".hpp",)):
        text = (REPO / p).read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            if "void render(RenderCallback" in line:
                if not re.search(r'=\s*0\s*;', line):
                    hits.append(p)
                break
    return len(hits), "tier 2 -- the bodies that leave `game` for `world_view`"


def lua_callback_groups():
    """Global Lua callback groups the shell injects via `UniverseClient::setLuaCallbacks`.

    D4 rests on this number. It counts call sites in the client shell only: the definition in
    `StarUniverseClient.cpp` and the copy in `StarTestUniverse.cpp` are not the shell injecting.
    """
    src = (REPO / "source/client/StarClientApplication.cpp").read_text(encoding="utf-8",
                                                                      errors="replace")
    groups = re.findall(r'setLuaCallbacks\(\s*"([a-z]+)"', src)
    return len(groups), "D4 -- the client Lua surface a composition must state (%s)" % ", ".join(
        "`%s`" % g for g in groups)


def ratchet_crossings():
    """Direct include crossings still to be removed, and how many declared edges carry them.

    Read from grant-sweep rather than re-derived: two instruments counting the same thing with two
    regexes is the defect that produced spec-model.py.
    """
    r = subprocess.run([sys.executable, str(REPO / "scripts/grant-sweep.py"), "--check"],
                       capture_output=True, text=True, cwd=str(REPO))
    m = re.search(r'(\d+) crossings remain to be removed across (\d+) declared edges',
                  r.stdout + r.stderr)
    if not m:
        raise SystemExit("spec-measures: grant-sweep did not report a crossing count -- refusing to "
                         "guess. A measurement that cannot be taken is not a measurement of zero.")
    return "%s / %s" % (m.group(1), m.group(2)), "Section 10 -- the delta's completion criterion"


def render_callback_files():
    """Files naming `RenderCallback`, and whether they are still confined to `game`.

    Section 1 rests on the CONFINEMENT, not on the count: "it occurs in 39 files and all 39 are in
    `source/game`". So the second clause is what this asserts; the count merely travels with it.

    SUBSTRING, NOT WORD BOUNDARY, and the difference is one file. `\bRenderCallback\b` misses
    `StarWorldClient.cpp`, where every one of the thirteen occurrences reads `ClientRenderCallback` --
    the in-tree implementation of the hook, which is exactly the thing the claim is about. The
    word-boundary form silently reported 38 against the document's 39 and looked like a caught defect.
    This is the same shape as the `([A-Za-z]+)(?:Const)?Ptr` regex that swallowed its own suffix and
    returned zero for forty database searches: a tighter pattern is not a more correct one.
    """
    hits = _files_matching(r'RenderCallback', "source")
    outside = [p for p in hits if not str(p).startswith("source/game/")]
    where = "all in `game`" if not outside else "**%d outside `game`**" % len(outside)
    return "%d (%s)" % (len(hits), where), \
        "Section 1 -- `RenderCallback` is game-internal frame assembly, not a crossing"


MEASURES = [
    ("render bodies", render_bodies),
    ("lua callback groups", lua_callback_groups),
    ("RenderCallback files", render_callback_files),
    ("ratchet crossings / edges", ratchet_crossings),
]


def build():
    rows = ["| measure | value | what rests on it |", "|---|---:|---|"]
    for name, fn in MEASURES:
        value, why = fn()
        rows.append("| `%s` | **%s** | %s |" % (name, value, why))
    rows.append("")
    rows.append("*Measured from the tree by `scripts/spec-measures.py`, which owns exactly the "
                "figures a design decision rests on. Every other number in this document is prose "
                "and is the author's to keep true.*")
    return "\n".join(rows)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inject", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args(argv)

    table = build()
    if args.show:
        print(table)
        return 0

    text = SPEC.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        print("spec-measures: no marker pair in %s" % SPEC.name)
        return 1
    head, rest = text.split(BEGIN, 1)
    old, tail = rest.split(END, 1)
    want = "\n%s\n" % table

    if args.inject:
        SPEC.write_text(head + BEGIN + want + END + tail, encoding="utf-8")
        print("spec-measures: written -- %d measures" % len(MEASURES))
        return 0
    if old != want:
        print("spec-measures: STALE -- the tree moved; rerun `scripts/spec-measures.py --inject`")
        return 1 if args.check else 0
    print("spec-measures: OK -- %d measures match the tree" % len(MEASURES))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
