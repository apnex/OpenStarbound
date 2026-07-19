# Render Layer 1 vs Vanilla — Comparative Assessment

> **For the canonical definition of render Layer 1 — its purpose, scope, intent, and acceptance bar — see
> [`layer1-architecture.md`](layer1-architecture.md).** This document is a point-in-time *comparative audit*:
> it measures the Layer-1 substrate against the vanilla/upstream equivalent across a fixed set of dimensions and
> records, for each, whether Layer 1 is genuinely superior — and, where a superiority claim failed, exactly what
> would make it valid.

**Produced 2026-07-18 by an 80-agent read-only workflow.** Six dimension-families were measured on hardware
sources; every drafted superiority claim then faced a three-lens skeptic panel (measurement / attribution /
significance), and two completeness critics hunted for overstatements and un-recorded debits. Every WOUNDED or
REFUTED verdict carries a **path-to-validity** — the concrete work or fact that would make the claim hold.

> **HEADLINE VERDICT: 9 CONFIRMED · 15 DOWNGRADED · 0 REFUTED.** Nothing was outright false — the *measurements*
> held everywhere. What downgraded 15 of 24 claims was **attribution or significance**, not fact: claims that
> borrowed the drama of the vs-upstream contrast to dress a much narrower vs-monolith move. This is the campaign's
> own meta-lesson — *a claim can be measured-true and still overreach* — landing on the assessment itself.

---

## 1. Method and baselines

**Primary baseline (structural): `03ebb98b^`** — the fork one commit before the render-surface extraction landed.
**Defect cross-check: `upstream/main` (`2c7f972b`)** — living vanilla.

**A baseline correction the agents forced, and it reshapes the whole comparison.** The brief assumed `03ebb98b^`
was a pre-decomposition god-object. It is not. At `03ebb98b^` the four components **already exist as nested private
classes** inside `OpenGlRenderer` (`GlFrameBuffer`@237, `GlTargets`@386, `GlPass`@435, `GlEffects`@501), with zero
`friend`, the borrow trio, `Maybe<Face>`, the single seal predicate, and the single allocator — and `GlLoneTexture`
was already split into `StarGlTexturePrimitives` one commit earlier (`50ea666c`). **The monolith baseline is a
*partially-decomposed nested* state. The pure god-object is upstream.** So "vanilla" means two different things, and
the honest register keeps them apart (§2).

**On "83 commits behind" (corrected here and in [`architecture-assessment.md`](architecture-assessment.md)):** that
figure was a mislabel. `c5b39f9a` (2026-07-14) *merged* 83 upstream commits (range `1012a5a..2ea33530`); the "83" is
the size of that merge, not a deficit. Current upstream `2c7f972b` is dated the same day — we are days, not 83
commits, behind. `git merge-base` reports no common ancestor only because the player-save purge rewrote our SHAs;
our content still descends from `2ea33530`.

The verdict rule: a claim is **REFUTED** if a lens shows its numbers wrong or vanilla shares the property;
**DOWNGRADED** if a lens wounds attribution or significance; **CONFIRMED** only if all lenses hold.

## 2. The finding that reframes everything: two comparisons, not one

| | **vs. living UPSTREAM** (the true god-object) | **vs. our own MONOLITH** (`03ebb98b^`, partially decomposed) |
|---|---|---|
| **Sovereign owners** | 5 vs **0** dedicated owner classes (3 of 5 facts are loose inline members) | 5 vs **5** already exist — the delta is *nested → top-level own-TU* |
| **Borrow/format sealing (RB-1/RB-6)** | upstream tracks **no borrow, records no format** — the bug class is fork-introduced | monolith has the trio but **public**; delta is the `m_owned` bit + private descriptor |
| **Frame-boundary cache (RB-7)** | `justSwapped` side-channel, **no invalidate at all** | `loadConfig`-only invalidate; delta is the **`startFrame` invalidate** |
| **Zero `friend`** | present in both | **present in both — not a delta** |
| **Byte-identity gate** | upstream ships **zero** render-correctness evidence | gate is **byte-identical in the monolith — not a delta** |

