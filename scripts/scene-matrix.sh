#!/usr/bin/env bash
# Run the LEVER x SCENE cartesian: scripts/lever-matrix.sh once per scene in scripts/scene-table.json.
#
#   scripts/scene-matrix.sh --repeats 3
#   scripts/scene-matrix.sh --repeats 3 --dry-run    # print the plan, launch nothing
#   scripts/scene-matrix.sh --selftest
#
# WHY A DRIVER AND NOT FIVE SHELL LINES. The five runs are the easy part; what a hand-typed loop keeps
# getting wrong is everything around them. A scene name is the join key between a measurement and its
# floor (scene-table.json _why_the_warp_string_is_the_identity), so the set of scenes has to come from
# the table rather than from whatever was typed that day -- one typo does not fail, it produces a leg
# tagged with a scene that has no floor, and leg-diff then refuses for a reason that looks like a
# missing measurement rather than a missing letter.
#
# NO SURGERY ON THE RUNNER. Each scene is an ordinary lever-matrix.sh invocation with its own run id,
# its own manifest, its own lock and its own baseline pair. The cartesian is the SET of those runs, not
# a new kind of run, so nothing here can change what a leg means.
#
# ONE SCENE'S FAILURE DOES NOT DISCARD THE OTHERS. A failing scene is recorded and the driver keeps
# going -- losing four good scenes because the fifth could not warp would be the expensive mistake --
# but the exit status is non-zero and the summary NAMES the failures. A partial cartesian that reports
# itself as complete is worse than one that stops.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2

TABLE=scripts/scene-table.json
FLOORS=scripts/scene-floor.json
REPEATS=3
SECONDS_PER_LEG=90
DRY=0

scenes_from_table() { python3 -c '
import json,sys
for s in json.load(open(sys.argv[1]))["scenes"]: print(s["warp"])
' "$TABLE"; }

# A scene with no floor still MEASURES fine; what it cannot do is be compared. Named at plan time so
# the operator learns it before four hours, not after -- absent is not zero, and the run is still worth
# banking, so this warns rather than refuses.
scenes_without_floor() { python3 -c '
import json,sys
t=json.load(open(sys.argv[1])); f=json.load(open(sys.argv[2]))
missing=[s["warp"] for s in t["scenes"] if s["warp"] not in f["scenes"]]
print(" ".join(missing))
' "$TABLE" "$FLOORS"; }

if [ "${1:-}" = "--selftest" ]; then
  fails=0; arms=0
  ok()  { arms=$((arms+1)); echo "  ok   $1"; }
  bad() { arms=$((arms+1)); echo "  FAIL $1"; fails=1; }

  n=$(scenes_from_table | grep -c .)
  [ "$n" -ge 2 ] && ok "the scene set is read from $TABLE ($n scenes)" \
                 || bad "read $n scenes from the table -- a cartesian needs at least 2"

  # EVERY SCENE MUST HAVE A FLOOR, or its legs are bankable but not quotable. This is the arm that
  # would have caught the space-vs-hyphen keying: both files parsed, both looked right, and two scenes
  # silently had no reachable floor.
  miss=$(scenes_without_floor)
  [ -z "$miss" ] && ok "every declared scene has a measured floor" \
                 || bad "no floor for:$miss -- comparisons there will refuse"

  # The plan must name every scene it intends to run. A driver that silently drops one produces a
  # cartesian with a hole and a summary that does not mention it.
  plan=$("$0" --repeats 2 --dry-run 2>&1)
  planned=0
  while read -r s; do echo "$plan" | grep -qF -- "$s" && planned=$((planned+1)); done < <(scenes_from_table)
  [ "$planned" -eq "$n" ] && ok "the plan names all $n scenes" \
                          || bad "the plan names $planned of $n scenes"

  # DERIVED, NEVER RESTATED -- the ninth instance of this defect in this tree was a hardcoded arm count
  # in lever-matrix.sh's own selftest banner.
  [ $fails -eq 0 ] || { echo "scene-matrix selftest: FAILED"; exit 1; }
  echo "scene-matrix selftest: $arms/$arms arms ok -- table read, floors present, plan complete"
  exit 0
fi

while [ $# -gt 0 ]; do
  case "$1" in
    --repeats) REPEATS="$2";         shift 2 ;;
    --seconds) SECONDS_PER_LEG="$2"; shift 2 ;;
    --dry-run) DRY=1;                shift   ;;
    *) echo "scene-matrix: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

mapfile -t SCENES < <(scenes_from_table)
[ ${#SCENES[@]} -gt 0 ] || { echo "scene-matrix: no scenes in $TABLE" >&2; exit 2; }

# A matrix already owns the mutable config; a second one would interleave its leg writes with ours and
# both runs would be measuring a config neither of them set.
if id=$(scripts/harness-active.sh 2>/dev/null); then
  echo "scene-matrix: REFUSING -- harness run '$id' already owns the config (scripts/harness-active.sh)." >&2
  echo "  If that run is dead, clear harness/matrix/.active by hand AFTER checking $FLOORS's config is baseline." >&2
  exit 2
fi

echo "=== scene-matrix: ${#SCENES[@]} scenes x lever matrix, ${REPEATS} repeat(s) each ==="
miss=$(scenes_without_floor)
[ -n "$miss" ] && echo "  !! NO MEASURED FLOOR for:$miss -- those legs will bank but cannot be quoted"
for s in "${SCENES[@]}"; do echo "  scene         $s"; done
echo "  seconds/leg   $SECONDS_PER_LEG"
echo

INDEX="harness/matrix/cartesian-index.tsv"
[ "$DRY" = 1 ] || { mkdir -p harness/matrix; : > "$INDEX"; }

failed=(); ran=0
for s in "${SCENES[@]}"; do
  echo "=== scene $((ran+1))/${#SCENES[@]}: $s ==="
  if [ "$DRY" = 1 ]; then
    echo "    scripts/lever-matrix.sh --warp '$s' --repeats $REPEATS --seconds $SECONDS_PER_LEG"
    ran=$((ran+1)); continue
  fi
  before=$(ls -1d harness/matrix/matrix-* 2>/dev/null | sort | tail -1)
  scripts/lever-matrix.sh --warp "$s" --repeats "$REPEATS" --seconds "$SECONDS_PER_LEG"; rc=$?
  after=$(ls -1d harness/matrix/matrix-* 2>/dev/null | sort | tail -1)
  # The run id is whatever directory the runner just created -- ASKED FOR, not predicted. Deriving it
  # from a timestamp we compute here would be a second clock, and a second clock is a second answer.
  [ "$after" != "$before" ] && printf '%s\t%s\t%s\n' "$s" "$(basename "$after")" "$rc" >> "$INDEX"
  [ $rc -eq 0 ] || { echo "  !! scene '$s' exited $rc"; failed+=("$s"); }
  ran=$((ran+1))
done

[ "$DRY" = 1 ] && exit 0
echo
echo "=== cartesian complete: $ran scene(s) run, index at $INDEX ==="
cat "$INDEX" 2>/dev/null
if [ ${#failed[@]} -gt 0 ]; then
  echo
  echo "scene-matrix: ${#failed[@]} of $ran scene(s) FAILED: ${failed[*]}"
  echo "  The other $((ran - ${#failed[@]})) are banked and usable. This is a PARTIAL cartesian -- say so when quoting it."
  exit 1
fi
echo "scene-matrix: all $ran scenes completed"
