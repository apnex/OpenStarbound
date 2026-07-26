# M7 Axiom-Alignment Audit — Render Subsystem

> **Generated verbatim from the audit run, not transcribed.** Body sections below are the adjudicated
> output of workflow `wf_414dfe8b-952` (8 agents, 1.1M tokens, 2026-07-25), reproduced without edit.
> Only the header and the closing *Decisions and status* section are maintained by hand — and that
> section records what has since changed, rather than editing the audit's findings after the fact.

**Why this file exists.** Commit messages on `integration` cite guardrails by number (G1, G3, G4, G8,
G9, G10). Until 2026-07-26 the artifact defining them lived only in a workflow JSON under `~/.claude/`,
so `git log` referenced rules a reader had no way to look up. The audit's own closeout hook 10 asked
for it to be filed as the subsystem's first M7 artifact; this is that filing, late.

**Verdict:** `pass-with-guardrails`

The render subsystem's code is constitutionally sound — genuinely air-gapped passes, an earned L2 primitive, rationale banked where the next author reads it — but its enforcement layer is not: at HEAD the `render_layering` ctest invokes an instrument that cannot parse its own arguments and exits 2, while three documents and a commit message assert it runs in CI, so the subsystem passes only with a blocking guardrail to make HEAD self-consistent and prove it from a clean checkout.

---

## Axiom mapping

IDENTITY: render subsystem of /root/frackin/OpenStarbound, branch `integration`, HEAD `1e46f71c` (working tree dirty: 2 modified files). Constitution snapshot /root/mission-kit/axioms (A0-A14, all `active`); methodology M7; gate W22 (falsifier: speculative or laundered axiom citations). Audited read-only 2026-07-25 against tree content; git identity, commit messages and doc assertions treated as non-evidence throughout. Source: seven auditor dossiers, adjudicated; A8/A9 arrived empty ("test shape") and were performed by the adjudicator directly.

A0 Sovereign Intelligence Engine — LOAD-BEARING (narrow). Verified: `scripts/render-profile.sh:3` ("No Director in the chair, no window on his screen"), `render-gate.sh:25` (refuses to certify a stale log), `StarRetainedSurface.hpp:19-40` (the three-term contract written into the primitive), and #177's motion term being DERIVED rather than an `if (flying)` special case — strategic intent has genuinely become substrate that decides without a human. Its unique load at verdict level is the question "does this drag the Director into how-to", and the answer is mostly no. I struck the second A0 charge (see `rejected`).

A1 Sovereign State Transparency — LOAD-BEARING. Verified: every decision variable of the retained caches is private and unqueryable (`StarBackdropPass.hpp:92,103,108,129-133,146-163`); the only query surface, `/rendercache status` (`StarClientCommandProcessor.cpp:661-672`), echoes configuration, not effective state; the effective adaptive N exists only as a `Logger::info` on change. Telemetry counters registered as function-local statics inside conditional blocks mean ABSENT is indistinguishable from zero for a snapshot-differencing consumer. Hidden State Problem, live.

A2 Isomorphic Specification — LOAD-BEARING. Verified against the tree: the asset-side spec is genuinely isomorphic (`opengl.config`, `effects/*.config` parsed at runtime, mod-overridable), but the C++ side declares render behaviour in four competing places. I confirmed `StarRootLoader.cpp:107` ships `envRefreshInterval:4` with a comment naming the exact fault, while `StarWorldPainter.cpp:226` still supplies a fallback of `1`; `parallaxMaxDriftStepPx` ships 0.75 against a call-site 1.5f (`:233`, knowingly preserved for byte-identity); `lightingWorldUpscale` ships 2.0 against 1.0f (`:286`). `newLighting` is read and written but declared nowhere.

A3 Sovereign Composition — LOAD-BEARING. Verified: `BackdropParams` is a real declared contract resolved per frame at the composition root (BackdropPass measures 0 `Root::singleton` reads via `render-inventory.py`); `RetainedSurface` is earned exposure with a real second consumer and a compile-enforced no-Renderer fence. Against that: BackdropPass concedes its own Law-of-One violation in its header; `StarWorldPass.cpp:59` destructively `std::move`s the per-entity layer lists out of a `WorldRenderData&` that the header does not declare as consumed; the painters (1810 lines, 6 reads) are outside every gate.

A4 Zero-Loss Knowledge — LOAD-BEARING. This is where the record fails hardest and I verified it myself: `git show --stat aba06048` is `scripts/{layer1-layering-lint.sh => layering-lint.sh} | 0`, 1 file changed, 0 insertions, 0 deletions — a pure rename under a 40-line message asserting CI wiring, a ratcheting lint and a self-test. At HEAD the target-state doc still states "now 296" (tree: 318), "427 → 119-line render()" (tree: ~141) and "7 `Root::singleton()` reads in `BackdropPass`" (tree: 0); the Director-approved design spec still prescribes constructor injection, which is now known to ship a regression.

A5 Perceptual Parity — LOAD-BEARING. Verified in `render-gate.sh:16,24-27`: the guard deletes the log, runs, then asserts log-newer-than-binary — after a failed build the binary never moved, so a stale binary certifies as PASS. Every byte-identity claim in the campaign rests on this gate. Second: the harness camera never moves (stated in code at `StarBackdropPass.cpp:303-305,516-518`), so agent and Director do not perceive the same subsystem.

