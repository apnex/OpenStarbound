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
  COUNT_DRIFT  a prose count of something the model knows, disagreeing with the model. It reads the
               DENOMINATOR of an `N of M` and deliberately not the numerator, which is why the three
               verdicts below exist.
  CLOSURE_DRIFT a composition size -- the `39` in "39 of 49 components" -- that is no ENTRYPOINT's
               transitive closure, or is not the closure of the entrypoint whose heading or table row
               it sits in. Twelve sentences and two whole tables carried one, unread by everything.
  MODAL_ZONE   the one `N of M components` that is not a closure: how far ZONE is determined by KIND.
  SUBTRACTION  "`X` is exactly `Y` minus N grants -- ..." checked against the grant table AND against
               what the two closures actually differ by, which is not the same number.
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
Drawable", "29 SDL_GL references" -- are not re-measured here. They are claims about a moving
codebase, and a gate that re-ran every one of them would be slow, flaky, and would silently change
what the document says. Those belong to the ratchets that own them. **This gate checks the document
against ITSELF, not against the world**, and saying so is the difference between a limit and a hole.

HISTORICAL PASSAGES. A block wrapped in `<!-- HISTORICAL -->` ... `<!-- END HISTORICAL -->` is exempt
from STALE_ZONE, because explaining why a name was replaced requires naming it. Nothing else is exempt.
"""
import argparse
import collections
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
    # AXIOM DOMAIN-OF-VALIDITY TAGS. Not components and never will be: an axiom binds a system whose
    # architecture satisfies its tags, so these are the names of the CONDITIONS, not of anything the
    # register holds. They lived only inside the axiom table's tag column until D14 argued the two
    # conditional axioms into force in prose, which is the first time the checker could see them.
    "stateful": "axiom domain-of-validity tag (A1), not a component",
    "declarative": "axiom domain-of-validity tag (A2), not a component",
    "any-system": "axiom domain-of-validity tag (the unconditional set), not a component",
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
    # NOT AN EXEMPTION. This is the only entry here whose number is checked by another verdict:
    # `check_subtraction` derives both figures from the grant table and the two closures. The entry
    # exists so COUNT_DRIFT does not read a closure DELTA as a register total -- it is a pointer to
    # the owner, not a permission to be wrong.
    ("Five grants, nine components", "a closure delta, owned by SUBTRACTION"),
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


# THE DEAD-KIND RATCHET. `CONTRACT` was retired on 2026-08-02 when it split into INTERFACE and
# VOCABULARY, and the split reached the register, the diagrams, the tally and the tree while leaving
# the prose alone. Two of those leftovers were not cosmetic -- the GRAFT rule and the `==>` arity
# rule were stated over "contract" in a form `spec-consistency` had already narrowed to INTERFACE,
# so the document and its gate disagreed about the document's own rules, and the gate was right.
#
# THE RATCHET REACHED ZERO ON THE DAY IT WAS SET. All 25 sites were read and decided: most meant
# INTERFACE, four meant VOCABULARY (`net`, `scene`, `sound`), and the last two were narration OF THE
# RENAME ITSELF -- the one thing that cannot be said without naming the retired kind, which is what
# the HISTORICAL fence is for.
#
# At zero this stops being a ratchet and becomes a ban, and that is the right shape now: there is no
# longer a legitimate live use, so the next appearance is a regression rather than a leftover. Raising
# this number is not a way to make a build green.
DEAD_KIND_CEILING = 0
DEAD_KINDS = ("CONTRACT",)


# A contract may name FOUNDATIONs and other contracts, and nothing else. Section 7 said "only
# foundation types" for a day -- a rule two of the eight INTERFACEs already broke, `host` naming
# `platform` and `presentation` naming `scene` and `sound`. It was not a tightening, it was false,
# and it sat in a table three gates could see while the grant table's own rationale stated the
# correct rule three separate times. Prose said one thing, the register did another, nothing
# compared them. This compares them.
CONTRACT_KINDS = ("INTERFACE", "VOCABULARY")


def check_contract_grants(comp, grants):
    """An INTERFACE or VOCABULARY may grant only a FOUNDATION or another contract."""
    allowed = ("FOUNDATION",) + CONTRACT_KINDS
    out = []
    for name, v in sorted(comp.items()):
        if v.get("kind") not in CONTRACT_KINDS:
            continue
        for g in sorted(grants.get(name, set())):
            k = comp.get(g, {}).get("kind", "?")
            if k not in allowed:
                out.append(("CONTRACT_GRANT",
                            "`%s` is a %s and grants `%s`, which is a %s. A contract may name only "
                            "FOUNDATIONs and other contracts -- naming a %s makes it a coupling "
                            "wearing a seam's label" % (name, v["kind"], g, k, k)))
    return out


# A TARGET STATE HAS NO DATE. It describes what the system should be, which is not a claim about a
# particular day. A sentence of the form "X was Y until <date>" is either irrelevant now -- cut it --
# or it carries the reason the design is what it is, in which case the REASON is load-bearing and the
# date is scaffolding. Six such sentences were restructured to keep the warrant and drop the calendar;
# the seventh is in Section 17, whose subject IS the current tree and whose measurements are
# unfalsifiable without a date. That section is exempt BY NAME and nothing else is.
_DATE = re.compile(r'\b20\d{2}-\d{2}-\d{2}\b')
DATED_SECTION = "17"


def check_no_dates(text):
    """No calendar dates outside the one section whose subject is the current tree."""
    lines, out, section = text.splitlines(), [], None
    for i, ln in enumerate(lines, 1):
        m = re.match(r'^## (\d+)\. ', ln)
        if m:
            section = m.group(1)
        if section == DATED_SECTION:
            continue
        for d in _DATE.findall(ln):
            out.append(("DATED_CLAIM",
                        "line %d carries the date %s outside Section %s. A target state has no date: "
                        "either the passage is irrelevant now and should be cut, or it carries a "
                        "warrant -- state the warrant and drop the calendar" % (i, d, DATED_SECTION)))
    return out


# A3'S LAW OF ONE, WHICH THE DOCUMENT CITES MORE THAN ANY OTHER RULE AND NEVER CHECKED. Section 2
# states that A3 forbids *"god objects, dual-purpose modules, 'and'/'also' in a duty"*, and eight duty
# strings carried "and".
#
# WHY IT MATTERS MORE THAN TIDINESS: the register IS A DUTY INDEX AT COMPONENT ALTITUDE -- 45 rows,
# one duty each -- and the element register is the same index at ELEMENT altitude. A row whose duty
# says "and" cannot be read as an index entry, because the row no longer denotes one thing. The column
# stops being a decomposition and becomes a description.
#
# Of the eight, four NAMED EXAMPLES RATHER THAN THE CONCERN and were rewritten to their own `boundary`
# facet: `universe` ("decides which worlds exist and who is where" -> "the decisions no single world
# can make"), `universe_view`, `platform_pc`, `world_gen`. Three are genuinely one duty and are
# declared below WITH THE REASON, because an undeclared exemption is where the next real violation
# hides. One is a real violation and is left standing as this ratchet's target.
SINGLE_DUTY = {
    "core": "defined by SUBTRACTION -- what remains once every duty has been named and moved out. "
            "Its boundary facet says so: the language, the containers, the algorithms are three "
            "examples of one residue, not three duties",
    "storage": "the derivation argues this explicitly -- *a store that cannot read yesterday's file "
               "is not a store* -- so persistence and migration are one duty stated in two clauses",
    "participant": "a participant IS a clock and the parts it drives; separate them and the clock "
                   "has no subject. The `and` joins a thing to what it is made of, not two jobs",
}
# ONE. `celestial` came off this list by being SPLIT, not by being reworded: its duty read "the star
# map's vocabulary and its lookup interface" and it is now four components -- the VOCABULARY keeps the
# name, and the lookup is an INTERFACE with two BACKENDs. The ratchet fell 2 -> 1 because the
# architecture changed, which is the only way a ratchet is allowed to fall.
#
#   `platform_pc`  "Steam, Discord and P2P services" -- its own `owes` facet calls this "the plainest
#                  Law-of-One violation in the register ... three backends, or `platform` should be
#                  three contracts, and neither has been decided".
#
# I REWROTE `platform_pc`'S DUTY TO MAKE THIS GATE GREEN AND HAD TO REVERT IT. The duty string is the
# EVIDENCE of the open question; rewording it to "the vendor implementations of `platform`" would have
# retired a recorded defect by renaming it, which is the failure this whole file exists to catch. A
# ratchet that can be satisfied by editing the thing it measures is not a ratchet.
LAW_OF_ONE_CEILING = 1
_CONJUNCTION = re.compile(r'\b(and|also)\b', re.I)


def check_law_of_one(comp):
    """A duty names ONE thing. A3's most-cited rule, finally read."""
    offenders = sorted(n for n, v in comp.items()
                       if n not in SINGLE_DUTY and _CONJUNCTION.search(v.get("duty", "")))
    if len(offenders) > LAW_OF_ONE_CEILING:
        return [("LAW_OF_ONE",
                 "%d duty strings join two things with and/also, above the ceiling of %d: %s. A duty "
                 "names ONE thing -- split the component, rewrite the duty to the concern its "
                 "boundary facet names, or declare it in SINGLE_DUTY with the reason"
                 % (len(offenders), LAW_OF_ONE_CEILING,
                    ", ".join("`%s`" % o for o in offenders)))]
    return []


