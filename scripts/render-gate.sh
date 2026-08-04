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

# Which oracles are allowed a nonzero difference, and how much. envoracle and spreadoracle are exact by
# construction. paralloracle is not: see the long note at its call site in BackdropPass, and the block
# below that reads it.
oracle_tolerance() {
  case "$1" in
    paralloracle) echo 0.00392157 ;;   # 1/255: the 8-bit LSB
    *)            echo 0 ;;
  esac
}

# Factored out of the loop ONLY so --selftest can drive it with synthetic logs. A tolerance that has never
# been watched to fire is not known to fire, and this file already carries three warnings about checks that
# report without gating; a fourth arm added on trust would be the same mistake wearing a number.
oracle_verdict() {
  local o="$1" log="$2" tol total ok bad skip worst
  tol=$(oracle_tolerance "$o")
  total=$(grep -c "\[$o\]" "$log")
  ok=$(grep -cE "\[$o\] (MATCH|EXACT)" "$log")
  bad=$(grep -cE "\[$o\] (DIFF|diff=)" "$log")
  skip=$(grep -cE "\[$o\] SKIPPED" "$log")
  worst=$(grep -oE "\[$o\] .*maxAbs=[0-9.]+" "$log" | sed 's/.*maxAbs=//' | sort -g | tail -1)
  printf "  %-14s ran=%-5s pass=%-5s DIFF=%-5s SKIPPED=%-5s worst=%-9s" \
    "$o" "$total" "$ok" "$bad" "$skip" "${worst:-none}"
  # Spelled out arm by arm rather than folded into one condition: every way of NOT being clean has to land
  # on an explicit failure, or the next tolerance someone adds acquires a silent-pass hole.
  if [ "$total" -eq 0 ]; then
    echo "   <-- FAIL: never ran"; return 1
  elif [ $((ok + bad)) -eq 0 ]; then
    echo "   <-- FAIL: nothing but SKIPPED -- the oracle was never armed"; return 1
  elif [ "$bad" -eq 0 ]; then
    echo "   ok"; return 0
  elif [ "$tol" = "0" ]; then
    echo "   <-- FAIL: zero-diff oracle reported a difference"; return 1
  elif [ -z "$worst" ]; then
    echo "   <-- FAIL: reported a difference with no maxAbs to judge it by"; return 1
  elif [ "$(awk -v w="$worst" -v t="$tol" 'BEGIN{print (w>t)?1:0}')" -eq 1 ]; then
    echo "   <-- FAIL: maxAbs $worst exceeds the $tol tolerance -- blend/compose bug, not rounding"; return 1
  else
    echo "   ok (within $tol)"; return 0
  fi
}

if [ "${1:-}" = "--selftest" ]; then
  tmp=$(mktemp) || exit 1
  trap 'rm -f "$tmp"' EXIT
  fails=0
  check() {   # $1=description  $2=expected verdict (ok|fail)  $3=oracle  $4=synthetic log body
    printf '%s\n' "$4" > "$tmp"
    if oracle_verdict "$3" "$tmp" >/dev/null; then got=ok; else got=fail; fi
    if [ "$got" != "$2" ]; then
      echo "  SELFTEST FAIL: $1 -- expected $2, got $got"; fails=$((fails + 1))
    else
      echo "  ok   ($got, as expected)  $1"
    fi
  }
  echo "=== render-gate --selftest: every verdict arm, both directions ==="
  check "clean paralloracle"                 ok   paralloracle "[paralloracle] EXACT (0 diff) N=4"
  check "paralloracle diff INSIDE tolerance" ok   paralloracle "[paralloracle] diff=738507 maxAbs=0.00073 first=(1,2) N=4"
  check "paralloracle diff OVER tolerance"   fail paralloracle "[paralloracle] diff=12 maxAbs=0.05000 first=(1,2) N=4"
  check "paralloracle diff with no maxAbs"   fail paralloracle "[paralloracle] diff=12 first=(1,2) N=4"
  check "paralloracle never ran"             fail paralloracle "[somethingelse] EXACT"
  check "paralloracle only SKIPPED"          fail paralloracle "[paralloracle] SKIPPED (absent fbo) N=4"
  check "clean spreadoracle"                 ok   spreadoracle "[spreadoracle] MATCH"
  check "zero-tolerance oracle, any diff"    fail spreadoracle "[spreadoracle] MATCH
