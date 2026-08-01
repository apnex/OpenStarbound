#!/usr/bin/env python3
"""Verify each composition's grant closure AT THE LINK ALTITUDE -- what actually lands in the binary.

WHY THIS EXISTS. `grant-sweep.py` checks #include edges; `composition-graphs.py` draws the closure the
grant table implies. Both read the SOURCE. Neither can see what the linker did, and the linker is the
only witness that matters for the question this design exists to answer: does a headless binary contain
a renderer.

The gap was not theoretical. A one-off sweep of `starbound_server` on 2026-08-01 found:

    Renderer 0   Pane 0   Widget 0   GuiContext 0   TextPainter 0   WorldPainter 0
    Drawable 249   RenderCallback 87   Image 209   AudioInstance 159   Mixer 98   Songbook 126

Every boundary that HELD is a directory -- `rendering/`, `windowing/`, `frontend/` are their own build
targets, so the server simply does not link them. Every boundary that FAILED is a type living inside
`game/`, which the server is legitimately granted. So a source-altitude gate reports PASS over a
73 MB dedicated server that contains a software audio mixer, and it is not wrong to do so: `server`
IS granted `game`, and `Mixer` IS in `base`. The declaration is satisfied and the binary is still
wrong. That is the exact shape of defect no existing instrument here can see.

HOW IT ATTRIBUTES. Symbol -> defining object file -> source path -> component.

  * UNIQUE-DEFINER ONLY. A symbol defined by more than one .o (templates, inline functions, vtables
    emitted in every TU) is NOT attributed to any component. First-wins attribution was tried during
    the tier-2 separability measurement and produced spuriously-reachable files; the Director caught
    the consequence. An arbitrary answer is worse than a declared abstention, so these are counted
    into AMBIGUOUS and reported.
  * REFINE overrides the directory default for the handful of files whose TARGET component already
    has a name but no directory yet. This is the same device as grant-sweep's FROM_APPLICATION, and
    it is deliberately small: it claims only what has been measured, never the whole of `game`.

PRESENCE, NOT USE -- AND THAT IS THE POINT. Every Star library is declared `ADD_LIBRARY(... OBJECT ...)`:
core, base, game, rendering, frontend, windowing, application, extern. An OBJECT library links ALL of
its objects into every consumer; there is no per-object pruning the way a static archive prunes. So
`starbound_server` contains all 237 `star_game` objects unconditionally -- including `WorldClient`, the
client-side replica, at 308 symbols.

Two consequences, and they are the reason this gate measures containment rather than reachability:

  1. The design's claim IS about containment. "A headless client does not link a renderer" is a
     statement about what is in the binary, not about which functions run.
  2. Under OBJECT-library semantics, containment is decided entirely by which libraries an
     ENTRYPOINT's CMakeLists names. That is an all-or-nothing, directory-granular switch -- so a
     component boundary in this codebase can only ever be a DIRECTORY. The boundaries that held in
     the sweep above held because `rendering/`, `windowing/` and `frontend/` are separate libraries
     the server does not name; nothing analysed a dependency. Any component in the register that is
     not its own directory is unenforceable by construction.

WHAT IT REPORTS. Coverage first, verdicts second. A gate that prints "2 violations" over a binary it
could only attribute 3% of reads as a clean bill of health -- `oracle_vocabulary_trap` cost us that
lesson once already. Unattributed symbols are split into VENDORED (defined by no object in our build
tree -- SDL, Lua, freetype, libstdc++) and AMBIGUOUS (defined by several, see above), because those
are different kinds of ignorance and lumping them hides which one is growing. MONOLITH counts symbols
landing in a component the design intends to split but which is still one directory; they are not
violations, they are the work remaining.

LEAKING is a RATCHET, not a pass list. Each entry is a component the binary links today and its
closure does not permit. Ceilings may only go down; the design is finished when the table is empty.
"""
import argparse
import collections
import importlib.util
import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "source"
BUILD = REPO / "build/linux-release-clang"
DIST = REPO / "dist"


def _load(name):
    """Import a sibling script by path. The filenames are hyphenated and cannot be imported normally;
    restating their tables here instead would be the one-knob-two-declarations defect #185 deletes."""
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"),
                                                  str(REPO / "scripts" / (name + ".py")))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


GRANT = _load("grant-sweep")
COMPO = _load("composition-graphs")

# ---------------------------------------------------------------------------------------------
# ENTRYPOINT -> the binary that composition builds today. The design declares six ENTRYPOINTs and
# the tree builds two of them; the other four are reported UNBUILT rather than skipped, so this
# never reads as a sweep over the whole register.
# ---------------------------------------------------------------------------------------------
ENTRYPOINT_BINARY = {"client_opengl": "starbound", "server": "starbound_server"}

