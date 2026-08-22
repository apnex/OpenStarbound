#!/usr/bin/env bash
# Run a PAIRED A/B: N interleaved pairs at one scene, each leg tagged so the comparison is a property
# of the artifact rather than a claim in a sentence.
#
#   scripts/render-ab.sh 90 gputax --warp "Desert Town" --repeats 2 \
#       --a "" --b "env:STAR_NO_PERPASS_GPU_TIMERS=1"
#   scripts/render-ab.sh 90 border --warp "04 Ocean Factory" --repeats 3 \
#       --a "set:lightingAdaptiveBorder=true" --b "set:lightingAdaptiveBorder=false"
#   scripts/render-ab.sh ... --dry-run          # print the plan, launch nothing
#   scripts/render-ab.sh --selftest             # prove the plan's shape without a GPU
#
# WHY THIS EXISTS (#279 item 1). scripts/leg-diff.py READS meta.pair and refuses to infer pairing from
# equal arm sizes -- so until something WROTE that tag, the easy path was still the unpaired one, and
# the unpaired path is held to a floor that swallows most real levers (Desert Town: ~6.6% CV).
#
# AND BECAUSE I WROTE THIS LOOP THREE TIMES BY HAND FIRST. The [#276] timer-tax legs, the [#278]
# stability probe and the epoch-0 check were each an ad-hoc `for` loop in a shell call. Two of them
# were interleaved correctly. The third was not, and it produced "content addition made the frame
# 18.9% FASTER" -- the one that went into a report.
#
# INTERLEAVED, NEVER BLOCKED. Pair i runs arm A then arm B before pair i+1 starts. Thermal and cache
# drift over a multi-leg run is real -- [#276] measured 4.4-4.8% WITHIN each arm while its paired
# differences agreed to 0.2% -- and blocking (all A, then all B) converts that drift directly into
# the result.
#
# WARM-UP IS DISCARDED, NOT MEASURED (#279 item 4). 03 Surface Outpost's cpu_cost ran 611 -> 560 ->
# 216 us/frame across three visits, ~3x monotonic: a first-visit transient that any ad-hoc probe
# through render-profile.sh eats whole. lever-matrix.sh has discarded a warm-up leg for this reason
# since [#239]; this is the same rule for the profiler. Warm-up legs carry NO pair tag, so leg-diff
# ignores them even if someone points it at the whole directory.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

SECONDS_PER_LEG=90; GROUP=""; WARP=""; REPEATS=2; WARMUP=1; SPEC_A=""; SPEC_B=""; DRY=0

if [ "${1:-}" = "--selftest" ]; then
  fails=0; arms=0
  # DERIVED, NEVER RESTATED. The first version of this banner said "5/5" while six arms ran -- the
  # same defect this project has now hit five times, where a summary keeps a number the thing it
  # summarises has outgrown. A count that can disagree with what happened is worse than no count.
  ok()   { arms=$((arms+1)); echo "  ok   $1"; }
  bad()  { arms=$((arms+1)); echo "  FAIL $1"; fails=1; }
  # THE PLAN IS THE TESTABLE PART. Whether a leg is fast or slow needs a GPU; whether the ORDER
  # interleaves, the tags are well formed, and warm-ups are untagged does not -- and those are
  # exactly the properties that were wrong when this loop was written by hand.
  plan=$(GROUP=g WARP="S" REPEATS=2 WARMUP=1 "$0" 90 g --warp S --repeats 2 --warmup 1 \
         --a "" --b "env:X=1" --dry-run 2>&1)

  echo "$plan" | grep -q "warmup" \
    && ok "a warm-up leg is planned" || bad "no warm-up leg"

  # Warm-up must carry NO pair tag, or leg-diff would fold a transient-contaminated leg into an arm.
  if echo "$plan" | grep "warmup" | grep -q -- "--pair"; then
    bad "warm-up leg carries a pair tag"
  else
    ok "the warm-up leg carries no pair tag"
  fi

  # INTERLEAVED: the tagged arms must read a,b,a,b -- not a,a,b,b. This is the property that failed
  # by hand and produced a published 18.9%.
  order=$(echo "$plan" | grep -o -- "--pair g:[0-9]*:[ab]" | sed 's/.*://' | tr -d '\n')
  [ "$order" = "abab" ] \
    && ok "arms interleave a,b,a,b (got '$order')" \
    || bad "arms are '$order', expected 'abab' -- blocked, not interleaved"

  # Indices must pair up: 0,0,1,1 -- an index that advances per LEG rather than per PAIR would tag
  # two halves of one pair as different pairs, and leg-diff would then decline to pair them at all.
  idx=$(echo "$plan" | grep -o -- "--pair g:[0-9]*:[ab]" | sed 's/--pair g:\([0-9]*\):.*/\1/' | tr -d '\n')
  [ "$idx" = "0011" ] \
    && ok "pair indices are 0,0,1,1 (got '$idx')" \
    || bad "pair indices are '$idx', expected '0011'"

  # An arm spec must reach the leg. An A/B whose arms are identical measures nothing and would look
  # exactly like a real one in the artifact.
  echo "$plan" | grep -q "X=1" \
    && ok "the b-arm spec reaches its legs" || bad "b-arm spec lost"

  # A MALFORMED SPEC MUST ABORT, NOT WARN. This arm exists because the first version RETURNED 2 from
  # a helper nobody checked and the run exited 0 -- the arm silently unapplied, both halves baseline.
  # Verify the STATUS, never the message: a complaint that does not stop the run is decoration.
  "$0" 90 g --warp S --a "" --b "bogus:token" --dry-run >/dev/null 2>&1
  [ $? -eq 2 ] && ok "a malformed spec token aborts with status 2" \
                || bad "a bad spec token did not abort -- the arm would run unapplied"

  [ $fails -eq 0 ] || { echo "render-ab selftest: FAILED"; exit 1; }
  echo "render-ab selftest: $arms/$arms arms ok -- warm-up planned and untagged, arms interleave, indices pair, a bad spec aborts"
  exit 0