[spreadoracle] DIFF=1 maxAbs=0.00001"
  echo
  [ "$fails" -eq 0 ] && { echo "SELFTEST: PASS"; exit 0; } || { echo "SELFTEST: $fails FAILED"; exit 1; }
fi

# THE STALENESS GUARD, AND IT WAS ALREADY WRONG ONCE. It used to assert one thing only -- that the log
# is newer than the binary -- which a FAILED BUILD satisfies trivially: the compile errors out, the
# binary never moves, this script deletes the log and writes a fresh one, and the gate certifies code
# that was never compiled. That fired TWICE in one session (1e46f71c) and was caught by a human reading
# mtimes by hand, not by the gate. Every byte-identity claim made through here inherits that hole. #178
#
# Two questions, and the honest gate has to ask both:
#   1. did the BUILD happen?   the binary is newer than every source it is built from
#   2. did the RUN happen?     the log is newer than the binary
#
# (1) runs FIRST, before the game boots, so a stale binary costs milliseconds instead of a 30-frame GPU
# run. There is deliberately no override: an escape hatch here is the defect.
[ -x "$BIN" ] || { echo "REFUSING TO CERTIFY: no executable at $BIN."; exit 1; }

# dist/ IS the CMake runtime output directory (source/CMakeLists.txt:574), so $BIN's mtime is its link
# time -- not a copy's. source/test is pruned: it builds the test binaries, not the game.
stale=$(find source -path source/test -prune -o -type f \
          \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name 'CMakeLists.txt' \) \
          -newer "$BIN" -print 2>/dev/null | sort | head -20)
if [ -n "$stale" ]; then
  echo "REFUSING TO CERTIFY: these sources are newer than $BIN -- the build never ran, or it FAILED:"
  echo "$stale" | sed 's/^/    /'
  echo "  Rebuild, CONFIRM THE LINK SUCCEEDED, then re-run. A failed build leaves the binary untouched,"
  echo "  so everything this gate would then tell you is a fact about the PREVIOUS binary."
  exit 1
fi

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

# Name the binary that was certified. A verdict that does not say what it certified is a verdict
# somebody will later attach to a different binary.
echo "certifying $BIN  ($(date -r "$BIN" '+%Y-%m-%d %H:%M:%S'))"

echo "=== oracles ==="
# The three oracles do NOT share a vocabulary. envoracle/spreadoracle say MATCH; paralloracle says
# EXACT. A grep for "MATCH" scores paralloracle 0/0/0 and prints DIFF=0 -- a screaming oracle read as
# a clean one. Match on what each one ACTUALLY writes, and require a nonzero pass count: an oracle that
# said nothing at all is not a pass, it is an oracle that never ran.
#
# NOT EVERY ORACLE IS A ZERO-DIFF ORACLE, AND READING THEM AS IF THEY WERE MADE THIS GATE CRY WOLF.
# paralloracle is a BOUNDED-diff gate by construction -- BackdropPass says so at the call site and in the
# line it emits ("<=~1 LSB expected: premult double-rounding"), because the premultiplied parallax cache
# double-rounds every partial-alpha texel. This loop scored any `diff=` as red, so the oracle's declared
# tolerance existed only as prose and the gate asserted something stricter than the code it watches.
#
# It went unnoticed because the harness runs at one location by default, and that scene has almost no
# parallax: 26/26 EXACT there, but 0/25 at a daylit surface base, all at maxAbs 0.00073 -- under a fifth
# of the 8-bit LSB the oracle names as its own noise. Warping to the Director's real bases is what exposed
# it. maxAbs is the discriminator the code nominates ("A large maxAbs would flag a real blend/compose bug
# rather than the rounding"), so judge on maxAbs, and keep zero tolerance for the two oracles that really
# are exact.
pass=1
# gatheroracle is armed in harness/storage, so it always runs here and the never-ran arm stays meaningful.
# It only ever exercises the cache-HIT path under this gate -- a frozen camera does not scroll -- and that
# is still the claim most worth checking, because a hit ships a grid gathered on some earlier frame.
for o in envoracle gatheroracle paralloracle spreadoracle; do
  oracle_verdict "$o" "$LOG" || pass=0
