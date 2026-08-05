#!/usr/bin/env python3
"""THE SYSTEM-BOUNDARY GRAPH GENERATOR.

It answers, from the tree, the question `docs/architecture/system-boundaries.md` asks: what are this
system's primary boundaries, and what evidence says so?

WHY THIS EXISTS AS AN INSTRUMENT AND NOT A HAND-DRAWN DIAGRAM
-------------------------------------------------------------
Every architecture diagram in this repository that was hand-drawn has been wrong within a week. The
2026-07-19 render set was a picture of the INTENT on the day of the reorg. The target-state doc claimed
BackdropPass held 7 `Root::singleton()` reads against a tree measuring 0, within a day of being written.
That is why `render-inventory.py --inject` exists, and why `docs/render/README.md` carries the rule:

    No document may state a current-state number that an instrument can measure.

A Mermaid edge labelled `675` is such a number. So are node sizes, tier memberships, binary compositions
and singleton counts. All of them are generated here and written between markers; `--check` fails CI if
the document and the tree disagree.

ONE DIAGRAM IS DELIBERATELY NOT GENERATED. The determination-method flowchart in that document contains
no measurable fact -- it is the reasoning that picks the instruments, not an output of them. It is
hand-authored, has no marker, and that is correct rather than an omission.

WHAT IT MEASURES, AND FROM WHERE
--------------------------------
  * GRANTED VISIBILITY -- each source directory's `INCLUDE_DIRECTORIES` block. This is the authority.
    A `#include` of a header outside the grant is a hard compile error, not a lint, which is why the
    cross-directory include graph is acyclic: a cycle is unbuildable. Everything else here is arithmetic
    over that fact.
  * USED EDGES -- actual `#include` directives, resolved to the owning directory by header basename.
  * SEVERABILITY -- which object libraries each declared executable links. Commented-out targets are
    stripped first; the tree carries several.
  * REACH -- `Root::singleton()` per directory. Note this is partly a CONSEQUENCE of the grants: `Root`
    lives in `source/game`, so core/base/application read zero because they cannot see it, not because
    they are virtuous. The document says so; do not read the zero as an achievement.
  * MASS -- files and lines per directory.
  * CROSS-CUTTING CONCERNS -- vocabulary that spans tiers and therefore has no directory to live in.
    These are the subsystems the directory-shaped tests structurally cannot see.
  * RENDER LAYERS -- imported from render-inventory.py, not restated. Two copies of the layer table
    would drift and the two instruments would disagree about what L1 is.

Usage:
    scripts/arch-graph.py                  # every block, to stdout, with its marker key
    scripts/arch-graph.py --facts          # machine-readable JSON
    scripts/arch-graph.py --inject FILE    # write every generated block into the document
    scripts/arch-graph.py --check  FILE    # exit 1 if any block in the document is stale
"""

import functools
import importlib.util
import json
import os
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent


def _load(name, filename):
    spec = importlib.util.spec_from_file_location(name, REPO / "scripts" / filename)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# BORROWED, NOT COPIED -- the same rule boundary-inventory.py follows. layering-lint owns the comment
# stripper and render-inventory owns the render layer table; a second copy of either would drift, and the
# two gates would then disagree about what they are measuring. The hyphens in the filenames are why this
# is importlib and not an import statement.
_layering = _load("layering_lint", "layering-lint.py")
_inventory = _load("render_inventory", "render-inventory.py")
strip_code = _layering.strip_comments_and_strings
RENDER_LAYERS = _inventory.LAYERS

# ---------------------------------------------------------------------------------------------------
# ARCHITECTURAL STATEMENTS. Everything below this line that is not measured is declared here, in one
# place, so that a reader can see the judgement calls separately from the arithmetic.
# ---------------------------------------------------------------------------------------------------

# Tier assignment. This is the one editorial claim in the structural diagrams, and the measurement can
# contradict it: `application` is granted only core+platform, so it is a SIBLING of base at tier 2, not
# a member of the presentation stack it is always drawn inside. If a future grant list moves a directory,
# `--check` will not catch it -- but assert_tiers_match_grants() below will.
TIERS = [
    ("T0 vendored", ["extern"]),
    ("T1 language", ["core"]),
    # `metrics` is a T2 sibling of `base`, not a member of any stack above it: it is granted `core`
    # and nothing else, deliberately, because a measurement that named what it measures would be a
    # peer of its subject. Left out of TIERS it would have been generated as "outside the tier
    # lattice; measured by nothing here" -- filed beside `json_tool` as scaffolding, which is the
    # opposite of what registering it as a sovereign component was for.
    ("T2 services", ["base", "metrics", "platform", "application"]),
    ("T3 simulation", ["game"]),
    ("T4 presentation", ["rendering", "windowing", "frontend"]),
    ("T5 shells", ["client", "server"]),
]
DIRS = [d for _t, ds in TIERS for d in ds]
TIER_OF = {d: t for t, ds in TIERS for d in ds}

# Edge classification. The threshold is on FILES TOUCHED, not include count, because files predict the
# work of severing an edge and include count does not: `windowing -> rendering` is 3 includes but only 1
# file, which is an afternoon; `windowing -> base` is 19 includes across 17 files, which is not.
THIN_MAX_FILES = 9

# The six top-level parts of the system. Only one of them is the engine, and four of the other five are
# invisible to any tool that only reads source/. The counts are measured; the taxonomy is the claim.
TOP_LEVEL = [
    ("Engine", "source/", "the C++ tree -- a compile-enforced tier lattice"),
    ("Content", "assets/", "separately versioned, loaded at runtime, ships in both directions"),
    ("Protocol", "net + save", "a compatibility surface spanning core and game, with no directory"),
    ("Toolchain", "cmake/vcpkg", "decides which binaries exist at all"),
    ("Instruments", "scripts/ + tests", "what makes the other boundaries enforceable rather than stated"),
    ("Governance", "docs/", "where a boundary is declared before it is enforced"),
]

# CROSS-CUTTING CONCERNS -- real subsystems with no directory. The first three tests in the document are
# all directory-shaped and structurally cannot see these; they are found by reading duty, which is why
# the vocabulary is declared rather than discovered.
CROSS_CUTTING = [
    ("Scripting", r"\bLua(Engine|Root|Context|Callbacks|Value|Converters|Bindings)\b",
     "Lua VM is 4 files in core; the binding surface is spread across six directories"),
    ("Protocol", r"\b(NetElement|DataStream|Packet)\w*\b",
     "wire format and save format, versioned independently of the code that reads them"),
    ("Telemetry", r"\bTelemetry::(timer|counter|gauge)\b",
     "the measurement substrate the perf campaign runs on"),
    ("Assets", r"\b(Assets|AssetPath|AssetSource)\b",
     "loader in base, consumed everywhere, content lives outside the tree entirely"),
]

# Cross-layer dependency edges in the render class diagram are capped so the picture stays readable. The
# cap is REPORTED rather than silently applied -- a truncated diagram that looks complete is the same
# defect class as a ratchet that meters three of seventeen sites (#183).
MAX_CLASS_EDGES = 24

# THE PRESENTATION TIER'S THREE DUTIES. The tier lattice treats T4 as one row; it is three directories
# doing three different jobs, and the difference is not written down anywhere else. The duty strings are
# the claim; everything measured beside them below is what tests it.
PRESENTATION_DUTY = [
    ("rendering", "draws the WORLD — tiles, entities, lighting, parallax, sky"),
    ("windowing", "a WIDGET TOOLKIT — layout, hit-testing, focus, key bindings, widget trees from JSON"),
    ("frontend", "THIS GAME'S SCREENS — inventory, crafting, quests, chat, menus, built from widgets"),
]

# AMBIENT vs DOMAIN game headers, for the presentation-tier duty check. `Root` appears in almost every UI
# file and says nothing -- it is the god-object, already measured in the reach section, and counting it
# here would bury the signal underneath it. The question a toolkit widget should fail is narrower: does it
# name one of the simulation's NOUNS? A widget that knows what an Item is cannot be generic; a widget that
# reads Root is merely living in this codebase.
GAME_AMBIENT = {
    "StarRoot.hpp", "StarGameTypes.hpp", "StarGameTimers.hpp", "StarInput.hpp",
    "StarImageMetadataDatabase.hpp", "StarDrawable.hpp", "StarLuaGameConverters.hpp",
}

