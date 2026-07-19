#!/bin/bash
# build-skeleton.sh -- Phase 1-3 OWNED-file skeleton for the render-branch reorg.
# For each content cluster: cut branch off its base, checkout its OWNED files verbatim from trunk,
# commit, partial-oracle (owned files == trunk). Shared-file hunks applied in a later pass.
# Idempotent: `switch -C` resets each branch, so re-running rebuilds cleanly.
set -euo pipefail
WT=/root/frackin/reorg-wt
MANIFEST=/root/frackin/OpenStarbound/scripts/reorg/manifest.tsv
BASE=2ea33530
TRUNK=dev/upstream-merge
G() { git -C "$WT" "$@"; }
owned() { awk -F'\t' -v c="$1" '$2==c{print $1}' "$MANIFEST"; }

build() {  # build <branch> <base-ref> <manifest-key>
  local BR="$1" B="$2" KEY="$3"
  local files; files=$(owned "$KEY")
  G switch -C "$BR" "$B" >/dev/null 2>&1
  if [ -n "$files" ]; then
    # shellcheck disable=SC2086
    G checkout "$TRUNK" -- $files
    G commit -q -m "reorg($BR): owned files verbatim from trunk (skeleton)"
    local resid; resid=$(G diff "$BR" "$TRUNK" -- $files)
    if [ -z "$resid" ]; then echo "  PASS  $BR : $(echo $files | wc -w) owned == trunk (base $B)"
    else echo "  FAIL  $BR :"; echo "$resid" | awk 'NR<=60'; exit 1; fi
  else
    echo "  PASS  $BR : 0 owned -> branch = base $B (shared hunks come later)"
  fi
}

#      branch                            base                            manifest-key
build docs                              "$BASE"                          docs
build tooling/build-toolchain           "$BASE"                          tooling/build-toolchain
build tooling/telemetry                 "$BASE"                          tooling/telemetry
build tooling/render-harness-oracle     "$BASE"                          tooling/render-harness-oracle
build perf/entity-dispatch              "$BASE"                          perf/entity-dispatch
build perf/core                         "$BASE"                          perf/core
build render/layer1                     "$BASE"                          render/layer1
build render/layer2-retained-surface    render/layer1                    render/layer2-retained-surface
build render/layer3-passes              render/layer2-retained-surface   render/layer3
build perf/server-tick                  tooling/telemetry                perf/server-tick
build perf/animation-drawable           tooling/telemetry                perf/animation-drawable
build perf/world-client-lighting        tooling/telemetry                perf/world-client-lighting

echo
echo "=== SKELETON BRANCHES ==="
for b in docs tooling/build-toolchain tooling/telemetry tooling/render-harness-oracle \
         perf/entity-dispatch perf/core perf/server-tick perf/animation-drawable perf/world-client-lighting \
         render/layer1 render/layer2-retained-surface render/layer3-passes; do
  printf "  %-34s %s\n" "$b" "$(git -C "$WT" rev-parse --short "$b")"
done