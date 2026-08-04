#!/usr/bin/env bash
# LIVE render profile. Boots the real client offscreen on the real GPU, drops into single-player, waits for the
# world to quiesce, then leaves the SIM RUNNING and records telemetry for a wall-clock window. No Director in
# the chair, no window on his screen.
#
# THE GATE (scripts/render-gate.sh) AND THIS ARE DIFFERENT INSTRUMENTS. The gate FREEZES the world so frames are
# bit-identical and a refactor can be certified byte-for-byte -- but a frozen scene has no entity animation, no
# particles, no liquid motion and no lighting recomputes, so it measures a FLOOR, not play. This leaves the sim
# running (STAR_RENDERTEST_NOFREEZE=1): non-deterministic, therefore useless for correctness, and therefore the
# only honest thing to quote a millisecond from.
#
# HOW IT AVOIDS PERTURBING WHAT IT MEASURES:
#   * WARMUP is set absurdly high, so the harness never leaves its settle phase and never runs the golden-frame
#     capture. That capture does a full-framebuffer glReadPixels every frame -- a hard GPU->CPU sync that breaks
#     pipelining and inflates the very frame timings we are here to read.
#   * storage-perf/ pins the three pixel oracles OFF. They allocate reference surfaces and issue extra draws and
#     readbacks; profiling with them armed measures the scaffolding, not the shipping path.
#   * vsync is off in storage-perf/, so passes report their real cost instead of the wait for the 60Hz flip.
#
# Usage:
#   render-profile.sh [seconds] [label] [--set key=jsonValue]... [--warp <bookmark substring>]
#     render-profile.sh 90 baseline
#     render-profile.sh 90 spread-16 --set lightingGpuSpreadIterations=16 --warp exploring
#
# --set writes the key into storage-perf/starbound.config before launch, so an A/B is two runs with the key
# flipped between them. The in-process STAR_RENDERTEST_AB path is the GATE's mechanism -- it needs a FROZEN
# world to make its two legs comparable, so it does not apply to a live run.
#
# --warp aims the run at a real location (any substring of a teleport bookmark name). Unset parks at the ship,
# where the parallax pass costs approximately nothing and the number would flatter every lever.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."

SECONDS_TO_RUN=${1:-90}
LABEL=${2:-profile}
shift 2 2>/dev/null || shift $#

SETS=()
WARP=${STAR_RENDERTEST_WARP:-}
while [ $# -gt 0 ]; do
  case "$1" in
    --set)  SETS+=("$2"); shift 2 ;;
    --warp) WARP="$2";    shift 2 ;;
    *) echo "unknown arg '$1'"; exit 2 ;;
  esac
done

BIN=dist/starbound
BOOT="$PWD/harness/sbinit-perf.config"
SNAPDIR="$PWD/harness/storage-perf/telemetry"
# The game writes here unconditionally (logDirectory in sbinit-perf.config), so the live name is fixed. Each
# run's log is ARCHIVED under its label on the way out -- see archive_log. Overwriting it in place cost a real
# post-mortem once: a capture behaved differently from its predecessor and the predecessor's log was already
# gone, so the discrepancy could not be explained and had to be recorded as unresolved.
LOG="$PWD/harness/logs-perf/starbound.log"
ARCHIVE="$PWD/harness/logs-perf/archive"
# Cache of the teleport bookmarks seen on the last successful load, so --warp can be validated BEFORE paying
# for a world load. Refreshed every run; the engine is the authority, this is only a fast pre-flight.
BOOKMARKS="$PWD/harness/logs-perf/bookmarks.txt"

[ -x "$BIN" ] || { echo "no $BIN -- build first"; exit 1; }

mkdir -p "$PWD/harness/logs-perf" "$ARCHIVE"

# Preserve the log of whatever ran last under its own name. Called on every exit path that has a log worth
# keeping -- including the failure paths, which are the ones you actually want to read afterwards.
archive_log() {
  [ -f "$LOG" ] || return 0
  command cp "$LOG" "$ARCHIVE/${1:-run}-$(date +%Y%m%d-%H%M%S).log" 2>/dev/null || true
}