**The honest summary:**

- **Against living upstream, Layer 1's superiority is large, real, and multi-dimensional.** The god-object owns
  three of five render-surface facts as loose inline members; it has no borrow tracking, no recorded format, a
  `justSwapped` side-channel with no invalidate, public field-bags, and no automated correctness evidence of any
  kind. Every flagship claim is strong here.
- **Against our own monolith, the honest deltas are narrow and specific:** (1) module sovereignty
  (nested→top-level own-TU — Local-Reasoning, constructible-in-a-test capability); (2) the RB-1 `m_owned` bit;
  (3) the private storage descriptor (RB-6 raw-poke seat); (4) the `startFrame` frame-boundary `invalidate`
  (RB-7); (5) the effect-parameter type-ladder collapse; (6) RAII `Face`. **Several of these are §9-hardening
  moves independent of the code-motion** — they could have been made in the nested monolith. Crediting "the
  decomposition" for them overreaches, which is precisely why 15 claims downgraded.

## 3. The dimensions (18 axes, 6 families)

| # | Dimension | OURS | VANILLA |
|---|---|---|---|
| **I. Decomposition & Ownership** | | | |
| D1 | Sovereign owners (one no-"and" owner class per fact) | 5 top-level | monolith 5 *nested*; upstream **0** dedicated (2 field-bags) |
| D2 | Writer cardinality per RB fact | 1 writer each (RB-6 `recordStorage`; RB-1 `m_owned`) | monolith RB-6 = **5** funcs on a public field; upstream RB-1/5/6 absent |
| D3 | God-object mass **by concern** | 1146-line module; renderer 2263→1652 | monolith all inline; **net lines rose +231** (see DB-1) |
| D4 | Duplication (predicate/allocator/type-ladder) | 0 inline copies | monolith **1 of 3** items dup (type ladder); upstream **3 of 3** |
| **II. Encapsulation & Air-Gap** | | | |
| D5 | Reach-through seats (`friend` + raw pokes) | 0 friend, **0** raw descriptor pokes | monolith 0 friend, **11** pokes / 6 funcs; upstream no format field |
| D6 | Publicly-mutable descriptor/borrow seats | **0** | monolith **4**; upstream **2** |
| D7 | Renderer contract width vs. need | **byte-identical** across the decomposition | **+16 virtuals *wider* than upstream** (fork features) — a debit |
| **III. Defect surface (close-by-construction)** | | | |
| D8 | RB-1/5/6/7 representable? | unrepresentable (private + chokepoint + RAII) | monolith: public/name-derived; upstream: bug class absent or side-channel |
| D9 | By-construction vs by-care | mostly by-construction + **1 irreducible by-care residue** | monolith by-care; upstream open |
| D10 | Failure-mode inventory (leak / stale / desync) | all closed | monolith leaks FBO on throw, RB-7 open; upstream leaks `altId` every teardown |
| **IV. Evolvability** | | | |
| D11 | Change-locality (add-face / resize / add-format) | add-format **1 site** | monolith add-face/resize **tie**; upstream 3–4 sites |
| D12 | Testability-in-isolation | *possible, unrealized* (0 unit tests) | monolith impossible |
| D13 | Composability for L2/L3 | composition over primitives; **RetainedSurface not built** | monolith hand-rolled; composite() predates extraction |
| D14 | Reasoning-locality | module compiles without the 1652-line renderer | monolith/upstream nested in `OpenGlRenderer` |
| **V. Verification rigor** | | | |
| D15 | Correctness evidence | hardware byte-identity gate + 3 oracles | upstream **none**; monolith **byte-identical gate (not a delta)** |
| **VI. Debits (where vanilla wins)** | | | |
| D16 | Indirection cost | +4 files, +231 lines, **zero added hot-path hops** | monolith one inline TU |
| D17 | Upstream-merge divergence | permanent per-merge reconciliation tax (~4×/yr) | upstream layout is the merge target |
| D18 | Residual by-care | the RB-5/RB-7 `loadConfig` teardown companions | present identically in monolith |

