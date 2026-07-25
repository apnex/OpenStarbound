#!/bin/bash
# THE LAYERING LINT. One instrument, two registrations.
#
# It answers a single question: does a set of files name a forbidden symbol IN CODE, more often than an
# agreed ceiling? Comments are allowed and deliberately so -- they narrate history ("lifted out of
# OpenGlRenderer") and prose about a symbol is not coupling to it. // line-comments, /* */ blocks and
# "string literals" are blanked (newlines preserved, so line numbers stay exact) before matching.
#
# TWO CALLERS TODAY:
#
#   layer1_layering   needle OpenGlRenderer, ceiling 0, over the sovereign L1 render-surface module.
#                     A code reference -- a forward decl, an OpenGlRenderer:: use, a friend declaration --
#                     would reopen the reach-into-the-renderer coupling the extraction closed.
#
#   render_layering   needle Root::singleton, per-file ceilings, over the L3 passes. THIS ONE RATCHETS
#                     RATHER THAN FORBIDS, and that is a deliberate design choice, not a compromise.
#
# WHY A RATCHET AND NOT ZERO. The L3 Air-Gap residual has been GROWING: the 8th singleton read in
# BackdropPass was added by a CORRECT bug fix (#177, the env cache had no motion term and the Director
# saw choppy stars in flight). A zero-tolerance gate would have scored that fix as a violation and
# invited someone to route around it. A ceiling set at today's counts cannot stop the debt existing, but
# it stops the debt GROWING SILENTLY -- the next author who needs a ninth read must edit a number in this
# file, which is exactly the moment to ask whether the value belongs in a params struct instead. Lower
# the ceilings as the pay-down lands; never raise one without saying why.
#
# USAGE
#   layering-lint.sh                                  # L1 defaults: OpenGlRenderer, ceiling 0
#   layering-lint.sh --needle SYM  path[=MAX] ...     # MAX defaults to 0
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2

NEEDLE="OpenGlRenderer"
SPECS=(
  source/application/StarGlRenderSurface.hpp
  source/application/StarGlRenderSurface.cpp
  source/application/StarGlTexturePrimitives.hpp
  source/application/StarGlTexturePrimitives.cpp
)

if [ "$#" -gt 0 ]; then
  if [ "$1" = "--needle" ]; then
    NEEDLE="$2"; shift 2
  fi
  [ "$#" -gt 0 ] && SPECS=("$@")
fi

python3 - "$NEEDLE" "${SPECS[@]}" <<'PY'
import sys
needle = sys.argv[1]
files = sys.argv[2:]
# Each spec is "path" (ceiling 0) or "path=MAX".
specs = []
for a in files:
    path, _, cap = a.partition("=")
    specs.append((path, int(cap) if cap else 0))

counts, sites = {}, {}
for path, cap in specs:
    try:
        src = open(path).read()
    except OSError as e:
        print("layering-lint: cannot read %s (%s)" % (path, e))
        sys.exit(2)
    # Emit a code-only copy with comments/strings blanked but newlines preserved, so line numbers are exact.
    out = []
    i, n, state = 0, len(src), "code"
    while i < n:
        c = src[i]
        d = src[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and d == "/":
                state = "line"; i += 2; continue
            if c == "/" and d == "*":
                state = "block"; i += 2; continue
            if c == '"':
                state = "string"; out.append(" "); i += 1; continue
            out.append(c); i += 1; continue
        if state == "line":
            if c == "\n":
                state = "code"; out.append("\n")
            i += 1; continue
        if state == "block":
            if c == "*" and d == "/":
                state = "code"; i += 2; continue
            if c == "\n":
                out.append("\n")
            i += 1; continue
        if state == "string":
            if c == "\\":
                i += 2; continue
            if c == '"':
                state = "code"; out.append(" "); i += 1; continue
            if c == "\n":
                out.append("\n")
            i += 1; continue
    code = "".join(out)
    hits = [(lineno, line.strip()) for lineno, line in enumerate(code.split("\n"), 1) if needle in line]
    counts[path] = len(hits)
    sites[path] = hits

over = [(path, cap) for path, cap in specs if counts[path] > cap]
if over:
    print("LAYERING CEILING EXCEEDED for %s" % needle)
    print("A ceiling is not a licence -- it is a ratchet. If this rose because the value genuinely has to")
    print("be read here, that is the moment to ask whether it belongs in a params struct resolved at the")
    print("boundary instead. If it must stand, raise the number in source/test/CMakeLists.txt and say why.")
    for path, cap in over:
        print("  %s: %d references, ceiling %d" % (path, counts[path], cap))
        for lineno, text in sites[path]:
            print("      %s:%d: %s" % (path, lineno, text))
    sys.exit(1)

total = sum(counts.values())
caps = sum(cap for _, cap in specs)
slack = caps - total
print("layering-lint: OK -- %s appears %d time(s) in code across %d file(s), ceiling %d.%s"
      % (needle, total, len(specs), caps,
         ("  %d BELOW ceiling -- lower it." % slack) if slack > 0 else ""))
PY
