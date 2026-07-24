#!/usr/bin/env bash
# Run game_tests against the CLEAN asset set (vanilla base + our opensb overlay, NO third-party workshop mods).
#
# WHY: game_tests turns any error-level log into a test failure (game_tests_main.cpp ErrorLogSink). Several
# installed Steam Workshop mods in sbinit.config have broken items/patches (missing .animation, nil buildscripts,
# bad dungeon patches) that log errors on load -- those are MOD bugs, not engine bugs, and they spuriously fail
# ConstructItems / RootTest. This runs against harness/sbinit-test.config (base + opensb only) so the suite
# validates the engine + our assets. The render gate (scripts/render-gate.sh) keeps the full mod set for
# real-world coverage.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
mkdir -p harness/storage-test
exec taskset -c 6-15 nice -n 19 dist/game_tests -bootconfig "$PWD/harness/sbinit-test.config" "$@"
