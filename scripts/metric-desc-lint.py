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

Usage:
    metric-desc-lint.py --check      # scan source/, exit 1 on any violation
    metric-desc-lint.py --selftest   # prove the detector fires, and does not over-fire
"""
import pathlib
import re
import sys

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


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sys.exit(selftest() if arg == "--selftest" else check())
