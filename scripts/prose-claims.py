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
# THE LIVE KINDS, from spec-model rather than retyped. This line read
# `(?:FOUNDATION|CONTRACT|BACKEND|LIBRARY|ENTRYPOINT)` for a day after the CONTRACT split --
# so the gate written to catch stale vocabulary could not see INTERFACE or VOCABULARY at all,
# and treated CONTRACT as live. A gate's vocabulary outliving the document's is a defect this
# project has now hit four times; the fix each time is to stop keeping a second copy.
KINDWORD = r'(?:%s)' % '|'.join(MODEL.KINDS)
# A dead zone name is also a live claim when the word "zone" is what sits beside it. The sentence
# "the dedicated server ... has no SEAM zone at all" survived every run of this gate, because the
# KIND-word adjacency test was the only trigger and that sentence names no KIND.
#
# CASE-INSENSITIVE ON THE ZONE WORD, and that one flag is worth a defect. The first version wrote
# `\bzones?\b` case-sensitively and stayed green over a live five-row zone TABLE headed `| ZONE |`,
# whose `SEAM` row contradicted the sentence directly beneath it saying the zones were cut from five
# to four. The gate could see the lowercase sentence it was written against and not the uppercase
# table three hundred lines earlier.
TRIGGER = r'(?:%s|\b[Zz][Oo][Nn][Ee][Ss]?\b)' % KINDWORD[3:-1]

# Backticked lowercase tokens that are legitimately NOT components. Each carries its reason, so the
# list cannot quietly become a place to bury a stale name -- which is the only way this gate fails.
ALLOWED = {
    # INSTRUMENTS THAT ARE NOT REGISTERED GATES. Registered gate names are folded in below, read from
    # gates.yml -- they were hand-listed here until 2026-08-02, and the hand list did what hand lists
    # do: registering `round_trip_ceiling` in the workflow made the document red, because the second
    # declaration of the same fact had not been updated. One writer. What stays here is what
    # gates.yml does NOT know about.
    "dedup_measure": "measurement, not a gate",
    "link_sweep": "MEASURES containment; deliberately not gated -- it reads a build tree",
    "render_surface_tests": "test", "core_tests": "test", "game_tests": "test",
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
    # Lua callback GROUP names -- a namespace of its own, listed in Section 2's Surface A table.
    # Worth noting what this list cannot do: `celestial` and `world` are group names AND component
    # names, so those two reach the clean bucket for the wrong reason. The overlap is real in the
    # code, not an artifact here, and it is why the Surface A table names what each group *binds*
    # rather than trusting the group name to say it.
    "camera": "Lua callback group; binds a WorldCamera owned by the WorldPainter",
    "renderer": "Lua callback group; binds the shell itself",
    "clipboard": "Lua callback group; binds the Application",
    "interface": "Lua callback group; binds MainInterface",
    "voice": "Lua callback group; defined in source/frontend",
    # DOMAIN NOUNS from Section 1 -- the fourth namespace, and the one the document argues in before
    # any component exists. These are what the SYSTEM is made of; components are what the CODE is
    # made of, and the two vocabularies are deliberately different. Section 1 says so in the same
    # words, and did NOT until 2026-08-02: it claimed instead that a register name it had not defined
    # was "naming something the domain does not contain", which this list has always contradicted.
    # Two writers, one fact. Section 1 now states the narrower rule it was actually protecting --
    # where the register REUSES one of these words, both senses must be stated -- and
    # `check_shared_words` reads it.
    #
    # The list holds the nouns that are NOT also component names. `universe`, `world`, `content` and
    # `participant` are both, so they reach the clean bucket as components and never arrive here --
    # which is exactly the intersection the shared-words table has to declare.
    #
    # Worth recording that `participant` is BOTH -- a role in section 0 and a component in
    # `composition/` -- so it reaches the clean bucket as a component and its role sense is never
    # checked. That is a homonym of exactly the kind the index review flagged as needing an explicit
    # declaration, and it is the reason section 0 says a role is not a component in so many words.
    "authority": "Section 1 role: owns the truth of a world or universe",
    "device": "Section 1 role: a display, speaker, file or recorder",
    "player": "Section 1: an ENTITY in a world, emphatically not a participant",
    "entity": "Section 1 noun: a thing IN a world -- player, monster, object, projectile",
    "system": "Section 1 noun: a star and its bodies, simulated on a slower cadence than a world",
    # THREAD names from the runtime register's thread column -- a third namespace, alongside
    # components and Lua groups. `driver`, `main`, `universe`, `world` and `audio`; only `driver`
    # and `main` are not also component names, so only those two ever reach this list.
    "driver": "runtime THREAD name, from the element register's thread column",
    "main": "runtime THREAD name; the process's initial thread",
}


