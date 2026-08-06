#!/usr/bin/env bash
# REFUSE TO MEASURE A BINARY THAT DOES NOT CONTAIN THE SOURCES ON DISK.
#
# A failed build leaves the previous binary untouched and exits non-zero somewhere the caller may not
# be watching. Everything measured afterwards is then a fact about the PREVIOUS binary, wearing the
# label of the current one -- and nothing in a profile can tell you that happened.
#
# render-gate.sh has refused to certify on this basis since #178. render-profile.sh and
# lever-matrix.sh did NOT, and they are the two that produce the numbers a campaign quotes: the gate
# proves a refactor is byte-identical, the profiler says what things cost. The check lived in the
# instrument with the least to lose by omitting it.
#
# ONE DECLARATION, THREE CONSUMERS. Extracted here rather than pasted a third time, for the reason
# #185 and the --from-cmake ratchets already record: a rule restated in n places is a rule that can
# drift in n-1 of them.
#
#   assert-binary-fresh.sh <binary>      # exit 1 and name the offending sources if stale
#   assert-binary-fresh.sh --selftest
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ "${1:-}" = "--selftest" ]; then
  fails=0
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  # A binary NEWER than every source passes.
  mkdir -p "$tmp/source"; echo 'int main(){}' > "$tmp/source/a.cpp"
  sleep 0.05; : > "$tmp/bin"; chmod +x "$tmp/bin"
  ( cd "$tmp" && find source -type f -name '*.cpp' -newer bin -print | grep -q . ) \
    && { echo "  FAIL a fresh binary was reported stale"; fails=1; } \
    || echo "  ok   a binary newer than its sources passes"
  # ...and one OLDER than a source is caught. This is the arm that matters: without it the check
  # could be a no-op and every run would look clean.
  sleep 0.05; touch "$tmp/source/a.cpp"
  ( cd "$tmp" && find source -type f -name '*.cpp' -newer bin -print | grep -q . ) \
    && echo "  ok   a source newer than the binary is caught" \
    || { echo "  FAIL a stale binary was NOT caught"; fails=1; }
  [ $fails -eq 0 ] || { echo "assert-binary-fresh selftest: FAILED"; exit 1; }
  echo "assert-binary-fresh selftest: 2/2 arms ok -- fresh passes, stale is caught"
  exit 0
fi

BIN=${1:?usage: assert-binary-fresh.sh <binary>}
[ -x "$BIN" ] || { echo "REFUSING TO MEASURE: no executable at $BIN." >&2; exit 1; }

# dist/ IS the CMake runtime output directory (source/CMakeLists.txt), so the binary's mtime is its
# LINK time, not a copy's. source/test is pruned: it builds the test binaries, not the game.
stale=$(find source -path source/test -prune -o -type f \
          \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name 'CMakeLists.txt' \) \
          -newer "$BIN" -print 2>/dev/null | sort | head -20)
if [ -n "$stale" ]; then
  echo "REFUSING TO MEASURE: these sources are newer than $BIN -- the build never ran, or it FAILED:" >&2
  echo "$stale" | sed 's/^/    /' >&2
  echo "  Rebuild, CONFIRM THE LINK SUCCEEDED, then re-run. A failed build leaves the binary" >&2
  echo "  untouched, so every number produced from here would describe the PREVIOUS binary." >&2
  exit 1
fi
