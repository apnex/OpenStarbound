#!/usr/bin/env bash
# Does a harness run currently OWN the mutable state that gates read?
#
#   exit 0  -- yes. The run id is printed on stdout. Callers must SKIP, not pass and not fail.
#   exit 1  -- no. The tree's state is the tree's own, and a gate may read it.
#
# WHY THIS EXISTS (#275). `lever_table` asserts that the levers declared in scripts/lever-table.json
# match the harness config. That is a claim about the TREE. But scripts/lever-matrix.sh MUTATES that
# same config once per leg -- setting exactly one lever off -- so while a matrix runs the gate is
# instead reporting which leg happens to be executing. It went red on 2026-08-16 for precisely this
# reason, mid-leg on off-renderDrawableCache, with nothing wrong in the tree.
#
# THE RED IS THE HARMLESS HALF. Sampled during a baseline leg, between legs, or before the run
# starts, the same gate reads GREEN -- and that green is indistinguishable from a real one while
# certifying nothing. An absent verdict must be spelled differently from a passing one; that is what
# exit 77 is for, and this script is how a caller learns to use it.
#
# A LOCKFILE, NOT AN INFERENCE, AND NOT A PROCESS SCAN.
#
#   Not an inference from run artifacts. The obvious detector -- "a harness/matrix/<id>/ directory
#   with no manifest.json" -- is wrong in a way that matters: a runner that DIES leaves such a
#   directory for ever, so the gate could never pass again. assert-binary-fresh.sh already carries
#   the ruling: "a check that cannot pass is worth no more than one that cannot fail, and it costs
#   more to ignore."
#
#   Not a process scan by name either, and [#275] says so explicitly: a runner can die and leave the
#   config mutated. That is a DIFFERENT state from "running" and it is equally unsafe, so a detector
#   keyed on liveness would call it clean at exactly the moment it is dirtiest.
#
#   So: the lock is written by the act it describes (the runner, at start), removed by the same act
#   on clean exit, and SURVIVES A CRASH ON PURPOSE. A surviving lock means "the config was last
#   touched by a run that did not finish tidying up" -- which is true, and which the operator clears
#   deliberately after checking the config, rather than a script clearing it by guessing.
set -uo pipefail

LOCK=${HARNESS_LOCK:-harness/matrix/.active}

if [ "${1:-}" = "--selftest" ]; then
  fails=0
  tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
  mkdir -p "$tmp/harness/matrix"

  # ARM 1: no lock -> not active. Without this the remaining arms prove only that it can say "active",
  # and a detector stuck at "active" would disable the gate permanently rather than guard it.
  ( cd "$tmp" && HARNESS_LOCK=harness/matrix/.active "$OLDPWD/scripts/harness-active.sh" >/dev/null 2>&1 )
  [ $? -eq 1 ] && echo "  ok   no lock reads as NOT active" \
               || { echo "  FAIL a clean tree read as active -- the gate would never run"; fails=1; }

  # ARM 2: a lock -> active, and the run id reaches the caller so the skip message can name it.
  echo "matrix-19700101-000000 pid=1234" > "$tmp/harness/matrix/.active"
  out=$( cd "$tmp" && HARNESS_LOCK=harness/matrix/.active "$OLDPWD/scripts/harness-active.sh" 2>/dev/null ); rc=$?
  { [ $rc -eq 0 ] && [ "$out" = "matrix-19700101-000000" ]; } \
    && echo "  ok   a lock reads as active and yields its run id" \
    || { echo "  FAIL lock present but rc=$rc id='$out'"; fails=1; }

  # ARM 3: A GARBLED LOCK MUST STILL READ ACTIVE. The failure mode to avoid is a truncated or empty
  # lock -- a runner killed mid-write -- being parsed as "no id, therefore nothing running". That
  # turns the crash case, the one this exists for, into a silent pass.
  : > "$tmp/harness/matrix/.active"
  out=$( cd "$tmp" && HARNESS_LOCK=harness/matrix/.active "$OLDPWD/scripts/harness-active.sh" 2>/dev/null ); rc=$?
  { [ $rc -eq 0 ] && [ -n "$out" ]; } \
    && echo "  ok   an empty lock still reads active, with a placeholder id" \
    || { echo "  FAIL an empty lock read as rc=$rc id='$out' -- the crash case passes silently"; fails=1; }

  # ARM 4: a stale lock whose process is long gone STILL reads active. This is the [#275] ruling made
  # executable: died-with-config-mutated is not the same state as clean, and only a human may say so.
  echo "matrix-19700101-000000 pid=999999" > "$tmp/harness/matrix/.active"
  ( cd "$tmp" && HARNESS_LOCK=harness/matrix/.active "$OLDPWD/scripts/harness-active.sh" >/dev/null 2>&1 )
  [ $? -eq 0 ] && echo "  ok   a stale lock from a dead pid still reads active" \
               || { echo "  FAIL a dead runner's lock read as clean -- the dirtiest state passed"; fails=1; }

  [ $fails -eq 0 ] || { echo "harness-active selftest: FAILED"; exit 1; }
  echo "harness-active selftest: 4/4 arms ok -- clean passes, present/empty/stale all read active"
  exit 0
fi

[ -e "$LOCK" ] || exit 1
id=$(head -n1 "$LOCK" 2>/dev/null | awk '{print $1}')
echo "${id:-<unnamed-run>}"
exit 0
