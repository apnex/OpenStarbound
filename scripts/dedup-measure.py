#!/usr/bin/env python3
"""Measure what the client and the presentation side would BOTH have to ship, if they were split.

WHY THIS EXISTS. "Why this is fully deduplicated" is a section of the target-state architecture, and until
now it was the one north-star claim with no instrument behind it -- asserted, never measured. Section 5
supplies the definition that makes it measurable: a TICK is the highest-order call a loop drives, so it
is the ROOT of a call tree, and everything reachable from it executes at that tick's cadence.

    the deduplication measure  =  closure(clientTick) INTERSECT closure(presentTick)

That intersection is exactly the code that must exist on BOTH machines once the two sides are split
across a network. The design says it should be the scene vocabulary plus the foundations and nothing
else. Anything else in it is a leak, its size is a number, and the number may only go down.

HOW IT MEASURES. Not from includes. `grant-sweep` already measures include closure, which is
PERMISSION -- who may name whom. This measures USE, from the compiled object files: `objdump -dr` gives
every relocation a function emits, so the edges are what the compiler actually generated, not what the
source made legal. The gap between permission and use is where dead grants and undeclared coupling both
hide, which is why both instruments exist.

WHAT IT CANNOT SEE, stated plainly because the number is a LOWER BOUND and would otherwise read as
exact:

  - VIRTUAL CALLS terminate a closure. `call *%rax` names no target. This is not merely a limitation:
    a virtual call through a contract is a SEAM, and Section 5 says the call tree is supposed to stop
    there. The blind spot and the boundary are the same place. Indirect call sites are counted and
    reported so the size of the unknown is visible rather than implied.
  - LUA is wholly opaque. Script callbacks resolve at run time; Section 2 measures that surface
    separately.
  - INLINED callees vanish into their caller, which attributes their cost to the caller's component.
    That makes the measure conservative in the right direction: it under-reports sharing, never over.
  - TEMPLATE AND INLINE SYMBOLS emitted into several objects are attributed to whichever object the
    scan reached first, which is arbitrary. Per-component symbol counts are therefore reliable in
    aggregate and NOT reliable for a small count in one component. File-granularity conclusions do not
    have this problem, because a file is named once.

VERDICTS.

  LEAK      a component in the intersection that the design does not allow to be shared -> FAIL
  RATCHET   the leak total rose above its recorded ceiling                               -> FAIL
  VACUOUS   too few objects or edges parsed to believe the result                        -> FAIL
"""
import argparse
import collections
import concurrent.futures
import importlib.util
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "source"
BUILD = REPO / "build"

# The two roots, as the compiler mangled them. clientTick and presentTick are `update()` and
# `render()` on the class that implements the host's Application contract.
ROOTS = {
    "clientTick": "_ZN4Star17ClientApplication6updateEv",
    "presentTick": "_ZN4Star17ClientApplication6renderEv",
}

# What the design permits both sides to ship. Foundations and contracts only: a contract is shared by
# construction -- that is what a contract is -- and the foundations are shared because everything rests
# on them. Anything else appearing in the intersection is logic living on both machines.
ALLOWED_SHARED = {"core", "base", "scene", "presentation", "host", "platform"}

# Object trees that are not the shipping client.
SKIP_DIRS = {"extern", "test", "utility", "mod_uploader", "json_tool", "discord"}

# The measured leak at the time this was recorded. It is enormous by design: today `update()` and
# `render()` are two methods on ONE class in ONE translation unit, which is precisely why the split
# does not exist yet. This ceiling may only go down.
# MEASURED 2026-08-01 on a current linux-release-clang build. Enormous by design: today `update()`
# and `render()` are two methods on ONE class in ONE translation unit, which is exactly why the split
# does not exist yet. The breakdown corroborates grant-sweep's REMOVING ratchet almost edge for edge --
# game, rendering, frontend, gpu_opengl, host_sdl, client and windowing all appear in both -- which is
# independent evidence that this measures the thing the design exists to delete. It may only go down.
CEILING = 596

FUNC = re.compile(r'^[0-9a-f]+ <(.+)>:$')
RELOC = re.compile(r'R_X86_64_(?:PLT32|PC32|GOTPCREL|REX_GOTPCRELX)\s+(\S+?)(?:[-+]0x[0-9a-f]+)?$')
INDIRECT = re.compile(r'\bcall\w*\s+\*')

_grant_sweep = None


def owner_of(rel):
    """Map a source path onto its TARGET-STATE component, reusing grant-sweep's mapping rather than
    duplicating it -- one hand-authored placement of application/'s files, not two."""
    global _grant_sweep
    if _grant_sweep is None:
        spec = importlib.util.spec_from_file_location("gs", REPO / "scripts/grant-sweep.py")
        _grant_sweep = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_grant_sweep)
    return _grant_sweep.owner_of(pathlib.Path(rel))


def objects():
    """-> [(object path, source path relative to source/)] for the shipping client only."""
    out = []
    for o in sorted(BUILD.rglob("*.o")):
        parts = o.relative_to(BUILD).parts
        if len(parts) < 4 or ".dir" not in " ".join(parts):
            continue
        component = parts[1]
        if component in SKIP_DIRS:
            continue
        tail = o.as_posix().split(".dir/", 1)
        if len(tail) != 2:
            continue
        src = tail[1][:-2]                       # strip the trailing .o
        out.append((o, "%s/%s" % (component, src)))
    return out


