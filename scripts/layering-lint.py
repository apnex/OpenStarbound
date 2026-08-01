#!/usr/bin/env python3
"""THE LAYERING LINT. One instrument, two registrations.

It answers a single question: does a set of files name a forbidden symbol IN CODE, more often than an
agreed ceiling? Comments are allowed and deliberately so -- they narrate history ("lifted out of
OpenGlRenderer") and prose about a symbol is not coupling to it. // line-comments, /* */ blocks and
"string literals" are blanked (newlines preserved, so line numbers stay exact) before matching.

TWO CALLERS TODAY:

  layer1_layering   needle OpenGlRenderer, ceiling 0, over the sovereign L1 render-surface module.
                    A code reference -- a forward decl, an OpenGlRenderer:: use, a friend declaration --
                    would reopen the reach-into-the-renderer coupling the extraction closed.

  render_layering   needle Root::singleton, per-file ceilings, over the L3 passes. THIS ONE RATCHETS
                    RATHER THAN FORBIDS, and that is a deliberate design choice, not a compromise.

WHY A RATCHET AND NOT ZERO. The L3 Air-Gap residual has been GROWING: the 8th singleton read in
BackdropPass was added by a CORRECT bug fix (#177, the env cache had no motion term and the Director
saw choppy stars in flight). A zero-tolerance gate would have scored that fix as a violation and
invited someone to route around it. A ceiling set at today's counts cannot stop the debt existing, but
it stops the debt GROWING SILENTLY -- the next author who needs a ninth read must edit a number in
source/test/CMakeLists.txt, which is exactly the moment to ask whether the value belongs in a params
struct instead. Lower the ceilings as the pay-down lands; never raise one without saying why.

WHY PYTHON AND NOT BASH, AND IT COST A RED CI TO LEARN (#192). This was layering-lint.sh, and on
windows-latest ctest reported it `***Not Run` -- every run, silently guarding nothing, while the label
and the preset filter both said it was covered. ctest starts a test with CreateProcess, which honours
neither a `#!` line nor PATHEXT, so a .sh (and equally a .py) named directly as COMMAND cannot start at
all. The fix is not only this port: the CMake registration must name the INTERPRETER explicitly. Never
rely on the shebang for anything ctest runs.

USAGE
  layering-lint.py                                  # L1 defaults: OpenGlRenderer, ceiling 0
  layering-lint.py --needle SYM  path[=MAX] ...     # MAX defaults to 0
  layering-lint.py --needle SYM  --from-cmake       # read the ceilings from the ctest registration
"""

import os
import re
import sys

# Self-locating: the lint reads repo-relative paths, so it must run from the repo root no matter where
# ctest invoked it from (ctest's WORKING_DIRECTORY for these is the build dir).
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_NEEDLE = "OpenGlRenderer"
DEFAULT_SPECS = [
    "source/application/StarGlRenderSurface.hpp",
    "source/application/StarGlRenderSurface.cpp",
    "source/application/StarGlTexturePrimitives.hpp",
    "source/application/StarGlTexturePrimitives.cpp",
    # Added 2026-07-26 alongside the layer-table claim. These two declare the abstract types the GL
    # backend IMPLEMENTS -- TextureAtlasSet, GpuTimer, RenderOracle -- so they are on the sovereign side
    # of the fence by construction, and holding them to it costs nothing today while making a future
    # back-reference a build failure rather than a review comment. StarRenderDiagnostics.hpp names
    # OpenGlRenderer exactly once, in a comment explaining what it deliberately does NOT depend on; the
    # stripper below removes it, which is the entire reason this lint strips before it greps.
    "source/application/StarTextureAtlas.hpp",
    "source/application/StarRenderDiagnostics.hpp",
]


def strip_comments_and_strings(src):
    """Return a code-only copy with comments and string literals blanked, newlines preserved.

    Line numbers stay exact so the failure report can point at the real site."""
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
    return "".join(out)


# THE RATCHET HAS ONE HOME: the render_layering registration in source/test/CMakeLists.txt.
#
# --from-cmake exists so a second caller can run this gate without RESTATING the ceilings. The GitHub
# gates workflow is that caller: it runs the four script gates on every push with no paths filter, because
# build.yml's filter excludes the very trees these gates police. Had that workflow pasted the numbers, a
# lowered ceiling in CMakeLists would have left a stale copy quietly passing -- the same one-knob-two-
# declarations defect that #185 spent a day removing from the config. render-inventory.py already parses
# this registration for the same reason.
CEILING = re.compile(r"(source/rendering/\w+\.cpp)=(\d+)")


def ceilings_from_cmake():
    path = os.path.join(REPO, "source/test/CMakeLists.txt")
    try:
        with open(path, encoding="utf-8") as f:
            found = CEILING.findall(f.read())
    except OSError as e:
        print("layering-lint: --from-cmake cannot read source/test/CMakeLists.txt (%s)" % e)
        return None
    if not found:
        # Silently linting zero files would report OK and mean nothing.
        print("layering-lint: --from-cmake found no `source/rendering/*.cpp=N` ceilings in "
              "source/test/CMakeLists.txt -- the registration moved or was reshaped.")
        return None
    return ["%s=%s" % (f, n) for f, n in found]


def main(argv):
    needle = DEFAULT_NEEDLE
    args = list(argv)
    if args and args[0] == "--needle":
        if len(args) < 2:
            print("layering-lint: --needle requires a symbol")
            return 2
        needle = args[1]
        args = args[2:]

    # A needle prefixed `re:` is a regular expression rather than a substring. Added because
    # host_api_neutral was blind to its own table: it counted `SDL_GL_` while the seam-2 table three
    # paragraphs above it named "the context" and "the window creation flags" as GL couplings --
    # spelled `SDL_GLContext` and `SDL_WINDOW_OPENGL`, neither of which contains `SDL_GL_`. A single
    # coupling with three spellings cannot be expressed as one substring, and picking the loosest
    # common prefix would have swept in unrelated SDL. The two existing callers pass plain symbols
    # and are unaffected.
    if needle.startswith("re:"):
        try:
            rx = re.compile(needle[3:])
        except re.error as e:
            print("layering-lint: bad regex needle %r (%s)" % (needle[3:], e))
            return 2
        match = lambda line: rx.search(line) is not None
    else:
        match = lambda line: needle in line

    if args and args[0] == "--from-cmake":
        args = ceilings_from_cmake()
        if args is None:
            return 2
    raw_specs = args if args else DEFAULT_SPECS

    # Each spec is "path" (ceiling 0) or "path=MAX".
    specs = []
    for a in raw_specs:
        path, _, cap = a.partition("=")
        try:
            specs.append((path, int(cap) if cap else 0))
        except ValueError:
            print("layering-lint: bad ceiling in %r" % a)
            return 2

    counts, sites = {}, {}
    for path, _cap in specs:
        try:
            with open(os.path.join(REPO, path), encoding="utf-8") as f:
                src = f.read()
        except OSError as e:
            print("layering-lint: cannot read %s (%s)" % (path, e))
            return 2
        code = strip_comments_and_strings(src)
        hits = [(lineno, line.strip())
                for lineno, line in enumerate(code.split("\n"), 1) if match(line)]
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
        return 1

    total = sum(counts.values())
    caps = sum(cap for _, cap in specs)
    slack = caps - total
    print("layering-lint: OK -- %s appears %d time(s) in code across %d file(s), ceiling %d.%s"
          % (needle, total, len(specs), caps,
             ("  %d BELOW ceiling -- lower it." % slack) if slack > 0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
