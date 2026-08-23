#!/usr/bin/env python3
"""kind_buildable -- the register's KIND column against what CMake actually builds.

WHY THIS EXISTS. `metrics` was declared KIND `LIBRARY` in the register while
source/metrics/CMakeLists.txt contained ADD_EXECUTABLE(metrics ... metrics_main.cpp). The register
defines ENTRYPOINT as "an executable" -- so the declaration and the build system said different
things about the same component, and NOTHING COMPARED THEM.

That is the shape worth fixing, not the one cell. Sixteen gates check the register against itself:
grants against grants, closures against the graph, prose against the tables. The register's
correspondence to the BUILD -- the one place the architecture is actually realised -- was checked
nowhere. A KIND is a claim about how a component is compiled and linked; CMake is the only thing that
can falsify it, and CMake was not being read.

WHAT IT CHECKS, in one direction only and deliberately.

  ADD_EXECUTABLE in a component's CMakeLists  =>  its KIND must be ENTRYPOINT.

The converse is NOT checked, and that asymmetry is the point. A component may legitimately be
declared ENTRYPOINT before its executable exists -- the register describes the TARGET state, and
D2 says the scope is the whole target state while sequencing is what defers. Requiring an executable
today for every ENTRYPOINT would convict the register for being ahead of the tree, which is its job.
Requiring the reverse convicts the register for being BEHIND the tree, which is drift.

ON OBJECT LIBRARIES. A component may hold both ADD_LIBRARY(... OBJECT ...) and ADD_EXECUTABLE --
`metrics` does. That is not a contradiction: the object library is how the executable's own
translation units are compiled, not a second identity. The executable is what decides the KIND,
because the executable is what the ENTRYPOINT definition names.

ON COMPONENTS WITH NO REGISTER ROW. Several directories that build executables -- json_tool,
mod_uploader, utility, test -- have no row at all. They are REPORTED, never failed: an absent row is
a coverage question for the register's authors, not a mismatch between two claims. Failing on them
would make this gate an argument about scope every time it ran.
"""

import importlib.util
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
SPEC = REPO / "docs" / "superpowers" / "specs" / "2026-08-01-target-state-system-architecture.md"

ADD_EXECUTABLE = re.compile(r"^\s*ADD_EXECUTABLE\s*\(\s*([A-Za-z0-9_]+)", re.I | re.M)


def _model():
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def declared_kinds():
    """-> {component: KIND} from the register, via the parser that already owns that table.

    Imported rather than re-parsed: a second reader of the same table is a second writer whose drift
    no gate can see, which is the defect [#252] step 2 spent a commit removing from the owner
    spellings. One parser, many callers.
    """
    m = _model()
    return {name: row["kind"] for name, row in m.components(SPEC.read_text()).items()}


def executables():
    """-> {component_dir: [target names]} for every source/*/CMakeLists.txt that builds one."""
    out = {}
    for cml in sorted((REPO / "source").glob("*/CMakeLists.txt")):
        found = ADD_EXECUTABLE.findall(cml.read_text(errors="ignore"))
        if found:
            out[cml.parent.name] = found
    return out


def check(kinds=None, exes=None):
    kinds = declared_kinds() if kinds is None else kinds
    exes = executables() if exes is None else exes
    if not exes:
        print("kind_buildable: FAIL -- no ADD_EXECUTABLE matched anywhere under source/. The pattern "
              "has drifted from the tree, so this gate is measuring nothing.")
        return 1

    bad, unrowed = [], []
    for comp, targets in sorted(exes.items()):
        if comp not in kinds:
            unrowed.append((comp, targets))
            continue
        if kinds[comp] != "ENTRYPOINT":
            bad.append((comp, kinds[comp], targets))

    for comp, targets in unrowed:
        print(f"  note  {comp}: builds {', '.join(targets)} but has no register row -- not checked")

    if bad:
        print(f"kind_buildable: FAIL -- {len(bad)} component(s) build an executable but are not "
              f"declared ENTRYPOINT:")
        for comp, kind, targets in bad:
            print(f"  {comp}: register says {kind}, CMake has ADD_EXECUTABLE({', '.join(targets)})")
        print("\nThe register defines ENTRYPOINT as \"an executable\". Either the KIND is wrong and the")
        print("register should say ENTRYPOINT, or the executable should not be there. Both are edits")
        print("someone must make deliberately; neither is a thing to leave disagreeing.")
        return 1

    checked = len(exes) - len(unrowed)
    print(f"kind_buildable: {checked} component(s) with an executable all declare ENTRYPOINT"
          f"{f'; {len(unrowed)} unrowed and reported above' if unrowed else ''}")
    return 0


def selftest():
    """The gate must fail on a mismatch and stay quiet on the shipped tree."""
    arms = []

    def arm(label, ok):
        arms.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    real_kinds, real_exes = declared_kinds(), executables()

    arm("the shipped tree passes", check(real_kinds, real_exes) == 0)

    # A LIBRARY that builds an executable is the exact defect this was written for.
    injected = dict(real_kinds)
    victim = next(c for c in real_exes if c in real_kinds)
    injected[victim] = "LIBRARY"
    arm(f"a component declared LIBRARY that builds an executable is REFUSED ({victim})",
        check(injected, real_exes) == 1)

    # AND THE PATTERN MUST STILL MATCH SOMETHING. A regex that silently stops matching turns this
    # gate green for the wrong reason -- the [#223] failure, where a screaming oracle read as a pass.
    arm("an empty executable set is REFUSED, not treated as clean", check(real_kinds, {}) == 1)

    # A component with no register row is REPORTED, never failed -- an absent row is a coverage
    # question, not a disagreement between two claims.
    arm("an unrowed component does not fail the gate",
        check({}, {"nosuchcomp": ["nosuchcomp"]}) == 0)

    print(f"kind_buildable selftest: {sum(arms)}/{len(arms)} arms ok")
    return 0 if all(arms) else 1


if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] == "--selftest":
            sys.exit(selftest())
        print(f"unknown argument {sys.argv[1]!r}; expected --selftest or nothing", file=sys.stderr)
        sys.exit(2)
    sys.exit(check())