# THE SHAPE TEST. Tests 1-4 ask whether a boundary EXISTS. This asks whether it is any good, which is a
# different axis and the one most architecture work actually turns on: a boundary can be perfectly
# enforced and still be badly shaped.
#
# The predicate: for a data aggregate that crosses a tier, how much of it does the consumer actually
# read? A consumer that receives 23 members and reads 3 is being handed a bundle, not an interface.
#
# THE FILTERS ARE THE MEASUREMENT, and each was learned by getting a false answer without it:
#   * struct, not class    -- for a CLASS, low fit is ENCAPSULATION WORKING, not a defect. Without this
#                             the metric reported `Object` (59 members) at 3% fit in five consumers and
#                             called it a finding. It is not; those consumers hold a pointer and call
#                             two methods, which is correct design.
#   * data-dominant        -- a struct with member functions is a class in disguise; same argument.
#   * not foundation-owned -- a core utility type used everywhere is fine by construction.
#   * cross-TIER only      -- within a tier, wide sharing is the point of being in the same tier.
#   * >= SHAPE_MIN_MEMBERS -- a 4-member struct at 50% fit is not evidence of anything.
#
# Member reads are counted as `.name` or `->name`, which is why the aggregate must be data-dominant:
# a bare identifier match would collide with every local variable of the same name.
# VENDORED SUBTREES, skipped by the recursive file scan. These are third-party sources that live inside
# our directories; they are not our architecture and counting them distorts every measure here -- and
# `extern/.../core.h` collides by basename with `application/discord/core.h`, which correctly tripped the
# ambiguous-basename guard the moment the scan started recursing. Declared rather than pattern-matched so
# that adding a vendored tree is a visible decision.
VENDORED_SUBTREES = (
    "extern/curve25519", "extern/fmt", "extern/lua",
    "application/discord",
    "test/gtest",
)

SHAPE_MIN_MEMBERS = 8
SHAPE_MAX_FUNCS = 2
SHAPE_WHOLESALE = 0.34   # below this, the consumer receives far more than it reads
SHAPE_FITTED = 0.67      # above this, the type is shaped to its consumer

# COHESION. The document's own headline advice is "to make a boundary real, make it a directory with its
# own grant list". This measures whether that route is even OPEN: a directory whose internal include
# graph is one giant strongly-connected component cannot be partitioned, because there is no cut. The
# advice was written before this was measured and was wrong for the directory it most wanted to apply to.
COHESION_BLOB = 0.50     # largest SCC >= half the directory: no partition exists
COHESION_PARTLY = 0.15
COHESION_MIN_UNITS = 5   # below this the ratio is noise -- a 2-file shell is trivially one component

SINGLETON = re.compile(r"Root::singleton\(\)")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
SRC_EXT = (".cpp", ".hpp", ".h", ".c")
HDR_EXT = (".hpp", ".h")

MARK_BEGIN = "<!-- BEGIN GENERATED: scripts/arch-graph.py#%s -->"
MARK_END = "<!-- END GENERATED: %s -->"


# ---------------------------------------------------------------------------------------------------
# MEASUREMENT
# ---------------------------------------------------------------------------------------------------

@functools.lru_cache(maxsize=None)
def read(path):
    """Memoized. Seven measurement passes walk the same ~900 files, and re-reading them each time cost
    this gate five seconds against the other script gates' one. The cache is keyed on the path object,
    which is safe here because the script measures a fixed tree and never writes back to source/.

    encoding="utf-8" is pinned deliberately: Python's text mode defaults to the LOCALE encoding, which
    is cp1252 on the windows-latest CI image, and a doc full of em dashes decodes there as mojibake --
    so --check would compare a fresh block against a corrupted copy of itself and report drift that does
    not exist. That is #192, and it cost a red CI to learn once already."""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


@functools.lru_cache(maxsize=None)
def code(path):
    """Comments and string literals stripped, memoized. Stripping is not optional: render-inventory.py's
    first run reported a "direct GL call" in an L3 pass that turned out to be a comment explaining why
    the pass does NOT make it. An instrument that cannot tell code from commentary about code will
    manufacture exactly the findings it was built to detect."""
    return strip_code(read(path))


@functools.lru_cache(maxsize=None)
def files_in(d):
    """Every source file under source/<d>, RECURSIVELY, as paths relative to that directory.

    This walked only the top level until 2026-07-26, and the omission was invisible until the cohesion
    measurement -- which prototyped with os.walk -- disagreed with the generator about how many
    translation units source/game has: 264 against 172. source/game has five subdirectories
    (interfaces, items, objects, scripting, terrain) holding 162 files and 19,230 lines, and every
    count this script produced excluded all of them. application/discord was missing too.

    The lesson is the one this campaign keeps paying for: two implementations of the same measurement
    disagreeing is the only reason anyone looked. A single instrument is unfalsifiable by construction."""
    root = REPO / "source" / d
    if not root.is_dir():
        return ()
    out = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            if not fn.endswith(SRC_EXT):
                continue
            rel = (pathlib.Path(dirpath) / fn).relative_to(root).as_posix()
            if any(("%s/%s" % (d, rel)).startswith(v + "/") for v in VENDORED_SUBTREES):
                continue
            out.append(rel)
    return tuple(sorted(out))


def cmake_head(d):
    """The INCLUDE_DIRECTORIES block, which is the grant declaration and therefore the authority."""
    text = read(REPO / "source" / d / "CMakeLists.txt")
    m = re.search(r"INCLUDE_DIRECTORIES\s*\((.*?)\)", text, re.S | re.I)
    return m.group(1) if m else ""


def grants():
    out = {}
    for d in DIRS:
        found = {m.lower() for m in re.findall(r"STAR_([A-Z]+)_INCLUDES", cmake_head(d))}
        out[d] = {x for x in found if x in DIRS and x != d}
    return out


def owner_map():
    """Header basename -> owning directory, plus any collisions, which would mis-attribute silently."""
    owner, dupes = {}, {}
    for d in DIRS:
        for f in files_in(d):
            if not f.endswith(HDR_EXT):
                continue
            # BASENAME, because that is what an #include resolves by. files_in() now returns paths
            # relative to the directory, so `items/StarFoo.hpp` must key as `StarFoo.hpp` or every
            # include of a subdirectory header silently resolves to no owner.
            b = os.path.basename(f)
            if b in owner:
                dupes.setdefault(b, [owner[b]]).append(d)
            else:
                owner[b] = d
    return owner, dupes


def uses():
    """Used include edges: (consumer, provider) -> (include count, distinct consumer files)."""
    owner, _dupes = owner_map()
    counts, filesets = {}, {}
    for d in DIRS:
        for f in files_in(d):
            src = read(REPO / "source" / d / f)
            for inc in INCLUDE.findall(src):
                o = owner.get(os.path.basename(inc))
                if o and o != d:
                    counts[(d, o)] = counts.get((d, o), 0) + 1
                    filesets.setdefault((d, o), set()).add(f)
    return {k: (v, len(filesets[k])) for k, v in counts.items()}


def reduce_transitively(g):
    """The Hasse diagram of the grant partial order.

    Grant lists are cumulative -- `game` names core AND base AND platform -- so the raw graph has ~45
    edges and no shape. The reduction has ~11 and IS the shape: in particular it shows that base and
    platform+application are incomparable branches at tier 2 that only join at rendering, which is the
    finding that `application` is not a presentation library."""
    out = {}
    for a, targets in g.items():
        keep = set()
        for b in targets:
            if not any(b in g.get(c, ()) for c in targets if c != b):
                keep.add(b)
        out[a] = keep
    return out


def sizes():
    out = {}
    for d in DIRS:
        fs = files_in(d)
        n = sum(len(read(REPO / "source" / d / f).splitlines()) for f in fs)
        out[d] = (len(fs), n)
    return out


def reach():
    out = {}
    for d in DIRS:
        refs = touched = 0
        for f in files_in(d):
            n = len(SINGLETON.findall(code(REPO / "source" / d / f)))
            if n:
                refs += n
                touched += 1
        out[d] = (touched, refs)
    return out


def binaries():
    """Declared executables and the object libraries each links.

    Two things this must get right, both learned by getting them wrong. Commented-out targets are
    stripped first -- source/utility/CMakeLists.txt carries seven, and counting those as shipping
    artifacts would overstate severability by a third. And the argument list is found by MATCHING
    PARENTHESES, not by a regex: most ADD_EXECUTABLE blocks close inline (`...RESOURCES})`) rather
    than on their own line, so a `^\\s*\\)` terminator silently skipped all but one target and then
    over-captured into the next command."""
    out = {}
    head = re.compile(r"\bADD_EXECUTABLE\s*\(", re.I)
    for cm in sorted((REPO / "source").glob("*/CMakeLists.txt")):
        live = "\n".join(l for l in read(cm).splitlines() if not l.lstrip().startswith("#"))
        for m in head.finditer(live):
            depth, i = 1, m.end()
            while i < len(live) and depth:
                depth += (live[i] == "(") - (live[i] == ")")
                i += 1
            body = live[m.end():i - 1]
            name = body.split(None, 1)[0] if body.split() else None
            if name:
                out[name] = frozenset(re.findall(r"TARGET_OBJECTS:star_(\w+)", body))
    return out


