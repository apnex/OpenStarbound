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

# THE WARP MUST PRECEDE THE LOAD END, and until #238 nothing here read the order. The warp cannot be issued
# until the client reaches inWorld(); if the load phase ends before that, the engine declares the world
# settled, this script purges snapshots and opens its window, and the warp plus an entire destination-world
# load then happen INSIDE it -- reported as render cost. The engine now refuses that at the cause. This is the
# INDEPENDENT read of the same fact, and it is the one that still fires against a binary predating the fix.
#
# Pure: takes a log path and reads nothing else, so --selftest can drive all three verdicts. A missing load-end
# line is `ok` here rather than a failure -- a different check owns that, and an instrument that reports another
# instrument's defect under its own name is how a red gets attributed to the wrong cause.
warp_order_verdict() {
  local log="$1" warp_at load_at
  warp_at=$(grep -nE "rendertest\] WARPING to bookmark" "$log" 2>/dev/null | head -1 | cut -d: -f1)
  load_at=$(grep -nE "rendertest\] world (QUIESCED|did NOT settle)" "$log" 2>/dev/null | head -1 | cut -d: -f1)
  [ -n "$warp_at" ] || { echo never; return; }
  [ -n "$load_at" ] || { echo ok; return; }
  if [ "$warp_at" -lt "$load_at" ]; then echo ok; else echo late; fi
}

if [ "${1:-}" = "--selftest" ]; then
  t=$(mktemp -d) || exit 2
  printf 'x\n[rendertest] WARPING to bookmark a\ny\n[rendertest] world QUIESCED after 300 frames\n' > "$t/ok.log"
  printf 'x\n[rendertest] world did NOT settle: hit the cap\ny\n[rendertest] WARPING to bookmark a\n' > "$t/late.log"
  printf 'x\n[rendertest] world QUIESCED after 300 frames\n' > "$t/never.log"
  rc=0
  for arm in ok late never; do
    got=$(warp_order_verdict "$t/$arm.log")
    if [ "$got" = "$arm" ]; then
      printf '  ok   %-5s -> %s\n' "$arm" "$got"
    else
      printf '  FAIL %-5s -> %s\n' "$arm" "$got"; rc=1
    fi
  done
  rm -rf "$t"
  [ $rc -eq 0 ] && echo "  render-profile warp-order selftest: 3/3 arms ok -- ordered, reversed and absent are three distinct verdicts"
  exit $rc
fi

SECONDS_TO_RUN=${1:-90}
# How many snapshot intervals the window spans. Declared here and passed to the consumer, so the
# window shape is chosen rather than inherited from however many files happened to exist.
# DERIVED FROM THE REQUESTED DURATION AND THE ENGINE'S OWN CADENCE, not defaulted to 1. Snapshots are
# emitted every `telemetryReportInterval` SIMULATED seconds, so the number of intervals a run of
# SECONDS_TO_RUN can supply is (seconds / cadence) minus the three files the windowing trims (the
# first, the last, and the interval's own second endpoint). Hardcoding 1 here would have turned a
# 90-second leg into a ~20-second one while still calling it 90 -- the exact class of quiet mismatch
# this whole pass exists to remove.
REPORT_INTERVAL=$(python3 -c "import json;print(json.load(open('harness/storage-perf/starbound.config')).get('telemetryReportInterval',5))")
INTERVALS=${STAR_PROFILE_INTERVALS:-$(( SECONDS_TO_RUN / REPORT_INTERVAL - 3 ))}
[ "$INTERVALS" -ge 1 ] || INTERVALS=1
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

