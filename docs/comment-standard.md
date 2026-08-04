# The comment standard

## The rule

**A comment may not assert what no instrument can catch going false, and may not restate what the code
already says.** State the rule at the site and the derivation in the commit. Cite symbols, never line
numbers. Every measurement carries the task id that produced it.

This rule exists because it was broken, and the break was not cosmetic. `StarWorldClient.cpp` carried a
comment asserting that the light calculation region "is strictly inside the loaded-sector region … so no
unloaded sector is ever gathered." The arithmetic said otherwise: our own #127 lever rounds the light
window's *size* up to a 32-bucket anchored at the min corner, growing the region on the max side only,
while the needed-sector set stayed derived from the unbucketed window. That sentence was the only thing
standing between the code and a use-after-free (#210). It was defended by no assert and no test, and
nothing ever read it.

The same shape appeared twice more within a day. Three comments claimed a bad adaptive-border value
"costs performance, never correctness"; the border is in fact the membership test for off-region point
lights, and the estimator behind it measured reach from the channel mean while the engine used the
channel max — so saturated lights were being dropped from the frame (#217). A fourth stated the warrant
for leaving two lighting fields non-atomic as a *guard* condition, when the real warrant is thread
ownership; a reader trusting the stated rule could add a main-thread write and introduce a real race
(#218).

## What the rule is not

It is not a length limit, and it is not a cleanup mandate.

A twelve-agent survey of the 11,313 comment lines in our sources (excluding vendored gtest) found that
narration — comments that merely restate the adjacent code — is about **7%** of the corpus and about
**95% of it is upstream**, concentrated in the 2023-06-20 Chucklefish import. Restricted to the 5,549
comment lines this project authored, the detector returned **four** hits, three of them legitimate. We
do not have a verbosity problem in our own work.

The accuracy problem, by contrast, is entirely ours:

| | |
|---|---|
| `File.cpp:NNN` citation sites | 9, **100% authored by this project** — upstream has never used the form |
| …still resolving correctly | 5 of 11 targets — **55% rot** |
| …counting only files we actively edit | 3 of 9 — **67% rot** |
| Rot velocity | two refs added together on 2026-07-25 were correct at birth; **both wrong within ten days** |
| Measurement claims (%, µs, ×) | 102 on 76 lines — **none dated, none sourced** |

So the rule is scoped to **code we author or touch**, enforced at review. A tree-wide rewrite of
inherited comments would be ~700 lines of churn in code we do not maintain, for no benefit, against a
real merge-conflict cost — we still take upstream merges (#144). Comments that are actively *wrong* are
a different matter and get fixed wherever they live, because a false comment is a defect regardless of
who wrote it.

## What a comment may carry

Warrant the code cannot carry itself:

- **why this and not the obvious alternative** — name the alternative and the mechanism that defeats it
- **what was measured** — with the task id that produced the number
- **what breaks if you change it** — the hazard for the next editor
- **what is deliberately NOT guaranteed** — the non-promise
- **a cross-thread or lifetime contract no type expresses**

If a reader would get *nothing* wrong without the comment, delete it. If a reader would misread the
*code*, fix the code instead — rename the thing, extract the function, name the constant, write the
assert, or use a type that cannot hold the bad state.

## Placement

The **rule** goes at the site. The **derivation** goes in the commit message and on the task, where it is
discoverable but not in the reader's way.

This split is itself a correction. The PR-570 comparative analysis scored this project *down* on
simplicity for 55 lines of defect narrative at one fix site. Verbosity is not neutral; it is a cost we
have already been billed for once. When #210 was fixed, the first draft carried 56 lines of comment for
a five-line change and was cut to 21 — the lock rule and the do-not-add-this-guard hazard stayed at the
site, the arithmetic moved to the commit.

## The instrument

`scripts/comment-claims.py`, registered as the `comment_claims` gate, **bans `file:line` citations in
source comments**. It is a ban rather than a staleness checker deliberately: a checker would police a
population of nine, all ours, more than half already rotted. A line number is not a fact a comment can
hold — every edit above the target moves it, and nothing in the language, the build, or review connects
the two files. Discipline cannot fix that, so the form goes rather than the instances.

`comment_claims_fires` is its companion, on the same principle as `prose_claims_fires`: a green ban
proves nothing unless the scanner can see. It asserts 11 citation forms are caught and 8 legal forms are
not.

The one citation form that is genuinely self-verifying stays legal, and is the model — a commit hash
beside its quoted subject, as in `StarRenderer_opengl.hpp`:

```
upstream (36a389c6, "Fix HDR crash (#535)")
```

That checks in one line, with no false positives, because the hash carries its own text.

## What is deliberately not gated, and why

| Candidate | Status |
|---|---|
| `#NNN` task ids | **Not built.** 27 board ids cited, zero dangling — it would ship green. But five namespaces share the `#N` syntax (board tasks, Levers, audit rows, upstream PR numbers, plain prose). Needs a mandatory form first. |
| Dangling C++ symbols | **Not built.** 6 real danglers out of ~729 checked, but ~11 *deliberate* post-mortem references to deleted symbols would false-positive — they outnumber the real defects roughly 2:1. Needs a suppression convention. |
| Config / telemetry keys | **Not built.** Zero dangling today, and `config_declared` already covers the config half. |
| Measurement claims | **Not mechanisable.** Writing rule only: carry the task id that produced the number, so the board becomes the expiry record. |
| Comment-to-code ratio | **Rejected.** A ratio ratchet is satisfied by deleting a true comment — the measured thing and the edited thing are identical, so it is gameable in exactly the direction that destroys warrant. |

## The bar

Worked examples from this tree, not invented ones:

- **`StarWorldClient.cpp`, the `lightingCalc` containment block** — the repaired version of the comment
  that caused all this. It states the arithmetic that decides the safety property, marks which side of
  it fails, records that the previous text asserted the opposite, and hands the claim to a live counter
  (`lighting.gather.calc_outside_loaded`) instead of to prose. A reader cannot take it on faith.
- **`StarWorldClient.cpp`, the sector-unload lock** — the mutex's *name* says "lighting prep"; at that
  call site its job is object lifetime. It also pre-refutes the obvious optimisation with the reason it
  is wrong, and prices the lock so a future performance pass has the number.
- **`StarTelemetry.hpp`, the `MetricDomain` block** — names the obvious alternative (infer the domain
  from the recording thread) and the mechanism that defeats it (asynchronous GL query readback). The
  claim it makes is enforced by the compiler.

Each of these carries something no name, type, or assert could express. That is the test.
