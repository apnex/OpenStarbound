#!/usr/bin/env python3
"""Inventory every cadence loop in the tree, and reconcile it with the design's claim.

WHY THIS EXISTS. The headless-client design states how many loops the system has, and that number has
been wrong twice -- both times found by the Director pushing on a claim, not by any check:

  1. "exactly one loop" missed the fixed-timestep accumulator nested inside the frame loop.
  2. "three clocks" missed UniverseServer::run entirely, because the model was client-centric.

A count nobody can recompute is a claim, not a measurement.

WHAT A CADENCE LOOP IS. Not every `while` is one. The discriminator is that **a cadence loop yields to
time**: it either paces itself against the wall clock, or it is the body of a thread. A parse loop
spins to completion inside one call and never yields. So a loop is a candidate when either:

  THREAD    it is the outermost loop in `X::run()` where `class X : public Thread`
  YIELDS    its body sleeps, waits on a condition, or consults a rate governor

A NESTED loop counts separately only when its OWN body carries a cadence signal. The fixed-timestep
accumulator inside the frame loop is a distinct cadence -- it has its own clock -- so it must be
reported; a plain `for (auto x : y)` in the same body is not, because its own body yields nothing.
Suppressing everything nested inside a cadence loop hid `clientLoop`, which is the whole reason the
count was wrong the first time.

WHAT IS SCANNED. Every `*.cpp` under source/, excluding vendored trees. An earlier version globbed
`Star*.cpp` and only matched `X::method` or inline member definitions, which made `server/main.cpp`
doubly invisible -- wrong filename, and a free `int main(...)` body. It held the real server loop, and
the gate stayed green over the gap because the spec's `serverLoop` had been declared target-state-only.
A blind spot plus a matching false declaration reads exactly like agreement.

VERDICTS. Every candidate must appear in DECLARED below.

  UNCLAIMED  a cadence loop the inventory does not know about   -> FAIL
  STALE      a declared loop no longer found in the tree        -> FAIL
  SPEC DRIFT a `*Loop` element in the spec with no declared      -> FAIL
             counterpart, or vice versa

The third verdict is the point: it makes the document's loop count and the tree's agree by
construction, so the next miscount is a build failure rather than a conversation.
"""
import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "source"
SPEC = REPO / "docs/superpowers/specs/2026-08-01-sovereign-headless-client-design.md"

LOOPHEAD = re.compile(r'^(\s*)(?:while|for|do)\s*[({]')
FUNC = re.compile(r'^(?:\w[\w:<>,&*\s]*?\s+)?(\w+)::(\w+)\s*\(')
YIELD = re.compile(r'\b(?:Thread::sleep|sleepPrecise|SDL_Delay|ConditionVariable|'
                   r'm_updateTicker|m_renderTicker|ticksBehind|spareTime|\.wait\(|waitFor)\b')
THREAD_BASE = re.compile(r'class\s+(\w+)\s*:\s*public\s+Thread\b')

