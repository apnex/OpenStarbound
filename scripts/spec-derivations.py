#!/usr/bin/env python3
"""Measure whether every component is actually DERIVED, facet by facet.

WHY THIS EXISTS. On 2026-08-02 the register was measured against Section 10 and the answer was
9 derivations for 41 components. Thirty-two components had a one-line duty, a one-line warrant, and
nothing else -- and NOTHING COULD SEE IT. `spec_consistency` checks that a warrant exists and cites
something; a warranted row looks complete. That is the defect class the purpose survey named as
primary: an agentic designer reading a register row has no way to know a derivation was owed and
never written, so it infers one, plausibly, and the fiction propagates.

WHAT A DERIVATION IS. Not a length -- a set of questions answered. Section 10 was reported at 1691
lines and turned out to be 711 once 980 lines of misfiled generated diagrams were moved out; that
mistake is exactly what comes of measuring depth by size. The facets below are the questions. A
derivation is deep when it answers them, and it is as long as answering them takes.

  FIVE live in the register (Section 9), one writer per fact, and are NOT repeated here:
      kind · zone · duty · warrant · contents

  SIX live in the derivation, and this instrument counts them:
      boundary     why the boundary is HERE -- not one step out, not one step in. THE derivation;
                   everything else is description
      rejected     the alternative placement, named. Without it a boundary reads as inevitable
                   when it was in fact chosen
      excludes     what it may not name, and why each exclusion holds
      falsified    how you would know the boundary was wrong. A component with no falsifier is
                   decoration -- P7 applied one level down
      history      evidence that bears on it, or an explicit "none". Warrant, never anchor: history
                   may justify a boundary, it may never place one
      owes         what is still unresolved, or an explicit "nothing". Zero at ratification

USAGE
    scripts/spec-derivations.py            # report the ledger
    scripts/spec-derivations.py --inject   # write the generated ledger into the spec
    scripts/spec-derivations.py --check    # fail if the injected ledger is stale
"""
import argparse
import importlib.util
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def _spec_model():
    """The one reader of the document -- and the one declaration of its path."""
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()
SPEC = MODEL.SPEC

# The six facets, in the order they must appear. Order is fixed so a reader who has read one
# derivation can skim the next without re-learning its shape -- and so a missing facet is a
# visible hole rather than a different arrangement.
FACETS = ["boundary", "rejected", "excludes", "falsified", "history", "owes"]

BEGIN = "<!-- BEGIN GENERATED: scripts/spec-derivations.py#ledger -->"
END = "<!-- END GENERATED: derivation-ledger -->"

# A facet row: | **boundary** | ... |   The label may carry trailing words ("falsified by").
_ROW = re.compile(r'^\|\s*\*\*(\w+)[^|*]*\*\*\s*\|(.*)\|\s*$')
_H3 = re.compile(r'^### (.*)$')
_H2 = re.compile(r'^## (\d+)\. ')

# A facet is UNANSWERED if it is absent, empty, or one of these placeholders.
_EMPTY = {"", "—", "-", "tbd", "todo", "owed", "?"}


def derivation_region(lines):
    """Line range of Section 10, where component derivations live."""
    start = end = None
    for i, l in enumerate(lines, 1):
        m = _H2.match(l)
        if not m:
            continue
        if m.group(1) == "10":
            start = i
        elif start is not None and end is None:
            end = i - 1
    if start is None:
        raise SystemExit("spec-derivations: Section 10 not found -- the parse is not believable")
    return start, (end or len(lines))


def parse(text, components):
    """-> {component: {facet: bool}} for every component in the register."""
    lines = text.splitlines()
    lo, hi = derivation_region(lines)

    found = {}
    cur = None
    for i in range(lo, hi + 1):
        line = lines[i - 1]
        m = _H3.match(line)
        if m:
            named = [n for n in re.findall(r'`(\w+)`', m.group(1)) if n in components]
            # A heading naming exactly one component is that component's derivation. A heading
            # naming several is a cross-cutting discussion, not a derivation, and is skipped --
            # otherwise one paragraph mentioning four components would "derive" all four.
            cur = named[0] if len(named) == 1 else None
            continue
        if cur is None:
            continue
        r = _ROW.match(line)
        if r:
            label = r.group(1).strip().lower()
            if label in FACETS:
                value = r.group(2).strip().strip("*").strip()
                found.setdefault(cur, {})[label] = value.lower() not in _EMPTY

    return {c: {f: found.get(c, {}).get(f, False) for f in FACETS} for c in components}


def ledger(cov, components):
    """The generated block: one row per component, one column per facet."""
    order = sorted(components, key=lambda c: (components[c]["kind"], c))
    out = [BEGIN, ""]
    total = len(components) * len(FACETS)
    answered = sum(1 for c in cov for f in FACETS if cov[c][f])
    complete = sum(1 for c in cov if all(cov[c].values()))
    out.append("**%d of %d components fully derived · %d of %d facets answered.** A component is "
               "derived when all six are answered; the five register facets (kind, zone, duty, "
               "warrant, contents) are counted in Section 9 and deliberately not repeated here."
               % (complete, len(components), answered, total))
    out.append("")
    out.append("| component | kind | " + " | ".join(FACETS) + " |")
    out.append("|---|---|" + "---|" * len(FACETS))
    for c in order:
        cells = " | ".join("yes" if cov[c][f] else "**owed**" for f in FACETS)
        out.append("| `%s` | %s | %s |" % (c, components[c]["kind"], cells))
    out.append("")
    out.append(END)
    return "\n".join(out)


def inject(text, block):
    if BEGIN in text and END in text:
        pre = text.split(BEGIN, 1)[0]
        post = text.split(END, 1)[1]
        return pre + block + post
    raise SystemExit("spec-derivations: no ledger markers in %s -- add %s / %s where the ledger "
                     "belongs" % (SPEC.name, BEGIN, END))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inject", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)

    text = SPEC.read_text(encoding="utf-8")
    components = MODEL.components(text)
    if len(components) < 30:
        raise SystemExit("spec-derivations: only %d components parsed -- the parse has regressed"
                         % len(components))
    cov = parse(text, components)
    block = ledger(cov, components)

    complete = sum(1 for c in cov if all(cov[c].values()))
    answered = sum(1 for c in cov for f in FACETS if cov[c][f])
    total = len(components) * len(FACETS)

    if args.inject:
        SPEC.write_text(inject(text, block), encoding="utf-8")
        print("spec-derivations: written -- %d/%d components derived, %d/%d facets"
              % (complete, len(components), answered, total))
        return 0

    if args.check:
        if BEGIN not in text or END not in text:
            print("spec-derivations: FAIL -- ledger markers absent")
            return 1
        current = BEGIN + text.split(BEGIN, 1)[1].split(END, 1)[0] + END
        if current.strip() != block.strip():
            print("spec-derivations: STALE -- rerun `scripts/spec-derivations.py --inject`")
            return 1
        print("spec-derivations: OK -- ledger matches (%d/%d components derived, %d/%d facets)"
              % (complete, len(components), answered, total))
        return 0

    print("spec-derivations: %d of %d components fully derived, %d of %d facets answered"
          % (complete, len(components), answered, total))
    for c in sorted(cov):
        missing = [f for f in FACETS if not cov[c][f]]
        if missing:
            print("  %-18s owes %s" % (c, ", ".join(missing)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