def crosscut():
    out = {}
    for name, pattern, _why in CROSS_CUTTING:
        rx = re.compile(pattern)
        per = {}
        for d in DIRS:
            n = sum(1 for f in files_in(d) if rx.search(code(REPO / "source" / d / f)))
            if n:
                per[d] = n
        out[name] = per
    return out


def system_parts():
    """Counts for the six top-level parts. Every one is measured; the taxonomy above is the claim."""
    src_files = sum(len(files_in(d)) for d in DIRS)
    src_lines = sum(sizes()[d][1] for d in DIRS)
    assets = list((REPO / "assets").rglob("*")) if (REPO / "assets").is_dir() else []
    asset_files = [p for p in assets if p.is_file()]
    proto = crosscut().get("Protocol", {})
    toolchain = (list((REPO / "cmake").glob("*.cmake")) + list((REPO / "toolchains").rglob("*"))
                 + list((REPO / "triplets").rglob("*")) + list(REPO.glob("CMakePresets.json"))
                 + list((REPO / "source").rglob("CMakeLists.txt")))
    scripts = [p for p in (REPO / "scripts").iterdir()
               if p.is_file() and p.suffix in (".py", ".sh")] if (REPO / "scripts").is_dir() else []
    ctests = len(re.findall(r"ADD_TEST", read(REPO / "source" / "test" / "CMakeLists.txt"), re.I))
    ci = len(re.findall(r"^\s+- name:", read(REPO / ".github" / "workflows" / "gates.yml"), re.M))
    docs = list((REPO / "docs").rglob("*.md")) if (REPO / "docs").is_dir() else []
    return {
        "Engine": ["%d files" % src_files, "%d lines" % src_lines, "%d tiers" % len(TIERS)],
        "Content": ["%d files in tree" % len(asset_files),
                    "%d lua" % sum(1 for p in asset_files if p.suffix == ".lua"),
                    "vanilla pak is external"],
        "Protocol": ["%d files name it" % sum(proto.values()),
                     "spans %d directories" % len(proto), "no directory of its own"],
        "Toolchain": ["%d build files" % len([p for p in toolchain if p.is_file()]),
                      "%d declared binaries" % len(binaries())],
        "Instruments": ["%d scripts" % len(scripts), "%d ctest gates" % ctests, "%d CI gates" % ci],
        "Governance": ["%d markdown documents" % len(docs)],
    }


# Bases that are not ours. `enable_shared_from_this` is std; without this filter it is reported as a
# class of unknown home and then flagged as a library-boundary crossing -- a manufactured finding of
# exactly the kind render-inventory.py's comment stripper exists to prevent.
EXTERNAL_BASES = {"enable_shared_from_this", "true_type", "false_type", "exception", "runtime_error"}

# FOUNDATION LIBRARIES. Inheriting a base from one of these is not a finding: `RefCounter` lives in core
# because everything is meant to use it, and `GlSurface : RefCounter` is the intended use. Reporting that
# beside `TilePainter : TileDrawer` would make the one real finding a third of a list instead of the
# whole of it -- the same dilution a ratchet suffers when it meters sites nobody is going to change.
FOUNDATION = {"extern", "core", "base", "platform"}

DECL = re.compile(r"^[ \t]*(?:class|struct)\s+(\w+)\s*(?::\s*([^{;]+))?\{", re.M)


def split_bases(spec):
    """Split a base-clause on commas at angle-bracket depth zero, and strip access specifiers and
    template arguments. `enable_shared_from_this<GlTextureGroup>, public TextureGroup` is two bases,
    and a naive split on the first token loses the second -- which is the real one."""
    out, depth, cur = [], 0, ""
    for ch in spec:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    names = []
    for part in out:
        part = re.sub(r"\b(public|private|protected|virtual)\b", " ", part)
        part = re.sub(r"<.*?>", "", part, flags=re.S).strip()
        part = part.split("::")[-1].strip()
        if re.fullmatch(r"\w+", part or ""):
            names.append(part)
    return names


def declarations_in(path):
    """(class name, [base names]) for every class or struct declared in a file, nested included."""
    return [(m.group(1), split_bases(m.group(2) or "")) for m in DECL.finditer(code(path))]


def class_homes():
    """class name -> owning directory, measured from declarations across the whole tree.

    This exists because the obvious shortcut is wrong: resolving a base class by looking for
    `Star<Base>.hpp` fails for every class declared inside another class's header -- Texture, GpuTimer,
    RenderBuffer, RenderOracle and TextureAtlasSet all live inside StarRenderer.hpp. The first run of
    this script resolved all five to "unknown" and then reported them as crossing a library boundary:
    eleven false positives around the one real finding."""
    home = {}
    for d in DIRS:
        for f in files_in(d):
            for cls, _bases in declarations_in(REPO / "source" / d / f):
                home.setdefault(cls, d)
    return home


def render_classes():
    """Classes per render layer, and every inheritance edge in the render tree.

    Measured rather than listed, because the one that matters -- `TilePainter : public TileDrawer` --
    is a render class inheriting a game class across a library boundary, and a hand-maintained list
    would lose the second one the day someone adds it."""
    layer_of, rep_of, layer_dirs = {}, {}, {}
    for name, _purpose, files in RENDER_LAYERS:
        for rel in files:
            layer_of[pathlib.Path(rel).name] = name
            # Which directory a layer LIVES in, measured from the table rather than assumed. This is the
            # question a reader of the treemap cannot answer: L1 is in source/application at tier 2,
            # below the simulation, while L2/L3 are in source/rendering at tier 4, above it. The render
            # decomposition is not contiguous in the tier lattice and no directory-shaped diagram says so.
            layer_dirs.setdefault(name, set()).add(rel.split("/")[1])

    homes = class_homes()
    classes, inherits, decls_by_file = {}, [], {}
    for d in ("application", "rendering"):
        for rel in files_in(d):
            f = os.path.basename(rel)
            if not f.endswith(HDR_EXT) or f not in layer_of:
                continue
            layer = layer_of[f]
            decls = declarations_in(REPO / "source" / d / rel)
            decls_by_file[f] = [c for c, _b in decls]
            for cls, bases in decls:
                classes.setdefault(layer, set()).add(cls)
                for base in bases:
                    if base in EXTERNAL_BASES:
                        continue
                    inherits.append((cls, base, layer, homes.get(base, "external")))

    # REPRESENTATIVE CLASS PER FILE, for the include-derived dependency edges. Preference order:
    # the class named after the file, else the first declared. Stated because it is a heuristic --
    # StarGlRenderSurface.hpp declares no `GlRenderSurface`, and its representative is `GlSurface`.
    for f, decls in decls_by_file.items():
        stem = pathlib.Path(f).stem
        want = stem[4:] if stem.startswith("Star") else stem
        rep_of[f] = want if want in decls else (decls[0] if decls else want)
    return classes, inherits, rep_of, layer_of, homes, layer_dirs


def cross_layer_edges(rep_of, layer_of):
    """Include edges between render layers, lifted to representative classes."""
    seen, over = set(), 0
    for d in ("application", "rendering"):
        for rel in files_in(d):
            f = os.path.basename(rel)
            if f not in layer_of or f not in rep_of:
                continue
            for inc in INCLUDE.findall(read(REPO / "source" / d / rel)):
                b = os.path.basename(inc)
                if b in layer_of and layer_of[b] != layer_of[f] and rep_of.get(b) != rep_of[f]:
                    edge = (rep_of[f], rep_of.get(b, b))
                    if edge in seen:
                        continue
                    if len(seen) < MAX_CLASS_EDGES:
                        seen.add(edge)
                    else:
                        over += 1
    return sorted(seen), over


STRUCT = re.compile(r"^struct\s+(\w+)\s*(?::[^{]*)?\{", re.M)
FIELD = re.compile(r"^\s{2,}(?!return|using|typedef|friend)[\w:<>,\s\*&]+?[\s\*&](\w+)\s*(?:=[^;]*)?;\s*$")
FUNCLIKE = re.compile(r"\w+\s*\([^)]*\)\s*(?:const)?\s*[;{]")