# ---------------------------------------------------------------------------------------------
# Files whose TARGET-state component is already named but whose directory does not exist yet.
# Overrides grant-sweep's directory default. Keep this list to files that have been MEASURED --
# an unmeasured guess here would silently move symbols between verdicts.
# ---------------------------------------------------------------------------------------------
REFINE = {
    # The audio stack, per the register: `mixing` is "`Mixer` and the `Audio` decoder".
    #
    # NOTE the co-habitation, because it is a finding and not an accident of this mapping:
    # base/StarMixer.hpp defines BOTH `AudioInstance` (the `sound` CONTRACT -- what a sound is) and
    # `Mixer` (the `mixing` BACKEND -- what turns sounds into PCM). Contract and backend in one
    # header is precisely the state `scene`/`rendering` was in before that split, so `sound` cannot
    # be attributed separately until the header is divided. Both files therefore score as `mixing`,
    # which UNDERSTATES the leak rather than overstating it.
    #
    # StarSongbook is deliberately ABSENT: it holds AudioInstance but is game logic that produces
    # sound, so it splits under tier 3 rather than moving wholesale. A file whose target component
    # is ambiguous does not belong in a table that decides verdicts.
    "core/StarAudio.hpp": "mixing", "core/StarAudio.cpp": "mixing",
    "base/StarMixer.hpp": "mixing", "base/StarMixer.cpp": "mixing",
    # The appearance vocabulary (tier 2). Drawable is the unit of "how a thing looks"; the render
    # callback is the sink an entity pushes them into.
    "game/StarDrawable.hpp": "scene", "game/StarDrawable.cpp": "scene",
    "game/StarEntityRendering.hpp": "scene", "game/StarEntityRendering.cpp": "scene",
}

# Components that are still one directory but which the design splits. Symbols landing here are the
# work remaining, not a violation -- reported so the coverage number cannot be mistaken for progress.
MONOLITH = {"game": "splits into game / world / universe / worldgen / *_view",
            "core": "holds Image and the audio decoder",
            "base": "holds the mixer and the animation substrate"}

# ---------------------------------------------------------------------------------------------
# THE RATCHET. Component leaks measured on 2026-08-01. Each may only go down, and the design is
# finished when this table is empty.
#
# These two numbers UNDERSTATE the leak, and the understatement is the point of the MONOLITH line
# beside them. They count only the eight files REFINE can attribute. The rest of the same defect is
# inside the 27,645 `game` symbols every binary links -- 111 game files hold `Drawable`, 31 hold
# `AudioInstance`, and 25 hold both. Tier 2 and tier 3 move those out; as they land, symbols migrate
# from MONOLITH `game` into these rows, so the ceilings will RISE before they fall. Raise them
# deliberately when that happens, with the migration named in the reason -- never to silence a red.
# ---------------------------------------------------------------------------------------------
LEAKING = {
    ("server", "mixing"): (186, "a dedicated server links a software audio mixer and the Ogg decoder"),
    ("server", "scene"): (48, "tier 2: an entity still knows how it looks, so `Drawable` follows it in"),
}

SYMTYPES = set("TtWwVvDdBbRrGgSs")


def nm(paths, extra=()):
    """-> list of (file, symbol). -A prefixes every line with its file, which is the only reliable
    way to keep attribution when nm is handed hundreds of objects at once."""
    out = []
    paths = [str(p) for p in paths]
    for i in range(0, len(paths), 200):
        chunk = paths[i:i + 200]
        r = subprocess.run(["nm", "-A", "--defined-only", "--no-demangle", *extra, *chunk],
                           capture_output=True, text=True)
        for line in r.stdout.splitlines():
            f, _, rest = line.partition(":")
            parts = rest.split()
            if len(parts) >= 2 and parts[-2] in SYMTYPES and len(parts[-2]) == 1:
                out.append((f, parts[-1]))
    return out


def source_of(objpath):
    """build/<preset>/<top>/CMakeFiles/<lib>.dir/<sub>/<Name>.cpp.o -> source-relative <top>/<sub>/<Name>.cpp"""
    p = pathlib.Path(objpath)
    try:
        rel = p.relative_to(BUILD)
    except ValueError:
        return None
    parts = list(rel.parts)
    if "CMakeFiles" not in parts:
        return None
    i = parts.index("CMakeFiles")
    top, tail = parts[:i], parts[i + 2:]          # skip CMakeFiles and <lib>.dir
    if not tail:
        return None
    name = tail[-1]
    if not name.endswith(".o"):
        return None
    return "/".join(top + tail[:-1] + [name[:-2]])


