# HANDOVER — Target State System Architecture (#204)

**Describes commit `21ba119a` on branch `integration`.**

This document is point-in-time on purpose, which is what separates it from the spec it describes:
`docs/superpowers/specs/2026-08-01-target-state-system-architecture.md` is a TARGET STATE and
carries no dates, and this is the note that says where the work stopped. Detect staleness by
comparing the SHA above to `HEAD`:

```bash
git log --oneline 21ba119a..HEAD -- docs/superpowers/specs/ scripts/
```

If that prints nothing, this file is current. If it prints commits, read them — do not trust the
figures below over the instruments that generate them.

**Nothing here is a source of truth.** Every number in this file can be recomputed, and the command
that recomputes it is given beside it. That is the rule the whole task now runs on and the reason
the last two commits exist.

---

## 1. Sixty-second orientation

The spec is a whole-system target-state architecture for the OpenStarbound fork: a register of
COMPONENTS, each with a KIND, a ZONE, a duty, a warrant and its contents, plus a grant table saying
which components may name which. Around it sits a set of gates that check the document against
itself and against the tree.

The live thread is **dissolving `platform`**: a component that grouped four vendor services because
they arrive from one supplier rather than because they do one thing. It is being replaced by one
contract per duty. Two of five stages are done.

```bash
scripts/ci/run-gates.sh          # the whole gate set. Green IS the gate. Read $?, never pipe.
python3 scripts/spec-model.py    # component / grant / element counts, straight from the register
```

---

## 2. State at `21ba119a`

| | value | recompute with |
|---|---|---|
| branch | `integration`, pushed to `origin` | `git rev-parse --abbrev-ref HEAD` |
| gates | 21 of 21 green | `scripts/ci/run-gates.sh; echo $?` |
| components | 52 | `python3 scripts/spec-model.py` |
| grant rows | 50 | same |
| elements | 24 | same |
| derivations | 52/52 components, 312/312 facets | `python3 scripts/spec-derivations.py --check` |
| tables | 166 — 58 read, 6 generated, 102 unread | `python3 scripts/table-census.py --check` |
| open `owes` | 14 of 52 components | the generated ledger at the end of Section 16 |

The task board is exported to `docs/board.md` by `scripts/board-export.py` — that is how a `[#NNN]`
in a commit message resolves without the session store. Regenerate it before finishing a session.

---

## 3. The live thread: dissolving `platform`

**The finding.** `platform` was one INTERFACE over `DesktopService`, `P2PNetworkingService`,
`StatisticsService` and `UserGeneratedContentService`. Those four are together because **Steam sells
them in one SDK**, which is filing by supplier. A composition that wanted achievements therefore
also linked a socket. `source/platform/` already holds four separate contract headers — the split
was in the tree before it was in the register, the same shape the `celestial` split produced.

**Director's standing selection:** *"Refile by duty, dissolve `platform`."*

| stage | contract | vendor backend | null backend | state |
|---|---|---|---|---|
| 1 | `transport_p2p` → the existing `transport` | — | — | **done** (`60a66f02`, completed in `21ba119a`) |
| 2 | `statistics` | `statistics_steam` | `statistics_null` | **done** (`21ba119a`) |
| 3 | `ugc` | `ugc_steam` | `ugc_null` | not started |
| 4 | `desktop` | `desktop_steam` | `desktop_null` | not started |
| 5 | `platform` and `platform_null` deleted; `platform_pc` **renamed `vendor`** | | | not started |

Target is roughly 56 components. It is not a goal — per the Director, *"the number of components
will be complete when our architecture decides it is."*

### 3.1 The decision that changed the plan

Splitting `platform_pc` into per-duty vendor backends leaves the **shared vendor session** unowned.
`PcPlatformServicesState` holds Steam availability, the Discord core and its event pump, and the
`overlayActive` flag — and all four services are constructed from it:

```cpp
services->m_state = make_shared<PcPlatformServicesState>();
services->m_statisticsService = make_shared<SteamStatisticsService>(services->m_state);
```

It cannot be per-backend: `SteamAPI_Init` is process-global and there is one Discord event pump.
`overlayActive` is a fifth duty that is none of the four services — the **host** reads it, for
cursor visibility (`source/application/StarMainApplication_sdl.cpp`).

