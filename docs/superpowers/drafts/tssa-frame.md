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

Three kinds of thing use that universe, and the distinction runs through everything below:

| | what it is | how many |
|---|---|---|
| **an authority** | owns the truth of a world or a universe, and answers to nobody about it | one per world, one per universe |
| **a participant** | holds a *view* of a world, predicts against it, and asks the authority to change it | N, and they come and go |
| **a device** | a display, a speaker, a file, a recorder — something outside the simulation entirely | zero or more, per participant |

A **process** composes some of these. That composition — not a class, not a fork of the codebase — is
what makes one binary a graphical client, another a dedicated server, another a recorder, and another
an agent with no senses at all.

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

### N3 — Composable clients

A "client" is not a thing. It is a **composition** — a set of components wired together by an entry
point. A graphical client composes a participant with a renderer and a GPU backend; a headless client
composes the same participant with a recorder; an agent composes it with no senses at all; a CI
harness composes it with assertions.

None of these is a special case of another, and none is a stripped-down build of the "real" one. They
are peers, and the contract each shares is proven right precisely because more than one implementation
satisfies it.

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
