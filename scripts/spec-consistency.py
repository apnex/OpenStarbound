#!/usr/bin/env python3
"""Reconcile the target-state architecture's two projections against each other and against its registers.

WHY THIS EXISTS. The design is described twice on purpose: Section 4 is the COMPILE projection (who may
name whom, enforced by INCLUDE_DIRECTORIES) and Section 5 is the RUNTIME projection (what executes, on
which thread, in what order). Two descriptions of one model drift, and every check added here was added
because it had already found a real defect by hand:

  1. the diagram drew `gpu --> base` against a grant row giving `gpu` only `core`
  2. the register tally read "Twenty components ... two ENTRYPOINTs" long after it was 21 and three
  3. `headlessLoop` was in the element register and absent from the runtime diagram -- the composition
     the whole design exists for was missing from its own runtime picture
  4. `audioTick` sat in a subgraph labelled "SDL" against a register saying "audio"
  5. frameLoop -> presentTick and headlessLoop -> presentTick were runtime edges that NO compile-time
     grant permitted: the host could not trigger the paint

Number 5 is the one that matters most, and it is the reason this is a gate and not a linter. It is the
GRAFT RULE, and it is what makes the two projections one design:

    every runtime edge must be legal in the compile projection --
    same component, a direct grant, or a shared CONTRACT to dispatch through

VERDICTS. Any of these fails the gate.

  UNREGISTERED  a drawn node with no row in the register it belongs to
  DRIFT         a field disagrees between a diagram and its register
  MISSING       a registered row absent from the diagram that must show it
  TALLY         the prose count disagrees with the register it counts
  UNGRANTED     a compile-projection edge the grant table does not permit
  GRAFT         a runtime edge with no legal compile-time route
  ORDER         a source with several outgoing edges does not sequence them, or sequences them badly
  VACUOUS       the parse found implausibly little -- a parser regression, not a clean document

VACUOUS deserves its own name. A gate whose parser silently stops matching reports success, and this
project has already been bitten by exactly that: an oracle that grepped for the wrong word scored a
screaming failure as PASS, and a loop inventory that globbed the wrong filename could not see the file
holding the server's loop. Finding nothing is not the same as finding nothing wrong.
"""
import argparse
import collections
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