def scan_object(job):
    """-> (source, {defined symbols}, {caller: {callees}}, indirect call count).

    Runs in a worker process: objdump is the slow part and there are hundreds of objects."""
    obj, src = job
    try:
        text = subprocess.run(["objdump", "-dr", str(obj)], capture_output=True, text=True,
                              check=True).stdout
    except (subprocess.CalledProcessError, OSError):
        return src, set(), {}, 0
    defined, edges, indirect, current = set(), {}, 0, None
    for line in text.splitlines():
        m = FUNC.match(line)
        if m:
            current = m.group(1)
            defined.add(current)
            edges.setdefault(current, set())
            continue
        if current is None:
            continue
        if INDIRECT.search(line):
            indirect += 1
            continue
        m = RELOC.search(line.strip())
        if m:
            edges[current].add(m.group(1))
    return src, defined, edges, indirect


def build_graph(jobs, workers):
    defines, edges, indirect = {}, collections.defaultdict(set), 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=workers) as pool:
        for src, defined, e, ind in pool.map(scan_object, jobs, chunksize=8):
            indirect += ind
            for sym in defined:
                defines.setdefault(sym, src)
            for caller, callees in e.items():
                edges[caller] |= callees
    return defines, edges, indirect


def closure(root, edges, defines):
    """Symbols reachable from root, following only edges whose target we can resolve to a definition.
    An unresolved target is a leaf -- an external, a virtual dispatch, or a symbol in a skipped tree."""
    seen, stack = set(), [root]
    while stack:
        sym = stack.pop()
        if sym in seen:
            continue
        seen.add(sym)
        for callee in edges.get(sym, ()):
            if callee in defines and callee not in seen:
                stack.append(callee)
    return seen


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 on a leak above the ceiling")
    ap.add_argument("--ceiling", type=int, default=CEILING,
                    help="max shared symbols outside ALLOWED_SHARED; omit to report only")
    ap.add_argument("-j", type=int, default=8, help="worker processes")
    ap.add_argument("--allow-stale", action="store_true",
                    help="measure even if source is newer than the built objects")
    args = ap.parse_args(argv)

    jobs = objects()
    if not jobs:
        print("dedup-measure: SKIP -- no object files under %s. This instrument reads compiled "
              "objects, so it runs only where the tree was built with an ELF toolchain." % BUILD)
        return 0
    if len(jobs) < 100:
        print("dedup-measure: VACUOUS -- %d objects found, which is too few to be a real build. "
              "Some objects present but not the tree: the scan or the layout has changed."
              % len(jobs))
        return 1
    newest_obj = max(o.stat().st_mtime for o, _ in jobs)
    newest_src = max((f.stat().st_mtime for f in SRC.rglob("Star*.[ch]pp")
                      if "extern" not in f.parts), default=0)
    if newest_src > newest_obj and not args.allow_stale:
        print("dedup-measure: STALE -- source is newer than the objects it would measure. "
              "Rebuild, or pass --allow-stale to measure the tree as it was built.")
        return 1

    defines, edges, indirect = build_graph(jobs, args.j)
    if len(edges) < 1000:
        print("dedup-measure: VACUOUS -- only %d call sites parsed; the extraction has regressed"
              % len(edges))
        return 1

    missing = [n for n, s in ROOTS.items() if s not in defines]
    if missing:
        print("dedup-measure: root(s) not found in any object: %s" % ", ".join(missing))
        return 1

    closures = {name: closure(sym, edges, defines) for name, sym in ROOTS.items()}
    shared = closures["clientTick"] & closures["presentTick"]

    by_component = collections.Counter()
    for sym in shared:
        comp = owner_of(defines[sym]) or "unmapped"
        by_component[comp] += 1
    leak = sum(n for c, n in by_component.items() if c not in ALLOWED_SHARED)

    direct = sum(len(v) for v in edges.values())
    resolved = sum(1 for v in edges.values() for c in v if c in defines)
    print("dedup-measure: %d objects, %d symbols, %d direct call edges (%d resolved in-tree), "
          "%d indirect" % (len(jobs), len(defines), direct, resolved, indirect))
    for name, c in sorted(closures.items()):
        print("  closure(%-12s) %6d symbols" % (name, len(c)))
    print("  INTERSECTION   %6d symbols -- what both machines would have to ship" % len(shared))
    print()
    print("  %-14s %7s   %s" % ("component", "symbols", "verdict"))
    for comp, n in by_component.most_common():
        ok = comp in ALLOWED_SHARED
        print("  %-14s %7d   %s" % (comp, n, "shared by design" if ok else "*** LEAK ***"))

    print()
    print("dedup-measure: LEAK = %d symbols outside {%s}" % (leak, ", ".join(sorted(ALLOWED_SHARED))))
    print("               this is a LOWER BOUND: %d indirect call sites were not followed" % indirect)
    if args.ceiling is None:
        print("dedup-measure: no ceiling set -- reporting only, not gating")
        return 0
    if leak > args.ceiling:
        print("dedup-measure: FAIL -- leak %d exceeds ceiling %d" % (leak, args.ceiling))
        return 1 if args.check else 0
    print("dedup-measure: OK -- leak %d is at or below the ceiling %d" % (leak, args.ceiling))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