def data_structs(d, f):
    """Data-dominant structs declared in one file: name -> member names."""
    src = code(REPO / "source" / d / f)
    out = {}
    for m in STRUCT.finditer(src):
        i, depth = m.end(), 1
        while i < len(src) and depth:
            depth += (src[i] == "{") - (src[i] == "}")
            i += 1
        members, funcs = [], 0
        for line in src[m.end():i - 1].splitlines():
            if FUNCLIKE.search(line):
                funcs += 1
                continue
            fm = FIELD.match(line)
            if fm:
                members.append(fm.group(1))
        if len(members) >= SHAPE_MIN_MEMBERS and funcs <= SHAPE_MAX_FUNCS:
            out[m.group(1)] = members
    return out


def shape():
    """Cross-tier aggregate crossings, and how much of each the consumer actually reads."""
    owners = {}
    for d in DIRS:
        if d in FOUNDATION:
            continue
        for f in files_in(d):
            if f.endswith(HDR_EXT):
                for name, members in data_structs(d, f).items():
                    owners.setdefault(name, (d, f, members))
    order = {t: i for i, (t, _ds) in enumerate(TIERS)}
    rows = []
    for name, (hd, hf, members) in owners.items():
        rx = re.compile(r"\b%s\b" % re.escape(name))
        reads = [re.compile(r"[.\->]\b%s\b" % re.escape(m)) for m in members]
        for d in DIRS:
            if d == hd or order[TIER_OF[d]] == order[TIER_OF[hd]]:
                continue
            for f in files_in(d):
                src = code(REPO / "source" / d / f)
                if not rx.search(src):
                    continue
                used = sum(1 for r in reads if r.search(src))
                if used:
                    rows.append((used / len(members), name, hd, len(members), d, f, used))
    return sorted(rows)


def presentation():
    """The T4 directories' duties, tested three ways.

    (a) how much of `game` each names -- a widget toolkit should name none of it;
    (b) which source/rendering headers are consumed from OUTSIDE source/rendering -- the ones that are
        turn out not to be render-subsystem-internal at all but shared infrastructure, which is the
        standing explanation for why the painters were never decomposed;
    (c) which source/windowing files name game types -- game-aware widgets sitting in the toolkit."""
    owner, _d = owner_map()
    t4 = [d for d, _why in PRESENTATION_DUTY]

    # (b) external consumers of each source/rendering header
    external = {}
    for d in DIRS:
        if d == "rendering":
            continue
        for rel in files_in(d):
            for inc in INCLUDE.findall(read(REPO / "source" / d / rel)):
                b = os.path.basename(inc)
                if owner.get(b) == "rendering":
                    external.setdefault(b, set()).add(d)

    # (c) windowing files that name a game header, and which ones
    gameaware = []
    for rel in files_in("windowing"):
        hits = []
        for inc in INCLUDE.findall(read(REPO / "source" / "windowing" / rel)):
            b = os.path.basename(inc)
            if owner.get(b) == "game":
                hits.append(b)
        hits = [h for h in hits if h not in GAME_AMBIENT]
        if hits:
            gameaware.append((os.path.basename(rel), sorted(set(hits))))
    return t4, external, sorted(gameaware, key=lambda kv: (-len(kv[1]), kv[0]))


def scc(graph, nodes):
    """Tarjan, iterative. Recursive would need a raised recursion limit for a 226-node component, and a
    gate that dies with RecursionError on a big directory is a gate nobody trusts."""
    index, low, onstack, stack, out, counter = {}, {}, {}, [], [], [0]
    for root in nodes:
        if root in index:
            continue
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        onstack[root] = True
        work = [(root, iter(sorted(graph.get(root, ()))))]
        while work:
            v, it = work[-1]
            descended = False
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stack.append(w)
                    onstack[w] = True
                    work.append((w, iter(sorted(graph.get(w, ())))))
                    descended = True
                    break
                if onstack.get(w):
                    low[v] = min(low[v], index[w])
            if descended:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    onstack[w] = False
                    comp.append(w)
                    if w == v:
                        break
                out.append(comp)
    return sorted(out, key=len, reverse=True)


def cohesion():
    """Per directory, TWO graphs over the same nodes, because they answer different questions.

    A translation unit is Star*.hpp + Star*.cpp as one node.

      ALL EDGES     -- every internal include. Answers: can these files be moved into sub-directories as
                       they stand? A cycle blocks that, because a grant list cannot be handed to a
                       directory whose files include across the proposed boundary.
      HEADERS ONLY  -- .hpp including .hpp. Answers the deeper question: are the DECLARATIONS
                       hierarchical? That is what decides whether a decomposition is POSSIBLE at all.

    THE SECOND GRAPH WAS NOT HERE ORIGINALLY, and its absence made this section assert that `game` had
    no partition when what it had measured was that `game` has no FREE partition. The difference is not
    academic: every directory in this tree is acyclic at header level, so every cycle reported by the
    first graph is implementation-side. A library boundary is enforced on headers -- a .cpp reaching
    across a boundary is what a boundary is FOR -- so an acyclic declaration graph means the strata
    exist and nobody has drawn them, which is a very different problem from breaking a 226-node cycle."""
    out = {}
    # Basename, because an include names a basename; a subdirectory path would never match one.
    stem = lambda n: re.sub(r"\.(hpp|cpp|h|c)$", "", os.path.basename(n))
    for d in DIRS:
        fs = files_in(d)
        if not fs:
            continue
        units = {stem(f) for f in fs}
        graphs = [{}, {}]
        for f in fs:
            from_header = f.endswith(HDR_EXT)
            for inc in INCLUDE.findall(read(REPO / "source" / d / f)):
                base = os.path.basename(inc)
                b = stem(base)
                if b not in units or b == stem(f):
                    continue
                graphs[0].setdefault(stem(f), set()).add(b)
                if from_header and base.endswith(HDR_EXT):
                    graphs[1].setdefault(stem(f), set()).add(b)
        big = [len(c[0]) if (c := scc(g, sorted(units))) else 0 for g in graphs]
        out[d] = (len(units), sum(len(v) for v in graphs[0].values()), big[0], big[1])
    return out


def assert_tiers_match_grants(g):
    """TIERS is editorial; the grants are not. If a directory's grants ever contradict its declared tier,
    say so loudly rather than drawing a diagram that is wrong in a way nobody can see."""
    order = {t: i for i, (t, _ds) in enumerate(TIERS)}
    bad = []
    for a, targets in g.items():
        for b in targets:
            if order[TIER_OF[b]] > order[TIER_OF[a]]:
                bad.append("%s (%s) is granted %s (%s) -- a grant pointing UP a tier"
                           % (a, TIER_OF[a], b, TIER_OF[b]))
    return bad


# ---------------------------------------------------------------------------------------------------
# DIAGRAMS
# ---------------------------------------------------------------------------------------------------

def heat_class(refs):
    if refs == 0:
        return "clean"
    if refs < 50:
        return "warm"
    if refs < 250:
        return "hot"
    return "blaze"


def dir_counts(p):
    """Recursive source-file and line counts beneath a directory, PRUNING VENDORED SUBTREES.

    The pruning is not optional. files_in() excludes vendored trees, so every other number in this
    document does too; a tree whose rollups included them would have shown `extern` at 96 files against
    the lattice's 18 and `application` at 56 against 25 -- the document contradicting itself inside two
    adjacent sections. A view added to make omissions visible must not introduce one."""
    files = lines = 0
    src = REPO / "source"
    for dirpath, dirnames, fns in os.walk(p):
        rel = pathlib.Path(dirpath).relative_to(src).as_posix()
        if is_vendored(rel):
            dirnames[:] = []
            continue
        for fn in fns:
            if fn.endswith(SRC_EXT):
                files += 1
                lines += len(read(pathlib.Path(dirpath) / fn).splitlines())
    return files, lines


def is_vendored(rel):
    return any(rel == v or rel.startswith(v + "/") for v in VENDORED_SUBTREES)


def dir_tree(p, rel=""):
    """Every directory beneath p that holds source somewhere, at ANY depth. Vendored subtrees are
    reported but not descended into -- the whole subtree is excluded, so enumerating its internals
    would be noise about code that is not ours."""
    out = []
    for sub in sorted(x for x in p.iterdir() if x.is_dir()):
        r = "%s/%s" % (rel, sub.name) if rel else sub.name
        # Vendored is tested BEFORE counting, because dir_counts() prunes vendored trees to zero and a
        # zero-count node would then be dropped by the emptiness check below -- silently disappearing
        # the very rows that exist to show what is excluded. Excluded from the COUNTS, present in the VIEW.
        if is_vendored(r):
            if any(fn.endswith(SRC_EXT) for _dp, _dn, fns in os.walk(sub) for fn in fns):
                out.append((sub.name, r, 0, 0, True, []))
            continue
        files, lines = dir_counts(sub)
        if not files:
            continue
        out.append((sub.name, r, files, lines, False, dir_tree(sub, r)))
    return out