# ---------------------------------------------------------------------------------------------
# Every cadence loop in the tree, classified. `kind` is what the loop DOES:
#   driver      owns a process's cadence and steps other work
#   accumulator catches a fixed timestep up to real time
#   worker      a thread doing its own job on its own schedule
#   supervisor  waits for shutdown; ticks nothing
#   plumbing    a primitive that blocks (locks, file waits) -- not architectural
# `element` names the spec's ELEMENT this loop realises, or None when the spec does not model it.
# ---------------------------------------------------------------------------------------------
DECLARED = {
    # --- the client's two cadences, nested in one function and clocked separately
    ("application/StarMainApplication_sdl.cpp", "SdlPlatform::run", 1):
        ("frameLoop", "driver", "frameLoop"),
    ("application/StarMainApplication_sdl.cpp", "SdlPlatform::run", 2):
        ("clientLoop", "accumulator", "clientLoop"),
    # --- the server entry point's supervisor. Free `main()`, so nothing matched it until the
    #     scanner learned to read free functions.
    ("server/main.cpp", "main", 1): ("superviseLoop", "supervisor", "superviseLoop"),
    # --- the authoritative world tick. The design missed this one entirely.
    ("game/StarUniverseServer.cpp", "UniverseServer::run", 1):
        ("universeLoop", "worker", "universeLoop"),
    # --- workers: threads doing their own job on their own schedule
    ("application/StarPlatformServices_pc.cpp",
     "PcPlatformServicesState::PcPlatformServicesState", 1): ("discordEvents", "worker", None),
    ("base/StarAssets.cpp", "Assets::workerMain", 1): ("assetWorker", "worker", None),
    ("core/StarWorkerPool.cpp", "WorkerPool::getWorkerCount", 1): ("workerPool", "worker", None),
    ("frontend/StarVoice.cpp", "Voice::thread", 1): ("voiceThread", "worker", None),
    ("game/StarRoot.cpp", "Root::Root", 1): ("rootMaintenance", "worker", None),
    ("game/StarSystemWorldServerThread.cpp", "SystemWorldServerThread::run", 1):
        ("systemWorldThread", "worker", None),
    ("game/StarUniverseConnection.cpp",
     "UniverseConnectionServer::UniverseConnectionServer", 1): ("connectionWorkers", "worker", None),
    ("game/StarUniverseConnection.cpp",
     "UniverseConnectionServer::UniverseConnectionServer", 2): ("connectionPump", "worker", None),
    ("game/StarWorldClient.cpp", "WorldClient::lightingCalc", 1): ("lightingCalc", "worker", None),
    ("game/StarWorldClient.cpp", "WorldClient::lightingMain", 1): ("lightingThread", "worker", None),
    # --- ONE PER RESIDENT WORLD, and the spec models it as `worldLoop`. Until 2026-08-01 it modelled
    #     nothing here: the runtime projection drew a universe thread and no world thread, while the
    #     tree has had one clock per world all along. The element this whole design exists to run
    #     without a participant was the one element missing from its own picture.
    ("game/StarWorldServerThread.cpp", "WorldServerThread::run", 1):
        ("worldServerThread", "worker", "worldLoop"),
    ("game/scripting/StarScriptableThread.cpp", "ScriptableThread::run", 1):
        ("scriptThread", "worker", None),
    ("server/StarServerQueryThread.cpp", "ServerQueryThread::run", 1): ("queryThread", "worker", None),
    ("server/StarServerRconClient.cpp", "ServerRconClient::run", 1): ("rconClient", "worker", None),
    ("server/StarServerRconThread.cpp", "ServerRconThread::run", 1): ("rconThread", "worker", None),
    # --- plumbing: primitives that block. They yield to time but govern no architectural cadence.
    ("base/StarAssets.cpp", "Assets::getAsset", 1): ("assetWait", "plumbing", None),
    ("core/StarLockFile_unix.cpp", "LockFile::lock", 1): ("lockRetry", "plumbing", None),
    ("core/StarLockFile_windows.cpp", "LockFile::lock", 1): ("lockRetry", "plumbing", None),
    ("core/StarThread.cpp", "ReadersWriterMutex::readLock", 1): ("readLock", "plumbing", None),
    ("core/StarThread.cpp", "ReadersWriterMutex::writeLock", 1): ("writeLock", "plumbing", None),
    ("game/StarUniverseConnection.cpp", "UniverseConnection::sendAll", 1):
        ("sendAllWait", "plumbing", None),
    ("game/StarUniverseConnection.cpp", "UniverseConnection::receiveAny", 1):
        ("receiveAnyWait", "plumbing", None),
    ("mod_uploader/StarModUploader.cpp", "ModUploader::uploadToSteam", 1):
        ("steamCreateWait", "plumbing", None),
    ("mod_uploader/StarModUploader.cpp", "ModUploader::uploadToSteam", 2):
        ("steamSubmitWait", "plumbing", None),
    ("test/StarTestUniverse.cpp", "TestUniverse::warpPlayer", 1): ("warpWait", "plumbing", None),
    ("test/StarTestUniverse.cpp", "TestUniverse::update", 1): ("testStep", "plumbing", None),
    ("test/universe_connection_test.cpp", "ASyncClientThread::run", 1):
        ("testClientThread", "worker", None),
}

# Loops the spec models that do not exist in the tree yet. Listed so SPEC DRIFT distinguishes
# "target state, not built" from "the document invented one". Keep this set as small as the truth
# allows: an entry here silences the drift check for that name, so a wrong one hides a real loop.
TARGET_ONLY = {"headlessLoop"}


def thread_classes():
    names = set()
    for p in SRC.rglob("Star*.hpp"):
        if "extern" in p.parts:
            continue
        names.update(THREAD_BASE.findall(p.read_text(errors="replace")))
    return names


def loop_body(lines, i):
    """The loop's own text, by brace matching. No line cap -- SdlPlatform::run's frame loop is
    hundreds of lines, and a cap silently truncated it into looking like two separate loops."""
    depth, out, started = 0, [], False
    for l in lines[i:]:
        out.append(l)
        for ch in l:
            if ch == "{":
                depth += 1
                started = True
            elif ch == "}":
                depth -= 1
                if started and depth == 0:
                    return "\n".join(out), i + len(out)
        if started and depth == 0:
            break
    return "\n".join(out), i + len(out)


CLASSDEF = re.compile(r'^\s*(?:class|struct)\s+(\w+)\b')
# Column 0 only. An indented `Logger::info(` is a CALL, not a definition, and allowing leading
# whitespace made the scanner attribute loops to whatever was last logged.
QUALIFIED = re.compile(r'^(?:[\w:<>,&*~][\w:<>,&*\s]*?\s+)?(\w+)::(\w+)\s*\([^;]*$')
INLINE = re.compile(r'^\s*(?:virtual\s+|static\s+|inline\s+)*[\w:<>,&*]+[\s&*]+(\w+)\s*\([^;]*$')
# A free function definition: column 0, no `::` before the parameter list. `int main(...)` is the
# one that matters -- it is where an entry point's loop lives.
FREE = re.compile(r'^(?:[\w:<>,&*][\w:<>,&*\s]*?[\s&*])(\w+)\s*\([^;]*$')
NOTFN = {"if", "for", "while", "switch", "return", "catch", "else", "do", "sizeof"}


