#!/bin/bash
# THE RENDER GATE. Boots the game offscreen on the real GPU, freezes a world, runs the three
# in-frame oracles, and certifies. Never trust a grep for DIFF=0 alone -- an UNARMED oracle
# prints SKIPPED and greps as a pass. MATCH must be > 0 too.
#
# Usage: gate.sh [extra env assignments...]
#   gate.sh                                  # oracles only
#   gate.sh STAR_RENDERTEST_AB='key=A|B'     # + in-process A/B of two code paths
set -u
cd /root/frackin/OpenStarbound

BIN=dist/starbound
LOG=harness/logs/starbound.log

# A stale log reads exactly like a passing one. Refuse to certify against a binary the log predates.
[ -f "$LOG" ] && rm -f "$LOG"

env "$@" \
  SDL_VIDEO_DRIVER=offscreen \
  STAR_RENDERTEST_FRAMES=${STAR_RENDERTEST_FRAMES:-30} \
  STAR_RENDERTEST_QUIESCE=${STAR_RENDERTEST_QUIESCE:-90} \
  taskset -c 6-15 nice -n 19 "$BIN" -bootconfig /root/frackin/OpenStarbound/harness/sbinit.config >/dev/null 2>&1

if [ ! "$LOG" -nt "$BIN" ]; then
  echo "REFUSING TO CERTIFY: log is not newer than the binary -- the run did not happen."
  exit 1
fi

echo "=== oracles ==="
# The three oracles do NOT share a vocabulary. envoracle/spreadoracle say MATCH; paralloracle says
# EXACT. A grep for "MATCH" scores paralloracle 0/0/0 and prints DIFF=0 -- a screaming oracle read as
# a clean one. Match on what each one ACTUALLY writes, and require a nonzero pass count: an oracle that
# said nothing at all is not a pass, it is an oracle that never ran.
pass=1
for o in envoracle paralloracle spreadoracle; do
  total=$(grep -c "\[$o\]" "$LOG")
  ok=$(grep -cE "\[$o\] (MATCH|EXACT)" "$LOG")
  bad=$(grep -cE "\[$o\] (DIFF|diff=)" "$LOG")
  skip=$(grep -cE "\[$o\] SKIPPED" "$LOG")
  printf "  %-14s ran=%-5s pass=%-5s DIFF=%-5s SKIPPED=%-5s" "$o" "$total" "$ok" "$bad" "$skip"
  if [ "$bad" -ne 0 ] || [ "$ok" -eq 0 ]; then echo "   <-- FAIL"; pass=0; else echo "   ok"; fi
done
grep -hoE "\[(envoracle|paralloracle|spreadoracle)\] (diff|DIFF)=[^ ]* [^ ]*" "$LOG" | sort -u | head -3 | sed 's/^/    ! /'

echo "=== GL errors ==="
grep -oE "GL error summary.*" "$LOG" | tail -2 | sed 's/^/  /'
grep -cE "GL_INVALID" "$LOG" | sed 's/^/  GL_INVALID lines: /'

if grep -q "RENDERTEST_AB" "$LOG"; then
  echo "=== in-process A/B ==="
  grep -E "renderTest.*(A/B|hashA|hashB|IDENTICAL|DIFFER)" "$LOG" | sed 's/^/  /'
fi

echo
[ "$pass" -eq 1 ] && echo "GATE: PASS" || { echo "GATE: FAIL"; exit 1; }
