#!/bin/bash
# THE MOTION GATE (#174, guardrail G9). Boots the game offscreen on the real GPU with the sim LIVE, drives a
# deterministic scripted walk, and certifies the motion-only paths that the frozen gate is structurally blind
# to.
#
# WHY IT IS A SEPARATE INSTRUMENT FROM render-gate.sh, and this was the decision #174 demanded be made
# deliberately rather than drifted into: the byte-identity gate FREEZES the world so its input is
# deterministic, and a frozen world does not tick, so the player cannot move in it at all. Motion is only
# expressible in the unfrozen run. What this gate asserts is therefore a COUNTER invariant, not a pixel hash.
#
# IT USES THE PERF BOOTCONFIG ON PURPOSE. harness/storage/starbound.config (the byte-identity gate's) sets
# parallaxOracle=true, and BackdropPass deliberately pins the cache path while an oracle is on -- "oracle must
# stay on the cache path to gate it" -- so the moving-camera bypass can NEVER engage there. Running the walk
# against the gate's config reports bypassed_moving=0 forever and looks exactly like a renderer that has
# stopped bypassing. That is not hypothetical; it is what the first run of this instrument did, and the oracle
# correctly refused it. harness/sbinit-perf.config has the oracles off.
#
# Usage: render-motion.sh [extra env assignments...]
#   render-motion.sh
#   render-motion.sh STAR_RENDERTEST_WARP=base
set -u
cd /root/frackin/OpenStarbound

BIN=dist/starbound
LOG=harness/logs-perf/starbound.log

# Same two staleness questions the byte-identity gate asks, for the same reason: a failed build leaves the
# binary untouched, so everything a run then tells you is a fact about the PREVIOUS binary. See #178.
[ -x "$BIN" ] || { echo "REFUSING TO CERTIFY: no executable at $BIN."; exit 1; }
stale=$(find source -path source/test -prune -o -type f \
          \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name 'CMakeLists.txt' \) \
          -newer "$BIN" -print 2>/dev/null | sort | head -20)
if [ -n "$stale" ]; then
  echo "REFUSING TO CERTIFY: these sources are newer than $BIN -- the build never ran, or it FAILED:"
  echo "$stale" | sed 's/^/    /'
  exit 1
fi

[ -f "$LOG" ] && rm -f "$LOG"

# WALK=45 gives four 45-frame legs: right, pause, left, pause. The pauses are not padding -- the still<->moving
# TRANSITION is where ParkFrames hysteresis and the compose arm-switch live, and a harness that only walked
# would exercise one arm and call it coverage.
#
# TOGGLE=antiAliasing=90 flips AA twice per walk cycle. Every flip DESTROYS AND REBUILDS every framebuffer with
# undefined content -- the churn the frameBufferGeneration guard exists for and the GL-state audit (#139) was
# built to watch. Motion plus realloc is the combination no frozen run can produce.
env "$@" \
  SDL_VIDEO_DRIVER=offscreen \
  STAR_RENDERTEST_FRAMES=${STAR_RENDERTEST_FRAMES:-240} \
  STAR_RENDERTEST_QUIESCE=${STAR_RENDERTEST_QUIESCE:-90} \
  STAR_RENDERTEST_WALK=${STAR_RENDERTEST_WALK:-45} \
  STAR_RENDERTEST_TOGGLE=${STAR_RENDERTEST_TOGGLE:-antiAliasing=90} \
  taskset -c 6-15 nice -n 19 "$BIN" -bootconfig /root/frackin/OpenStarbound/harness/sbinit-perf.config >/dev/null 2>&1

if [ ! "$LOG" -nt "$BIN" ]; then
  echo "REFUSING TO CERTIFY: log is not newer than the binary -- the run did not happen."
  exit 1
fi

echo "certifying $BIN  ($(date -r "$BIN" '+%Y-%m-%d %H:%M:%S'))"
pass=1

echo "=== motion ==="
# Did the driver actually move the player? A walk that issues moves the world ignores is indistinguishable, in
# every counter downstream, from a renderer that has stopped bypassing -- so prove the input first and read the
# counters second. Distinct player X positions across the run is the proof.
legs=$(grep -c "\[walk\] frame=" "$LOG" || true)
positions=$(grep -oE "player=\([-0-9.]+" "$LOG" | sort -u | wc -l)
printf "  walk legs: %-6s distinct player positions: %-6s" "$legs" "$positions"
if [ "$legs" -lt 4 ] || [ "$positions" -lt 3 ]; then
  echo "  <-- FAIL"
  pass=0
  echo "    ! the scripted walk did not move the player. Everything below is then meaningless: the counters"
  echo "    ! would report a still camera correctly, and the run would look like a bypass regression."
else
  echo "  ok"
fi

echo "=== walk oracle ==="
# The deliverable. refreshed + skipped + bypassed_moving partition the frames the parallax pass ran, and
# bypassed_moving > 0 is the assertion that was structurally unreachable before this instrument existed.
grep "\[walkoracle\] parallax arms" "$LOG" | sed 's/^[^]]*] *\[Info\] *//' | sed 's/^/  /'
oracleFail=$(grep -c "\[walkoracle\] FAIL" "$LOG" || true)
oraclePass=$(grep -c "\[walkoracle\] PASS" "$LOG" || true)
printf "  verdict: PASS=%-4s FAIL=%-4s" "$oraclePass" "$oracleFail"
if [ "$oracleFail" -ne 0 ] || [ "$oraclePass" -eq 0 ]; then
  echo "  <-- FAIL"
  pass=0
  grep "\[walkoracle\] FAIL" "$LOG" | sed 's/^/    ! /'
else
  echo "  ok"
fi

echo "=== gl state (under motion + FBO realloc) ==="
# This is why #139 was sequenced first. A moving camera plus an antiAliasing flip generates exactly the
# ambient-state churn the audit was built to catch, and no frozen run can generate it.
desync=$(grep -c "^\[.*\] \[Error\] \[glstate\]" "$LOG" || true)
printf "  gl-state desyncs: %-6s" "$desync"
if [ "$desync" -ne 0 ]; then
  echo "  <-- FAIL"
  pass=0
  grep -m2 "\[glstate\]" "$LOG" | sed 's/^/    ! /'
else
  echo "  ok"
fi

echo "=== GL errors ==="
glerr=$(grep -cE "GL_INVALID|GL_OUT_OF_MEMORY|GL_STACK_(UNDER|OVER)FLOW|<UNRECOGNIZED GL ERROR>" "$LOG" || true)
printf "  GL error lines: %-6s" "$glerr"
if [ "$glerr" -ne 0 ]; then
  echo "  <-- FAIL"; pass=0
  grep -oE "OpenGL errors [a-z ]+" "$LOG" | sort | uniq -c | sed 's/^/    ! /'
else
  echo "  ok"
fi

echo "=== config restored ==="
# A motion run writes config keys, and Configuration::set PERSISTS on exit. Leaving antiAliasing flipped would
# poison every later run from this install -- the pinning trap that has bitten this campaign twice.
restored=$(grep -c "\[walk\] restored " "$LOG" || true)
printf "  keys restored: %-6s" "$restored"
if [ "$restored" -eq 0 ]; then
  echo "  <-- FAIL"
  pass=0
  echo "    ! the run wrote a config key and did not put it back; the harness config is now pinned."
else
  echo "  ok"
fi

echo
[ "$pass" -eq 1 ] && echo "MOTION GATE: PASS" || { echo "MOTION GATE: FAIL"; exit 1; }
