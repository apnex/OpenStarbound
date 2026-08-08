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
CLOCK_CEILING = 109

METRIC_DESC_BLOCK = re.compile(r"MetricDesc\{.*?\}", re.S)


def undeclared_clocks(text):
    """Count of MetricDesc sites with no MetricClock, comments stripped."""
    stripped = strip_comments(text)
    return sum(1 for m in METRIC_DESC_BLOCK.finditer(stripped) if "MetricClock::" not in m.group(0))


def check_clocks(files=None, ceiling=CLOCK_CEILING):
    # sources() hands back {path: text}, the same shape check_registrations takes. Taking a list of paths
    # instead cost a debugging round: the loop iterated the dict's KEYS, parsed filenames as if they were
    # source, and reported that nothing matched -- which the "measuring nothing" arm caught, exactly as it
    # was written to.
    files = sources() if files is None else files
    total, sites = 0, 0
    for text in files.values():
        total += undeclared_clocks(text)
        sites += len(METRIC_DESC_BLOCK.findall(strip_comments(text)))
    if sites == 0:
        # A ratchet that matched nothing has not been satisfied. reg_ratchet learned this the same way:
        # a pattern that stops matching reads exactly like a tree that stopped offending.
        print("clock_ratchet: FAIL -- no MetricDesc sites matched at all. The pattern has drifted from "
              "the tree, so this gate is measuring nothing.")
        return 1
    if total > ceiling:
        print(f"clock_ratchet: FAIL -- {total} MetricDesc site(s) declare no MetricClock, ceiling is "
              f"{ceiling}. A metric that cannot say whether it measures work or waiting is the defect "
              f"this field exists to end; declare the clock at the new site rather than raising the bar.")
        return 1
    slack = ceiling - total
    print(f"clock_ratchet: OK -- {total} of {sites} MetricDesc site(s) declare no MetricClock "
          f"(ceiling {ceiling}, slack {slack}). {sites - total} DO declare one.")
    return 0


def selftest_clocks():
    import contextlib, io
    fails = []

    def arm(name, ok):
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            fails.append(name)

    declared = "MetricDesc{MetricDomain::Cpu, MetricRole::Total, MetricClock::Wall}"
    bare = "MetricDesc{MetricDomain::Cpu, MetricRole::Total}"
    arm("a site with no clock counts", undeclared_clocks(bare) == 1)
    arm("a site with a clock does not", undeclared_clocks(declared) == 0)
    arm("a commented-out site does not count", undeclared_clocks("// " + bare) == 0)
    arm("both are seen when both are present", undeclared_clocks(bare + "\n" + declared) == 1)

    # Both ends, per #194's rule. Silence has to be earned by being under the bar, not by matching nothing.
    with contextlib.redirect_stdout(io.StringIO()):
        over = check_clocks({"a": bare + "\n" + bare}, ceiling=1)
        under = check_clocks({"a": bare}, ceiling=1)
        empty = check_clocks({"a": "int main(){}"}, ceiling=1)
    arm("two undeclared against a ceiling of one FAILS", over == 1)
    arm("one undeclared against a ceiling of one passes", under == 0)
    arm("a corpus with NO MetricDesc at all fails rather than passing vacuously", empty == 1)

    print()
    if fails:
        print(f"clock_ratchet selftest: FAILED -- {len(fails)}: {', '.join(fails)}")
        return 1
    print("  clock_ratchet selftest: 7/7 arms ok -- counts the undeclared, ignores comments and declared "
          "sites, fires on growth, and refuses to pass on an empty corpus")
    return 0


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sys.exit({"--selftest": selftest,
              "--check-registrations": check_registrations,
              "--selftest-registrations": selftest_registrations,
              "--check-timeline": check_timeline,
              "--check-clocks": check_clocks,
              "--selftest-clocks": selftest_clocks,
              "--selftest-timeline": selftest_timeline}.get(arg, check)())
