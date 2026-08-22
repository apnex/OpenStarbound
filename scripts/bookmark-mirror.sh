#!/usr/bin/env bash
# Does harness/logs-perf/bookmarks.txt still describe the save it claims to mirror?
#
#   exit 0   every mirrored name is present in the player save
#   exit 1   at least one is not -- the mirror has drifted
#   exit 77  SKIP: no player save here (a fresh checkout). NOTHING WAS COMPARED.
#
# WHY THIS EXISTS. render-profile.sh's --warp pre-flight reads that file and refuses in milliseconds
# when the name is not in it, quoting "the real list". It was not the real list: nothing in this tree
# ever wrote it. It is a hand-maintained copy of state that lives in the save, and on 2026-08-22 it
# drifted -- the bookmarks were renamed to hyphens, the save was correct, and the pre-flight rejected
# every renamed scene while reporting an authoritative-looking list of names that no longer existed.
# A cached answer with no writer is the same defect as a claim with no instrument; the pre-flight was
# not consulting the bookmarks, it was consulting a memory of them.
#
# WHAT THIS DOES AND DOES NOT PROVE. It checks CORRESPONDENCE in one direction: every name the mirror
# offers really occurs in the save. That is the direction the pre-flight can get WRONG in a way that
# costs a run -- offering a name the game will reject, or (worse) rejecting a name the game accepts.
# It deliberately does NOT check the other direction: the save legitimately holds bookmarks nobody has
# added to the mirror, and failing on those would make the check unpassable, which is worth no more
# than one that cannot fail.
#
# IT DOES NOT PARSE SBVJ01. The save is a versioned-JSON binary and a parser here would be a second
# implementation of the format to keep correct. A bookmark name is stored as literal bytes, so a
# substring search answers the question that matters without pretending to understand the container.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2

EXIT_SKIP=77
MIRROR=${BOOKMARK_MIRROR:-harness/logs-perf/bookmarks.txt}
SAVEDIR=${BOOKMARK_SAVEDIR:-harness/storage-perf/player}

check() {
  local mirror="$1" savedir="$2" save missing=0 total=0
  save=$(ls -1 "$savedir"/*.player 2>/dev/null | head -1)
  # ABSENT IS NOT ZERO. harness/ is untracked, so CI and any fresh clone have no save at all. That is
  # not a passing mirror and it is not a failing one -- it is a comparison that did not happen, and it
  # has to be spelled differently from both.
  [ -n "$save" ] && [ -s "$mirror" ] || return $EXIT_SKIP
  while IFS= read -r name; do
    [ -z "$name" ] && continue
    total=$((total + 1))
    grep -qaF -- "$name" "$save" || { echo "  DRIFTED: mirror offers '$name', which is not in the save"; missing=$((missing + 1)); }
  done < "$mirror"
  [ "$total" -gt 0 ] || return $EXIT_SKIP
  if [ "$missing" -gt 0 ]; then
    echo "bookmark-mirror: FAIL -- $missing of $total mirrored name(s) are not in $(basename "$save")."
    echo "  The --warp pre-flight will offer names the game does not have. Re-export the mirror."
    return 1
  fi
  echo "bookmark-mirror: OK -- all $total mirrored name(s) present in $(basename "$save")"
  return 0
}

if [ "${1:-}" = "--selftest" ]; then
  fails=0; arms=0
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  arm() { arms=$((arms+1)); if [ "$2" -eq "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1 (expected rc $2, got $3)"; fails=1; fi; }

  mkdir -p "$tmp/save"
  printf 'Desert-Town\x00\x0501-Lava-Refinery' > "$tmp/save/x.player"

  printf 'Desert-Town\n' > "$tmp/ok.txt"
  check "$tmp/ok.txt" "$tmp/save" >/dev/null 2>&1; arm "a mirror that matches the save passes" 0 $?

  # THE ARM THAT MATTERS: this is the exact 2026-08-22 failure -- a name that reads perfectly well and
  # is simply not what the save calls it.
  printf 'Desert Town\n' > "$tmp/drift.txt"
  check "$tmp/drift.txt" "$tmp/save" >/dev/null 2>&1; arm "a space-vs-hyphen drift is caught" 1 $?

  # A binary save must not defeat the search. If grep treated the file as unreadable the check would
  # report every name missing, which fails loudly -- but it would fail for the wrong reason.
  printf '01-Lava-Refinery\n' > "$tmp/bin.txt"
  check "$tmp/bin.txt" "$tmp/save" >/dev/null 2>&1; arm "names are found inside a BINARY save" 0 $?

  check "$tmp/ok.txt" "$tmp/nosuchdir" >/dev/null 2>&1; arm "no save present SKIPs (77), does not pass" $EXIT_SKIP $?

  : > "$tmp/empty.txt"
  check "$tmp/empty.txt" "$tmp/save" >/dev/null 2>&1; arm "an empty mirror SKIPs rather than passing vacuously" $EXIT_SKIP $?

  [ $fails -eq 0 ] || { echo "bookmark-mirror selftest: FAILED"; exit 1; }
  echo "bookmark-mirror selftest: $arms/$arms arms ok -- match, drift, binary, no-save, empty"
  exit 0
fi

check "$MIRROR" "$SAVEDIR"
rc=$?
[ $rc -eq $EXIT_SKIP ] && echo "bookmark-mirror: SKIP -- no player save under $SAVEDIR. Nothing was compared."
exit $rc
