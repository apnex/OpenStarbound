#!/usr/bin/env bash
# LEVER MATRIX -- the orchestration half.
#
# WHAT THIS IS. One unattended run that profiles the shipped baseline and then each perf lever turned
# OFF in turn, interleaved across repeats, at one pinned location. It produces the raw per-leg
# telemetry and a manifest saying exactly what ran. It does NOT attribute cost: see "WHAT THIS DOES
# NOT DO" below. Every lever it tests is declared in scripts/lever-table.json.
#
# WHY IT EXISTS, AND WHY LOOPING IS THE LEAST OF IT. Looping render-profile.sh is ten lines. The
# reason this script is longer than ten lines is a defect that already happened here:
#
#     On 2026-08-04 an adaptive-border A/B ran `--set lightingAdaptiveBorder=false` on one leg and
#     relied on the CODE DEFAULT for the other. The default is inert -- a pinned key in the harness
#     config overrides it -- so BOTH legs ran with the lever OFF. The profile reported a full table of
#     plausible-looking deltas, every one an artefact of differing recompute counts. The tell was that
#     lighting.calc.cells was identical across the legs: the one quantity the lever exists to move.
#     NOTHING IN THE TOOLING OBJECTED.
#
# render-profile.sh has since been hardened to snapshot and restore the config on every exit path, so
# a leg can no longer INHERIT the previous leg's pin. That fixes half of it. The other half is that a
# leg can still be under-specified, or aimed at a lever that does not do what its name says, and come
# back looking exactly like a result. So this runner asserts the experiment HAPPENED:
#
#   * every lever declares a WITNESS -- the metric it exists to move -- and a leg whose witness did
#     not move against its own repeat's baseline is reported VOID, not quoted;
#   * every witness must EXIST in a real profile before the matrix is allowed to start, because
#     "checked by X" is itself a claim and four named instruments here once did not exist;
#   * the harness config is asserted equal to the declared baseline BEFORE every leg, so a failed
#     restore is caught at the next leg instead of silently poisoning the rest of the run;
#   * every leg writes the FULL lever set explicitly, not just the key under test.
#
# WHAT THIS DOES NOT DO -- stated so the boundary is a decision, not an omission. It does not compute
# cost deltas, per-pass attribution, or CPU-vs-GPU ratios. That half reads the telemetry vocabulary,
# and the vocabulary is being converged (schema 3 -> 4, see docs/superpowers/specs/
# 2026-08-06-metric-descriptor-convergence-design.md). Building the analysis against schema 3 would
# mint a baseline in a vocabulary we are in the middle of replacing -- which is exactly the defective
# baseline this campaign already paid for once. The runner therefore emits raw legs and a manifest,
# and the analysis half lands when the descriptor does.
#
# The witness check is deliberately on the near side of that line. It asks "did the experiment
# happen", not "what did it cost", and it needs one integer per leg. Deferring it would mean finding
# out that a three-hour run was void after the three hours.
#
# Usage:
#   scripts/lever-matrix.sh --warp <bookmark substring> [--repeats N] [--seconds N] [--dry-run]
#   scripts/lever-matrix.sh --check-table    # CI: the declared table still matches the harness config
#   scripts/lever-matrix.sh --selftest
#
# Exit: 0 ok  1 a leg failed or a check fired  64 usage  77 SKIP (nothing was measured)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2

EXIT_SKIP=77
EXIT_USAGE=64

TABLE=scripts/lever-table.json
CFG=harness/storage-perf/starbound.config
PROFILE=scripts/render-profile.sh
REPEATS=3
SECONDS_PER_LEG=90
WARP=""
DRY_RUN=0

# ---------------------------------------------------------------------------------------------
# Small python helpers. python3 rather than jq because every other gate in this repo already
# depends on python3 and none of them depend on jq -- a new tool dependency is a new way for a gate
# to be skipped on a machine that does not have it, and a skipped gate reads like a passing one.
# ---------------------------------------------------------------------------------------------