done
grep -hoE "\[(envoracle|paralloracle|spreadoracle)\] (diff|DIFF)=[^ ]* [^ ]*" "$LOG" | sort -u | head -3 | sed 's/^/    ! /'

echo "=== GL errors ==="
# This block used to COUNT and PRINT without asserting -- it happily printed "GL_INVALID lines: 0" for
# weeks and would have printed a non-zero count just as happily, with the verdict still PASS. Same defect
# class as the oracle-vocabulary trap: a check that reports but does not gate is not a gate.
#
# Match every GL error the renderer can name (StarRenderer_opengl.cpp logGlErrorSummary), not just
# GL_INVALID* -- GL_OUT_OF_MEMORY and the stack errors were being counted as clean.
glerr=$(grep -cE "GL_INVALID|GL_OUT_OF_MEMORY|GL_STACK_(UNDER|OVER)FLOW|<UNRECOGNIZED GL ERROR>" "$LOG" || true)
printf "  GL error lines: %-6s" "$glerr"
if [ "$glerr" -ne 0 ]; then
  echo "  <-- FAIL"
  pass=0
  # Which drain caught them tells you WHERE: "this frame" is in-frame, "destroying effects/targets" is
  # teardown, "setting effect config" is load. Show the distinct prefixes rather than a raw count.
  grep -oE "OpenGL errors [a-z ]+" "$LOG" | sort | uniq -c | sed 's/^/    ! /'
else
  echo "  ok"
fi

echo "=== contract violations ==="
# GUARDRAIL G6 (#181): a counter no verdict reads is not a signal. render.backdrop.compose_recovered
# existed for a week, incremented by a real detector, and appeared in ZERO scripts, tests and docs --
# observable only by someone who already suspected the fault. The gate's own comment states the rule it
# was breaking: "a check that reports but does not gate is not a gate."
#
# Asserted on the LOG LINE, not the counter, and that is a limitation worth naming: telemetry counters
# never reach the harness log, so the gate cannot read them. The line is rate-limited to 4 per world
# entry, which is plenty -- any occurrence at all must fail the run, and one line proves occurrence.
#
# Verify with STAR_BACKDROP_FORCE_DEFER=1: that injects the fault, and this block MUST turn the run red.
recovered=$(grep -c "Recovering by compositing env directly" "$LOG" || true)
printf "  backdrop clause-2 recoveries: %-6s" "$recovered"
if [ "$recovered" -ne 0 ]; then
  echo "  <-- FAIL"
  pass=0
  echo "    ! renderEnvironment found a deferral renderParallax never consumed. Both entry points must"
  echo "    ! run on the same frame; the backdrop would have gone black. See BackdropPass clause 2."
else
  echo "  ok"
fi

echo "=== gl state ==="
# THE CHECK THE ORACLES STRUCTURALLY CANNOT BE (#139 phase 1b). The three above are DIFFERENTIAL: reference
# and cache-under-test share one draw lambda, at one frame position, under one ambient GL state -- so anything
# ambient cancels on both sides and reads as MATCH. That is not a gap in their implementation, it is what
# "differential" means, and four bugs reached the Director through it (#136).
#
# Proven, not asserted: STAR_RENDERTEST_GLSTATE_DESYNC=1 binds a real framebuffer behind the pass's back and
# produced 9 desync reports while envoracle 100/100, paralloracle 20/20 and spreadoracle 222/222 all still said
# DIFF=0 and this script still said PASS. The oracles cannot see it. This block is why the run now goes red.
desync=$(grep -c "^\[.*\] \[Error\] \[glstate\]" "$LOG" || true)
printf "  gl-state desyncs: %-6s" "$desync"
if [ "$desync" -ne 0 ]; then
  echo "  <-- FAIL"
  pass=0
  # The first line is the one worth reading: a desync is usually persistent, so every later line describes the
  # same fault, and only the first has the surrounding log lines that say what caused it.
  grep -m2 "\[glstate\]" "$LOG" | sed 's/^/    ! /'
else
  echo "  ok"
fi