**Decided:** `platform_pc` is **not** deleted at stage 5. It becomes `vendor`, duty *"the vendor
session"*. Recorded in its `owes` facet.

**Deliberately not applied yet.** Renaming it while it still holds `SteamDesktopService` and
`SteamUserGeneratedContentService` would be a row whose name lies for two stages. The rename lands
at stage 4, when Desktop leaves and the session is all that remains. **If a future session prefers
the name to land earlier and carry the discrepancy in `owes` instead, that is a Director call, not
a default.**

### 3.2 What stage 3 has to touch

Stage 2's diff is the template. Every one of these needed editing, and the gates named all of them:

1. register rows in Section 9 — the contract, both backends, and `platform`'s shrinking contents
2. grant rows — the contract, both backends, plus `host`, `host_sdl`, `host_null`, `game`, and the
   three server-side entrypoints that must name a null backend because they reach the contract
3. Section 10 derivations — three new at the six-facet depth standard, plus corrections to
   `platform`, `platform_pc`, `platform_null`
4. the compile projection diagram in Section 4 — nodes, `==>` implements edges, grant edges, and the
   `class ... kBackend` / `kContract` lines
5. `ELEMENT_FREE` in `scripts/spec-consistency.py` — a BACKEND with no runtime element must say what
   drives it or declare that nothing does
6. `excludes` facets of every component whose grants changed
7. the prose tally in Section 9 (`Fifty-two components: nine INTERFACEs, …`)
8. `scripts/spec-measures.py --inject`, `scripts/spec-derivations.py --inject`,
   `scripts/tree-map.py --inject`, `scripts/composition-graphs.py --inject`

Closure figures do **not** need hand-editing — see §4.

---

## 4. The instruments, and why they exist

`prose_claims` had a structural blind spot: `COUNT_DRIFT` validates the **denominator** of an
`N of M` and skips the numerator as "a subset, not the register total". So `39` — never followed by
the word *components* — was read by nothing, across twelve sentences and two tables.

Four defects were found, all green through 21 of 21 gates (`5d276333`):

- Section 7's ZONE tally summed to **45** beside a 49-row register
- Section 12 carried **two** independent tables reading `32 of 41` / `26 of 41` / `19 of 41` — the
  41-component era, two register generations stale, one of them with the correct figures in the
  prose directly beneath it
- `world_sim` "linking 12 of 49", contradicting the generated diagram above it
- the subtraction claim `client_agent` = `client_headless` minus "three grants" — it is **five**,
  and those five remove **nine** components

Added, in `5d276333`:

| instrument | what it owns |
|---|---|
| `tree-map.py#zones` | Section 7's ZONE tally, now a generated block |
| `CLOSURE_DRIFT` | every composition size. Strict under a `### <entrypoint>` heading or in a table row naming one; "must equal SOME closure" elsewhere, because sentences legitimately quote another composition's number as comparison |
| `MODAL_ZONE` | the one `N of M components` that is not a closure — how far ZONE is determined by KIND |
| `SUBTRACTION` | `X is exactly Y minus N grants` against the grant table **and** against what the two closures differ by, which is a different number |

`ZONE_FACES` now lives once in `scripts/spec-model.py`; Section 7's table and the composition
diagrams' subgraph titles were two hand-written answers to the same question, drifted apart on three
of four.

**They paid for themselves in stage 2.** Every closure moved — `client_opengl` 39→41,
`client_headless` 33→35, `client_agent` 24→26, `server` 18→20, `world_sim` 13→15, `world_gen`
12→14 — and the gate named each one instead of leaving 17 sites to be recounted by hand.

### 4.1 Three traps a future session will hit again

1. **Machine-read cells must not carry backticked prose.** `transport_p2p`'s `excludes` said "for
   the reason `statistics_steam` names it", and `spec-derivations` read that as a claimed grant.
2. **Placement is WIRING.** Writing "the Discord core and its event *thread*" into a contents cell
   fires `PLACEMENT` — a component owns a pump; what thread it runs on is not its property.
3. **A self-test must not be keyed on the numbers it mutates.** `MODAL_ZONE`'s drive did
   `text.replace("15 of 49 …")`; when the register reached 52 the replace became a no-op and the
   verdict went **silent while reporting nothing wrong**. It is derived by regex now.

