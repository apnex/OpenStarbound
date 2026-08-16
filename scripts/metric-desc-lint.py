#!/usr/bin/env python3
"""metric_desc_lint -- a MetricDesc prose field may not be set positionally.

THE HAZARD THIS EXISTS FOR. MetricDesc is an aggregate whose fields are, with one exception, all
distinct enum types: transposing domain and owner is ALREADY a compile error, which is why nobody
needs designated initializers to write one safely. The exception is the pair

    char const* measures;    // the physical quantity, in words
    char const* validWhen;   // the condition under which the value IS that quantity

-- same type, adjacent, and a third `char const* whole` behind them. Written in the wrong order they
compile, link, pass every existing gate, and produce a descriptor whose stated meaning and stated
condition are SWAPPED. There is no runtime symptom: the two strings are only ever printed.

THE RULE. Any string literal inside a MetricDesc initializer must be introduced by its own designator
-- `.measures =`, `.validWhen =` or `.whole =`. Then the field a string lands in is written down at
the site rather than counted out by position, and a transposition is a visible edit rather than an
invisible one.

WHY THIS FILE EXISTS AT ALL. StarMetricDesc.hpp cited it by name -- "scripts/metric-desc-lint.py
therefore requires DESIGNATED INITIALIZERS ... A convention is not enforcement; the lint is" -- and
the file did not exist. That is the sharpest form of the failure this project keeps finding: not a
missing check, but a sentence asserting that a check is in place. Found 2026-08-06 while verifying
an unrelated cluster; the choice was to build it or to write "none", and building it was cheaper
than the paragraph explaining why not.

IT REPORTS ITS DENOMINATOR, and that is not decoration. Zero sites set a prose field today, so a
bare "OK" would be a gate that ran and compared nothing -- the exact shape #223 caught elsewhere.
Printing "N sites examined, M set a prose field" makes a vacuous pass legible as one.

SECOND RULE IN THIS FILE, reg_ratchet (#243). A telemetry handle registered on a path that is not
always taken makes the key ABSENT rather than ZERO, and a consumer differencing two snapshots cannot
tell "ran and found nothing" from "never ran". SEVEN instances of that one pattern were found in two
days -- R07 through R11, then R13 (tick.server.lock.sync.us, which stamped 28 of 28 matrix legs
not-quotable), then R14 (a compose arm that never runs at a given scene) -- and two more, R15 and R16,
while fixing R14.

Full static reachability analysis is hard and not worth it. The cheap sound form is a RATCHET on the
one spelling that always means lazy:

    static auto x = Telemetry::(counter|gauge|timer|rate)(...)   in a function  -> LAZY
    auto x       = Telemetry::(counter|gauge|timer|rate)(...)    namespace scope -> EAGER

That distinction is a real convention here, not an assumption: all 95 `static auto` registrations are
indented (none at column 0), and every eager registration shipped by #240 and #241 is written WITHOUT
`static`. So `static` on a Telemetry registration MEANS lazy, and the ratchet counts it. Writing an
eager namespace-scope registration? Drop the `static` -- in an anonymous namespace it buys nothing,
and keeping it would make a correct fix read as a regression here.

THE ONE SHAPE THIS DOES NOT COVER, named rather than left silent: a function-local NON-static
registration -- `auto c = Telemetry::counter(key, desc)` inside a function body -- registers on first
call exactly like the static form. There is exactly one in the tree, cpu.process.total_us inside
writeSnapshot() in source/core/StarTelemetryReporter.cpp, and it is low risk because writeSnapshot
runs on a fixed interval and so always registers early. It is NOT counted, because separating it from
the eager namespace-scope form needs brace-depth parsing, and an indentation heuristic is precisely
what scripts/gputimer-brackets.py refuses ("indentation is a style, braces are the language"). If a
second one appears, this paragraph is where the note is.

Usage:
    metric-desc-lint.py --check                   # prose rule: scan source/, exit 1 on violation
    metric-desc-lint.py --selftest                # prove the prose detector fires and does not over-fire
    metric-desc-lint.py --check-registrations     # ratchet: the lazy registration count may not grow
    metric-desc-lint.py --selftest-registrations  # prove the ratchet fires, and cannot pass vacuously
"""
import pathlib
import re
import sys

# `static` is the whole signal -- see the second rule above. Star:: appears on the sites in the
# anonymous namespace of StarRenderer_opengl.cpp, which sits outside `namespace Star`.
LAZY_REG = re.compile(r"\bstatic\s+auto\s+\w+\s*=\s*(?:Star::)?Telemetry::(?:counter|gauge|timer|rate)\s*\(")

# TODAY'S COUNT, PINNED 2026-08-07. LOWER IT whenever the real number drops: a ceiling left above the
# count is slack, and slack is exactly the room the next lazy registration slides into unnoticed. The
# check prints the gap and says so rather than quietly tolerating it.
LAZY_REG_CEILING = 87

# No nested braces occur inside a MetricDesc initializer, so [^}] is a sound terminator here.
SITE = re.compile(r"MetricDesc\s*\{([^}]*)\}", re.S)
STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
DESIGNATED = re.compile(r"\.(measures|validWhen|whole)\s*=\s*$", re.S)
PROSE_FIELDS = ("measures", "validWhen", "whole")


def strip_comments(text):
    """Drop // comments while PRESERVING line structure, so reported line numbers stay true.

    Truncating rather than blanking shifts columns and not lines, and only lines are reported.
    """
    return "\n".join(ln[:ln.find("//")] if "//" in ln else ln for ln in text.split("\n"))


def violations(text, path="<mem>"):
    """Every string literal inside a MetricDesc initializer that no designator introduces."""
    out = []
    clean = strip_comments(text)
    for site in SITE.finditer(clean):
        body, base = site.group(1), site.start(1)
        for lit in STRING.finditer(body):
            if DESIGNATED.search(body[:lit.start()]):
                continue
            line = clean.count("\n", 0, base + lit.start()) + 1
            out.append((path, line, lit.group(0)[:40]))
    return out


