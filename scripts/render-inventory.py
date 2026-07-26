#!/usr/bin/env python3
"""Measure the render subsystem, so the architecture docs can state facts instead of recollections.

WHY THIS EXISTS
---------------
docs/render/ describes a three-stage story -- vanilla, accreted monolith, target state -- and the
published artifacts visualise it. Every factual claim in that set (what lives where, how big it is,
which layer it belongs to, how much coupling is left) was true when written and decays silently as the
code moves. The 2026-07-19 set was a picture of the INTENT on the day of the reorg; by 2026-07-25
WorldPainter had gone 119 -> 296 lines and the Air-Gap residual had GROWN.

A doc that asserts a number nobody re-measures is a doc that will mislead exactly when someone trusts
it. So the numbers come from here, and the prose in the docs is reserved for judgement -- the part a
script cannot produce.

WHAT IT MEASURES
  * layer assignment and size, from an EXPLICIT table below (that table is itself an architectural
    statement -- if a file is unassigned the script says so rather than guessing)
  * the Air-Gap probes: Root::singleton() reads in pass bodies, direct GL in layers that must not have
    it, telemetry handles owned per file
  * cross-layer includes -- the check a real layering lint would make
  * test coverage per layer

GENERATING INTO THE DOC, NOT JUST BESIDE IT
-------------------------------------------
Printing a table a human then copies into a doc is the same defect one indirection out: the copy is a
hand-typed number the moment it is pasted, and it drifted again within a day of this script existing --
the target-state doc claimed BackdropPass held 7 `Root::singleton()` reads while the tree measured 0.
So `--inject` writes the residual straight between markers in the doc and `--check` fails if the doc
and the tree disagree. `render_docs_fresh` runs `--check` in CI.

The injected block deliberately carries NO LINE COUNTS. Gating on those would redden CI on every render
commit, which is the zero-tolerance failure this campaign already decided against for `render_layering`
-- a gate people route around is worse than no gate. What it carries is the COUPLING residual, which is
directional: a wrong line count is cosmetic, a wrong singleton count sends the next author to pay down
work that does not exist.

Usage:
    scripts/render-inventory.py            # markdown to stdout, for embedding in docs/render/
    scripts/render-inventory.py --json     # machine-readable
    scripts/render-inventory.py --residual # just the generated block
    scripts/render-inventory.py --inject FILE   # rewrite the block inside FILE
    scripts/render-inventory.py --check  FILE   # exit 1 if FILE's block disagrees with the tree
"""

import argparse
import difflib
import json
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# EVERY read_text/write_text BELOW PINS encoding="utf-8", AND THAT IS NOT DEFENSIVE STYLE (#192).
# Python's text mode defaults to the LOCALE encoding, which is cp1252 on the windows-latest CI image. The
# architecture doc is full of em dashes; there they decoded as mojibake, so --check compared a freshly
# generated block against a corrupted copy of itself, and render_docs_fresh failed on that job and only
# that job -- a gate reporting doc drift that did not exist. The write pins newline="\n" as well, so
# --inject run on Windows cannot rewrite the marker block with CRLF and manufacture the drift for real.

