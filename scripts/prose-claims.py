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
  SCOPE_CLAIM  prose deferring something to a later spec, or declaring it out of scope. D2 says the
               scope IS the whole target state and that what defers is sequencing, so every such
               sentence is either wrong or is the rule stating itself. See below.

THE SCOPE_CLAIM VERDICT, and why it is phrased against the vocabulary rather than the register.
D2 used to read *"SDL_GPU backend, CI harness, bot driver and distributed decomposition are each
follow-on specs that consume the contract"* while `client_sdl_gpu`, `client_agent`, `world_sim`,
`world_gen` and `colocation` sat in the register as first-class components. The document had grown
into a whole-system target state and its own scope statement still described a presentation-seam
ticket. Five review lenses read the contents and none of them read that sentence, because it is a
claim about the document rather than a claim inside it.

A gate keyed on component NAMES would not have caught it: D2 named no component. It said "bot driver"
and "SDL_GPU backend" in English. So the trigger is the ASSERTION -- any sentence deferring work to a
later spec -- and every occurrence must be listed in SCOPE_EXEMPT with a reason. The list is short by
construction, because under D2 there are only a few legitimate places to discuss scope at all.

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
# A dead zone name is also a live claim when the word "zone" is what sits beside it. The sentence
# "the dedicated server ... has no SEAM zone at all" survived every run of this gate, because the
# KIND-word adjacency test was the only trigger and that sentence names no KIND.
TRIGGER = r'(?:%s|\bzones?\b)' % KINDWORD[3:-1]

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

# Sentences that defer architecture to a later document, or declare something not covered. Under D2
# the scope IS the whole target state, so each of these is a defect unless it appears below.
SCOPE_PATTERNS = (
    r'follow[- ]on spec', r'follow[- ]on\b', r'separate spec', r'later spec', r'future spec',
    r'another spec', r'its own spec', r'out of scope', r'outside the scope', r'outside this spec',
    r'not in scope', r'beyond this spec', r'does not cover', r'does not include',
    r'deferred to a spec', r'left to a later',
)

# A finding is suppressed when one of these literal phrases appears in its context window. The phrase
# is the exemption's key, so an exemption cannot silently widen to cover a sentence it was not written
# for -- rewording the sentence re-arms the gate. Each carries the reason it is legitimate.
SCOPE_EXEMPT = (
    ("What is deferred is sequencing, not scope",
     "D2 itself -- the rule this verdict enforces has to be allowed to state itself"),
    ("there are no follow-on *specs* for anything architectural",
     "D2's own negation; naming the excluded thing is how the rule is written"),
    ("nothing architectural is out of scope, so this is a **schedule, not a boundary**",
     "Section 8's deferral item, which exists to keep sequencing from becoming scope again"),
)


# Counts get spelled out in prose as often as they are written in digits: the sentence "the design now
# has forty components" sat beside a 41-row register through every green run of this gate, because the
# COUNT_DRIFT regex only ever looked for `\d+`. A number word is a number. Enabling that immediately
# surfaced two more stale totals -- "nine of thirty-four components", "Twenty-three of the thirty-four".
#
# WHY THE SPELLED FORM IS DECLARED RATHER THAN JUDGED. Most spelled counts are LOCAL -- "`game` is now
# three components", "two of the four components adopted today" -- and no regex distinguishes a local
# denominator from a register total. Attempts to tell them apart by preceding verb or by the `N of M`
# shape both misfire on real sentences in this document. So every spelled count beside a register noun
# must appear in LOCAL_COUNT with the reason it is not the register total. The list is reviewed once
# and any NEW spelled count fires until someone says which kind it is. A gate that cannot judge should
# demand a declaration, not guess and then get suppressed into vacuity.
_UNITS = ("zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
          "fifteen sixteen seventeen eighteen nineteen").split()
_TENS = {"twenty": 20, "thirty": 30, "forty": 40, "fifty": 50,
         "sixty": 60, "seventy": 70, "eighty": 80, "ninety": 90}
_WORDNUM = {w: i for i, w in enumerate(_UNITS)}
for _t, _tv in _TENS.items():
    _WORDNUM[_t] = _tv
    for _u, _uv in list(zip(_UNITS[1:10], range(1, 10))):
        _WORDNUM["%s-%s" % (_t, _u)] = _tv + _uv
# Longest-first so "forty-one" wins over "forty".
_WORDNUM_RE = "|".join(sorted((re.escape(w) for w in _WORDNUM), key=len, reverse=True))


def _as_int(token):
    return _WORDNUM[token.lower()] if token.lower() in _WORDNUM else int(token)


# Spelled counts beside a register noun that are LOCAL, not the register total. Keyed on the literal
# phrase, so rewording re-arms the gate.
LOCAL_COUNT = (
    ("`client_opengl` names five components", "a closure size, not the register"),
    ("`game` is now three components", "the decomposition of one component"),
    ("included by **197 files across four components**", "the four that include StarRoot.hpp"),
    ("two of the four components adopted today", "a subset of one day's adoptions"),
    ("A shared input consumed by two components", "the two consumers of that input"),
    ("it stands at **seven components today**", "the assertion-only subset of Section 4"),
    ("the client's three elements", "the three time-domain elements, not the register"),
    ("`frameLoop` reaching four elements", "that loop's out-degree"),
    # Both surfaced only once the scan became case-insensitive: a sentence-initial count is
    # capitalised, and every one of them had been invisible.
    ("Four components for four files", "the audio carve-out, not the register"),
    ("Three components have now been carved out", "the running tally of that section"),
)