def scan():
    """-> [(relpath, func, ordinal, line, code, why)] for the OUTERMOST cadence loop per function.

    Tracks the enclosing class so INLINE member definitions are attributed. SdlPlatform is declared
    and defined entirely inside a .cpp, so its `void run() {` never matches a qualified `X::run(`
    pattern -- and a scanner that only looks for qualified definitions misses the frame loop, which
    is the single most important loop in the system."""
    threads = thread_classes()
    out = []
    for p in sorted(SRC.rglob("*.cpp")):
        if "extern" in p.parts or "discord" in p.parts:
            continue
        rel = str(p.relative_to(SRC))
        lines = p.read_text(errors="replace").splitlines()
        fn, ordinal = None, {}
        depth, classes = 0, []                 # (name, depth-at-declaration)
        for i, l in enumerate(lines):
            m = CLASSDEF.match(l)
            if m and not l.rstrip().endswith(";"):
                classes.append((m.group(1), depth))
            m = QUALIFIED.match(l)
            if m:
                fn = "%s::%s" % (m.group(1), m.group(2))
            elif classes:
                m = INLINE.match(l)
                if m and m.group(1) not in NOTFN:
                    fn = "%s::%s" % (classes[-1][0], m.group(1))
            elif not l[:1].isspace():
                m = FREE.match(l)
                if m and m.group(1) not in NOTFN and "::" not in l.split("(")[0]:
                    fn = m.group(1)
            if LOOPHEAD.match(l) and fn:
                body, end = loop_body(lines, i)
                cls = fn.split("::")[0]
                # thread-run qualifies only the OUTERMOST loop; a nested one must yield on its own
                is_thread = fn.endswith("::run") and cls in threads and fn not in ordinal
                if is_thread or YIELD.search(body):
                    ordinal[fn] = ordinal.get(fn, 0) + 1
                    out.append((rel, fn, ordinal[fn], i + 1, l.strip()[:52],
                                "thread-run" if is_thread else "yields-to-time"))
            depth += l.count("{") - l.count("}")
            while classes and depth <= classes[-1][1]:
                classes.pop()
    return out


def spec_loops():
    if not SPEC.exists():
        return None
    t = SPEC.read_text(encoding="utf-8")
    return set(re.findall(r'\|\s*\*\*`(\w+Loop)`\*\*\s*\|\s*LOOP\s*\|', t))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit 1 on any finding (gate mode)")
    ap.add_argument("--seed", action="store_true", help="print a DECLARED block for what is found")
    args = ap.parse_args(argv)

    found = scan()
    if args.seed:
        for rel, fn, n, line, code, why in found:
            print('    ("%s", "%s", %d): ("", "", None),' % (rel, fn, n))
        return 0

    keys = {(r, f, n) for r, f, n, _l, _c, _w in found}
    unclaimed = sorted(k for k in keys if k not in DECLARED)
    stale = sorted(k for k in DECLARED if k not in keys)

    by_kind = {}
    for r, f, n, line, code, why in found:
        d = DECLARED.get((r, f, n))
        if d:
            by_kind.setdefault(d[1], []).append((d[0], r, line))

    print("loop-inventory: %d cadence loop(s) found, %d declared" % (len(found), len(DECLARED)))
    for kind in ("driver", "accumulator", "worker", "supervisor", "plumbing"):
        rows = by_kind.get(kind, [])
        if not rows:
            continue
        print("  %-12s %2d   %s" % (kind, len(rows), ", ".join(sorted(n for n, _r, _l in rows))))

    for r, f, n in unclaimed:
        line = next(l for rr, ff, nn, l, _c, _w in found if (rr, ff, nn) == (r, f, n))
        print("  UNCLAIMED   %s:%d  %s  -- a cadence loop nothing declares" % (r, line, f))
    for r, f, n in stale:
        print("  STALE       %s  %s #%d  -- declared but no longer in the tree" % (r, f, n))

    drift = 0
    spec = spec_loops()
    if spec is None:
        print("  NOTE        spec not found; skipping the spec-drift check")
    else:
        modelled = {d[2] for d in DECLARED.values() if d[2]} | TARGET_ONLY
        for name in sorted(spec - modelled):
            drift += 1
            print("  SPEC DRIFT  the spec models `%s` but no declared loop realises it" % name)
        for name in sorted(modelled - spec - TARGET_ONLY):
            drift += 1
            print("  SPEC DRIFT  `%s` is realised in the tree but the spec does not model it" % name)
        print("  spec models %d *Loop element(s); %d realised here, %d target-state-only"
              % (len(spec), len(spec & {d[2] for d in DECLARED.values() if d[2]}),
                 len(spec & TARGET_ONLY)))

    bad = len(unclaimed) + len(stale) + drift
    print("loop-inventory: UNCLAIMED=%d  STALE=%d  SPEC DRIFT=%d" % (len(unclaimed), len(stale), drift))
    if bad:
        print("loop-inventory: FAIL")
        return 1 if args.check else 0
    print("loop-inventory: OK -- every cadence loop is declared and the spec agrees")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
