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

mapfile -t STEPS < <(python3 - <<'PY'
import yaml, pathlib, json
wf = yaml.safe_load(pathlib.Path(".github/workflows/gates.yml").read_text())
for job in wf["jobs"].values():
    for step in job["steps"]:
        if "run" in step:
            print(json.dumps([step.get("name", "(unnamed)"), step["run"]]))
PY
)

pass=0; fail=0; failed=()
for s in "${STEPS[@]}"; do
  name=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[0].split(" --")[0])' "$s")
  cmd=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[1])' "$s")
  printf '  %-24s ' "$name"
  if [ "$VERBOSE" = 1 ]; then
    echo; bash -c "$cmd"; rc=$?
  else
    out=$(bash -c "$cmd" 2>&1); rc=$?
  fi
  if [ $rc -eq 0 ]; then echo "OK"; pass=$((pass+1))
  else
    echo "FAIL (exit $rc)"; fail=$((fail+1)); failed+=("$name")
    [ "$VERBOSE" = 0 ] && echo "$out" | tail -6 | sed 's/^/      /'
  fi
done

echo
if [ $fail -eq 0 ]; then
  echo "run-gates: OK -- $pass/$pass gates green (the whole Gates workflow, not a subset)"
  exit 0
fi
echo "run-gates: FAIL -- $fail of $((pass+fail)) red: ${failed[*]}"
exit 1
