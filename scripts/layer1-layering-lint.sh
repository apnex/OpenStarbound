#!/bin/bash
# LAYER-1 LAYERING LINT.
#
# The render-surface module (StarGlRenderSurface.{hpp,cpp}, StarGlTexturePrimitives.{hpp,cpp}) is a SOVEREIGN
# module with a real OUTSIDE: it must not name OpenGlRenderer in CODE. Comments are allowed -- they narrate the
# history ("lifted out of OpenGlRenderer", "once carried friend class OpenGlRenderer"). A code reference (a
# forward-decl `class OpenGlRenderer;`, an `OpenGlRenderer::` use, a `friend class OpenGlRenderer`) would reopen
# the reach-into-the-renderer coupling the extraction closed.
#
# This converts the comparative assessment's "zero code references to OpenGlRenderer" snapshot (C-IV-1) into a
# build-time GUARANTEE (backlog item 3). Registered as the `layer1_layering` ctest; fails the build if a code
# reference reappears. The check strips // line-comments, /* */ block-comments, and "string literals" before
# matching, so only genuine code triggers it.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2

FILES=(
  source/application/StarGlRenderSurface.hpp
  source/application/StarGlRenderSurface.cpp
  source/application/StarGlTexturePrimitives.hpp
  source/application/StarGlTexturePrimitives.cpp
)
# Allow an explicit file list override (used by tests of the lint itself).
[ "$#" -gt 0 ] && FILES=("$@")

python3 - "OpenGlRenderer" "${FILES[@]}" <<'PY'
import sys
needle = sys.argv[1]
files = sys.argv[2:]
violations = []
for path in files:
    try:
        src = open(path).read()
    except OSError as e:
        print("layer1-layering: cannot read %s (%s)" % (path, e))
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
    for lineno, line in enumerate(code.split("\n"), 1):
        if needle in line:
            violations.append((path, lineno, line.strip()))

if violations:
    print("LAYER-1 LAYERING VIOLATION: the sovereign render-surface module must not name %s in code." % needle)
    print("(Comments are fine; this caught a real code reference. Route through the Renderer interface instead.)")
    for path, lineno, text in violations:
        print("  %s:%d: %s" % (path, lineno, text))
    sys.exit(1)

print("layer1-layering: OK -- %d module files name %s only in comments." % (len(files), needle))
PY
