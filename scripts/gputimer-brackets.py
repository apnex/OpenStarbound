#!/usr/bin/env python3
"""gputimer_brackets -- a GPU timer may not bracket a gate it does not enter.

THE DEFECT THIS EXISTS FOR (GPUTIMER-1, 9b3c9428). Three call sites opened a GL_TIME_ELAPSED query
on every frame but enclosed the draw only when a cache/dim gate passed:

    gpuTimer().begin("render.pass.parallax.gpu_us", {... Cadence::Frame ...});
    if (refreshParallax) {
      ... the only real work ...
    }
    gpuTimer().end("render.pass.parallax.gpu_us");

On the 2-in-3 skip frames that records an empty bracket -- a ~0us sample. 87% of parallax records,
80% of environment's and 100% of compose's were zeros, so `count` counted FRAMES rather than DRAWS
and the reported mean was a blend of real work with zeros: neither a per-frame nor a per-draw cost.
It also advanced the query ring once per frame against a refresh cadence of 3, phase-locking the one
expensive sample to a single ring slot, so its capture went all-or-nothing and shifted 26% -> 98%
mid-run. That moved the reported parallax cost 4.04x while the GPU did identical work, and was read
as a renderer regression until the histogram showed the heavy lobe had never moved.

Nothing detected it because a zero is a plausible number. The oracles were all green; the metric was
simply describing something other than what its name claimed.

THE RULE. If the work is gated, the bracket opens INSIDE the gate, and the metric is Cadence::Call --
because the act being timed is the call, not the frame that may or may not contain one.

THE SECOND RULE, gpu_pass_keys (R14). A pass key is registered by GlGpuTimer::begin on its first call --
which cannot register a pass whose arm never runs, because begin() is never reached for it. Measured across
27 matrix legs: three different registered key sets out of one binary, depending on which mutually exclusive
compose arm executed. Each key is therefore ALSO registered eagerly at namespace scope in the file that
begins it, and this gate asserts the two sets are equal IN BOTH DIRECTIONS.

Both directions, because each has its own failure. A key begun with no eager registration is the R14 defect
returning. A key registered with no begin is its mirror: a constant that outlived its pass, reporting
present-at-zero forever -- "this pass exists and cost nothing" -- which is harder to notice than an absence,
for the same reason the straddle above went unnoticed. R14's fix note demanded exactly this pair.

SCOPE OF THE SECOND RULE: keys ending `.gpu_us`, which is precisely the set GlGpuTimer brackets.
render.frame.gpu_span_us is deliberately outside it -- a GL_TIMESTAMP span read directly rather than a
GpuTimer bracket, so it has no begin() to pair with.

Usage:
    gputimer-brackets.py --check         # bracket rule: scan source/, exit 1 on any violation
    gputimer-brackets.py --selftest      # prove the straddle detector fires, and does not over-fire
    gputimer-brackets.py --check-keys    # key rule: begun set == eagerly-registered set, both ways
    gputimer-brackets.py --selftest-keys # prove the bijection detector fires four ways
"""
import re
import sys
import pathlib

# EITHER SPELLING. These were `gpuTimer\(\)\.` alone, so the two sites reaching the timer through the
# renderer's own member -- render.frame.clear.gpu_us and render.frame.blit.gpu_us in
# StarRenderer_opengl.cpp -- were scanned by neither rule. 13 of 15 sites, and the gate printed OK.
BEGIN = re.compile(r'(?:gpuTimer\(\)|m_gpuTimer)\.begin\(\s*"([^"]+)"')
END = re.compile(r'(?:gpuTimer\(\)|m_gpuTimer)\.end\(\s*"([^"]+)"')
# The eager registration: a LITERAL key handed to Telemetry::timer. Deliberately not matched against a
# variable, because a variable is what begin() already passes and is exactly what cannot be checked here.
# The closing quote is load-bearing: it stops "<key>.gpu_us.nested" matching as if it were a pass key.
EAGER = re.compile(r'Telemetry::timer\(\s*"([^"]*\.gpu_us)"')

# THE PAIR (R15). Every pass key has a <key>.nested counter recording samples the nesting guard rejected,
# and it carried the identical absent-vs-zero defect: registered inside begin(), so for a pass whose arm
# never runs "no samples were rejected" and "nothing was watching" were the same reading.
#
# The EXPECTATION IS DERIVED, not declared. The gate appends ".nested" to each begun key rather than
# reading a second list, so there is nothing here for a future key to be forgotten from -- which is the
# hazard R14's fix note warned about when it said "do not simply duplicate the list".
EAGER_NESTED = re.compile(r'Telemetry::counter\(\s*"([^"]*\.gpu_us\.nested)"')
NESTED_SUFFIX = ".nested"