def d_tree(m):
    """3. The engine's shape on disk -- the one view that shows containment rather than relationships."""
    root = REPO / "source"
    all_top = {name: node for node in dir_tree(root) for name in [node[0]]}
    # Lattice directories in tier order first, then everything else. The "everything else" is the point:
    # source/test, utility, json_tool and mod_uploader are real code that NO measurement in this document
    # covers, because TIERS does not name them. A tree that quietly omitted them would hide that.
    ordered = [(d, TIER_OF[d]) for _t, ds in TIERS for d in ds if d in all_top]
    ordered += [(n, None) for n in sorted(all_top) if n not in TIER_OF]

    lines_out = ["```", "source/"]

    def emit(node, prefix, last):
        name, _rel, files, nlines, vendored, kids = node
        branch = "└── " if last else "├── "
        label = ("vendored — excluded from every count here" if vendored
                 else "%4d files  %8s lines" % (files, "{:,}".format(nlines)))
        lines_out.append("%s%s%-16s %s" % (prefix, branch, name + "/", label))
        child_prefix = prefix + ("    " if last else "│   ")
        for i, kid in enumerate(kids):
            emit(kid, child_prefix, i == len(kids) - 1)

    for i, (name, tier) in enumerate(ordered):
        node = all_top[name]
        last = i == len(ordered) - 1
        _n, _rel, files, nlines, vendored, kids = node
        branch = "└── " if last else "├── "
        tag = tier.split()[0] if tier else "—"
        note = ("" if tier else "   ← outside the tier lattice; measured by nothing here")
        lines_out.append("%s%-16s %-3s %4d files  %8s lines%s"
                         % (branch, name + "/", tag, files, "{:,}".format(nlines), note))
        child_prefix = "    " if last else "│   "
        for j, kid in enumerate(kids):
            emit(kid, child_prefix, j == len(kids) - 1)
    lines_out.append("```")

    outside = [n for n, t in ordered if t is None]
    out_files = sum(all_top[n][2] for n in outside)
    deepest = 0

    def depth_of(nodes, d=1):
        nonlocal deepest
        for n in nodes:
            deepest = max(deepest, d)
            depth_of(n[5], d + 1)
    depth_of([all_top[n] for n, _t in ordered])

    lines_out.append("")
    lines_out.append("**Every directory that holds code of ours, at every depth — and no files.** "
                     "Counts are recursive and exclude vendored subtrees, matching every other number "
                     "in this document. Vendored trees are named but not descended into, since the whole "
                     "subtree is out of scope and listing its internals would be noise about code that "
                     "is not ours. Set those aside and the tree is only %d level%s deep: the engine's "
                     "structure is flatter than its size suggests, which is itself the finding — "
                     "`source/game` carries 500 files with exactly five subdirectories and no boundary "
                     "between them." % (deepest, "" if deepest == 1 else "s"))
    lines_out.append("")
    lines_out.append("Two things this view exists to make impossible to miss. **Subdirectories hide "
                     "real code** — `arch-graph.py` walked past every one of them until 2026-07-26, "
                     "and `source/game` alone hid 162 files and 19,230 lines from every number this "
                     "document published. And **%d top-level directories (%d files) sit outside the "
                     "tier lattice entirely**: %s. They are real code that `TIERS` does not name, so "
                     "no test in this document covers them. That is a scope boundary, and it should be "
                     "visible rather than inferred from an absence."
                     % (len(outside), out_files, ", ".join("`%s`" % n for n in outside)))
    return "\n".join(lines_out)


def d_lattice(m):
    """1. The grant lattice -- what the compiler permits, transitively reduced."""
    red = reduce_transitively(m["grants"])
    out = ["```mermaid", "flowchart TD"]
    for tier, ds in TIERS:
        present = [d for d in ds if d in m["sizes"]]
        if not present:
            continue
        out.append('  subgraph %s["%s"]' % (tier.split()[0], tier))
        out.append("    direction LR")
        for d in present:
            touched, refs = m["reach"][d]
            out.append('    %s["%s<br/><small>%d files · %s lines · Root×%d</small>"]'
                       % (d, d, m["sizes"][d][0], "{:,}".format(m["sizes"][d][1]), refs))
        out.append("  end")
    for a in DIRS:
        for b in sorted(red.get(a, ())):
            out.append("  %s --> %s" % (a, b))
    out.append("  classDef clean fill:#1b4332,stroke:#2d6a4f,color:#d8f3dc")
    out.append("  classDef warm  fill:#5c4d1e,stroke:#8a7420,color:#fff3bf")
    out.append("  classDef hot   fill:#7f3e12,stroke:#b5561b,color:#ffe8d6")
    out.append("  classDef blaze fill:#7a1420,stroke:#c1121f,color:#ffe5e5")
    for bucket in ("clean", "warm", "hot", "blaze"):
        members = [d for d in DIRS if heat_class(m["reach"][d][1]) == bucket]
        if members:
            out.append("  class %s %s" % (",".join(members), bucket))
    out.append("```")
    out.append("")
    out.append("Arrows point from consumer to provider and are **transitively reduced** -- `game` is "
               "granted `core` directly, but the edge is implied through `base` and drawing it adds no "
               "information. Node fill is `Root::singleton()` density: green is zero, red is over 250.")
    return "\n".join(out)


def d_grantuse(m):
    """2. Granted vs spent -- the three-state edge."""
    use = m["uses"]
    unused = thin = heavy = 0
    out = ["```mermaid", "flowchart LR"]
    for a in DIRS:
        for b in sorted(m["grants"].get(a, ())):
            if b == "extern":
                continue
            incs, nfiles = use.get((a, b), (0, 0))
            if nfiles == 0:
                out.append("  %s -.->|0| %s" % (a, b))
                unused += 1
            elif nfiles <= THIN_MAX_FILES:
                out.append("  %s -->|%d in %d| %s" % (a, incs, nfiles, b))
                thin += 1
            else:
                out.append("  %s ==>|%d in %d| %s" % (a, incs, nfiles, b))
                heavy += 1
    out.append("```")
    out.append("")
    out.append("Edge labels are `includes in files`. **Dotted** is a granted permission spent zero "
               "times -- %d of them, free to revoke. **Solid** is thin: used in %d files or fewer, a "
               "bounded cut. **Thick** is load-bearing. Counts: %d unused, %d thin, %d load-bearing. "
               "Grants of `extern` are omitted -- all are unused, because `extern` is reached through "
               "`core`." % (unused, THIN_MAX_FILES, unused, thin, heavy))
    out.append("")
    out.append("| edge | includes | files | state |")
    out.append("|:-----|---------:|------:|:------|")
    rows = []
    for a in DIRS:
        for b in sorted(m["grants"].get(a, ())):
            if b == "extern":
                continue
            incs, nfiles = use.get((a, b), (0, 0))
            state = ("UNUSED -- revocable" if nfiles == 0
                     else "thin" if nfiles <= THIN_MAX_FILES else "load-bearing")
            rows.append((nfiles, incs, "| `%s → %s` | %d | %d | %s |" % (a, b, incs, nfiles, state)))
    for _f, _i, row in sorted(rows):
        out.append(row)
    return "\n".join(out)


def d_shape(m):
    """6. Shape -- how much of what crosses is actually needed."""
    rows = m["shape"]
    out = ["| type | owner | width | consumer | reads | fit | verdict |",
           "|:-----|:------|------:|:---------|------:|----:|:--------|"]
    for fit, name, hd, w, d, f, used in rows:
        verdict = ("**WHOLESALE**" if fit < SHAPE_WHOLESALE
                   else "partial" if fit < SHAPE_FITTED else "fitted")
        out.append("| `%s` | `%s` | %d | `%s/%s` | %d | %.0f%% | %s |"
                   % (name, hd, w, d, f, used, 100 * fit, verdict))
    bad = [r for r in rows if r[0] < SHAPE_WHOLESALE]
    good = [r for r in rows if r[0] >= SHAPE_FITTED]
    out.append("")
    out.append("Data-dominant structs of %d+ members, declared outside a foundation library, read by a "
               "consumer in a **different tier**. `width` is declared members; `reads` is how many the "
               "consumer names via `.` or `->`. %d crossings measured: **%d wholesale**, %d partial, "
               "%d fitted." % (SHAPE_MIN_MEMBERS, len(rows), len(bad), len(rows) - len(bad) - len(good),
                               len(good)))
    out.append("")
    out.append("The filters carry the meaning. **Classes are excluded**: for a class, a consumer using "
               "two of fifty-nine members is encapsulation working, not a defect — an earlier cut of "
               "this measurement without that filter reported five such \"findings\" and every one was "
               "wrong. Same-tier crossings are excluded because wide sharing inside a tier is the point "
               "of being in one.")
    return "\n".join(out)