def _scope_claims(text):
    """Prose asserting something belongs to a later document. Historical blocks are exempt."""
    body = re.sub(r'<!-- HISTORICAL -->.*?<!-- END HISTORICAL -->', "", text, flags=re.S)
    flat = body.replace("**", "").replace("*", "").replace("~~", "")
    out = []
    for pat in SCOPE_PATTERNS:
        for m in re.finditer(pat, flat, re.I):
            ctx = flat[max(0, m.start() - 150):m.end() + 150]
            if any(ex.replace("**", "").replace("*", "") in ctx for ex, _why in SCOPE_EXEMPT):
                continue
            out.append(("SCOPE_CLAIM",
                        "%r defers work to another document; D2 scopes this spec to the WHOLE target "
                        "state, so this is stale or needs a SCOPE_EXEMPT entry: ...%s..."
                        % (m.group(0), " ".join(ctx[110:230].split()))))
    return out


def scan(text):
    comp, grants, elem = MODEL.components(text), MODEL.grants(text), MODEL.elements(text)
    findings = []

    # Historical blocks are exempt from every verdict that is a claim about NOW: a dead zone name and
    # a superseded count are the same kind of fact, and explaining either requires stating it.
    live = re.sub(r'<!-- HISTORICAL -->.*?<!-- END HISTORICAL -->', "", text, flags=re.S)
    for z in DEAD_ZONES:
        for m in re.finditer(r'.{0,60}\b%s\b.{0,60}' % z, live):
            ctx = m.group(0)
            if re.search(TRIGGER, ctx):
                findings.append(("STALE_ZONE",
                                 "%r used as a zone beside a KIND or ZONE word: ...%s..."
                                 % (z, ctx.strip().replace("\n", " ")[:96])))

    prose = "\n".join(l for l in live.splitlines()
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
        # re.I is load-bearing: "Forty-one components" is capitalised at the start of its sentence, so
        # a case-sensitive scan skipped it and then matched the bare "one" after the hyphen, reporting
        # a register of 41 as a prose claim of 1. Longest-first alternation only helps once the
        # longest alternative can match at all.
        for m in re.finditer(r'\b(%s|\d+)\s+%s\b' % (_WORDNUM_RE, re.escape(thing)), prose, re.I):
            got = _as_int(m.group(1))
            if got == actual:
                continue
            # "9 of 41 components" -- the second number is the total, the first is a subset
            if re.search(r'\bof\s+%d\s+%s' % (actual, re.escape(thing)),
                         prose[max(0, m.start() - 30):m.end()]):
                continue
            # Whitespace-normalised, because prose is hard-wrapped and a declared phrase would
            # otherwise stop matching the moment a line break landed inside it.
            ctx = " ".join(prose[max(0, m.start() - 110):m.end() + 70].split())
            if any(" ".join(ph.split()) in ctx for ph, _why in LOCAL_COUNT):
                continue
            findings.append(("COUNT_DRIFT",
                             "prose says %r %s; the register has %d -- if this is a local count and "
                             "not the register total, declare it in LOCAL_COUNT: ...%s..."
                             % (m.group(1), thing, actual, ctx[-100:])))

    findings.extend(_scope_claims(text))
    return findings


# Each entry is a defect this gate has actually shipped past at least once, restated as an injection.
# A green --check proves the document is clean; it proves nothing about the gate. Two of the five
# verdicts here were silently broken while reporting OK -- STALE_ZONE could not see "no SEAM zone at
# all" because nothing beside it was a KIND word, and COUNT_DRIFT could not see "forty components"
# because it only read digits. Both were found by hand. So the firing is asserted, not assumed.
SELFTEST = [
    ("SCOPE_CLAIM", "| **D2** | **Scope of THIS spec.** SDL_GPU backend, CI harness and bot driver "
                    "are each **follow-on specs that consume the contract**. |"),
    ("SCOPE_CLAIM", "but D2 places it out of scope for this spec."),
    ("SCOPE_CLAIM", "an explicit list of what this spec does not cover."),
    ("SCOPE_CLAIM", "The audio work is left to a later document entirely."),
    ("COUNT_DRIFT", "The design now has forty components."),
    ("COUNT_DRIFT", "the dedicated server links nine of thirty-four components."),
    ("COUNT_DRIFT", "The register holds 34 components."),
    ("STALE_ZONE",  "the dedicated server has no SEAM zone at all."),
    ("STALE_ZONE",  "`content` is a LIBRARY, SUBSTRATE."),
    ("UNKNOWN_NAME", "The `client` component owns the prediction clock."),
    ("DANGLING_D",  "This follows directly from D14."),
]
# A gate that fires on everything is as useless as one that fires on nothing.
SELFTEST_CONTROL = "The `world` component owns a fixed clock and `participant` predicts against it."


def selftest(text):
    base = scan(text)
    if base:
        print("prose-claims: selftest needs a clean document; --check reports %d finding(s)" % len(base))
        return 1
    bad = 0
    for want, injection in SELFTEST:
        kinds = {k for k, _ in scan(text + "\n\n" + injection)}
        ok = want in kinds
        bad += not ok
        print("  %-12s %-6s %s" % (want, "FIRES" if ok else "SILENT", injection[:64]))
    control = scan(text + "\n\n" + SELFTEST_CONTROL)
    if control:
        bad += 1
        print("  CONTROL      FIRED  -- legitimate prose was flagged: %s" % control[0][1][:80])
    else:
        print("  CONTROL      quiet  -- legitimate prose is not flagged")
    if bad:
        print("prose-claims: SELFTEST FAIL -- %d verdict(s) did not behave" % bad)
        return 1
    print("prose-claims: selftest OK -- %d injections fire, control stays quiet" % len(SELFTEST))
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--selftest", action="store_true",
                    help="assert every verdict still fires on a known defect")
    args = ap.parse_args(argv)

    text = MODEL.SPEC.read_text(encoding="utf-8")
    if args.selftest:
        return selftest(text)
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
