---
# Survey envelope — captures stakeholder intent BEFORE a design is committed.
# Scaffolded by survey-init.sh. Placeholders in <angle-brackets> are unfilled;
# validate-envelope.sh rejects any pick/required-field still left as <...>.
survey-title: TSSA Purpose
work-item: #204-tssa-purpose
methodology-source: mission-kit skills/survey (2-round, 3-orthogonal-questions-per-round pick-list)
lifecycle-handoff:
  from: intent-open
  to: intent-captured
  authority-ref: "Director, 2026-08-02: 'You have what is written in our TSSA and our conversational history, as a basis upon which to run a survey on Purpose. Proceed'"
  planning-input-ref: self
stakeholder-picks:
  round-1:
    Q1: bd
    Q1-rationale: "reasoning instrument + target, PLUS a volunteered role not on the list: 'Ensures that a detailed design spec conforms to a new architecture and doesnt drift from the intent'. Explicitly did NOT pick (a) specification or (c) contract-over-code."
    Q2: abcd
    Q2-rationale: "all four readers, rejecting the premise that they conflict, PLUS the real primary: 'Agentic technical designers will build detailed implementation plans from this TSSA'"
    Q3: ab
    Q3-rationale: "finished once AND living — contradictory as posed. Volunteered: 'It does exist to produce a migration plan, but it's job is not finished once the delta is written and plan scheduled'"
  round-2:
    Q4: bd
    Q4-rationale: "review pass + citation. Declined (a) a blocking gate and (c) generation-from-registers — so A2 is adopted in traceability form, not generation form"
    Q5: abd
    Q5-rationale: "whole-document + section-by-section + nothing-freezes. Declined only (c) freeze-model-float-prose, which makes the prose first-class. A8 reconciles the three."
    Q6: ac
    Q6-rationale: "exhaustive completeness + machine-parseable structure. Both make the document longer and more tabular. Declined (b) anti-goals and (d) confidence marking."
# classification is OPTIONAL. Delete this key if your project has no work-item
# taxonomy. If you keep it and set SURVEY_CLASSES (env) or --classes, the value
# must be one of that pipe-separated set; otherwise any non-placeholder string passes.
classification: spec-purpose
outcome-axis:
  # OA-1 buildability | OA-2 agent-legibility | OA-3 enforceability
  # OA-4 director-leverage | OA-5 durability | OA-6 north-star-fidelity
  primary: [OA-2-agent-legibility, OA-3-enforceability]
  secondary: [OA-1-buildability, OA-4-director-leverage, OA-5-durability, OA-6-north-star-fidelity]
  round-1:
    primary: [OA-2-agent-legibility, OA-3-enforceability, OA-6-north-star-fidelity]
    secondary: [OA-1-buildability, OA-4-director-leverage, OA-5-durability]
  round-2:
    primary: [OA-1-buildability, OA-2-agent-legibility, OA-3-enforceability]
    secondary: [OA-5-durability, OA-6-north-star-fidelity]
axiom-principle-anchors:
  primary: [A8-gated-recursive-integrity, A4-zero-loss-knowledge, A2-isomorphic-specification-traceability-form]
  secondary: [A13-director-intent-amplification, A0-sovereign-intelligence-engine, A3-sovereign-composition]
  round-1: [A8-gated-ascension, A2-scoped-to-design-layer, A4-documentation-as-collective-RAM, A13-director-intent-amplification]
  round-2: [A8-law-of-fallback, A8-binary-certification-in-tension, A4-expansionist-bias, A4-anti-prose-constraint, A2-traceability-form]
anti-goals-count: 6
flags-count: 7
calibration-data:
  stakeholder-time-cost-minutes: 4
  comparison-baseline: "none — first survey in this project; prior process was proposer-inferred intent, which failed twice this session (unfalsifiable §0b audience claims; a TOC treated as approved when only its sub-decisions were)"
  notes: "Highest-value output was the volunteered free text on all three Round-1 questions, each of which corrected the question rather than answering it. Always leave the option set escapable. Separately: Q5's three-way 'contradictory' pick was not contradictory — A8 already reconciled it, so the contradiction was an artifact of the proposer's question, not the stakeholder's model."