# Read the lever table and refuse anything under-declared. Prints one "key<TAB>baseline<TAB>off" line
# per lever on success.
read_table() {
  python3 - "$1" <<'PY'
import json, sys
path = sys.argv[1]
try:
    doc = json.load(open(path))
except Exception as e:
    print("lever-matrix: cannot parse %s (%s)" % (path, e), file=sys.stderr); sys.exit(1)
levers = doc.get("levers")
if not isinstance(levers, list):
    print("lever-matrix: %s has no 'levers' list" % path, file=sys.stderr); sys.exit(1)
if not levers:
    # Distinct from an invalid table: there is nothing to measure, which is a SKIP, not a failure.
    sys.exit(77)
required = ("key", "baseline", "off", "witness", "changesOutput", "why")
bad = []
for i, lv in enumerate(levers):
    for f in required:
        if f not in lv:
            bad.append("lever[%d] is missing field '%s'" % (i, f)); continue
        if lv[f] is None:
            # A null off-value or witness is an ADMISSION that nobody read the code. Refusing here
            # is the whole point: an A/B against a guessed off-value measures nothing and says so to
            # nobody.
            bad.append("lever[%d] (%s) has %s = null -- undeclared, not defaulted"
                       % (i, lv.get("key", "?"), f))
seen = set()
for lv in levers:
    k = lv.get("key")
    if k in seen:
        bad.append("duplicate lever key %r -- the matrix would test it twice and the second leg would win" % k)
    seen.add(k)
if bad:
    for b in bad: print("lever-matrix: " + b, file=sys.stderr)
    sys.exit(1)
for lv in levers:
    print("%s\t%s\t%s" % (lv["key"], json.dumps(lv["baseline"]), json.dumps(lv["off"])))
PY
}

# Assert the live harness config carries exactly the declared baseline for every lever key. A drift
# here means either the table is stale or a previous leg's restore did not fire; both are fatal and
# neither is visible in a profile.
# Paths are ARGUMENTS, not inherited globals. `VAR=x some_function` leaves VAR set after the call in
# bash, so a selftest arm that overrode $TABLE would silently poison every arm after it -- a checker
# whose own harness leaks state is not evidence of anything.
assert_baseline() { # assert_baseline <table> <config>
  python3 - "$1" "$2" <<'PY'
import json, sys
table, cfgpath = sys.argv[1], sys.argv[2]
doc = json.load(open(table))
levers = doc["levers"]
cfg = json.load(open(cfgpath))
bad = []
# NOT LEVERS, but they must hold on every leg. A governor left adaptive, or an oracle left armed,
# changes the workload between legs without appearing in any lever's descriptor -- so it is asserted
# here, where a drift is caught BEFORE the measurement rather than inferred from it afterwards.
for k, want in doc.get("pinned", {}).items():
    if k not in cfg:
        bad.append("%s: pinned to %r but ABSENT from %s -- the engine would use its code default"
                   % (k, want, cfgpath))
    elif cfg[k] != want:
        bad.append("%s: config has %r, pinned value is %r" % (k, cfg[k], want))
for lv in levers:
    k = lv["key"]
    if k not in cfg:
        bad.append("%s: declared baseline %r but the key is ABSENT from %s -- the engine would fall "
                   "back to its code default, which a pinned config exists to override"
                   % (k, lv["baseline"], cfgpath))
    elif cfg[k] != lv["baseline"]:
        bad.append("%s: config has %r, table declares baseline %r" % (k, cfg[k], lv["baseline"]))
if bad:
    print("lever-matrix: HARNESS CONFIG DOES NOT MATCH THE DECLARED BASELINE", file=sys.stderr)
    for b in bad: print("  " + b, file=sys.stderr)
    print("  Either a previous leg's config restore did not fire, or scripts/lever-table.json is "
          "stale. Nothing measured from here would be attributable.", file=sys.stderr)
    sys.exit(1)
PY
}

# Assert every declared witness metric actually appears in a profile. "checked by X" is itself a
# claim; this repo has already shipped four named instruments that did not exist or did not measure
# the thing. Run against the first baseline leg, BEFORE paying for the rest of the matrix.
assert_witnesses_exist() { # assert_witnesses_exist <table> <profile>
  python3 - "$1" "$2" <<'PY'
import json, sys
table, prof = sys.argv[1], sys.argv[2]
levers = json.load(open(table))["levers"]
metrics = json.load(open(prof)).get("metrics", {})
missing = [ (lv["key"], lv["witness"]) for lv in levers if lv["witness"] not in metrics ]
if missing:
    print("lever-matrix: DECLARED WITNESS METRIC DOES NOT EXIST", file=sys.stderr)
    for k, w in missing:
        print("  lever %s declares witness %r -- not present in %s" % (k, w, prof), file=sys.stderr)
    print("  A witness that does not exist cannot fail, so every leg would pass vacuously. Fix the "
          "witness in scripts/lever-table.json or register the metric.", file=sys.stderr)
    sys.exit(1)
print("  all %d declared witnesses present in the baseline profile" % len(levers))
PY
}