A6 Frictionless Agentic Collaboration — LOAD-BEARING. Verified: `board-export.py` and `render-inventory.py` delete transcription rather than easing it; `render-gate.sh:36-46` and `StarBackdropPass.cpp:110-113` produce genuinely actionable failures. But the best failure message in the set — the ratchet's "edit this number and ask this question" — is unreachable at HEAD; what an operator actually gets is `cannot read --needle`, which names no layer, no ceiling and no remedy.

A7 Resilient Agentic Operations — LOAD-BEARING (Error Isolation / Boundaries / Actionable Signals only). Verified: `mergedCompose` degrades to two byte-identical sequential composites; the one-frame deferral bound is enforced; `renderInit` invalidates caches so a new world never composites the previous world's sky. Adjudicated against intent-authority: the clause-2 "recovery" does not recover in the shipped configuration — `backdropComposeMerge` ships `true`, so after the reset the same frame re-defers at `StarBackdropPass.cpp:288-289` and the logged sentence "Recovering by compositing env directly this frame" describes an action not taken.

A8 Gated Recursive Integrity — LOAD-BEARING (adjudicator-performed; the assigned auditor returned nothing). Verified: layers are explicitly enumerated with ground-truth status (success signal 4 — met, uniquely well). Gated Ascension is not: L3's only correctness proof is `render-gate.sh`, which is referenced by no workflow, no preset and no ctest (verified by grep) — it needs a GPU, a built binary and an operator, so L3 has no automated certification at all, while L1's two gates had never executed in CI until today and one of them cannot execute now. Binary Certification is deliberately traded away by the ratchet (partial credit by construction), which I judge the right call but a recorded deviation. Law of Fallback is the sharpest hit: the stale-binary defect is a base-layer instrument failure that silently invalidates every certification above it, and it was patched nowhere.