contradictory-constraints:
  - round: 1
    questions: [Q2]
    picks: [a, b, c, d]
    constraint-envelope: "All four readers at once; the question's premise (that readers conflict and one wins) is rejected. Satisfiable under A4 Expansionist Bias — serve every reader by expanding, never by trading one off. No omission may be justified by naming the reader it was omitted for."
  - round: 1
    questions: [Q3]
    picks: [a, b]
    constraint-envelope: "Finished once AND living. Resolved by volunteered text: authoring finishes, authority does not. A ratified end state as an act of writing; continued governance after the delta is written and the migration scheduled."
  - round: 2
    questions: [Q5]
    picks: [a, b, d]
    constraint-envelope: "Whole-document ratification + section-by-section sealing + nothing permanently frozen. NOT actually contradictory: A8 Gated Ascension makes sealing mean safe-to-build-on rather than immutable, and the Law of Fallback mandates re-opening a lower layer on failure above. Sealed = certified, not frozen."
---

# TSSA Purpose — Survey envelope

**Methodology:** mission-kit `skills/survey` (2-round, 3-orthogonal-questions-per-round pick-list)
**Work item:** #204-tssa-purpose
**Classification candidate:** spec-purpose
**Lifecycle handoff:** `intent-open -> intent-captured` only; this envelope grants no design, seed, implementation, or delivery effect.

---

## §0 Context

**Source work-item text** (provided at survey init):

> **Work item: the purpose of the Target State System Architecture (TSSA).**

