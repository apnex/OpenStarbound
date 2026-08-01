#!/usr/bin/env python3
"""Check the headless-client design's grant table against the tree.

WHY THIS EXISTS. Every other artifact in that design is machine-checked -- the diagram's edges come
from a measured include sweep and are verified against the register on every render. The grant table's
CONTENTS were derived by hand, and that is exactly where two defects sat:

  1. `host` was granted to nobody while 8 files across 3 directories already named
     ApplicationController. Section 4 as first published would not have compiled.
  2. Both host backends were missing `platform`, which they need because ApplicationController's four
     service accessors return platform types and INCLUDE_DIRECTORIES here is directory-scoped.

Both were found by inspection. Two occurrences is a pattern, not a slip, so the table gets an
instrument.

WHAT IT CHECKS. The design's target directories do not all exist yet. This script maps today's files
onto their target-state owners (TARGET_OWNER below), measures what each owner actually includes, and
compares that against the grant table parsed out of the spec:

  MISSING   an owner includes something its grant list does not permit  -> FAILURE, exit 1
  UNUSED    a grant list permits something nothing includes             -> reported, not fatal
  UNVERIFIABLE  a target component with no files yet                    -> reported, never PASS

UNVERIFIABLE rows are printed rather than silently skipped. A gate that says nothing about the part it
cannot see reads as a pass over the whole table, which is the failure mode `oracle_vocabulary_trap`
already cost us once.
"""
import argparse
import collections
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SPEC = REPO / "docs/superpowers/specs/2026-08-01-sovereign-headless-client-design.md"
SRC = REPO / "source"

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
CODE_EXT = (".hpp", ".cpp", ".h")

# ---------------------------------------------------------------------------------------------
# The one place this script restates the spec: which of today's files each target component takes.
# The register's "assembled from" column says the same thing in prose; assert_register_counts()
# below cross-checks the file counts so the two cannot drift silently.
# ---------------------------------------------------------------------------------------------
FROM_APPLICATION = {
    "gpu": {"StarRenderer.hpp", "StarRenderer.cpp",
            "StarTextureAtlas.hpp", "StarRenderDiagnostics.hpp"},
    "gpu_opengl": {"StarRenderer_opengl.hpp", "StarRenderer_opengl.cpp",
                   "StarGlRenderSurface.hpp", "StarGlRenderSurface.cpp",
                   "StarGlTexturePrimitives.hpp", "StarGlTexturePrimitives.cpp"},
    "host": {"StarApplication.hpp", "StarApplication.cpp", "StarApplicationController.hpp"},
    "host_sdl": {"StarMainApplication.hpp", "StarMainApplication_sdl.cpp"},
    "platform_pc": {"StarPlatformServices_pc.hpp", "StarPlatformServices_pc.cpp",
                    "StarP2PNetworkingService_pc.hpp", "StarP2PNetworkingService_pc.cpp",
                    "StarDesktopService_pc_steam.hpp", "StarDesktopService_pc_steam.cpp",
                    "StarStatisticsService_pc_steam.hpp", "StarStatisticsService_pc_steam.cpp",
                    "StarUserGeneratedContentService_pc_steam.hpp",
                    "StarUserGeneratedContentService_pc_steam.cpp"},
}

# Target components that exist today under their own name and need no remapping.
# `server` was missing here until 2026-08-01, so source/server/ mapped to None and the component
# reported UNVERIFIABLE ("not in the tree") while four of its files sat in the tree. A component the
# instrument cannot see is indistinguishable from one that agrees with the table.
PASSTHROUGH = ("core", "base", "platform", "game", "windowing", "frontend", "rendering", "client",
               "server")

# Declared in the design but with no files yet. Listed so they are reported UNVERIFIABLE rather than
# quietly absent -- an unlisted name would just look like a typo in the grant table.
NOT_YET_BUILT = ("scene", "presentation", "transcript", "host_null", "gpu_sdl",
                 "client_opengl", "client_headless", "client_sdl_gpu",
                 "universe", "world", "universe_view", "world_view", "world_sim", "worldgen", "world_gen",
                 "sound", "mixing", "audio", "audio_sdl", "celestial",
                 "interaction", "client_agent")

# The register's own file counts, cross-checked against FROM_APPLICATION.
REGISTER_COUNTS = {"gpu": 4, "gpu_opengl": 6, "host": 3, "host_sdl": 2, "platform_pc": 10}

