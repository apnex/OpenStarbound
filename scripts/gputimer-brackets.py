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

Usage:
    gputimer-brackets.py --check      # scan source/, exit 1 on any violation
    gputimer-brackets.py --selftest   # prove the detector fires, and does not over-fire
"""
import re
import sys
import pathlib

BEGIN = re.compile(r'gpuTimer\(\)\.begin\(\s*"([^"]+)"')
END = re.compile(r'gpuTimer\(\)\.end\(\s*"([^"]+)"')


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


def check():
    root = pathlib.Path(__file__).resolve().parent.parent / "source"
    found = []
    for p in sorted(root.rglob("*.cpp")):
        found += violations(p.read_text(encoding="utf-8", errors="replace"),
                            str(p.relative_to(root.parent)))
    for path, line, key in found:
        print(f"  {path}:{line}: GPU timer '{key}' brackets a gate it does not enter -- "
              f"skip iterations record ~0us samples. Open the bracket inside the conditional "
              f"and declare MetricCadence::Call.")
    if found:
        print(f"gputimer_brackets: FAIL -- {len(found)} straddled bracket(s)")
        return 1
    print("gputimer_brackets: OK -- no GPU timer brackets a gate it does not enter")
    return 0


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "--check"
    sys.exit(selftest() if arg == "--selftest" else check())
