#!/usr/bin/env python3
"""THE GAME<->RENDER BOUNDARY INVENTORY.

It answers one question with a number: how much VIEW-SHAPED surface does the game layer carry?

WHY THIS EXISTS, and what it is NOT for. `starbound_server` already runs headless: it links
star_extern + star_core + star_base + star_game and nothing else -- no star_rendering, no star_application,
no star_windowing, no star_frontend -- and CI assembles and ships it. Zero files in source/game reference
Renderer, OpenGl, GL_ or glew. The graphics API genuinely does not exist below the render layer, and
RenderCallback (source/game/StarEntityRendering.hpp) is an abstract sink the game layer defines and the
render layer implements. The dependency arrow already points the right way.

So this instrument is NOT here to prove headless is possible. It is here because the CLIENT has no headless
expression: WorldClient unconditionally produces WorldRenderData, every Entity carries render() /
renderLightSources() / destroy(RenderCallback*) vtable slots a dedicated server never calls, and view code
like TileDrawer lives in the sim library. None of that blocks a headless server; all of it blocks a headless
CLIENT, and all of it is invisible until someone counts it.

MEASURE BEFORE MOVING. The L3 decomposition learned this the hard way (#183): a ratchet that metered three
of seventeen coupling sites was satisfiable by RELOCATION, and had already been satisfied that way -- eight
reads moved from a pass into an unmetered orchestrator and the gate scored a perfect result for a net change
of zero. Ceilings have to exist before the code moves, or "better" is unfalsifiable. This is that, for a
boundary nobody has ever counted.

WHAT IT COUNTS. References, in code (comments and string literals stripped), to the render-shaped vocabulary
below. It counts VOCABULARY, not includes: an include is one line whatever it drags in, while the vocabulary
is what actually has to be paid down. A file with `Drawable` in forty signatures is forty units of work.

Usage:
    boundary-inventory.py                 # the report
    boundary-inventory.py --facts         # machine-readable JSON
    boundary-inventory.py --inject FILE   # write the generated block into a doc
    boundary-inventory.py --check FILE    # exit 1 if the doc's block is stale
"""

import importlib.util
import json
import os
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# ONE COMMENT STRIPPER, borrowed rather than copied. layering-lint.py owns it and is gate-critical, so it is
# loaded rather than duplicated -- a second copy would drift and the two gates would disagree about what
# "in code" means, which is exactly the class of defect this campaign keeps finding. The hyphen in the
# filename is why this is importlib and not an import statement.
_spec = importlib.util.spec_from_file_location("layering_lint", REPO / "scripts" / "layering-lint.py")
_layering = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_layering)
strip_code = _layering.strip_comments_and_strings

# THE VOCABULARY. Each term is a render-shaped concept the game layer names. The rationale matters as much as
# the term: this list is the definition of "view surface", and anyone widening it is widening the debt.
VOCAB = [
    ("Drawable",             "the view data model -- what to draw, produced by game code"),
    ("RenderCallback",       "the abstract sink entities push drawables into"),
    ("WorldRenderData",      "the 35-member frame view model the render passes consume"),
    ("EntityDrawables",      "per-entity drawable bundle"),
    ("EntityRenderLayer",    "draw ordering -- a render concern expressed in game types"),
    ("EntityHighlightEffect", "a visual effect described by the sim"),
    ("renderLightSources",   "entity API that exists only to feed the renderer"),
]

# Files that are view code BY DUTY even though they live in the sim library. Called out separately because
# "move it" and "shrink it" are different remedies, and lumping them hides that.
VIEW_BY_DUTY = [
    "source/game/StarTileDrawer.hpp",
    "source/game/StarTileDrawer.cpp",
    "source/game/StarWorldRenderData.hpp",
    "source/game/StarWorldRenderData.cpp",
    "source/game/StarEntityRendering.hpp",
    "source/game/StarEntityRendering.cpp",
    "source/game/StarEntityRenderingTypes.hpp",
    "source/game/StarEntityRenderingTypes.cpp",
    "source/game/StarDrawable.hpp",
    "source/game/StarDrawable.cpp",
]

# THE SPLIT THAT DECIDES THE REMEDY, and the reason a single total is the wrong headline.
#
# `Drawable` is an entity SAYING WHAT IT LOOKS LIKE. It carries no GL, no renderer and no frame state; it is
# data, and a game object describing its own appearance is a legitimate game-layer duty. Seventy percent of
# the raw count is this, and shrinking it is not obviously progress -- it might just be moving character
# appearance out of the character.
#
# Everything else is the PUSH SINK and the FRAME MODEL: interfaces that exist so a renderer can pull work out
# per frame. Those are the ones a headless client has to be able to not have. Counting them together with
# Drawable produces one big number that cannot be acted on; counting them apart produces two numbers with
# different remedies.
APPEARANCE = {"Drawable"}

MARK_BEGIN = "<!-- BEGIN GENERATED: scripts/boundary-inventory.py --inject -->"
MARK_END = "<!-- END GENERATED -->"


def game_files():
    root = REPO / "source" / "game"
    out = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.endswith((".hpp", ".cpp")):
                p = pathlib.Path(dirpath) / fn
                out.append(str(p.relative_to(REPO)))
    return sorted(out)


def scan():
    per_file = {}
    for rel in game_files():
        try:
            src = (REPO / rel).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        code = strip_code(src)
        counts = {}
        for term, _why in VOCAB:
            n = len(re.findall(r"\b%s\b" % re.escape(term), code))
            if n:
                counts[term] = n
        if counts:
            per_file[rel] = counts
    return per_file