def prose_sites(text):
    """How many MetricDesc sites set a prose field at all -- this lint's live denominator."""
    clean = strip_comments(text)
    return sum(1 for s in SITE.finditer(clean) if STRING.search(s.group(1)))


def site_count(text):
    return len(SITE.findall(strip_comments(text)))


ENUMS_ONLY = '''
  static auto t = Telemetry::timer("x.y.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
'''

DESIGNATED_OK = '''
  static auto t = Telemetry::timer("x.y.us",
    MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Gl,
               .measures = "microseconds of GPU spread", .validWhen = "deep telemetry is armed"});
'''

POSITIONAL_BAD = '''
  static auto t = Telemetry::timer("x.y.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail,
               MetricUnit::Microseconds, MetricClock::Wall, MetricSource::InProcess,
               MetricBoundedness::Monotonic, "microseconds of GPU spread", "deep telemetry is armed"});
'''

# THE HALF-DESIGNATED CASE IS THE ONE THAT MATTERS. One designator followed by a bare literal is
# exactly the transposition this rule exists to stop, and it is the shape a careless edit produces.
HALF_BAD = '''
  static auto t = Telemetry::timer("x.y.us",
    MetricDesc{.measures = "microseconds of GPU spread", "deep telemetry is armed"});
'''

# A string literal OUTSIDE any MetricDesc -- the metric key itself -- must never trip the detector.
# Every registration site in the tree has one, so over-firing here would flag all 139 of them.
KEY_ONLY = '''
  static auto c = Telemetry::counter("render.vbo.orphaned",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
'''