# THE CHECK THAT WAS MISSING. Compare one lever's witness between its own repeat's baseline and its
# off leg. Identical -> the lever did not engage -> VOID.
witness_moved() {
  python3 - "$1" "$2" "$3" <<'PY'
import json, sys
base_prof, off_prof, witness = sys.argv[1], sys.argv[2], sys.argv[3]

def read(path):
    m = json.load(open(path)).get("metrics", {})
    if witness not in m:
        return None
    e = m[witness]
    # Counters carry 'value'; timers carry a count and a total. Prefer the count, because it is the
    # quantity that says HOW OFTEN the guarded work ran -- which is what "did the lever engage"
    # actually asks. A timer total can drift on noise alone; a count cannot.
    for field in ("count", "value"):
        if field in e:
            return e[field]
    return None

b, o = read(base_prof), read(off_prof)
if b is None or o is None:
    print("WITNESS-ABSENT")
    sys.exit(0)
print("MOVED %s %s" % (b, o) if b != o else "IDENTICAL %s %s" % (b, o))
PY
}

# ---------------------------------------------------------------------------------------------
# Selftest. Every check above is itself a claim, and an unwatched checker is the defect it exists to
# prevent. Each arm proves one check FIRES -- not merely that it is present.
# ---------------------------------------------------------------------------------------------
selftest() {
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' RETURN
  fails=0
  arm() { # arm <name> <expected-rc> <actual-rc>
    if [ "$2" -eq "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1 (expected rc $2, got $3)"; fails=1; fi
  }

  # 1. A null off-value is refused, not defaulted.
  cat > "$tmp/null-off.json" <<'EOF'
{"levers":[{"key":"k","baseline":true,"off":null,"witness":"w","changesOutput":false,"why":"x"}]}
EOF
  read_table "$tmp/null-off.json" >/dev/null 2>&1; arm "null off-value refused" 1 $?

  # 2. A fully declared table is accepted.
  cat > "$tmp/good.json" <<'EOF'
{"levers":[{"key":"k","baseline":true,"off":false,"witness":"w","changesOutput":false,"why":"x"}]}
EOF
  read_table "$tmp/good.json" >/dev/null 2>&1; arm "declared table accepted" 0 $?

  # 3. An EMPTY table is a SKIP, not a pass. A matrix with no levers measured nothing, and this repo
  #    has already counted a gate that compared nothing as green.
  echo '{"levers":[]}' > "$tmp/empty.json"
  read_table "$tmp/empty.json" >/dev/null 2>&1; arm "empty table SKIPs, does not pass" "$EXIT_SKIP" $?

  # 4. A duplicate key is refused (the second leg would silently win).
  cat > "$tmp/dup.json" <<'EOF'
{"levers":[{"key":"k","baseline":true,"off":false,"witness":"w","changesOutput":false,"why":"x"},
           {"key":"k","baseline":1,"off":0,"witness":"w2","changesOutput":false,"why":"y"}]}
EOF
  read_table "$tmp/dup.json" >/dev/null 2>&1; arm "duplicate lever key refused" 1 $?

  # 5. A witness naming a metric that does not exist is refused BEFORE the matrix runs.
  echo '{"metrics":{"other.metric":{"value":1}}}' > "$tmp/prof-nowitness.json"
  assert_witnesses_exist "$tmp/good.json" "$tmp/prof-nowitness.json" >/dev/null 2>&1
  arm "absent witness metric refused" 1 $?

  # 5b. ...and a present one is accepted, so arm 5 is not passing because the check always fails.
  #     A null control on the instrument itself: this repo has already read an instrument's own
  #     artefact as a result once.
  echo '{"metrics":{"w":{"value":1}}}' > "$tmp/prof-witness.json"
  assert_witnesses_exist "$tmp/good.json" "$tmp/prof-witness.json" >/dev/null 2>&1
  arm "present witness metric accepted" 0 $?

  # 6. THE 2026-08-04 ARM. An identical witness across the two legs reads as IDENTICAL, which the
  #    runner treats as VOID. This is the exact shape that produced a full table of fake deltas.
  echo '{"metrics":{"w":{"count":1000}}}' > "$tmp/a.json"
  echo '{"metrics":{"w":{"count":1000}}}' > "$tmp/b.json"
  v=$(witness_moved "$tmp/a.json" "$tmp/b.json" w)
  case "$v" in IDENTICAL*) echo "  ok   identical witness reads VOID";; *) echo "  FAIL identical witness read as '$v'"; fails=1;; esac

  # 7. ...and a witness that DID move reads as moved. Arm 6 alone would pass if the checker always
  #    said IDENTICAL, which would void every real result instead of none.
  echo '{"metrics":{"w":{"count":2000}}}' > "$tmp/c.json"
  v=$(witness_moved "$tmp/a.json" "$tmp/c.json" w)
  case "$v" in MOVED*) echo "  ok   moved witness reads as moved";; *) echo "  FAIL moved witness read as '$v'"; fails=1;; esac

  # 8. A drifted config is caught against the declared baseline.
  echo '{"k": false}' > "$tmp/drifted.cfg"
  assert_baseline "$tmp/good.json" "$tmp/drifted.cfg" >/dev/null 2>&1
  arm "drifted baseline refused" 1 $?

  # 9. ...and a matching config passes, so arm 8 is not passing because the checker always fails.
  echo '{"k": true}' > "$tmp/match.cfg"
  assert_baseline "$tmp/good.json" "$tmp/match.cfg" >/dev/null 2>&1
  arm "matching baseline accepted" 0 $?

  # 10. An ABSENT key is refused as loudly as a wrong one. A key missing from the pinned config falls
  #     back to the engine's code default, which is precisely the inert-default trap of 2026-08-04.
  echo '{"unrelated": 1}' > "$tmp/absent.cfg"
  assert_baseline "$tmp/good.json" "$tmp/absent.cfg" >/dev/null 2>&1
  arm "absent baseline key refused" 1 $?

  echo
  [ $fails -eq 0 ] || { echo "lever-matrix selftest: FAILED"; return 1; }
  echo "lever-matrix selftest: 11/11 arms ok -- every check proven to fire AND to pass"
  return 0
}