A9 Chaos-Validated Deployment — LOAD-BEARING on two mechanics, explicitly not on three (adjudicator-performed). Full-Stack Simulation and Simulation↔Production Fidelity genuinely reach: the harness boots the real game on the real GPU offscreen, and the measured sim↔production delta is large and known — the parallax bypass and standalone env compose never occur offline, which produced both a wrong measurement (owner `gl` at 118.4%, parts exceeding the whole by 1279 µs/tick) and a defect that reached the Director in play (#177, choppy stars). That is Happy-Path Brittleness by the charter's own name, with a filed defect (#174) and no fix. NOT reaching: Standardized Entropy Battery, Telemetry Feedback Loop and Deterministic Trunk Gate over a coordinating-actor graph — node death, packet loss and cascade failure have no referent in a single-process GL renderer, and I refuse to manufacture them.

A10 Autopoietic Evolution — LOAD-BEARING. Verified: the corrective loop is entirely manual and failed at the one point that mattered today. Commit `1e46f71c` states the render-gate staleness defect was "filed as a follow-up rather than fixed here"; the task list contains no such item (highest id 177, ENV-CHOP), `docs/board.md` has no row, and the standing memory node's rule 3 was not amended. The lesson's own failure mode recurred and was left in two commit messages.

A11 Cognitive Minimalism — SUPPORTING, and half of it does not reach at all. Token Accounting, Economic Telemetry, hydration-before-a-model-call and model-tier migration have no referent in C++ on a render thread; citing them here would be W22's named falsifier. What does reach is Deterministic Primitives and Cognitive-Boundary Discipline over the maintenance loop: the ContentKey unification is textbook, and `StarBackdropPass.cpp` at 641 lines with two entry points sharing three mutable members across the lightmap phase, and load-bearing rules ("cadenceHit must stay unconditional and last") enforced only by comment, is the failure case.

A12 Precision Context Engineering — SUPPORTING over the artifact set, NOT MATERIALLY IMPLICATED over the runtime. No prompt, no context window, no model on any render path. Over the docs the finding is navigational, not volumetric: `docs/render/` holds 6 files and 1384 lines with no index, and the two doc chains never reference each other, so a cold agent's arbitrary reading order routes through the stale authority doc. The long documents themselves are good projections, not dumps — the brief's hypothesis that length is the fault does not survive contact with the files.

A13 Director Intent Amplification — LOAD-BEARING on authority and attention; its profile mechanics do not reach. Verified: the spec records that the Director does not review spec markdown and that boundary calls are Director-held; `docs/board.md` is generated so he is not the archivist; #169 stops at his threshold because output changes; #174 was filed after he corrected an agent's excuse. Against that, #136 is open solely on a six-item eyeball list of which item (b)'s root cause is now unit-gated off-GPU (`retained_surface_test.cpp:22-28`) and item (f) is counter-reading the substrate can do. Preference Overreach and Profile Miscalibration are NOT MATERIALLY IMPLICATED — nothing here infers a lean; `/rendercache` and `/lighting` are direct manipulation.

A14 Compounding Learning — LOAD-BEARING. Verified: friction was mined to root cause and banked as substrate (the three-term contract in the L2 header; the counts delegated to an instrument rather than corrected by hand; `render-inventory.py:71-78` recording its own first false positive permanently). Three failures: the day's two most reusable artifacts are uncommitted, a captured lesson's failure mode recurred unfiled, and ContentKey — whose entire justification is that hand-written keys drift on rounding — ships with zero tests beside a sibling with nine.

## Layered application

Umbrella / intent (A0). The subsystem converts "the game should feel smooth on my machine" into instruments that decide without the Director: a profiler that declares it needs no window, a gate that refuses to certify a stale run, a cache contract written into the primitive. The residual drag on the Director is #136's eyeball list and the fact that every motion-gated regime still requires him in-game.

Substrate / state (A1, A5, A7). Source of truth for the knobs is `StarRootLoader.cpp` and it survives restart; source of truth for cache decisions is private in-process member state with no query surface at all. On restart, caches invalidate correctly (`renderInit`); on renderer re-init, `BackdropPass` has `setRenderer()` and `GpuLightmapPass` does not (latent — one renderer per process on the SDL path). On failure between the two backdrop entry points, the code detects and logs but, under shipped defaults, does not perform the recovery it announces.

Specification / configuration (A2, A11). GPU-facing behaviour is declared as data and is mod-overridable — real isomorphism. C++-side render behaviour is declared in four competing places with no reconciliation rule, and the fallbacks are reachable, not dead: `Configuration::set` with a null value erases the key, and the A/B restore path can pass null. Spec and runtime can and do drift, and one drift is operator-visible: `/rendercache status` can report the parallax cache "off" (fallback 1) while the render path runs adaptive (fallback 0).

Composition / boundaries (A3). L2 is a clean, earned, compile-fenced boundary with a real second consumer. L3 is one honest pass (LightmapPass, whose clean sheet is bought by the orchestrator), one small residual (WorldPass, 2 reads, plus an undeclared destructive consumption), and one 641-line pass that concedes it owns three concerns. The orchestrator is accreting the concerns the passes shed (9 reads, 318 lines, lighting diagnostics and lightmap uniform binding). The painters — the largest and most Root-coupled group — sit outside the architecture doc's stack and outside every gate.

Knowledge / audit (A4). Rationale is durable and co-located, retractions are first-class, and dead commit ids are made interpretable rather than dangling — this is genuinely above the bar. The failures are at the authority layer: a commit message that describes work its commit does not contain, a Director-approved spec still carrying a prescription known to ship a regression, and five hand-typed counts at HEAD that the tree contradicts.

Perception / context (A5, A12). The agent's channel can certify a binary that was never built; the harness's camera never moves, so the agent structurally cannot see the regime the Director actually plays in. The doc set is well-engineered per file and unnavigable as a set.

Collaboration / operations (A6, A7). Administrative handoff is genuinely abolished where it was measurable (board export, inventory). Failure signals are excellent where they fire and absent where they matter most: the ratchet's message is unreachable, the clause-2 counter reaches no verdict and no HUD, and both warn budgets are process-lifetime statics, so a burst buys session-long silence.

Integrity / validation / deployment (A8, A9). Layers are enumerated, which most systems never do. But L3 depends on L1/L2 certifications that had never run in CI until today, L3's own proof is a manual GPU script wired to nothing, and the newest gate errors on its own arguments. The chaos surface that exists here is not node death but motion, resize, AA toggle and world re-entry, and the harness exercises none of them; the one defect that escaped to the Director escaped precisely through that gap.

Self-evolution (A10, A14). Discovered friction routinely becomes substrate — and today, twice, it did not: an explicitly deferred defect was never filed, and the day's most reusable artifact was never committed.

Director attention / authority (A13). Authority is explicit, non-delegated and recorded at the decision points. Attention is leaking in two identifiable places, one of which has a passing unit test asserting the exact invariant the Director is still being asked to eyeball.

## Tensions

1. A8 (Binary Certification — "no partial credit") vs A13 (do not score a Director-visible fix as a violation). The `render_layering` ratchet is partial credit by construction: ceilings set at today's counts. RESOLUTION: the ratchet is correct and I uphold it. The 8th singleton read arrived with #177, a fix the Director could see; a zero gate would have scored it a violation and invited routing around. It is a recorded deviation with a ratchet log and a "lower these, never raise one without saying why" rule, not a hidden exception. COST: A8's binary property is genuinely spent, and the trade is only honest while the ceilings descend — nothing yet enforces descent, and nothing distinguishes "ceiling met" from "ceiling merely not exceeded" now that BackdropPass is actually at 0.

2. A2 (declare the knob) vs A3/A11 (concept count in the pass). Every knob promoted from a hidden literal to a live-tunable config key adds a read and a concept. `envMaxDriftStepPx` is the worked example: declaring it is what created the 8th read that forced the ratchet. RESOLUTION: A2 wins, and the counter-pressure is paid at the composition root — but that resolution is what grew WorldPainter from 119 to 318 lines. The metric that survives is "pass bodies are pure functions of their parameters"; the ratchet measures the gameable proxy instead. Fix the metric before the next pay-down or the subsystem will optimise the number.

3. A0 (seal config inside the substrate) vs A13 (preserve direct manipulation). The design doc prescribed constructor injection; the implementation refused it and resolves per frame at the boundary, because the eight knobs are live-tunable mid-session and freezing them would have broken the console levers the campaign uses to A/B itself. RESOLUTION: A13 correctly overrules a doctrinaire A0 reading, the reasoning is written where the next author will hit it — and the Director-approved spec still carries the superseded prescription. A correct override of an authority document that never got propagated back to the authority is half a resolution.

4. A7 (do not crash) vs A5 (the operator must see it). Clause 2 chose detect-and-recover over assert — right, because a hard failure turns a mod-induced ordering mistake into a crash on the Director's machine. But it converts an unmissable failure into four log lines and a counter that appears in zero scripts, zero tests and zero docs (verified by grep), AND the recovery is nominal under shipped defaults. The resolution currently sits on the worst point of the curve: quiet and not actually recovered. Making the recovery real and gating the counter moves it to the good point without reintroducing the crash.

5. A3 (Law of One — split BackdropPass) vs A8/A9 (prove the lower layer before you move it). Splitting is the A3-correct move, but the only correctness evidence for the backdrop is a pair of in-frame oracles that are structurally blind to the paths a split would move — the harness camera never moves, so the bypass and standalone-compose arms never execute. RESOLUTION: A8 outranks A3 here, and the sequencing is #174 first, split second. This must be a recorded deviation with a trigger, not a standing excuse, or the Law-of-One violation calcifies behind a permanent "not yet certified".

6. A5/A9 (byte-identical provability) vs A1/A2 (one source of truth). Two duplications exist purely so the oracle can prove parity: `parallaxMaxDriftStepPx`'s dead 1.5f fallback against the shipped 0.75, and the parallax cache's 0.0f pixelRatio sentinel against the primitive's -1.0f. RESOLUTION: correct trade, correctly documented, correction filed as a separate deliberate change. Residual: A1's single-source claim now holds only conditionally, and the sentinel is permanently part of L2's public shape for one caller's migration history.

7. A14 (bank the lesson) vs A11 (minimal concept count) inside the same file. `StarBackdropPass.cpp` is 280 comment lines over 326 code lines, and each block encodes a separately-expensive discovery. RESOLUTION: do not cut the prose — move the invariants it protects into code (tests, an L2 pure function, a target-generation counter). That pays both axioms; cutting the prose pays neither.

8. A6 (zero administrative friction) vs A14 (capture must be durable). `board-export.py --check` is deliberately not wired into CI because a CI machine has no task store — the honest A6 call. The consequence is that A14's capture step rests on a local habit, and the habit is currently unpaid.

## Deltas

| # | axiom | size | change | why |
|---|---|---|---|---|
| 0 | A8 | small | Commit the working-tree `scripts/layering-lint.sh` (which implements `--needle` and `path=MAX`), or revert `source/test/CMakeLists.txt:121-125` until the instrument exists. A registered test whose instrument cannot parse its own arguments is strictly worse than no registration, by this campaign's own rule. | At HEAD, `render_layering` feeds `--needle` to a script that does `FILES=("$@")` with a hardcoded needle; python calls `open("--needle")`, prints `cannot read --needle` and exits 2 on all six CI jobs. The flagship L3 architectural guarantee cannot execute while three docs and a commit message assert it runs. |
| 1 | A8 | small | Prove the gate set from a pristine checkout: clone HEAD to a scratch dir, configure, `ctest -L NoAssets`, and paste the output into the closing record. Do this before any further render change. | The claim 'CI gate set 4/4' was only producible from a dirty tree. Nothing today distinguishes 'the gates pass' from 'the gates pass on this workstation with two uncommitted files'. |
| 2 | A5 | small | Harden `render-gate.sh`'s staleness guard: compare the binary against the newest source mtime, or have the build embed a build-id the gate asserts. Comparing the log to the binary is trivially satisfiable. | After a failed build the binary never moves, so a fresh log certifies a stale binary as PASS. Commit 1e46f71c records this firing twice in one session, caught only by a human checking mtimes. Every byte-identity claim in the campaign inherits that lie. |
| 3 | A10 | small | File the staleness defect as a real board item, and amend the standing memory rule from 'the log is newer than the binary' to 'the BINARY is newer than its sources AND the log is newer than the binary'. | Verified: no task exists (highest id 177), no board row, memory rule 3 unrevised. The only trace is prose inside two commit messages. The next session starts cold and re-hits it — Friction Fossilization with the lesson already written down and incomplete. |
| 4 | A7 | small | Make the clause-2 recovery real — force `composeMerge=false` for the recovering frame — or correct the log line to say detection with recovery deferred. | `backdropComposeMerge` ships true, so after the reset the same frame re-defers at StarBackdropPass.cpp:288-289. A throw recurring between the two entry points keeps the whole backdrop black indefinitely while the log claims recovery, then goes silent after four frames because the warn budget is a process-lifetime static. |
| 5 | A1 | small | Add `render.backdrop.compose_recovered` to `render-gate.sh`'s verdict block, and reset the warn budgets on world entry (`invalidateCaches` is the natural hook). | Verified by grep: the counter appears in no script, no test and no doc. A contract violation the code deliberately survives is currently observable only by someone who already suspects it — the gate's own comment states the rule it is breaking ('a check that reports but does not gate is not a gate'). |
| 6 | A14 | small | Add ContentKey unit tests to `source/test/retained_surface_test.cpp`: determinism, order-sensitivity, quantisation boundary at scale 255, Vec3B/Vec4B distinctness, and behaviour for values outside [0,1]. | ContentKey shipped today into the one L2 file CI actually executes, with nine sibling tests and zero of its own, and its entire stated justification is that 'rounding is exactly where two hand-written keys drift apart'. `(uint64_t)(unsigned)floor(scale * v)` on a negative input is the untested edge of a primitive two axioms now depend on; a drifted quantiser's symptom is a silently wrong sky, not a crash. |
| 7 | A3 | medium | Extend the `render_layering` ratchet to the unmetered files at their current counts: `StarWorldPainter.cpp=9`, `StarTilePainter.cpp=3`, `StarTextPainter.cpp=3`, `StarEnvironmentPainter.cpp=0`, `StarDrawablePainter.cpp=0`. | The ratchet meters 2 of the subsystem's 17 residual singleton reads. Because closing the Air-Gap moves reads into the orchestrator, the metric is satisfiable by relocation — the exact move that took BackdropPass from 8 to 0. The painters, at 1810 lines and 6 reads, are the growth surface and no gate touches them. |
| 8 | A9 | medium | Sequence #174 (motion-driven harness: WALK, plus ZOOM and an AA/HDR toggle knob) ahead of any further L3 pay-down or BackdropPass split. | Verified from the knob inventory: there is no WALK, ZOOM or AA toggle. Every motion-gated branch — the parallax bypass, the standalone env compose, #177's whole regime — is unverifiable offline. That gap already produced one wrong measurement (owner `gl` at 118.4%) and one defect that reached the Director in play. Splitting a pass certified by a gate blind to the paths being moved is Foundation-of-Sand. |
| 9 | A4 | small | Regenerate or delete the five hand-typed counts at `docs/render/architecture-3-target-state.md` HEAD lines 3, 35, 39, 41, 132/138, and re-score the compliance table; the uncommitted rewrite still marks BackdropPass '❌ the worst offender' for a pass now measuring 0. | A cold agent reading HEAD is told to pay down 7 singleton reads in BackdropPass that no longer exist, and #137 carries the same dead number. The correction written today to fix hand-typed counts introduced a fresh wrong one. |
| 10 | A4 | small | Put a correction banner on `docs/superpowers/specs/2026-07-19-render-decomposition-design.md` §3, retracting the constructor-injection prescription and the 'each pass owns its metric handles' claim, pointing at StarBackdropPass.hpp:29-33. | The retraction exists only downstream. An agent following the Director-approved authority document implements constructor injection and silently freezes `/rendercache envrefresh` — the precise regression the implementation refused, re-introduced by the record. |
| 11 | A12 | small | Add `docs/render/README.md`: ~30 lines giving each file's role (canonical / point-in-time / panel), its freshness basis (generator-backed vs hand-typed and last verified when), and a reading order; cross-link architecture-3 to layer1-architecture.md. | Six files, 1384 lines, no index, and the two doc chains reference each other zero times (verified). The arbitrary reading order a cold agent takes routes through the stale authority doc — this is the gap that converts A4's stale prescription into a wrong action. |
| 12 | A13 | small | Split #136: close item (b) as substrate-decided (retained_surface_test.cpp:22-28 asserts the exact pixelRatio invariant off-GPU in CI) and convert item (f) into the counters oracle #174 already specifies (refreshed+skipped+bypassed == frame count). Leave only genuinely perceptual items on the Director. | Shipped work is parked on the Director's eyeballs for two items the substrate already decides, one of which has a passing unit test asserting it. The Director is the one non-scalable resource and this is measurable throughput lost. |
| 13 | A3 | small | Declare WorldPass's destructive consumption at `StarWorldPass.hpp:37` in one line, or take `WorldRenderData&&` / rename to `consumeAndRenderWorld`. | `renderWorld` std::moves the per-entity layer lists out of a reference the contract calls an input. The rendertest state fingerprint already reads `m_renderData` after render; it survives today only because it reads outer `.size()`. Extending it to hash nametag or overlay content — the obvious next step for its stated purpose — yields a stable, wrong fingerprint claiming the inputs matched. A diagnostic that lies is worse than none. |
| 14 | A2 | medium | Declare `newLighting` in a default block, and route render config reads through one accessor that falls back to `Configuration::getDefault(key)` rather than to hand-typed call-site literals. | Four competing declaration sites with no authority rule. `/rendercache status` can tell the operator the parallax cache is off (fallback 1) while the render path runs adaptive (fallback 0); and once a key is erased — reachable via the A/B restore path with a null captured original — the call-site literal becomes authoritative for the session. |
| 15 | A6 | small | Mechanise the `NoAssets` label rule: a small ctest that parses registered test names and asserts every test not on an explicit assets-required allowlist carries the label. | The omission this replaces is the one that left both L1 architecture gates dormant since creation — they existed, passed locally, and guarded nothing. It is now protected by a shouting comment, which is prompt-only enforcement of a deterministic rule. |
| 16 | A1 | small | Hoist the four telemetry counter registrations out of conditional function-local statics to unconditional registration at pass construction. | A path never taken yields an ABSENT key in snapshot(), which a consumer differencing two snapshots cannot distinguish from zero — including `compose_recovered`, which by construction did not exist until the fault first fired. |
| 17 | A3 | small | Delete the dead `StarRoot.hpp` / `StarConfiguration.hpp` includes at StarBackdropPass.cpp:2-3, fold the duplicated standalone-compose call sites (:296-312 / :509-525) into one helper, and correct the inaccurate build claim at StarRetainedSurface.hpp:53-57 (the real fence is StarRenderer.hpp in source/application, not StarColor.hpp). | Tidiness, mostly — but the dead includes keep a dependency edge the needle-based lint cannot see, and a boundary contract defended by a build claim that is false invites the next author to test the claim and conclude the boundary is soft. |

## Implementation guardrails

G1 (BLOCKING). No further render change lands until HEAD is self-consistent and proven from a pristine checkout: clone HEAD, configure, `ctest -L NoAssets`, paste the output. If G1 is not closed before the next render commit, this verdict flips to `blocked` — a subsystem whose only architectural gate errors on its own arguments cannot bear weight under A8.

G2. A gate's registration and its instrument land in the SAME commit. A commit message may not describe work outside its own diff; `git show --stat` is the record, prose is not.

G3. No document may state a number that `scripts/render-inventory.py` can measure. Either generate it or delete it. This applies to the docs that were rewritten today to say exactly this.

G4. Any new `Root::singleton()` read in a pass requires a ratchet edit plus an argument in the commit message. Once the orchestrator and painters carry ceilings, relocating a read counts against the receiving file — relocation is not a route to green.

G5. No new retained surface, cache or key primitive ships without off-GPU unit coverage of its invalidation terms in `core_tests`. ContentKey is the standing counter-example; do not repeat it.

G6. Any telemetry counter that signals a contract violation must be asserted by `render-gate.sh`'s verdict block. A counter no verdict reads is not a signal.

G7. No new comment-only invariant may be added to `StarBackdropPass`. New rules are mechanised (a test, a type, or detect-with-real-recovery) or the deviation is recorded with a named owner and a trigger. The file already carries ~34 interacting decision concepts and several load-bearing rules enforced by asking the next author to be careful.

G8. No "byte-identical" or "gate passes" claim may be made from a dirty tree, and none may be made until the staleness guard asserts the binary is newer than its sources. Certification names the binary it certified.

G9. No BackdropPass split and no L3 boundary change until #174 lands. The only correctness evidence available is structurally blind to the paths a split would move; A8 outranks A3 here until the harness can see them.

G10. When an implementation overrides a Director-approved design prescription on evidence — as the per-frame BackdropParams decision correctly did — the authority document is corrected in the same change. An override that lives only downstream is a trap for whoever reads the authority next.

## Closeout hooks

1. Clean-checkout gate run, output pasted (G1). Must show `layer1_layering`, `render_layering`, `render_surface_tests` and `core_tests` all RAN and PASSED — not skipped, not exit 2. Re-check whether an ADD_TEST of a `.sh` COMMAND executes on the `windows-latest` runner; if it does not, both layering gates are green-by-absence on one of six jobs and the label wiring bought nothing there.

2. Working-tree reconciliation: confirm `scripts/layering-lint.sh` and `docs/render/architecture-3-target-state.md` are committed or deliberately reverted, and that `git status` is clean before any closeout claim about the gate set.

3. Staleness-guard fix verified by construction, not assertion: build a binary, intentionally fail a rebuild, run the gate, confirm it REFUSES. Then confirm the board item exists and the memory rule is amended.

4. Clause-2 recovery: after the fix, confirm by reading the shipped-default path that the frame genuinely composites env to main, and that `render.backdrop.compose_recovered` fails the gate.

5. ContentKey tests present and green in `core_tests`, including the negative/out-of-range quantiser case.

6. Ratchet extended to the orchestrator and painters, with the ratchet log entry stating the ceilings and the date; verify `render-inventory.py` and the lint agree on every metered file (they use different comment-stripping strategies — confirm, do not assume).

7. Doc reconciliation: no number remains in `docs/render/` that the inventory can measure; the compliance table reflects BackdropPass=0; the design spec carries its correction banner; `docs/render/README.md` exists and the two chains cross-link.

8. #136 re-scoped to perceptual items only, with the closure of (b) citing `retained_surface_test.cpp:22-28` and (f) replaced by the counters oracle.

9. #174 status re-checked before any claim of parity coverage over cache paths, and before any BackdropPass split (G9).

10. This audit is filed as the subsystem's first M7 artifact and referenced by the next render arc's seed, so the next reusable seam is gated BEFORE code rather than adjudicated after it.

## Process fault

RECORDED, not excused. This audit is retrospective: the decomposition and all six of today's commits are already in the tree, so it cannot function as the gate M7 requires, and I decline to backfill it as though it had.

The charge is not blanket. A partial gate DID exist for the decomposition: `docs/superpowers/specs/2026-07-19-render-decomposition-design.md` §8 carries an "Axiom scoring (A3 · A8 · A4)" block written at design time, Director-approved section-by-section in conversation, which correctly singles out Air-Gap as THE load-bearing axiom rather than scoring everything equally and refuses three named faults. That is more M7 discipline than most work receives, and I credit it. Measured against M7's eight-row artifact contract it is incomplete: no verdict, no tensions, no deltas, no guardrails, no closeout hooks, one of seven citations weighted, and silent on A0, A5, A9 and A13 — the four axioms that produced this audit's sharpest findings.

Pieces that needed a gate and got none:
(a) THE RATCHET AND THE CI-LABEL DOCTRINE. By M7's own trigger list this is operating procedure — it instructs future authors ("edit a number here and say why"), and the NoAssets rule changes what authors will perceive as a registered test. The cost of skipping the gate is not hypothetical: a pre-implementation gate asking "how will you prove this gate executes from a clean checkout?" would have caught the registration/instrument skew before three documents began citing a gate that exits 2.
(b) THE L2/L3 SEAM — RetainedSurface, ContentKey, BackdropParams. Substrate other passes will rely on; a world-band cache is already anticipated in the code as the next consumer. It landed with a compile-enforced fence and zero tests for its newest member.
(c) THE "RESOLVE PER FRAME AT THE BOUNDARY, NOT AT CONSTRUCTION" DOCTRINE CHANGE. This is the sharpest one, and no auditor named it as its own item. A Director-approved design prescription was overturned by an implementer's judgement — correctly, on evidence — with no gate, and the authority document still carries the superseded prescription. Overturning an approved design is exactly the class of decision M7 exists to gate.

Correctly exempt, and I refuse to charge them: #177's env-cache motion term is the short local bugfix M7 names; the clause-2 detect-and-recover is local hardening; `render-inventory.py` measures without enforcing and nothing depends on it programmatically (which is itself a delta, not a fault).

Aggravating fact about the record, verified directly rather than inherited: commit `aba06048`, whose 40-line message asserts the CI wiring, the two-registration lint and a self-test ("verified it FAILS when the ceiling is tightened to 7"), is `1 file changed, 0 insertions(+), 0 deletions(-)` — a rename. The CMake half landed in the next commit; the lint's argument parsing has never been committed. Whether the self-test was ever executed is UNVERIFIABLE — it could only have run against an uncommitted script and no result artifact exists. Any future reader trusting `git log` will believe a self-tested ratcheting lint landed today.

Verified absence: a repo-wide grep for `W22|axiom-alignment|M7-axiom|mission-kit` returns nothing. This artifact is the first.

## Struck and adjudicated

STRUCK AS DECORATIVE OR MISFILED (W22 falsifier: speculative or laundered citations):

1. A11's runtime mechanics — Token Accounting, Economic Telemetry, hydration-before-a-model-call, "model-tier migrations need only a config change" — against C++ on a render thread. There is no model on any render path. The spec-minimalism auditor pre-refused these and I confirm the strike so downstream work cannot revive them; the subsystem's GPU/CPU accounting is excellent but A11 is not the axiom that earns it. A11 stands as SUPPORTING on cognitive-boundary grounds only.

2. A1 as the home of the uncommitted-lint finding ("Ephemeral Truth Loss ... the L3 ratchet's truth is not in the tree"). A1 governs system state and its queryability, not a developer's workspace hygiene. The finding is real and severe; its axiom home is A8 (an uncertified gate) and A4 (the durable record), and it is filed there. Struck from A1's mapping, which stands on its own evidence.

3. A0 double-billing of the commit-message/content divergence as a breach of "perfect institutional memory". A0 is the umbrella; charging it here inflates the umbrella with a fault that belongs squarely to A4 and to M4's frozen-history rule. The finding survives intact under A4. A0's mapping is narrowed to what only A0 can adjudicate: whether intent became substrate that decides without the Director.

4. "Add a repo-root CLAUDE.md" (A12 delta 2). This is a repo-wide tooling preference proposed on subsystem evidence, with no demonstrated failure attached. The `docs/render/README.md` index survives because a concrete failure path was traced through it; the CLAUDE.md recommendation does not and is struck as scope creep.

5. A5's human-vs-agent HUD channel asymmetry as a delta. The observation is true — the /debug HUD carries four pass timers while the snapshot JSON carries more — but no failure was demonstrated from it, and A5's load here is carried entirely by the stale-binary certification and the motionless harness. Demoted to a note; no work.

6. A9's Standardized Entropy Battery, Telemetry Feedback Loop and Deterministic Trunk Gate. Node death, packet loss, jitter and cascade failure across a coordinating-actor graph have no referent in a single-process GL renderer. I refused to manufacture conformance by metaphor. A9 is load-bearing on Full-Stack Simulation and Simulation↔Production Fidelity only, both of which reach on measured evidence.

7. A13's Preference Overreach and Profile Miscalibration; A7's Deferred-Backlog Reconnect and multi-actor rehydration. Nothing here builds or consumes a revealed-preference profile, and there is no connector, rate limit or second actor. Correctly marked non-implicated by their auditors; recorded so the absence is a result rather than an omission.

8. "Split BackdropPass" as work to do NOW (A3 delta 5). Rejected for this cycle and converted to guardrail G9. Splitting a pass whose only correctness evidence is blind to the paths being moved trades a Law-of-One violation for a Foundation-of-Sand risk. The deviation is recorded with a trigger (#174), not left as a standing excuse.

9. "Every failed unit of work auto-spawns a defect record" as a delta (A10 success signal 1). Building auto-spawn machinery for a single-repo game fork is ceremony bloat. The load-bearing remedy is filing the one defect that was explicitly deferred and never filed, and fixing the guard — which is what the delta says.

10. "Version harness/profiles (2.5MB)" as a required change (A4 delta 5). A deviation with a fallback path is already recorded, and force-adding binary evidence is a Director-held judgement. Downgraded to: correct the `.gitignore` rationale, since "regenerate it, never version it" is false for irreproducible telemetry and is the sentence the next author will read.

CORRECTIONS TO THE DOSSIER AND THE BRIEF, made because the brief demanded tree-verification and two auditors did not fully honour it:

11. The brief's premise that the painters are "NEVER audited, no layer claims them" is stale by one day. `scripts/render-inventory.py` buckets them as "painters (pre-decomposition)" — I ran it and confirmed the row (1810 lines, 8 files, 6 reads). The accurate statement, which is the one I acted on: the painters are MEASURED by the instrument, unclaimed by the architecture doc's four-layer stack, and covered by no gate.

12. The knowledge-context auditor's V1 ("the doc declares hand-typed counts to be THE defect, then leaves five hand-typed counts in the same file") conflates HEAD with the uncommitted rewrite. At HEAD the doc contains no such declaration — it simply carries the stale counts. The self-contradiction belongs to the working-tree version, where it is real but smaller: the corrected table still scores BackdropPass "❌ the worst offender" at an actual 0. Finding retained in corrected form; the framing struck, because mixing tree states is precisely the rigour failure the brief warned against.

13. DISPUTE ADJUDICATED — clause-2 "detect and recover". Intent-authority reads it as correct A13-shaped hardening; substrate-state reads the recovery as nominal. I read the code: the reset at StarBackdropPass.cpp:121 is unconditional and does prevent a permanently latched flag, but with `backdropComposeMerge` shipping true and the env cache active, line 288-289 sets the flag again on the same frame, so the logged sentence "Recovering by compositing env directly this frame" is false in the shipped configuration and a recurring fault is not broken out of. SIDE: substrate-state, with two sharpenings — the detection and counter are genuinely valuable and worth keeping, and the fault is currently unreachable (single caller, verified ordering), so this is medium severity, not high.

14. DISPUTE ADJUDICATED — A11's file-size figure. The brief says BackdropPass.cpp is 603 lines; the instrument measures 641. The instrument wins; the brief's L3 total is stale by the same margin.

---

# Decisions and status since

*Hand-maintained. The audit above is frozen; this section records what changed after it, so the two are
never confused. M7 requires that a flaw found by an audit is either fixed or recorded as an explicit
authority-accepted deviation — both appear here.*

## Authority-accepted deviation: G9 narrowed (2026-07-26, Director-approved)

**G9 as written:** *"No BackdropPass split and no L3 boundary change until #174 lands."*

**G9 as it now stands:** no BackdropPass **split** until #174 lands. Contract-① DTO work is **released**.

The Director asked what G9 was, was given the reasoning below, and authorised the narrowing.

The rationale G9 records is entirely about *splitting* — relocating code in ways that could reorder GL
state on paths the frozen harness cannot execute, because its camera never moves. A DTO swap moves no
code and changes no branch; it changes a parameter type. The in-frame oracles certify it. So the text
was broader than the risk it was written to prevent, and the narrowing is to the rationale, not away
from it.

**Still gated on #174, unchanged:** splitting `BackdropPass`, and any change that moves code across the
parallax-bypass or standalone-compose arms. Those are exactly the paths the harness is blind to, and the
gap has already produced one wrong measurement and one defect that reached the Director in play.

## Delta status

| # | axiom | state | where |
|---|---|---|---|
| 0 | A8 | **closed** | `da0125b2` — lint body committed |
| 1 | A8 | **partial** | script gates proven from a pristine clone; compiled tests not — #190 |
| 2 | A5 | **closed** | `2643b1ce` — gate asserts the build happened (#178) |
| 3 | A10 | **closed** | #178 filed; `render-harness` memory rule amended |
| 4 | A7 | open | #180 — clause-2 recovery is nominal under shipped defaults |
| 5 | A1 | open | #181 — `compose_recovered` in no gate verdict |
| 6 | A14 | open | #182 — ContentKey has no tests |
| 7 | A3 | **closed** | `8eddcb37` — every receiving file metered (#183) |
| 8 | A9 | open | #174 — motion harness; see the G9 narrowing above |
| 9 | A4 | **closed** | `c89be289` — counts generated and CI-gated (#179) |
| 10 | A4 | open | #188 — design spec still prescribes constructor injection |
| 11 | A12 | open | #189 — no `docs/render/` index |
| 12 | A13 | **closed** | #136 re-scoped to perceptual items only |
| 13 | A3 | **closed** | `252bed68` — consumption declared (#184) |
| 14 | A2 | open | #185 — four config declaration sites, `newLighting` in none |
| 15 | A6 | open | #187 — `NoAssets` rule still prompt-only |
| 16 | A1 | open | folded into #181 |
| 17 | A3 | **closed** | `252bed68` — dead includes, duplicated compose, false build claim (#186) |

## Guardrail status

**Mechanised** (no longer depends on anyone remembering): G3 → `render_docs_fresh`; G4 → the extended
`render_layering` ceilings; G8 → the gate's source-vs-binary check.
**Standing rules:** G2, G5, G7, G10.
**Open:** G1 (compiled half), G6 (#181).
**Narrowed:** G9, above.

## Corrections to this audit, found by reading the tree since

- **"BackdropPass concedes its own Law-of-One violation in its header" (A3) is overstated.** The header
  does not concede; it *enumerates* three owned concerns and then argues they are inseparable — both
  entry points share `m_cacheFrameBufferGeneration` and `m_envRefreshedThisFrame` across the lightmap
  phase, "which is exactly why they are one pass." The concern count is real and the split is a live
  question; "concedes" implies an admitted debt that the file does not admit.
- **The `GpuLightmapPass` → `LightmapPass` rename is struck, not deferred.** The audit carried it as
  cosmetic residual matching the docs' naming. `Gpu` is load-bearing: there is a live CPU lightmap path
  (`renderData.lightMap`, gated by `lightingGpu`; the pass's own header documents falling back to it).
  Dropping it would stop the type naming which of the two it is. The docs were corrected to the code.
- **`renderWorld` consumes FOUR inputs, not one** (delta 13). The audit found `entityDrawables`; the tree
  also has `backgroundOverlays`, `foregroundOverlays` and `nametags`, all via `drawDrawableSet`. That
  quadruples the finding's surface and is what makes it the *prerequisite* for contract ①, not a
  follow-up to it — `WorldInput` cannot be a `const&` view.
- **The layer table omitted the abstract `Renderer` interface**, which is why this instrument's L1 total
  and the published artifacts' disagreed by exactly its 340 lines. Claimed 2026-07-26.
- **Three files were in no layer at all** (`StarAnchorTypes`, `StarAssetTextureGroup`,
  `StarFontTextureGroup`). Claiming them raised the measured residual 15 → 17: two `Root::singleton()`
  reads in `StarAssetTextureGroup` that no count had ever included.
