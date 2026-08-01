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
SPEC = REPO / "docs/superpowers/specs/2026-08-01-sovereign-headless-client-design.md"

MARK_BEGIN = "<!-- BEGIN GENERATED: scripts/composition-graphs.py#%s -->"
MARK_END = "<!-- END GENERATED: %s -->"

KINDS = ("FOUNDATION", "CONTRACT", "BACKEND", "LIBRARY", "ENTRYPOINT")
ZONES = ("SUBSTRATE", "SEAM", "INTERIOR", "PERIPHERY", "SHELL")
ZONE_TITLE = {"SUBSTRATE": "SUBSTRATE", "SEAM": "SEAM", "INTERIOR": "INTERIOR",
              "PERIPHERY": "PERIPHERY", "SHELL": "SHELL"}
# Same palette as Section 4's map, so a reader moving between them is not relearning colours.
CLASSDEF = """  classDef kFoundation fill:#3d3d3d,stroke:#1f1f1f,color:#fff
  classDef kContract fill:#1f4e79,stroke:#0f2d46,color:#fff
  classDef kBackend fill:#7a3e9d,stroke:#4d2763,color:#fff
  classDef kLibrary fill:#2e6da4,stroke:#1f4e79,color:#fff
  classDef kEntrypoint fill:#1d6b4f,stroke:#0e3a2a,color:#fff"""
CLASS_OF = {"FOUNDATION": "kFoundation", "CONTRACT": "kContract", "BACKEND": "kBackend",
            "LIBRARY": "kLibrary", "ENTRYPOINT": "kEntrypoint"}

COMPONENT_ROW = re.compile(
    r'\|\s*\*\*`(\w+)`\*\*\s*\|\s*(' + "|".join(KINDS) + r')\s*\|\s*(' + "|".join(ZONES) +
    r')\s*\|\s*([^|]+?)\s*\|')
GRANT_ROW = re.compile(r'^\|\s*`(\w+)`\s*\|\s*([^|]+?)\s*\|', re.M)


def parse(text):
    comp = {n: dict(kind=k, zone=z, duty=d.strip()) for n, k, z, d in COMPONENT_ROW.findall(text)}
    grants = {n: set(re.findall(r'\w+', g.replace('*', '')))
              for n, g in GRANT_ROW.findall(text) if re.fullmatch(r'[\w,` *]+', g)}
    return comp, grants


def closure(root, comp, grants):
    seen, stack = set(), [root]
    while stack:
        c = stack.pop()
        if c in seen:
            continue
        seen.add(c)
        stack.extend(x for x in grants.get(c, ()) if x in comp and x not in seen)
    return seen


def diagram(entry, comp, grants):
    linked = closure(entry, comp, grants)
    absent = sorted(set(comp) - linked)
    out = ["```mermaid", "%%%% composition: %s" % entry, "flowchart TD"]
    for zone in ZONES:
        members = sorted(n for n in linked if comp[n]["zone"] == zone)
        if not members:
            continue
        out.append('  subgraph Z_%s ["%s"]' % (zone, ZONE_TITLE[zone]))
        for n in members:
            out.append('    %s["<b>%s</b><br/>%s"]' % (n, n, comp[n]["kind"]))
        out.append("  end")
    for a in sorted(linked):
        for b in sorted(grants.get(a, ())):
            if b in linked and b != a:
                out.append("  %s --> %s" % (a, b))
    out.append(CLASSDEF)
    for kind in KINDS:
        members = sorted(n for n in linked if comp[n]["kind"] == kind)
        if members:
            out.append("  class %s %s" % (",".join(members), CLASS_OF[kind]))
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
    return {e: diagram(e, comp, grants) for e in entries}


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