# ---------------------------------------------------------------------------------------------

# TABLE-ONLY CHECK, for CI. The matrix itself needs a GPU and hours, so it is not a gate. But the
# correspondence between the declared table and the harness config IS checkable anywhere, costs
# milliseconds, and is exactly the thing that rots silently: someone re-pins a key in
# harness/storage-perf/starbound.config, the table keeps declaring the old value, and every later
# matrix run measures a baseline nobody declared. Checking the table against ITSELF would prove
# nothing -- this checks it against the tree.
check_table_only() {
  local rows rc
  rows=$(read_table "$TABLE"); rc=$?
  if [ $rc -eq "$EXIT_SKIP" ]; then
    echo "lever_table: $TABLE declares no levers -- nothing to check against the harness config."
    echo "lever_table: SKIP. This is an ABSENT verdict, not a passing one."
    return "$EXIT_SKIP"
  fi
  [ $rc -eq 0 ] || return 1
  assert_baseline "$TABLE" "$CFG" || return 1
  local npin
  npin=$(python3 -c "import json;print(len(json.load(open('$TABLE')).get('pinned',{})))")
  # NAME WHAT WAS CHECKED. A gate that reports "8 levers" while also verifying pinned values leaves
  # the pinned check invisible -- and an invisible check is one nobody notices going missing.
  echo "lever_table: $(printf '%s\n' "$rows" | wc -l) lever baselines + $npin pinned value(s) all match $CFG"
  return 0
}