# THE NUMERATOR NOBODY READ.
#
# COUNT_DRIFT has a subset rule: on seeing `N of M components` it verifies M against the register and
# then SKIPS, because N is a subset and not the total. That is correct as far as it goes, and it
# leaves N read by nothing. `39` is never followed by the word "components", so no verdict in this
# file has ever looked at it -- and twelve hand-written sentences state one.
#
# Those numbers are composition CLOSURE sizes: an ENTRYPOINT plus the transitive closure of its
# grants, which `composition-graphs` already computes to draw the per-composition diagrams. Every
# stage of a decomposition changes them. The register went 41 -> 45 -> 48 -> 49 in four steps and the
# twelve sentences were re-typed by hand each time, four times, with the gate reporting OK on all of
# it -- so the ONLY thing standing between this document and a wrong closure count was whether the
# author had miscounted that day. That is not a mechanism.
#
# ATTRIBUTION IS TWO-TIER, and the weaker tier is deliberate rather than lazy:
#
#   STRICT   under a `### <entrypoint>` heading, or in a table row whose first cell names one, N must
#            equal THAT entrypoint's closure.
#   LOOSE    everywhere else, N must equal the closure of SOME entrypoint. Sentences legitimately
#            quote another composition's number as a COMPARISON -- "24 of 49 against the graphical
#            client's 39" sits under a `colocation` heading -- so a strict rule everywhere would cry
#            wolf, and a verdict that cries wolf gets suppressed. The loose tier still catches what
#            actually happens: a stage changes the closure sizes and a stale number matches none.
#
# ATTRIBUTE FROM THE BACKTICKED SUBJECT, NEVER FROM THE HEADING'S ENGLISH. The first version searched
# the whole heading for any entrypoint name, so `### \`colocation\` -- the client stops containing a
# server` attributed its section to `server`, on the strength of the last word of a sentence about
# something else. Two of this verdict's first four findings were that bug. A heading's subject is the
# component it backticks first; the rest is prose and must not be read as a name.
#
# THE ROW FORM EXISTS BECAUSE THE WORST DRIFT HAD NO NOUN. Section 12 carried a three-row table
# reading `32 of 41`, `26 of 41`, `19 of 41` -- the 41-component era, two register generations stale,
# with the correct figures in the prose directly beneath it. COUNT_DRIFT could not see it, because
# nothing in those cells is the word "components", and a bare `N of M` in running prose is far too
# common to match on. In a row whose first cell backticks an ENTRYPOINT it is not ambiguous at all.
#
# Not every `N of M components` is a closure. The one that is not -- Section 7's KIND-modal-ZONE
# deviation -- is CHECKED by `check_modal_zone` below rather than excused here, because an exemption
# is a hole and a second verdict is not.
_CLOSURE_CLAIM = re.compile(r'(\d+)\s+of\s+(\d+)\s+components')
_CLOSURE_ROW = re.compile(r'^\|([^|]*)\|[^|]*?\b(\d+)\s+of\s+(\d+)\b')
_SUBJECT = re.compile(r'`([a-z_][a-z0-9_]*)`')

