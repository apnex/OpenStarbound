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
import importlib.util
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
def _spec_model():
    """The one reader of the document -- and the one declaration of its path."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()
SPEC = MODEL.SPEC

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

COVERAGE_BEGIN = "<!-- BEGIN GENERATED: spec-measures coverage -->"
COVERAGE_END = "<!-- END GENERATED: spec-measures coverage -->"


def _model():
    spec = importlib.util.spec_from_file_location("spec_model",
                                                  str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _sweep_counts():
    """(verifiable grant rows, total) and (built ENTRYPOINTs, total), read from the owning gates."""
    m = _model()
    comp, grants, elem = m.load()
    entrypoints = sum(1 for v in comp.values() if v["kind"] == "ENTRYPOINT")

    gs = subprocess.run([sys.executable, str(REPO / "scripts/grant-sweep.py"), "--check"],
                        capture_output=True, text=True, cwd=str(REPO))
    unver = len(re.findall(r'^\s+UNVERIFIABLE\s', gs.stdout + gs.stderr, re.M))
    if not unver:
        raise SystemExit("spec-measures: grant-sweep reported no UNVERIFIABLE lines -- refusing to "
                         "report full anchoring coverage on a parse that found nothing.")

    # BUILT is read from link-sweep's ENTRYPOINT_BINARY DECLARATION, not from its UNBUILT output.
    # Counting the output made this block depend on whether the machine happened to have compiled:
    # green here with a build tree, STALE in CI with none, which is how it went red on 2026-08-02
    # after passing locally. A generated block whose content varies with the state of dist/ can never
    # be green in both places. The map is the build DEFINITION -- which entrypoints have a binary
    # target at all -- and that is the same in every checkout.
    ls = importlib.util.spec_from_file_location("link_sweep", str(REPO / "scripts/link-sweep.py"))
    lsm = importlib.util.module_from_spec(ls)
    ls.loader.exec_module(lsm)
    built = len(lsm.ENTRYPOINT_BINARY)
    return (len(comp), len(elem), len(grants) - unver, len(grants), built, entrypoints)


def build_coverage():
    """The honesty ledger: what each verdict establishes, and over how much of the design.

    Every number here was hand-written and every one had drifted -- "all 34 components and all 15
    elements" (41 and 24), "12 of 35 grant rows" (of 39), "2 of 6 ENTRYPOINTs" (of 7), "the 23
    components with no files yet" (27). A coverage claim that overstates its own denominator is worse
    than no coverage claim, because it is the sentence a reader trusts INSTEAD of checking.
    """
    ncomp, nelem, anchored, ngrants, built, neps = _sweep_counts()
    rows = [
        "| | what it establishes | coverage |",
        "|---|---|---|",
        "| **coherence** | the document does not contradict itself — each diagram against its "
        "register, drawn edges against the grant table, prose tallies against both, and **every "
        "runtime edge against the compile projection** | **all %d components and all %d elements.** "
        "Gated as `spec_consistency`; says nothing about correctness |" % (ncomp, nelem),
        "| **anchoring** | where a target name covers files that exist today, the grant row matches a "
        "measured *transitive* include closure | **%d of %d grant rows.** Gated as `grant_sweep` |"
        % (anchored, ngrants),
        "| **containment** | what each built ENTRYPOINT's binary actually contains, attributed "
        "symbol-by-symbol back to a component | **%d of %d ENTRYPOINTs** — the ones that exist. "
        "**Measured by `link_sweep`, and deliberately not gated**: it reads a build tree, and a gate "
        "that reads a build tree passes or fails on what someone last compiled rather than on what "
        "the repository says. This is the one row here whose coverage cannot ratchet in CI, and "
        "saying so is the difference between a limit and a hole |" % (built, neps),
        "| **correctness** | the designed system compiles, runs, and does what it claims | **zero.** "
        "Not obtainable before it is built |",
        "",
        "**%d of the %d components have no files yet** and their grant rows are pure assertion; "
        "`grant-sweep` reports them UNVERIFIABLE rather than passing them, which is the only honest "
        "verdict available. Generated by `scripts/spec-measures.py`."
        % (ngrants - anchored, ncomp),
    ]
    return "\n".join(rows)


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

    blocks = [(BEGIN, END, build()), (COVERAGE_BEGIN, COVERAGE_END, build_coverage())]
    if args.show:
        for _b, _e, table in blocks:
            print(table)
            print()
        return 0

    text = SPEC.read_text(encoding="utf-8")
    stale = []
    for begin, end, table in blocks:
        if text.count(begin) != 1 or text.count(end) != 1:
            print("spec-measures: expected exactly one %s / %s in %s" % (begin, end, SPEC.name))
            return 1
        head, rest = text.split(begin, 1)
        old, tail = rest.split(end, 1)
        want = "\n%s\n" % table
        if args.inject:
            text = head + begin + want + end + tail
        elif old != want:
            stale.append(begin)

    if args.inject:
        SPEC.write_text(text, encoding="utf-8")
        print("spec-measures: written -- %d measures + the coverage ledger" % len(MEASURES))
        return 0
    if stale:
        print("spec-measures: STALE -- %d block(s); rerun `scripts/spec-measures.py --inject`"
              % len(stale))
        return 1 if args.check else 0
    print("spec-measures: OK -- %d measures and the coverage ledger match the tree" % len(MEASURES))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
