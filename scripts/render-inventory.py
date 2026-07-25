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

Usage:
    scripts/render-inventory.py            # markdown to stdout, for embedding in docs/render/
    scripts/render-inventory.py --json     # machine-readable
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# THE LAYER TABLE IS THE ARCHITECTURAL STATEMENT. Everything else here is arithmetic over it. A file
# that matches nothing is reported as UNASSIGNED rather than silently bucketed -- an unassigned render
# file is either a layering question nobody has answered or a new file whose author had no map.
LAYERS = [
    ("L1 substrate", "the GL-owning layer: surfaces, texture primitives, the OpenGL backend", [
        "source/application/StarGlRenderSurface.hpp", "source/application/StarGlRenderSurface.cpp",
        "source/application/StarGlTexturePrimitives.hpp", "source/application/StarGlTexturePrimitives.cpp",
        "source/application/StarRenderer_opengl.hpp", "source/application/StarRenderer_opengl.cpp",
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
    ("painters (pre-decomposition)", "drawing helpers the passes consume; untouched by the decomposition", [
        "source/rendering/StarEnvironmentPainter.hpp", "source/rendering/StarEnvironmentPainter.cpp",
        "source/rendering/StarTilePainter.hpp", "source/rendering/StarTilePainter.cpp",
        "source/rendering/StarDrawablePainter.hpp", "source/rendering/StarDrawablePainter.cpp",
        "source/rendering/StarTextPainter.hpp", "source/rendering/StarTextPainter.cpp",
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
        return len(p.read_text(errors="replace").splitlines())
    except OSError:
        return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
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
            raw = p.read_text(errors="replace")
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
    assigned = {pathlib.Path(f).name for _, _, fs in LAYERS for f in fs}
    unassigned = sorted(p.name for p in (REPO / "source/rendering").glob("Star*")
                        if p.name not in assigned)

    tests = sorted(p.name for p in (REPO / "source/test").glob("*.cpp")
                   if any(k in p.name for k in ("render", "surface", "lighting")))

    if args.json:
        json.dump({"layers": report, "unassigned": unassigned, "tests": tests}, sys.stdout, indent=1)
        return 0

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