# THE LAYER TABLE IS THE ARCHITECTURAL STATEMENT. Everything else here is arithmetic over it. A file
# that matches nothing is reported as UNASSIGNED rather than silently bucketed -- an unassigned render
# file is either a layering question nobody has answered or a new file whose author had no map.
LAYERS = [
    # The abstract Renderer interface belongs here and was MISSING until 2026-07-26. Every architecture
    # doc defines L1 as "abstract Renderer interface / OpenGlRenderer" -- the interface IS the seam the
    # whole decomposition is measured against, and layer1_layering enforces that L2 never names it. Its
    # absence is why the published artifacts' L1 total and this instrument's disagreed by exactly 340.
    # THE LAST TWO WERE UNCLAIMED UNTIL 2026-07-26, and by exactly the mechanism #137 found for the three
    # texture/type helpers in source/rendering: in the build, in no layer, and therefore invisible to every
    # count this instrument produces. They were missed a second time because the unassigned-file check
    # below only ever looked at source/rendering -- the directory where the FIRST instance was found. The
    # check now covers source/application too, which is the only reason a third instance would be caught.
    #
    # Both are L1 by duty, measured rather than asserted:
    #   StarTextureAtlas       418 lines. Declares TextureAtlasSet, which GlTextureAtlasSet inherits.
    #                          Sole consumer in the tree: StarRenderer_opengl.hpp.
    #   StarRenderDiagnostics   77 lines. Declares GpuTimer and RenderOracle, which GlGpuTimer and
    #                          GlRenderOracle inherit. Consumers: StarRenderer.hpp, StarRenderer_opengl.hpp.
    #
    # Claiming them moves L1 from 3796 to 4291 lines and moves NO coupling metric at all: zero
    # Root::singleton() reads, zero GL calls, zero telemetry handles, so the gated residual block is
    # byte-unchanged and 17 is still 17. StarRenderDiagnostics does name Telemetry:: and OpenGlRenderer,
    # but only in comments explaining what it deliberately does not depend on -- code_only() removes both,
    # which is the same reason that stripper exists at all. The coupling did not appear; the table simply
    # started looking.
    ("L1 substrate", "the GL-owning layer: the abstract seam, surfaces, texture primitives, the backend", [
        "source/application/StarRenderer.hpp", "source/application/StarRenderer.cpp",
        "source/application/StarGlRenderSurface.hpp", "source/application/StarGlRenderSurface.cpp",
        "source/application/StarGlTexturePrimitives.hpp", "source/application/StarGlTexturePrimitives.cpp",
        "source/application/StarRenderer_opengl.hpp", "source/application/StarRenderer_opengl.cpp",
        "source/application/StarTextureAtlas.hpp", "source/application/StarRenderDiagnostics.hpp",
    ]),
    ("L2 primitives", "pass-agnostic building blocks; no Renderer, no GL, testable off the GPU", [
        "source/rendering/StarRetainedSurface.hpp",
    ]),
    ("L3 passes", "sovereign render passes, orchestrated but not owned by WorldPainter", [
        "source/rendering/StarBackdropPass.hpp", "source/rendering/StarBackdropPass.cpp",
        "source/rendering/StarWorldPass.hpp", "source/rendering/StarWorldPass.cpp",
        "source/rendering/StarGpuLightmapPass.hpp", "source/rendering/StarGpuLightmapPass.cpp",
    ]),
    ("L3 orchestrator", "sequences the passes and owns nothing a pass should own", [
        "source/rendering/StarWorldPainter.hpp", "source/rendering/StarWorldPainter.cpp",
    ]),
    # The three texture/type helpers below were UNCLAIMED until #137 -- in the build, in no layer, and so
    # invisible to every count this instrument produced. Answering that layering question is what this
    # bucket's "unassigned" report existed to force. The evidence, measured rather than assumed:
    #   StarAnchorTypes        35 lines, includes only StarBiMap, no Renderer/GL/Root. Text-positioning
    #                          vocabulary; sole consumer is StarTextPainter.hpp.
    #   StarFontTextureGroup  201 lines, TexturePtr/TextureGroupPtr only, no GL, no Root. Glyph texture
    #                          cache; sole consumer is StarTextPainter.hpp.
    #   StarAssetTextureGroup 133 lines, TexturePtr/TextureGroupPtr, no GL, but TWO Root::singleton()
    #                          reads (a reload-listener registration and an assets handle) that nothing
    #                          was counting. Claiming it is why the measured residual rose 15 -> 17: the
    #                          coupling did not appear, the table simply started looking.
    #
    # ONE OF THEM IS NOT REALLY OURS. StarAssetTextureGroup is also consumed by source/frontend
    # (ChatBubbleManager) and source/windowing (GuiContext) -- it is a shared texture-caching service, not
    # a render-subsystem-internal helper. It sits here because that is where it lives and what the passes
    # use it for; do not "decompose" it without looking outside source/rendering first.
    ("painters (pre-decomposition)", "drawing helpers the passes consume; untouched by the decomposition", [
        "source/rendering/StarEnvironmentPainter.hpp", "source/rendering/StarEnvironmentPainter.cpp",
        "source/rendering/StarTilePainter.hpp", "source/rendering/StarTilePainter.cpp",
        "source/rendering/StarDrawablePainter.hpp", "source/rendering/StarDrawablePainter.cpp",
        "source/rendering/StarTextPainter.hpp", "source/rendering/StarTextPainter.cpp",
        "source/rendering/StarAnchorTypes.hpp", "source/rendering/StarAnchorTypes.cpp",
        "source/rendering/StarAssetTextureGroup.hpp", "source/rendering/StarAssetTextureGroup.cpp",
        "source/rendering/StarFontTextureGroup.hpp", "source/rendering/StarFontTextureGroup.cpp",
    ]),
]

