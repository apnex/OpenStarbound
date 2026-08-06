#!/usr/bin/env bash
# THE MUTUAL CHECK. Two kernel mechanisms measure the same GPU render-engine busy time over one
# window, and have to agree.
#
#   ClientBusyReader  /proc/<pid>/fdinfo, per-client, deduped by drm-client-id   (dist/metrics)
#   the i915 PMU      perf_event_open on rcs0-busy, system-wide                  (scripts/pmu-render-busy.py)
#
# WHY IT IS WORTH A SCRIPT. Every defect this component exists to prevent was caught only when an
# instrument that did NOT share the first one's assumptions disagreed with it: a 4x fdinfo sum caught
# by a ratio exceeding 100%, a 4.04x GPU-timer step caught by a histogram, a "+47% instrument cost"
# caught by replication. None was caught by the oracles, all of which were green throughout, because
# a wrong number is a plausible number. This makes that disagreement a scheduled check instead of a
# lucky one.
#
# The PMU reader is a separate Python implementation ON PURPOSE. Running dist/metrics' own
# EngineBusyReader against dist/metrics' own ClientBusyReader would share the parser, the syscall
# wrapper, the arithmetic and the author -- an instrument checked against itself.
#
#   metrics-mutual-check.sh [pid]     # default target: pgrep -x starbound
#   metrics-mutual-check.sh --selftest
#
# exit 0  the two agreed
# exit 77 the check COULD NOT RUN and said so -- the runner prints SKIP and refuses to count it green.
#         This was exit 0 until 2026-08-05, and scripts/ci/run-gates.sh printed OK for it: a check
#         that compared nothing was counted toward "31/31 gates green". An absent verdict must be
#         spelled differently from a passing one, or the tally lies on the one day it matters.
# exit 1  the two disagreed by more than the bound
# exit 2  the caller asked for a comparison that has no subject (no pid, no binary)
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2

WINDOW=${METRICS_MUTUAL_WINDOW:-8}

# THE BOUND IS MEASURED, NOT CHOSEN. Forty concurrent 8-second windows against a live starbound at
# 03-Surface Outpost (sim running, E-core pinned, 20.9%-25.5% render busy) gave |fdinfo - PMU| of
#
#   min 0.0020pp   median 0.0757pp   mean 0.0935pp   p95 0.2657pp   max 0.2863pp
#
# and the worst was 1.23% of the reading it sat on. 20-25% IS the operating range: the design's own
# five-location table tops out at 23.83%, at the very location these were taken.
#
# 1.00pp is 3.5x the worst of forty. The headroom is deliberate and it is not slack for a defect to
# hide in -- the residual error scales with load, and the term that was NOT varied here is the few
# tens of milliseconds of skew between the two processes' windows, which at a load that moves during
# the window charges a real difference to the instruments. What this exists to catch is a FACTOR,
# not a fraction: the fdinfo dup bug was 4x (+70pp at this load) and the smallest mis-scaling worth
# a name, 2x, is +23pp. Nothing between 0.3pp and 1.0pp is a defect this comparison could name.
#
# A tolerance nobody has watched fire is not known to fire (GATE-TOLERANCE-1, #223), so --selftest
# drives every arm below in both directions, and the first forty runs of this check found a real
# defect in the PMU reader rather than certifying it -- see POLL_SECONDS in pmu-render-busy.py.
BOUND=${METRICS_MUTUAL_BOUND:-0.0100}

# THE PRECONDITION, MEASURED RATHER THAN ASSERTED. The PMU counts the DEVICE and fdinfo counts ONE
# CLIENT, so the two are comparable only while the target is the only process putting work on the
# render engine. That is a `validWhen`, and this component's whole thesis is that a condition stated
# in prose and checked by nobody is how a number comes to mean something other than its name -- so
# the other clients are swept and their share subtracted from nothing, only reported and gated.
#
# 0.20pp is a fifth of the bound, so contamination that survives this arm cannot on its own move the
# comparison past it. An idle GNOME session with Chrome and Xwayland up measured 0.005pp, and all
# forty bound-setting runs above reported 0.0000pp.
OTHERS_CEILING=${METRICS_MUTUAL_OTHERS_CEILING:-0.0020}

