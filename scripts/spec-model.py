#!/usr/bin/env python3
"""THE ONE READER of the headless-client design's tables. Every gate imports this; none re-implements it.

WHY THIS EXISTS, and it is the most embarrassing defect this document has produced.

Three tools parsed the same grant table with three regexes and got three answers: grant-sweep 44,
spec-consistency 62, composition-graphs 62. Nobody noticed for the length of a design session, because
each tool printed only its own number and every one of them was internally consistent. A count nobody
compares is not a measurement -- it is three opinions in a trenchcoat.

The false positives came from shape-guessing. `GRANT_ROW` matched any markdown row beginning with a
backticked word, so 22 rows from OTHER tables -- `Drawable`, `Renderer`, `clientTick`, `planet_mapgen`
-- were read as grants. They were harmless only by luck: nothing referenced them in a closure. The
next table added with a backticked first column would have silently joined the grant graph.

THE FIX IS NOT A BETTER REGEX. A better regex is a better guess, and the guess is the defect. Three
rules, in order of importance:

  1. TABLES ARE DELIMITED, NOT DETECTED. Each table sits between `<!-- TABLE: name -->` and
     `<!-- END TABLE: name -->`. Parsing is scoped to that region, so a row's meaning comes from where
     it IS, not from what it looks like. This is the same device `projections()` uses for the two
     mermaid diagrams and `--inject` uses for the generated blocks; it has worked both times.

  2. A MISSING MARKER IS A HARD FAILURE, never a fallback. A parser that cannot find its input must
     say so. `oracle_vocabulary_trap` and VACUOUS are both in this project's history because silence
     read as success.

  3. ONE IMPLEMENTATION. Consumers import this module rather than restating its regexes, so the three
     counts cannot disagree -- there is only one count. That is #185's one-knob rule applied to a
     parser instead of a config value.

FLOORS. Each table declares a minimum plausible row count. Below it the parse is not believable and
this raises rather than returning a short answer, because a gate that checks four rows of a forty-row
table reports OK just as loudly as one that checks all forty.
"""
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SPEC = REPO / "docs/superpowers/specs/2026-08-01-sovereign-headless-client-design.md"

KINDS = ("FOUNDATION", "CONTRACT", "BACKEND", "LIBRARY", "ENTRYPOINT")
ZONES = ("MACHINE", "DOMAIN", "DEVICE", "COMPOSITION")
# Ordered: every grant edge points DOWN this list, verified at zero exceptions. That is what makes
# zones directories rather than labels -- a path lint can enforce the layering.
ZONE_ORDER = {z: i for i, z in enumerate(("MACHINE", "DOMAIN", "DEVICE", "COMPOSITION"))}
EKINDS = ("LOOP", "TICK", "WIRING", "SIGNAL")
CADENCES = ("DISPLAY", "FIXED", "FREE", "EXTERNAL", "DERIVED", "ONCE", "EVENT")
CARDINALITIES = ("PROCESS", "PARTICIPANT", "UNIVERSE", "WORLD", "DEVICE")

# Floors, not targets. Raise when the design genuinely grows; never lower to make a red gate green.
FLOOR = {"components": 35, "grants": 33, "elements": 20}

MARK = "<!-- TABLE: %s -->"
ENDMARK = "<!-- END TABLE: %s -->"

_CELL = re.compile(r'`([\w_]+)`')


def region(text, name):
    """The lines strictly between this table's markers. Hard-fails on absence or duplication."""
    begin, end = MARK % name, ENDMARK % name
    if text.count(begin) != 1 or text.count(end) != 1:
        raise SystemExit("spec-model: expected exactly one %s and one %s in %s (found %d / %d)"
                         % (begin, end, SPEC.name, text.count(begin), text.count(end)))
    body = text.split(begin, 1)[1].split(end, 1)[0]
    if body.find(MARK % "") != -1:
        raise SystemExit("spec-model: nested table markers inside %s" % name)
    return [l for l in body.splitlines() if l.startswith("|")]


def _rows(text, name):
    """Data rows only: the header and its |---| separator are dropped by shape, inside the region."""
    out = []
    for line in region(text, name):
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if not cells or all(set(c) <= set("-: ") for c in cells):
            continue
        out.append(cells)
    if len(out) < FLOOR[name]:
        raise SystemExit("spec-model: table %r yielded %d rows, floor is %d -- the parse is not "
                         "believable" % (name, len(out), FLOOR[name]))
    return out


def components(text):
    """-> {name: {kind, zone, duty, contents}}"""
    out = {}
    for cells in _rows(text, "components"):
        m = re.fullmatch(r'\*\*`([\w_]+)`\*\*', cells[0])
        if not m:
            continue                      # the header row, whose first cell is the word "name"
        if cells[1] not in KINDS or cells[2] not in ZONES:
            raise SystemExit("spec-model: component `%s` has kind %r zone %r, which are not in the "
                             "taxonomy" % (m.group(1), cells[1], cells[2]))
        out[m.group(1)] = dict(kind=cells[1], zone=cells[2], duty=cells[3],
                               contents=cells[4] if len(cells) > 4 else "")
    return out


def grants(text):
    """-> {component: set(of component names it may name)}. `extern` is vendored and out of scope."""
    out = {}
    for cells in _rows(text, "grants"):
        m = re.fullmatch(r'`([\w_]+)`', cells[0])
        if not m:
            continue                      # header
        listed = {x for x in _CELL.findall("`" + cells[1].replace(",", "` `") + "`")}
        listed = {x for x in (c.strip(" `*") for c in cells[1].replace("**", "").split(","))
                  if x and x != "extern"}
        out[m.group(1)] = listed
    return out


def elements(text):
    """-> {name: {kind, cadence, cardinality, owner, thread, duty}}"""
    out = {}
    for cells in _rows(text, "elements"):
        m = re.fullmatch(r'\*\*`([\w_]+)`\*\*', cells[0])
        if not m:
            continue
        card = cells[3].strip("*")
        owner = cells[4].strip("`")
        thread = cells[5].strip("`")
        if cells[1] not in EKINDS or cells[2] not in CADENCES:
            raise SystemExit("spec-model: element `%s` has kind %r cadence %r, not in the taxonomy"
                             % (m.group(1), cells[1], cells[2]))
        out[m.group(1)] = dict(kind=cells[1], cadence=cells[2], cardinality=card,
                               owner=owner, thread=thread, duty=cells[6] if len(cells) > 6 else "")
    return out


def load(path=None):
    """-> (components, grants, elements) from one read of the spec."""
    text = pathlib.Path(path or SPEC).read_text(encoding="utf-8")
    return components(text), grants(text), elements(text)


if __name__ == "__main__":
    c, g, e = load(sys.argv[1] if len(sys.argv) > 1 else None)
    print("spec-model: %d components, %d grant rows, %d elements" % (len(c), len(g), len(e)))
    missing = sorted(n for n, v in c.items() if v["kind"] != "FOUNDATION" and n not in g)
    if missing:
        print("  components with no grant row: %s" % ", ".join(missing))
    stray = sorted(set(g) - set(c))
    if stray:
        print("  grant rows naming no component: %s" % ", ".join(stray))
        sys.exit(1)
