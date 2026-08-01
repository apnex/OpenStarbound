#!/usr/bin/env python3
"""Generate the WHAT DRIVES WHAT table from the runtime projection's edges.

WHY THIS EXISTS. The element register answers what each element IS -- kind, cadence, cardinality,
owner, thread, duty. It does not answer the first question anyone actually asks of a runtime model:
**what drives this?** That relationship lived only as edges inside a mermaid diagram, so reading it
meant tracing arrows by eye. The Director asked for it as a table and it had to be assembled by hand,
which is the definition of a missing artifact.

DERIVED, NOT WRITTEN. A hand-maintained drive table would be a ninth declaration of something the
runtime diagram already states, and this document's entire defect history is second declarations
drifting from first ones -- the stale component tally, the orphaned `client` node, the missing
`gpu_sdl` grant row, three parsers with three counts. So this reads the diagram and writes the table.

WHAT IT SHOWS, and the arrows are load-bearing:

    CALL      -->    a direct call inside one component
    DISPATCH  ==>    through a CONTRACT -- the caller names an interface, never the implementation
    HANDOFF   -.->   across a thread or process boundary

An element with NO incoming edge is reported as such rather than omitted. That is not an absence of
information: `audioTick` has no driver we own because the device pulls it, and every WIRING is started
by the process itself. Silence would make those look like oversights instead of the design decisions
they are.
"""
import argparse
import importlib.util
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def _spec_model():
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()

BEGIN = "<!-- BEGIN GENERATED: scripts/drive-table.py -->"
END = "<!-- END GENERATED: drive-table -->"

EDGE = re.compile(r'^\s*(\w+)\s*(-->|==>|-\.->)\s*(?:\|([^|\n]*)\|)?\s*(\w+)\s*$', re.M)
NODE = re.compile(r'^\s*(\w+)\["<b>(\w+)</b> · (LOOP|TICK|WIRING|SIGNAL)<br/>', re.M)
ARROW = {"-->": "CALL", "==>": "DISPATCH", "-.->": "HANDOFF"}
ORDER = {"LOOP": 0, "TICK": 1, "SIGNAL": 2, "WIRING": 3}

# Elements with no driver, and the reason each is correct. Absence is a design statement here, so it
# is declared rather than inferred; an element that falls off this list and has no edge is a defect.
#
# WIRINGs are covered by a RULE rather than eight entries: composition is where the process begins,
# so by definition nothing precedes it. Enumerating them would be eight declarations of one fact --
# which is the defect that produced spec-model.py an hour ago.
NO_DRIVER = {
    "frameLoop": "the process starts it — a driver is the root of its own cadence",
    "headlessLoop": "same, for a process with no display",
    "superviseLoop": "same; `main` enters it and waits",
    "audioTick": "**the device pulls it.** EXTERNAL means the clock is not ours to see",
}
WIRING_RULE = "the process entry point runs it — composition is where a process begins"


def build(text):
    elem = MODEL.elements(text)
    rt = text.split("%% projection: runtime", 1)
    if len(rt) != 2:
        raise SystemExit("drive-table: no runtime projection in the spec")
    rt = rt[1].split("```", 1)[0]
    ids = {i: n for i, n, _k in NODE.findall(rt)}

    incoming, outgoing = {}, {}
    for a, arrow, label, b in EDGE.findall(rt):
        if a not in ids or b not in ids:
            continue
        seq = re.match(r'\s*(\d+|\*)\s*:\s*(.*)', label or "")
        note = (seq.group(2) if seq else (label or "")).strip()
        incoming.setdefault(ids[b], set()).add((ids[a], ARROW[arrow], note))
        outgoing.setdefault(ids[a], set()).add(ids[b])

    # A SIGNAL WITH NO CONSUMER IS NOT A SIGNAL. This gate asked "what drives this?" of every element
    # and never asked "who receives it?", so `resizeSignal` -- duty "the window changed; surfaces must
    # be rebuilt" -- sat with an incoming edge from `inputTick`, NO outgoing edge at all, and a
    # permanently green run. Nothing rebuilt any surface, and no instrument could say so.
    #
    # The asymmetry is deliberate and applies to SIGNALs only. A LOOP or a TICK computes and returns;
    # it is complete whether or not anything reads the result here. A signal exists ENTIRELY to reach
    # someone, so an unreceived one is a hole by definition rather than a design choice -- which is
    # why this verdict takes no declared exceptions, unlike NO_DRIVER above.
    unheard = sorted(n for n, e in elem.items() if e["kind"] == "SIGNAL" and n not in outgoing)

    out = ["| element | kind | cadence | driven by | how | duty |", "|---|---|---|---|---|---|"]
    undriven = []
    for name, e in sorted(elem.items(), key=lambda kv: (ORDER[kv[1]["kind"]], kv[0])):
        got = sorted(incoming.get(name, ()))
        if got:
            by = " · ".join(sorted({"`%s`" % s for s, _h, _n in got}))
            how = " · ".join(sorted({h for _s, h, _n in got}))
        else:
            reason = WIRING_RULE if e["kind"] == "WIRING" else NO_DRIVER.get(name)
            if reason is None:
                undriven.append(name)
                reason = "UNDECLARED"
            by, how = "**nothing**", "*%s*" % reason
        out.append("| **`%s`** | %s | %s | %s | %s | %s |"
                   % (name, e["kind"], e["cadence"], by, how, e["duty"]))
    return "\n".join(out), undriven, unheard


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inject", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)

    path = MODEL.SPEC
    text = path.read_text(encoding="utf-8")
    table, undriven, unheard = build(text)

    if unheard:
        print("drive-table: FAIL -- %d SIGNAL(s) reach nobody: %s"
              % (len(unheard), ", ".join(unheard)))
        print("  A signal exists to be received. Draw the edge to its consumer in the runtime")
        print("  projection, or it is not a signal -- it is a value the design forgot to deliver.")
        return 1

    if undriven:
        print("drive-table: FAIL -- %d element(s) have no incoming edge and no declared reason: %s"
              % (len(undriven), ", ".join(undriven)))
        print("  Either draw what drives them in the runtime projection, or add them to NO_DRIVER")
        print("  with the reason. An undriven element is a design statement or a hole; never neither.")
        return 1

    if BEGIN not in text or END not in text:
        print("drive-table: no marker pair (%s ... %s) in %s" % (BEGIN, END, path.name))
        return 1
    head, rest = text.split(BEGIN, 1)
    old, tail = rest.split(END, 1)
    want = "\n%s\n" % table

    if args.inject:
        path.write_text(head + BEGIN + want + END + tail, encoding="utf-8")
        print("drive-table: written -- %d elements" % (len(table.splitlines()) - 2))
        return 0
    if old != want:
        print("drive-table: STALE -- rerun `scripts/drive-table.py --inject`")
        return 1 if args.check else 0
    print("drive-table: OK -- %d elements, every one with a driver or a declared reason"
          % (len(table.splitlines()) - 2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
