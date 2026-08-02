#!/usr/bin/env python3
"""Generate one compile-time diagram per composition, derived from the grant table.

WHY THIS EXISTS. Section 4's diagram puts the whole system on one map, which answers "what exists" and
cannot answer "what does THIS binary actually link". Those are different questions and the second is
the one a reader building `client_headless` needs. Three more hand-drawn diagrams would be three more
artifacts to drift out of agreement with the register -- drift being the single largest source of
defects in this document's history -- so these are DERIVED.

A composition is an ENTRYPOINT plus the transitive closure of its grant list. That closure is already
written down; nothing here is a new decision. The generator only makes it visible, which is enough to
raise questions the whole-system map hides. The first run raised two:

  * `server` links `scene` -- the presentation vocabulary, in a process that never presents, because
    `game` grants `scene` and the server links `game`.
  * `client_headless` links `windowing` and `frontend` -- a widget toolkit and this game's screens, in
    a client that draws nothing.

Neither is answered here. A generator's job is to show the consequence of a decision, not to make one.

FRESHNESS. Same contract as `arch-graph.py`: `--inject` rewrites the blocks between markers, `--check`
fails when they no longer match what the register implies. A generated diagram nobody regenerates is
worse than no diagram, because it looks authoritative while being stale.
"""
import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