# Section 7's one non-closure `N of M components`: how many components sit in a zone other than the
# modal zone for their KIND. It is the measurement that decided ZONE was worth keeping as an axis at
# all -- when it was 8 of 45, the axis was 80% redundant with KIND and nearly did not survive review.
# A number carrying that much argument should not be a number somebody typed.
MODAL_ZONE_PHRASE = "components deviate from their KIND's modal ZONE"

# The same sentence names the KIND whose modal zone is weakest, and states the split. It read "BACKEND
# has no modal zone at all: it splits exactly six MACHINE to six DEVICE" while the register held nine
# MACHINE, six DEVICE and two DOMAIN -- a claim about the axis's own weakest case, ungated, sitting
# one clause after a claim that was gated. The verdict below reads all three figures AND re-derives
# which KIND is weakest, because naming the wrong KIND would leave three correct numbers.
BACKEND_SPREAD = re.compile(
    r'BACKEND is where KIND predicts ZONE least: \*\*(\d+) of its (\d+) sit in the modal (\w+), '
    r'the other (\d+) split (\d+) (\w+) and (\d+) (\w+)\*\*')


def _closures(text):
    """-> {entrypoint: closure size}, computed by the module that owns the computation."""
    spec = importlib.util.spec_from_file_location("composition_graphs",
                                                  str(REPO / "scripts" / "composition-graphs.py"))
    cg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cg)
    comp, grants = cg.parse(text)
    return {n: len(cg.closure(n, comp, grants))
            for n, v in comp.items() if v["kind"] == "ENTRYPOINT"}