## 4. The superiority-claims register

### 4.1 CONFIRMED (9) — survived all lenses

| ID | Dim | Claim (compressed) | Note |
|---|---|---|---|
| **C-I-1** | D2 | RB-6 format record: public field / 5 writers → **1 private writer**; RB-1 write-guard: name-derived → dedicated **`m_owned` bit**. No second mutator can leave the record stale. | The strongest structural win. Cost: co-location still by convention. |
| **C-I-3** | D2 | RB-7 bind cache dropped at the frame boundary (`startFrame` invalidate), closing staleness both baselines leave open. | Critic: "at *every* site" is overstated — it's drop-**or**-restore, invalidate at exactly 2 sites. |
| **C-II-2** | D5 | Storage descriptor: **11 raw cross-object pokes / 6 funcs → 0**, all collapsed into `recordStorage`/`setAllocatedSize`. | The mechanism under C-II-1/C-I-1. |
| **C-II-3** | D5 | "Zero `friend`" is real — **but explicitly flagged NOT a delta**: monolith and upstream also have zero. | A confirmed *non-superiority*. Do not claim friend-count over the monolith. |
| **C-III-1** | D8 | RB-6 raw-field-poke seat unrepresentable (private `textureSize`+`internalFormat`) vs. monolith's 5 public writers / upstream's no-format. | Critic: "desync unrepresentable" overreaches — only the *raw-poke seat* is compiler-closed; full co-location still by care. |
| **C-III-3** | D8 | RB-1 borrow-pair desync unrepresentable — trio sealed private behind three atomic acts. | vs monolith's public `borrowedFrom`/`textureValue`; upstream tracks no borrow. |
| **C-V-3** | D15 | The gate/oracle is **NOT** a Layer-1 artifact (byte-identical in the monolith); Layer-1's real D15 contribution is that the extraction re-passed it at DIFF=0. | Honest deflation of C-V-1. |
| **C-V-4** | D15 | The correctness envelope has a hard boundary: AA/multisample path un-gated (RB-5's home), one frozen config, no unit tests. | This claim *is* the D15 debit ledger. |
| **C-VI-4** | D19 | The 394-line header converts rationale into a correctness surface that must stay true — a maintenance debit vanilla's terse structs never incur (a confirmed *debit*). | Readability/maintenance, not a mechanical failure. |

**Of the 9, the genuinely strong *superiority* wins are five** (C-I-1, C-I-3, C-II-2, C-III-1, C-III-3) — all in
the defect-surface / encapsulation families, all attributable to the §9 sealing pass. The other four are an
honest-deflation claim, a debit-ledger claim, a flagged non-delta, and a confirmed debit.

### 4.2 DOWNGRADED (15) — measured-true but over-attributed or over-significant

The recurring wound is the same in almost every case: **the property is real, but it is a monolith property (prior
fork work) or a §9-hardening move, not a consequence of the four-component extraction** — and its magnitude was
borrowed from the vs-upstream contrast. The full path-to-validity for each is §5.

| ID | Dim | Why downgraded (one line) |
|---|---|---|
| C-I-2 | D1 | "Every fact has one top-level owner with private state" — `GlPass::effect`/`target` are **public**; owner classes + zero friend are **pre-existing monolith** properties (axiom restatement vs primary). |
| C-I-4 | D4 | Type-ladder collapse is a real DRY win but **separable from the extraction** (lives in top-level `loadEffectConfig`); `parameterType` is a public field — by-absence, not A8. |
| C-II-1 | D6 | 4/2→0 public seats is real, but 4→0 leans on the **fork-introduced** descriptor facts; the clean fork-independent delta is 2→0. Closure is the §9 seal, which the extraction *enables* not *causes*. |
| C-II-4 | D7 | Contract is byte-identical across the decomposition (**zero narrowing credit**) and +16 virtuals *wider* than upstream — a debit, not a credit; the "48 virtuals" count miscounts one comment line. |
| C-III-2 | D10a | RAII-`Face` FBO-leak fix is real but from hardening commit `bab5a7cf`, **independent of code-motion**; the happy path is byte-identical; manifests only on a rare GL-incomplete throw. |
| C-III-4 | D8/D9 | RB-7 frame-boundary closure is **by-care** (a hand-placed `invalidate`), not "by construction"; and the delta is a baseline-snapshot artifact (`03ebb98b^` predates `d60a2dba`). |
| C-IV-1 | D14 | Reasoning-locality is real vs both baselines but is **axiom-compliance, not a prevented bug**; "zero refs to `OpenGlRenderer`" is a snapshot, not build-enforced; whole-TU line counts mixed baselines. |
| C-IV-2 | D11 | Single-site add-format is a **2→1 win vs monolith** (dramatic only vs upstream); the seal is on the fields, `recordStorage` is public; co-location still by convention. |
| C-IV-3 | D13 | Composition-over-primitives is **byte-identical in the monolith**; `composite()` is a fork feature; the Layer-2 `RetainedSurface` **is not built** — only the top-level-type *availability* is the real delta. |
| C-IV-4 | D12 | Constructible-in-isolation is a **latent, unrealized** capability — no unit test exists, `star_application` isn't linked into the test targets. |
| C-V-1 | D15 | The gate certifies fork-vs-vanilla rigor, **not** a decomposition artifact (byte-identical in the monolith). |
| C-V-2 | D15 | The gate's honest-by-construction property is a **fork shell script**, identical across the extraction boundary — not decomposition-attributable. |
| C-VI-1 | D17 | Merge-tax real but the "7× undercount" over-counts: git only 3-way-merges the two shared files; the 4 new files auto-add. Restate as ~2×. |
| C-VI-2 | D18 | The by-care residue is **~1 unfoldable call** (`invalidate`), not "three a god-object folds into one"; `rebindBorrows` is data-dependency-forced in any decomposition; `destroyAll` *does* notify retained surfaces. |
| C-VI-3 | D16 | Indirection is organizational (zero added hot-path hops — **refutes the "abstraction slows the draw path" charge**), but the file/line accounting mixed baselines; one per-sampler accessor hop is inlined-away, not "byte-identical". |

### 4.3 REFUTED (0)

No claim was outright false. The measurement lens confirmed every number (with minor citation nits). The value of
the exercise is entirely in the **downgrades and the debit discoveries** — the register is honest because it
refuses to state the 15 downgraded claims at the strength their authors first gave them.

## 5. Conditions-for-validity ledger (the actionable backlog)

For every DOWNGRADED claim the skeptic recorded what would flip it to full strength. They cluster into **six
concrete engineering moves** — this is the forward backlog the assessment produced:

1. **Make storage-spec-without-record unrepresentable.** A `GlLoneTexture`-owned upload method (glTex\*Image2D +
   record as one inseparable act, `recordStorage` made private) — upgrades **C-I-1, C-II-1, C-III-1, C-IV-2**
   (the RB-6 co-location residue). Blocked at DIFF=0 today by the shared atlas caller + `glPixelStorei`
   co-location; a real design task, not a tweak.
2. **Privatize the bind pair.** Make `GlPass::effect`/`GlPass::target` private behind a `boundTarget()` accessor +
   an explicit rebind/clear method, routing oracle-restore and `loadConfig` teardown through the interface —
   upgrades **C-I-2** and closes Critic-1 debit #1 (the raw `effect` alias).
3. **Build the Layer-2 `RetainedSurface`.** A first-class retained-cache surface holding a top-level
   `GlFrameBuffer` directly, with the env/parallax refresh-gate policy lifted out of `WorldPainter` — upgrades
   **C-IV-3** (and is the RS-0 forward design, task #145).
4. **Write component unit tests.** Link `star_application` into a test target (or split pure CPU-side state from
   the GL calls) and assert the RB-1/5/6/7 invariants directly — upgrades **C-IV-4, C-V-4**; the cheapest of the six.
5. **Earn "by construction" for the frame boundary.** Route the frame-boundary FBO-0 reset through a `GlPass`
   primitive (e.g. `GlPass::unbind()` / a frame-scope RAII token) so no deletable `invalidate()` exists to
   forget — upgrades **C-I-3, C-III-4**.
6. **Add a build-time layering lint.** A CI grep/architecture gate that fails the build if the module ever names
   `OpenGlRenderer` — converts C-IV-1's "zero refs" snapshot into a close-by-construction guarantee.

Plus two register-hygiene fixes the critics require: correct the Renderer-contract count to **+15 virtuals wider
than upstream** (the "48/+16" miscounts one comment line — **C-II-4**), and restate the merge-tax multiplier as
~2× (**C-VI-1**).

## 6. The debit ledger

### 6.1 Measured debits (from the hostile-critic family)

- **DB-1 — Net lines rose (+231).** 2897 → 3128. The decomposition *concentrates* the concern; it does not shrink
  the code. Raw TU size is a lying metric (the fork's renderer carries an oracle + retained caches + 4 fork-only
  effect-texture setters upstream lacks — it is *larger* than upstream's 1379-line god-object).
- **DB-3 — Irreducible by-care residue.** The `loadConfig` teardown companions (`rebindBorrows` + `invalidate`) —
  present identically in the monolith, so **not a delta OURS won**; forced by A3's Air-Gap.
- **DB-4 — RB-6 co-location by convention.** The raw-poke seat is compiler-closed; "recorded beside the spec" is
  still by care (move #1 above would close it).
- **DB-5 — The baseline is pre-hardened.** The reason D1/D4/D15 and much of D5/D6 are much stronger vs upstream
  than vs the monolith.
- **DB-6 — No unit tests; single-config oracle.** Correctness rests on the render gate, certified for one config
  (AA off, HDR on); the AA path — RB-5's home — is verified by reasoning, not exercised.

### 6.2 Critic-surfaced debits — adversarially verified (2026-07-18)

These four were found by the completeness critic and did **not** go through the original skeptic panel, so each
faced a dedicated 3-lens read-only verification (code-accuracy / defend-the-code / attribution + doc-reconciliation,
16 agents). **Verdict: 0 confirmed as new decomposition defects — 2 REFUTED, 2 QUALIFIED-narrow. None requires any
change to `layer1-architecture.md`** (the canonical doc was vindicated). The exercise caught the *completeness critic*
overreaching the same way the critic caught the original claims — "a finding is a claim that can be wrong" held one
level up.

- **NEW-1 — `GlPass.effect` is a raw unmanaged alias into `GlEffects`' `m_byName`. → QUALIFIED (real but narrow).**
  The alias is real, but its two load-bearing claims are false: it is **pre-existing** (the monolith's
  `m_currentEffect` was the identical bare pointer into `m_effects`, same ~11 interior reaches, same public-interface
  handle cast), **not** a Law-of-One or Air-Gap breach (`m_byName` stays sealed; `Effect&` is Earned Exposure), and
  the alleged dangle is not reachable (the only mutator, `load()`, is followed by an unconditional rebind). What
  survives is a genuine *by-care* lifetime coupling invisible to the friend metric — and it is **reducible**: making
  `GlPass::effect` a RefPtr borrow closes it by construction (**backlog item 4**). *(hpp:303, 349-356; cpp:496, 559;
  renderer cpp:245.)*
- **NEW-2 — Three-way GPU-resource ownership / Law-of-One violated. → REFUTED (3/3 lenses NOT_REAL).** The critic
  redefined Law-of-One as "one RefPtr per resource *lifetime*" and found *that* violated — the exact conflation the
  verification was told to reject. The canonical Law-of-One is "one **writer** per descriptor **fact**," which holds.
  "`destroyAll` orphans the storage" is **false as a leak**: `loadConfig` drops all three refs in the *same call*
  (`invalidate`→pass, `destroyAll`→registry+FBOs, `rebindBorrows`→last sampler ref), and the RefPtr frees on
  last-drop — a re-point, not a plugged leak. The shared-RefPtr graph is **verbatim in the pre-split monolith**
  (`90169603`), where `loadConfig` was `m_frameBuffers.clear()` with no re-point — the actual live RB-5/RB-7 bug. **The
  decomposition added the *cure*, not the hazard.** `layer1-architecture.md` §4.2 item 3 is correct as written.
- **NEW-3 — Source boundary, not a compile firewall. → REFUTED (category error).** Build-time decoupling was never a
  Layer-1 goal; §6 correctly assigns the compile firewall to the `Renderer` interface (which pulls no GL headers).
  And the property is pre-existing — the nested monolith already had all fields in a GL-including header. Fails all
  three debit tests (against a stated goal / introduced by the decomposition / undisclosed).
- **NEW-4 — Cross-file locality + same-named forwarder. → QUALIFIED (real but narrow).** The facts hold, but the
  forwarder is **intended-by-design** and already disclosed (`layer1-architecture.md` Appendix A rejects deleting it
  as churn; the renderer's single door injects `m_screenSize` across the Air-Gap), and the boundary-span half is the
  **generic tax of any module split** with zero correctness weight — the cost-side counterweight to the D14/D16
  credits, not a standalone defect.

## 7. Honest verdict

**Layer 1 is decisively superior to living upstream** across ownership, defect surface, encapsulation, and
verification rigor — the god-object owns three of five facts inline, has no borrow/format tracking, a
`justSwapped` side-channel, public field-bags, and zero correctness evidence. If the comparator is "what a vanilla
OpenStarbound user runs," the superiority is real and large.

**Against our own pre-hardened monolith, the honest superiority is narrow and specific:** module sovereignty plus
five sealing/hardening moves, of which the genuinely load-bearing wins are the RB-1 `m_owned` bit, the private RB-6
descriptor, and the RB-7 frame-boundary invalidate. Much of what reads as "Layer-1 superiority" — owner classes,
zero friend, the gate, `composite()`, the borrow trio — is **prior fork work the decomposition inherited**, not
something the four-component split created.

**Three things this audit changes going forward:**
1. **Stop claiming the monolith-shared properties as decomposition wins.** State the vs-upstream contrast and the
   vs-monolith delta separately (§2), every time.
2. **The by-care teardown residue is genuinely axiom-forced, not a hidden hazard.** The completeness critic's
   NEW-2 claim — that it symptomised a three-way-ownership defect the decomposition introduced — was adversarially
   **REFUTED** (§6.2): the shared RefPtr is a correct-by-design borrow model that frees on last-drop, `destroyAll`
   does not leak, and the graph pre-dates the extraction. The residue stands as A3-Air-Gap-forced, exactly as
   `layer1-architecture.md` §4.2 item 3 states.
3. **The six path-to-validity moves (§5) are the forward backlog** — the concrete work that would convert the 15
   downgraded claims into full-strength ones, and most of it (moves 1, 2, 3) is the RS-0 `Surface` design's job.

**Caveat on every "upstream has X" statement:** verified against `upstream/main` `2c7f972b` (2026-07-14), which is
current within days — not the stale fork-point the older assessment assumed.