SINGLETON = re.compile(r"Root::singleton\(\)")
GL_CALL = re.compile(r"\bgl[A-Z]\w+\s*\(")
TELEMETRY = re.compile(r"Telemetry::(timer|counter|gauge)\s*\(")
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)


# Strip comments before scanning. The first run of this script reported a "direct GL call" in an L3
# pass -- a layering violation that would have gone straight into the architecture doc as fact. Both
# hits were PROSE: comments naming glBlitFramebuffer and glMinSampleShading while explaining why the
# pass does NOT call them. A measurement instrument that cannot tell code from commentary about code
# will manufacture exactly the findings it was built to detect.
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")


def code_only(text):
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def lines(p):
    try:
        return len(p.read_text(encoding="utf-8", errors="replace").splitlines())
    except OSError:
        return 0


MARK_BEGIN = "<!-- BEGIN GENERATED: scripts/render-inventory.py --inject -->"
MARK_END = "<!-- END GENERATED -->"

# The `render_layering` ctest's per-file ceilings, read from the registration rather than restated here.
# Restating them would recreate the exact drift this script exists to delete -- and the gap between what
# is METERED and what merely EXISTS is the finding worth surfacing: closing the Air-Gap moves reads to
# the composition root, so a ratchet that meters only the passes is satisfiable by relocation.
CEILING = re.compile(r"(source/rendering/\w+\.cpp)=(\d+)")


def ceilings():
    p = REPO / "source/test/CMakeLists.txt"
    if not p.exists():
        return {}
    return {pathlib.Path(f).name: int(n) for f, n in CEILING.findall(p.read_text(encoding="utf-8", errors="replace"))}


def residual_block(report, unassigned):
    """The generated block: coupling residual only. No line counts -- see the module docstring."""
    caps = ceilings()
    out = [MARK_BEGIN, ""]
    out.append("**Air-Gap residual — every `Root::singleton()` read in the render subsystem, measured "
               "from the tree.** Regenerate with `scripts/render-inventory.py --inject "
               "docs/render/architecture-3-target-state.md`; `render_docs_fresh` fails CI if this block "
               "and the tree disagree.")
    out.append("")
    out.append("| layer | reads | of those, metered by a gate |")
    out.append("|:------|------:|----------------------------:|")
    tot = met = 0
    for L in report:
        fs = [f for f in L["files"] if not f.get("missing")]
        r = sum(f["singletonReads"] for f in fs)
        m = sum(f["singletonReads"] for f in fs if pathlib.Path(f["file"]).name in caps)
        tot += r
        met += m
        out.append(f"| {L['layer']} | {r} | {m} |")
    out.append(f"| **total** | **{tot}** | **{met}** |")
    out.append("")

    rows = []
    for L in report:
        for f in L["files"]:
            if f.get("missing"):
                continue
            name = pathlib.Path(f["file"]).name
            if f["singletonReads"] == 0 and name not in caps:
                continue
            cap = caps.get(name)
            rows.append(f"| `{name}` | {L['layer']} | {f['singletonReads']} | "
                        f"{cap if cap is not None else '— not metered'} |")
    if rows:
        out.append("Files that carry a read, plus every file the ratchet holds at a ceiling:")
        out.append("")
        out.append("| file | layer | reads | `render_layering` ceiling |")
        out.append("|:-----|:------|------:|--------------------------:|")
        out.extend(rows)
        out.append("")
    if unassigned:
        out.append("**Unclaimed by the layer table** (`source/rendering/`): "
                   + ", ".join(f"`{u}`" for u in unassigned)
                   + " — a layering question nobody has answered.")
        out.append("")
    out.append(MARK_END)
    return "\n".join(out)


