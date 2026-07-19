#!/bin/bash
# integrate.sh -- Phase 5: assemble `integration` = merge(all sovereign branches) + toolchain overlay,
# then run THE oracle (integration tree == dev/upstream-merge). Conflicts resolve to trunk (the proven
# union of the partitioned hunks). Runs in the reorg worktree. Does NOT build (see gate.sh).
set -uo pipefail
WT=/root/frackin/reorg-wt
BASE=2ea33530
TRUNK=dev/upstream-merge
G() { git -C "$WT" "$@"; }

# Merge order: render stack tip (brings L1+L2+L3), then the perf/tooling clusters, toolchain LAST.
SOVEREIGN=(
  render/layer3-passes
  perf/entity-dispatch
  perf/core
  tooling/telemetry
  perf/server-tick
  perf/animation-drawable
  perf/world-client-lighting
  tooling/render-harness-oracle
  docs
)
OVERLAY=tooling/build-toolchain

echo "=== assemble integration @ $BASE ==="
G switch -C integration "$BASE" >/dev/null 2>&1

merge_one() {  # merge_one <branch>
  local b="$1"
  if G merge --no-edit --no-ff "$b" >/dev/null 2>&1; then
    echo "  merged (clean): $b"
  else
    # resolve every conflicted path to the trunk version (the proven union), then continue
    local conflicts; conflicts=$(G diff --name-only --diff-filter=U)
    if [ -n "$conflicts" ]; then
      echo "  merged (resolved->trunk): $b  [$(echo "$conflicts" | wc -l) file(s)]"
      # shellcheck disable=SC2086
      G checkout "$TRUNK" -- $conflicts
      echo "$conflicts" | while IFS= read -r f; do G add "$f"; done
      G commit --no-edit >/dev/null 2>&1
    else
      echo "  MERGE FAILED (no conflicts?): $b"; G merge --abort 2>/dev/null; return 1
    fi
  fi
}

for b in "${SOVEREIGN[@]}"; do merge_one "$b" || exit 1; done
echo "=== toolchain overlay (last) ==="
merge_one "$OVERLAY" || exit 1

echo
echo "=== THE ORACLE: integration tree == $TRUNK ? ==="
bash /root/frackin/OpenStarbound/scripts/reorg/oracle.sh integration