while [ $# -gt 0 ]; do
  case "$1" in
    --selftest)    selftest; exit $? ;;
    --check-table) check_table_only; exit $? ;;
    --warp)     WARP=${2:-}; shift 2 ;;
    --repeats)  REPEATS=${2:-}; shift 2 ;;
    --seconds)  SECONDS_PER_LEG=${2:-}; shift 2 ;;
    --table)    TABLE=${2:-}; shift 2 ;;
    --dry-run)  DRY_RUN=1; shift ;;
    -h|--help)  sed -n '/^# Usage:/,/^# Exit:/p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "lever-matrix: unknown argument '$1'" >&2; exit $EXIT_USAGE ;;
  esac
done

# --warp is REQUIRED, with no default. render-profile.sh notes that the harness player's position
# PERSISTS between runs, so an unpinned matrix would measure whatever the last experiment left behind
# -- and at the ship the parallax pass costs approximately nothing, which would flatter every lever
# that touches it. A location nobody declared is not a baseline.
if [ -z "$WARP" ]; then
  echo "lever-matrix: --warp is required. The harness player's position persists between runs, so an" >&2
  echo "  unpinned matrix measures wherever the previous experiment finished. Pass a bookmark substring." >&2
  exit $EXIT_USAGE
fi

echo "=== lever matrix: preflight ==="

[ -f "$TABLE" ]   || { echo "lever-matrix: no $TABLE" >&2; exit 1; }
[ -x "$PROFILE" ] || { echo "lever-matrix: no $PROFILE" >&2; exit 1; }
# BEFORE the plan is printed and long before the world loads: a stale binary discovered on leg 1 has
# already cost a world load, and discovered later has cost the run.
scripts/assert-binary-fresh.sh dist/starbound || exit 1

# The Director must be out of the game: a second client competes for the same GPU, and every number
# this run produces would be contention. pgrep -x, not -f: -f matches this script's own command line.
if pgrep -x starbound >/dev/null 2>&1; then
  echo "lever-matrix: a starbound process is running. Every leg would be measured under contention" >&2
  echo "  with it. Close the game and re-run." >&2
  exit 1
fi

TABLE_ROWS=$(read_table "$TABLE"); rc=$?
if [ $rc -eq "$EXIT_SKIP" ]; then
  echo "lever-matrix: $TABLE declares NO levers, so this matrix would measure nothing."
  echo "lever-matrix: SKIP -- this is not a pass. Populate the table (off-values and witnesses read"
  echo "              from the code, not remembered) and re-run."
  exit $EXIT_SKIP
fi
[ $rc -eq 0 ] || exit 1

