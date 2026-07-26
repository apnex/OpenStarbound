#!/usr/bin/env python3
"""THE CONFIG DECLARATION LINT.

One question: does every key read through `Configuration::getOrDefault` actually have a declared default?

WHY THIS EXISTS (#185). Defaults are jsonMerge'd from three blocks -- BaseDefaultConfiguration in
StarRootLoader.cpp, AdditionalDefaultConfiguration in StarClientApplication.cpp, and bootconfig's -- and
before this lint a hand-typed literal at the call site acted as a fourth, unwritten one. Five render keys
had a shipped default and a call-site literal that DISAGREED, and `newLighting` was read, written and
toggled in the graphics menu while being declared in none of the three.

`getOrDefault` exists to make the declared value the only fallback. Its callers therefore consume the key
with a plain .toBool()/.toFloat()/.toUInt() and no second literal -- which is the point, and which is also
why an undeclared key must be a build-time failure rather than a runtime throw. That is this lint.

It deliberately does NOT police `get(key, default)`. Plenty of callers genuinely mean "absent is a state I
handle"; that form keeps working and stays unchecked. The rule is narrow and total: if you asked for the
DECLARED default, a declaration must exist.

USAGE
  config-lint.py            # scans source/, exits 1 on any undeclared getOrDefault key
"""

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Each entry is (file, name of the C++ Json constant holding a raw-JSON default block).
DEFAULT_BLOCKS = [
    ("source/game/StarRootLoader.cpp", "BaseDefaultConfiguration"),
    ("source/client/StarClientApplication.cpp", "AdditionalDefaultConfiguration"),
]

READ = re.compile(r'getOrDefault(Path)?\(\s*"([^"]+)"\s*\)')


def strip_line_comments(text):
    """Star's JSON parser accepts // comments and the default blocks use them; json.loads does not.

    String-aware, so a "http://..." style value is never mistaken for a comment."""
    out, in_string, escaped = [], False, False
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if in_string:
            out.append(c)
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                in_string = False
            i += 1
            continue
        if c == '"':
            in_string = True
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def declared_keys():
    """Union of the top-level keys in each default block.

    A block is not one raw string: BaseDefaultConfiguration is spliced from several R"JSON(...)JSON"
    fragments around a #ifdef STAR_SYSTEM_WINDOWS. Concatenating every fragment in the assignment yields
    the union of both platform branches -- which is what a declaration check wants, since a key declared
    on one platform is still declared."""
    keys = {}
    for path, const in DEFAULT_BLOCKS:
        full = os.path.join(REPO, path)
        try:
            src = open(full, encoding="utf-8").read()
        except OSError as e:
            print("config-lint: cannot read %s (%s)" % (path, e))
            sys.exit(2)
        opener = "Json const %s = Json::parseJson(" % const
        start = src.find(opener)
        if start < 0:
            print("config-lint: could not locate %s in %s -- the block was renamed or reshaped, and this "
                  "lint would otherwise pass with a silently smaller key set. Fix DEFAULT_BLOCKS."
                  % (const, path))
            sys.exit(2)
        end = src.find(')JSON");', start)
        if end < 0:
            print("config-lint: %s in %s has no terminating )JSON\");" % (const, path))
            sys.exit(2)
        frags = re.findall(r'R"JSON\((.*?)\)JSON"', src[start:end + len(')JSON");')], re.S)
        if not frags:
            print("config-lint: %s in %s contains no raw-JSON fragments" % (const, path))
            sys.exit(2)
        try:
            block = json.loads(strip_line_comments("".join(frags)))
        except ValueError as e:
            print("config-lint: %s in %s is not parseable JSON after comment stripping (%s)"
                  % (const, path, e))
            sys.exit(2)
        for k in block:
            keys.setdefault(k, path)
    return keys


def main():
    declared = declared_keys()

    reads, undeclared = 0, []
    for dirpath, dirnames, filenames in os.walk(os.path.join(REPO, "source")):
        dirnames[:] = [d for d in dirnames if d != "gtest"]
        for fn in sorted(filenames):
            if not fn.endswith((".cpp", ".hpp")):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, REPO)
            try:
                src = open(full, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for lineno, line in enumerate(src.split("\n"), 1):
                if line.lstrip().startswith("//"):
                    continue
                for m in READ.finditer(line):
                    reads += 1
                    key = m.group(2).split(".")[0] if m.group(1) else m.group(2)
                    if key not in declared:
                        undeclared.append((rel, lineno, m.group(2)))

    if undeclared:
        print("UNDECLARED CONFIG KEY read through getOrDefault")
        print("getOrDefault promises the DECLARED default as the fallback, so its callers consume the value")
        print("with a bare .toBool()/.toFloat()/.toUInt() and no second literal. With no declaration there is")
        print("nothing to fall back to: the read returns a null Json and the conversion throws at runtime.")
        print("Declare the key in one of:")
        for path, const in DEFAULT_BLOCKS:
            print("      %s (%s)" % (path, const))
        print("or use get(key, default) if 'absent' is a state you actually handle.")
        for rel, lineno, key in undeclared:
            print("  %s:%d: %s" % (rel, lineno, key))
        return 1

    print("config-lint: OK -- %d getOrDefault read(s), every key declared (%d declared keys across %d blocks)."
          % (reads, len(declared), len(DEFAULT_BLOCKS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