def strip_comment(line):
    """Good enough for this corpus: no string literal here contains '//'."""
    i = line.find("//")
    return (line[:i] if i >= 0 else line).rstrip()


def violations(text, path="<mem>"):
    """Report every begin/end region whose entire body sits inside one conditional.

    Brace-counted rather than indentation-matched: indentation is a style, braces are the language.
    """
    out = []
    lines = text.splitlines()
    open_at = {}
    for n, raw in enumerate(lines):
        line = strip_comment(raw)
        mb = BEGIN.search(line)
        if mb:
            open_at[mb.group(1)] = n
            continue
        me = END.search(line)
        if not me:
            continue
        key = me.group(1)
        start = open_at.pop(key, None)
        if start is None:
            continue

        # Body = between the begin statement and the end statement. The begin's MetricDesc argument
        # usually continues onto the next line, so skip forward to the statement terminator.
        i = start
        while i < n and ";" not in strip_comment(lines[i]):
            i += 1
        body = [(j, strip_comment(lines[j])) for j in range(i + 1, n)]
        body = [(j, s) for j, s in body if s.strip()]
        if not body:
            continue

        first_no, first = body[0]
        if not re.match(r'^\s*(if|else\s+if)\s*\(', first):
            continue

        # Does that conditional enclose the WHOLE body, AND is there a path through it that does
        # nothing? A bare `if` has an empty else-path, so a skip iteration records a ~0us sample --
        # that is the defect. An `if/else` chain terminating in `else` does work on every path and
        # is fine (render.pass.parallax.compose.gpu_us is exactly that, and tripped an earlier
        # version of this rule). Depth-counted, so only a TOP-LEVEL else counts.
        depth = 0
        closed_at = None
        for j, s in body:
            depth += s.count("{") - s.count("}")
            # `} else {` nets zero braces, so depth never returns to 0 there; the chain closes only
            # at its final brace. (An earlier version counted on depth hitting 0 at the else and so
            # never saw one -- it flagged every if/else in the tree.)
            if depth <= 0 and j > first_no:
                closed_at = j
                break
        if closed_at is None or closed_at != body[-1][0]:
            continue
        # The chain spans the whole body. It leaves an EMPTY path -- the defect -- unless it ends in
        # a bare `else`. `else if` is not a terminator: it still falls through to nothing.
        chain = "\n".join(s for _, s in body)
        if re.search(r'\belse\b\s*(\{|$)', chain, re.M):
            continue
        out.append((path, first_no + 1, key))
    return out


CLEAN = '''
void P::draw() {
  if (refresh) {
    m_renderer->gpuTimer().begin("render.pass.x.gpu_us",
      MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Budget});
    drawIt();
    m_renderer->gpuTimer().end("render.pass.x.gpu_us");
  }
}
'''

DIRTY = '''
void P::draw() {
  m_renderer->gpuTimer().begin("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
  if (refresh) {
    drawIt();
  }
  m_renderer->gpuTimer().end("render.pass.x.gpu_us");
}
'''

# Ungated work that merely CONTAINS a conditional must not trip the detector -- that is the
# over-fire arm, and it is the one that would quietly erode the gate into noise.
PARTIAL = '''
void P::draw() {
  m_renderer->gpuTimer().begin("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Budget});
  if (special)
    drawSpecial();
  drawAlways();
  m_renderer->gpuTimer().end("render.pass.x.gpu_us");
}
'''


# Mutually exclusive arms that BOTH draw: no path records an empty bracket, so this is correct code.
# It is in the tree (render.pass.parallax.compose.gpu_us) and tripped the first version of this rule,
# which is why the arm is pinned here rather than left to be rediscovered.
IFELSE = '''
void P::draw() {
  m_renderer->gpuTimer().begin("render.pass.x.compose.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Budget});
  if (merged) {
    mergedCompose(size);
  } else {
    standardCompose(size);
  }
  m_renderer->gpuTimer().end("render.pass.x.compose.gpu_us");
}
'''


def selftest():
    fails = []
    if not violations(DIRTY, "DIRTY"):
        fails.append("detector did NOT fire on a bracket straddling a gate")
    if violations(CLEAN, "CLEAN"):
        fails.append("detector over-fired on a correctly nested bracket")
    if violations(PARTIAL, "PARTIAL"):
        fails.append("detector over-fired on ungated work that merely contains a conditional")
    if violations(IFELSE, "IFELSE"):
        fails.append("detector over-fired on an if/else that does work on every path")
    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  gputimer_brackets selftest: 4/4 arms ok (fires once, declines to fire three ways)")
    return 0