# ---------------------------------------------------------------------------------------------------
# ARTIFACT FACTS. The published artifacts (claude.ai) quote per-module hpp/impl/total line counts and
# group percentages. They CANNOT run this script and no gate reaches them, so on 2026-07-26 all four
# current-state panels were found stale -- every number CORRECT when written and decayed since (verified
# by re-measuring their own cited commit 872244f8: 291/116/411/74, all exact). Understated growth,
# overstated completion.
#
# `--facts` is the answer that is actually available off-repo: a refresh becomes "run this, paste, diff"
# instead of "retype from memory". It emits provenance (commit + commit date, never wall-clock) so a
# reader can see the vintage without trusting the prose.
ARTIFACT_GROUPS = [
    ("Orchestrator", [
        ("WorldPainter", "source/rendering/StarWorldPainter.hpp", "source/rendering/StarWorldPainter.cpp"),
    ]),
    ("L3 passes", [
        ("BackdropPass", "source/rendering/StarBackdropPass.hpp", "source/rendering/StarBackdropPass.cpp"),
        ("LightmapPass (`GpuLightmapPass`)", "source/rendering/StarGpuLightmapPass.hpp",
         "source/rendering/StarGpuLightmapPass.cpp"),
        ("WorldPass", "source/rendering/StarWorldPass.hpp", "source/rendering/StarWorldPass.cpp"),
    ]),
    # RetainedSurface is header-only; its "impl" column is the off-GPU test suite, as the artifact says.
    ("L2 primitive", [
        ("RetainedSurface", "source/rendering/StarRetainedSurface.hpp", "source/test/retained_surface_test.cpp"),
    ]),
    ("Draw painters", [
        ("TextPainter", "source/rendering/StarTextPainter.hpp", "source/rendering/StarTextPainter.cpp"),
        ("EnvironmentPainter", "source/rendering/StarEnvironmentPainter.hpp",
         "source/rendering/StarEnvironmentPainter.cpp"),
        ("TilePainter", "source/rendering/StarTilePainter.hpp", "source/rendering/StarTilePainter.cpp"),
        ("DrawablePainter", "source/rendering/StarDrawablePainter.hpp", "source/rendering/StarDrawablePainter.cpp"),
    ]),
    ("L1 substrate", [
        ("OpenGlRenderer", "source/application/StarRenderer_opengl.hpp",
         "source/application/StarRenderer_opengl.cpp"),
        ("`StarGlRenderSurface` — GL* structs", "source/application/StarGlRenderSurface.hpp",
         "source/application/StarGlRenderSurface.cpp"),
        ("Renderer interface", "source/application/StarRenderer.hpp", "source/application/StarRenderer.cpp"),
        ("GlTexturePrimitives", "source/application/StarGlTexturePrimitives.hpp",
         "source/application/StarGlTexturePrimitives.cpp"),
    ]),
]

# Function-level extents the artifacts cite. Brace-counted from column 0 -- style-dependent on purpose:
# this codebase closes top-level functions with a bare `}`, and a real parser would be a lie about how
# much rigour is here. If the style ever changes this returns None rather than a wrong number.
ARTIFACT_FUNCTIONS = [
    ("WorldPainter::render()", "source/rendering/StarWorldPainter.cpp", "void WorldPainter::render("),
]


def function_extent(rel, signature):
    p = REPO / rel
    if not p.exists():
        return None
    lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    start = next((i for i, l in enumerate(lines) if l.startswith(signature)), None)
    if start is None:
        return None
    for j in range(start + 1, len(lines)):
        if lines[j] == "}":
            return {"file": rel, "first": start + 1, "last": j + 1, "lines": j - start + 1}
    return None


def provenance():
    def git(*a):
        try:
            r = subprocess.run(["git", "-C", str(REPO), *a], capture_output=True, text=True)
            return r.stdout.strip() if r.returncode == 0 else "?"
        except OSError:
            return "?"
    # Committer date, not wall-clock: two runs at the same commit must produce the same bytes.
    return git("rev-parse", "--short", "HEAD"), git("log", "-1", "--format=%cs")