def _derivations():
    spec = importlib.util.spec_from_file_location("spec_derivations",
                                                  str(REPO / "scripts" / "spec-derivations.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check_closures(text):
    """Every prose closure count must be a closure some ENTRYPOINT actually has."""
    sizes = _closures(text)
    if not sizes:
        return [("CLOSURE_DRIFT", "no ENTRYPOINT closures could be computed -- the register parse "
                                  "has regressed and this verdict is checking nothing")]
    deriv = _derivations()
    lines = text.splitlines()
    total = len(MODEL.components(text))

    # Spans this verdict must not read: a generator writes its own numbers, and a HISTORICAL block
    # states a SUPERSEDED one on purpose -- "only 8 of 45 components deviated" is the argument for
    # why the zones were replaced, and it is in the past tense precisely so it can say 45.
    skip, start, hstart = [], None, None
    for i, ln in enumerate(lines, 1):
        if "BEGIN GENERATED" in ln:
            start = i
        elif "END GENERATED" in ln and start is not None:
            skip.append((start, i))
            start = None
        if "<!-- HISTORICAL -->" in ln:
            hstart = i
        elif "END HISTORICAL" in ln and hstart is not None:
            skip.append((hstart, i))
            hstart = None

    def subject_above(line):
        """The entrypoint a `###` heading is ABOUT: the first component it backticks, never a name
        that merely appears in its English."""
        for j in range(line - 1, 0, -1):
            h = deriv._H3.match(lines[j - 1])
            if h:
                m = _SUBJECT.search(h.group(1))
                return m.group(1) if m and m.group(1) in sizes else None
        return None

    findings = []
    for i, ln in enumerate(lines, 1):
        if any(a <= i <= b for a, b in skip):
            continue
        flat = " ".join(ln.split())

        # THE ROW FORM: `| \`client_agent\` | **19 of 41** | ... |`
        row = _CLOSURE_ROW.match(ln)
        if row:
            named = [e for e in _SUBJECT.findall(row.group(1)) if e in sizes]
            n, m = int(row.group(2)), int(row.group(3))
            for e in named:
                if (n, m) != (sizes[e], total):
                    findings.append(("CLOSURE_DRIFT",
                                     "line %d is `%s`'s row and reads %d of %d; its closure is %d of "
                                     "%d" % (i, e, n, m, sizes[e], total)))
            if named:
                continue

        for mt in _CLOSURE_CLAIM.finditer(ln):
            if MODAL_ZONE_PHRASE in flat:
                continue                   # owned by check_modal_zone
            n = int(mt.group(1))
            owner = subject_above(i)
            if owner and n != sizes[owner]:
                findings.append(("CLOSURE_DRIFT",
                                 "line %d sits under `%s`'s heading and says it links %d components; "
                                 "its closure is %d" % (i, owner, n, sizes[owner])))
            elif not owner and n not in set(sizes.values()):
                findings.append(("CLOSURE_DRIFT",
                                 "line %d claims a composition of %d components, which is no "
                                 "ENTRYPOINT's closure (%s) -- if it is not a closure count, it "
                                 "needs a verdict that owns it, not an exemption: ...%s..."
                                 % (i, n, ", ".join("%s=%d" % kv for kv in sorted(sizes.items())),
                                    flat[:90])))
    return findings


# A SUBTRACTION CLAIM: "`client_agent` is exactly `client_headless` minus five grants -- ...".
#
# This is the densest sentence shape in the document. It does not describe a component, it asserts a
# RELATIONSHIP between two compositions, and one sentence of it encodes what would otherwise be two
# closure listings. That is why it was worth writing and why it must be checked: it said "minus
# `windowing`, `frontend` and `transcript` ... three grants on an entrypoint" while the real grant
# difference was FIVE -- it omitted `colocation` and `starmap_authority`, which are the two that
# carry the design's actual point. Undercounting there is not a typo; it deletes the argument.
#
# THE SECOND NUMBER IS THE INTERESTING ONE. Five grants remove NINE components, because `universe`,
# `world`, `worldgen` and `transport_local` were reachable only through the authority. A reader who
# checks the grant count and stops never sees that, so both are checked and both are derived.
#
# EXPECTED_SUBTRACTIONS exists because absence is the failure mode a keyed check cannot see on its
# own: reword the sentence and this verdict goes quiet while reporting OK, which is exactly how
# LOCAL_COUNT entries outlived their sentences. If the count of matched claims falls, say so.
EXPECTED_SUBTRACTIONS = 1
_SUBTRACTION = re.compile(
    r'`(\w+)` is exactly `(\w+)` minus ([a-z]+|\d+) grants? — ((?:[^.]*?`\w+`)+[^.]*?)\.', re.S)
_PAIRED = re.compile(r'\*\*([A-Za-z]+|\d+) grants?, ([a-z]+|\d+) components?\.\*\*')


def check_subtraction(text, comp, grants):
    """`X` is exactly `Y` minus N grants -- verified against the grant table and the closures."""
    flat = " ".join(text.split())
    sizes = _closures(text)
    findings, seen = [], 0
    for m in _SUBTRACTION.finditer(flat):
        seen += 1
        x, y, n, listed = m.group(1), m.group(2), _as_int(m.group(3)), set(_SUBJECT.findall(m.group(4)))
        if x not in grants or y not in grants:
            findings.append(("SUBTRACTION",
                             "`%s` minus `%s`: one of them has no grant row" % (x, y)))
            continue
        real = grants[y] - grants[x]
        if listed != real:
            findings.append(("SUBTRACTION",
                             "prose says `%s` is `%s` minus %s; the grant table says %s"
                             % (x, y, ", ".join("`%s`" % g for g in sorted(listed)) or "nothing",
                                ", ".join("`%s`" % g for g in sorted(real)) or "nothing")))
        if n != len(real):
            findings.append(("SUBTRACTION",
                             "prose counts %d grants between `%s` and `%s`; the grant table has %d"
                             % (n, x, y, len(real))))
        # The paired "N grants, M components" restatement, when the sentence carries one.
        p = _PAIRED.search(flat[m.end():m.end() + 400])
        if p and x in sizes and y in sizes:
            drop = {c for c in set(_closure_members(text, y)) - set(_closure_members(text, x))
                    if comp[c]["kind"] != "ENTRYPOINT"}
            want_g, want_c = len(real), len(drop)
            got_g, got_c = _as_int(p.group(1)), _as_int(p.group(2))
            if (got_g, got_c) != (want_g, want_c):
                findings.append(("SUBTRACTION",
                                 "prose says %d grants, %d components between `%s` and `%s`; the "
                                 "register says %d grants, %d components"
                                 % (got_g, got_c, x, y, want_g, want_c)))
    if seen < EXPECTED_SUBTRACTIONS:
        findings.append(("SUBTRACTION",
                         "matched %d subtraction claim(s), expected %d -- a reworded claim leaves "
                         "this verdict quiet while reporting OK, which is the failure it exists for"
                         % (seen, EXPECTED_SUBTRACTIONS)))
    return findings


def _closure_members(text, entry):
    spec = importlib.util.spec_from_file_location("composition_graphs",
                                                  str(REPO / "scripts" / "composition-graphs.py"))
    cg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cg)
    comp, grants = cg.parse(text)
    return cg.closure(entry, comp, grants)


def _modal_deviation(comp):
    by_kind = {}
    for v in comp.values():
        by_kind.setdefault(v["kind"], []).append(v["zone"])
    total = 0
    for zones in by_kind.values():
        modal = max(set(zones), key=zones.count)
        total += sum(1 for z in zones if z != modal)
    return total


def check_modal_zone(text, comp):
    """The KIND-modal-ZONE deviation, measured rather than asserted."""
    want = _modal_deviation(comp)
    flat = " ".join(text.split())
    hits = re.findall(r'(\d+) of (\d+) %s' % re.escape(MODAL_ZONE_PHRASE), flat)
    if not hits:
        return [("MODAL_ZONE",
                 "no sentence states the KIND-modal-ZONE deviation, but this verdict exists to check "
                 "one -- either the claim was deleted and this check should go with it, or it was "
                 "reworded out of the gate's reach, which is the failure mode it was written for")]
    return [("MODAL_ZONE",
             "prose says %s of %s components deviate from their KIND's modal ZONE; the register "
             "measures %d of %d" % (n, m, want, len(comp)))
            for n, m in hits if (int(n), int(m)) != (want, len(comp))]


def _zone_spread(comp):
    """-> {kind: Counter(zone)}, and the kind whose modal zone covers the smallest share of it."""
    by_kind = {}
    for v in comp.values():
        by_kind.setdefault(v["kind"], collections.Counter())[v["zone"]] += 1
    weakest = min(by_kind, key=lambda k: max(by_kind[k].values()) / sum(by_kind[k].values()))
    return by_kind, weakest


def check_backend_spread(text, comp):
    """The KIND whose ZONE is least predicted by it, and the split, measured rather than asserted."""
    by_kind, weakest = _zone_spread(comp)
    m = BACKEND_SPREAD.search(" ".join(text.split()))
    if not m:
        return [("ZONE_SPREAD",
                 "no sentence states which KIND predicts ZONE least, but this verdict exists to "
                 "check one -- either the claim was deleted and this check should go with it, or it "
                 "was reworded out of the gate's reach, which is how the last one went wrong")]
    n1, tot, z1, n2, n3, z3, n4, z4 = m.groups()
    got = by_kind["BACKEND"]
    want = [(int(n1), z1), (int(n3), z3), (int(n4), z4)]
    bad = [(k, v) for v, k in want if got[k] != v]
    out = []
    if bad or int(tot) != sum(got.values()) or int(n2) != sum(got.values()) - int(n1):
        out.append(("ZONE_SPREAD",
                    "prose splits BACKEND %s of %s in %s, %s in %s and %s in %s; the register "
                    "measures %s" % (n1, tot, z1, n3, z3, n4, z4, dict(sorted(got.items())))))
    if weakest != "BACKEND":
        out.append(("ZONE_SPREAD",
                    "prose names BACKEND as the KIND whose modal ZONE covers least of it; the "
                    "register makes that %s" % weakest))
    return out


# Section 16's R4 and the hard-constraint blockquote above it both say N of M components are not yet
# their own directory and both name `grant_sweep` as the source. Neither was read by it: they said
# "27 of the 41" while the register held 54 and the sweep reported 38 UNVERIFIABLE. Both phrasings are
# matched, because correcting one and not the other is how the pair got out of step in the first place.
UNVERIFIABLE_PHRASES = (
    re.compile(r'\*\*(\d+) of the (\d+) components are not yet their own directory\*\*'),
    re.compile(r'its own directory\.\*\* (\d+) of the (\d+) are not yet'),
)


def check_unverifiable(text, comp):
    """How many components no instrument can check, measured by the instrument the prose names.

    Measured over the REGISTER, not over the grant table. `core` and `base` grant nothing so they have
    no grant row, and both are their own directory today -- counting them out of the denominator would
    make the claim read 38 of 52 and put the prose one edit away from being wrong in the other
    direction."""
    spec = importlib.util.spec_from_file_location("grant_sweep", str(REPO / "scripts" / "grant-sweep.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    _, _, _, files = gs.scan()
    want = len(gs.unverifiable_components(comp, files))
    flat = " ".join(text.split())
    out, seen = [], 0
    for pat in UNVERIFIABLE_PHRASES:
        for n_, m_ in pat.findall(flat):
            seen += 1
            if (int(n_), int(m_)) != (want, len(comp)):
                out.append(("UNVERIFIABLE_COUNT",
                            "prose says %s of %s components are not yet their own directory; "
                            "`grant_sweep` reports %d of %d" % (n_, m_, want, len(comp))))
    if not seen:
        out.append(("UNVERIFIABLE_COUNT",
                    "no sentence states how many components are not yet their own directory, but "
                    "R4's whole argument is that this number and the UNVERIFIABLE count are one "
                    "number -- either the claim went, and this check goes with it, or it was "
                    "reworded out of reach"))
    return out


def _absent_decision(text):
    """-> a decision tag the document certainly does not define, derived from the ones it does.

    The self-test needs a reference that DANGLES. Hard-coding one works until that decision is
    ratified, which is how this drive went silent on the day D14 landed."""
    used = [int(m) for m in re.findall(r'\| \*\*D(\d+)\*\*', text)]
    return "D%d" % ((max(used) if used else 0) + 1)


def check_dead_kinds(text):
    """Uses of a retired KIND, ratcheting toward zero."""
    body = re.sub(r'<!-- HISTORICAL -->.*?<!-- END HISTORICAL -->', "", text, flags=re.S)
    n = sum(len(re.findall(r'\b%s\b' % k, body)) for k in DEAD_KINDS)
    if n > DEAD_KIND_CEILING:
        return [("DEAD_KIND",
                 "%d uses of a retired KIND (%s), above the ceiling of %d. The kind was split into "
                 "INTERFACE and VOCABULARY; a rule stated over the retired word can be true of one "
                 "and false of the other" % (n, ", ".join(DEAD_KINDS), DEAD_KIND_CEILING))]
    return []


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
    # HTML COMMENTS ARE MACHINERY, NOT PROSE. Naming a generated block `tree-map#zones` put the word
    # "zones" within the ±60 window of the sentence "SEAM is gone, and no `boundary/` directory
    # replaces it" -- and STALE_ZONE fired on a marker. A gate reading its own scaffolding as a claim
    # is noise of the kind that gets a verdict suppressed, and suppression is how these go blind.
    flat = " ".join(re.sub(r'<!--.*?-->', " ", live, flags=re.S).split())
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
    findings.extend(check_closures(text))
    findings.extend(check_modal_zone(text, comp))
    findings.extend(check_subtraction(text, comp, grants))
    findings.extend(check_backend_spread(text, comp))
    findings.extend(check_unverifiable(text, comp))
    findings.extend(check_dead_kinds(text))
    findings.extend(check_no_dates(text))
    findings.extend(check_contract_grants(comp, grants))
    findings.extend(check_law_of_one(comp))
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
    # DERIVED, NOT PASTED -- this drive named D14 as its obviously-absent decision, and went SILENT
    # the day D14 was ratified. Exactly the failure the MODAL_ZONE comment above describes, in a
    # different verdict: a drive keyed on a literal stops testing the moment the literal becomes real.
    # `_absent_decision()` reads the register and returns one past the highest, so it cannot collide.
    ("DANGLING_D",  "This follows directly from {ABSENT_D}."),
    ("DANGLING_SECTION", "The rule is stated in full in Section 27."),
    ("UNREGISTERED_GATE", "The closure is gated by `composition_freshness` on every push."),
    ("MISSING_SCRIPT", "The figures come from `scripts/nonexistent-measure.py`."),
    ("BARE_GOAL", "| **serves** | **N3** — it composes, which is all anyone needs to know. |"),
    ("DANGLING_CLAUSE", "| **serves** | **N1.e** — a fifth clause N1 does not have. |"),
    # All three tiers of CLOSURE_DRIFT, because they are three different code paths and the row form
    # exists precisely because the prose form could not see the defect it was written for.
    ("CLOSURE_DRIFT", "The composition links 77 of 49 components."),
    ("CLOSURE_DRIFT", "| `client_agent` | **99 of 49** | an injected row |"),
    ("CLOSURE_DRIFT", "### `server` — injected\n\nIt links 3 of 49 components."),
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
        # {ABSENT_D} is resolved HERE, against the document in hand, so the drive cannot go silent
        # the day its literal becomes real -- which is exactly how it failed when D14 was ratified.
        injection = injection.replace("{ABSENT_D}", _absent_decision(text))
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

    # MODAL_ZONE and SUBTRACTION read ONE sentence each, so appending prose cannot exercise them:
    # the sentence they check is already in the document and already correct. Both are driven by
    # mutating that sentence, and both are driven in the ABSENCE direction too -- because the way a
    # keyed check dies is not by reporting the wrong answer, it is by matching nothing and staying
    # quiet. That is the failure LOCAL_COUNT's dead entries had, and it is the one worth proving.
    grants_now = MODEL.grants(text)
    drives = (
        # DERIVED, NOT PASTED. This drive read `text.replace("15 of 49 ...")` and went SILENT the
        # moment the register grew to 52: the literal no longer existed, the replace was a no-op, and
        # the verdict was handed an unmodified document. A self-test keyed on the numbers it is
        # meant to mutate stops testing exactly when the numbers move, which is the only time it
        # matters. The phrase is stable; the digits in front of it are not, so match them.
        ("MODAL_ZONE", "a wrong deviation count",
         lambda: check_modal_zone(
             re.sub(r'\d+( of \d+ %s)' % re.escape(MODAL_ZONE_PHRASE), r'999\1', text), comp)),
        ("MODAL_ZONE", "the claim reworded out of reach",
         lambda: check_modal_zone(text.replace(MODAL_ZONE_PHRASE, "components sit oddly"), comp)),
        # Same discipline as MODAL_ZONE: mutate the digits IN FRONT of a stable phrase, never a
        # literal, so the drive keeps testing after the numbers move.
        ("ZONE_SPREAD", "a wrong BACKEND zone split",
         lambda: check_backend_spread(
             re.sub(r'(BACKEND is where KIND predicts ZONE least: \*\*)\d+', r'\g<1>999', text), comp)),
        ("ZONE_SPREAD", "the split reworded out of reach",
         lambda: check_backend_spread(
             text.replace("BACKEND is where KIND predicts ZONE least", "BACKEND is spread about"), comp)),
        ("UNVERIFIABLE_COUNT", "a wrong count of components that are not yet directories",
         lambda: check_unverifiable(
             re.sub(r'\*\*\d+( of the \d+ components are not yet their own directory)',
                    r'**999\1', text), comp)),
        ("UNVERIFIABLE_COUNT", "the count reworded out of reach",
         lambda: check_unverifiable(
             text.replace("components are not yet their own directory", "components are unbuilt")
                 .replace("its own directory.** ", "its own directory.** roughly "), comp)),
        ("SUBTRACTION", "a wrong grant count",
         lambda: check_subtraction(text.replace("minus five grants", "minus three grants"),
                                   comp, grants_now)),
        ("SUBTRACTION", "a grant omitted from the list",
         lambda: check_subtraction(text.replace("`colocation` and `starmap_authority`",
                                                "and `starmap_authority`"), comp, grants_now)),
        ("SUBTRACTION", "the claim reworded out of reach",
         lambda: check_subtraction(text.replace("is exactly `client_headless` minus",
                                                "is roughly `client_headless` less"),
                                   comp, grants_now)),
    )
    for label, what, drive in drives:
        if not drive():
            bad += 1
            print("  %-12s SILENT -- %s was not reported" % (label, what))
        else:
            print("  %-12s %-6s %s" % (label, "FIRES", what))

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

    # DEAD_KIND is a ceiling, so it can only be exercised by pushing the count UP -- appending two
    # more uses of the retired word. A ratchet that has never been seen to fail is a number, not a
    # gate.
    if check_dead_kinds(text + "\n\nA CONTRACT is a CONTRACT.\n"):
        print("  %-12s %-6s %s" % ("DEAD_KIND", "FIRES", "two more uses of the retired kind"))
    else:
        bad += 1
        print("  DEAD_KIND    SILENT -- the retired-kind count rose and was not reported")

    # CONTRACT_GRANT reads the register, not the prose, so it is driven by mutating the grant map:
    # a VOCABULARY given a LIBRARY grant, which is the coupling the rule exists to forbid.
    comp_g, grants_g = MODEL.components(text), MODEL.grants(text)
    injected = dict(grants_g)
    injected["scene"] = set(grants_g.get("scene", set())) | {"world"}
    if check_contract_grants(comp_g, injected):
        print("  %-12s %-6s %s" % ("CONTRACT_GRANT", "FIRES", "a VOCABULARY granted a LIBRARY"))
    else:
        bad += 1
        print("  CONTRACT_GRANT SILENT -- a contract granting a LIBRARY was not reported")

    # DATED_CLAIM is positional -- it needs a date inside a section that is not the exempt one -- so
    # it is driven by injecting one under Section 2 rather than appending at the end of the file.
    if check_no_dates(text.replace("## 2. Axioms", "## 2. Axioms\n\nDecided on 2026-08-02.\n", 1)):
        print("  %-12s %-6s %s" % ("DATED_CLAIM", "FIRES", "a date outside the delta section"))
    else:
        bad += 1
        print("  DATED_CLAIM  SILENT -- a date in a load-bearing section was not reported")

    # LAW_OF_ONE reads the register, so it is driven by mutating a duty -- giving a component with a
    # clean single duty an "and", which is the shape the four rewrites removed.
    comp_l = MODEL.components(text)
    dirty = {k: dict(v) for k, v in comp_l.items()}
    dirty["core"] = dict(dirty["core"], duty="the substrate")          # declared -> now clean
    dirty["gpu"] = dict(dirty["gpu"], duty="the GPU contract and its atlas")
    if check_law_of_one(dirty):
        print("  %-12s %-6s %s" % ("LAW_OF_ONE", "FIRES", "a clean duty given a second job"))
    else:
        bad += 1
        print("  LAW_OF_ONE   SILENT -- a duty joining two things was not reported")

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