fi

[ $# -ge 2 ] || { echo "usage: render-ab.sh SECONDS GROUP --warp SCENE [--repeats N] [--warmup N] --a SPEC --b SPEC" >&2; exit 2; }
SECONDS_PER_LEG=$1; GROUP=$2; shift 2
while [ $# -gt 0 ]; do
  case "$1" in
    --warp)    WARP="$2";    shift 2 ;;
    --repeats) REPEATS="$2"; shift 2 ;;
    --warmup)  WARMUP="$2";  shift 2 ;;
    --a)       SPEC_A="$2";  shift 2 ;;
    --b)       SPEC_B="$2";  shift 2 ;;
    --dry-run) DRY=1;        shift   ;;
    *) echo "render-ab: unknown argument '$1'" >&2; exit 2 ;;
  esac
done
[ -n "$WARP" ] || { echo "render-ab: --warp is required. An unpinned A/B measures wherever the previous run left the player, which is not a fixture." >&2; exit 2; }
[ "$SPEC_A" != "$SPEC_B" ] || { echo "render-ab: --a and --b are identical ('$SPEC_A'). That is not an A/B; it would produce two arms differing only by noise and an artifact that looks like a real comparison." >&2; exit 2; }

# A spec is space-separated tokens: `env:K=V` exports for that leg, `set:K=V` becomes render-profile's
# --set. Both forms are needed because the two real A/Bs so far differed one way each -- [#276] by an
# environment variable, the lever matrix by a config key.
run_leg() {  # label, spec, pair-or-empty
  local label="$1" spec="$2" pair="$3" envs=() sets=() t
  for t in $spec; do
    case "$t" in
      env:*) envs+=("${t#env:}") ;;
      set:*) sets+=(--set "${t#set:}") ;;
      "")    ;;
      # ABORT, DO NOT SKIP. A typo -- `st:` for `set:` -- would otherwise drop the arm's only
      # difference and run BOTH arms as the baseline. That is an A/B with no B, and it is
      # indistinguishable in the artifact from a real one whose lever happened to do nothing. Caught
      # by verifying the guard's exit code rather than its message, which is the only way to tell a
      # refusal from a complaint.
      *) echo "render-ab: bad spec token '$t' (want env:K=V or set:K=V). Refusing: an unapplied arm "\
              "produces two baseline legs that look exactly like a measured comparison." >&2
         exit 2 ;;
    esac
  done
  local cmd=(scripts/render-profile.sh "$SECONDS_PER_LEG" "$label" --warp "$WARP")
  [ ${#sets[@]} -gt 0 ] && cmd+=("${sets[@]}")
  [ -n "$pair" ] && cmd+=(--pair "$pair")
  if [ "$DRY" = "1" ]; then
    echo "    ${envs[*]:-} ${cmd[*]}"
    return 0
  fi
  ( for e in "${envs[@]:-}"; do [ -n "$e" ] && export "${e?}"; done; "${cmd[@]}" ) 2>&1 | tail -2
}

echo "=== render-ab: group '$GROUP' at '$WARP' ==="
echo "  $REPEATS pair(s), interleaved a,b per pair; $WARMUP warm-up leg(s), DISCARDED"
echo "  a: ${SPEC_A:-<baseline, no changes>}"
echo "  b: ${SPEC_B:-<baseline, no changes>}"

for w in $(seq 1 "$WARMUP"); do
  echo "  --- warmup $w (discarded: first-visit transient, see #279)"
  run_leg "${GROUP}-warmup${w}" "$SPEC_A" ""
done
for i in $(seq 0 $((REPEATS - 1))); do
  echo "  --- pair $i"
  run_leg "${GROUP}-p${i}a" "$SPEC_A" "${GROUP}:${i}:a"
  run_leg "${GROUP}-p${i}b" "$SPEC_B" "${GROUP}:${i}:b"
done

[ "$DRY" = "1" ] && exit 0
echo
echo "=== compare (the pairing is in the artifacts; leg-diff will read it) ==="
python3 scripts/leg-diff.py --pair-group "$GROUP"