def facts_block(caps):
    sha, date = provenance()
    out = [f"<!-- generated by scripts/render-inventory.py --facts at {sha} ({date}) -->", ""]
    out.append(f"**Measured at `{sha}` ({date})** by `scripts/render-inventory.py --facts`. "
               "Every number below is generated; none is typed by hand.")
    out.append("")
    out.append("| Group | Module | `.hpp` | impl | **total** |")
    out.append("|:------|:-------|-------:|-----:|----------:|")
    grand, group_totals = 0, []
    for group, mods in ARTIFACT_GROUPS:
        gt, first = 0, True
        for name, hpp, impl in mods:
            h, i = lines(REPO / hpp), lines(REPO / impl)
            gt += h + i
            out.append(f"| {group if first else ''} | {name} | {h} | {i} | **{h + i}** |")
            first = False
        group_totals.append((group, gt))
        grand += gt
    out.append(f"| **total** | | | | **{grand}** |")
    out.append("")
    out.append("Distribution: " + " · ".join(
        f"{g} **{t * 100 // grand}%** ({t:,})" for g, t in group_totals))
    out.append("")
    for label, rel, sig in ARTIFACT_FUNCTIONS:
        e = function_extent(rel, sig)
        out.append(f"`{label}` — **{e['lines']} lines** ({rel}:{e['first']}-{e['last']})" if e
                   else f"`{label}` — UNMEASURABLE (signature not found; style changed?)")
    out.append("")
    out.append("Air-Gap residual — `Root::singleton()` reads, and which are metered by `render_layering`:")
    out.append("")
    return out, grand


def doc_path(arg):
    """Resolve relative to the REPO, not the cwd -- ctest runs this from the build directory."""
    p = pathlib.Path(arg)
    return p if p.is_absolute() else REPO / p