def component_of(srcrel):
    if srcrel in REFINE:
        return REFINE[srcrel]
    return GRANT.owner_of(pathlib.Path(srcrel))


def index_objects():
    """-> symbol -> component, for symbols with EXACTLY ONE defining object file."""
    objs = sorted(BUILD.rglob("*.o"))
    if not objs:
        raise SystemExit("link-sweep: no object files under %s -- build first" % BUILD)
    definers = collections.defaultdict(set)
    for f, sym in nm(objs):
        src = source_of(f)
        if src:
            definers[sym].add(src)
    unique, multi = {}, set()
    for sym, srcs in definers.items():
        if len(srcs) == 1:
            comp = component_of(next(iter(srcs)))
            if comp:
                unique[sym] = comp
        else:
            multi.add(sym)
    return unique, multi, len(objs)


def check_stale(binary):
    newest = max((p.stat().st_mtime for p in SRC.rglob("*")
                  if p.suffix in (".hpp", ".cpp", ".h")), default=0)
    return newest > binary.stat().st_mtime


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 on any leak above its ceiling")
    ap.add_argument("--allow-stale", action="store_true",
                    help="measure even if sources are newer than the binaries")
    args = ap.parse_args(argv)

    spec_text = COMPO.SPEC.read_text(encoding="utf-8")
    comp, grants = COMPO.parse(spec_text)
    entries = sorted(n for n, v in comp.items() if v["kind"] == "ENTRYPOINT")
    if not entries:
        raise SystemExit("link-sweep: no ENTRYPOINT components in the register")

    unique, multi, nobj = index_objects()
    print("link-sweep: %d object files, %d uniquely-defined symbols attributed, %d ambiguous "
          "(template/inline, deliberately unattributed)" % (nobj, len(unique), len(multi)))

    failures, stale, measured = 0, [], 0
    for entry in entries:
        binname = ENTRYPOINT_BINARY.get(entry)
        if not binname:
            print("\n  UNBUILT      %-14s declared in the register, no binary in the tree" % entry)
            continue
        binary = DIST / binname
        if not binary.exists():
            print("\n  UNBUILT      %-14s %s not present" % (entry, binary))
            continue
        measured += 1
        if check_stale(binary) and not args.allow_stale:
            stale.append(binname)

        allowed = COMPO.closure(entry, comp, grants)
        seen = collections.Counter()
        ambiguous = vendored = 0
        for _f, sym in nm([binary]):
            c = unique.get(sym)
            if c:
                seen[c] += 1
            elif sym in multi:
                ambiguous += 1
            else:
                vendored += 1
        attributed = sum(seen.values())
        total = attributed + ambiguous + vendored
        cov = 100.0 * attributed / total if total else 0.0

        print("\n  %s  ->  %s" % (entry, binname))
        print("    %d symbols: %d attributed (%.1f%%), %d ambiguous, %d vendored/system"
              % (total, attributed, cov, ambiguous, vendored))
        print("    grant closure permits: %s" % ", ".join(sorted(allowed)))
        for c, n in sorted(seen.items(), key=lambda kv: -kv[1]):
            if c in allowed:
                tag = "MONOLITH" if c in MONOLITH else "ok"
                note = "  -- %s" % MONOLITH[c] if c in MONOLITH else ""
                print("      %-10s %-12s %6d%s" % (tag, c, n, note))
            else:
                ceiling, why = LEAKING.get((entry, c), (None, "not yet triaged"))
                if ceiling is not None and n <= ceiling:
                    print("      %-10s %-12s %6d (ceiling %d) -- %s" % ("LEAKING", c, n, ceiling, why))
                else:
                    failures += 1
                    print("      %-10s %-12s %6d%s -- %s"
                          % ("VIOLATION", c, n,
                             "" if ceiling is None else " ABOVE ceiling %d" % ceiling, why))

    # A gate that measured nothing must not report OK. Four of the six ENTRYPOINTs are target-state
    # and have no binary, so "everything was UNBUILT" is a reachable state -- and it would print a
    # green line over a sweep that checked zero binaries. That is `oracle_vocabulary_trap` exactly.
    if not measured:
        print("\nlink-sweep: FAIL -- no ENTRYPOINT binary was found under %s; nothing was checked"
              % DIST)
        return 1 if args.check else 0
    if stale:
        print("\nlink-sweep: STALE -- sources are newer than %s; rebuild or pass --allow-stale"
              % ", ".join(stale))
        return 1 if args.check else 0
    if failures:
        print("\nlink-sweep: FAIL -- %d component(s) land in a binary their closure forbids" % failures)
        return 1 if args.check else 0
    print("\nlink-sweep: OK -- every attributed component is inside its composition's grant closure")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