# --warp PRE-FLIGHT. The engine matches case-insensitively and asks whether the BOOKMARK CONTAINS THE QUERY
# (StarClientApplication.cpp: b.bookmarkName.toLower().contains(want)) -- NOT the other way round. So --warp
# 'exploring' does not match a bookmark named 'explore', which is exactly the typo that burned three full
# 120s captures: the client loaded the world, failed the lookup, quit, and the script reported the generic
# "client exited during load". Mirror the engine's rule here and fail in milliseconds with the real list.
if [ -n "$WARP" ] && [ -s "$BOOKMARKS" ]; then
  want=$(printf '%s' "$WARP" | tr '[:upper:]' '[:lower:]')
  hits=$(tr '[:upper:]' '[:lower:]' < "$BOOKMARKS" | grep -cF -- "$want" || true)
  if [ "$hits" -eq 0 ]; then
    echo "FAIL: no teleport bookmark contains '$WARP' (matching is case-insensitive substring)."
    echo "  known bookmarks (from the last successful load):"
    sed 's/^/    /' "$BOOKMARKS"
    echo "  This cache is written from a PREVIOUS load and can disagree with the live save in BOTH"
    echo "  directions -- a bookmark added since will be missing, and one DELETED since will still be"
    echo "  listed. Delete $BOOKMARKS and re-run to refresh it."
    exit 2
  fi
  if [ "$hits" -gt 1 ]; then
    echo "WARNING: '$WARP' matches $hits bookmarks; the engine takes the FIRST in its own iteration order,"
    echo "         so the destination is not pinned. Narrow the string to make the capture reproducible:"
    tr '[:upper:]' '[:lower:]' < "$BOOKMARKS" | grep -F -- "$want" | sed 's/^/    /'
  fi
fi

# A stale snapshot reads exactly like a fresh one, and the windowing arithmetic below cannot tell them apart.
rm -rf "$SNAPDIR"
rm -f "$LOG"

# Apply --set BEFORE launch. Configuration::set persists to storage/starbound.config on exit, so the previous
# run's value is sitting in the file -- an A/B that only sets the key on one leg silently inherits it on the
# other. Every leg writes its own value explicitly; none of them relies on what the last run left behind.
#
# THAT RULE USED TO BE A COMMENT AND NOTHING ELSE, AND A COMMENT DOES NOT ENFORCE ANYTHING. On 2026-08-04 an
# adaptive-border A/B ran `--set lightingAdaptiveBorder=false` on one leg and relied on the CODE DEFAULT for
# the other. The default is inert: a pinned key in this file overrides it. So both legs ran with the lever
# OFF and the profile reported a full table of plausible-looking deltas -- every one an artefact of differing
# recompute counts. The tell was lighting.calc.cells identical across the legs, which is the one quantity the
# lever exists to move. Nothing in the tooling objected.
#
# The config is now SNAPSHOT here and RESTORED on every exit path, so a leg cannot inherit the previous leg's
# pin no matter what the caller passes. It does not make an under-specified A/B correct -- it makes it start
# from the shipped baseline instead of from whatever the last experiment left behind.
CFG="$PWD/harness/storage-perf/starbound.config"
CFG_SNAPSHOT="$(mktemp)"
command cp "$CFG" "$CFG_SNAPSHOT" 2>/dev/null || true
restore_cfg() {
  [ -s "$CFG_SNAPSHOT" ] && command cp "$CFG_SNAPSHOT" "$CFG" 2>/dev/null || true
  rm -f "$CFG_SNAPSHOT"
}
trap restore_cfg EXIT

