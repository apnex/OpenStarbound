#!/usr/bin/env python3
"""Generate the target directory tree from the component register.

WHY THIS EXISTS. Zones became directories, which means the register is now a filesystem layout and
not merely a classification. A hand-written tree beside a 41-row register is a second declaration of
the same fact, and every drift defect this document has produced has been exactly that. So the tree
is derived: `source/<zone>/<component>/`, one directory per component, zones ordered so that reading
the tree top to bottom reads the dependency order.

WHAT MAKES THE LAYOUT ENFORCEABLE, and it is two facts stacked:

  1. A component is enforceable if and only if it is its own directory -- the OBJECT-library finding.
     Every Star library links ALL of its objects into every consumer, so containment is decided by
     which directories an ENTRYPOINT names. A component that is not a directory cannot be excluded
     from anything.
  2. Every grant edge points DOWN the zone order, verified at zero exceptions across every component.
     So `domain/ must not include device/` is a statement about PATHS, checkable without parsing C++.

Together those turn the architecture into something `#include` can violate and a lint can catch,
rather than something a reviewer has to hold in their head.

TWO BLOCKS, AND THE SECOND ONE IS WHY THIS DOCSTRING NO LONGER SAYS "41". Section 7 introduces the
zones with a table carrying a per-zone COUNT, and that table was hand-written. It said machine 14 and
domain 14 -- a total of 45 -- against a 49-row register: stale since the `celestial` split added
three and `transport_p2p` added one. It went green through every gate, because no instrument read it;
the table census classes it UNREAD, which is exactly the category that looks authoritative and is
checked by nobody. The tree here was already derived from the same grouping, so the tally is the same
computation printed a second way, and it is now generated rather than typed.
"""
import argparse
import importlib.util
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def _spec_model():
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()

# Two blocks now, so the markers are per-name. The tree keeps the original unsuffixed pair -- renaming
# it would have been a pure churn edit across a 6,000-line document for no gain.
MARKERS = {
    "tree": ("<!-- BEGIN GENERATED: scripts/tree-map.py -->",
             "<!-- END GENERATED: tree-map -->"),
    "zones": ("<!-- BEGIN GENERATED: scripts/tree-map.py#zones -->",
              "<!-- END GENERATED: tree-map#zones -->"),
}

# Directories that are NOT components and never will be. Declared so the tree is the whole truth
# rather than the part the register happens to cover.
NON_COMPONENT = [
    ("source/extern/", "vendored third-party sources we do not architect: lua, fmt, xxhash, rpmalloc"),
    ("source/test/",   "the gates and unit tests; links whatever it measures"),
    ("scripts/",       "the instruments -- every gate in Section 6 lives here"),
    ("assets/",        "content, which `content` abstracts and no C++ component owns"),
]


def build_zones(text):
    """Section 7's zone tally: what each zone faces, and how many components face it.

    The `faces` column is prose and still has to come from somewhere; it comes from ZONE_FACES in
    `spec-model`, which is the ONE declaration of it and is also what the composition diagrams title
    their subgraphs with. The count is the only thing that moves, and it moves on its own now."""
    comp = MODEL.components(text)
    rows = ["| zone | faces | n |", "|---|---|---:|"]
    for z in MODEL.ZONES:
        n = sum(1 for v in comp.values() if v["zone"] == z)
        rows.append("| **`%s/`** | %s | %d |" % (z.lower(), MODEL.ZONE_FACES[z], n))
    # The total is stated rather than left to the reader's addition, because the defect this block
    # replaces was a column that summed to 45 beside a register of 49 -- visible only to someone who
    # added it up, which for four numbers across four months nobody did.
    rows.append("| | **total** | **%d** |" % len(comp))
    return "\n".join(rows)


def build(text):
    comp = MODEL.components(text)
    out = ["```", "source/"]
    for z in MODEL.ZONES:
        members = sorted(n for n, v in comp.items() if v["zone"] == z)
        out.append("  %s/%s# %d components" % (z.lower(), " " * (14 - len(z)), len(members)))
        for i, n in enumerate(members):
            branch = "└──" if i == len(members) - 1 else "├──"
            out.append("    %s %-16s %s" % (branch, n + "/", comp[n]["kind"]))
    out.append("")
    for path, why in NON_COMPONENT:
        out.append("%-18s %s" % (path, "# " + why))
    out.append("```")
    out.append("")
    out.append("**%d components in %d zone directories.** Reading top to bottom is reading the "
               "dependency order: every grant points down this list, checked by `spec_consistency`'s "
               "ZONE_ORDER verdict at zero exceptions." % (len(comp), len(MODEL.ZONES)))
    return "\n".join(out)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inject", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)

    path = MODEL.SPEC
    text = path.read_text(encoding="utf-8")
    bodies = {"tree": build(text), "zones": build_zones(text)}

    stale = []
    for name, body in bodies.items():
        begin, end = MARKERS[name]
        # A missing marker pair is a hard failure, never a skip. The block this check exists to keep
        # honest is one that was UNREAD for months; "no marker, so nothing to compare" would put it
        # straight back into that state while printing OK.
        if text.count(begin) != 1 or text.count(end) != 1:
            print("tree-map: expected exactly one %r marker pair in %s" % (name, path.name))
            return 1
        head, rest = text.split(begin, 1)
        old, tail = rest.split(end, 1)
        want = "\n%s\n" % body
        if old != want:
            stale.append(name)
        if args.inject:
            text = head + begin + want + end + tail

    if args.inject:
        path.write_text(text, encoding="utf-8")
        print("tree-map: written -- %s" % ", ".join(sorted(bodies)))
        return 0
    if stale:
        print("tree-map: STALE (%s) -- rerun `scripts/tree-map.py --inject`" % ", ".join(sorted(stale)))
        return 1 if args.check else 0
    print("tree-map: OK -- the tree and the zone tally match the register")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
