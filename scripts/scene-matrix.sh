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

# A LEVER THAT DID NOT ENGAGE IS NOT A SCENE THAT FAILED. On 2026-08-22 this script printed
# "2 of 5 scenes FAILED" over a run in which ZERO legs failed: both non-zero exits were VOID legs --
# lever-matrix refusing to quote a lever whose witness did not move. At the ship those VOIDs were the
# CORRECT ANSWER, because render.cache.parallax.skipped reads 0 and 0 on a world with no parallax to
# skip, which is the property the ship was added to the fixture set to exercise. The instrument
# reported itself working and the driver called it a failure.
#
# Same distinction this project enforces everywhere else: ABSENT is not ZERO, SKIP is not PASS. Read
# from the manifest's per-leg verdict rather than from the exit code, because the exit code cannot
# carry the difference -- one integer, three outcomes.
scene_outcome() {
  python3 - "$1" <<'PY' 2>/dev/null || echo unknown
import json, sys, collections
try:
    legs = json.load(open(sys.argv[1] + "/manifest.json"))["legs"]
except Exception:
    print("unknown"); raise SystemExit
c = collections.Counter(l.get("verdict", "?") for l in legs)
print("failed" if c.get("FAILED") or c.get("?") else "void-only" if c.get("VOID") else "clean")
PY
}