def _fold_in_registered_gates():
    """Every name gates.yml runs is a legitimate backticked token, by construction.

    Naming a gate in the prose is the OPPOSITE of the corpse UNKNOWN_NAME hunts: `check_instruments`
    already fails a gate name that is NOT registered, so the two checks meet in the middle -- prose
    may name a registered gate and may not name an unregistered one, and neither list is hand-kept.
    """
    for name in registered_gates() or ():
        ALLOWED.setdefault(name, "registered gate, read from gates.yml")

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
    ("Deferral is a schedule, never a boundary",
     "Section 18's deferral item, which exists to keep sequencing from becoming scope again"),
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
    ("two of the four components adopted today", "a subset of one day's adoptions"),
    ("A shared input consumed by two components", "the two consumers of that input"),
    ("because two components consume the star map", "the two consumers of the star map"),
    ("the client's three elements", "the three time-domain elements, not the register"),
    ("`frameLoop` reaching four elements", "that loop's out-degree"),
    ("Thirty-two components had a duty", "the underived subset, not the register total"),
    ("not survive the two components being placed", "the two sharing a lock, not the register"),
    ("do two components both know about it", "the two sharing a lock, not the register"),
    ("No two components name one lock", "the two sharing a lock, not the register"),
    # Both surfaced only once the scan became case-insensitive: a sentence-initial count is
    # capitalised, and every one of them had been invisible.
    ("Four components for four files", "the audio carve-out, not the register"),
    ("Two components have been carved out", "the two acyclic carve-outs, not the register"),
    ("it is the reason two components with no grant between them",
     "any two co-resident components, not a count"),
)


# A DECLARATION THAT OUTLIVED ITS SENTENCE.
#
# LOCAL_COUNT is a list of permissions, each keyed on a literal phrase, and rewording is meant to
# re-arm the gate. It does -- for the sentence. It does nothing about the permission, which stays in
# the table matching nothing, ready to excuse some future sentence that happens to be worded like the
# dead one. On 2026-08-02 two entries went dead within the hour: "197 files across four components"
# was corrected to 211 across seven, and "it stands at seven components today" was deleted outright
# once the generated figure said 27. Neither removal was noticed, because a whitelist that matches
# nothing is indistinguishable from a whitelist that is not needed.
#
# This is the same class as the CITES regex that kept accepting `A1`-`A6` after those symbols were
# retired: THE GATE'S VOCABULARY OUTLIVED THE DOCUMENT'S. The cure is the same in both places -- the
# accepted set is checked against the document, not merely consulted by it.
#
# BOTH permission tables are checked, and the second one earned its place the same day: rewriting
# Section 18's deferral item left SCOPE_EXEMPT's entry matching nothing, and the only reason anyone
# noticed is that SCOPE_CLAIM went red on the new wording. Had the rewrite happened to keep the old
# phrase's shape, the exemption would have survived as a standing permission for a sentence nobody
# had read. A check that covers one table of this kind and not the other is a half-applied lesson.
#
# `_scope_claims` matches against a flattened whole document rather than `prose`, so each table is
# checked against the corpus its own permission is consulted on -- otherwise this verdict would fire
# on entries that are alive.
def dead_declarations(corpus, table=LOCAL_COUNT):
    flat = " ".join(corpus.split())
    return [ph for ph, _why in table if " ".join(ph.split()) not in flat]