mapfile -t LEVERS < <(printf '%s\n' "$TABLE_ROWS")
N_LEVERS=${#LEVERS[@]}
echo "  $N_LEVERS levers declared in $TABLE"

assert_baseline "$TABLE" "$CFG" || exit 1
echo "  harness config matches the declared baseline for all $N_LEVERS keys"

# Build the full baseline --set list ONCE. Every leg passes all of it, then overrides exactly one
# key. render-profile.sh already restores the config per leg, so this is belt as well as braces --
# but it also means the manifest records the COMPLETE state each leg ran under, rather than one key
# and an assumption about the rest.
BASE_SETS=()
for row in "${LEVERS[@]}"; do
  k=${row%%$'\t'*}; rest=${row#*$'\t'}; b=${rest%%$'\t'*}
  BASE_SETS+=(--set "$k=$b")
done

RUN_ID="matrix-$(date +%Y%m%d-%H%M%S)"
OUT="harness/matrix/$RUN_ID"

# One leg costs the measurement window plus a full world load. The load is the part people forget,
# and it is what turns "90 seconds times 27" into an afternoon.
LOAD_EST=120
TOTAL_LEGS=$(( (N_LEVERS + 1) * REPEATS ))
EST_MIN=$(( TOTAL_LEGS * (SECONDS_PER_LEG + LOAD_EST) / 60 ))

echo
echo "=== plan ==="
echo "  run id        $RUN_ID"
echo "  location      --warp '$WARP'  (pinned; the same scene for every leg)"
echo "  legs per pass $((N_LEVERS + 1))  (baseline + one per lever, each lever OFF in turn)"
echo "  repeats       $REPEATS, INTERLEAVED -- pass 1 runs every leg, then pass 2, then pass 3."
echo "                Not three consecutive runs per leg: thermal state drifts over a multi-hour"
echo "                matrix, and consecutive repeats would bake that drift into whichever lever"
echo "                happened to run while the package was hot."
echo "  total legs    $TOTAL_LEGS at ${SECONDS_PER_LEG}s + ~${LOAD_EST}s load  ~= ${EST_MIN} min"
echo "  output        $OUT/"
echo
for row in "${LEVERS[@]}"; do
  k=${row%%$'\t'*}; rest=${row#*$'\t'}; b=${rest%%$'\t'*}; o=${rest##*$'\t'}
  printf '    %-32s %s -> %s\n' "$k" "$b" "$o"
done

if [ "$DRY_RUN" = 1 ]; then
  echo
  echo "lever-matrix: --dry-run, nothing executed."
  exit 0
fi

mkdir -p "$OUT"
MANIFEST="$OUT/manifest.json"
: > "$OUT/legs.tsv"

# A LEG THAT RAN AND A LEG WHOSE NUMBERS ARE QUOTABLE ARE TWO DIFFERENT VERDICTS, and the first
# version of this function collapsed them. telemetry-window.py exits 3 when its closure oracle finds a
# violation -- "parts exceed the whole", "of UNKNOWN" -- which means DO NOT QUOTE THESE COSTS. It does
# not mean the leg did not happen: the profile is written, the counters in it are the counters the
# engine recorded, and the WITNESS question ("did the code path change?") is answered by counters, not
# by closure. Treating exit 3 as a dead leg killed an entire pass on a pre-existing 1.06% GPU-domain
# overage that has nothing to do with any lever under test.
#
# So exit 3 is kept and FLAGGED, and every other nonzero is fatal. The flag rides into the manifest so
# the analysis half -- which does read costs -- can refuse the leg on exactly the ground the oracle
# raised, rather than inheriting a silent pass.
LEG_VIOLATED=0
run_leg() { # run_leg <label> <extra --set args...>
  local label=$1; shift
  local rc
  LEG_VIOLATED=0
  echo
  echo "--- leg $label ---"
  # Integrity BEFORE the leg, so a failed restore from the previous leg is caught here rather than
  # discovered as an inexplicable result three hours later.
  assert_baseline "$TABLE" "$CFG" || { echo "lever-matrix: aborting -- the config drifted before leg $label" >&2; return 1; }
  "$PROFILE" "$SECONDS_PER_LEG" "$label" "${BASE_SETS[@]}" "$@" --warp "$WARP"; rc=$?
  if [ $rc -eq 3 ]; then
    LEG_VIOLATED=1
    echo "  NOTE: the closure oracle raised a violation on this leg. The leg is KEPT -- its counters are"
    echo "        still what the engine recorded, and the witness check reads counters. Its COSTS are"
    echo "        flagged not-quotable in the manifest."
  elif [ $rc -ne 0 ]; then
    return 1
  fi
  [ -f "harness/profiles/$label.json" ] || { echo "lever-matrix: leg $label produced no profile" >&2; return 1; }
  command cp "harness/profiles/$label.json" "$OUT/$label.json"
}

FAILED=(); VOID=(); OK=()

for r in $(seq 1 "$REPEATS"); do
  echo
  echo "############ pass $r of $REPEATS ############"

  BASE_LABEL="$RUN_ID-r$r-baseline"
  if ! run_leg "$BASE_LABEL"; then
    FAILED+=("$BASE_LABEL")
    echo "lever-matrix: pass $r baseline FAILED -- every leg in this pass would have nothing to be" >&2
    echo "  compared against, so the pass is skipped rather than run blind." >&2
    continue
  fi
  BASE_VIOLATED=$LEG_VIOLATED
  printf '%s\tbaseline\t-\tOK\t-\t%s\n' "$BASE_LABEL" "$([ $BASE_VIOLATED -eq 1 ] && echo no || echo yes)" >> "$OUT/legs.tsv"

  # Witnesses are checked against a REAL profile, once, on the first baseline that succeeds. A
  # witness naming a metric that does not exist would otherwise let every leg pass vacuously.
  if [ ! -f "$OUT/.witnesses-checked" ]; then
    assert_witnesses_exist "$TABLE" "$OUT/$BASE_LABEL.json" || exit 1
    touch "$OUT/.witnesses-checked"
  fi

  for row in "${LEVERS[@]}"; do
    k=${row%%$'\t'*}; rest=${row#*$'\t'}; o=${rest##*$'\t'}
    label="$RUN_ID-r$r-off-$k"
    if ! run_leg "$label" --set "$k=$o"; then
      FAILED+=("$label"); printf '%s\t%s\t%s\tFAILED\t-\t-\n' "$label" "$k" "$o" >> "$OUT/legs.tsv"; continue
    fi
    # Costs are quotable only if NEITHER end of the comparison was flagged: a delta against a violated
    # baseline is as unusable as a violated leg, and only one of the two is the leg you are looking at.
    quotable=$([ $LEG_VIOLATED -eq 0 ] && [ $BASE_VIOLATED -eq 0 ] && echo yes || echo no)
    w=$(python3 -c "
import json,sys
for lv in json.load(open('$TABLE'))['levers']:
    if lv['key'] == '$k': print(lv['witness']); break
")
    verdict=$(witness_moved "$OUT/$BASE_LABEL.json" "$OUT/$label.json" "$w")
    case "$verdict" in
      MOVED*)
        echo "  witness $w: ${verdict#MOVED } -- the lever engaged"
        OK+=("$label"); printf '%s\t%s\t%s\tOK\t%s\t%s\n' "$label" "$k" "$o" "$verdict" "$quotable" >> "$OUT/legs.tsv" ;;
      IDENTICAL*)
        echo "  VOID: witness $w is IDENTICAL across baseline and off (${verdict#IDENTICAL })."
        echo "        The lever did not engage, so any cost delta from this leg is an artefact."
        VOID+=("$label"); printf '%s\t%s\t%s\tVOID\t%s\t%s\n' "$label" "$k" "$o" "$verdict" "$quotable" >> "$OUT/legs.tsv" ;;
      *)
        echo "  VOID: witness $w absent from one of the two profiles."
        VOID+=("$label"); printf '%s\t%s\t%s\tVOID\twitness-absent\t%s\n' "$label" "$k" "$o" "$quotable" >> "$OUT/legs.tsv" ;;
    esac
  done
done

python3 - "$MANIFEST" "$RUN_ID" "$WARP" "$REPEATS" "$SECONDS_PER_LEG" "$TABLE" "$OUT/legs.tsv" <<'PY'
import json, sys
manifest, run_id, warp, repeats, secs, table, legs = sys.argv[1:8]
rows = []
for line in open(legs):
    p = line.rstrip("\n").split("\t")
    rows.append({"label": p[0], "lever": p[1], "offValue": p[2], "verdict": p[3],
                 "witness": p[4] if len(p) > 4 else None,
                 # "no" means the closure oracle flagged this leg or its baseline. The leg RAN and its
                 # witness is valid; its COSTS may not be quoted. Two different verdicts, kept apart.
                 "costsQuotable": (p[5] if len(p) > 5 else None)})
json.dump({
    "runId": run_id, "warp": warp, "repeats": int(repeats), "secondsPerLeg": int(secs),
    "leverTable": json.load(open(table))["levers"],
    "legs": rows,
    "analysis": "NOT PERFORMED -- this runner emits raw legs only. Cost attribution reads the "
                "telemetry vocabulary, which is mid-convergence (schema 3 -> 4). See "
                "docs/superpowers/specs/2026-08-06-metric-descriptor-convergence-design.md.",
}, open(manifest, "w"), indent=2)
PY

echo
echo "=== lever matrix: $RUN_ID ==="
echo "  ${#OK[@]} legs OK, ${#VOID[@]} VOID, ${#FAILED[@]} failed  ->  $OUT/"
[ ${#VOID[@]}   -gt 0 ] && { echo "  VOID (the lever did not engage; do NOT quote a delta from these):"; printf '    %s\n' "${VOID[@]}"; }
[ ${#FAILED[@]} -gt 0 ] && { echo "  FAILED:"; printf '    %s\n' "${FAILED[@]}"; }
echo "  cost attribution NOT performed -- raw legs only, by design. See the header."

if [ ${#OK[@]} -eq 0 ]; then
  echo "lever-matrix: NOTHING was measured -- 0 legs produced a usable result. This is a SKIP, not a pass."
  exit $EXIT_SKIP
fi
[ ${#FAILED[@]} -eq 0 ] && [ ${#VOID[@]} -eq 0 ] || exit 1
exit 0