if [ ${#SETS[@]} -gt 0 ]; then
  python3 - "$PWD/harness/storage-perf/starbound.config" "${SETS[@]}" <<'PY'
import json, sys
path, pairs = sys.argv[1], sys.argv[2:]
cfg = json.load(open(path))
for p in pairs:
    k, _, v = p.partition('=')
    try:
        cfg[k] = json.loads(v)          # numbers, bools, null, objects
    except json.JSONDecodeError:
        cfg[k] = v                      # bare string
    print(f"  set {k} = {cfg[k]!r}")
json.dump(cfg, open(path, 'w'), indent=2, sort_keys=True)
PY
fi

echo "=== live profile '$LABEL' -- ${SECONDS_TO_RUN}s, sim RUNNING, warp='${WARP:-<ship>}' ==="

env ${WARP:+STAR_RENDERTEST_WARP="$WARP"} \
  SDL_VIDEO_DRIVER=offscreen \
  SDL_AUDIO_DRIVER=dummy \
  STAR_RENDERTEST_FRAMES=1 \
  STAR_RENDERTEST_NOFREEZE=1 \
  STAR_RENDERTEST_LOAD=${STAR_RENDERTEST_LOAD:-900} \
  STAR_RENDERTEST_QUIESCE=${STAR_RENDERTEST_QUIESCE:-90} \
  STAR_RENDERTEST_WARMUP=100000000 \
  taskset -c 6-15 nice -n 19 "$BIN" -bootconfig "$BOOT" >/dev/null 2>&1 &
PID=$!

# Wait for the LOAD PHASE to end before the measurement window opens -- a window that straddles world load is
# measuring asset streaming, not rendering.
#
# It ends one of two ways, and a live run almost always takes the second. The harness prefers to end the load
# on QUIESCENCE (the entity count holding still for N frames), which is a FROZEN-world idea: with the sim left
# running, entities keep spawning and despawning and the count never holds still, so the load ends on the
# STAR_RENDERTEST_LOAD frame cap instead. The harness logs that at error level and warns the frozen state is
# not reproducible -- true, and irrelevant here, because this instrument does not freeze and does not hash.
# Waiting only for QUIESCED, as the first version of this script did, blocks for the full timeout every run.
for _ in $(seq 1 180); do
  sleep 1
  grep -qE "rendertest\] world (QUIESCED|did NOT settle)" "$LOG" 2>/dev/null && break
  kill -0 $PID 2>/dev/null || {
    # "client exited during load" is true but useless on its own -- it reads like a crash. The commonest cause
    # by far is a --warp that matched nothing, which the engine reports and then quits cleanly. Name it.
    if grep -q "no teleport bookmark matching" "$LOG" 2>/dev/null; then
      echo "FAIL: --warp '$WARP' matched no teleport bookmark. The client loaded the world, could not find it,"
      echo "      and quit. Matching is case-insensitive and asks whether the BOOKMARK CONTAINS THE QUERY."
      echo "  available bookmarks:"
      grep -o "bookmark: '[^']*'" "$LOG" | sed "s/bookmark: //" | sort -u | sed 's/^/    /'
    else
      echo "client exited during load -- see $LOG"
    fi
    archive_log "$LABEL-FAILED"
    exit 1
  }
done
grep -hE "rendertest\] world (QUIESCED|did NOT settle)" "$LOG" \
  | sed 's/^/  /;s/The frozen state is NOT reproducible.*/(expected in a live run -- nothing here is hashed.)/'

# Refresh the pre-flight cache from what this load actually saw. The engine is the authority; this only lets
# the NEXT run reject a bad --warp in milliseconds instead of after a full world load.
grep -o "bookmark: '[^']*'" "$LOG" 2>/dev/null | sed "s/bookmark: '//;s/'$//" | sort -u > "$BOOKMARKS.tmp" || true
[ -s "$BOOKMARKS.tmp" ] && command mv "$BOOKMARKS.tmp" "$BOOKMARKS" || rm -f "$BOOKMARKS.tmp"

# Pin what was actually measured. A capture whose location is not recorded cannot be compared to another one
# later -- and the harness player's position PERSISTS between runs, so "no --warp" does not mean "the ship".
if grep -q "rendertest\] WARPING to bookmark" "$LOG" 2>/dev/null; then
  grep -o "rendertest\] WARPING to bookmark.*" "$LOG" | sed 's/^/  /' | head -1
else
  echo "  location: NOT PINNED (no --warp) -- wherever the harness player was left by the previous run."
fi

# Discard every snapshot taken during load. The values inside are cumulative, so differencing two POST-load
# snapshots yields the post-load window regardless -- but keeping the load-phase files would let --first/--last
# silently select a window that straddles it.
rm -f "$SNAPDIR"/*.json

# Environment sidecar: GPU clock and package temperature bracketing the window. The ~7% drift between distant
# profile runs is currently explained as "thermal state", which is a guess; this makes it checkable. Sampled
# here rather than in the engine because these paths are driver- and platform-specific.
read_gpu_mhz() {
  cat /sys/class/drm/card*/gt_cur_freq_mhz 2>/dev/null | head -1 ||
  cat /sys/class/drm/card*/device/tile0/gt0/freq0/cur_freq 2>/dev/null | head -1 ||
  echo null
}
read_pkg_temp() {
  for h in /sys/class/hwmon/hwmon*; do
    [ "$(cat "$h/name" 2>/dev/null)" = "coretemp" ] || continue
    t=$(cat "$h/temp1_input" 2>/dev/null) && { echo $((t / 1000)); return; }
  done
  echo null
}
GPU_START=$(read_gpu_mhz); TEMP_START=$(read_pkg_temp)

echo "  measuring for ${SECONDS_TO_RUN}s..."
sleep "$SECONDS_TO_RUN"

cat > "$SNAPDIR/../env-sidecar.json" <<EOF
{ "gpuClockMhzStart": ${GPU_START:-null}, "gpuClockMhzEnd": $(read_gpu_mhz),
  "packageTempCStart": ${TEMP_START:-null}, "packageTempCEnd": $(read_pkg_temp) }
EOF

kill -TERM $PID 2>/dev/null
for _ in $(seq 1 20); do kill -0 $PID 2>/dev/null || break; sleep 1; done
kill -KILL $PID 2>/dev/null || true
wait $PID 2>/dev/null || true

# Keep this run's log under its own label before anything else can overwrite it.
archive_log "$LABEL"

n=$(ls "$SNAPDIR"/*.json 2>/dev/null | wc -l)
echo "  $n telemetry snapshots -> $SNAPDIR"
[ "$n" -ge 2 ] || { echo "FAIL: need >=2 snapshots to window; got $n"; exit 1; }

mkdir -p harness/profiles
scripts/telemetry-window.py "$SNAPDIR" --label "$LABEL" --json "harness/profiles/$LABEL.json"