# A NAMED INSTRUMENT THAT IS NOT REGISTERED, OR DOES NOT EXIST.
#
# On 2026-08-02 this document named FOUR instruments that did not do what the sentence said: a
# "placement gate" that had never been written, edges "machine-verified against the register" by
# nothing, an UNVERIFIABLE count that "should fall to zero" as though that were a pass condition,
# and a gap "listed in Section 18" that Section 18 had never carried. A fifth was mine, written an
# hour earlier: `check_reachable_implementations` existed and was never called.
#
# An instrument NAME reads as evidence. A reader -- especially an agent building a plan from this --
# stops checking at the word "gated", so a false instrument claim is STRONGER than no claim: it
# closes the question instead of leaving it open. Vigilance is not a mechanism; this is.
#
# TWO VERDICTS, and the distinction matters because the document legitimately cites both:
#   UNREGISTERED_GATE  "gated by `X`" where X is not a step in .github/workflows/gates.yml. A gate
#                      that is not registered does not run, and `run-gates.sh` reads that file, so
#                      unregistered means unrun on every machine including this one.
#   MISSING_SCRIPT     a `scripts/X.py` named in the prose that is not on disk at all.
#
# A tool the document merely CITES ("`link_sweep` measures ...") is fine and is not checked here --
# only the claim that something is GATED, which is a claim about what runs.
_GATED_BY = re.compile(r'[Gg]ated (?:by|as) `([a-z_][a-z0-9_]*)`')
_SCRIPT = re.compile(r'`(scripts/[a-z0-9_.-]+\.py)`')


def registered_gates():
    """The gate names `scripts/ci/run-gates.sh` actually runs, read from the same file it reads."""
    wf = REPO / ".github" / "workflows" / "gates.yml"
    if not wf.exists():
        return None                        # nothing to check against; say so rather than pass
    try:
        import yaml
    except ImportError:
        return None
    data = yaml.safe_load(wf.read_text(encoding="utf-8"))
    return {s.get("name", "").split(" --")[0]
            for job in data.get("jobs", {}).values()
            for s in job.get("steps", []) if "run" in s}


def check_instruments(text, gates=None):
    """Every instrument the document says GATES something must be a registered gate that exists."""
    out = []
    gates = registered_gates() if gates is None else gates
    if gates is None:
        out.append(("MISSING_SCRIPT",
                    "the gate workflow could not be read, so no instrument claim in this document "
                    "can be checked -- which is itself the condition this verdict exists to report"))
        return out
    for m in _GATED_BY.finditer(text):
        if m.group(1) not in gates:
            out.append(("UNREGISTERED_GATE",
                        "the document says something is gated by `%s`, which is not a step in "
                        "gates.yml -- an unregistered gate is one that never runs. Register it, or "
                        "say `%s` MEASURES rather than gates" % (m.group(1), m.group(1))))
    for m in _SCRIPT.finditer(text):
        if not (REPO / m.group(1)).exists():
            out.append(("MISSING_SCRIPT",
                        "`%s` is named in the document and is not on disk" % m.group(1)))
    return out


_BARE_GOAL = re.compile(r'\*\*(N[123])\*\*')
_REGISTER_ROW = re.compile(r'^\| \*\*`[a-z_]+`\*\* \|')
_SERVES_ROW = re.compile(r'^\| \*\*serves\*\* \|')
_CLAUSE_DEF = re.compile(r'^\| \*\*(N[123]\.[a-z])\*\* \|', re.M)
_CLAUSE_CITE = re.compile(r'\b(N[123]\.[a-z])\b')


def defined_clauses(text):
    """The clause labels Section 3 actually defines, read from the clause tables' own rows."""
    return set(_CLAUSE_DEF.findall(text))