# THE A/B RAN, PRINTED NOTHING, AND WAS NEVER JUDGED -- two vocabulary misses in four lines. The guard
# looked for "RENDERTEST_AB", which the log never writes; the body grepped "renderTest" while the log
# writes "rendertest". Both scored zero, so the section vanished. And even had it printed it was an
# echo: it never touched `pass`, so a DIFFering A/B certified as GATE: PASS. Third instance of the
# vocabulary trap this file already carries two warnings about, and it silently un-gated every A/B
# ever run through here.
#
# Assert the legs EXIST before believing agreement -- an A/B that did not run emits nothing, and
# "nothing" must not read as "identical".
#
# FOURTH MISS OF THE SAME KIND, and it survived the fix above: this block matched "A/B DIFFER" while the
# code writes "A/B DIFF:", so the one line that QUANTIFIES a divergence -- count, percentage, maxAbs --
# never reached the summary. The run still went red, but only by accident through the abmatch==0 arm, and
# the number that made the divergence diagnosable had to be read out of the raw log by hand. Match the
# literals renderTestCapture actually emits, and re-check this block whenever either side is reworded.
#
# AB_EXPECT declares what the A/B is FOR, because "the legs differ" is not universally a failure. Most A/Bs
# here certify that a refactor changed nothing, so byte-identity is the default. But an A/B of a lever with
# an INTENTIONAL visual effect inverts the verdict: MATCH then means the lever did nothing, which is the
# real failure and the one that silently certifies a dead switch. Hardcoding DIFFER=FAIL cannot express
# that, and doing so was an over-correction of the un-gating bug above.
AB_EXPECT="${AB_EXPECT:-match}"
if [ "$AB_EXPECT" != match ] && [ "$AB_EXPECT" != differ ]; then
  # An unrecognised expectation must never fall through to a pass. A typo here would otherwise certify
  # BOTH outcomes -- the same "unasserted check" defect this file carries three warnings about.
  echo "REFUSING TO CERTIFY: AB_EXPECT must be 'match' or 'differ' -- got '$AB_EXPECT'."
  exit 1
fi
if [ -n "${STAR_RENDERTEST_AB:-}" ] || grep -q "A/B leg A:" "$LOG"; then
  echo "=== in-process A/B (expect: $AB_EXPECT) ==="
  grep -E "A/B leg [AB]:|leg[AB] .*hash=|A/B NULL|A/B MATCH|A/B DIFF|A/B diff|\[rendertest\]   edge |\[rendertest\]  \|" \
    "$LOG" | sed 's/^.*\[rendertest\] //; s/^/  /'
  ablegs=$(grep -cE "leg[AB] .*hash=" "$LOG")
  abnull=$(grep -c "A/B NULL OK" "$LOG")
  abmatch=$(grep -c "A/B MATCH" "$LOG")
  abdiff=$(grep -c "A/B DIFF:" "$LOG")
  # THE NULL CONTROL GATES EVERYTHING BELOW IT. Leg A is rendered twice with nothing changed; if those two
  # renders disagree, the harness differs from itself and no comparison in this run can be attributed to the
  # lever. Asserted as "exactly one OK" so an ABSENT null -- an old binary, a skipped phase -- fails too,
  # rather than passing for want of a line saying otherwise.
  if [ "$ablegs" -ne 2 ]; then
    echo "  <-- FAIL: expected 2 hashed legs, saw $ablegs -- the A/B did not run to completion"
    pass=0
  elif [ "$abnull" -ne 1 ]; then
    echo "  <-- FAIL: no passing null control. Leg A did not reproduce itself, so nothing this run measured"
    echo "  <-- can be attributed to '${STAR_RENDERTEST_AB:-the lever}'. Fix the harness, then re-measure."
    pass=0
  elif [ $((abmatch + abdiff)) -ne 1 ]; then
    echo "  <-- FAIL: expected exactly one verdict line, saw MATCH=$abmatch DIFF=$abdiff"
    pass=0
  elif [ "$AB_EXPECT" = match ] && [ "$abmatch" -ne 1 ]; then
    echo "  <-- FAIL: the legs are not byte-identical"
    pass=0
  elif [ "$AB_EXPECT" = differ ] && [ "$abdiff" -ne 1 ]; then
    echo "  <-- FAIL: the legs are byte-identical, but this A/B was declared to change the image."
    echo "  <-- The lever did nothing: check the key is declared and that the code path reads it."
    pass=0
  else
    echo "  ok"
  fi
fi

echo
[ "$pass" -eq 1 ] && echo "GATE: PASS" || { echo "GATE: FAIL"; exit 1; }
