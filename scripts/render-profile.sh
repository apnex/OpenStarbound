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
LOG="$PWD/harness/logs-perf/starbound.log"

[ -x "$BIN" ] || { echo "no $BIN -- build first"; exit 1; }

# A stale snapshot reads exactly like a fresh one, and the windowing arithmetic below cannot tell them apart.
rm -rf "$SNAPDIR" "$LOG"
mkdir -p "$PWD/harness/logs-perf"

# Apply --set BEFORE launch. Configuration::set persists to storage/starbound.config on exit, so the previous
# run's value is sitting in the file -- an A/B that only sets the key on one leg silently inherits it on the
# other. Every leg writes its own value explicitly; none of them relies on what the last run left behind.
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
  kill -0 $PID 2>/dev/null || { echo "client exited during load -- see $LOG"; exit 1; }
done
grep -hE "rendertest\] world (QUIESCED|did NOT settle)" "$LOG" \
  | sed 's/^/  /;s/The frozen state is NOT reproducible.*/(expected in a live run -- nothing here is hashed.)/'

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

n=$(ls "$SNAPDIR"/*.json 2>/dev/null | wc -l)
echo "  $n telemetry snapshots -> $SNAPDIR"
[ "$n" -ge 2 ] || { echo "FAIL: need >=2 snapshots to window; got $n"; exit 1; }

mkdir -p harness/profiles
scripts/telemetry-window.py "$SNAPDIR" --label "$LABEL" --json "harness/profiles/$LABEL.json"
