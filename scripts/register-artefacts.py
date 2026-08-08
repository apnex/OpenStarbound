#!/usr/bin/env python3
"""Does the TSSA component register still describe the tree's artefacts?

WHY THIS EXISTS. The register's contents column names each component's load-bearing types in prose:
`metrics` said "MetricSample ... ClientBusyReader and EngineBusyReader, two kernel paths to one truth".
By 2026-08-07 the tree also had ThreadBusyReader and a shared ProcFs reader, and the row still said
two. Nothing noticed, because nothing looked -- the grant sweep checks EDGES, spec-derivations checks
FACET SHAPE, and neither reads the artefact names.

TWO DIRECTIONS, AND THE SECOND IS THE ONE THAT CAUGHT IT.
  * FORWARD  the register names a type that is not in the tree -> FAIL. Unambiguous: the document is
             asserting something false, and a reader has no way to tell.
  * BACKWARD a header declares a type the register does not name -> RATCHETED, not failed. A register
             cell is a summary and not an inventory, so demanding every class would make the check
             noise. But the COUNT may not grow: that is exactly how the row went stale, by the tree
             gaining a reader while the prose stood still.

The backward ceiling is per component and seeded from the state at the time each was last reviewed.
Lower it when you lower it.
"""
import re
import sys
import pathlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import importlib.util
_spec = importlib.util.spec_from_file_location("spec_model",
        pathlib.Path(__file__).resolve().parent / "spec-model.py")
MODEL = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(MODEL)

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "source"

# Components whose directory name IS the component name and whose artefacts are worth checking.
# Deliberately NOT every component: the register summarises large ones and enumerates small ones, and
# a check that cannot tell the two apart would report the difference as a defect.
CHECKED = {"metrics": 0}

DECL = re.compile(r"^\s*(?:class|struct)\s+([A-Z]\w+)\s*[:{]", re.M)
NAMED = re.compile(r"`([A-Z]\w+)`")


def artefacts(component):
    """(named in the register, declared in the tree) for one component."""
    text = MODEL.SPEC.read_text(encoding="utf-8")
    cell = MODEL.components(text).get(component, {}).get("contents", "")
    named = set(NAMED.findall(cell))
    declared = set()
    d = SRC / component
    if d.is_dir():
        for p in sorted(d.rglob("*.hpp")):
            declared |= set(DECL.findall(p.read_text(encoding="utf-8", errors="replace")))
    return named, declared


def check():
    fails, lines = 0, []
    for component, ceiling in sorted(CHECKED.items()):
        named, declared = artefacts(component)
        if not declared:
            print("register-artefacts: FAIL -- `%s` declares no types in the tree at all. The pattern "
                  "has drifted or the directory moved; either way this is measuring nothing." % component)
            return 1
        absent = sorted(named - declared)
        unnamed = sorted(declared - named)
        for a in absent:
            lines.append("  %s: register names `%s`, which no header in source/%s/ declares"
                         % (component, a, component))
            fails += 1
        if len(unnamed) > ceiling:
            lines.append("  %s: %d type(s) declared and not named in the register (ceiling %d): %s"
                         % (component, len(unnamed), ceiling, ", ".join(unnamed)))
            fails += 1
        else:
            lines.append("  %s: %d named, %d declared, %d unnamed (ceiling %d)"
                         % (component, len(named), len(declared), len(unnamed), ceiling))
    for l in lines:
        print(l)
    if fails:
        print("register-artefacts: FAIL -- the component register does not describe the tree")
        return 1
    print("register-artefacts: OK -- %d component(s) checked, every named type exists and none is "
          "unaccounted for" % len(CHECKED))
    return 0


def selftest():
    fails = []

    def arm(name, ok):
        print("  %s %s" % ("ok  " if ok else "FAIL", name))
        if not ok:
            fails.append(name)

    # Two characters minimum, deliberately: a one-letter backtick is far more likely to be prose than
    # a type. The first draft of this arm asserted `X` would match and the arm failed -- the TEST was
    # wrong, which is the cheap direction for a disagreement between a check and its expectation.
    arm("a CamelCase backtick is read as a named artefact",
        NAMED.findall("`MetricSample` and `ProcFs`") == ["MetricSample", "ProcFs"])
    arm("a lowercase backtick is NOT -- `metrics` is the component, not a type",
        NAMED.findall("the `metrics` CLI") == [])
    arm("a class declaration is found", DECL.findall("class ThreadBusyReader {") == ["ThreadBusyReader"])
    arm("a struct declaration is found too", DECL.findall("struct BusyReading {") == ["BusyReading"])
    arm("a forward declaration is NOT a declaration", DECL.findall("class Foo;") == [])

    named, declared = artefacts("metrics")
    arm("the real register names at least one real type", bool(named & declared))
    arm("the real tree declares more than one type", len(declared) > 1)

    print()
    if fails:
        print("register-artefacts selftest: FAILED -- %d: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("  register-artefacts selftest: 7/7 arms ok -- reads named types, ignores component names "
          "and forward declarations, and both directions see the real tree")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv[1:] else check())