def _spec_model():
    """Import the ONE table reader. Hyphenated filenames cannot be imported normally; restating its
    regexes here instead is the defect it exists to delete -- three parsers once gave three different
    counts of one table."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()
SPEC = MODEL.SPEC          # one declaration of where the document lives

MARK_BEGIN = "<!-- BEGIN GENERATED: scripts/composition-graphs.py#%s -->"
MARK_END = "<!-- END GENERATED: %s -->"

KINDS = ("FOUNDATION", "INTERFACE", "VOCABULARY", "BACKEND", "LIBRARY", "ENTRYPOINT")
ZONES = ("MACHINE", "DOMAIN", "DEVICE", "COMPOSITION")
ZONE_TITLE = {"MACHINE": "MACHINE — the OS, the vendor, the asset store",
              "DOMAIN": "DOMAIN — the game's own state and rules",
              "DEVICE": "DEVICE — meets a display, a speaker, a file",
              "COMPOSITION": "COMPOSITION — wires the rest"}
CLASS_OF = {"FOUNDATION": "kFoundation", "INTERFACE": "kContract", "VOCABULARY": "kVocabulary",
            "BACKEND": "kBackend",
            "LIBRARY": "kLibrary", "ENTRYPOINT": "kEntrypoint"}

# The palette is READ FROM Section 4's map, never restated here.
#
# It used to be a pasted copy, under a comment asserting "Same palette as Section 4's map, so a reader
# moving between them is not relearning colours." That comment was false the day it was written -- the
# two palettes shared not one colour. A FOUNDATION was #3d3d3d grey here and #23282f slate there; a
# CONTRACT was blue here and amber there; a BACKEND purple here and red there. So a reader moving
# between the whole-system map and a per-composition view had to relearn every colour, which is the
# exact cost the comment claimed to have avoided. One knob, two declarations, and the second one lying
# -- the defect #185 exists to delete, in its most embarrassing form: a comment as the only evidence.
CLASSDEF_LINE = re.compile(r'^\s*classDef\s+(k\w+)\s+(.+?)\s*$', re.M)


def palette(text):
    """-> the five component classDefs, lifted verbatim from the compile projection.

    Scoped to that diagram rather than the whole document so a stray classDef elsewhere cannot
    silently win. Hard-fails on a missing kind: a generator that quietly drops a colour produces a
    diagram whose boxes all look alike, which reads as a rendering glitch rather than a broken tool."""
    start = text.find("%% projection: compile")
    if start < 0:
        raise SystemExit("composition-graphs: no compile projection in the spec -- cannot read the palette")
    end = text.find("```", start)
    found = dict(CLASSDEF_LINE.findall(text[start:end if end > 0 else len(text)]))
    missing = [c for c in list(CLASS_OF.values()) + ["kElement"] if c not in found]
    if missing:
        raise SystemExit("composition-graphs: the compile diagram declares no %s -- palette incomplete"
                         % ", ".join(missing))
    return "\n".join("  classDef %s %s" % (c, found[c])
                     for c in list(CLASS_OF.values()) + ["kElement"])

COMPONENT_ROW = re.compile(
    r'\|\s*\*\*`(\w+)`\*\*\s*\|\s*(' + "|".join(KINDS) + r')\s*\|\s*(' + "|".join(ZONES) +
    r')\s*\|\s*([^|]+?)\s*\|')
GRANT_ROW = re.compile(r'^\|\s*`(\w+)`\s*\|\s*([^|]+?)\s*\|', re.M)

# LOOP elements, so a composition diagram shows not just what a binary CONTAINS but what in it has a
# clock. A reader asking "what drives this?" could not answer it from these diagrams before; the
# whole-system map showed the loops and the per-composition views dropped them.
ELEMENT_ROW = re.compile(
    r'\|\s*\*\*`(\w+)`\*\*\s*\|\s*(LOOP|TICK)\s*\|\s*(\w+)\s*\|\s*\*\*(\w+)\*\*\s*\|\s*`(\w+)`\s*\|\s*`\w+`\s*\|')


def loops_of(text):
    """-> owner component -> [(name, kind, cadence, cardinality)] for everything that RUNS REPEATEDLY.

    LOOP and TICK only. WIRING runs once and SIGNAL is an event; drawing them would answer a question
    nobody asks of a composition diagram. Empty is legal and common -- most components are called."""
    out = {}
    for name, e in MODEL.elements(text).items():
        if e["kind"] in ("LOOP", "TICK"):
            out.setdefault(e["owner"], []).append((name, e["kind"], e["cadence"], e["cardinality"]))
    return out


def parse(text):
    """Delegates: see MODEL. The local COMPONENT_ROW/GRANT_ROW regexes are gone, and with them the
    22 rows from other tables that used to be read as grants."""
    return MODEL.components(text), MODEL.grants(text)


def closure(root, comp, grants):
    seen, stack = set(), [root]
    while stack:
        c = stack.pop()
        if c in seen:
            continue
        seen.add(c)
        stack.extend(x for x in grants.get(c, ()) if x in comp and x not in seen)
    return seen


def diagram(entry, comp, grants, classdef, loops):
    linked = closure(entry, comp, grants)
    absent = sorted(set(comp) - linked)
    out = ["```mermaid", "%%%% composition: %s" % entry, "flowchart TD"]
    for zone in ZONES:
        members = sorted(n for n in linked if comp[n]["zone"] == zone)
        if not members:
            continue
        out.append('  subgraph Z_%s ["%s"]' % (zone, ZONE_TITLE[zone]))
        for n in members:
            mine = loops.get(n, ())
            if not mine:
                out.append('    %s["<b>%s</b><br/>%s"]' % (n, n, comp[n]["kind"]))
                continue
            # Same shape as the compile map: a component that owns a clock becomes a container with
            # its loops inside. Subgraphs are legal edge endpoints in mermaid, so the id is unchanged
            # and every grant edge below still resolves.
            out.append('    subgraph %s ["<b>%s</b> · %s"]' % (n, n, comp[n]["kind"]))
            for lname, kind, cadence, card in sorted(mine):
                out.append('      %s_%s(["<b>%s</b> · %s<br/><i>%s · one per %s</i>"])'
                           % (n, lname, lname, kind, cadence, card.lower()))
            out.append("    end")
        out.append("  end")
    for a in sorted(linked):
        for b in sorted(grants.get(a, ())):
            if b in linked and b != a:
                out.append("  %s --> %s" % (a, b))
    out.append(classdef)
    for kind in KINDS:
        members = sorted(n for n in linked if comp[n]["kind"] == kind)
        if members:
            out.append("  class %s %s" % (",".join(members), CLASS_OF[kind]))
    elems = ["%s_%s" % (n, ln) for n in sorted(linked) for ln, _k, _c, _d in loops.get(n, ())]
    if elems:
        out.append("  class %s kElement" % ",".join(sorted(elems)))
    out.append("```")
    out.append("")
    out.append("**%s links %d of %d components.** Not linked: %s"
               % (entry, len(linked), len(comp),
                  ", ".join("`%s`" % n for n in absent) if absent else "*nothing*"))
    return "\n".join(out)


def blocks(text):
    comp, grants = parse(text)
    entries = sorted(n for n, v in comp.items() if v["kind"] == "ENTRYPOINT")
    if not entries:
        raise SystemExit("composition-graphs: no ENTRYPOINT components found in the register")
    if len(comp) < 15:
        raise SystemExit("composition-graphs: only %d components parsed; the register regex has "
                         "regressed" % len(comp))
    loops = loops_of(text)
    return {e: diagram(e, comp, grants, palette(text), loops) for e in entries}


def apply(text, generated, inject):
    stale = []
    for name, body in generated.items():
        begin, end = MARK_BEGIN % name, MARK_END % name
        if begin not in text or end not in text:
            stale.append("%s: no marker pair in the document" % name)
            continue
        head, rest = text.split(begin, 1)
        _old, tail = rest.split(end, 1)
        want = "\n%s\n" % body
        if _old != want:
            stale.append("%s: block does not match the grant table" % name)
        text = head + begin + want + end + tail if inject else text
    return text, stale


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inject", action="store_true", help="rewrite the generated blocks in place")
    ap.add_argument("--check", action="store_true", help="exit 1 if any block is stale")
    ap.add_argument("--spec", default=str(SPEC))
    args = ap.parse_args(argv)

    path = pathlib.Path(args.spec)
    text = path.read_text(encoding="utf-8")
    generated = blocks(text)
    new, stale = apply(text, generated, args.inject)

    if args.inject:
        path.write_text(new, encoding="utf-8")
        print("composition-graphs: %d block(s) written -- %s"
              % (len(generated), ", ".join(sorted(generated))))
        return 0
    for s in stale:
        print("  STALE  %s" % s)
    if stale:
        print("composition-graphs: FAIL -- rerun `scripts/composition-graphs.py --inject`")
        return 1 if args.check else 0
    print("composition-graphs: OK -- %d composition diagram(s) match the grant table"
          % len(generated))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