# THE SWEEP SET IS NOT THE FIXTURE SET. A scene can be registered, floored and reachable without
# belonging in every cartesian: 04-Ocean-Factory exists for ONE lever's re-derivation ([#274]) and
# sweeping it would spend ~50 minutes of GPU on seven levers nobody is asking about there. Absent
# means true, so the five original fixtures need no field and adding one cannot silently enrol it.
scenes_from_table() { python3 -c '
import json,sys
for s in json.load(open(sys.argv[1]))["scenes"]:
    if s.get("inCartesian", True): print(s["warp"])
' "$TABLE"; }

# Every REGISTERED scene, swept or not -- the floor check reads this, because a fixture with no floor
# is unquotable whether or not the cartesian visits it.
all_scenes_from_table() { python3 -c '
import json,sys
for s in json.load(open(sys.argv[1]))["scenes"]: print(s["warp"])
' "$TABLE"; }

# A scene with no floor still MEASURES fine; what it cannot do is be compared. Named at plan time so
# the operator learns it before four hours, not after -- absent is not zero, and the run is still worth
# banking, so this warns rather than refuses.
scenes_without_floor() { python3 -c '
import json,sys
t=json.load(open(sys.argv[1])); f=json.load(open(sys.argv[2]))
only_swept = len(sys.argv) > 3 and sys.argv[3] == "swept"
missing=[s["warp"] for s in t["scenes"]
         if s["warp"] not in f["scenes"] and (s.get("inCartesian", True) or not only_swept)]
print(" ".join(missing))
' "$TABLE" "$FLOORS" "${1:-all}"; }

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
  # A SWEPT SCENE WITHOUT A FLOOR IS A FAILURE -- the run would bank legs nobody can compare. This is
  # the arm that would have caught the space-vs-hyphen keying: both files parsed, both looked right,
  # and two scenes silently had no reachable floor.
  miss=$(scenes_without_floor swept)
  [ -z "$miss" ] && ok "every SWEPT scene has a measured floor" \
                 || bad "no floor for swept scene(s):$miss -- comparisons there will refuse"

  # A REGISTERED-BUT-UNSWEPT scene without a floor is a NOTE, not a failure: it is a known state with
  # a named owner (04-Ocean-Factory awaits [#274]'s floor probe). Reported so it cannot be forgotten,
  # not failed so the gate cannot be trusted. Absent is not zero, and not-yet is not broken.
  unfloored=$(scenes_without_floor all)
  arms=$((arms+1))
  if [ -z "$unfloored" ]; then echo "  ok   every registered scene has a floor"
  else echo "  ok   registered-but-unfloored (expected, not swept):$unfloored"; fi

  # The plan must name every scene it intends to run. A driver that silently drops one produces a
  # cartesian with a hole and a summary that does not mention it.
  plan=$("$0" --repeats 2 --dry-run 2>&1)
  planned=0
  while read -r s; do echo "$plan" | grep -qF -- "$s" && planned=$((planned+1)); done < <(scenes_from_table)
  [ "$planned" -eq "$n" ] && ok "the plan names all $n scenes" \
                          || bad "the plan names $planned of $n scenes"

  # THE THREE OUTCOMES MUST BE TOLD APART. This is the arm that would have stopped the 2026-08-22
  # mislabelling, where a run with ZERO failed legs was reported as "2 of 5 scenes FAILED". The
  # failed case has no real example on disk yet -- which is exactly why it is synthesised here rather
  # than waited for.
  t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
  mk() { mkdir -p "$t/$1"; printf '{"legs":%s}' "$2" > "$t/$1/manifest.json"; }
  mk clean '[{"verdict":"OK"},{"verdict":"OK"}]'
  mk voidy '[{"verdict":"OK"},{"verdict":"VOID"}]'
  mk bad   '[{"verdict":"OK"},{"verdict":"FAILED"}]'
  mk mixed '[{"verdict":"VOID"},{"verdict":"FAILED"}]'

  [ "$(scene_outcome "$t/clean")" = clean ]     && ok "all-OK reads clean" || bad "all-OK misread"
  [ "$(scene_outcome "$t/voidy")" = void-only ] && ok "a VOID leg reads void-only, NOT failed" \
                                                || bad "a VOID leg was read as a failure -- the 08-22 defect"
  [ "$(scene_outcome "$t/bad")"   = failed ]    && ok "a FAILED leg reads failed" || bad "a FAILED leg was not caught"
  # VOID AND FAILED TOGETHER MUST READ FAILED. Reporting "void-only" for a run that also lost legs
  # would hide a real failure behind an expected one -- the worst of the three confusions.
  [ "$(scene_outcome "$t/mixed")" = failed ]    && ok "VOID+FAILED reads failed, not void-only" \
                                                || bad "a failure hid behind a VOID"
  [ "$(scene_outcome "$t/nosuch")" = unknown ]  && ok "an unreadable run reads unknown, not clean" \
                                                || bad "a missing manifest read as clean"

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

failed=(); voided=(); ran=0
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

  # A LEVER THAT DID NOT ENGAGE IS NOT A SCENE THAT FAILED, and this script spelled them the same
  # way on 2026-08-22: it printed "2 of 5 scenes FAILED" over a run in which ZERO legs failed. Both
  # non-zero exits were VOID legs -- the runner refusing to quote a lever whose witness did not move
  # -- and at the ship those VOIDs were the CORRECT ANSWER: render.cache.parallax.skipped reads 0
  # and 0 because a world with no parallax has no parallax work to skip. That is the property the
  # ship was added to the fixture set to exercise, reporting itself, and it was labelled a failure.
  #
  # Same distinction this project enforces everywhere else: ABSENT is not ZERO, SKIP is not PASS.
  # A VOID-only scene is BANKED AND USABLE for every lever that did engage.
  if [ $rc -eq 0 ]; then :
  elif [ "$(scene_outcome "$after")" = "void-only" ]; then
    echo "  -- scene '$s': VOID leg(s) only -- a lever did not engage here. BANKED AND USABLE."
    voided+=("$s")
  else
    echo "  !! scene '$s' exited $rc with legs that FAILED, not merely VOID"
    failed+=("$s")
  fi
  ran=$((ran+1))
done

[ "$DRY" = 1 ] && exit 0
echo
echo "=== cartesian complete: $ran scene(s) run, index at $INDEX ==="
cat "$INDEX" 2>/dev/null
if [ ${#voided[@]} -gt 0 ]; then
  echo
  echo "scene-matrix: ${#voided[@]} of $ran scene(s) have VOID leg(s): ${voided[*]}"
  echo "  NOT A FAILURE. A VOID means the runner refused to quote a lever whose witness did not move --"
  echo "  the lever did not engage AT THAT SCENE. Every other lever there measured normally and the"
  echo "  scene is fully quotable for them. At the ship this is the EXPECTED answer for the parallax"
  echo "  cache: a world with no parallax has no parallax work to skip."
fi
if [ ${#failed[@]} -gt 0 ]; then
  echo
  echo "scene-matrix: ${#failed[@]} of $ran scene(s) FAILED: ${failed[*]}"
  echo "  The other $((ran - ${#failed[@]})) are banked and usable. This is a PARTIAL cartesian -- say so when quoting it."
  exit 1
fi
# VOID-only exits non-zero so a caller cannot mistake it for an all-engaged run, but it is spelled
# differently from failure above, and it is NOT a partial cartesian.
[ ${#voided[@]} -gt 0 ] && { echo "scene-matrix: all $ran scenes completed, ${#voided[@]} with VOID leg(s). Nothing failed."; exit 3; }
echo "scene-matrix: all $ran scenes completed, every lever engaged at every scene"
