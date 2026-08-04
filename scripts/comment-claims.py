#!/usr/bin/env python3
"""comment_claims -- source comments may not cite a file by LINE NUMBER.

WHY THIS IS A BAN AND NOT A CHECKER. A checker would resolve each citation and report the stale ones.
That is buildable, and it is the wrong instrument: the population it would police was 9 sites, 100% of
them written by this project (upstream has never used the style), and 55% had ALREADY rotted -- 67%
counting only references into files we actively edit. Two of them, added together on 2026-07-25, were
correct when written and both were wrong within ten days. A line number is not a fact a comment can
hold: every edit above the target moves it, and nothing in the language, the build, or review connects
the two files. Discipline cannot fix that, so the form goes rather than the instances (#218).

Cite the SYMBOL instead -- a function, class, member, or enumerator. Those are greppable, they survive
edits, and when one is deleted the compiler usually takes the reference with it.

The one citation form that IS self-verifying stays legal, and is worth copying: a commit hash beside
its quoted subject, as at StarRenderer_opengl.hpp -- 'upstream (36a389c6, "Fix HDR crash (#535)")'.
That checks in one line with no false positives, because the hash carries its own text.

  scripts/comment-claims.py            # scan, print violations, exit 1 if any
  scripts/comment-claims.py --selftest # prove the detector fires and that the exemptions hold
"""

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# Our sources only. source/extern is vendored, and source/test/gtest is a vendored drop inside a tree we
# do own -- both are excluded because we do not write their comments and will not rewrite them.
SCAN_DIRS = [
    "source/core", "source/base", "source/game", "source/platform", "source/client",
    "source/server", "source/application", "source/rendering", "source/frontend", "source/test",
]
EXCLUDE_PARTS = ("extern", "gtest")
SUFFIXES = (".cpp", ".hpp", ".h")

# A citation is a filename with a source-ish suffix followed by ':' and a digit. Deliberately broad on
# the name so a path-qualified form (source/extern/fast_float.h:424) is caught too.
CITATION = re.compile(r"\b[A-Za-z0-9_./\\-]+\.(?:cpp|hpp|h|cc|cxx|inl)\s*:\s*\d")

# Comment extraction. Line comments only for the '//' case; block comments are handled by tracking the
# /* ... */ state, so a citation inside one is still caught. String literals are NOT excluded, which can
# only over-report -- and a file:line inside a string literal is a log message, which we also do not want.
LINE_COMMENT = re.compile(r"//(.*)$")


def comment_spans(text):
    """Yield (line_number, comment_text) for every comment in `text`."""
    in_block = False
    for lineno, raw in enumerate(text.splitlines(), start=1):
        rest = raw
        if in_block:
            end = rest.find("*/")
            if end < 0:
                yield lineno, rest
                continue
            yield lineno, rest[:end]
            rest = rest[end + 2:]
            in_block = False

        # Walk the remainder, alternating between code and block comments.
        while True:
            block = rest.find("/*")
            line = rest.find("//")
            if line >= 0 and (block < 0 or line < block):
                yield lineno, rest[line + 2:]
                break
            if block < 0:
                break
            end = rest.find("*/", block + 2)
            if end < 0:
                yield lineno, rest[block + 2:]
                in_block = True
                break
            yield lineno, rest[block + 2:end]
            rest = rest[end + 2:]


def scan_text(text):
    """Return [(lineno, comment_text)] for comments carrying a file:line citation."""
    hits = []
    for lineno, comment in comment_spans(text):
        if CITATION.search(comment):
            hits.append((lineno, comment.strip()))
    return hits


def source_files():
    for d in SCAN_DIRS:
        root = REPO / d
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in SUFFIXES:
                continue
            if any(part in EXCLUDE_PARTS for part in path.parts):
                continue
            yield path


def run_scan():
    violations = []
    scanned = 0
    for path in source_files():
        scanned += 1
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            print(f"comment_claims: cannot read {path}: {exc}", file=sys.stderr)
            return 2
        for lineno, comment in scan_text(text):
            violations.append((path.relative_to(REPO), lineno, comment))

    if not violations:
        print(f"comment_claims: OK -- 0 file:line citations in {scanned} source files")
        return 0

    print(f"comment_claims: FAIL -- {len(violations)} file:line citation(s) in comments\n")
    for rel, lineno, comment in violations:
        trimmed = comment if len(comment) <= 110 else comment[:107] + "..."
        print(f"  {rel}:{lineno}\n      {trimmed}")
    print(
        "\nA line number cannot stay true: every edit above the target moves it and nothing connects\n"
        "the two files. Cite the SYMBOL (function, class, member) instead -- see scripts/comment-claims.py\n"
        "for why this is a ban rather than a staleness checker (#218)."
    )
    return 1


def run_selftest():
    """Prove the detector fires on what it must, and stays silent on what it must not."""
    must_fire = [
        ("bare", "// see StarWorldClient.cpp:2167 for the arithmetic"),
        ("range", "// (StarRenderer_opengl.cpp:1050-1067)"),
        ("multi", "// cites StarWorldClient.cpp:61,574 as evidence"),
        ("slashes", "// see StarGpuLightmapPass.cpp:42/44 -- a runtime fallback"),
        ("pathed", "// same split as source/extern/fast_float.h:424-435"),
        ("spaced", "// StarEntityMap.cpp : 8"),
        ("block", "/* see StarSectorArray2D.hpp:94 for the contract */"),
        ("block_multiline", "/*\n * see StarTelemetry.hpp:14\n */"),
        ("trailing", "float x = 1.0f;  // mirrors StarEntityMap.cpp:8"),
        # A '*' continuation only counts once its opening /* has been seen -- a bare '* foo' line is
        # code, not a comment. Test the real shape rather than the fragment.
        ("doc_block", "/** Only caller is StarWorldServerThread.cpp:230. */"),
        ("code_then_comment", "int n = f();  /* see StarWorldClient.cpp:2167 */"),
    ]
    must_not_fire = [
        ("symbol", "// see OpenGlRenderer::GlGpuTimer for the readback"),
        ("filename_only", "// mirrors EntityMapSpatialHashSectorSize in StarEntityMap.cpp"),
        ("task_id", "// the adaptive border (#170) sizes the region"),
        ("commit", '// upstream (36a389c6, "Fix HDR crash (#535)")'),
        ("measurement", "// Measured 7.8 us/recompute."),
        ("version", "// requires GL 4.0: glMinSampleShading"),
        ("code_not_comment", 'String s = "StarWorldClient.cpp:2167";'),
        ("ratio", "// the requirement was 20-28 of 48"),
    ]

    failures = []
    for name, sample in must_fire:
        if not scan_text(sample):
            failures.append(f"MISSED  [{name}]: {sample!r}")
    for name, sample in must_not_fire:
        hits = scan_text(sample)
        if hits:
            failures.append(f"FALSE+  [{name}]: {sample!r} -> {hits}")

    total = len(must_fire) + len(must_not_fire)
    if failures:
        print(f"comment_claims --selftest: FAIL -- {len(failures)}/{total} cases wrong\n")
        for f in failures:
            print("  " + f)
        return 1
    print(f"comment_claims --selftest: OK -- {total} cases "
          f"({len(must_fire)} must-fire, {len(must_not_fire)} must-not-fire)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true",
                    help="run the detector against known-positive and known-negative samples")
    args = ap.parse_args()
    return run_selftest() if args.selftest else run_scan()


if __name__ == "__main__":
    sys.exit(main())
