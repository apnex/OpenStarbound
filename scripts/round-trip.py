#!/usr/bin/env python3
"""Count the round trips seam 1 declares, and hold them at the ceiling N1.d asserts.

WHY THIS EXISTS. Section 3's clause N1.d says a seam is not chatty, and then says of itself:

    It is also **countable** -- and that is what makes it gateable rather than aspirational,
    because a count can ratchet.

Nothing counted. Section 18 said so in as many words -- *"the round-trip ratchet has no metric and no
starting ceiling ... `round trip` is not yet defined precisely enough to count"* -- so Sections 3 and
18 were two writers disagreeing about whether the document's own rule was enforceable. Section 18 was
the honest one.

THE DEFINITION WAS ALREADY IN THE DOCUMENT, in the seam table it describes and in the sentence beneath
it: *"a sink never answers, and there is exactly one source, polled once per frame."* A round trip is
a seam call THAT RETURNS A VALUE. `poll() -> InputBatch` returns; `accept(SceneDelta const&)` does
not. That is a `->` in the call column, which is arithmetic rather than judgement.

Section 18 worried that *"a `poll()` that returns is one by construction, and the rule is meant to
catch the ones that are not"*. That worry is answered by the SECOND assertion rather than by a
cleverer definition. The ones that are not are the ones that arrive on a sink -- a method that answers
on something named `Sink`. The document already states this as a naming rule:

    A method that returns a value on something called a *Sink* is a naming error before it is a
    design error -- which makes the constraint reviewable by reading, not only by counting.

This makes it checkable as well as reviewable. So there are two assertions, and the second is the one
with teeth:

  1. TOTAL round trips <= CEILING (1). The ratchet. Lower it; never raise it.
  2. NO `Sink` returns. The shape. A design can satisfy (1) and still be wrong by answering on the
     one call that was supposed to be one-way.

SCOPE, STATED SO IT IS NOT MISTAKEN FOR MORE. This counts SEAM 1 -- the presentation seam, the one
whose calls the document declares. Seam 2 (`gpu`) is described by a comparison table with no method
signatures, so it is not counted here and this gate does not speak for it. Saying which seam is
measured is the difference between a metric and a claim.
"""
import argparse
import importlib.util
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("spec_model", REPO / "scripts/spec-model.py")
MODEL = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(MODEL)

# The ratchet. N1.d's own words are "one query per frame in total"; the table declares exactly one.
# This may fall and may never rise -- a raise is a design change and belongs in a decision, not here.
CEILING = 1


def check(methods):
    findings = []
    trips = sorted(n for n, v in methods.items() if v["returns"])
    if len(trips) > CEILING:
        findings.append("ROUND_TRIPS %d > ceiling %d -- %s return a value. N1.d says a seam is not "
                        "chatty and the seam table says one query per frame in total; this is %d"
                        % (len(trips), CEILING, ", ".join("`%s`" % t for t in trips), len(trips)))
    for name in sorted(methods):
        if name.endswith("Sink") and methods[name]["returns"]:
            findings.append("SINK_ANSWERS `%s` is named a Sink and its call returns a value (`%s`). "
                            "A sink never answers -- this is a naming error before it is a design "
                            "error, and it is the round trip a total count cannot see"
                            % (name, methods[name]["call"]))
    return findings


def selftest(text):
    """Both assertions, driven against mutated tables. A check only ever run against a table it has
    been made to agree with proves nothing."""
    bad = 0
    real = MODEL.seam_methods(text)
    if check(real):
        print("round-trip: selftest needs a clean document; --check reports a finding")
        return 1

    over = dict(real)
    over["AudioSink"] = dict(real["AudioSink"], call="play(AudioBatch const&) -> Ack", returns=True)
    verdicts = check(over)
    for want in ("ROUND_TRIPS", "SINK_ANSWERS"):
        if any(v.startswith(want) for v in verdicts):
            print("  %-13s FIRES  a sink that answers, taking the count to 2" % want)
        else:
            bad += 1
            print("  %-13s SILENT -- not reported when a sink was given a return" % want)

    # A second round trip that is NOT a sink: the count must still catch it on its own.
    extra = dict(real)
    extra["ClockSource"] = dict(direction="round trip out", call="now() -> Tick",
                                strength="pluggable source", returns=True)
    if any(v.startswith("ROUND_TRIPS") for v in check(extra)):
        print("  %-13s FIRES  a second non-sink round trip" % "ROUND_TRIPS")
    else:
        bad += 1
        print("  %-13s SILENT -- a second round trip was not counted" % "ROUND_TRIPS")

    if bad:
        print("round-trip: SELFTEST FAIL -- %d assertion(s) did not behave" % bad)
        return 1
    print("round-trip: selftest OK -- both assertions fire, the real table stays clean")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--selftest", action="store_true", help="assert both assertions still fire")
    args = ap.parse_args(argv)
    text = MODEL.SPEC.read_text(encoding="utf-8")
    if args.selftest:
        return selftest(text)

    methods = MODEL.seam_methods(text)
    findings = check(methods)
    for f in findings:
        print("  " + f)
    if findings:
        print("round-trip: FAIL -- seam 1 is chattier than N1.d allows")
        return 1
    trips = [n for n, v in methods.items() if v["returns"]]
    print("round-trip: OK -- %d of %d seam-1 calls return a value (ceiling %d): %s"
          % (len(trips), len(methods), CEILING, ", ".join(trips) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