# AN AGREEMENT AT IDLE IS NOT AN AGREEMENT. At 0.005% busy a 4x error is 0.015pp, which passes a
# 1.00pp bound comfortably -- the check would be green and blind, which is the unarmed-oracle defect
# this repo has now met four times. Require a load at which the smallest failure worth naming (a 2x
# mis-scaling, +100% of the reading) is at least 10x the bound before believing a pass.
MIN_LOAD=${METRICS_MUTUAL_MIN_LOAD:-0.10}

# Sum of render-engine busy nanoseconds over every DISTINCT drm-client-id that is NOT the target's.
# Deduped by client id for the same reason ClientBusyReader is: a process that dups the DRM device
# gets one fdinfo file per dup, each restating the SAME whole-client total. Chrome is doing it on
# this host right now (client 65 on fds 17 and 18, identical byte-for-byte totals), so a guard that
# summed fds would read the desktop as several times busier than it is and skip runs that were fine.
others_render_ns() {
  local skip="$1"
  grep -sH -e '^drm-client-id:' -e '^drm-engine-render:' /proc/[0-9]*/fdinfo/* 2>/dev/null | awk -F: -v skip="$skip" '
    { split($1, seg, "/"); pid = seg[3] }
    $2 == "drm-client-id"     { cid[$1] = $3 + 0; owner[$1] = pid }
    $2 == "drm-engine-render" { ns[$1] = $3 + 0 }
    END {
      for (p in ns)
        if (p in cid && owner[p] != skip)
          byclient[cid[p]] = ns[p]
      total = 0
      for (c in byclient) total += byclient[c]
      printf "%.0f\n", total
    }'
}

# THE VERDICT, factored out so --selftest can drive every arm without a GPU, a target or root --
# the same reason render-gate.sh factors out oracle_verdict. Takes three ratios, prints the report,
# and returns 0 agree / 1 disagree / 2 could-not-compare.
#
# Each way of NOT comparing gets its own arm and its own sentence. A single "if it did not work,
# skip" would let a future condition inherit a message describing a different one.
mutual_verdict() {
  local client="$1" pmu="$2" others="$3"
  local diff
  diff=$(awk -v a="$client" -v b="$pmu" 'BEGIN{ d = a - b; printf "%.6f", (d < 0 ? -d : d) }')

  printf '  %-22s %s\n' "fdinfo (per-client)" "$(awk -v v="$client" 'BEGIN{printf "%8.4f%%", v*100}')"
  printf '  %-22s %s\n' "i915 PMU (device)" "$(awk -v v="$pmu" 'BEGIN{printf "%8.4f%%", v*100}')"
  printf '  %-22s %s\n' "other GPU clients" "$(awk -v v="$others" 'BEGIN{printf "%8.4f%%", v*100}')"
  printf '  %-22s %s  (bound %s)\n' "|difference|" \
    "$(awk -v v="$diff" 'BEGIN{printf "%8.4fpp", v*100}')" \
    "$(awk -v v="$BOUND" 'BEGIN{printf "%.4fpp", v*100}')"

  if [ "$(awk -v v="$others" -v c="$OTHERS_CEILING" 'BEGIN{print (v > c) ? 1 : 0}')" -eq 1 ]; then
    echo "  DID NOT COMPARE -- another process was using the render engine during the window, so the"
    echo "  DID NOT COMPARE -- device-wide PMU and the target's per-client total are not the same"
    echo "  DID NOT COMPARE -- quantity. Quiesce the desktop and re-run. THIS IS NOT A PASS."
    return 2
  fi
  if [ "$(awk -v v="$client" -v m="$MIN_LOAD" 'BEGIN{print (v < m) ? 1 : 0}')" -eq 1 ]; then
    echo "  DID NOT COMPARE -- the GPU was near idle, and at this load even a 4x reading error stays"
    echo "  DID NOT COMPARE -- inside the bound. Agreement here would prove nothing. Put the target"
    echo "  DID NOT COMPARE -- under real load and re-run. THIS IS NOT A PASS."
    return 2
  fi
  if [ "$(awk -v v="$diff" -v b="$BOUND" 'BEGIN{print (v > b) ? 1 : 0}')" -eq 1 ]; then
    echo "  <-- FAIL: two kernel mechanisms disagree about one physical quantity over one window."
    echo "  <-- One of them is wrong. Suspect the dedup rule first (ClientBusyReader), then the PMU"
    echo "  <-- event's config/unit, then whether the window really was shared."
    return 1
  fi
  echo "  ok -- the per-client and device-wide readers agree within the measured bound"
  return 0
}

if [ "${1:-}" = "--selftest" ]; then
  # Synthetic ratios only: no GPU, no PMU, no target, no root. This is what makes the check
  # registrable as a gate -- the live comparison needs hardware CI does not have, but its verdict
  # arms are pure arithmetic and must be proven to fire wherever the repo is checked out.
  fails=0
  check() {  # $1=description  $2=expected (ok|fail|skip)  $3=client  $4=pmu  $5=others
    local got rc
    mutual_verdict "$3" "$4" "$5" >/dev/null; rc=$?
    case $rc in 0) got=ok ;; 1) got=fail ;; 2) got=skip ;; *) got="rc$rc" ;; esac
    if [ "$got" != "$2" ]; then
      echo "  SELFTEST FAIL: $1 -- expected $2, got $got"; fails=$((fails + 1))
    else
      echo "  ok   ($got, as expected)  $1"
    fi
  }
  echo "=== metrics-mutual-check --selftest: every verdict arm, both directions ==="
  # The first two rows are a real observed pair (batch 2 run 11, the worst of the forty) and the
  # bound's own edge. A tolerance is only known to fire if something has stood on both sides of it.
  check "worst real pair of the forty"        ok   0.232791 0.229928 0.000000
  check "difference exactly AT the bound"     ok   0.240000 0.230000 0.000000
  check "difference just OVER the bound"      fail 0.240001 0.230000 0.000000
  check "fdinfo 4x the PMU (the GPUTIMER-3 bug)" fail 0.932000 0.233000 0.000000
  check "fdinfo 2x the PMU"                   fail 0.466000 0.233000 0.000000
  check "PMU above fdinfo, same magnitude"    fail 0.233000 0.300000 0.000000
  check "idle GPU: agreement proves nothing"  skip 0.000050 0.000048 0.000010
  check "4x error at idle would pass a bound" skip 0.000200 0.000050 0.000010
  check "a busy desktop is not a comparison"  skip 0.233000 0.400000 0.050000
  check "contamination outranks a diff"       skip 0.233000 0.900000 0.050000
  check "idle outranks a diff"                skip 0.000200 0.900000 0.000010
  echo
  [ "$fails" -eq 0 ] && { echo "SELFTEST: PASS"; exit 0; } || { echo "SELFTEST: $fails FAILED"; exit 1; }
fi

PID="${1:-}"
if [ -z "$PID" ]; then
  PID=$(pgrep -x starbound | head -1)
fi
if [ -z "$PID" ] || [ ! -d "/proc/$PID" ]; then
  # NOT a skip, and deliberately not exit 0. A cross-check with no subject is a caller error: the
  # script was asked to compare two readings of a process that is not there. Distinguishing this
  # from "compared and agreed" is the entire contract.
  echo "metrics-mutual-check: no target process. Pass a pid, or start the game:"
  echo "  taskset -c 6-15 nice -n 19 scripts/render-profile.sh 420 mutual --warp '03-Surface Outpost'"
  exit 2
fi
[ -x dist/metrics ] || {
  echo "metrics-mutual-check: no dist/metrics -- build it:"
  echo "  VCPKG_ROOT=/root/vcpkg taskset -c 6-15 nice -n 19 cmake --build build/linux-release-clang --target metrics -j 8"
  exit 2
}

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT

# THE PMU IS PROBED BEFORE THE WINDOW, not after it. An unprivileged host fails this in
# milliseconds; discovering it after an 8-second window would be the same answer, later.
if ! python3 scripts/pmu-render-busy.py --for 0.05 >/dev/null 2>"$TMP/probe.err"; then
  echo "metrics-mutual-check: DID NOT COMPARE -- the i915 PMU is unreadable here:"
  sed 's/^/    /' "$TMP/probe.err"
  echo "  The cross-check DID NOT RUN. Nothing was compared and nothing was verified; this line is"
  echo "  NOT a pass. Re-run with CAP_PERFMON (or as root, or with perf_event_paranoid <= 0) on a"
  echo "  host with an i915 GPU to get a verdict."
  exit 77
fi

echo "=== mutual check: pid $PID, ${WINDOW}s window, two kernel mechanisms ==="

# CONCURRENTLY, over ONE window. Sequential windows were tried first and are wrong: a live scene's
# GPU load moves between two adjacent 8-second windows by more than the quantity being tested for,
# so the workload's own variance gets charged to the instruments and the bound has to be widened
# until it can no longer see a real disagreement.
OTHERS_BEFORE=$(others_render_ns "$PID")
dist/metrics --pid "$PID" --for "$WINDOW" --json >"$TMP/client.json" 2>"$TMP/client.err" &
client_job=$!
python3 scripts/pmu-render-busy.py --for "$WINDOW" >"$TMP/pmu.txt" 2>"$TMP/pmu.err" &
pmu_job=$!
wait $client_job; client_rc=$?
wait $pmu_job;    pmu_rc=$?
OTHERS_AFTER=$(others_render_ns "$PID")

# Exit 3 is "could not measure" in both tools -- the target exited, lost its DRM fds, or the PMU
# went away mid-window. Any OTHER non-zero code is the instrument itself being broken, which is a
# finding rather than an environment, so it goes red instead of skipping.
if [ "$client_rc" -eq 3 ] || [ "$pmu_rc" -eq 3 ]; then
  echo "  DID NOT COMPARE -- a reader became unavailable during the window:"
  sed 's/^/    /' "$TMP/client.err" "$TMP/pmu.err" 2>/dev/null | grep -v '^    $' || true
  echo "  The cross-check DID NOT RUN. THIS IS NOT A PASS."
  exit 77
fi
if [ "$client_rc" -ne 0 ] || [ "$pmu_rc" -ne 0 ]; then
  echo "  <-- FAIL: an instrument failed outright (dist/metrics exit $client_rc, PMU reader exit $pmu_rc)"
  sed 's/^/    /' "$TMP/client.err" "$TMP/pmu.err" 2>/dev/null || true
  exit 1
fi

CLIENT=$(python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for s in d["samples"]:
    if s["key"] == "gpu.engine.render.busy_ratio":
        print("%.9f" % s["value"]); break
else:
    sys.exit("no gpu.engine.render.busy_ratio in the metrics output")
' "$TMP/client.json") || { echo "  <-- FAIL: dist/metrics --json produced no render busy ratio"; exit 1; }
PMU=$(cat "$TMP/pmu.txt")

# Wall clock comes from dist/metrics, which reports the window it actually slept. Both processes
# were started from the same line and are within a few tens of milliseconds of each other; that
# residual skew is the term the bound's headroom is for.
WALL_NS=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["wall_ns"])' "$TMP/client.json")
OTHERS=$(awk -v a="$OTHERS_BEFORE" -v b="$OTHERS_AFTER" -v w="$WALL_NS" \
  'BEGIN{ d = b - a; printf "%.9f", (d > 0 && w > 0) ? d / w : 0 }')

mutual_verdict "$CLIENT" "$PMU" "$OTHERS"
case $? in
  0) echo; echo "MUTUAL: PASS"; exit 0 ;;
  1) echo; echo "MUTUAL: FAIL"; exit 1 ;;
  *) echo; echo "MUTUAL: DID NOT RUN (not a pass)"; exit 77 ;;
esac