---

## 5. Standing constraints

These are not preferences. Each cost something to learn.

- **Never `git add -A` or `git add .`** — `.gitignore` once swept a 314 MB harness and a player save
  toward a public branch. Name every file. Verify with `git diff --cached --stat`, never
  `git status`. Never pipe `git add` through `2>/dev/null`.
- **`scripts/ci/run-gates.sh` green IS the gate.** CI is not part of how this project works: never
  push to check it, never poll it, never run `gh`. Prove fixes locally by construction.
- **Run the whole gate set**, never the subset you edited. **Do not pipe the runner** — a pipeline's
  exit status is the last command's, and reporting green off a piped run has happened here.
- **Push to `origin` only.** `upstream` push is disabled.
- **Use the Edit tool, not `sed`/`perl`,** for document changes; single-purpose Bash otherwise.
- **All builds E-core pinned:**
  `VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang -j 8`.
  An unpinned run once hit 105 °C. Director must be out of the game first — check `pgrep -x
  starbound`, not `pgrep -f`.
- **Commit messages end with `[#<task>]`.**
- **D7:** this is a target-state architecture. Current code warrants and informs; it does not bind.
- **A target state has no dates** outside Section 17, which is exempt by name.
- **CAPITALISE defined terms** — KIND, ZONE, ALTITUDE, COMPONENT, ELEMENT — where the word is the
  literal term rather than English.
- **Approval is aggregate only.** The Director does not read spec markdown: surface
  director-critical decisions inline, in chat, and follow Claude's lean otherwise.
- **Verification workflows are read-only** — spawn `Explore`-type agents with no Edit/Write; Claude
  writes the code.

---

## 6. Open, and honestly open

**14 of 52 components record an unresolved item** in their `owes` facet. Ratification requires that
ledger to be empty. It is generated at the end of Section 16 and cannot drift from the entries that
own it. As of `21ba119a` the components carrying one are:

`audio`, `base`, `colocation`, `content`, `gpu`, `platform`, `platform_pc`, `sound`,
`starmap_participant`, `statistics`, `storage`, `transcript`, `transport_p2p`, `universe`

Four are directly on this thread, and each is a question the decomposition raised rather than one it
inherited:

- **`platform`** — whether it should exist at all. Its duty is now "the contracts not yet named",
  which is honest and is not good: a component whose duty is *the remainder* is a queue.
- **`platform_pc`** — its name. Answered in principle (§3.1), applied at stage 4.
- **`statistics`** — whether a report that cannot fail is honest. `setStat` returns `false` both for
  "this store does not track that" and for "there is no store". If a caller can tell those apart it
  falsifies `statistics_null`; if it cannot, it may be silently dropping what it meant to record.
- **`transport_p2p`** — whether join brokering is one duty with carrying. `transport_tcp` finds its
  peer by being handed an address. If these separate, that is two components and the vendor half is
  a discovery service.

Beyond the decomposition, from earlier passes and not yet done:

- **#208** — register fields; re-home the anchoring gates; aggregate review
- ~90 unverified findings filed in `docs/superpowers/drafts/` (a levelling analysis and a Section 7
  purity audit). **Filed, not triaged** — they are candidate work, not accepted defects.
- rule-purity sweep: roughly 40 cells still mix measurement or commentary into a rule or definition
  cell, ~10 of them inside the machine-read `components` / `grants` tables
- Section 16's risk facets are read by no instrument and have no `history` facet
- Section 1 carries no `TABLE:` markers
- `spec-derivations`' heading rule can mistake a discussion heading for a derivation — noted in
  `scripts/table-census.py`'s docstring, harmless today because such headings carry no facet rows

---

## 7. Resuming

```bash
cd /root/frackin/OpenStarbound
git log --oneline 21ba119a..HEAD          # what changed since this note was written
scripts/ci/run-gates.sh; echo "exit $?"   # expect 21/21 and exit 0 before touching anything
```

Then either continue at **stage 3 (`ugc`)** using §3.2 as the checklist, or pick up **#208**. Both
follow the Director's standing selection; neither has been started.

Related records: `docs/board.md` (task ids), `docs/architecture/system-boundaries.md`,
`docs/render/README.md`.