def report(per_file):
    total_files = len(game_files())
    touched = len(per_file)
    grand = sum(sum(c.values()) for c in per_file.values())

    duty = {f: per_file[f] for f in VIEW_BY_DUTY if f in per_file}
    duty_refs = sum(sum(c.values()) for c in duty.values())

    lines = []
    lines.append("**The game layer's view surface, measured from the tree.** Regenerate with "
                 "`scripts/boundary-inventory.py --inject docs/render/game-render-boundary.md`; "
                 "`boundary_fresh` fails CI if this block and the tree disagree.")
    lines.append("")
    lines.append("| metric | value |")
    lines.append("|:-------|------:|")
    appearance = sum(c.get(t, 0) for c in per_file.values() for t in APPEARANCE)
    sink = grand - appearance
    sink_files = sum(1 for c in per_file.values() if any(t not in APPEARANCE for t in c))

    lines.append("| `star_game` files | %d |" % total_files)
    lines.append("| files free of view vocabulary | %d |" % (total_files - touched))
    lines.append("| files naming a view concept | %d |" % touched)
    lines.append("| appearance vocabulary (`Drawable`) | %d |" % appearance)
    lines.append("| **PUSH-SINK + FRAME-MODEL vocabulary** | **%d** across %d files |" % (sink, sink_files))
    lines.append("| of the above, in files that are view-by-duty | %d |" % duty_refs)
    lines.append("")
    lines.append("Per term:")
    lines.append("")
    lines.append("| term | refs | files | what it is |")
    lines.append("|:-----|-----:|------:|:-----------|")
    for term, why in VOCAB:
        refs = sum(c.get(term, 0) for c in per_file.values())
        files = sum(1 for c in per_file.values() if term in c)
        lines.append("| `%s` | %d | %d | %s |" % (term, refs, files, why))
    lines.append("")
    lines.append("The twenty heaviest files:")
    lines.append("")
    lines.append("| file | refs | terms | duty |")
    lines.append("|:-----|-----:|:------|:-----|")
    ranked = sorted(per_file.items(), key=lambda kv: -sum(kv[1].values()))
    for rel, counts in ranked[:20]:
        n = sum(counts.values())
        terms = ", ".join("%s×%d" % (t, c) for t, c in sorted(counts.items(), key=lambda kv: -kv[1]))
        lines.append("| `%s` | %d | %s | %s |" % (rel, n, terms, "view" if rel in VIEW_BY_DUTY else "sim"))
    return "\n".join(lines)


def doc_path(arg):
    p = pathlib.Path(arg)
    return p if p.is_absolute() else REPO / p


def inject(arg, block, check_only):
    path = doc_path(arg)
    text = path.read_text(encoding="utf-8", errors="replace")
    if MARK_BEGIN not in text or MARK_END not in text:
        print("boundary-inventory: %s has no generated-block markers" % arg)
        return 2
    head, rest = text.split(MARK_BEGIN, 1)
    _old, tail = rest.split(MARK_END, 1)
    new = "%s%s\n%s\n%s%s" % (head, MARK_BEGIN, block, MARK_END, tail)
    if check_only:
        if new != text:
            print("%s: generated block is STALE -- rerun "
                  "`scripts/boundary-inventory.py --inject %s`" % (arg, arg))
            return 1
        print("%s: generated block matches the tree" % arg)
        return 0
    path.write_text(new, encoding="utf-8", newline="\n")
    print("%s: generated block updated" % arg)
    return 0


def sink_total(per_file):
    return sum(c[t] for c in per_file.values() for t in c if t not in APPEARANCE)


def ratchet(per_file, ceiling):
    """A TOTAL ceiling, and unlike the L3 ratchet that is the correct shape here.

    #183's finding was that a per-file ratchet over a subset is satisfiable by RELOCATION: closing a seam
    MOVES coupling to the composition root, so metering only the passes scored a perfect result for a net
    change of zero. That failure mode does not exist for this metric. Moving a reference from one star_game
    file to another leaves the total identical -- the only way to lower it is to remove the reference or move
    the code OUT of star_game, which is exactly the outcome wanted. The gameable proxy and the real goal
    coincide, so the simplest metric is also the honest one."""
    total = sink_total(per_file)
    if total > ceiling:
        print("BOUNDARY RATCHET EXCEEDED")
        print("The game layer's push-sink + frame-model surface is %d, ceiling %d." % (total, ceiling))
        print("Something new in source/game named a renderer-facing concept. That is allowed, but it is a")
        print("decision: it moves the headless-client boundary further away. Raise the number in")
        print("source/test/CMakeLists.txt and say why in the commit message, or find another way to say it.")
        return 1
    slack = ceiling - total
    print("boundary-inventory: OK -- push-sink surface %d, ceiling %d.%s"
          % (total, ceiling, ("  %d BELOW ceiling -- lower it." % slack) if slack > 0 else ""))
    return 0


def main(argv):
    per_file = scan()
    if argv and argv[0] == "--max-sink":
        if len(argv) < 2:
            print("boundary-inventory: --max-sink needs a number")
            return 2
        return ratchet(per_file, int(argv[1]))
    if argv and argv[0] == "--facts":
        grand = sum(sum(c.values()) for c in per_file.values())
        print(json.dumps({"gameFiles": len(game_files()), "filesTouched": len(per_file),
                          "viewRefs": grand, "perFile": per_file}, indent=2, sort_keys=True))
        return 0
    if argv and argv[0] in ("--inject", "--check"):
        if len(argv) < 2:
            print("boundary-inventory: %s needs a file" % argv[0])
            return 2
        return inject(argv[1], report(per_file), argv[0] == "--check")
    print(report(per_file))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