def check_clause_vocabulary(text):
    """A cited clause must be one Section 3 defines.

    BARE_GOAL asks whether a warrant NAMED a clause. It cannot ask whether the clause EXISTS, and a
    check that accepts `N1.e` is a check that reads the shape of a citation rather than its meaning --
    the same trap as a gate that greps for the wrong word and scores a screaming oracle as PASS.
    `DANGLING_D` has always done this for decisions; goals had nothing until the clauses existed to
    be dangling from.
    """
    known = defined_clauses(text)
    if not known:
        return [("DANGLING_CLAUSE",
                 "Section 3 defines no goal clauses at all, so every clause citation in this "
                 "document is dangling -- the clause tables are gone or their row shape changed")]
    return [("DANGLING_CLAUSE",
             "%s is cited but Section 3 defines no such clause (it defines %s)"
             % (c, ", ".join(sorted(known))))
            for c in sorted(set(_CLAUSE_CITE.findall(text)) - known)]


def check_warrant_clauses(text):
    """A WARRANT must name a clause, not a goal.

    Section 3 states the rule about itself -- *"a warrant that says N1 without saying which clause has
    not said much"* -- and then, for a year, every N2 and N3 warrant in the document was exactly that.
    The rule could not be enforced while two of the three goals had no clauses to name; now all three
    do, so the rule is checkable and this is the check.

    Only WARRANT POSITIONS are read: field 5 of a register row, and the whole cell of a `serves` facet.
    Prose that discusses a goal as a whole -- Section 3's own headings, "N1 and N3 are one property
    seen at two scales" -- is not a warrant and is left alone. Narrowing the check to the position is
    what keeps it from becoming a ban on ever writing the goal's name.
    """
    out = []
    for line in text.splitlines():
        if _REGISTER_ROW.match(line):
            cells = line.split("|")
            cell = cells[5] if len(cells) > 6 else ""
        elif _SERVES_ROW.match(line):
            cell = line.split("|", 2)[2]
        else:
            continue
        for m in _BARE_GOAL.finditer(cell):
            out.append(("BARE_GOAL",
                        "a warrant cites **%s** with no clause. Every goal now has clauses, and a "
                        "warrant naming only the goal has not said which cost it is paying: %s"
                        % (m.group(1), " ".join(cell.split())[:110])))
    return out


_SEC1 = re.compile(r'^## 1\. .*?^## 2\. ', re.M | re.S)
_SEC1_NOUN = re.compile(r'^\| \*\*([a-z][a-z ]*)\*\* \|', re.M)
_SHARED_TABLE = re.compile(r'<!-- TABLE: shared-words -->(.*?)<!-- END TABLE: shared-words -->', re.S)
_SHARED_ROW = re.compile(r'^\| \*\*`([a-z_]+)`\*\* \|', re.M)


def check_kind_rules(text):
    """Section 7 must state a rule for every live KIND, and for no dead one."""
    rules = MODEL.kind_rules(text)
    if not rules:
        return [("KIND_RULE", "Section 7's kind-rules table is missing or unparseable, so the "
                              "document defines no kind at all")]
    live = set(MODEL.KINDS)
    return ([("KIND_RULE", "`%s` is a live KIND with no rule in Section 7 -- a component can be "
                           "declared of a kind the document never defines" % k)
             for k in sorted(live - set(rules))] +
            [("KIND_RULE", "Section 7 states a rule for `%s`, which is not a KIND -- a retired kind "
                           "still carrying a rule reads as live" % k)
             for k in sorted(set(rules) - live)])