def sources():
    """Every scanned .cpp as {repo-relative path: text}. One definition, both rules."""
    root = pathlib.Path(__file__).resolve().parent.parent / "source"
    return {str(p.relative_to(root.parent)): p.read_text(encoding="utf-8", errors="replace")
            for p in sorted(root.rglob("*.cpp"))}


def check():
    found = []
    begun = 0
    for path, text in sources().items():
        found += violations(text, path)
        begun += len(BEGIN.findall("\n".join(strip_comment(l) for l in text.splitlines())))
    for path, line, key in found:
        print(f"  {path}:{line}: GPU timer '{key}' brackets a gate it does not enter -- "
              f"skip iterations record ~0us samples. Open the bracket inside the conditional "
              f"and declare MetricCadence::Call.")
    if found:
        print(f"gputimer_brackets: FAIL -- {len(found)} straddled bracket(s) of {begun} scanned")
        return 1
    # THE DENOMINATOR IS PART OF THE VERDICT. This printed OK on whatever it happened to match, so a
    # refactor that changed the call spelling would have retired the gate silently while it went on
    # reporting a pass -- which is how the wrong word passed a screaming oracle (#223). An empty corpus
    # is now a FAILURE, not a vacuous success.
    if begun == 0:
        print("gputimer_brackets: FAIL -- scanned 0 begin sites. The detector matched nothing, so it "
              "verified nothing; the call spelling has moved out from under BEGIN.")
        return 1
    print(f"gputimer_brackets: OK -- {begun} begin site(s) scanned, none brackets a gate it does not enter")
    return 0


def key_problems(files):
    """Every key whose begun-set and eagerly-registered-set membership disagree.

    `files` is {path: text}, so the same function serves the tree and the selftest fixtures.
    """
    begun, eager, nested = {}, {}, {}
    for path, text in files.items():
        body = "\n".join(strip_comment(l) for l in text.splitlines())
        for k in BEGIN.findall(body):
            begun.setdefault(k, set()).add(path)
        for k in EAGER.findall(body):
            eager.setdefault(k, set()).add(path)
        for k in EAGER_NESTED.findall(body):
            nested.setdefault(k, set()).add(path)
    out = []
    for k in sorted(set(begun) - set(eager)):
        out.append(f"  {k}: begun at {', '.join(sorted(begun[k]))} but NEVER REGISTERED EAGERLY -- an arm "
                   f"that does not run leaves this key absent, not zero (R14). Add a namespace-scope "
                   f'Telemetry::timer("{k}", ...) to that file.')
    for k in sorted(set(eager) - set(begun)):
        out.append(f"  {k}: registered eagerly at {', '.join(sorted(eager[k]))} but NEVER BEGUN -- a "
                   f"constant that outlived its pass. It will report present-at-zero forever, which reads "
                   f"as 'this pass exists and cost nothing'. Delete the registration.")
    # THE .nested PAIR, derived from the begun key rather than from a second list.
    for k in sorted(set(begun)):
        if k + NESTED_SUFFIX not in nested:
            out.append(f"  {k}{NESTED_SUFFIX}: the pass key is begun but its REJECTION COUNTER is not "
                       f"registered eagerly. For a pass whose arm never runs, 'no samples were rejected' "
                       f'and "nothing was watching" are then the same reading (R15).')
    for k in sorted(set(nested)):
        if k[:-len(NESTED_SUFFIX)] not in begun:
            out.append(f"  {k}: a rejection counter with no pass -- {k[:-len(NESTED_SUFFIX)]} is never "
                       f"begun anywhere, so this counter reports present-at-zero forever.")

    # SET EQUALITY, NOT CO-LOCATION. An earlier draft also required the registration to sit in the file
    # that begins the key. It read well and was wrong: render.pass.interface.gpu_us is begun in
    # StarClientApplication.cpp, an UPSTREAM file that client_residency ratchets precisely so our code
    # leaves it -- so the rule would have forced nine lines of ours deeper into the file we are trying to
    # vacate, and the only way to stay green would have been to raise that ceiling. A rule that pushes
    # code into a file another rule is pushing it out of is not a rule, it is a collision. The invariant
    # R14 needs is that the two SETS agree; where the registration lives is a judgement, made at the site.
    return out, len(begun), len(eager), len(nested)


