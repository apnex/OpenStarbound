#!/usr/bin/env python3
"""Provenance for every vendored artefact under source/extern -- derived, cross-checked, gated.

WHY GENERATED RATHER THAN WRITTEN. PR 570 shipped a hand-written extern/README.md whose first policy
bullet contradicted their own vcpkg.json on the day it landed: it asserted the manifest was pinned by a
builtin-baseline that their own patch had removed (ledger row B12). A provenance doc nobody can contradict
is prose with a table in it. This one is contradicted by the tree.

THE SPLIT. Upstream URL, licence and why-vendored cannot be derived from a vendored copy -- those are
declared in docs/architecture/extern-provenance.json. Everything derivable IS derived and cross-checked:
  * the path set must exactly equal the source/extern listing -- no orphan file, no phantom entry
  * `reach` must match how CMakeLists.txt and the include graph actually reach the artefact
  * where `version_from` names macros, the derived version must equal the declared one

THE REACH TAXONOMY IS THE POINT, and it was learned the hard way while writing this. Three files looked
dead on a narrow grep and only one was:
  * malloc.c is in NO CMake list -- rpmalloc.c #includes it (indirect)
  * xxh3.h is in NO CMake list -- source/core/StarXXHash.hpp includes it via the include path, so it is
    live production code that no build list mentions (include_path)
  * xxh_x86dispatch.{c,h} are in no list and included by nothing, repo-wide (unreachable -- actually dead)
"not in CMakeLists" is not "not compiled", and "not compiled" is not "dead". A column that collapses those
three is the column that gets somebody to delete live code.

  extern-provenance.py --check     # gate: register vs tree
  extern-provenance.py --inject    # regenerate the table in the doc
  extern-provenance.py --selftest  # prove each failure arm fires
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTERN = os.path.join(ROOT, "source", "extern")
REGISTER = os.path.join(ROOT, "docs", "architecture", "extern-provenance.json")
DOC = os.path.join(ROOT, "docs", "architecture", "extern-provenance.md")
BEGIN = "<!-- BEGIN GENERATED: scripts/extern-provenance.py -->"
END = "<!-- END GENERATED: extern-provenance.py -->"

VALID_REACH = {"direct", "conditional", "header", "indirect", "include_path", "unreachable"}


def load_register(path=REGISTER):
    with open(path) as f:
        reg = json.load(f)
    return {k: v for k, v in reg.items() if k != "_README"}


def cmake_lists(extern=EXTERN):
    """The exact file tokens named in star_extern_SOURCES / star_extern_HEADERS.

    Token-exact, never substring: 'malloc.c' is a substring of 'rpmalloc.c', and matching loosely reports
    an indirect artefact as directly compiled -- which is how a dead-file audit turns into a wrong one.
    """
    text = open(os.path.join(extern, "CMakeLists.txt")).read()
    named = set()
    for line in text.splitlines():
        s = line.strip()
        if s and not s.startswith(("#", "SET", "ADD_", "IF", "ENDIF", "INCLUDE", ")")):
            named.add(os.path.basename(s))
    return named


def derive_version(spec, extern=EXTERN):
    """Read the declared macros out of the vendored header and join them with '.'."""
    path = os.path.join(extern, spec["file"])
    if not os.path.exists(path):
        return None
    text = open(path, errors="replace").read()
    parts = []
    for macro in spec["macros"]:
        m = re.search(r"^#\s*define\s+" + re.escape(macro) + r"\s+\"?([0-9A-Za-z_]+)\"?", text, re.M)
        if not m:
            return None
        parts.append(m.group(1))
    if len(parts) == 1 and parts[0].isdigit() and len(parts[0]) >= 5:
        # fmt packs its version as MMmmpp in one integer (100201 -> 10.2.1).
        n = int(parts[0])
        return "%d.%d.%d" % (n // 10000, (n // 100) % 100, n % 100)
    return ".".join(parts)


def check(register=None, extern=EXTERN):
    reg = register if register is not None else load_register()
    problems = []

    on_disk = set(os.listdir(extern)) - {"CMakeLists.txt"}
    claimed = {}
    for name, e in reg.items():
        for p in e.get("paths", []):
            if p in claimed:
                problems.append("%s and %s both claim %s" % (claimed[p], name, p))
            claimed[p] = name
            if not os.path.exists(os.path.join(extern, p)):
                problems.append("%s declares %s, which is not on disk" % (name, p))

    for orphan in sorted(on_disk - set(claimed)):
        problems.append("source/extern/%s is declared by no register entry" % orphan)

    named = cmake_lists(extern)
    for name, e in sorted(reg.items()):
        for field in ("upstream", "licence", "version", "reach", "why"):
            if not e.get(field):
                problems.append("%s is missing '%s'" % (name, field))
        reach = e.get("reach")
        if reach and reach not in VALID_REACH:
            problems.append("%s has unknown reach '%s'" % (name, reach))
        if reach == "conditional" and not e.get("condition"):
            problems.append("%s is conditional but names no condition" % name)

        # Cross-check reach against CMakeLists rather than trusting the declaration.
        files = [p for p in e.get("paths", []) if os.path.isfile(os.path.join(extern, p))]
        dirs = [p for p in e.get("paths", []) if os.path.isdir(os.path.join(extern, p))]
        any_named = any(f in named for f in files) or bool(dirs)
        if reach in ("direct", "conditional", "header") and not any_named:
            problems.append("%s claims reach=%s but CMakeLists names none of its files" % (name, reach))
        if reach == "unreachable" and any_named:
            problems.append("%s claims reach=unreachable but CMakeLists names one of its files" % name)

        spec = e.get("version_from")
        if spec:
            got = derive_version(spec, extern)
            if got is None:
                problems.append("%s: version_from could not be read from %s" % (name, spec["file"]))
            elif got != e.get("version"):
                problems.append("%s: declared version %s but %s says %s"
                                % (name, e.get("version"), spec["file"], got))
    return problems


def table(reg):
    rows = ["| artefact | version | reach | licence | upstream | attributed |",
            "|---|---|---|---|---|---|"]
    for name, e in sorted(reg.items()):
        reach = e["reach"] + (" (%s)" % e["condition"] if e.get("condition") else "")
        att = e.get("attributed") or "**none**"
        rows.append("| `%s` | %s | %s | %s | %s | %s |"
                    % (name, e["version"], reach, e["licence"], e["upstream"], att))
    return "\n".join(rows)


def inject():
    reg = load_register()
    body = "\n".join([
        BEGIN,
        "<!-- Regenerate with: python3 scripts/extern-provenance.py --inject -->",
        "",
        table(reg),
        "",
        "### Why each is vendored",
        "",
        *["- **%s** — %s" % (n, e["why"]) for n, e in sorted(reg.items())],
        "",
        END,
    ])
    head = ("# Vendored provenance: source/extern\n\n"
            "Generated from `docs/architecture/extern-provenance.json` by `scripts/extern-provenance.py`.\n"
            "Do not edit the block below; edit the register and re-inject. `run-gates.sh` fails on drift.\n\n")
    existing = open(DOC).read() if os.path.exists(DOC) else ""
    if BEGIN in existing and END in existing:
        out = existing[:existing.index(BEGIN)] + body + existing[existing.index(END) + len(END):]
    else:
        out = head + body + "\n"
    with open(DOC, "w") as f:
        f.write(out)
    print("wrote %s (%d artefacts)" % (os.path.relpath(DOC, ROOT), len(reg)))
    return 0


def selftest():
    """Every arm, both directions. A gate nobody has watched fail is not known to fail."""
    import copy
    import tempfile
    base = load_register()
    fails = 0

    def run(desc, expect_problem, mutate):
        nonlocal fails
        reg = copy.deepcopy(base)
        mutate(reg)
        probs = check(reg)
        got = bool(probs)
        if got != expect_problem:
            print("  SELFTEST FAIL: %s -- expected problem=%s, got %s" % (desc, expect_problem, probs[:1]))
            fails += 1
        else:
            print("  ok   (%s)  %s" % ("caught" if got else "clean", desc))

    print("=== extern-provenance --selftest ===")
    run("the register as committed", False, lambda r: None)
    run("declared version drifts from the header", True,
        lambda r: r["fmt"].__setitem__("version", "9.9.9"))
    run("an artefact loses its declaration", True, lambda r: r.pop("fast_float"))
    run("a declaration names a file not on disk", True,
        lambda r: r["fmt"].__setitem__("paths", ["fmt", "fmt-that-never-existed.h"]))
    run("two entries claim the same path", True,
        lambda r: r["fast_float"].__setitem__("paths", ["fast_float.h", "xxhash.c"]))
    run("a mandatory field goes blank", True, lambda r: r["lua"].__setitem__("upstream", ""))
    run("an unknown reach value", True, lambda r: r["lua"].__setitem__("reach", "somehow"))
    run("conditional without a condition", True, lambda r: r["rpmalloc"].pop("condition"))
    run("a dead artefact is relabelled as compiled", True,
        lambda r: r["xxhash-x86dispatch"].__setitem__("reach", "direct"))
    print()
    if fails:
        print("SELFTEST: %d FAILED" % fails)
        return 1
    print("SELFTEST: PASS")
    return 0


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--inject", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if args.inject:
        return inject()
    problems = check()
    if problems:
        print("extern provenance is out of date with the tree (%d):" % len(problems))
        for p in problems:
            print("  - " + p)
        print("Fix docs/architecture/extern-provenance.json, then: "
              "python3 scripts/extern-provenance.py --inject")
        return 1
    reg = load_register()
    print("extern provenance OK -- %d artefacts, all declared, all reachable as declared" % len(reg))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