# A binary that merely EXISTS is not a binary that contains the sources on disk. A failed build
# leaves the old one in place, and every number below would then describe it while wearing this
# run's label. render-gate.sh has refused on this basis since #178; the profiler did not.
scripts/assert-binary-fresh.sh "$BIN" || exit 1

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
loaded=0
for _ in $(seq 1 180); do
  sleep 1
  grep -qE "rendertest\] world (QUIESCED|did NOT settle)" "$LOG" 2>/dev/null && { loaded=1; break; }
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
# THE LOOP COULD TIME OUT AND SAY NOTHING. There was no check that the pattern was ever seen: the
# grep below discards its status, the script sets only `set -u` (no -e, no pipefail), and execution
# fell straight through to the snapshot purge and the measurement sleep. A leg whose load end was
# never observed would then window across the world load -- asset streaming reported as render cost.
# The budget is 180s against the matrix's own 120s estimate, i.e. 1.5x headroom, so this is not a
# theoretical path.
if [ "$loaded" != 1 ]; then
  echo "FAIL: the world-load end was never observed within 180s. This leg would have measured across"
  echo "      the load. Refusing to report a number for it."
  archive_log "$LABEL-NOLOAD"
  kill -TERM $PID 2>/dev/null; exit 1
fi
grep -hE "rendertest\] world (QUIESCED|did NOT settle)" "$LOG" \
  | sed 's/^/  /;s/The frozen state is NOT reproducible.*/(expected in a live run -- nothing here is hashed.)/'

# Refresh the pre-flight cache from what this load actually saw. The engine is the authority; this only lets
# the NEXT run reject a bad --warp in milliseconds instead of after a full world load.
grep -o "bookmark: '[^']*'" "$LOG" 2>/dev/null | sed "s/bookmark: '//;s/'$//" | sort -u > "$BOOKMARKS.tmp" || true
[ -s "$BOOKMARKS.tmp" ] && command mv "$BOOKMARKS.tmp" "$BOOKMARKS" || rm -f "$BOOKMARKS.tmp"

# Pin what was actually measured. A capture whose location is not recorded cannot be compared to another one
# later -- and the harness player's position PERSISTS between runs, so "no --warp" does not mean "the ship".
if [ -n "$WARP" ]; then
  case "$(warp_order_verdict "$LOG")" in
    never)
      echo "FAIL: --warp '$WARP' was requested and no 'WARPING to bookmark' line was ever logged. The load"
      echo "      ended somewhere nobody chose, and the fingerprint would be perfectly self-consistent there."
      archive_log "$LABEL-NOWARP"; kill -TERM $PID 2>/dev/null; exit 1 ;;
    late)
      echo "FAIL: the warp was issued AFTER the load ended. The destination world streamed in inside the"
      echo "      measurement window, so this leg would report asset streaming as render cost."
      archive_log "$LABEL-WARPLATE"; kill -TERM $PID 2>/dev/null; exit 1 ;;
  esac
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

# BOUND THE LEG BY SNAPSHOTS, NOT BY WALL CLOCK. Snapshots are emitted every `telemetryReportInterval`
# of SIMULATED time -- StarClientApplication feeds the report timer a fixed GlobalTimestep, not a wall
# delta -- while this script used to `sleep` a wall-clock duration. The main loop caps catch-up at
# maxFrameSkip, so a leg that cannot hold 60Hz falls behind wall time permanently and emits FEWER
# snapshots for the same sleep. The window length then moved with the very thing under measurement.
#
# Waiting for a snapshot COUNT puts the bound on the same clock as the cadence. The wall-clock limit
# below is a SAFETY STOP, not the measurement bound: reaching it is a failure, not a shorter run.
NEED_SNAPS=$(( INTERVALS + 3 ))     # first and last are trimmed; +1 for the interval itself
WALL_LIMIT=$(( SECONDS_TO_RUN * 4 + 60 ))
echo "  measuring until $NEED_SNAPS snapshots exist (safety stop ${WALL_LIMIT}s)..."
waited=0
while [ "$(ls "$SNAPDIR"/*.json 2>/dev/null | wc -l)" -lt "$NEED_SNAPS" ]; do
  sleep 1; waited=$((waited + 1))
  if ! kill -0 $PID 2>/dev/null; then
    echo "FAIL: the client exited before $NEED_SNAPS snapshots were written"; archive_log "$LABEL-DIED"; exit 1
  fi
  if [ "$waited" -ge "$WALL_LIMIT" ]; then
    echo "FAIL: only $(ls "$SNAPDIR"/*.json 2>/dev/null | wc -l) of $NEED_SNAPS snapshots after ${waited}s."
    echo "      The sim is running far behind wall clock -- this leg is not comparable to one that"
    echo "      kept up, and a short window is not a valid substitute for the declared one."
    archive_log "$LABEL-SLOW"; kill -TERM $PID 2>/dev/null; exit 1
  fi
