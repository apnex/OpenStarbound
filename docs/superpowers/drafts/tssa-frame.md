<!-- DRAFT. Not the specification. This is the frame -- axioms, north star, principles,
     decisions -- being written for the Target State System Architecture under the Director's
     2026-08-02 reframe: the document reads as a point in time in a perfect future, and history is
     cited only as WARRANT for why the target is better, never as an anchor placing a boundary by
     inertia. It lands in the spec once the index is agreed (task #206). Kept in the repo rather
     than a session scratchpad because the scratchpad was cleared once already today and took two
     artifacts with it. -->

# Target State System Architecture

**This document describes OpenStarbound as it is.** Not as it will be, not as it might be if the work
lands — as it *is*, in the target state. Everything here is written in the present tense of a system
that exists. Nothing here is a plan, an estimate, a migration or a progress report; where the past is
cited it appears as evidence for why a thing is the way it is, never as an instruction to change
anything.

---

## 0. What OpenStarbound is

Before any component: the system this architecture is an architecture *of*.

OpenStarbound simulates a **universe** — a star map of systems, each holding **worlds**. A world is a
terrain simulation with entities in it. Worlds are generated from a seed, ticked on their own clock,
and persisted.

### Three roles, and a fourth thing that is not a role

| | what it is | where it lives |
|---|---|---|
| **an authority** | owns the truth of a world or a universe, and answers to nobody about it | one per world, one per universe (A1) |
| **a participant** | holds a *view*, predicts against it, and asks an authority to change things | in a process that composed one |
| **a device** | a display, a speaker, a file, a recorder — outside the simulation entirely | attached to a participant, never to an authority |

**A player is not a participant.** A player is an **entity**, living in a world, owned by that world's
authority exactly like a monster or a door. A participant *drives* a player; it does not contain one,
and the player does not follow the participant home. Keeping these words apart is load-bearing: it is
the difference between "who is connected" and "what is in the world", and those two sets change
independently — a player entity outlives the participant's connection.

### A process composes roles; it does not have them by nature

The composition — not a class, not a build flag, not a fork — is what makes one binary a graphical
client and another a dedicated server. Measured from the grant closures:

| composition | authority | participant | devices |
|---|---|---|---|
| `client_opengl` · `client_sdl_gpu` | optional, embedded | **yes** | display, speaker |
| `client_headless` | optional, embedded | **yes** | a recorder |
| `client_agent` | **none** — it must connect to one | **yes** | **none** |
| `server` | **yes** — universe and worlds | **none** | **none** |
| `world_sim` | **yes** — one world | **none** | **none** |
| `world_gen` | **none** — it generates and never ticks | **none** | **none** |

**A server composes an authority and nothing else.** It has no participant, no agent, no player of
its own. There are player *entities* in its worlds — but every participant driving one is in another
process, usually on another machine. This is why `server` links neither `participant` nor either
`*_view` component, and why it never enters the `device/` zone at all.

Read the table the other way and N3 falls out of it: `client_agent` is a participant with no
authority and no devices, `world_sim` is an authority with no participant, and neither is a
cut-down version of a graphical client. They are different wirings of the same components.

**These six are examples, not an enumeration.** They are the aggregates that happen to have entry
points today, chosen because between them they exercise every seam. The architecture is the component
set and the rules about what may name what; the aggregates are what those rules permit, and the list
of permitted aggregates is not something this document closes.

Two further things are first-class rather than incidental, because they are what makes it *Starbound*
and not a physics demo:

- **Content.** Materials, items, species, dungeons, biomes. Data, not code. The engine loads it; the
  engine does not know what is in it.
- **The mod surface.** Lua, running against a declared set of bindings. A mod is content plus script,
  and the bindings it may call are a function of what its host composed.

Everything in this document is a statement about how those things are divided, what each may name,
and when each runs.

---

## 1. Axioms

Facts about Starbound, accepted without argument. They are not choices and not goals — they are what
the domain *is*, and an architecture that contradicts one is describing a different game. Everything
downstream is justified against these plus the north star, in that order.

| | the axiom | what it forbids |
|---|---|---|
| **A1** | **A world's truth has exactly one owner.** At any instant, one authority decides what is true in a world. | Two authorities for one world. Not a topology — a bug. |
| **A2** | **A participant's view is a prediction and may be wrong.** It runs ahead, it is corrected, it converges. | Treating a view as truth, which is what makes a client authoritative by accident. |
| **A3** | **Simulation advances in discrete, deterministic steps.** The same inputs to the same state give the same next state. | A world whose outcome depends on frame rate, wall-clock, or who was watching. |
| **A4** | **Content is data the engine does not understand.** The engine loads materials, items, species and dungeons; it does not know what any of them mean. | An engine that must be recompiled to add a rock. |
| **A5** | **Perception is optional.** Nothing in the simulation requires that anything is looking or listening. | Simulation that cannot run without a display — the whole reason a headless authority was hard. |
| **A6** | **Two processes never share a clock.** Time is local, always. | A seam that assumes both sides step together. |

Read them as a set and the north star stops being aspirational: **A5 and A6 are why N1 is reachable
at all**, and **A1 with A2 is the reason `authority` and `participant` are different words** rather
than two configurations of one thing.

---

## 2. North star

Three goals. Every decision in Section 3 is justified against at least one of them by name, and a
decision that serves none of them does not belong in this document.

### N1 — A modern distributed Starbound

The universe, its worlds and its participants can run **on different machines**. Not as a mode, not
behind a flag: the architecture has no seam that assumes co-residence, so placement is a deployment
choice rather than a rewrite.

This is the demanding goal, and it is demanding in a specific way. It means **every payload crossing
a seam is a value** — never a pointer, never a handle, never a reference into someone else's memory.
It means **every unit of placement is nameable**: you can place the many, not the one, so a component
that exists once per world is placeable and a component that exists once per process is not. And it
means a seam's co-located path is an *optimisation of* the split path, never a cheaper semantics.

### N2 — A sovereign, comprehensible engine

Every duty is owned by exactly one component. Every dependency is declared, and the declaration is
enforced by the build rather than by discipline. No god objects, no ambient singletons, no component
whose name is a noun covering three jobs.

The test is not aesthetic. It is: **can one person hold a component in their head, change it, and know
what they have not broken?** A boundary that cannot be enforced is a convention, and conventions decay
at exactly the rate the team turns over.

### N3 — Aggregate functionality comes from composition

The payoff of a modular system is not tidiness. It is that **components combine into aggregate
functions that nobody wrote a code path for**. A client is one such aggregate. A server is another.
Neither is the taxonomy of the system — they are two witnesses that the components compose, and if
they were the only two the property would not be worth claiming.

So the components are the vocabulary and the compositions are sentences. The document enumerates the
vocabulary exhaustively and the sentences only by example, because an architecture that can express a
fixed list of aggregates has not achieved anything a build flag could not.

**The test is falsifiable, and it is the one that matters.** Name a capability the system does not
have. Ask whether it needs new *components* or only new *wiring*. A few, none of which is a shipped
entry point today:

| you want | it composes | new components needed |
|---|---|---|
| a dedicated shard for one busy world | one `world` authority + `net`, placed alone | none |
| a load generator: 500 participants, no senses | `participant` × N, no `device` at all | none |
| a replay verifier | `world` authority + `transcript`, no participant | none |
| an offline map renderer | `worldgen` + `world_view` + `rendering`, nothing ticking | none |
| a save-migration tool | `storage` + `content`, no simulation whatever | none |

If the answer to that question is "new code" every time, the modularity is decorative. Every row
above is wiring, and that — not the count of components — is what the target state is *for*.

**N1 and N3 are one property seen at two scales.** Composition is placement inside one process;
distribution is placement across several. Both demand exactly the same thing of a component: a
declared boundary and a payload that is a value. Satisfy N3 honestly and N1 costs a deployment
decision; satisfy it dishonestly — with components that only compose in the arrangements someone
anticipated — and N1 is a rewrite wearing a config file.

---

## 3. Principles

Decisions are choices — Section 3 could have gone another way. These are not choices. They are the
invariants the model is built to satisfy, and a violation of one is a defect regardless of which
decision produced it.

| | the invariant | the failure it forbids |
|---|---|---|
| **P1** | **One duty per component.** A component's name is a duty, and the duty is singular. | `application` implemented a platform *and* a host, and its duty string hid that behind one noun. |
| **P2** | **Dependencies point down and are declared.** Every component names what it may include; the layering is a DAG with no upward edge. | A boundary that is a convention rather than a build rule reverts to a suggestion within a release. |
| **P3** | **A payload that crosses a seam is a value.** No pointer, no handle, no shared mutable state. | A pointer across a seam is a machine boundary that cannot be crossed, discovered at the worst moment. |
| **P4** | **What ticks does not depend on who is watching.** Residency is an explicit input; a world runs because something *requires* it, not because an observer is counting. | Simulation coupled to presentation — the defect that makes a headless authority impossible. |
| **P5** | **Placement is wiring.** Which process a component runs in is decided by the composition, never by the component. | A component that knows where it lives cannot be moved. |
| **P6** | **One writer per fact.** A descriptor is written by the act it describes; nothing is declared twice in two places. | The same fact stated twice drifts, and the drift is silent. |
| **P7** | **An instrument that cannot fail proves nothing.** Every rule stated as checkable is counted by something, and every check is proven to fire. | A rule called "checkable" that nothing counts is a rule in name only. |

P6 and P7 are architectural, not editorial. A model whose registers disagree is not a model, and a
boundary nothing enforces is not a boundary.

---

## 4. Decisions

Each decision states what was chosen, what was rejected, and which axiom or north star the choice serves.
*(D1–D9 to be rewritten into this form.)*
