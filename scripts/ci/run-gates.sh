#!/usr/bin/env bash
# Run every gate the Gates workflow runs, locally, in one command.
#
# WHY THIS EXISTS. On 2026-08-02 I pushed 30 commits after reporting "14 gates green". CI came back
# red on `arch_graph_fresh`, because the set I had been running was the set I happened to know about
# -- the spec gates I had been editing -- and not the set the workflow actually runs. Sixteen steps
# exist; I was checking eleven, and the five I skipped were the ones I had not touched, which is
# precisely the wrong selection rule. A gate you forget to run is indistinguishable from a gate that
# does not exist.
#
# It reads .github/workflows/gates.yml rather than restating the commands, so a step added to CI is
# run here on the next invocation with no second edit. That is the same one-declaration rule the
# workflow already applies to the ratchet ceilings via --from-cmake.
#
#   scripts/ci/run-gates.sh          # run all, summarise
#   scripts/ci/run-gates.sh -v       # show each gate's output
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2

VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

# A THIRD VERDICT, because two were not enough to tell the truth. This runner classified every gate as
# OK or FAIL on `rc == 0`, so a gate that RAN AND COMPARED NOTHING was indistinguishable from one that
# verified something. Demonstrated 2026-08-05: with no game running, metrics_mutual took its skip path,
# exited 0, printed OK, and was counted toward "31/31 gates green" -- while nothing had been compared.
# That is the shape render-gate.sh already carries three warnings about (an unarmed oracle prints
# SKIPPED and greps as a pass); it was living in the runner those warnings report through.
#
# 77 rather than a marker line in the output, for the reason this project keeps relearning: a verdict
# must be a STATUS. Text after the fact is decoration, and grepping it is how the wrong word passed a
# screaming oracle. 77 is the Automake/TAP skip convention, so it is borrowed rather than invented.
EXIT_SKIP=77

if [ "${1:-}" = "--selftest" ]; then
  # The classifier is itself a gate, and an unwatched classifier is the defect it exists to prevent.
  rcof() { bash -c "$1" >/dev/null 2>&1; echo $?; }
  ok=$(rcof 'true'); sk=$(rcof "exit $EXIT_SKIP"); bad=$(rcof 'false')
  fails=0
  [ "$ok"  -eq 0            ] || { echo "  FAIL: a passing step did not read as 0"; fails=1; }
  [ "$sk"  -eq "$EXIT_SKIP" ] || { echo "  FAIL: a skipping step did not read as $EXIT_SKIP"; fails=1; }
  [ "$bad" -ne 0 ] && [ "$bad" -ne "$EXIT_SKIP" ] || { echo "  FAIL: a failing step was not distinguishable from pass or skip"; fails=1; }
  [ $fails -eq 0 ] || exit 1
  echo "  run-gates selftest: 3/3 arms ok -- pass, skip and fail are three distinct verdicts"
  exit 0
fi

mapfile -t STEPS < <(python3 - <<'PY'
import yaml, pathlib, json
wf = yaml.safe_load(pathlib.Path(".github/workflows/gates.yml").read_text())
for job in wf["jobs"].values():
    for step in job["steps"]:
        if "run" in step:
            print(json.dumps([step.get("name", "(unnamed)"), step["run"]]))
PY
)

pass=0; fail=0; skip=0; failed=(); skipped=()
for s in "${STEPS[@]}"; do
  name=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[0].split(" --")[0])' "$s")
  cmd=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[1])' "$s")
  printf '  %-24s ' "$name"
  if [ "$VERBOSE" = 1 ]; then
    echo; bash -c "$cmd"; rc=$?
  else
    out=$(bash -c "$cmd" 2>&1); rc=$?
  fi
  if [ $rc -eq "$EXIT_SKIP" ]; then
    # The reason is printed even in quiet mode, unlike a pass. A skip nobody reads IS a pass.
    echo "SKIP (did not verify)"; skip=$((skip+1)); skipped+=("$name")
    [ "$VERBOSE" = 0 ] && echo "$out" | tail -4 | sed 's/^/      /'
  elif [ $rc -eq 0 ]; then
    echo "OK"; pass=$((pass+1))
  else
    echo "FAIL (exit $rc)"; fail=$((fail+1)); failed+=("$name")
    [ "$VERBOSE" = 0 ] && echo "$out" | tail -6 | sed 's/^/      /'
  fi
done

echo
if [ $fail -gt 0 ]; then
  echo "run-gates: FAIL -- $fail of $((pass+fail+skip)) red: ${failed[*]}"
  [ $skip -gt 0 ] && echo "run-gates: and $skip SKIPPED (unverified): ${skipped[*]}"
  exit 1
fi
if [ $skip -gt 0 ]; then
  # Exit 0 -- a skip is an ABSENT verdict, not a failing one, and failing CI for a gate that needs
  # hardware nobody has would train people to ignore it. But the WORDING may never read as all-green,
  # because the tally is the part that gets quoted into a commit message.
  echo "run-gates: $pass green, $skip SKIPPED and therefore UNVERIFIED: ${skipped[*]}"
  echo "run-gates: this is NOT $((pass+skip))/$((pass+skip)) green -- $skip gate(s) did not run."
  exit 0
fi
echo "run-gates: OK -- $pass/$pass gates green (the whole Gates workflow, not a subset)"
exit 0