done
echo "  $NEED_SNAPS snapshots after ${waited}s wall (declared window: $INTERVALS interval(s))"

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

# ARCHIVE THE WHOLE WINDOW, NOT JUST ITS TWO ENDPOINTS.
#
# Two `rm` calls above are correct and stay: line ~148 clears a stale SNAPDIR before launch, because a
# stale snapshot reads exactly like a fresh one, and the post-load purge drops the load phase so
# --first/--last cannot select a window straddling it. But NOTHING ever copied the survivors out, so the
# next leg's pre-launch wipe destroyed them. Verified against matrix-20260807-160513: of the ~15
# snapshots inside the r1-baseline window, ZERO remain on disk -- only the final leg of a run ever
# survived, and only until the next run started.
#
# What was thrown away is the series itself. The files are CUMULATIVE, so differencing CONSECUTIVE pairs
# yields a per-interval reading for every declared metric -- including the histogram buckets, so p99 over
# time -- while telemetry-window differences only files[lo] and files[hi]. Its own meta says so:
# `"intervals": 15, "windowIndices": [1, 16]`. It counts fifteen and keeps two. This copy is the whole
# difference between that and a plottable stream, and it costs ~1MB per leg.
#
# COPIED, NOT MOVED, and copied BEFORE the window runs: telemetry-window reads SNAPDIR, and a leg whose
# archive step failed must still produce its profile. The archive is an addition to this script's output,
# never a precondition of it.
SNAPARCHIVE="harness/profiles/$LABEL.snapshots"
rm -rf "$SNAPARCHIVE"; mkdir -p "$SNAPARCHIVE"
# -p, so the mtimes survive the copy. telemetry-window now prefers each snapshot's own tEpochNs and only
# falls back to mtime, but every snapshot captured before that field existed has nothing else -- and an
# archive that silently restamped them to the moment of copying would put a whole leg at the wrong place
# on the axis, uniformly, which is the hardest kind of wrong to notice.
command cp -p "$SNAPDIR"/*.json "$SNAPARCHIVE"/ 2>/dev/null || true
a=$(ls "$SNAPARCHIVE"/*.json 2>/dev/null | wc -l)
if [ "$a" -eq "$n" ]; then
  echo "  $a snapshots archived -> $SNAPARCHIVE/"
else
  # Loud rather than silent. An archive short of the window it claims to hold would produce a series with
  # a hole in it, and a hole in a cumulative series does not look like a hole -- it looks like one long
  # interval that did more work.
  echo "  WARNING: archived $a of $n snapshots to $SNAPARCHIVE/ -- the series for this leg is INCOMPLETE"
fi

# BOTH ARTEFACTS, from ONE traversal of one directory. --json is the verdict about the chosen window;
# --series is every interval under it, individually stamped. Emitting the series here rather than leaving
# it to be re-derived later is what makes it directly plottable, which is the whole ask -- and the raw
# snapshots are archived above regardless, so the series stays RE-DERIVABLE rather than becoming the only
# copy of anything.
scripts/telemetry-window.py "$SNAPDIR" --intervals "$INTERVALS" --label "$LABEL" \
  --json "harness/profiles/$LABEL.json" \
  --series "harness/profiles/$LABEL.series.json"