def check_shared_words(text, components):
    """Section 1's reuse rule, made exact.

    Section 1 once claimed that a register name it had not defined was "naming something the domain
    does not contain" -- false of 41 of 45 rows, and flatly contradicted by the ROLE-name comment in
    this file, which is two writers for one fact. The rule was narrowed to what it was actually
    protecting: where the register REUSES a Section 1 noun, the two senses must both be stated.

    The declared set is not a matter of taste, so it is not trusted: it must equal the intersection of
    Section 1's own bolded noun definitions with the register's names. A component named `entity`
    tomorrow makes the table stale in one direction; deleting `content` makes it stale in the other.
    Both are the same defect and both are reported.
    """
    sec1 = _SEC1.search(text)
    if not sec1:
        return [("SHARED_WORD", "Section 1 could not be located, so its reuse rule cannot be "
                                "checked -- the heading shape changed or the section is gone")]
    body = sec1.group(0)
    table = _SHARED_TABLE.search(body)
    if not table:
        return [("SHARED_WORD", "Section 1 states the reuse rule and declares no shared-words table, "
                                "so the rule names no words and checks nothing")]
    want = {n for n in _SEC1_NOUN.findall(body) if n in components}
    have = set(_SHARED_ROW.findall(table.group(1)))
    return ([("SHARED_WORD",
              "`%s` is both a Section 1 noun and a register name, and the shared-words table does "
              "not carry it -- a reused word with only one sense stated" % w)
             for w in sorted(want - have)] +
            [("SHARED_WORD",
              "the shared-words table carries `%s`, which is no longer both a Section 1 noun and a "
              "register name -- the row is stale" % w)
             for w in sorted(have - want)])


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
    _fold_in_registered_gates()
    comp, grants, elem = MODEL.components(text), MODEL.grants(text), MODEL.elements(text)
    findings = []

    # Historical blocks are exempt from every verdict that is a claim about NOW: a dead zone name and
    # a superseded count are the same kind of fact, and explaining either requires stating it.
    live = re.sub(r'<!-- HISTORICAL -->.*?<!-- END HISTORICAL -->', "", text, flags=re.S)
    # THE WINDOW MUST CROSS LINES. `.` does not match a newline, so a ±60 window written against
    # running prose never left the current line -- and the defect this verdict was extended to catch
    # was a markdown TABLE whose `| ZONE |` header sat two rows above its stale `| **SEAM** |` row.
    # Case-insensitivity alone did not find it; the window was the other half of the same blind spot.
    flat = " ".join(live.split())
    for z in DEAD_ZONES:
        for m in re.finditer(r'.{0,60}\b%s\b.{0,60}' % z, flat):
            ctx = m.group(0)
            if re.search(TRIGGER, ctx):
                findings.append(("STALE_ZONE",
                                 "%r used as a zone beside a KIND or ZONE word: ...%s..."
                                 % (z, ctx.strip()[:96])))

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

    # `_scope_claims` strips emphasis before matching, so SCOPE_EXEMPT is checked against the same
    # stripped corpus; otherwise an entry carrying `**` reads as dead while being live.
    def _strip(s):
        return s.replace("**", "").replace("*", "").replace("~~", "")

    stripped_table = tuple((_strip(ph), why) for ph, why in SCOPE_EXEMPT)
    for table, name, corpus in ((LOCAL_COUNT, "LOCAL_COUNT", prose),
                                (stripped_table, "SCOPE_EXEMPT", _strip(live))):
        for ph in dead_declarations(corpus, table):
            findings.append(("DEAD_DECLARATION",
                             "%s declares %r, which no longer appears in the document -- delete the "
                             "entry with the sentence, or it will excuse the next one worded like it"
                             % (name, ph)))

    # DANGLING_SECTION -- a cross-reference to a section this document does not have.
    #
    # WHY NOW. The document carries 72 "Section N" references and is about to be restructured, which
    # renumbers all of them. Hand-renumbering 72 references is how a document acquires a reference to
    # a section that no longer exists, and nothing here would have noticed: prose_claims checked
    # decision references (DANGLING_D) and never section references.
    #
    # THE QUALIFIER RULE, and it is the whole difficulty. Not every "Section N" is about THIS
    # document -- L4388 reads "The boundary document's Section 12", which is a live, correct
    # reference to docs/architecture/system-boundaries.md. A naive check flags it and gets
    # suppressed, and a suppressed verdict is worse than none. So a reference qualified by another
    # document is skipped, and the qualifier must appear in the sentence rather than be inferred.
    own = set(re.findall(r'^## (\d+)\.', text, re.M))
    for m in re.finditer(r'Section (\d+)', prose):
        before = prose[max(0, m.start() - 70):m.start()]
        if re.search(r"document's\s*$|\.md[^.]*$|boundary document\S*\s*$", before):
            continue
        if m.group(1) not in own:
            findings.append(("DANGLING_SECTION",
                             "references Section %s, which this document does not have: ...%s..."
                             % (m.group(1), " ".join(prose[max(0, m.start()-60):m.end()+40].split()))))

    findings.extend(check_instruments(text))
    findings.extend(check_warrant_clauses(text))
    findings.extend(check_clause_vocabulary(text))
    findings.extend(check_shared_words(text, comp))
    findings.extend(check_kind_rules(text))
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
    ("DANGLING_SECTION", "The rule is stated in full in Section 27."),
    ("UNREGISTERED_GATE", "The closure is gated by `composition_freshness` on every push."),
    ("MISSING_SCRIPT", "The figures come from `scripts/nonexistent-measure.py`."),
    ("BARE_GOAL", "| **serves** | **N3** — it composes, which is all anyone needs to know. |"),
    ("DANGLING_CLAUSE", "| **serves** | **N1.e** — a fifth clause N1 does not have. |"),
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
    # DEAD_DECLARATION cannot be injected as prose -- the defect is a table entry, not a sentence --
    # so it is exercised directly, both ways round. A check that only ever runs against a table it
    # has already been made to agree with proves nothing.
    fake = (("a phrase this document does not contain anywhere", "proof the check can fail"),)
    if dead_declarations("some prose", fake) != [fake[0][0]]:
        bad += 1
        print("  DEAD_DECLARATION SILENT -- an absent phrase was not reported")
    elif dead_declarations("... a phrase this document does not contain anywhere ...", fake):
        bad += 1
        print("  DEAD_DECLARATION FIRED  -- a present phrase was reported dead")
    else:
        print("  %-12s %-6s %s" % ("DEAD_DECLARATION", "FIRES", "an entry matching nothing in the document"))

    # SHARED_WORD is directional -- it compares a declared set against a computed intersection -- so
    # appending prose cannot exercise it. Both directions are driven directly, because a check that
    # only ever runs against a table it has already been made to agree with proves nothing.
    comp = MODEL.components(text)
    no_row = text.replace("| **`content`** | materials, items, species, dungeons, biomes, monsters, "
                          "recipes — declared as data", "| **`REMOVED`** | x", 1)
    stale = text.replace("<!-- END TABLE: shared-words -->",
                         "| **`gpu`** | x | x | x |\n\n<!-- END TABLE: shared-words -->", 1)
    for label, mutated in (("a reused word with no row", no_row), ("a row for an unshared word", stale)):
        if not check_shared_words(mutated, comp):
            bad += 1
            print("  SHARED_WORD  SILENT -- %s was not reported" % label)
        else:
            print("  %-12s %-6s %s" % ("SHARED_WORD", "FIRES", label))

    # KIND_RULE compares a declared set against spec-model's KINDS, so appended prose cannot reach
    # it either. Both directions driven directly: a live kind whose rule is gone, and a rule for a
    # kind that no longer exists -- which is exactly the state Section 7 was in for a day.
    renamed = text.replace("| **VOCABULARY** | declares the types that cross a seam",
                           "| **XXXX** | declares the types that cross a seam", 1)
    kinds = {k for k, _ in check_kind_rules(renamed)}
    if kinds == {"KIND_RULE"} and len(check_kind_rules(renamed)) == 2:
        print("  %-12s %-6s %s" % ("KIND_RULE", "FIRES", "a live kind unruled, and a dead kind ruled"))
    else:
        bad += 1
        print("  KIND_RULE    SILENT -- renaming a kind row was not reported both ways")

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
