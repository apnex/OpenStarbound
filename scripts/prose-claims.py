#!/usr/bin/env python3
"""Check the PROSE against the registers. The other gates check the tables; nothing checked the words.

WHY THIS EXISTS. Sixteen gates verify what is written in tables, diagrams and grants. Every one of
them was green while the prose said "three clocks" (there are five), called `participant` `client`
(renamed hours earlier), described `net` as "CONTRACT, SEAM" (that zone no longer exists), and
referenced D8 and D9 which were not in the decisions table at all. Prose is where claims go to rot,
because no generator owns it and no parser reads it.

The 4,057-line document carries 115 numbers outside tables and code. Each was true when written.

WHAT IT CHECKS, and each verdict exists because that class was found by hand at least once:

  STALE_ZONE   an old zone name used as a current claim -- detected by adjacency to a KIND word, so
               "LIBRARY, INTERIOR" fires and a sentence about the history of the naming does not.
               23 occurrences survived the four-zone rename; the gate found them, not a reader.
  UNKNOWN_NAME a backticked lowercase_snake token that is neither a component, an element, nor
               declared below. This is how a renamed component leaves a corpse in the prose.
  DANGLING_D   a decision referenced but never defined. D7/D8/D9 were all in this state.
  COUNT_DRIFT  a prose count of something the model knows, disagreeing with the model.

WHAT IT DELIBERATELY DOES NOT CHECK. Numbers measured from the tree -- "111 game files hold
Drawable", "15 SDL_GL_ references" -- are not re-measured here. They are claims about a moving
codebase, and a gate that re-ran every one of them would be slow, flaky, and would silently change
what the document says. Those belong to the ratchets that own them. **This gate checks the document
against ITSELF, not against the world**, and saying so is the difference between a limit and a hole.

HISTORICAL PASSAGES. A block wrapped in `<!-- HISTORICAL -->` ... `<!-- END HISTORICAL -->` is exempt
from STALE_ZONE, because explaining why a name was replaced requires naming it. Nothing else is exempt.
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

DEAD_ZONES = ("SUBSTRATE", "SEAM", "INTERIOR", "PERIPHERY", "SHELL")
KINDWORD = r'(?:FOUNDATION|CONTRACT|BACKEND|LIBRARY|ENTRYPOINT)'

# Backticked lowercase tokens that are legitimately NOT components. Each carries its reason, so the
# list cannot quietly become a place to bury a stale name -- which is the only way this gate fails.
ALLOWED = {
    # gates and instruments
    "spec_consistency": "gate", "grant_sweep": "gate", "loop_inventory": "gate",
    "composition_graphs": "gate", "dedup_measure": "gate", "link_sweep": "gate",
    "drive_table": "gate", "tree_map": "gate", "host_api_neutral": "gate",
    "boundary_ratchet": "gate", "render_layering": "gate", "layer1_layering": "gate",
    "render_docs_fresh": "gate", "boundary_fresh": "gate", "arch_graph_fresh": "gate",
    "config_declared": "gate", "prose_claims": "gate", "render_surface_tests": "test",
    "core_tests": "test", "game_tests": "test",
    # binaries and today's directories, named as facts about the current tree
    "starbound_server": "binary that exists today", "application": "today's directory, being split",
    "extern": "vendored, deliberately outside the register",
    # dead utilities, named as evidence they are dead
    "planet_mapgen": "commented-out utility", "world_benchmark": "commented-out utility",
    "generation_benchmark": "commented-out utility",
    "dungeon_generation_benchmark": "commented-out utility",
    # code fragments appearing inline
    "void": "C++ keyword in a signature", "for": "C++ keyword in a snippet",
    "m_item": "C++ member named in the tier-2 evidence", "m_mode": "C++ member, same",
    "connect": "a function name in the netcode discussion",
    "star_game": "a CMake OBJECT-library target, named as a fact about the build",
    "hosting": "a REJECTED name, kept to explain why `colocation` was chosen instead",
}


def scan(text):
    comp, grants, elem = MODEL.components(text), MODEL.grants(text), MODEL.elements(text)
    findings = []

    # strip historical blocks for the zone check only
    zone_text = re.sub(r'<!-- HISTORICAL -->.*?<!-- END HISTORICAL -->', "", text, flags=re.S)
    for z in DEAD_ZONES:
        for m in re.finditer(r'.{0,60}\b%s\b.{0,60}' % z, zone_text):
            ctx = m.group(0)
            if re.search(KINDWORD, ctx):
                findings.append(("STALE_ZONE",
                                 "%r used as a zone beside a KIND word: ...%s..."
                                 % (z, ctx.strip().replace("\n", " ")[:96])))

    prose = "\n".join(l for l in text.splitlines()
                      if not l.startswith(("|", " ", "```", "<!--", "%%")))
    for name in sorted(set(re.findall(r'`([a-z][a-z0-9_]{2,})`', prose))):
        if name not in comp and name not in elem and name not in ALLOWED:
            findings.append(("UNKNOWN_NAME",
                             "`%s` is backticked in prose but is not a component, an element, or "
                             "declared in ALLOWED -- a renamed component leaves exactly this corpse"
                             % name))

    defined = set(re.findall(r'\| \*\*(D\d+)\*\* \|', text))
    for d in sorted(set(re.findall(r'\b(D\d+)\b', text))):
        if d not in defined:
            findings.append(("DANGLING_D",
                             "%s is referenced but has no row in the decisions table" % d))

    counts = {"components": len(comp), "elements": len(elem), "grant rows": len(grants)}
    for thing, actual in counts.items():
        for m in re.finditer(r'\b(\d+)\s+%s\b' % re.escape(thing), prose):
            got = int(m.group(1))
            # "9 of 41 components" -- the second number is the total, the first is a subset
            if got != actual and not re.search(r'\bof\s+%d\s+%s' % (actual, re.escape(thing)),
                                               prose[max(0, m.start() - 30):m.end()]):
                findings.append(("COUNT_DRIFT",
                                 "prose says %d %s; the register has %d" % (got, thing, actual)))
    return findings


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)

    text = MODEL.SPEC.read_text(encoding="utf-8")
    findings = scan(text)
    for kind, msg in findings:
        print("  %-13s %s" % (kind, msg))
    if findings:
        print("prose-claims: FAIL -- %d prose claim(s) disagree with the registers" % len(findings))
        return 1 if args.check else 0
    print("prose-claims: OK -- every checkable prose claim matches the registers")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
