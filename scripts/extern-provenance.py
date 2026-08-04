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
  * the generated doc must match what --inject would write, so a register edit that skipped --inject
    fails here instead of leaving a stale table behind a green gate
  * `attributed` must name a file that EXISTS and that actually mentions the artefact's upstream --
    declaring an attribution file is not the same as being attributed in it

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


def generated_body(reg):
    return "\n".join([
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


def doc_problems(reg, doc_text):
    """The doc is DERIVED, so it needs its own freshness arm -- check() only compares register to TREE.

    Without this the gate had a hole its own doc denied: editing the register and forgetting --inject
    left the .md silently stale while run-gates stayed green, contradicting the doc's own line 4. The
    sibling gates (render_docs_fresh, boundary_fresh, arch_graph_fresh) all diff their generated doc;
    this one did not, which is the same class of blindness `gate-vocabulary-outlives-document` names.
    """
    rel = os.path.relpath(DOC, ROOT)
    if doc_text is None:
        return ["%s does not exist -- run: python3 scripts/extern-provenance.py --inject" % rel]
    if BEGIN not in doc_text or END not in doc_text:
        return ["%s has no generated block" % rel]
    got = doc_text[doc_text.index(BEGIN):doc_text.index(END) + len(END)]
    if got != generated_body(reg):
        return ["%s is stale -- re-run: python3 scripts/extern-provenance.py --inject" % rel]
    return []


def attribution_problems(reg, texts):
    """`attributed` names a file; this checks that the file actually ATTRIBUTES the artefact.

    The field existed from the day E10 landed, was rendered in the generated table, and was checked by
    NOTHING -- so five of seven artefacts sat unattributed behind a green gate, and a new one could
    land the same way. MIT and Apache-2.0 both require the notice to travel with redistribution and
    origin is a public fork, so this is a redistribution obligation rather than tidiness.

    It matches on the UPSTREAM URL rather than the artefact's register name: the name is ours and can
    drift, the URL is the thing a reader follows. Declaring a file is not attribution -- being named
    in it is.
    """
    problems = []
    for name, e in sorted(reg.items()):
        att = e.get("attributed")
        if not att:
            problems.append("%s names no attribution file -- add an entry and set `attributed`" % name)
            continue
        text = texts.get(att)
        if text is None:
            problems.append("%s is attributed to %s, which does not exist" % (name, att))
            continue
        needle = re.sub(r"^https?://", "", e.get("upstream") or "").rstrip("/")
        if not needle:
            problems.append("%s has no upstream to match an attribution against" % name)
        elif needle not in text:
            problems.append("%s claims attribution in %s, which never mentions %s" % (name, att, needle))
    return problems


def attribution_texts(reg, root=ROOT):
    """Read every file the register names, once each."""
    texts = {}
    for e in reg.values():
        att = e.get("attributed")
        if att and att not in texts:
            path = os.path.join(root, att)
            if os.path.exists(path):
                texts[att] = open(path, errors="replace").read()
    return texts


def inject():
    reg = load_register()
    body = generated_body(reg)
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
    # Both directions of the reach cross-check. The 'unreachable claimed for a compiled artefact' arm used
    # to relabel the one dead entry we carried; deleting it (#220) removed that fixture, which the gate on
    # this selftest caught rather than letting the arm rot into a silent skip.
    run("a compiled artefact relabelled as dead", True,
        lambda r: r["lua"].__setitem__("reach", "unreachable"))
    # A DELIBERATE MUST-NOT-FIRE, recording a real limit rather than implying coverage: `header` and
    # `direct` both mean "named in a CMake list", so nothing here can tell them apart. Confusing those two
    # mislabels a column; confusing either with `unreachable` gets live code deleted, and that IS caught.
    run("header vs direct -- NOT distinguishable, and not claimed to be", False,
        lambda r: r["fast_float"].__setitem__("reach", "direct"))

    # The doc-freshness arm, exercised on synthetic text so it needs no filesystem and cannot be
    # fooled by the committed doc happening to be in sync. This hole was live from the day E10
    # landed and was found by an audit, not by the gate.
    def run_doc(desc, expect_problem, doc_text):
        nonlocal fails
        probs = doc_problems(base, doc_text)
        got = bool(probs)
        if got != expect_problem:
            print("  SELFTEST FAIL: %s -- expected problem=%s, got %s" % (desc, expect_problem, probs[:1]))
            fails += 1
        else:
            print("  ok   (%s)  %s" % ("caught" if got else "clean", desc))

    fresh = generated_body(base)
    run_doc("a doc regenerated from the register", False, "# head\n\n" + fresh + "\ntail\n")
    run_doc("a register edit with no --inject (the hole this arm closes)", True,
            "# head\n\n" + fresh.replace("| `lua` |", "| `lua-EDITED` |") + "\ntail\n")
    run_doc("the generated block deleted from the doc", True, "# head\n\nprose only\n")
    run_doc("the doc missing entirely", True, None)

    # Attribution. All seven artefacts are attributed now, so these arms are the only thing standing
    # between that and a silent regression -- the field was live and unchecked while five sat empty.
    def run_att(desc, expect_problem, mutate, texts):
        nonlocal fails
        reg = copy.deepcopy(base)
        mutate(reg)
        probs = attribution_problems(reg, texts)
        got = bool(probs)
        if got != expect_problem:
            print("  SELFTEST FAIL: %s -- expected problem=%s, got %s" % (desc, expect_problem, probs[:1]))
            fails += 1
        else:
            print("  ok   (%s)  %s" % ("caught" if got else "clean", desc))

    real = attribution_texts(base)
    run_att("every artefact attributed, and named in the file", False, lambda r: None, real)
    run_att("an artefact loses its attribution", True,
            lambda r: r["fmt"].__setitem__("attributed", None), real)
    run_att("attributed names a file that does not exist", True,
            lambda r: r["fmt"].__setitem__("attributed", "doc/NOT-A-FILE.md"), real)
    # THE ARM THAT MATTERS: declaring a file is not being named in it.
    run_att("the attribution file never mentions the artefact", True, lambda r: None,
            {k: v.replace("github.com/fmtlib/fmt", "github.com/somebody/else") for k, v in real.items()})
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
    reg = load_register()
    problems = check()
    problems += doc_problems(reg, open(DOC).read() if os.path.exists(DOC) else None)
    problems += attribution_problems(reg, attribution_texts(reg))
    if problems:
        print("extern provenance is out of date with the tree (%d):" % len(problems))
        for p in problems:
            print("  - " + p)
        print("Fix docs/architecture/extern-provenance.json, then: "
              "python3 scripts/extern-provenance.py --inject")
        return 1
    print("extern provenance OK -- %d artefacts: all declared, all reachable as declared, "
          "all attributed in a file that names them, doc in sync with the register" % len(reg))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