def _spec_model():
    """Import the ONE table reader. Hyphenated filenames cannot be imported normally; restating its
    regexes here instead is the defect it exists to delete -- three parsers once gave three different
    counts of one table."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("spec_model", str(REPO / "scripts" / "spec-model.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MODEL = _spec_model()
SPEC = MODEL.SPEC          # one declaration of where the document lives

KINDS = ("FOUNDATION", "CONTRACT", "BACKEND", "LIBRARY", "ENTRYPOINT")
# The prose tally lists kinds in its own order, which is not KINDS'. Kept separate rather than
# reordered: KINDS is the taxonomy, this is one sentence's phrasing, and conflating them made the
# gate's first run report a drift that did not exist.
TALLY_ORDER = ("CONTRACT", "BACKEND", "LIBRARY", "FOUNDATION", "ENTRYPOINT")
ZONES = ("MACHINE", "DOMAIN", "DEVICE", "COMPOSITION")
ZONE_ORDER = {z: i for i, z in enumerate(ZONES)}
CADENCES = ("DISPLAY", "FIXED", "FREE", "EXTERNAL", "DERIVED", "ONCE", "EVENT")
# CARDINALITY -- how many instances exist at once. Added 2026-08-01 after the Director asked whether
# N worlds are N instances of the one `world` component. They are, and nothing in the taxonomy said
# so: "one per resident world" lived in a note, as prose, unchecked. Cardinality is what makes a
# thing distributable -- you can place the many, not the one -- so for D1's distributed purpose it is
# not decoration, it is the axis that identifies the unit of placement.
CARDINALITIES = ("PROCESS", "PARTICIPANT", "UNIVERSE", "WORLD", "DEVICE")
# Runtime element kinds. WIRING and SIGNAL were added once the Application contract was measured:
# of its ten virtuals only three are ticks, and six fit neither LOOP nor TICK.
EKINDS = "LOOP|TICK|WIRING|SIGNAL"

# Below these the parse is not believable. They are floors, not targets: raise them only when the
# design genuinely grows, and never lower them to make a red gate green.
FLOOR = {"components": 15, "elements": 13, "compile_edges": 20, "runtime_edges": 13, "grants": 15}

WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8,
         "nine": 9, "ten": 10, "Nineteen": 19, "Twenty": 20, "Twenty-one": 21, "Twenty-two": 22,
         "Twenty-three": 23, "Twenty-four": 24, "Twenty-five": 25,
         "Twenty-six": 26, "Twenty-seven": 27, "Twenty-eight": 28,
         "Twenty-nine": 29, "Thirty": 30, "Thirty-one": 31, "Thirty-two": 32,
         "Thirty-three": 33, "Thirty-four": 34, "Thirty-five": 35, "Thirty-six": 36,
         "Thirty-seven": 37, "Thirty-eight": 38, "Thirty-nine": 39, "Forty": 40, "Forty-one": 41, "Forty-two": 42,
         "Forty-three": 43, "Forty-four": 44, "Forty-five": 45,
         "thirteen": 13, "fourteen": 14,
         "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12, "thirteen": 13}

COMPONENT_ROW = re.compile(
    r'\|\s*\*\*`(\w+)`\*\*\s*\|\s*(' + "|".join(KINDS) + r')\s*\|\s*(' + "|".join(ZONES) +
    r')\s*\|\s*([^|]+?)\s*\|')
ELEMENT_ROW = re.compile(
    r'\|\s*\*\*`(\w+)`\*\*\s*\|\s*(' + EKINDS + r')\s*\|\s*(\w+)\s*\|\s*\*\*(\w+)\*\*\s*\|\s*`(\w+)`\s*\|\s*`(\w+)`\s*\|')
GRANT_ROW = re.compile(r'^\|\s*`(\w+)`\s*\|\s*([^|]+?)\s*\|', re.M)
TALLY = re.compile(r'([\w-]+) components: (\w+) CONTRACTs, (\w+) BACKENDs, (\w+) LIBRARYs, '
                   r'(\w+) FOUNDATIONs, (\w+)\s*\n?ENTRYPOINTs')

# compile projection
C_NODE = re.compile(r'^\s*(\w+)\["<b>(\w+)</b><br/>(\w+)<br/><i>([^<]+)</i>"\]', re.M)
C_BOX = re.compile(r'^\s*subgraph (\w+) \["<b>(\w+)</b> · (\w+)"\]', re.M)
# `--o` is the THIRD compile arrow: A implements a ROLE that B declares and is driven through it, as
# distinct from `==>`, which is A implementing the contract that says what A *is*. Both are genuine
# derivations, and both were once drawn `==>` -- which made `rendering` and `transcript` read as
# two-job components under the Law of One. Order matters in the alternation only in that `-->` and
# `--o` share a prefix; they differ at the last character, so either order parses. See IMPLEMENTS_ARITY.
C_EDGE = re.compile(r'^\s*(\w+)\s*(-->|==>|--o)\s*(\w+)\s*$', re.M)
# runtime projection
R_NODE = re.compile(r'^\s*(\w+)\["<b>(\w+)</b> · (' + EKINDS + r')<br/><i>(\w+)</i>"\]', re.M)
R_THREAD = re.compile(r'^\s*subgraph (t\w+) \["([^"]+)"\]', re.M)
# The |label| is OPTIONAL AS A UNIT. An earlier form made the pipes independently optional, so on an
# unlabelled edge the greedy label group swallowed the destination -- `frameloop --> swaptick` parsed
# as destination "k" -- and the edge was silently dropped from every check, the graft rule included.
# Seven of eighteen runtime edges were invisible, while the gate reported all of them legal.
R_EDGE = re.compile(r'^\s*(\w+)\s*(-->|==>|-\.->)\s*(?:\|([^|\n]*)\|)?\s*(\w+)\s*$', re.M)
# A flowchart edge set carries no sequence, and the frame's sequence is load-bearing -- the source
# says the host "does not own this ordering" about the one phase that has no home. So an edge leaving
# a source with siblings must be prefixed `N:` (contiguous from 1, ties legal) or `*:` (deliberately
# unordered). Without this the notation would be decoration.
SEQ = re.compile(r'^\s*(\d+|\*)\s*:')

# Elements are the graft point, but a runtime diagram may also name a non-element endpoint that the
# compile register owns -- the GPU backend's draw calls being the one that exists. Declared, not guessed.
EXTRA_ENDPOINTS = {"device": "gpu_opengl"}

# ---------------------------------------------------------------------------------------------
# RUNTIME COVERAGE. Section 4 grew from 21 components to 40 while Section 5 stayed at 15 elements,
# and nothing noticed: this gate checked that every DECLARED element appears in the diagram, never
# that a component which ought to own one does. The compile projection ran ahead of the runtime one
# for a whole design session, and the omission was `worldLoop` -- one clock per resident world, the
# single element this design exists to run without a participant, absent from its own picture.
#
# Two rules, both already stated in the document before they were checked:
#
#   1. "an ENTRYPOINT gets a single WIRING element" -- Section 5's taxonomy. Four of seven did not.
#   2. A LIBRARY or BACKEND either owns a runtime element or is declared element-free WITH A REASON.
#      Silence is not a claim; a component nobody drives should say so out loud.
#
# CONTRACTs and FOUNDATIONs are excluded structurally, not by listing: a contract is an interface
# and has no body to run, and both foundations are pure vocabulary and containers.
# ---------------------------------------------------------------------------------------------
ELEMENT_FREE = {
    "game": "the domain is called from inside other components' ticks; it owns no clock",
    "worldgen": "generation is invoked per region by `world`; it is a service, not a schedule",
    "world_view": "the replica steps inside `fixedTick`'s call tree, not on a clock of its own",
    "universe_view": "same: driven by the participant's tick, never self-scheduled",
    "interaction": "verbs are called by input and by scripts; a verb has no cadence",
    "script": "the Lua host runs inside whatever tick calls into it -- deliberately no clock",
    "storage": "reads and writes when asked; a store that ticked on its own would be deciding "
               "WHEN to save, which is the caller's business",
    "windowing": "widgets emit into the frame when asked; the toolkit drives nothing",
    "frontend": "screens are updated by the participant's tick",
    "gpu_opengl": "executes device calls issued by `presentTick`",
    "gpu_sdl": "same, other backend",
    "audio_sdl": "opens the device and PULLS `audioTick`; the clock is SDL's, not ours",
    "platform_pc": "vendor services answer when called",
    "host_sdl_extra": "placeholder guard -- never matches a real component",
}


def check_zone_order(comp, grants):
    """Every grant edge must point DOWN the zone order: COMPOSITION -> DEVICE -> DOMAIN -> MACHINE.

    This is the check that turns zones from labels into directories. The four-zone grouping was chosen
    BECAUSE it produced a perfectly layered DAG -- zero upward edges across 41 components -- and a
    layering nobody enforces reverts to a suggestion within a release.

    It also earns its keep immediately: the first four-zone assignment had exactly one upward edge,
    `storage` (MACHINE) -> `script` (DOMAIN). That was not a placement error to paper over, it was a
    real finding. `script` grants only core, base and content and names no domain type at all -- it is
    the Lua interpreter host, infrastructure like the allocator. It had been filed in DOMAIN by
    association with `game/scripting/`, which is where the files sit TODAY. D7 violation, caught by
    the layering rather than by reading."""
    out = []
    for a in sorted(grants):
        if a not in comp:
            continue
        for b in sorted(grants[a]):
            if b not in comp:
                continue
            if ZONE_ORDER[comp[b]["zone"]] > ZONE_ORDER[comp[a]["zone"]]:
                out.append(("ZONE_ORDER",
                            "`%s` (%s) grants `%s` (%s) -- that points UP the zone order, so the "
                            "directory layering would be circular"
                            % (a, comp[a]["zone"], b, comp[b]["zone"])))
    return out


def check_granted(comp, grants):
    """Every component except a FOUNDATION must have a grant row.

    grant-sweep iterates the GRANT TABLE, so a component with no row there is not reported as
    ungranted -- it is not reported at all. `gpu_sdl` had been in the register as a declared BACKEND
    with no grant row since it was added, and no instrument said a word: the sweep saw 39 components
    it could talk about and one it could not see. Absence from a table is the quietest defect there
    is, and this document has now been bitten by it three times.

    FOUNDATIONs are exempt by construction: `core` and `base` are the bottom, and a row saying they
    name nothing would be noise."""
    out = []
    for c, v in sorted(comp.items()):
        if v["kind"] == "FOUNDATION":
            continue
        if c not in grants:
            out.append(("UNGRANTED_ROW",
                        "`%s` is a registered %s with no grant row; grant-sweep cannot see it at all"
                        % (c, v["kind"])))
    return out


def check_cardinality(elem):
    """Every element declares a legal cardinality. An undeclared instance count is how `worldLoop`
    came to be 'one per resident world' in a note nobody could check."""
    out = []
    for name, e in sorted(elem.items()):
        if e.get("cardinality") not in CARDINALITIES:
            out.append(("CARDINALITY",
                        "`%s` declares cardinality %r, which is not one of %s"
                        % (name, e.get("cardinality"), ", ".join(CARDINALITIES))))
    return out


def check_runtime_coverage(comp, elem):
    """Every ENTRYPOINT owns exactly one WIRING; every LIBRARY/BACKEND owns an element or declares why not."""
    out = []
    owned = {}
    for name, e in elem.items():
        owned.setdefault(e["owner"], []).append((name, e["kind"]))
    for c, v in sorted(comp.items()):
        kind = v["kind"]
        mine = owned.get(c, [])
        if kind == "ENTRYPOINT":
            wirings = [n for n, k in mine if k == "WIRING"]
            if len(wirings) != 1:
                out.append(("UNWIRED",
                            "ENTRYPOINT `%s` owns %d WIRING elements; the taxonomy says exactly one"
                            % (c, len(wirings))))
        elif kind in ("LIBRARY", "BACKEND"):
            if not mine and c not in ELEMENT_FREE:
                out.append(("UNDRIVEN",
                            "`%s` is a %s with no runtime element and no ELEMENT_FREE reason -- "
                            "say what drives it, or declare that nothing does" % (c, kind)))
    return out


def projections(text):
    """-> {'compile': body, 'runtime': body}. A missing or duplicated marker is a hard failure: the
    gate must never fall back to guessing which diagram it is reading."""
    found = {}
    for body in re.findall(r'```mermaid\n(.*?)```', text, re.S):
        m = re.match(r'\s*%%\s*projection:\s*(compile|runtime)\s*$', body.splitlines()[0])
        if not m:
            continue
        name = m.group(1)
        if name in found:
            raise SystemExit("spec-consistency: two '%% projection: {}' diagrams; expected one".format(name))
        found[name] = body
    for want in ("compile", "runtime"):
        if want not in found:
            raise SystemExit("spec-consistency: no diagram marked '%% projection: {}'".format(want))
    return found


def parse(text):
    # Delegates: see MODEL. Three tools once parsed the grant table three ways and reported 44, 62
    # and 62 rows for it. There is one parser now, so there is one count.
    return MODEL.components(text), MODEL.elements(text), MODEL.grants(text)


def check(text):
    findings = []
    comp, elem, grants = parse(text)
    proj = projections(text)
    dep, exe = proj["compile"], proj["runtime"]

    # ---- compile projection -------------------------------------------------------------------
    plain = C_NODE.findall(dep)
    boxes = C_BOX.findall(dep)
    ids = {i: n for i, n, _k, _d in plain}
    ids.update({i: n for i, n, _k in boxes})
    for _i, n, k, duty in plain:
        if n not in comp:
            findings.append(("UNREGISTERED", "component `%s` is drawn but has no register row" % n))
        elif comp[n]["kind"] != k:
            findings.append(("DRIFT", "`%s` drawn as %s, register says %s" % (n, k, comp[n]["kind"])))
        elif comp[n]["duty"] != duty.strip():
            findings.append(("DRIFT", "`%s` duty differs between diagram and register" % n))
    for _i, n, k in boxes:
        if n not in comp:
            findings.append(("UNREGISTERED", "container `%s` is drawn but has no register row" % n))
        elif comp[n]["kind"] != k:
            findings.append(("DRIFT", "`%s` drawn as %s, register says %s" % (n, k, comp[n]["kind"])))
        elif not comp[n]["duty"]:
            findings.append(("MISSING", "container `%s` drops its duty line and the register has none"
                             % n))
    for n in sorted(set(comp) - set(ids.values())):
        findings.append(("MISSING", "component `%s` is registered but absent from the compile diagram"
                         % n))
    compile_edges = C_EDGE.findall(dep)
    for a, _arrow, b in compile_edges:
        # UNDECLARED, CHECKED FIRST. The grant test below opens with `a in ids and b in ids`, which
        # SILENTLY SKIPS any edge naming an id the diagram never declared. Mermaid does not skip it --
        # it invents a bare, unstyled, empty box with that name and floats it beside the map.
        #
        # That is how `csg --> client` lived here. Every other entrypoint writes `--> shell`, the id of
        # the client subgraph; `client` was never a node id. So `client_sdl_gpu` had NO edge to the
        # client component at all, and an orphan box appeared instead. The Director found it by looking
        # at the picture, which is the one method this gate exists to make unnecessary.
        #
        # It survived because `client` IS a registered component NAME -- ids and names are different
        # namespaces and this check conflated them. Same shape as the R_EDGE label defect: a silent
        # skip on the unresolvable, which reads as agreement.
        for end in (a, b):
            if end not in ids:
                findings.append(("UNDECLARED",
                                 "edge `%s --> %s` names `%s`, which no node or subgraph declares; "
                                 "mermaid draws that as an orphan box" % (a, b, end)))
        if a in ids and b in ids and ids[a] in grants and ids[b] not in grants[ids[a]]:
            findings.append(("UNGRANTED", "`%s --> %s` is drawn, but %s's grant list omits it"
                             % (ids[a], ids[b], ids[a])))

    # IMPLEMENTS_ARITY -- the Law of One, counted at last.
    #
    # Section 4 states the rule as "a backend with two `==>` edges is doing two jobs" and calls it
    # checkable. Nothing checked it. `rendering` and `transcript` each carried two -- `presentation`
    # and `host` -- through every green run of every gate, because no instrument counted implements
    # edges at all. Not a parser blind spot: `ids` above merges C_NODE and C_BOX, so subgraph-drawn
    # components like `rendering` were always visible. The verdict simply did not exist.
    #
    # Resolved 2026-08-02 by splitting the arrow rather than the component: `==>` is identity, `--o`
    # is a role you are driven through, and only `==>` counts here. Two clauses, because a backend
    # that implements exactly one thing which is NOT a contract is just as wrong as one implementing
    # two contracts.
    implements = {}
    for a, arrow, b in compile_edges:
        if arrow == "==>" and a in ids and b in ids:
            implements.setdefault(ids[a], []).append(ids[b])
    for name, v in sorted(comp.items()):
        got = sorted(implements.get(name, []))
        if v["kind"] == "BACKEND" and len(got) != 1:
            findings.append(("IMPLEMENTS_ARITY",
                             "BACKEND `%s` has %d `==>` edge(s) (%s); the Law of One says exactly "
                             "one. A role you are driven through is `--o`, not `==>`"
                             % (name, len(got), ", ".join(got) or "none")))
        for target in got:
            if comp.get(target, {}).get("kind") != "CONTRACT":
                findings.append(("IMPLEMENTS_ARITY",
                                 "`%s ==> %s` implements a %s; `==>` may only point at a CONTRACT"
                                 % (name, target, comp.get(target, {}).get("kind", "?"))))

    # ---- runtime projection -------------------------------------------------------------------
    rnodes = R_NODE.findall(exe)
    rids = {i: n for i, n, _k, _o in rnodes}
    for _i, n, k, owner in rnodes:
        e = elem.get(n)
        if not e:
            findings.append(("UNREGISTERED", "element `%s` is drawn but has no register row" % n))
            continue
        if e["kind"] != k:
            findings.append(("DRIFT", "`%s` drawn as %s, register says %s" % (n, k, e["kind"])))
        if e["owner"] != owner:
            findings.append(("DRIFT", "`%s` drawn owned by %s, register says %s"
                             % (n, owner, e["owner"])))
    for n, e in sorted(elem.items()):
        if e["cadence"] not in CADENCES:
            findings.append(("DRIFT", "`%s` cadence %s is not in the taxonomy" % (n, e["cadence"])))
        if e["owner"] not in comp:
            findings.append(("UNREGISTERED", "`%s` is owned by %s, which is not a component"
                             % (n, e["owner"])))
    for n in sorted(set(elem) - set(rids.values())):
        findings.append(("MISSING", "element `%s` is registered but absent from the runtime diagram"
                         % n))
    # the register's thread column must match the subgraph that actually encloses the node
    for sid, label in R_THREAD.findall(exe):
        start = exe.index("subgraph %s [" % sid)
        seg = exe[start:]
        seg = seg[:seg.index("\n    end")] if "\n    end" in seg else seg
        thread = label.split()[0]
        for _i, n, _k, _o in re.findall(
                r'(\w+)\["<b>(\w+)</b> · (' + EKINDS + r')<br/><i>(\w+)</i>"\]', seg):
            if elem.get(n, {}).get("thread") != thread:
                findings.append(("DRIFT", "`%s` is drawn in thread '%s', register says '%s'"
                                 % (n, thread, elem.get(n, {}).get("thread"))))

    # ---- THE GRAFT RULE -----------------------------------------------------------------------
    owner = {n: e["owner"] for n, e in elem.items()}
    owner.update(EXTRA_ENDPOINTS)
    endpoints = dict(rids)
    endpoints.update({k: k for k in EXTRA_ENDPOINTS})
    runtime_edges = []
    for a, _arrow, _label, b in R_EDGE.findall(exe):
        if a not in endpoints or b not in endpoints:
            continue
        oa, ob = owner.get(endpoints[a]), owner.get(endpoints[b])
        if oa is None or ob is None:
            continue
        runtime_edges.append((endpoints[a], endpoints[b]))
        if oa == ob or ob in grants.get(oa, set()):
            continue
        shared = [c for c in sorted(grants.get(oa, set()) & grants.get(ob, set()))
                  if comp.get(c, {}).get("kind") == "CONTRACT"]
        if not shared:
            findings.append(("GRAFT", "`%s -> %s` runs at run time, but %s cannot reach %s: no grant "
                                      "and no shared contract" % (endpoints[a], endpoints[b], oa, ob)))

    # ---- ORDER: sequencing of sibling runtime edges ------------------------------------------
    out_edges = collections.defaultdict(list)
    for a, _arrow, label, b in R_EDGE.findall(exe):
        if a in endpoints and b in endpoints:
            out_edges[a].append((endpoints[b], label or ""))
    for src, outs in sorted(out_edges.items()):
        if len(outs) < 2:
            continue
        prefixes = []
        for dst, label in outs:
            m = SEQ.match(label)
            if not m:
                findings.append(("ORDER", "`%s -> %s` leaves a source with %d edges and carries no "
                                          "sequence prefix" % (endpoints[src], dst, len(outs))))
            else:
                prefixes.append(m.group(1))
        if len(prefixes) != len(outs):
            continue
        stars = [p for p in prefixes if p == "*"]
        if stars and len(stars) != len(prefixes):
            findings.append(("ORDER", "`%s` mixes `*:` with numbered edges; a source is either "
                                      "ordered or it is not" % endpoints[src]))
        elif not stars:
            nums = sorted({int(p) for p in prefixes})
            if nums != list(range(1, len(nums) + 1)):
                findings.append(("ORDER", "`%s` sequences its edges %s, which is not contiguous "
                                          "from 1" % (endpoints[src], nums)))

    # ---- the prose tally ----------------------------------------------------------------------
    m = TALLY.search(text)
    if not m:
        findings.append(("VACUOUS", "the component tally sentence was not found; it cannot be checked"))
    else:
        claimed = [WORDS.get(g, -1) for g in m.groups()]
        actual = [len(comp)] + [sum(1 for v in comp.values() if v["kind"] == k) for k in TALLY_ORDER]
        if claimed != actual:
            findings.append(("TALLY", "prose says %s, register says %s" % (claimed, actual)))

    counts = dict(components=len(comp), elements=len(elem), compile_edges=len(compile_edges),
                  runtime_edges=len(runtime_edges), grants=len(grants))
    for key, floor in sorted(FLOOR.items()):
        if counts[key] < floor:
            findings.append(("VACUOUS", "only %d %s parsed, floor is %d -- the parser has regressed"
                             % (counts[key], key, floor)))
    findings.extend(check_zone_order(comp, grants))
    findings.extend(check_granted(comp, grants))
    findings.extend(check_cardinality(elem))
    findings.extend(check_runtime_coverage(comp, elem))

    return findings, counts


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 on any finding (gate mode)")
    ap.add_argument("--spec", default=str(SPEC), help="spec to read")
    args = ap.parse_args(argv)

    path = pathlib.Path(args.spec)
    if not path.exists():
        print("spec-consistency: %s not found" % path)
        return 1
    findings, counts = check(path.read_text(encoding="utf-8"))

    print("spec-consistency: %d components, %d elements, %d compile edges, %d runtime edges, "
          "%d grant rows" % (counts["components"], counts["elements"], counts["compile_edges"],
                             counts["runtime_edges"], counts["grants"]))
    for verdict, message in findings:
        print("  %-12s %s" % (verdict, message))
    if findings:
        print("spec-consistency: FAIL -- %d finding(s)" % len(findings))
        return 1 if args.check else 0
    print("spec-consistency: OK -- both projections agree with the registers, and every runtime edge "
          "is legal in the compile projection")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