def splice(path, block):
    """Return (old_block, new_text) for FILE, or raise if the markers are absent/malformed."""
    text = path.read_text(encoding="utf-8", errors="replace")
    i, j = text.find(MARK_BEGIN), text.find(MARK_END)
    if i < 0 or j < 0 or j < i:
        raise SystemExit(f"{path}: markers not found. Add a block delimited by:\n"
                         f"  {MARK_BEGIN}\n  {MARK_END}")
    j += len(MARK_END)
    return text[i:j], text[:i] + block + text[j:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--residual", action="store_true", help="print just the generated block")
    ap.add_argument("--facts", action="store_true",
                    help="per-module line counts + provenance, for refreshing the published artifacts")
    ap.add_argument("--inject", metavar="FILE", help="rewrite the generated block inside FILE")
    ap.add_argument("--check", metavar="FILE", help="exit 1 if FILE's block disagrees with the tree")
    args = ap.parse_args()

    # Map every assigned file to its layer, so cross-layer includes can be resolved by basename.
    owner = {}
    for name, _, files in LAYERS:
        for f in files:
            owner[pathlib.Path(f).name] = name

    report = []
    for name, purpose, files in LAYERS:
        entries = []
        for rel in files:
            p = REPO / rel
            if not p.exists():
                entries.append({"file": rel, "lines": 0, "missing": True})
                continue
            raw = p.read_text(encoding="utf-8", errors="replace")
            text = code_only(raw)
            deps = sorted({owner[i.split("/")[-1]] for i in INCLUDE.findall(text)
                           if i.split("/")[-1] in owner and owner[i.split("/")[-1]] != name})
            entries.append({
                "file": rel,
                "lines": lines(p),
                "singletonReads": len(SINGLETON.findall(text)),
                "glCalls": len(GL_CALL.findall(text)),
                "telemetryHandles": len(TELEMETRY.findall(text)),
                "dependsOn": deps,
            })
        report.append({"layer": name, "purpose": purpose,
                       "lines": sum(e["lines"] for e in entries), "files": entries})

    # Anything in the render dirs the table does not claim.
    #
    # BOTH DIRECTORIES, NOT JUST source/rendering. This check existed to force the layering question that
    # #137 answered for three unclaimed helpers -- and then missed StarTextureAtlas and
    # StarRenderDiagnostics for weeks because it only ever globbed the directory the first instance was
    # found in. A check scoped to where the last bug was is a check that finds the last bug.
    #
    # source/application is a MIXED directory: half of it is L1, half is the SDL main loop and the Steam
    # platform services. So it cannot simply be globbed -- it needs the partition below, which is an
    # architectural statement in the same sense LAYERS is. Every Star* file in either directory must be
    # claimed by a layer OR named here as explicitly-not-render; a new file that is neither is reported.
    # That is the point: adding a file to source/application should force the question "is this render?"
    APPLICATION_NON_RENDER = {
        "StarApplication.cpp", "StarApplication.hpp", "StarApplicationController.hpp",
        "StarMainApplication.hpp", "StarMainApplication_sdl.cpp",
        "StarPlatformServices_pc.cpp", "StarPlatformServices_pc.hpp",
        "StarP2PNetworkingService_pc.cpp", "StarP2PNetworkingService_pc.hpp",
        "StarDesktopService_pc_steam.cpp", "StarDesktopService_pc_steam.hpp",
        "StarStatisticsService_pc_steam.cpp", "StarStatisticsService_pc_steam.hpp",
        "StarUserGeneratedContentService_pc_steam.cpp", "StarUserGeneratedContentService_pc_steam.hpp",
    }
    assigned = {pathlib.Path(f).name for _, _, fs in LAYERS for f in fs}
    unassigned = sorted(p.name for p in (REPO / "source/rendering").glob("Star*")
                        if p.name not in assigned)
    unassigned += sorted("application/" + p.name for p in (REPO / "source/application").glob("Star*")
                         if p.name not in assigned and p.name not in APPLICATION_NON_RENDER)

    tests = sorted(p.name for p in (REPO / "source/test").glob("*.cpp")
                   if any(k in p.name for k in ("render", "surface", "lighting")))

    if args.json:
        json.dump({"layers": report, "unassigned": unassigned, "tests": tests}, sys.stdout, indent=1)
        return 0

    block = residual_block(report, unassigned)

    if args.residual:
        print(block)
        return 0

    if args.facts:
        head, _ = facts_block(ceilings())
        print("\n".join(head))
        print(block)
        return 0

    if args.inject:
        path = doc_path(args.inject)
        old, new = splice(path, block)
        if old == block:
            print(f"{args.inject}: already current")
            return 0
        path.write_text(new, encoding="utf-8", newline="\n")
        print(f"{args.inject}: regenerated")
        return 0

    if args.check:
        path = doc_path(args.check)
        old, _ = splice(path, block)
        if old == block:
            print(f"{args.check}: generated block matches the tree")
            return 0
        # An actionable failure names the remedy. This gate has exactly one.
        print(f"STALE: {args.check} disagrees with the tree.\n")
        sys.stdout.writelines(difflib.unified_diff(
            old.splitlines(True), block.splitlines(True),
            fromfile=f"{args.check} (committed)", tofile="measured from the tree"))
        print(f"\nFix: scripts/render-inventory.py --inject {args.check}")
        print("Then read the diff before committing it -- a changed count is an architectural event,")
        print("not a formatting one. A read that MOVED rather than went away is still a read.")
        return 1

    out = []
    out.append("| layer | lines | files | `Root::singleton()` | direct GL | telemetry handles |")
    out.append("|:------|------:|------:|--------------------:|----------:|------------------:|")
    for L in report:
        fs = [f for f in L["files"] if not f.get("missing")]
        out.append(f"| **{L['layer']}** | {L['lines']} | {len(fs)} | "
                   f"{sum(f['singletonReads'] for f in fs)} | {sum(f['glCalls'] for f in fs)} | "
                   f"{sum(f['telemetryHandles'] for f in fs)} |")
    out.append("")
    out.append("Per file:")
    out.append("")
    out.append("| file | layer | lines | singleton | GL | telemetry | depends on |")
    out.append("|:-----|:------|------:|----------:|---:|----------:|:-----------|")
    for L in report:
        for f in L["files"]:
            if f.get("missing"):
                out.append(f"| `{f['file']}` | {L['layer']} | — | — | — | — | **MISSING** |")
                continue
            dep = ", ".join(f["dependsOn"]) or "—"
            out.append(f"| `{f['file'].split('/')[-1]}` | {L['layer']} | {f['lines']} | "
                       f"{f['singletonReads']} | {f['glCalls']} | {f['telemetryHandles']} | {dep} |")
    if unassigned:
        out.append("")
        out.append("**Unassigned files in `source/rendering/`** — the layer table does not claim these, "
                   "which means either a layering question nobody has answered or a file whose author "
                   "had no map: " + ", ".join(f"`{u}`" for u in unassigned))
    out.append("")
    out.append(f"**Tests touching the render subsystem:** " + (", ".join(f"`{t}`" for t in tests) or "none"))
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
