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
  2. Every grant edge points DOWN the zone order, verified at zero exceptions across 41 components.
     So `domain/ must not include device/` is a statement about PATHS, checkable without parsing C++.

Together those turn the architecture into something `#include` can violate and a lint can catch,
rather than something a reviewer has to hold in their head.
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

BEGIN = "<!-- BEGIN GENERATED: scripts/tree-map.py -->"
END = "<!-- END GENERATED: tree-map -->"

# Directories that are NOT components and never will be. Declared so the tree is the whole truth
# rather than the part the register happens to cover.
NON_COMPONENT = [
    ("source/extern/", "vendored third-party sources we do not architect: lua, fmt, xxhash, rpmalloc"),
    ("source/test/",   "the gates and unit tests; links whatever it measures"),
    ("scripts/",       "the instruments -- every gate in Section 6 lives here"),
    ("assets/",        "content, which `content` abstracts and no C++ component owns"),
]


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
    if BEGIN not in text or END not in text:
        print("tree-map: no marker pair in %s" % path.name)
        return 1
    head, rest = text.split(BEGIN, 1)
    old, tail = rest.split(END, 1)
    want = "\n%s\n" % build(text)

    if args.inject:
        path.write_text(head + BEGIN + want + END + tail, encoding="utf-8")
        print("tree-map: written")
        return 0
    if old != want:
        print("tree-map: STALE -- rerun `scripts/tree-map.py --inject`")
        return 1 if args.check else 0
    print("tree-map: OK -- the tree matches the register")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