The TSSA (`docs/superpowers/specs/2026-08-01-target-state-system-architecture.md`, 4,583 lines,
41 components, 17 gates, task #204, branch `integration`) describes OpenStarbound as it is in a
perfect future — present tense, no migration, decisions justified against a north star.

**The document has never had its purpose stated from first principles.** It began as task #204,
"headless client design" — a ticket. Decisions D1/D2 rescoped it to a whole-system target state.
The purpose was back-formed from that scope expansion, and §0b "Obligations" was a retrofit
attempt whose claims a 76-agent adversarial audit found unfalsifiable — the signature of a
document inferring its own reason to exist.

**What is already settled by the Director:**

- North star: N1 a modern distributed Starbound; N2 a sovereign, comprehensible engine;
  N3 aggregate functionality comes from composition (client and server are witnesses, not the taxonomy).
- Tense: a point in time in a perfect future. Never "getting there".
- History is admissible as *warrant* ("this coupling caused the same bug four times") but never
  as *anchor* ("rendering is granted game today").
- `mission-kit/axioms` is canonical. Both the architecture (the engine) and the architect
  (this document and how it is made) are `any-system`, so A3, A4, A8, A9, A14 and the A0
  umbrella are in force.
- Diagrams remain — "a key contextual reasoning aid for me and other agents".
- Approval is aggregate only.

**What is not settled, and forks the whole document:**

Whether **A2 Isomorphic Specification** is in force — *"The specification IS the system. The
manifest is the master — no state changes through imperative drift; declared intent
auto-reconciles the running system."*

| | A2 off — a design document | A2 on — the manifest |
|---|---|---|
| the registers | describe an intended model | *are* the model; code conforms or is in drift |
| the 17 gates | quality checks on prose | the reconciliation mechanism |
| §10 "Delta from today" | a migration plan someone writes once | a continuously-generated drift report |
| lifespan | ratified, then archived | never done; dies only with the engine |

Two of the Director's four outstanding starred decisions ("who writes the delta", and the
register's Consequence column) dissolve or change shape depending on this answer.

**Secondary unresolved dimension:** who reads this. The Director has said diagrams serve
"me and other agents", which puts agents among the primary readers and makes A4's
*"an actor loaded today must reach context identical to the author who designed the thing"*
the operative constraint — but this has never been stated, and the document has been
optimized for human skimmability and agent cold-loading simultaneously without saying so.

**Provenance.** Work item `#204-tssa-purpose`, branch `integration`. The survey was commissioned by
the Director on 2026-08-02 after the proposer argued that *structure* no longer needed stakeholder
input — it is derivable from the newly-canonical axiom set (A3 Law of One over sections, A4
Expansionist Bias, A8 Gated Ascension, plus the approved AXIOMS → NORTH STAR → PRINCIPLES → DECISIONS →
MODEL spine) — while *purpose* had never been stated from first principles and was the load-bearing
unknown. The Director's instruction: *"You have what is written in our TSSA and our conversational
history, as a basis upon which to run a survey on Purpose. Proceed."*

**Methodology anchor.** mission-kit `skills/survey` (2-round, 3-orthogonal-questions-per-round
pick-list), run against six consumer-supplied outcome axes: **OA-1 buildability**, **OA-2
agent-legibility**, **OA-3 enforceability**, **OA-4 director-leverage**, **OA-5 durability**, **OA-6
north-star-fidelity**. Axiom anchoring draws on mission-kit `axioms/` (A0–A14), declared canonical by
the Director on 2026-08-02, with both the architecture (the engine) and the architect (this document
and how it is made) in scope as `any-system`. Related: the 76-agent adversarial audit `wca60o174`
(40 confirmed findings) closed immediately before this survey ran, and three of its finding clusters
are addressed by the envelope's Round-2 reading.

---

## §1 Round 1 picks

| Q | Pick | Intent reading (1-line summary) |
|---|---|---|
| Q1 — Role | **b** reasoning instrument + **d** target + *volunteered:* "ensures a detailed design spec conforms to a new architecture and doesn't drift from the intent" | Not a spec of code, not a contract over code — a **conformance authority over the design layer beneath it** |
| Q2 — Reader | **a+b+c+d** all four + *volunteered:* "agentic technical designers will build detailed implementation plans from this TSSA" | Rejects the premise that readers conflict; names a **producer**, not a passive reader, as the real primary |
| Q3 — Lifespan | **a** finished once + **b** living + *volunteered:* "it does exist to produce a migration plan, but its job is not finished once the delta is written and plan scheduled" | **Authoring finishes; authority does not.** Explicitly rejects the terminal clause of (d) |

### §1.Q1 — Per-question interpretation

The Director declined **(a) a specification** and declined **(c) a contract** *as posed* — and then
volunteered a role that is neither of my four: *"ensures that a detailed design spec conforms to a new
architecture and doesn't drift from the intent."* That is **A2 Isomorphic Specification in force, but
one layer above where the question placed it.** I framed A2 as *code* conforming to the TSSA; the
answer is that *detailed design specs* conform to the TSSA. The manifest is master over the artifact
directly beneath it in the onion, not over source three layers down.

This dissolves the A2-on / A2-off binary the work item posed. A2 holds — the TSSA is a master that
things are reconciled against — but its reconciliation target is the **design layer**, which means the
17 existing gates do *not* become code-conformance instruments, and the missing instrument is one that
checks a *design spec* against the TSSA. No such instrument exists today. Correspondingly, "who writes
the delta" does not dissolve as I predicted; it re-forms as **"what checks a design against the
target."** Axes: OA-3 Enforceability (primary), OA-6 North-star fidelity (primary), OA-5 Durability
(secondary).

### §1.Q2 — Per-question interpretation

All four readers were picked, which rejects the question's premise — that the readers conflict and one
must win. Under **A4's Expansionist Bias** that is coherent rather than evasive: you do not trade
readers off against each other, you expand until every one is served. The volunteered answer then
names the actual primary consumer, and the significant thing about it is that it is a **producer, not
a reader**: *agentic technical designers who will build detailed implementation plans from the TSSA.*

That changes the completeness bar in a specific and demanding way. A human reader who hits a gap
notices the gap and asks; **an agent that lacks context does not know it lacks context — it infers,
and the inference is plausible.** This makes A4's *"an actor loaded today must reach context identical
to the author who designed the thing"* the operative constraint, and makes **silent gaps the primary
defect class** — which is precisely what the 76-agent audit found: a fabricated warrant that read as
authoritative, and five normative rules whose register fields do not exist. Both are defects an agent
would consume without noticing. Axes: OA-2 Agent-legibility (primary), OA-1 Buildability (secondary —
displaced upward, to plan level rather than code level), OA-4 Director-leverage (secondary).

### §1.Q3 — Per-question interpretation

**(a) and (b) are contradictory as posed** — "ratified then stable" against "never done" — and per the
methodology a contradictory multi-pick is signal, not error: it asks for a constraint envelope
satisfying both. Read together with the volunteered text, the resolution is clean and is not a
compromise: **authoring finishes; authority does not.** There is a real end state to *writing* the
TSSA — it can be ratified, and ratification means something — and after that it continues to govern,
including after the delta is written and the migration is scheduled.

That rules out two readings I had live. It is **not** auto-regenerated from the code (that would make
it a report of what is, when its whole tense commitment is to describe a perfect future). And it is
**not** consumed by the delta — the Director explicitly struck the terminal clause of option (d). The
document outlives its own migration plan, because the thing it governs (design conformance) recurs
every time a new design is written. Axes: OA-5 Durability (primary), OA-3 Enforceability (secondary).

**Round-1 composite read:** The TSSA is a **reasoning instrument and a fixed target that functions as
the ratified upper layer of an A8 Sovereign Onion** — its job is to keep the design layer beneath it
conformant and non-drifting, it is authored primarily for agentic designers who will produce
implementation plans from it, and it is finished as an act of authorship while permanent as an
authority. **The tension carried to Round 2:** the Director wants conformance-and-no-drift but
declined "a contract", and *nothing today checks a design against the TSSA* — so Round 2 must ask what
enforces it, what "finished" actually seals, and what an agentic designer is owed that a human is not.

**Round-1 axiom / principle anchoring:** The round is anchored by **A8 Gated Ascension** — the TSSA is
layer N and detailed designs are layer N+1, which "cannot bear weight" until layer N is sealed, which
is exactly the Director's conformance clause — and by **A2 scoped to the design layer** rather than to
source. **A4 Documentation as Collective RAM** becomes load-bearing rather than aspirational once the
primary consumer is an agent that cannot detect its own missing context, and **A13 Director Intent
Amplification** is named almost verbatim in "doesn't drift from *the intent*": the document's function
is to carry the Director's intent across a layer boundary without attenuation.

---

## §2 Round 2 picks

| Q | Pick | Round-1 aggregate relation | Intent reading (1-line summary) |
|---|---|---|---|
| Q4 — Conformance mechanism | **b** a review pass + **d** by citation | disambiguates | Traceability plus judgement. Declined **(a)** a blocking gate and **(c)** generation-from-registers |
| Q5 — What ratification seals | **a** whole document + **b** section by section + **d** nothing freezes | disambiguates | Three-way contradictory pick. Declined only **(c)** freeze-model-float-prose |
| Q6 — What an agent is owed | **c** machine-parseable structure + **a** exhaustive completeness | deepens | Longer and more tabular, not shorter. Declined **(b)** anti-goals and **(d)** confidence marking |

### §2.Q4 — Per-question interpretation

This **disambiguates** the Round-1 tension: the Director wanted conformance-and-no-drift while
declining "a contract", and Q4 asked what actually enforces it. The answer pairs a **mechanical floor**
with a **judgement layer**: every design decision cites the TSSA element it derives from (cheap,
checkable, and already the pattern the `warrant` column established), and a review pass reads the
design against the TSSA for the drift a parser cannot see — a design that satisfies every register and
still misses the point.

The two declines carry as much information as the picks. Declining **(c) by construction** rejects the
strongest form of A2: the TSSA is not a generator, and designs are authored rather than emitted.
Declining **(a) a gate** — which I framed as *blocking* acceptance — suggests conformance is a
discipline with a trail rather than a merge-blocker, though the boundary between "citation check" and
"gate" is thin enough that this is carried to the design phase as flag **F1**. Axes: OA-3
Enforceability (primary), OA-5 Durability (secondary).

### §2.Q5 — Per-question interpretation

Three picks that I posed as mutually exclusive — and **the contradiction is in my question, not in the
Director's model.** A8 already reconciles them. *Gated Ascension* is about not building on an
uncertified layer, not about never revising one; and A8's *Law of Fallback* explicitly **requires**
re-opening layer N-1 when a failure appears at layer N. So sections sealing individually **(b)**, a
whole-document ratification milestone **(a)**, and permanent revisability **(d)** are one coherent
model: **"sealed" means certified-and-safe-to-build-on, not frozen-forever.**

The single decline is the sharpest signal in the survey. **(c)** proposed freezing the model while
letting explanatory prose float, and rejecting it says **the prose is not second-class** — the artifact
is unitary, and you cannot ratify the registers and leave the text to drift. That is consistent with
A4 (prose is connective tissue *within* one bit-perfect artifact, not a soft outer layer) and with the
standing "diagrams remain, they are a key contextual reasoning aid" ruling. It also forecloses a
tempting shortcut: I cannot ratify the registers early and keep rewriting §4's prose behind them.
Axes: OA-5 Durability (primary), OA-6 North-star fidelity (secondary).

### §2.Q6 — Per-question interpretation

This **deepens** Round 1's finding that the primary consumer is an agentic *producer*. Both picks push
the same direction and both make the document **bigger**: exhaustive completeness means no gap an
agent could plausibly fill by inference, and machine-parseable structure means everything normative
lives in a register or table, with prose never load-bearing. Together they are A4's **Expansionist
Bias** and **Anti-Prose Constraint** taken literally, and they close the compression question for good
— the restructure is about order and findability, and the document grows.

They also settle an open Director decision by implication: the five audit findings where a normative
rule pointed at a register field that does not exist (observer, bound, timeout, validator, instrument
row) must be resolved by **adding the fields**, not by softening the rules — and the `consequence`
column I asked about is required, because a rule an instrument cannot read is not a rule. The two
declines are weaker signal than the picks but worth recording: declining **(b) explicit anti-goals**
is consistent with the tense commitment (a target-state document describes what *is* in the perfect
future; what is absent is simply not described, and rejection rationale lives in Decisions), and
declining **(d) confidence marking** is plausibly subsumed by (a) — if everything is decided, there is
nothing to mark. That reading collides with §0b's four **owed** figures and with the audit's central
defect class, and is carried as flag **F2**. Axes: OA-2 Agent-legibility (primary), OA-1 Buildability
(primary), OA-3 Enforceability (secondary).

**Round-2 composite read:** Round 2 sharpened rather than changed Round 1 — conformance is enforced by
a mechanical citation trail plus a judgement review rather than by generation or a blocking gate;
ratification is real, incremental and non-terminal, with the prose sealed alongside the model; and the
document owes an agentic designer exhaustive completeness in machine-parseable structure, which makes
it substantially longer and more tabular. The one genuine surprise was that Q5's apparent
contradiction dissolves under A8 rather than needing a compromise.

**Round-2 axiom / principle anchoring:** **A8 Gated Ascension + Law of Fallback** supply the model that
reconciles Q5's three picks, and **A8 Binary Certification** is the one axiom this round sits in
tension with — a review pass is inherently non-binary, so the pass/fail half of conformance must rest
on the citation check (flag **F3**). **A4 Expansionist Bias** and **Anti-Prose Constraint** are named
almost verbatim by Q6's two picks. **A2** is adopted in its *traceability* form (citation) rather than
its *generation* form, and **A13 Director Intent Amplification** is why a review pass survives beside
the mechanical check at all: no parser detects intent drift.

---

## §3 Composite intent envelope

**The TSSA is the ratified upper layer of a sovereign onion.** It is a reasoning instrument and a fixed
target whose operative job is to keep the design layer beneath it conformant to a new architecture and
non-drifting from the Director's intent. It is explicitly **not** a specification of code, **not** a
contract over source, and **not** a generator. Its primary consumer is an **agentic technical designer
producing detailed implementation plans from it** — a producer, not a reader — and every other reader
(the Director, a cold-loading agent, a future human, the gates) is served by *expansion* rather than by
trade-off, because A4 forbids buying one reader's clarity with another's context.

**Conformance is a two-part instrument.** A mechanical citation trail: every design decision names the
TSSA element it derives from, so a dangling or absent citation is detectable without judgement. Plus a
review pass: an agent reads a design against the target for the intent drift no parser can see — a
design that satisfies every register and still misses the point. Neither generation-from-registers nor
a blocking binary gate was chosen. **Ratification is real but not terminal:** sections seal
individually so downstream work can begin on certified ground, the whole reaches a ratification
milestone that means something, and any layer re-opens on a named trigger — with **the prose sealed
alongside the model**, never left floating behind a frozen register.

**The content obligations follow directly and all point the same way:** exhaustive completeness — no
gap an agentic designer could fill by plausible inference — expressed in machine-parseable structure,
with everything normative in a register or table and prose never load-bearing. The document therefore
gets **longer and more tabular**. Compression is off the table permanently. *Primary outcomes:* OA-2
Agent-legibility, OA-3 Enforceability. *Secondary:* OA-1 Buildability (at plan level, not code level),
OA-5 Durability, OA-6 North-star fidelity, OA-4 Director-leverage. *Four design constraints surfaced:*
(1) an instrument that reads a **design spec** against the TSSA does not exist and must be built;
(2) the registers gain the fields their rules require, including the `consequence` column, because a
rule no instrument can read is not a rule; (3) sections sealing individually makes **TOC ratification a
prerequisite**, since sealing order is a dependency order; (4) "exhaustive completeness" has no
stopping rule, so one must be defined or the "finished" state of Q5 is unreachable.

**Final axiom / principle anchoring:** The envelope is anchored primarily by **A8 Gated Recursive
Integrity** — the Sovereign Onion supplies the layer model, Gated Ascension supplies the conformance
requirement, and the Law of Fallback supplies the re-open trigger that makes "seals but never freezes"
coherent rather than contradictory. **A2 Isomorphic Specification is adopted in its traceability form**
(citation) rather than its generation form, and scoped one layer above where it was posed: master over
designs, not over source. **A4 Zero-Loss Knowledge** governs all three content decisions — Expansionist
Bias (the document grows), Anti-Prose Constraint (tables are the language), and Documentation as
Collective RAM (the agent must reach the author's context from the artifact alone). **A13 Director
Intent Amplification** is why a judgement review survives beside the mechanical check, and **A0**
describes the whole chain the TSSA sits in the middle of: the principal manipulates strategic what-if,
the TSSA carries that intent across a layer boundary without attenuation, and automated substrates own
the imperative how-to beneath it. The one live tension is **A8 Binary Certification** — "gates are
pass/fail only, no partial credit" — against a review pass that is inherently non-binary; the design
phase must rest the pass/fail half on the citation check and classify the review as a distinct,
non-gating instrument.

---

## §4 Scope summary

| Axis | Bound |
|---|---|
| Title | TSSA Purpose |
| Classification | spec-purpose |
| Location / scope | `docs/superpowers/specs/2026-08-01-target-state-system-architecture.md` (4,583 lines, 41 components, 17 gates), branch `integration`, task #204 |
| Primary outcome | The TSSA is the ratified upper layer of a sovereign onion: a reasoning instrument and fixed target that keeps the design layer beneath it conformant and non-drifting from Director intent |
| Secondary outcomes | Authored for agentic designers producing implementation plans; exhaustive and machine-parseable; seals incrementally without freezing; conformance by citation + review |
| Outcome-axis (primary) | OA-2 agent-legibility, OA-3 enforceability |
| Outcome-axis (secondary) | OA-1 buildability (plan level), OA-4 director-leverage, OA-5 durability, OA-6 north-star-fidelity |
| Outcome-axis (Round-1) | primary: OA-2 agent-legibility, OA-3 enforceability, OA-6 north-star-fidelity; secondary: OA-1 buildability, OA-4 director-leverage, OA-5 durability |
| Outcome-axis (Round-2) | primary: OA-1 buildability, OA-2 agent-legibility, OA-3 enforceability; secondary: OA-5 durability, OA-6 north-star-fidelity |
| Axiom/principle anchors | primary: A8 gated-recursive-integrity, A4 zero-loss-knowledge, A2 isomorphic-specification (traceability form); secondary: A13 director-intent-amplification, A0 sovereign-intelligence-engine, A3 sovereign-composition |
| Axiom/principle anchors (Round-1) | A8 gated-ascension, A2 scoped-to-design-layer, A4 collective-RAM, A13 director-intent-amplification |
| Axiom/principle anchors (Round-2) | A8 law-of-fallback, A8 binary-certification (in tension), A4 expansionist-bias, A4 anti-prose-constraint, A2 traceability-form |

---

## §5 Anti-goals (out-of-scope; deferred)

| AG | Description | Composes-with target |
|---|---|---|
| AG-1 | **The TSSA does not gate code.** Source conformance is not its job; it governs the design layer directly beneath it. Code conformance belongs to instruments the design layer owns. | The existing 17 gates stay scoped to the TSSA's own internal consistency; a future design-layer instrument owns design→code |
| AG-2 | **The TSSA is not generated from source.** Q4 declined "by construction". It describes a perfect future, so deriving it from what exists would invert its tense commitment. | `scripts/spec-model.py` stays a reader of the document, never a writer of it |
| AG-3 | **The TSSA is not consumed by the delta.** Q3 explicitly struck the terminal clause: its job continues after the migration plan is written and scheduled. | §10 Delta / task #208 — the delta is an output, not the document's terminus |
| AG-4 | **Compression and summarization are out of scope, permanently.** A4 Expansionist Bias plus Q6's completeness pick close this. The restructure is order and findability only. | TSSA-1 (#206) index work — re-order, never shrink |
| AG-5 | **Enumerated anti-goals do not belong inside the TSSA.** Q6 declined them: a target-state document describes what *is* in the perfect future; rejection rationale lives in Decisions, not in a "what we won't build" list. | §4 Decisions (chose / rejected / serves) carries rejection; TSSA-0 (#205) |
| AG-6 | **The multi-agent axioms (A5–A7, A10–A13) do not enter the TSSA body.** They govern the architect's method, not the architecture's content. | Method/process docs; this envelope cites A13 as anchoring, not as content |

---

## §6 Flags / open questions for the design phase

Open questions and risks surfaced during interpretation, each with a recommendation
to challenge during design review.

| # | Flag | Recommendation |
|---|---|---|
| F1 | **Is the citation check blocking or advisory?** Q4 picked "by citation" but declined "a gate", which I framed as *blocking acceptance*. The boundary between a citation check and a gate is thin. | Design phase should propose it as a **blocking** check (a dangling citation is binary and cheap to prove) and let the Director veto. A8 Binary Certification favours blocking. |
| F2 | **Q6 declined confidence marking, but §0b marks four figures "owed"** — and the audit's central defect class was rules that *read* as settled and were not. If everything must be decided, "owed" markers are a contradiction; if they stay, they are confidence marking under another name. | Treat **owed** as a temporary authoring state that must be zero at ratification, not a permanent document feature. That satisfies both the decline and the audit. |
| F3 | **A8 Binary Certification vs a review pass.** "Gates are pass/fail only, no partial credit" — a review pass is inherently judgement. | Rest the pass/fail half on the citation check; classify the review as a **non-gating instrument** whose output is a finding list, not a verdict. Record the classification explicitly so the review is never mistaken for certification. |
| F4 | **The design-conformance instrument does not exist.** Nothing in the repo reads a design spec against the TSSA. This is new engineering, in no current task. | Open a task under TSSA-3 (#208). It is the direct consequence of Q1 and cannot be deferred silently, or the conformance clause is decoration — the exact failure the audit found five times. |
| F5 | **"Exhaustive completeness" has no stopping rule.** Q5 wants a ratifiable "finished" state; Q6 wants no gap an agent could fill by inference. Without a completeness criterion, "finished" is unreachable and the document expands forever. | Define completeness as a **closure property over the registers** (every component has a duty/warrant/consequence, every seam a payload, every payload a validator, every handoff a bound/policy/observer, every edge a citation) so completeness is computable rather than felt. |
| F6 | **Q2 picked all four readers, but §0b currently states the audience wrongly.** The audit found its claims unfalsifiable. Serving all four is satisfiable under A4 expansion, but only if stated. | §0b must be rewritten to name the primary consumer (agentic technical designers producing implementation plans) and state that no reader is traded off — with the expansion obligation that follows. |
| F7 | **Section-by-section sealing makes TOC ratification a hard prerequisite**, because sealing order is a dependency order and a section cannot seal before the layer beneath it. | Ratify the TOC before any section-level uplift begins (TSSA-1, #206). This promotes the TOC from "blocking decision" to "structural precondition of the chosen model". |

---

## §7 Sequencing / cross-work considerations

### §7.1 Branch + review strategy

Branch `integration`, task **#204**. Seven commits currently unpushed. Approval remains **aggregate
only** — no section is approved until the whole can be reasoned with together — which composes with
Q5's model as follows: *sections seal individually for the purpose of ascent* (downstream work may
begin on a sealed section), while *Director approval remains a whole-document act*. These are not in
conflict: sealing is a technical certification, approval is an authority act.

### §7.2 Composability with concurrent / pending work

- **TSSA-0 (#205)** — the spine. Now additionally owes the F1–F6 axiom reconciliation: canonical axiom
  adoption, the domain-fact rename out of the `A` namespace, and the reduction of P1–P7 to four.
- **TSSA-1 (#206)** — the index. Reframed by this envelope: **order and findability only**, never
  compression (AG-4), and now a *precondition* of section sealing rather than a parallel activity (F7).
- **TSSA-2 (#207)** — section-by-section uplift. Directly implements Q5(b) Gated Ascension. Cannot
  begin before #206 ratifies.
- **TSSA-3 (#208)** — aggregate review + gate re-homing. Gains F4: the design-conformance instrument.
- **The 40 confirmed audit findings** — five of them (observer, bound, timeout, validator, instrument
  row) are resolved *by adding register fields*, not by softening rules, per Q6.

### §7.3 Compressed-timeline candidate?

**No.** Q6's exhaustive-completeness pick and Q5's section-sealing model both push toward more work,
not less, and the Director has stated an explicit preference to take time on this uplift. The one
genuine compression available is that **structure is now derivable** from the adopted axioms (A3 Law of
One over sections, A4 Expansionist Bias, A8 Gated Ascension) rather than requiring stakeholder input —
which is why this survey was run on purpose rather than on structure.

---

## §calibration — Calibration data point

Captures an empirical baseline for the methodology-evolution loop.

- **Stakeholder time-cost (minutes):** 4 (across both rounds; six pick-list questions, three answered
  with volunteered free-text additions)
- **Comparison baseline:** none — first survey run in this project. Prior process was the proposer
  inferring intent from conversational history, which had failed twice in this session: §0b's audience
  claims were unfalsifiable because they were inferred, and the 14-section TOC was treated as approved
  when only its sub-decisions had been.
- **Notes:** The instrument's highest-value output was **not** the picks — it was the **volunteered
  free text on all three Round-1 questions**, each of which corrected the question rather than
  answering it. Q1's addition relocated A2 from the code layer to the design layer, which no option
  offered. Q2's addition named a *producer* where all four options named readers. Q3's addition struck
  a clause from an option rather than picking one. **Recommendation for question design: always leave
  the option set escapable**, because the highest-information answer was consistently the one outside
  it. Second observation: Q5's three-way "contradictory" pick was not contradictory — the adopted axiom
  set (A8 Gated Ascension + Law of Fallback) already reconciled it, meaning **the contradiction was an
  artifact of the proposer's question, not the stakeholder's model**. Before flagging a multi-pick as a
  constraint envelope, check whether an adopted axiom already resolves it.

---

## §contradictory — Contradictory multi-pick carry-forward

| Round | Question(s) | Picks | Constraint envelope description |
|---|---|---|---|
| 1 | Q2 | a, b, c, d | All four readers at once. The question asked which reader wins *when they conflict*; the answer rejects the premise. Satisfiable under **A4 Expansionist Bias** — readers are served by expanding the document rather than by trading one against another. The design must never justify an omission by naming the reader it was omitted for. |
| 1 | Q3 | a, b | "Finished once" and "living" together. Resolved by the volunteered text: **authoring finishes, authority does not.** The document reaches a ratified end state as an act of writing, and continues to govern after the delta is written and the migration scheduled. |
| 2 | Q5 | a, b, d | Whole-document ratification + section-by-section sealing + nothing permanently frozen. **Not actually contradictory:** A8 supplies the reconciliation — Gated Ascension makes sealing about safe-to-build-on rather than immutable, and the Law of Fallback mandates re-opening a lower layer when a failure appears above it. "Sealed" = certified, not frozen. |

---

## §8 Cross-references

- **mission-kit `skills/survey`** — the survey methodology this followed
- **mission-kit `axioms/`** — canonical axiom set (A0–A14); the anchoring source for this envelope
- **#204-tssa-purpose** — source work item
- **`docs/superpowers/specs/2026-08-01-target-state-system-architecture.md`** — the document whose purpose this captures
- **#205 TSSA-0 / #206 TSSA-1 / #207 TSSA-2 / #208 TSSA-3** — the work this envelope is load-bearing input to
- **Audit `wca60o174`** — 76-agent adversarial review, 40 confirmed findings; five of its findings are resolved by this envelope's Q6 reading
- **`docs/superpowers/drafts/tssa-frame.md`** — the frame draft this envelope now supersedes on purpose and audience

---

— Proposer: Claude / 2026-08-02 (Survey envelope; 6 questions, 13 picks + 3 volunteered additions, ratified across 2 rounds)
