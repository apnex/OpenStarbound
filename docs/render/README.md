# `docs/render/` — what is in here, and which of it you can trust

Seven files, ~1700 lines, written across six weeks of the render campaign. Until 2026-07-26 there was no
index and the two doc chains referenced each other **zero** times, so a cold agent's reading order was
arbitrary — and the arbitrary order routed through whichever file happened to be stale. That is not a
tidiness complaint: it is the mechanism that turns a stale prescription into a wrong action. Fixing the
content without fixing the reading order fixes nothing.

## Read in this order

1. **`architecture-3-target-state.md`** — where the subsystem is *now*. Start here.
2. **`axiom-alignment-audit.md`** — what is wrong with it, and which guardrails constrain the next change.
   Read before proposing architectural work; commit messages cite its guardrails by number (G1…G10).
3. **`layer1-architecture.md`** — the L1 substrate in depth, if you are touching GL.
4. The rest are history and point-in-time assessments. Read them for *why*, never for *what is true now*.

## The files

| file | role | freshness basis |
|---|---|---|
| `architecture-3-target-state.md` | **canonical** — current state, layers, Air-Gap compliance | **generator-backed.** Its counts come from `scripts/render-inventory.py --inject` and are CI-gated by the `render_docs_fresh` ctest. Prose is hand-written; every *current-state number* is generated |
| `axiom-alignment-audit.md` | **canonical** — the M7 audit, its 10 guardrails, and delta status | body generated verbatim from the audit run; the *Decisions and status* section is hand-maintained and dated |
| `game-render-boundary.md` | **canonical** — what `star_game` carries that only a renderer wants, and why a headless *client* does not exist while a headless *server* ships | **generator-backed.** Counts come from `scripts/boundary-inventory.py --inject`, gated by `boundary_fresh`; the ceiling is gated by `boundary_ratchet` |
| `layer1-architecture.md` | **canonical for L1** — the sovereign render-surface substrate | hand-written, last verified 2026-07-25. Enforced in part by the `layer1_layering` ctest |
| `architecture-1-vanilla-baseline.md` | **panel** — upstream `2ea33530`, before the campaign | pinned to a fixed commit; cannot go stale |
| `architecture-2-accreted-monolith.md` | **panel** — the fork at branch point `75d29067` | pinned to a fixed commit. Its *"what each concern becomes"* column tracks live work and is dated |
| `architecture-assessment.md` | **point-in-time**, 2026-07-19 | ⚠ not maintained. Historical reasoning only |
| `layer1-vs-vanilla-assessment.md` | **point-in-time**, 2026-07-19 | ⚠ not maintained, and **contains at least one claim now false** — see below |

## Known-stale content, named rather than silently left

- **`layer1-vs-vanilla-assessment.md` rows D13 / C-IV-3 say `RetainedSurface` "is not built".** It is: it
  shipped as L2, has two consumers (env + parallax caches), a compile-enforced no-`Renderer` fence, and 15
  off-GPU unit tests in `core_tests`. The rows were true on 2026-07-19 and are not corrected in place
  because that file is a dated assessment, not a living document — this line is the correction.
- **`architecture-assessment.md`** predates the decomposition entirely. Treat every structural claim in it
  as a description of the monolith.

## The rule that keeps this set honest

**No document here may state a current-state number that `scripts/render-inventory.py` can measure.**
Generate it or leave it out. Historical numbers (876, 112, 589) are fine — they describe past states no
instrument can measure and no drift can falsify.

This rule exists because it was broken: the target-state doc claimed `BackdropPass` held 7
`Root::singleton()` reads against a tree measuring 0, and a `WorldPainter` line count 7% low, *within a day
of being written*. Measuring beside the doc was not enough — a measurement a human pastes is hand-typed
again the instant it lands. Hence `--inject` and the CI gate.

## Related, outside this directory

- **`docs/architecture/system-boundaries.md` — read this FIRST if you are new.** It is the whole-system
  map: the six top-level parts, the compile-enforced tier lattice, and the four tests that decide whether
  a boundary is real. Everything in *this* directory describes one tier of one of those six parts. It
  also carries the finding that reframes the L1 work: `source/application` reads zero globals because its
  grant list cannot see `Root`, so L1's sovereignty is the narrower, hand-enforced claim about an
  *internal* split — which is exactly why `layering-lint.py` has to exist.

- `docs/superpowers/specs/2026-07-19-render-decomposition-design.md` — the **Director-approved design
  authority** for the decomposition. §3 carries a correction banner (2026-07-26): its constructor-injection
  prescription is retracted, because implementing it would freeze the live-tunable console knobs.
- `docs/superpowers/specs/2026-07-14-render-surface-subsystem-design.md` — the L1/L2 design it refines.
- `docs/board.md` — every task id cited in commit messages, regenerated by `scripts/board-export.py`.
- `scripts/render-inventory.py`, `scripts/render-gate.sh`, `scripts/layering-lint.py` — the instruments
  these documents defer to. When a doc and an instrument disagree, the instrument is right.
