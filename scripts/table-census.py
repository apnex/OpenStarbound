#!/usr/bin/env python3
"""Census every table in the spec, and say which ones an instrument actually reads.

WHY THIS EXISTS. The Director asked a one-sentence question -- *is every table generated from a
source?* -- and I answered it three times, wrongly, from the command line:

    191 tables, 4 read     grep counted the `| | |` header rows Section 5's decision tables use as
                           though they were separators, inflating the total by a third
    158 tables, 68 read    counted EVERY table inside Section 10 as a derivation; 18 of them are
                           ordinary prose tables that merely live there
    158 tables, 55 read    classified by ROW LABEL instead, so Section 5's decision tables matched
                           on `**rejected**` and Section 16's risk tables matched on their own shape

The correct answer is 158 tables, 50 read: 45 component derivations and 5 delimited tables. Each
wrong answer was checked, quotable, and confidently given. That is what a number held in prose is
worth. The rule this project already had -- numbers are owned by instruments, not by sentences --
applies to an answer in conversation exactly as it applies to a claim in the document, and this
script is the instrument.

TWO FINDINGS FELL OUT OF GETTING IT RIGHT, and neither was reachable from the wrong counts:

  * Section 16's risk facets carry a four-facet shape as disciplined as Section 10's, and NO SCRIPT
    MENTIONS THEM. They look read and are not.
  * `spec-derivations` decides a heading naming exactly one component IS that component's
    derivation. `### transcript's three modes` is such a heading and is not a derivation. It carries
    no facet rows so nothing breaks today, but a `| **boundary** |` row added beneath it would be
    attributed to `transcript` and would silently overwrite the real one.

WHAT A TABLE IS. A maximal run of consecutive lines beginning with `|`, outside fenced code blocks.
Counting SEPARATOR rows instead is the mistake above: `| | |` is a legal header, and Section 5 uses it
throughout. This definition cannot be fooled by an empty header, and it agrees with the separator
method once the separator method is told to require a header line above it -- two methods, one answer,
which is the only reason to trust either.

WHAT "READ" MEANS. Not "looks structured". A table is READ when a named instrument parses it:

  DELIMITED   inside `<!-- TABLE: name -->` .. `<!-- END TABLE: name -->`, parsed by `spec-model.py`.
  DERIVATION  inside Section 10, carrying the six facet labels, parsed by `spec-derivations.py`.
  GENERATED   inside a `BEGIN GENERATED` block -- written rather than read.

Everything else is UNREAD, however official it looks. Section 16's risk facets are the case worth
naming: they carry a four-facet shape as disciplined as Section 10's, and NO SCRIPT MENTIONS THEM.
A table that resembles a read table is not a read table, and the resemblance is what makes it worth
counting separately.

TWO CONVENTIONS, AND ONE OF THEM IS THE ONE THE OTHER FORBIDS. `spec-model.py`'s first principle is
*"TABLES ARE DELIMITED, NOT DETECTED ... a row's meaning comes from where it IS, not from what it
looks like."* `spec-derivations.py` detects: it finds facet rows by their labels within Section 10.
Both work today. The census reports the split because the day a table outside Section 10 grows a
`**boundary**` row, or a derivation moves, only one of those two conventions notices.

This is a MEASUREMENT, not a ratchet. There is no ceiling on unread tables -- most of the document's
tables are illustrative and should stay prose, and a gate that punished adding one would be pushing
toward machine-readability as an end in itself. What it exists to do is make the number reproducible,
so nobody has to take mine.
"""
import argparse
import importlib.util
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, REPO / path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _load("spec_model", "scripts/spec-model.py")
DERIV = _load("spec_derivations", "scripts/spec-derivations.py")

_ROW = re.compile(r'^\|\s*\*\*(\w+)[^|*]*\*\*\s*\|(.*)\|\s*$')
_SEP = re.compile(r'^\|[\s:|-]+\|\s*$')


def tables(lines):
    """Maximal runs of consecutive `|` lines outside code fences."""
    fence, out, cur = False, [], []
    for i, ln in enumerate(lines, 1):
        if ln.lstrip().startswith("```"):
            fence = not fence
            if cur:
                out.append(cur)
                cur = []
            continue
        if not fence and ln.startswith("|"):
            cur.append((i, ln))
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return out