def d_cohesion(m):
    """7. Cohesion -- whether a directory can be partitioned at all."""
    coh = m["cohesion"]
    out = ["```mermaid", "xychart-beta",
           '    title "Largest strongly-connected component, as % of the directory"',
           "    x-axis [%s]" % ", ".join(d for d in DIRS if d in coh),
           '    y-axis "percent of translation units" 0 --> 100',
           "    bar [%s]" % ", ".join("%.0f" % (100.0 * coh[d][2] / coh[d][0]) for d in DIRS if d in coh),
           "```", ""]
    out.append("| directory | units | edges | cycle: all edges | share | cycle: headers only | can it be split? |")
    out.append("|:----------|------:|------:|-----------------:|------:|--------------------:|:-----------------|")
    for d in DIRS:
        if d not in coh:
            continue
        units, edges, big, bighdr = coh[d]
        share = big / units if units else 0
        # A 2-unit shell is trivially "one component" and that means nothing. Below the floor the
        # question is not answerable rather than answered badly.
        if units <= COHESION_MIN_UNITS:
            verdict = "n/a — too small"
        elif bighdr > 1:
            verdict = "**type-level entanglement**"
        elif share >= COHESION_BLOB:
            verdict = "**not by moving files** — see below"
        elif share >= COHESION_PARTLY:
            verdict = "partly, as it stands"
        else:
            verdict = "yes, freely"
        out.append("| `%s` | %d | %d | %d | %.0f%% | %d | %s |"
                   % (d, units, edges, big, 100 * share, bighdr, verdict))
    blobs = [d for d in DIRS if d in coh and coh[d][0] > COHESION_MIN_UNITS
             and coh[d][2] / coh[d][0] >= COHESION_BLOB]
    hdr_cyclic = [d for d in DIRS if d in coh and coh[d][3] > 1]
    out.append("")
    out.append("A translation unit is `StarFoo.hpp` + `StarFoo.cpp` as one node. **Two graphs over the "
               "same nodes, answering different questions.** *All edges* asks whether these files could "
               "be moved into sub-directories as they stand — a cycle blocks that, because a grant list "
               "cannot be handed to a directory whose files include across the proposed boundary. "
               "*Headers only* asks whether the **declarations** are hierarchical, which is what decides "
               "whether a decomposition is possible at all.")
    ours = [d for d in DIRS if d in coh and d != "extern"]
    ours_cyclic = [d for d in ours if coh[d][3] > 1]
    out.append("")
    if not ours_cyclic:
        out.append("**The header column is 1 for every directory of our own code.** Not \"low\" — one "
                   "node. `source/game`'s declaration graph is entirely acyclic, and so is "
                   "`windowing`'s. Every cycle in the first column is therefore implementation-side: "
                   "`A.cpp` includes `B.hpp` while `B.cpp` includes `A.hpp`, which is a cycle between "
                   "translation units and no cycle at all between types. Where that matters most: %s."
                   % (", ".join("`%s`" % d for d in blobs) or "none"))
    else:
        out.append("**Header-level cycles — genuine type entanglement — appear in %s.** Everywhere "
                   "else the declaration graph is acyclic and the first column's cycles are "
                   "implementation-side." % ", ".join("`%s`" % d for d in ours_cyclic))
    if "extern" in [d for d in DIRS if d in coh and coh[d][3] > 1]:
        out.append("")
        out.append("`extern` is the one header-cyclic row and it is not ours — vendored C libraries "
                   "with mutually-including headers, which is ordinary for that code and outside the "
                   "scope of anything here.")
    out.append("")
    out.append("That distinction decides the shape of the work. A library boundary is enforced on "
               "**headers** — a `.cpp` reaching across a boundary is what a boundary is *for* — so an "
               "acyclic declaration graph means the strata already exist and nobody has drawn them. "
               "Reading them out and confining each stratum's implementation files is a different and "
               "far more tractable problem than breaking a 226-node cycle, and it can be done one "
               "stratum at a time with each step provable.")
    return "\n".join(out)


def d_sankey(m):
    """3. Weighted coupling."""
    out = ["```mermaid", "sankey-beta", ""]
    for (a, b), (incs, _f) in sorted(m["uses"].items(), key=lambda kv: -kv[1][0]):
        out.append("%s,%s,%d" % (a, b, incs))
    out.append("```")
    out.append("")
    out.append("**Magnitude only -- this is not a flow.** Sankey implies conservation and include "
               "counts do not conserve: `game → core` at %d and `base → core` at %d do not \"arrive "
               "at\" core in any meaningful sense. It is here because it is the only form that shows "
               "the dynamic range the three-state diagram above deliberately flattens."
               % (m["uses"].get(("game", "core"), (0, 0))[0],
                  m["uses"].get(("base", "core"), (0, 0))[0]))
    return "\n".join(out)


def d_shells(m):
    """4. Severability shells."""
    bins = m["binaries"]
    shells = {}
    for name, libs in bins.items():
        shells.setdefault(libs, []).append(name)
    ordered = sorted(shells.items(), key=lambda kv: len(kv[0]))
    out = ["```mermaid", "flowchart TD"]
    prev = None
    for i, (libs, names) in enumerate(ordered):
        label = " + ".join(sorted(libs))
        out.append('  subgraph S%d["shell %d — %s"]' % (i, i, label))
        out.append("    direction LR")
        for n in sorted(names):
            out.append('    %s(["%s"])' % (n, n))
        out.append("  end")
        if prev is not None:
            out.append("  S%d -.->|adds %s| S%d"
                       % (prev, ", ".join(sorted(set(libs) - set(ordered[prev][0]))) or "nothing", i))
        prev = i
    out.append("```")
    out.append("")
    nested = all(set(ordered[i][0]) <= set(ordered[i + 1][0]) for i in range(len(ordered) - 1))
    out.append("%d declared executables fall into **%d distinct link sets**, and %s. Each shell is what "
               "ships without everything below it: the existence of `starbound_server` is the proof "
               "that game↔presentation is a primary boundary, and nothing in this tree proves any "
               "boundary *within* presentation, because those four libraries appear together in "
               "exactly one binary."
               % (len(bins), len(ordered),
                  "they are **strictly nested**" if nested
                  else "they are **not strictly nested** -- see the exceptions above"))
    return "\n".join(out)


def d_mass(m):
    """5. Mass."""
    out = ["```mermaid", "treemap-beta", '"source/"']
    for tier, ds in TIERS:
        present = [(d, m["sizes"][d][1]) for d in ds if m["sizes"].get(d, (0, 0))[1]]
        if not present:
            continue
        out.append('    "%s"' % tier)
        for d, n in sorted(present, key=lambda kv: -kv[1]):
            out.append('        "%s": %d' % (d, n))
    out.append("```")
    out.append("")
    total = sum(m["sizes"][d][1] for d in DIRS)
    game = m["sizes"]["game"][1]
    out.append("`treemap-beta` is a beta diagram type; the table is the drift-proof fallback and "
               "carries the same numbers.")
    out.append("")
    out.append("| tier | directory | files | lines | share |")
    out.append("|:-----|:----------|------:|------:|------:|")
    for tier, ds in TIERS:
        for d in ds:
            f, n = m["sizes"].get(d, (0, 0))
            if n:
                out.append("| %s | `%s` | %d | %s | %.1f%% |"
                           % (tier, d, f, "{:,}".format(n), 100.0 * n / total))
    out.append("")
    out.append("`game` is **%.0f%% of the engine in one directory** -- one grant list, no "
               "sub-`CMakeLists.txt`, and therefore no internal boundary the compiler can enforce."
               % (100.0 * game / total))
    return "\n".join(out)


