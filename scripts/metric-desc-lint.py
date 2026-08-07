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
LAZY_REG_CEILING = 95

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


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sys.exit({"--selftest": selftest,
              "--check-registrations": check_registrations,
              "--selftest-registrations": selftest_registrations}.get(arg, check)())