def check_keys():
    problems, nb, ne, nn = key_problems(sources())
    for p in problems:
        print(p)
    if problems:
        print(f"gpu_pass_keys: FAIL -- {len(problems)} key(s) disagree ({nb} begun, {ne} registered, {nn} .nested)")
        return 1
    if nb == 0:
        print("gpu_pass_keys: FAIL -- scanned 0 begin sites, so the two sets are trivially equal and "
              "nothing was checked.")
        return 1
    print(f"gpu_pass_keys: OK -- {nb} key(s) begun, {ne} eagerly registered, {nn} rejection counters; all three sets agree in both directions")
    return 0


# Fixtures for the bijection detector. Deliberately spelled the way the tree spells it, so a change to
# the call shape breaks these arms rather than quietly retiring the rule.
KEYS_MATCHED = {"m.cpp": '''
namespace {
  auto s_xTimer = Telemetry::timer("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
  auto s_xNested = Telemetry::counter("render.pass.x.gpu_us.nested",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
}
void P::draw() {
  m_renderer->gpuTimer().begin("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
  drawIt();
  m_renderer->gpuTimer().end("render.pass.x.gpu_us");
}
'''}

KEYS_BEGUN_ONLY = {"m.cpp": '''
void P::draw() {
  m_renderer->gpuTimer().begin("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
  drawIt();
  m_renderer->gpuTimer().end("render.pass.x.gpu_us");
}
'''}

KEYS_EAGER_ONLY = {"m.cpp": '''
namespace {
  auto s_goneTimer = Telemetry::timer("render.pass.gone.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
}
'''}

# The member spelling, which neither rule matched before R14. If this arm stops firing, the two
# StarRenderer_opengl.cpp sites have fallen out of scope again.
KEYS_MEMBER_SPELLING = {"m.cpp": '''
void R::startFrame() {
  m_gpuTimer.begin("render.frame.clear.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Frame, MetricRole::Detail});
  clearIt();
  m_gpuTimer.end("render.frame.clear.gpu_us");
}
'''}


# A pass key registered eagerly but with NO rejection counter -- R15's defect, one level down.
KEYS_NO_NESTED = {"m.cpp": '''
namespace {
  auto s_xTimer = Telemetry::timer("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
}
void P::draw() {
  m_renderer->gpuTimer().begin("render.pass.x.gpu_us",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
  drawIt();
  m_renderer->gpuTimer().end("render.pass.x.gpu_us");
}
'''}

# A rejection counter whose pass no longer exists -- the mirror, reporting present-at-zero forever.
KEYS_ORPHAN_NESTED = {"m.cpp": '''
namespace {
  auto s_goneNested = Telemetry::counter("render.pass.gone.gpu_us.nested",
    MetricDesc{MetricDomain::Gpu, MetricOwner::Gl, MetricCadence::Call, MetricRole::Detail});
}
'''}


def selftest_keys():
    fails = []
    if key_problems(KEYS_MATCHED)[0]:
        fails.append("bijection detector fired on a key that is both begun and registered")
    if not key_problems(KEYS_BEGUN_ONLY)[0]:
        fails.append("detector did NOT fire on a key begun with no eager registration (the R14 defect)")
    if not key_problems(KEYS_EAGER_ONLY)[0]:
        fails.append("detector did NOT fire on a registration that outlived its pass")
    if not key_problems(KEYS_MEMBER_SPELLING)[0]:
        fails.append("detector did NOT see m_gpuTimer.begin -- the two renderer-member sites are unscanned")
    if not any("REJECTION COUNTER" in p for p in key_problems(KEYS_NO_NESTED)[0]):
        fails.append("detector did NOT fire on a pass key with no .nested rejection counter (R15)")
    if not any("no pass" in p for p in key_problems(KEYS_ORPHAN_NESTED)[0]):
        fails.append("detector did NOT fire on a .nested counter whose pass does not exist")
    # An empty corpus must FAIL, not pass vacuously. Without this arm the gate is one refactor away from
    # the silent-vacuous green it exists to replace.
    if key_problems({})[1] != 0:
        fails.append("an empty corpus did not read as zero begin sites")
    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  gpu_pass_keys selftest: 7/7 arms ok (fires five ways incl. both .nested directions, "
          "declines once, counts an empty corpus)")
    return 0


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sys.exit({"--selftest": selftest, "--check-keys": check_keys,
              "--selftest-keys": selftest_keys}.get(arg, check)())