def _spans(lines, opener, closer):
    out, start = [], None
    for i, ln in enumerate(lines, 1):
        if opener in ln:
            start = i
        elif closer in ln and start is not None:
            out.append((start, i))
            start = None
    return out


def _marked(lines):
    out = []
    for i, ln in enumerate(lines, 1):
        if "<!-- TABLE:" not in ln:
            continue
        name = ln.split("TABLE:")[1].split("-->")[0].strip()
        for j, other in enumerate(lines, 1):
            if j > i and "END TABLE: %s" % name in other:
                out.append((i, j))
                break
    return out


def _heading_above(lines, line, lo):
    """The nearest `###` heading above a line, or None if the line precedes them all."""
    for i in range(line - 1, lo - 1, -1):
        m = DERIV._H3.match(lines[i - 1])
        if m:
            return m.group(1)
    return None


def census(text):
    lines = text.splitlines()
    blocks = tables(lines)

    # Cross-check with the independent separator method. Two methods or the count is one person's
    # opinion -- which is how this file came to exist.
    sep = [i for i, ln in enumerate(lines, 1)
           if _SEP.match(ln) and lines[i - 2].lstrip().startswith("|")]

    generated = _spans(lines, "BEGIN GENERATED", "END GENERATED")
    delimited = _marked(lines)
    lo, hi = DERIV.derivation_region(lines)
    facets = set(DERIV.FACETS)

    def within(n, spans):
        return any(a <= n <= b for a, b in spans)

    out = {"total": len(blocks), "separator_method": len(sep), "delimited": 0,
           "derivation": 0, "generated": 0, "unread": 0, "unread_lines": [],
           "components": len(MODEL.components(text)), "legend": 0}
    for b in blocks:
        top = b[0][0]
        labels = {m.group(1).lower() for _, l in b if (m := _ROW.match(l))}
        if within(top, delimited):
            out["delimited"] += 1
        elif within(top, generated):
            out["generated"] += 1
        elif lo <= top <= hi and len(labels & facets) >= 4:
            # THE LEGEND is the one facet-shaped table that sits above every `###` heading: Section
            # 10's opening table, which names the six facets rather than answering them.
            if _heading_above(lines, top, lo) is None:
                out["legend"] += 1
            else:
                out["derivation"] += 1
        else:
            out["unread"] += 1
            out["unread_lines"].append(top)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="report the census")
    ap.add_argument("--verbose", action="store_true", help="list the unread tables' line numbers")
    args = ap.parse_args(argv)

    c = census(MODEL.SPEC.read_text(encoding="utf-8"))
    if c["total"] != c["separator_method"]:
        print("table-census: FAIL -- the two counting methods disagree (%d runs vs %d separators). "
              "A table count nobody can reproduce two ways is the defect this file exists about"
              % (c["total"], c["separator_method"]))
        return 1

    if c["derivation"] != c["components"] or c["legend"] != 1:
        print("table-census: FAIL -- %d derivation tables and %d legend(s) against %d registered "
              "components. Expected one derivation each, plus exactly one legend. A gap means a "
              "derivation was added, lost, or written in a shape no instrument reads"
              % (c["derivation"], c["legend"], c["components"]))
        return 1

    read = c["delimited"] + c["derivation"]
    print("table-census: %d tables -- %d read, %d generated, %d unread"
          % (c["total"], read, c["generated"], c["unread"] + c["legend"]))
    print("    %3d  READ      Section 10 component derivations, parsed by spec-derivations"
          % c["derivation"])
    print("    %3d  READ      delimited by a TABLE: marker, parsed by spec-model" % c["delimited"])
    print("    %3d  WRITTEN   inside a generated block" % c["generated"])
    print("    %3d  UNREAD    prose tables no instrument parses (incl. %d facet-shape legend)"
          % (c["unread"] + c["legend"], c["legend"]))
    if args.verbose:
        print("    unread at lines: %s" % ", ".join(str(n) for n in c["unread_lines"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