# ---------------------------------------------------------------------------------------------
# EDGES THE DESIGN EXISTS TO DELETE. These are not missing grants -- granting them would declare the
# coupling permanent, which is the opposite of the intent. They are a RATCHET: each is measured, each
# may only go down, and the design is finished when every one reaches zero.
#
# Ceilings are the measured counts at the time each was recorded. Lower them as work lands; the gate
# fails if any rises. Set from the first transitive sweep on 2026-08-01.
# ---------------------------------------------------------------------------------------------
REMOVING = {
    ("rendering", "game"): (24, "the headline revocation -- Section 4's one line"),
    ("windowing", "rendering"): (3, "GuiContext draws; under the design it emits into the frame"),
    ("windowing", "gpu"): (1, "GuiContext holds a RendererPtr"),
    ("frontend", "rendering"): (9, "four named files reach WorldPainter/EnvironmentPainter"),
    ("frontend", "gpu"): (1, "Cinematic holds a RendererPtr"),
    ("client", "rendering"): (1, "ClientApplication names a painter"),
    ("client", "gpu"): (1, "RenderingLuaBindings calls app->renderer()"),
    ("client", "host_sdl"): (1, "the STAR_MAIN_APPLICATION macro moves to client_opengl"),
    ("client", "frontend"): (11, "the UI is composed in by an entrypoint that wants one"),
    ("client", "windowing"): (1, "same: `client_agent` must link no widget toolkit"),
    ("host_sdl", "gpu"): (1, "the host should not know what a Renderer is"),
    ("host_sdl", "gpu_opengl"): (1, "the entrypoint constructs the backend, not the host"),
}


def owner_of(path):
    """Target-state component that owns a file of today's tree, or None if it is out of scope."""
    top = path.parts[0]
    if top == "application":
        for component, names in FROM_APPLICATION.items():
            if path.name in names:
                return component
        return None            # discord/ and anything unclaimed
    return top if top in PASSTHROUGH else None


def scan():
    """-> (owner -> Counter of owners it needs, owner -> {target -> files}, owner -> file count).

    TRANSITIVE, because the preprocessor is. If `host_sdl` includes StarApplicationController.hpp and
    that includes StarStatisticsService.hpp, then `platform` must be on host_sdl's include path even
    though no file in host_sdl names it directly. A direct-only sweep reported exactly that grant as
    UNUSED -- a false negative that would have argued for deleting a grant the build needs."""
    owner_of_header, files, headers = {}, collections.Counter(), {}
    paths = []
    # Not rglob("Star*"): entry points are named main.cpp, and one of them holds the server's loop.
    # owner_of() filters everything out of scope, so widening the glob only adds real files.
    for p in SRC.rglob("*"):
        if p.suffix not in CODE_EXT:
            continue
        rel = p.relative_to(SRC)
        if "extern" in rel.parts or "discord" in rel.parts:
            continue
        own = owner_of(rel)
        if own is None:
            continue
        paths.append((p, own))
        files[own] += 1
        if p.suffix in (".hpp", ".h"):
            owner_of_header[p.name] = own
            headers[p.name] = INCLUDE.findall(p.read_text(errors="replace"))

    def closure(names, seen):
        """Every header reachable from `names`, following includes."""
        for n in names:
            base = n.split("/")[-1]
            if base in seen or base not in owner_of_header:
                continue
            seen.add(base)
            closure(headers.get(base, ()), seen)
        return seen

    # NEEDS is transitive -- what the include path must contain for the file to preprocess.
    # DIRECT is what a developer actually deletes, and is the right unit for the removal ratchet.
    needs = collections.defaultdict(collections.Counter)
    direct = collections.defaultdict(collections.Counter)
    sites = collections.defaultdict(lambda: collections.defaultdict(set))
    for p, own in paths:
        listed = INCLUDE.findall(p.read_text(errors="replace"))
        for inc in listed:
            target = owner_of_header.get(inc.split("/")[-1])
            if target and target != own:
                direct[own][target] += 1
                sites[own][target].add(p.name)
        for base in closure(listed, set()):
            target = owner_of_header[base]
            if target != own:
                needs[own][target] += 1
    return needs, direct, sites, files


def parse_grants():
    """Pull the grant table out of the spec. Rows look like:
         | `rendering` | core, base, presentation, scene, gpu | ...statement... |
       Bold markers and an `extern` grant are stripped; `extern` is vendored and out of scope."""
    text = SPEC.read_text(encoding="utf-8")
    grants = {}
    for m in re.finditer(r"^\| ([`\w, ]+) \| ([^|]+) \| [^|]+ \|$", text, re.M):
        names = re.findall(r"`([\w_]+)`", m.group(1))
        if not names:
            continue
        raw = re.sub(r"\*\*", "", m.group(2))
        listed = [x.strip(" `+*") for x in re.split(r"[,+]", raw)]
        listed = [x for x in listed if x and x != "extern"]
        if not listed or not all(re.fullmatch(r"[\w_]+", x) for x in listed):
            continue
        for n in names:
            grants[n] = listed
    return grants