def d_reach(m):
    """6. God-object reach."""
    vals = [(d, m["reach"][d][1]) for d in DIRS if d != "extern"]
    top = max(v for _d, v in vals) if vals else 0
    step = max(100, ((top // 700) + 1) * 100)
    out = ["```mermaid", "xychart-beta",
           '    title "Root::singleton() reads per directory"',
           "    x-axis [%s]" % ", ".join(d for d, _v in vals),
           '    y-axis "references" 0 --> %d' % (((top // step) + 1) * step),
           "    bar [%s]" % ", ".join(str(v) for _d, v in vals),
           "```", ""]
    zeros = [d for d, v in vals if v == 0]
    out.append("**Read the zeros carefully.** `Root` lives in `source/game`. %s read zero because they "
               "are not granted `game` and therefore *cannot see it* -- that is a consequence of the "
               "grant list, not a property anyone earned. The render campaign's L1 sovereignty is a "
               "different and narrower claim: an *internal* split of `source/application` that no "
               "compiler checks and `layering-lint.py` does." % ", ".join("`%s`" % d for d in zeros))
    out.append("")
    out.append("| directory | files reading Root | references |")
    out.append("|:----------|-------------------:|-----------:|")
    for d, v in sorted(vals, key=lambda kv: -kv[1]):
        out.append("| `%s` | %d | %d |" % (d, m["reach"][d][0], v))
    return "\n".join(out)


def d_crosscut(m):
    """7. Cross-cutting subsystems."""
    cc = m["crosscut"]
    out = ["```mermaid", "flowchart TB"]
    for tier, ds in TIERS:
        present = [d for d in ds if d in m["sizes"] and m["sizes"][d][0]]
        if not present:
            continue
        out.append('  subgraph %s["%s"]' % (tier.split()[0], tier))
        out.append("    direction LR")
        for d in present:
            out.append("    %s" % d)
        out.append("  end")
    elided = 0
    for i, (name, _pat, _why) in enumerate(CROSS_CUTTING):
        per = cc.get(name, {})
        out.append('  X%d{{"%s"}}' % (i, name))
        for d, n in sorted(per.items(), key=lambda kv: -kv[1]):
            if n >= 2:
                out.append("  X%d -.->|%d| %s" % (i, n, d))
            else:
                elided += 1
    out.append("  classDef cc fill:#3a2d5c,stroke:#7b5ea7,color:#e8e0f5")
    out.append("  class %s cc" % ",".join("X%d" % i for i in range(len(CROSS_CUTTING))))
    out.append("```")
    out.append("")
    out.append("Edge labels are files naming the concern. Single-file touches are elided (%d of them) "
               "to keep the picture legible." % elided)
    out.append("")
    out.append("| concern | files | directories | tiers | why it has no home |")
    out.append("|:--------|------:|------------:|------:|:-------------------|")
    for name, _pat, why in CROSS_CUTTING:
        per = cc.get(name, {})
        tiers = {TIER_OF[d] for d in per}
        out.append("| **%s** | %d | %d | %d of %d | %s |"
                   % (name, sum(per.values()), len(per), len(tiers), len(TIERS), why))
    return "\n".join(out)


def d_taxonomy(m):
    """8. The six top-level system parts."""
    parts = m["parts"]
    out = ["```mermaid", "mindmap", "  root((OpenStarbound))"]
    for name, where, _why in TOP_LEVEL:
        out.append("    %s" % name)
        out.append("      %s" % where)
        for leaf in parts.get(name, []):
            out.append("      %s" % leaf)
    out.append("```")
    out.append("")
    out.append("A mindmap because this genuinely is a tree: six independent children of one root with "
               "no cross-links. Five of the six are invisible to any tool that only reads `source/`.")
    return "\n".join(out)


def d_presentation(m):
    """12. The presentation tier's three duties, and where they blur."""
    _t4, external, gameaware = m["presentation"]
    use = m["uses"]
    sizes_ = m["sizes"]
    out = ["| directory | files | lines | duty | names `game` |",
           "|:----------|------:|------:|:-----|-------------:|"]
    for d, why in PRESENTATION_DUTY:
        f, n = sizes_.get(d, (0, 0))
        incs, gf = use.get((d, "game"), (0, 0))
        out.append("| `%s` | %d | %s | %s | %d includes in %d files |"
                   % (d, f, "{:,}".format(n), why, incs, gf))
    out.append("")
    out.append("**Two draw paths, not one.** `windowing/StarGuiContext.hpp` holds its own `RendererPtr` "
               "alongside a `TextPainterPtr`, `DrawablePainterPtr` and `AssetTextureGroupPtr`. So the "
               "frame reaches the GPU twice over: the world through `WorldPainter` and the L3 passes, "
               "and the interface through `GuiContext` and the painters. That single file is the entire "
               "`windowing → rendering` edge.")
    out.append("")
    shared = sorted((h, ds) for h, ds in external.items())
    shells = {"client", "server"}
    out.append("Which `source/rendering` headers are consumed from **outside** `source/rendering`:")
    out.append("")
    out.append("| header | consumed by | what that means |")
    out.append("|:-------|:------------|:----------------|")
    ui = 0
    for h, ds in shared:
        if "windowing" in ds:
            verdict = "**UI draw path** — shared infrastructure, not render-internal"
            ui += 1
        elif ds <= shells:
            verdict = "composition root wiring it up, not a second consumer"
        else:
            verdict = "`frontend` only — mixed; see the note below"
        out.append("| `%s` | %s | %s |" % (h, ", ".join("`%s`" % x for x in sorted(ds)), verdict))
    out.append("")
    out.append("This is the standing answer to a question the render decomposition never resolved. "
               "**%d of these serve the UI path as well as the world path**, so they are not "
               "render-subsystem-internal and decomposing them into L3 would have broken the interface. "
               "The painters were never leftover work; they are a shared service that happens to live in "
               "`source/rendering`." % ui)
    out.append("")
    out.append("The rows are deliberately not given one verdict, because they are not one thing. "
               "`client` **owns** a `WorldPainterPtr` — that is the composition root, and expected. "
               "`frontend` mostly passes that pointer through (`MainMixer::setWorldPainter`, "
               "`WirePane`'s constructor) rather than reaching into render internals. But "
               "`TitleScreen.cpp` constructs an `EnvironmentPainter` outright, which is a genuine second "
               "consumer. Include-level measurement cannot separate *passes a handle* from *draws with "
               "it*; those three cases are named here rather than flattened into a verdict this "
               "instrument has not earned.")
    out.append("")
    out.append("Where the `windowing`/`frontend` duty line blurs — toolkit files naming a simulation "
               "**noun**. Ambient services (`Root`, `GameTypes`, `ImageMetadataDatabase` and friends) are "
               "excluded: `Root` alone appears in seventeen of these files and would bury the signal "
               "under the god-object §10 already measures.")
    out.append("")
    out.append("| file in `source/windowing` | game types it names |")
    out.append("|:---------------------------|:--------------------|")
    for f, hits in gameaware:
        out.append("| `%s` | %s |" % (f, ", ".join("`%s`" % h.replace("Star", "").replace(".hpp", "")
                                                   for h in hits)))
    out.append("")
    out.append("**A widget that knows what an `Item` is is not a toolkit widget** — it is a frontend "
               "widget in the wrong directory. These %d files are where the `windowing → game` edge that "
               "§5 marks load-bearing actually comes from." % len(gameaware))
    out.append("")
    out.append("**Before acting on that, read §9.** `windowing` is one strongly-connected component, so "
               "these files may be entangled with the toolkit rather than cleanly liftable. Extraction "
               "is a different operation from partition and may well be possible — but assuming so "
               "without measuring is exactly the mistake §9 exists to stop repeating.")
    return "\n".join(out)


def d_renderclasses(m):
    """9. Render layers and the one inheritance edge that crosses a library."""
    classes, inherits, _rep, _layer_of, _homes, layer_dirs = m["render"]
    edges, over = m["classedges"]
    ns = re.compile(r"[^A-Za-z0-9]+")

    def label(layer):
        """Namespace label carries the OWNING LIBRARY, because the layer name alone does not say where
        the code is and a reader coming from the mass treemap will assume `rendering`. L1 is not there."""
        dirs = "_".join("star_" + d for d in sorted(layer_dirs.get(layer, ())))
        return ns.sub("_", "%s in %s" % (layer, dirs)).strip("_")


    # Only classes that participate in a drawn edge are placed. L1 alone declares thirty-one types, most
    # of them vertex PODs and ring buffers with no relationship to show, and drawing them turns the one
    # informative picture in this document into a wall. What is omitted is counted and listed below the
    # diagram rather than dropped silently.
    inherits = sorted(set(inherits))
    involved = ({c for c, _p, _l, _h in inherits} | {p for _c, p, _l, _h in inherits}
                | {a for a, _b in edges} | {b for _a, b in edges})
    omitted = []
    placed = set()
    out = ["```mermaid", "classDiagram", "  direction LR"]
    for name, _purpose, _files in RENDER_LAYERS:
        members = sorted(classes.get(name, ()))
        shown = [c for c in members if c in involved]
        omitted += [(name, c) for c in members if c not in involved]
        if not shown:
            continue
        out.append("  namespace %s {" % label(name))
        for c in shown:
            out.append("    class %s" % c)
            placed.add(c)
        out.append("  }")

    # Bases declared OUTSIDE the render tree, grouped by the library that owns them. This grouping is
    # the finding: anything appearing under star_game is a render class inheriting a simulation class.
    foreign = {}
    for _c, parent, _l, home in inherits:
        if parent not in placed:
            foreign.setdefault(home, set()).add(parent)
    for home in sorted(foreign):
        out.append("  namespace star_%s {" % home)
        for p in sorted(foreign[home]):
            out.append("    class %s" % p)
        out.append("  }")

    # EDGE-WRITING DIRECTION IS A LAYOUT LEVER, and the only one Mermaid's classDiagram offers.
    # `A <|-- B` and `B --|> A` are semantically identical -- both say B extends A -- but the parser
    # records the first-named node first, and dagre ranks by that: under `direction LR` the first name
    # goes left. So an edge written base-first puts the base on the LEFT.
    #
    # Internal edges stay base-first, which is the natural reading and keeps the L1 cluster laid out as
    # it was. The edges that LEAVE the subsystem are written derived-first, so the foreign namespace
    # lands to the RIGHT of the layer it escapes from -- the picture then reads as "out of the
    # subsystem" rather than "in from the side", which is what the section is about. One edge today.
    crossing_now = {(c, p) for c, p, _l, h in inherits
                    if h not in ("application", "rendering", "external") and h not in FOUNDATION}
    for child, parent, _layer, _home in inherits:
        if (child, parent) in crossing_now:
            out.append("  %s --|> %s" % (child, parent))
        else:
            out.append("  %s <|-- %s" % (parent, child))
    for a, b in edges:
        out.append("  %s ..> %s" % (a, b))
    out.append("```")
    out.append("")
    out.append("`classDiagram` earns its place here and nowhere else in this document, because "
               "inheritance is the actual relationship rather than a metaphor for one. `<|--` is "
               "inheritance; `..>` is a compile-time dependency between render layers, drawn between "
               "each file's representative class.")
    if omitted:
        by_layer = {}
        for layer, c in omitted:
            by_layer.setdefault(layer, []).append(c)
        out.append("")
        out.append("**%d declared types are not drawn** because they participate in no edge -- vertex "
                   "PODs, parameter structs and ring buffers. They are listed rather than dropped: %s."
                   % (len(omitted), "; ".join("%s: %s" % (l, ", ".join("`%s`" % c for c in sorted(cs)))
                                              for l, cs in sorted(by_layer.items()))))
    if over:
        out.append("")
        out.append("**%d cross-layer dependency edges are not drawn** (cap %d). The cap is stated "
                   "rather than applied silently." % (over, MAX_CLASS_EDGES))
    out.append("")
    crossing = [(c, p, l, h) for c, p, l, h in inherits
                if h not in ("application", "rendering", "external") and h not in FOUNDATION]
    foundation = [(c, p, l, h) for c, p, l, h in inherits if h in FOUNDATION]
    out.append("Every inheritance edge in the render tree, and where the base class lives:")
    out.append("")
    out.append("| child | inherits | child's layer | base declared in | verdict |")
    out.append("|:------|:---------|:--------------|:-----------------|:--------|")
    for row in inherits:
        child, parent, layer, home = row
        if row in crossing:
            verdict = "**leaves the render subsystem for the simulation**"
        elif row in foundation:
            verdict = "foundation base -- intended use"
        else:
            verdict = "internal"
        out.append("| `%s` | `%s` | %s | `star_%s` | %s |" % (child, parent, layer, home, verdict))
    out.append("")
    if crossing:
        out.append("Of %d inheritance edges, %d stay inside the render libraries, %d take a base from "
                   "a foundation library (`RefCounter` and friends -- that is what foundations are "
                   "for), and **%d leaves the subsystem entirely**: %s. That last one is why "
                   "`WorldPass` cannot finish its input DTO and why the client has no headless "
                   "expression -- a render class whose base is a simulation class cannot be "
                   "compiled without the simulation."
                   % (len(inherits), len(inherits) - len(crossing) - len(foundation), len(foundation),
                      len(crossing),
                      ", ".join("`%s : %s` (`star_%s`)" % (c, p, h) for c, p, _l, h in crossing)))
    else:
        out.append("**No inheritance edge leaves the render libraries.**")
    return "\n".join(out)


DIAGRAMS = [
    ("tree", d_tree),
    ("lattice", d_lattice),
    ("grantuse", d_grantuse),
    ("shape", d_shape),
    ("cohesion", d_cohesion),
    ("sankey", d_sankey),
    ("shells", d_shells),
    ("mass", d_mass),
    ("reach", d_reach),
    ("crosscut", d_crosscut),
    ("presentation", d_presentation),
    ("taxonomy", d_taxonomy),
    ("renderclasses", d_renderclasses),
]


# ---------------------------------------------------------------------------------------------------
# DRIVER
# ---------------------------------------------------------------------------------------------------

def measure():
    owner, dupes = owner_map()
    render = render_classes()
    return {
        "grants": grants(), "uses": uses(), "sizes": sizes(), "reach": reach(),
        "binaries": binaries(), "crosscut": crosscut(), "parts": system_parts(),
        "render": render, "classedges": cross_layer_edges(render[2], render[3]),
        "shape": shape(), "cohesion": cohesion(), "presentation": presentation(),
        "owner": owner, "dupes": dupes,
    }


def blocks(m):
    return [(key, fn(m)) for key, fn in DIAGRAMS]


def inject(arg, m, check_only):
    path = pathlib.Path(arg)
    path = path if path.is_absolute() else REPO / path
    text = read(path)
    if not text:
        print("arch-graph: cannot read %s" % arg)
        return 2
    new = text
    for key, block in blocks(m):
        begin, end = MARK_BEGIN % key, MARK_END % key
        if begin not in new or end not in new:
            print("arch-graph: %s has no marker pair for '%s'" % (arg, key))
            return 2
        head, rest = new.split(begin, 1)
        _old, tail = rest.split(end, 1)
        new = "%s%s\n%s\n%s%s" % (head, begin, block, end, tail)
    if check_only:
        if new != text:
            stale = [k for k, b in blocks(m) if b not in text]
            print("%s: generated blocks are STALE (%s) -- rerun "
                  "`scripts/arch-graph.py --inject %s`" % (arg, ", ".join(stale) or "whitespace", arg))
            return 1
        print("%s: all %d generated blocks match the tree" % (arg, len(DIAGRAMS)))
        return 0
    path.write_text(new, encoding="utf-8", newline="\n")
    print("%s: %d generated blocks updated" % (arg, len(DIAGRAMS)))
    return 0


def main(argv):
    m = measure()
    if m["dupes"]:
        print("arch-graph: AMBIGUOUS HEADER BASENAMES -- include attribution is unreliable:")
        for f, ds in sorted(m["dupes"].items()):
            print("  %s in %s" % (f, ", ".join(ds)))
        return 2
    bad = assert_tiers_match_grants(m["grants"])
    if bad:
        print("arch-graph: TIER TABLE CONTRADICTS THE GRANTS -- fix TIERS, do not draw this:")
        for b in bad:
            print("  %s" % b)
        return 2
    if argv and argv[0] == "--facts":
        print(json.dumps({
            "grants": {k: sorted(v) for k, v in m["grants"].items()},
            "uses": {"%s->%s" % k: {"includes": v[0], "files": v[1]} for k, v in m["uses"].items()},
            "sizes": {k: {"files": v[0], "lines": v[1]} for k, v in m["sizes"].items()},
            "reach": {k: {"files": v[0], "refs": v[1]} for k, v in m["reach"].items()},
            "binaries": {k: sorted(v) for k, v in m["binaries"].items()},
            "crosscut": m["crosscut"], "parts": m["parts"],
        }, indent=2, sort_keys=True))
        return 0
    if argv and argv[0] in ("--inject", "--check"):
        if len(argv) < 2:
            print("arch-graph: %s needs a file" % argv[0])
            return 2
        return inject(argv[1], m, argv[0] == "--check")
    for key, block in blocks(m):
        print("### %s\n" % key)
        print(block)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