def selftest():
    fails = []

    def arm(desc, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {desc}")
        if not ok:
            fails.append(desc)

    arm("a positional prose field fires", bool(violations(POSITIONAL_BAD, "POSITIONAL_BAD")))
    arm("one designator then a bare literal fires -- the transposition shape",
        bool(violations(HALF_BAD, "HALF_BAD")))
    arm("fully designated prose does not over-fire", not violations(DESIGNATED_OK, "DESIGNATED_OK"))
    arm("an enum-only descriptor does not over-fire", not violations(ENUMS_ONLY, "ENUMS_ONLY"))
    arm("the metric KEY outside the braces does not over-fire", not violations(KEY_ONLY, "KEY_ONLY"))
    arm("the prose-site denominator counts what it says",
        prose_sites(DESIGNATED_OK) == 1 and prose_sites(ENUMS_ONLY) == 0)

    print()
    if fails:
        print(f"metric-desc-lint selftest: FAILED -- {len(fails)}: {', '.join(fails)}")
        return 1
    print("  metric-desc-lint selftest: 6/6 arms ok -- fires on both positional shapes, declines to "
          "fire four ways")
    return 0


def check():
    root = pathlib.Path(__file__).resolve().parent.parent / "source"
    found, sites, prose = [], 0, 0
    for p in sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.hpp"))):
        text = p.read_text(encoding="utf-8", errors="replace")
        rel = str(p.relative_to(root.parent))
        found += violations(text, rel)
        sites += site_count(text)
        prose += prose_sites(text)
    for path, line, lit in found:
        print(f"  {path}:{line}: MetricDesc prose field set POSITIONALLY ({lit}). measures/validWhen/"
              f"whole are all `char const*`, so the order is not checked by the compiler -- write "
              f"`.measures = ` / `.validWhen = ` / `.whole = ` at the site.")
    if found:
        print(f"metric_desc_lint: FAIL -- {len(found)} positional prose field(s)")
        return 1
    if prose == 0:
        # SAY SO. The fields are declared and nothing populates them, so this gate is guarding a
        # hazard that is latent rather than live. A bare OK would read as "checked and clean".
        print(f"metric_desc_lint: OK -- {sites} MetricDesc site(s), NONE setting measures/validWhen/"
              f"whole. Nothing to transpose yet; the rule is armed for the first site that does.")
        return 0
    print(f"metric_desc_lint: OK -- {sites} MetricDesc site(s), {prose} setting a prose field, all "
          f"via designated initializers")
    return 0


def lazy_registrations(text):
    """Count of function-local `static auto ... = Telemetry::<kind>(` sites, comments stripped."""
    return len(LAZY_REG.findall(strip_comments(text)))


def sources():
    root = pathlib.Path(__file__).resolve().parent.parent / "source"
    return {str(p.relative_to(root.parent)): p.read_text(encoding="utf-8", errors="replace")
            for p in sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.hpp")))}


def check_registrations(files=None, ceiling=LAZY_REG_CEILING):
    files = sources() if files is None else files
    per = {path: lazy_registrations(text) for path, text in files.items()}
    per = {p: n for p, n in per.items() if n}
    total = sum(per.values())

    if total > ceiling:
        print(f"reg_ratchet: FAIL -- {total} lazy telemetry registration(s), ceiling {ceiling}")
        for path, n in sorted(per.items(), key=lambda kv: -kv[1]):
            print(f"    {n:3d}  {path}")
        print()
        print("  A `static auto x = Telemetry::...` inside a function registers the key on the first")
        print("  call, so a path that is never taken leaves the key ABSENT rather than at zero -- and")
        print("  a consumer differencing two snapshots cannot tell that from 'ran and found nothing'.")
        print("  Register at namespace scope instead, WITHOUT `static` (see #240, #241 for the shape).")
        print("  Raising the ceiling is a decision, not a formality: it says one more metric may lie")
        print("  about whether its code ran.")
        return 1

    # ZERO IS A FAILURE, NOT A CLEAN BILL. If the regex stops matching -- a spelling change, a moved
    # namespace, a refactor of the accessor -- the count collapses to 0 and a ratchet that only tests
    # `total > ceiling` would report the tree's best-ever result. That is the vacuous-pass shape this
    # repo has now hit three times (#223, and twice today in gates written the same afternoon).
    if total == 0:
        print("reg_ratchet: FAIL -- matched 0 registrations in the whole tree. The tree cannot have "
              "become perfect; the pattern has stopped matching what the code says.")
        return 1

    if total < ceiling:
        # LOUD, because slack is where the next one hides. Not a failure: shrinking is the goal.
        print(f"reg_ratchet: OK -- {total} lazy telemetry registration(s), ceiling {ceiling}")
        print(f"    the ceiling is {ceiling - total} above the real count. LOWER IT to {total}: a "
              f"ratchet with slack is a ratchet that lets the next one in unnoticed.")
        return 0

    print(f"reg_ratchet: OK -- {total} lazy telemetry registration(s), exactly at the ceiling "
          f"({ceiling}); none added")
    return 0


LAZY_ONE = '''
void f() {
  static auto t = Telemetry::timer("a.b.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
}
'''

# The EAGER shape, which must NOT be counted -- otherwise the ratchet punishes the very fix that
# #240 and #241 shipped, and the next person removes the fix to get the gate green.
EAGER_NS = '''
namespace {
  auto s_t = Telemetry::timer("a.b.us",
    MetricDesc{MetricDomain::Cpu, MetricOwner::Frame, MetricCadence::Call, MetricRole::Detail});
}
'''

# A registration named only inside a COMMENT is not a registration.
COMMENTED_OUT = '''
void f() {
  // static auto t = Telemetry::timer("a.b.us", MetricDesc{});
}
'''


def selftest_registrations():
    fails = []
    if lazy_registrations(LAZY_ONE) != 1:
        fails.append("the lazy shape was not counted")
    if lazy_registrations(EAGER_NS) != 0:
        fails.append("the EAGER namespace-scope shape was counted as lazy")
    if lazy_registrations(COMMENTED_OUT) != 0:
        fails.append("a commented-out registration was counted")
    # Captured: a selftest that prints its negative arms' FAIL text while PASSING is how you learn to
    # skim past FAIL text. Same correction as scripts/pmu-join.py, same afternoon.
    import contextlib, io
    with contextlib.redirect_stdout(io.StringIO()):
        grew = check_registrations({"a.cpp": LAZY_ONE * 3}, ceiling=2)
        at_ceiling = check_registrations({"a.cpp": LAZY_ONE * 2}, ceiling=2)
        vacuous = check_registrations({"a.cpp": "int main() { return 0; }"}, ceiling=2)
    if grew != 1:
        fails.append("growth past the ceiling did not fire")
    if at_ceiling != 0:
        fails.append("a count exactly at the ceiling did not pass")
    if vacuous != 1:
        fails.append("an empty match set passed vacuously instead of failing")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  reg_ratchet selftest: 6/6 arms ok (counts lazy, ignores eager and comments, fires on "
          "growth, passes at the ceiling, refuses to pass on zero matches)")
    return 0


# A GPU TIMELINE SPAN CANNOT BE A BUDGET OR A WHOLE, and this is the rule that keeps it that way.
#
# EARNED, AND EXPENSIVELY. render.frame.gpu_span_us was declared MetricRole::Total -- owner gl's
# denominator -- while measuring a GL_TIME_ELAPSED span. It reported ~16,200us, the frame PERIOD, at
# 0.22%, 11.39%, 23.39% and 32.93% real engine busy alike: constant across a 1.4x change in the very
# quantity it denominated. Every "% of the GPU frame" derived from it was a ratio against a constant.
#
# The 13 pass timers were declared Budget on the same reasoning and closed against it, which is how a
# zero-tolerance oracle came to compare one span against a sum of spans and call the agreement a budget.
#
# THE TEST IS THE CLOCK, NOT THE DOMAIN. A gpu-domain metric read from the PMU or from drm-engine
# accounting has clock=GpuEngine and IS work; it may legitimately be a Total. What may never be one is
# a TIMELINE: elapsed wall between two GPU markers, which includes every stall and every gap.
GPU_TIMELINE_WHOLE = re.compile(
    r"MetricDesc\{[^}]*?MetricRole::(Budget|Total)[^}]*?MetricClock::GpuTimeline[^}]*?\}"
    r"|MetricDesc\{[^}]*?MetricClock::GpuTimeline[^}]*?MetricRole::(Budget|Total)[^}]*?\}",
    re.S)


def timeline_wholes(text):
    """(line, role) for every descriptor pairing a GpuTimeline clock with Budget or Total."""
    stripped = strip_comments(text)
    out = []
    for m in GPU_TIMELINE_WHOLE.finditer(stripped):
        out.append((stripped[:m.start()].count("\n") + 1, m.group(1) or m.group(2)))
    return out


def check_timeline():
    root = pathlib.Path(__file__).resolve().parent.parent / "source"
    found, seen = [], 0
    for p in sorted(list(root.rglob("*.cpp")) + list(root.rglob("*.hpp"))):
        if "/test/" in str(p):
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        seen += text.count("MetricClock::GpuTimeline")
        for line, role in timeline_wholes(text):
            found.append((str(p.relative_to(root.parent)), line, role))
    for path, line, role in found:
        print(f"  {path}:{line}: MetricRole::{role} declared with MetricClock::GpuTimeline. A timeline "
              f"span is elapsed time between two GPU markers, stalls and gaps included -- it is not "
              f"work, so it can neither BE a whole nor close against one. Declare it Detail, or measure "
              f"work (clock=GpuEngine) instead.")
    if found:
        print(f"gpu_timeline_role: FAIL -- {len(found)} timeline span(s) declared as a budget or a whole")
        return 1
    if seen == 0:
        # A rule with nothing in scope has not been satisfied, it has been skipped. Saying so is the
        # difference between this gate and one that greps for a word the tree stopped using.
        print("gpu_timeline_role: OK -- but NOTHING declares MetricClock::GpuTimeline, so nothing was "
              "checked. This gate is inert, not clean.")
        return 0
    print(f"gpu_timeline_role: OK -- {seen} GpuTimeline declaration(s), none of them Budget or Total")
    return 0


def selftest_timeline():
    """Fires on both field orders, and declines where the clock is work rather than a timeline."""
    fails = []

    def arm(name, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            fails.append(name)

    arm("Total after the clock fires", len(timeline_wholes(
        "MetricDesc{MetricDomain::Gpu, MetricClock::GpuTimeline, MetricRole::Total}")) == 1)
    arm("Total before the clock fires too -- field order is not the rule", len(timeline_wholes(
        "MetricDesc{MetricDomain::Gpu, MetricRole::Total, MetricClock::GpuTimeline}")) == 1)
    arm("Budget fires", len(timeline_wholes(
        "MetricDesc{MetricRole::Budget, MetricClock::GpuTimeline}")) == 1)
    arm("Detail does NOT fire -- the shape the tree now uses", len(timeline_wholes(
        "MetricDesc{MetricRole::Detail, MetricClock::GpuTimeline}")) == 0)
    # THE ARM THAT STOPS THIS BECOMING A DOMAIN RULE. Work measured on the GPU may be a whole; only a
    # timeline may not. Without this, someone would "fix" a false positive by widening the exemption.
    arm("a GpuEngine Total does NOT fire -- that one IS work", len(timeline_wholes(
        "MetricDesc{MetricDomain::Gpu, MetricRole::Total, MetricClock::GpuEngine}")) == 0)
    arm("commented-out code does not count", len(timeline_wholes(
        "// MetricDesc{MetricRole::Total, MetricClock::GpuTimeline}")) == 0)

    print()
    if fails:
        print(f"gpu_timeline_role selftest: FAILED -- {len(fails)}: {', '.join(fails)}")
        return 1
    print("  gpu_timeline_role selftest: 6/6 arms ok -- fires on both field orders and both roles, "
          "declines on Detail, declines on a work clock, and ignores comments")
    return 0


# THE CLOCK RATCHET. MetricClock has had the right enumerators since the descriptor convergence and, until
# today, ZERO production sites populated any of them -- a field declared, documented, and answering nothing.
#
# It has no default ON PURPOSE, which the header spells out: defaulting to Wall would make every unmigrated
# timer assert a clock nobody checked, and a field claiming otherwise is more convincing than an absent one.
# So Undeclared is honest, and the number of them is the honest measure of how much of this model still
# cannot say whether it measures work or waiting.
#
# A CEILING RATHER THAN A REQUIREMENT, because 165 sites cannot be migrated in one pass without becoming the
# highest-risk moment for reintroducing every pattern this file already guards. The count may fall and may
# not rise. Lower it when you lower it.
# 105 sites use a form that declares NEITHER a clock nor a unit. Measured over the repaired corpus
# (production only, empty sentinels excluded) -- see check_desc_facets for why each exclusion exists.
FACET_CEILING = 105

METRIC_DESC_BLOCK = re.compile(r"MetricDesc\{.*?\}", re.S)
# `MetricDesc{}` is not an under-declared descriptor, it is the deliberate absence of one: one site
# passes it beside `/* hasDesc */ false`, the other returns it for "this metric has no descriptor".
# Counting them as sites that failed to declare a clock made the ratchet count a unit that the thing
# it names cannot move.
EMPTY_DESC = re.compile(r"^MetricDesc\{\s*\}$")


def declaring_sites(text):
    """-> [str] of MetricDesc blocks that actually declare something. Comments stripped."""
    return [m.group(0) for m in METRIC_DESC_BLOCK.finditer(strip_comments(text))
            if not EMPTY_DESC.match(m.group(0))]


def undeclared_facets(text):
    """-> (no_clock, no_unit, sites) over the declaring sites in one file."""
    sites = declaring_sites(text)
    return (sum(1 for b in sites if "MetricClock::" not in b),
            sum(1 for b in sites if "MetricUnit::" not in b),
            len(sites))


def check_desc_facets(files=None, ceiling=FACET_CEILING):
    """The count of MetricDesc sites that under-declare may not grow.

    TWO CORPUS REPAIRS, BOTH BECAUSE A RATCHET MUST COUNT A UNIT ONLY THE THING IT NAMES CAN MOVE.
    It rglobbed all of source/ INCLUDING source/test/, where one fixture site was undeclared against
    a slack of exactly one -- so deleting a test file was a legal way to buy headroom for a new
    production violation. And it counted the two `MetricDesc{}` sentinels in StarTelemetry.cpp,
    which are not under-declared descriptors but the deliberate absence of one. 108/165 was
    therefore three parts wrong; the repaired corpus is 105/161.

    WHY BOTH FACETS UNDER ONE CEILING RATHER THAN A SECOND `unit_ratchet`. The plan called for a
    separate unit ratchet. Measured first: the two sets are IDENTICAL -- all 105 sites that declare
    no clock also declare no unit, and every site declaring one declares the other, because the
    population is exactly the sites still using the positional form. A second ratchet would be a
    second name for one fact, with two ceilings free to drift apart while measuring one population.
    One ceiling counts sites that under-declare; the per-facet numbers are still reported, so a
    regression in EITHER facet alone moves the count and is named in the output.
    """
    files = sources() if files is None else files
    no_clock = no_unit = under = sites = 0
    for path, text in sorted(files.items()):
        if "/test/" in path.replace("\\", "/"):
            continue
        blocks = declaring_sites(text)
        sites += len(blocks)
        for b in blocks:
            c = "MetricClock::" not in b
            u = "MetricUnit::" not in b
            no_clock += c
            no_unit += u
            under += (c or u)
    if sites == 0:
        # A ratchet that matched nothing has not been satisfied. reg_ratchet learned this the same way:
        # a pattern that stops matching reads exactly like a tree that stopped offending.
        print("desc_facet_ratchet: FAIL -- no declaring MetricDesc sites matched at all. The pattern "
              "has drifted from the tree, so this gate is measuring nothing.")
        return 1
    if under > ceiling:
        print(f"desc_facet_ratchet: FAIL -- {under} MetricDesc site(s) under-declare "
              f"({no_clock} no clock, {no_unit} no unit), ceiling is {ceiling}. A metric that cannot "
              f"say whether it measures work or waiting, or in what unit, is the defect these fields "
              f"exist to end; declare them at the new site rather than raising the bar.")
        return 1
    print(f"desc_facet_ratchet: OK -- {under} of {sites} declaring MetricDesc site(s) under-declare "
          f"({no_clock} no clock, {no_unit} no unit; ceiling {ceiling}, slack {ceiling - under}). "
          f"{sites - under} declare both. source/test/ and empty MetricDesc{{}} are excluded.")
    return 0


def selftest_desc_facets():
    import contextlib, io
    fails = []

    def arm(name, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            fails.append(name)

    both = "MetricDesc{MetricDomain::Cpu, MetricClock::Wall, MetricUnit::Us}"
    bare = "MetricDesc{MetricDomain::Cpu, MetricRole::Total}"
    unit_only = "MetricDesc{MetricDomain::Cpu, MetricUnit::Us}"
    arm("a site declaring neither counts", undeclared_facets(bare)[:2] == (1, 1))
    arm("a site declaring both does not", undeclared_facets(both)[:2] == (0, 0))
    arm("a commented-out site does not count", undeclared_facets("// " + bare)[2] == 0)
    arm("both are seen when both are present", undeclared_facets(bare + "\n" + both)[0] == 1)

    # The repairs, each proven to bite rather than merely described in the docstring.
    arm("an empty MetricDesc{} is not a declaring site", undeclared_facets("MetricDesc{}")[2] == 0)
    arm("...and does not inflate the undeclared count", undeclared_facets("MetricDesc{}")[:2] == (0, 0))

    with contextlib.redirect_stdout(io.StringIO()):
        prod_only = check_desc_facets({"source/core/a.cpp": bare, "source/test/t.cpp": bare},
                                      ceiling=1)
        # If source/test/ still counted, two undeclared against a ceiling of one would FAIL.
    arm("a fixture under source/test/ cannot move the count", prod_only == 0)

    with contextlib.redirect_stdout(io.StringIO()):
        over = check_desc_facets({"source/a.cpp": bare + "\n" + bare}, ceiling=1)
        under = check_desc_facets({"source/a.cpp": bare}, ceiling=1)
        empty = check_desc_facets({"source/a.cpp": "int main(){}"}, ceiling=1)
        # A site that declares a unit but no clock must still be counted by the single ceiling --
        # this is what a separate unit_ratchet would have caught, and why one ceiling suffices.
        clock_regression = check_desc_facets({"source/a.cpp": both + "\n" + unit_only}, ceiling=0)
    arm("two undeclared against a ceiling of one FAILS", over == 1)
    arm("one undeclared against a ceiling of one passes", under == 0)
    arm("a corpus with NO MetricDesc at all fails rather than passing vacuously", empty == 1)
    arm("a site missing ONLY the clock still moves the single ceiling", clock_regression == 1)

    print()
    if fails:
        print(f"desc_facet_ratchet selftest: FAILED -- {len(fails)}: {', '.join(fails)}")
        return 1
    print("  desc_facet_ratchet selftest: 11/11 arms ok -- counts the under-declared, ignores "
          "comments, empty sentinels and test fixtures, fires on growth in EITHER facet, and "
          "refuses to pass on an empty corpus")
    return 0


GAUGE_SITE = re.compile(r'Telemetry::gauge\s*\(\s*"([^"]+)"\s*,\s*(MetricDesc\s*\{[^}]*\})', re.S)


def undeclared_gauges(text):
    """-> [key] for gauge registrations whose descriptor declares no MetricBoundedness."""
    stripped = strip_comments(text)
    return [m.group(1) for m in GAUGE_SITE.finditer(stripped)
            if "MetricBoundedness::" not in m.group(2)]


def check_boundedness(files=None):
    """Every gauge must say whether it is a LEVEL or a PEAK. Not a ratchet -- a rule.

    A ratchet is the right shape when a population is large and must be walked down. This population
    is EIGHT, and it is at zero today, so a ceiling would only be a licence for the ninth gauge to
    arrive undeclared. The rule is cheaper and it is the shape this project reaches for whenever the
    option exists: structural impossibility over vigilance.

    WHY IT MATTERS RATHER THAN BEING TIDINESS. `telemetry-window.py` used to window EVERY gauge as a
    level -- "carry the latest reading" -- and a HighWaterMark gauge is written `s = max(s, x)` and
    never falls, so its latest reading is the largest since PROCESS START, not since the window
    opened. Carried into a window's `value` it reads as "the peak during this leg", which it is not.
    StarMetricDesc.hpp:95-101 records that defect shipping twice, and the histogram `max` field as a
    third instance. source/test/ is excluded so the count cannot be moved by deleting a fixture.
    """
    files = files if files is not None else sources()
    bad = {}
    corpus = 0
    for path, text in sorted(files.items()):
        if "/test/" in path.replace("\\", "/"):
            continue
        stripped = strip_comments(text)
        corpus += len(GAUGE_SITE.findall(stripped))
        u = undeclared_gauges(text)
        if u:
            bad[path] = u
    for path, keys in bad.items():
        for k in keys:
            print(f"  {path}: gauge \"{k}\" declares no MetricBoundedness -- is it a LEVEL or a PEAK?")
    total = sum(len(v) for v in bad.values())
    print(f"boundedness_declared: {corpus} gauge site(s) outside source/test/, {total} undeclared")
    if not corpus:
        print("boundedness_declared: NOT RUN -- no gauge sites found. The pattern has drifted; "
              "nothing was checked and this is NOT a pass.")
        return 77
    if total:
        print(f"boundedness_declared: FAIL -- {total} gauge(s) undeclared. A consumer cannot tell a "
              f"level from a peak, and windowing a peak as a level has shipped three times.")
        return 1
    print("boundedness_declared: OK -- every gauge says whether it is a level or a peak")
    return 0


def selftest_boundedness():
    arms, bad = [], 0

    def arm(name, ok):
        nonlocal bad
        arms.append((name, ok))
        if not ok:
            bad += 1

    declared = ('static auto g = Telemetry::gauge("a.b", MetricDesc{.domain = MetricDomain::Cpu, '
                '.boundedness = MetricBoundedness::Level});')
    bare = 'static auto g = Telemetry::gauge("a.b", MetricDesc{MetricDomain::Cpu});'
    arm("a declared gauge does not fire", undeclared_gauges(declared) == [])
    arm("a bare gauge fires, naming its key", undeclared_gauges(bare) == ["a.b"])
    arm("a commented-out site does not count", undeclared_gauges("// " + bare) == [])
    arm("a counter is not a gauge and is out of scope",
        undeclared_gauges('Telemetry::counter("a.b", MetricDesc{MetricDomain::Cpu});') == [])
    arm("an undeclared gauge in a NON-test file FAILS the check",
        check_boundedness({"source/x.cpp": bare}) == 1)
    arm("...the same gauge under source/test/ does not, and the empty corpus SKIPS",
        check_boundedness({"source/test/x.cpp": bare}) == 77)
    arm("a declared gauge passes", check_boundedness({"source/x.cpp": declared}) == 0)

    for name, ok in arms:
        print("  %-62s %s" % (name, "ok" if ok else "FAILED"))
    if bad:
        print(f"boundedness_declared: SELFTEST FAIL -- {bad} arm(s)")
        return 1
    print(f"boundedness_declared selftest: {len(arms)}/{len(arms)} arms ok -- an undeclared gauge "
          f"fires, source/test/ cannot move the count, and an empty corpus SKIPS rather than passes")
    return 0


REPO = pathlib.Path(__file__).resolve().parent.parent

# A claim that something is IN FLIGHT, present tense. History ("the schema WAS mid-flight, the bump
# landed at 9db54200") is warrant and is deliberately not matched -- see check_convergence_claims.
IN_FLIGHT = re.compile(r"(?<!was )(?<!were )(?<!been )"
                       r"(mid-convergence|mid-flight|being converged|being replaced|"
                       r"in the middle of replacing)", re.I)
# The destination such a claim must name.
TRANSITION = re.compile(r"schema\s*(\d+)\s*->\s*(\d+)")
# One writer for the live schema: telemetry-window.py declares it, this gate reads it.
SCHEMA_DECL = re.compile(r"^SCHEMA\s*=\s*(\d+)", re.M)


# The rule's own definition and fixtures necessarily contain the strings the rule forbids: the
# regex spells them, and selftest_convergence_claims() asserts against the exact sentence that
# shipped. This file is therefore the one place the scan cannot read literally. A closed literal of
# ONE, not a pattern -- a self-exclusion that can grow is how a gate stops gating, and an arm below
# asserts the set never widens. The cost is a declared blind spot: a genuinely stale claim in THIS
# file's prose is not caught by this gate.
SELF_EXCLUDED = frozenset({pathlib.Path(__file__).resolve()})


def live_schema():
    """-> int, read from the consumer that declares it. Raises if the declaration moves."""
    src = (REPO / "scripts" / "telemetry-window.py").read_text()
    m = SCHEMA_DECL.search(src)
    if not m:
        raise SystemExit("convergence_claims: telemetry-window.py no longer declares SCHEMA = N; "
                         "this gate reads that declaration and cannot run without it")
    return int(m.group(1))


def stale_claims(text, live, path="<mem>"):
    """-> [(lineno, why)] for present-tense in-flight claims that are unfalsifiable or already done.

    THE RULE: a claim that something is mid-flight must name its destination, and that destination
    must not already have been reached. Both halves are load-bearing. Without the first, "the
    vocabulary is converging" is a claim no future reader can check and no gate can retire. Without
    the second, the claim survives its own completion -- which is exactly what happened here.
    """
    lines = text.splitlines()
    out = []
    for i, line in enumerate(lines):
        if not IN_FLIGHT.search(line):
            continue
        # The destination may wrap onto a neighbouring line in a comment block.
        near = " ".join(lines[max(0, i - 2):i + 3])
        dests = [int(m.group(2)) for m in TRANSITION.finditer(near)]
        if not dests:
            out.append((i + 1, "claims something is in flight but names no destination -- "
                               "unfalsifiable, so nothing can ever retire it"))
        elif min(dests) <= live:
            out.append((i + 1, "claims in-flight to schema %d, but the live schema is already %d -- "
                               "the transition landed and the claim did not" % (min(dests), live)))
    return out


def check_convergence_claims(files=None):
    """No runner may claim a migration is in flight after it has landed.

    WHY A GATE AND NOT A PROOFREAD. lever-matrix.sh did not merely CONTAIN a stale sentence, it
    WROTE one: the string went into `manifest.json` for every leg, so the claim was copied into
    banked evidence that nothing ever revisits. The schema bumped at 9db54200 and the runner was
    edited two days later without anyone noticing, so stale manifests already exist.

    WHY IT IS ANCHORED ON THE LIVE CONSTANT, NOT ON THE STRING "schema 3 -> 4". Three times in this
    project a reference stayed GREEN while naming the wrong thing, because the gate's vocabulary
    outlived the document's. A grep for the literal transition would pass the moment someone wrote
    "4 -> 5" and would have to be re-taught at every bump. Reading SCHEMA from telemetry-window.py
    means this gate retires each claim automatically, on the commit that lands the bump.
    """
    live = live_schema()
    scan = files if files is not None else sorted(
        f for f in (REPO / "scripts").rglob("*")
        if f.suffix in (".sh", ".py") and f.is_file() and f.resolve() not in SELF_EXCLUDED)
    bad, checked = [], 0
    for f in scan:
        path, text = (f, f.read_text()) if hasattr(f, "read_text") else f
        checked += 1
        for lineno, why in stale_claims(text, live, str(path)):
            bad.append((path, lineno, why))
    for path, lineno, why in bad:
        rel = path.relative_to(REPO) if hasattr(path, "relative_to") else path
        print("  %s:%d  %s" % (rel, lineno, why))
    print("convergence_claims: %d file(s) scanned against live schema %d, %d stale claim(s)"
          % (checked, live, len(bad)))
    if bad:
        print("convergence_claims: FAIL -- a runner states a migration is in flight that has "
              "landed, or names no destination. Rewrite it in the past tense as warrant, or name a "
              "destination beyond schema %d." % live)
        return 1
    print("convergence_claims: OK -- no runner claims an in-flight migration that has already "
          "landed, and every in-flight claim names a destination")
    return 0


def selftest_convergence_claims():
    """Prove the rule fires on the ACTUAL text that shipped, and does not fire on history."""
    arms, bad = [], 0

    def arm(name, ok):
        nonlocal bad
        arms.append((name, ok))
        if not ok:
            bad += 1

    # The exact sentence lever-matrix.sh wrote into every manifest.json, at today's live schema.
    shipped = ('"analysis": "NOT PERFORMED -- Cost attribution reads the telemetry vocabulary, '
               'which is mid-convergence (schema 3 -> 4)."')
    arm("the sentence that actually shipped FIRES", len(stale_claims(shipped, 4)) == 1)

    # ... and would NOT have fired when it was written, which is why proofreading never caught it.
    arm("the same sentence was CLEAN at schema 3", stale_claims(shipped, 3) == [])

    arm("an in-flight claim naming no destination FIRES",
        len(stale_claims("# the vocabulary is being converged", 4)) == 1)

    arm("a still-future destination is CLEAN",
        stale_claims("# mid-convergence (schema 4 -> 5)", 4) == [])

    arm("past-tense history is CLEAN, not narration to strip",
        stale_claims("# It said the schema was mid-flight (3 -> 4); the bump landed at 9db54200", 4)
        == [])

    arm("a bare transition with no in-flight claim is CLEAN (history is warrant)",
        stale_claims("# the bump landed: schema 3 -> 4, at 9db54200", 4) == [])

    arm("a destination wrapped onto the next line is still SEEN",
        len(stale_claims("# the vocabulary is mid-convergence\n# (schema 3 -> 4). See the spec.",
                         4)) == 1)

    arm("the live schema is read, not assumed", live_schema() >= 4)

    # The tree itself must be clean, or the gate is being registered over a known violation.
    arm("the self-exclusion is exactly one file -- this one",
        SELF_EXCLUDED == frozenset({pathlib.Path(__file__).resolve()}))

    arm("the real scripts/ tree passes", check_convergence_claims() == 0)

    for name, ok in arms:
        print("  %-62s %s" % (name, "ok" if ok else "FAILED"))
    if bad:
        print(f"convergence_claims: SELFTEST FAIL -- {bad} arm(s)")
        return 1
    print(f"convergence_claims selftest: {len(arms)}/{len(arms)} arms ok -- the shipped sentence "
          f"fires, it was clean when written, and past-tense history survives")
    return 0



# ============================================================================================
# THE LIGHTING-CPU UNION (#269). A cross-thread AGGREGATE, not a budget: its value IS the sum of its
# members, so it cannot fail a closure and declaring it buys a DEFINITION rather than a check. This
# gate is the check that makes the definition worth having, and it has exactly two jobs, because the
# arithmetic is only ever as good as the membership list:
#
#   EXHAUSTIVE     -- every member declares it, so the union cannot silently lose a term.
#   NON-OVERLAPPING -- nothing else declares it, so the union cannot silently count one twice.
#
# THE SECOND JOB IS THE ONE THAT NEARLY FAILED. `lighting.produce.particles.us` is lighting CPU by
# any plain reading and belongs in no union: it is nested inside `lighting.produce.prep.us`, which is
# a member. The first draft of this list had it in, and only reading the use site caught it -- the
# registration comment says "nested inside prep" in as many words.
UNION = "lighting.cpu.union.us"
UNION_MEMBERS = {
    "lighting.cpu.total.us",          # owner lighting's own Total; a member AND a whole, both true
    "lighting.produce.entities.us",
    "lighting.produce.prep.us",       # particles is INSIDE this -- see above
    "lighting.produce.adjust.us",
    "lighting.upload.us",
    "lighting.gpu.spread_scan.us",    # runs BEFORE cpu_cost's scope opens, so disjoint from it
    "lighting.gpu.cpu_cost.us",       # the thirteen drive parts are nested inside this one
}

# Cpu-domain `lighting.*` microsecond timers that are NOT members. Every one is nested inside a
# member, and this ratchet is what makes that a DECISION rather than an oversight: a new lighting CPU
# timer lands here and fails the gate until someone says which it is. A ceiling, not a target -- it
# may fall freely. Measured 2026-08-16, and it accounts for itself exactly:
#
#   12  lighting.cpu.{prologue,params,begin,gather,lights,point,spread,export,convert,calculate,
#                     publish,post}.us   -- inside lighting.cpu.total.us
#   13  lighting.gpu.drive.*.us          -- inside lighting.gpu.cpu_cost.us
#    1  lighting.produce.particles.us    -- inside lighting.produce.prep.us
#
# A ceiling whose composition is not written down is a number nobody can audit, which is how a
# ratchet comes to permit the thing it was raised against.
UNION_NONMEMBER_CEILING = 26

REGISTRATION = re.compile(
    r"Telemetry::(timer|counter|gauge|rate|declare)\(\s*\"([^\"]+)\"\s*,\s*(MetricDesc\{[^{}]*\})",
    re.S)


def union_sites(files):
    """-> (declared, candidates) key sets, over production sources with comments stripped.

    A CANDIDATE IS A Cpu-DOMAIN `lighting.*` TIMER WHOSE KEY ENDS `.us`, and each clause of that
    excludes a population no arithmetic could ever include -- the rule being that a ratchet must
    count only a unit the thing it names can move. Gpu-domain keys share the prefix
    (lighting.gpu.point.gpu_us) and are not CPU. Counters and gauges (lighting.cells,
    lighting.epoch.bump.*, lighting.temporal.recomputed) are COUNTS: a count cannot be a term in a
    sum of microseconds, so admitting them would pad this ceiling with 20-odd keys that can never
    offend, and pad it in a way that quietly buys headroom for one that can.
    """
    declared, candidates = set(), set()
    for path, text in sorted(files.items()):
        if "/test/" in path.replace("\\", "/"):
            continue
        for fn, key, block in REGISTRATION.findall(strip_comments(text)):
            if UNION in block:
                declared.add(key)
            if (fn == "timer" and key.startswith("lighting.") and key.endswith(".us")
                    and "MetricDomain::Cpu" in block):
                candidates.add(key)
    return declared, candidates


def check_union_membership(files=None, ceiling=UNION_NONMEMBER_CEILING):
    files = sources() if files is None else files
    declared, cpu_lighting = union_sites(files)

    if not cpu_lighting:
        # A pattern that stopped matching reads exactly like a tree that stopped offending -- the
        # lesson reg_ratchet and desc_facet_ratchet each learned once. Say so instead of passing.
        print("union_membership: FAIL -- no Cpu-domain lighting.* registration matched at all. The "
              "pattern has drifted from the tree, so this gate is measuring nothing.")
        return 1

    missing = UNION_MEMBERS - declared
    extra = declared - UNION_MEMBERS
    nonmembers = sorted(cpu_lighting - UNION_MEMBERS)

    violations = 0
    if missing:
        print(f"union_membership: FAIL -- {len(missing)} declared member(s) do NOT carry .whole = "
              f"\"{UNION}\", so the union is missing a term: {', '.join(sorted(missing))}")
        violations += 1
    if extra:
        print(f"union_membership: FAIL -- {len(extra)} key(s) declare the union but are not members. "
              f"Each is nested inside a member and would be counted twice: {', '.join(sorted(extra))}")
        violations += 1
    if len(nonmembers) > ceiling:
        print(f"union_membership: FAIL -- {len(nonmembers)} Cpu-domain lighting.* non-member key(s), "
              f"ceiling {ceiling}. A new one is not automatically outside the union; decide whether it "
              f"is a MEMBER (add it to UNION_MEMBERS and declare .whole) or NESTED inside one (leave "
              f"it, and raise this ceiling in the same commit): {', '.join(nonmembers)}")
        violations += 1

    if violations:
        return 1
    print(f"union_membership: {len(declared)}/{len(UNION_MEMBERS)} members declare {UNION}; "
          f"{len(nonmembers)} non-member Cpu lighting key(s) <= ceiling {ceiling}; "
          f"0 double-counted")
    return 0


def selftest_union_membership():
    """Each arm INJECTS the defect and requires the gate to fail. A gate nobody has seen fail is a
    claim with no instrument -- and this one guards arithmetic, where a silent pass is a wrong number
    rather than a missing warning."""
    ok = dict(sources())
    arms = []

    def fails(label, files, ceiling=UNION_NONMEMBER_CEILING):
        arms.append((label, check_union_membership(files, ceiling) == 1))

    # 1. The real tree passes. Without this the remaining arms prove only that the gate can fail.
    arms.append(("the shipped tree passes", check_union_membership(ok) == 0))

    # 2. A member drops its declaration -> the union silently loses a term.
    dropped = dict(ok)
    for path, text in ok.items():
        if "lighting.gpu.cpu_cost.us" in text:
            dropped[path] = text.replace(f'.whole = "{UNION}"', ".role = MetricRole::Detail", 1)
    fails("a member that stops declaring is caught", dropped)

    # 3. A NESTED key declares the union -> double count. This is the particles defect, injected.
    doubled = dict(ok)
    for path, text in ok.items():
        if "lighting.produce.particles.us" in text:
            doubled[path] = text.replace(
                'Telemetry::timer("lighting.produce.particles.us",\n    MetricDesc{',
                'Telemetry::timer("lighting.produce.particles.us",\n    MetricDesc{'
                f'.whole = "{UNION}", ', 1)
    fails("a nested key that declares the union is caught", doubled)

    # 4. A brand-new Cpu lighting key that is neither declared nor accounted for.
    grown = dict(ok)
    for path, text in ok.items():
        if "lighting.gpu.spread_scan.us" in text:
            grown[path] = text + (
                '\nnamespace { auto s_x = Telemetry::timer("lighting.invented.us",'
                '\n  MetricDesc{.domain = MetricDomain::Cpu, .owner = MetricOwner::Frame}); }\n')
    fails("a new Cpu lighting key forces a member-or-nested decision", grown)

    # 5. The pattern going blind must read as FAIL, not as a clean tree.
    fails("a corpus the pattern cannot match fails rather than passes", {"a.cpp": "int main(){}"})

    bad = sum(1 for _, good in arms if not good)
    for label, good in arms:
        print(f"  [{'ok' if good else 'FAIL'}] {label}")
    if bad:
        print(f"union_membership: SELFTEST FAIL -- {bad} of {len(arms)} arm(s)")
        return 1
    print(f"union_membership selftest: {len(arms)}/{len(arms)} arms ok -- exhaustive and "
          f"non-overlapping are both enforced, and a blind pattern fails loudly")
    return 0


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sys.exit({"--selftest": selftest,
              "--check-registrations": check_registrations,
              "--selftest-registrations": selftest_registrations,
              "--check-timeline": check_timeline,
              "--check-desc-facets": check_desc_facets,
              "--selftest-desc-facets": selftest_desc_facets,
              "--check-boundedness": check_boundedness,
              "--check-convergence-claims": check_convergence_claims,
              "--selftest-convergence-claims": selftest_convergence_claims,
              "--check-union-membership": check_union_membership,
              "--selftest-union-membership": selftest_union_membership,
              "--selftest-boundedness": selftest_boundedness,
              "--selftest-timeline": selftest_timeline}.get(arg, check)())