def assert_register_counts(files, out):
    """The register states file counts for the components carved out of `application`. If those
    disagree with FROM_APPLICATION, one of the two moved and the other did not."""
    bad = 0
    for component, expected in sorted(REGISTER_COUNTS.items()):
        actual = files.get(component, 0)
        if actual != expected:
            out.append("  COUNT DRIFT  %-12s register says %d file(s), the mapping finds %d"
                       % (component, expected, actual))
            bad += 1
    return bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 on any missing grant or count drift (gate mode)")
    ap.add_argument("--verbose", action="store_true",
                    help="name the files behind every measured edge")
    args = ap.parse_args(argv)

    if not SPEC.exists():
        print("grant-sweep: spec not found: %s" % SPEC)
        return 2

    needs, direct, sites, files = scan()
    grants = parse_grants()
    if not grants:
        print("grant-sweep: no grant table found in %s -- has the table format changed?" % SPEC.name)
        return 2

    out, removing, missing, unused, unverifiable = [], [], 0, 0, 0
    for component in sorted(grants):
        listed = grants[component]
        if component in NOT_YET_BUILT:
            unverifiable += 1
            out.append("  UNVERIFIABLE %-12s no files yet; grants %s" % (component, ", ".join(listed)))
            continue
        if component not in files:
            unverifiable += 1
            out.append("  UNVERIFIABLE %-12s not in the tree and not declared future" % component)
            continue
        actual = needs.get(component, collections.Counter())
        for target, n in sorted(actual.items(), key=lambda kv: -kv[1]):
            if (component, target) in REMOVING:
                ceiling, why = REMOVING[(component, target)]
                removing.append((component, target, direct[component][target], ceiling, why))
                continue
            if target not in listed:
                missing += 1
                where = ", ".join(sorted(sites[component][target])[:4]) or "transitively only"
                more = "" if len(sites[component][target]) <= 4 else " +%d more" % (
                    len(sites[component][target]) - 4)
                out.append("  MISSING      %-12s includes %-12s x%-4d but is not granted it  [%s%s]"
                           % (component, target, n, where, more))
        for target in listed:
            if target in NOT_YET_BUILT:
                continue                     # cannot be included by anything that exists yet
            if target not in actual and target in files:
                unused += 1
                out.append("  UNUSED       %-12s is granted %-12s but includes nothing from it"
                           % (component, target))

    drift = assert_register_counts(files, out)

    # The ratchet. These edges exist today and the design exists to delete them; granting them would
    # declare the coupling permanent. A rise above the recorded ceiling is a regression.
    risen, total_removing = 0, 0
    for component, target, n, ceiling, why in sorted(removing, key=lambda r: -r[2]):
        total_removing += n
        if ceiling is not None and n > ceiling:
            risen += 1
            out.append("  RISEN        %-12s -> %-12s %d crossings, ceiling %d -- %s"
                       % (component, target, n, ceiling, why))
        else:
            out.append("  REMOVING     %-12s -> %-12s %4d crossings%s -- %s"
                       % (component, target, n,
                          "" if ceiling is None else " (ceiling %d)" % ceiling, why))

    print("grant-sweep: %d grant rows, %d measurable, %d not yet built"
          % (len(grants), len(grants) - unverifiable, unverifiable))
    if args.verbose:
        for component in sorted(direct):
            for target, n in sorted(direct[component].items(), key=lambda kv: -kv[1]):
                print("    %-12s -> %-12s %4d direct include(s) across %d file(s)"
                      % (component, target, n, len(sites[component][target])))
    for line in out:
        print(line)
    print("grant-sweep: MISSING=%d  RISEN=%d  COUNT DRIFT=%d  UNUSED=%d  UNVERIFIABLE=%d"
          % (missing, risen, drift, unused, unverifiable))
    print("grant-sweep: %d crossings remain to be removed across %d declared edges"
          % (total_removing, len(removing)))

    if missing or drift or risen:
        print("grant-sweep: FAIL -- the grant table does not describe the tree")
        return 1 if args.check else 0
    print("grant-sweep: OK -- every measurable grant row matches the tree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
