#!/usr/bin/env python3
"""Continuous SOVEREIGN busy series -- per-owner CPU and per-client GPU -- stamped so it joins to a leg.

THE GAP THIS CLOSES. After #235 the busy model had three of its four cells filled and the fourth had
no consumer at all. `dist/metrics` could read per-owner CPU busy and per-client GPU busy out of any
process, and the only way to make it do so was to point it at a pid BY HAND. Nothing in the harness
called it, so a matrix run measured GPU engine busy (the PMU sampler) and in-process telemetry, and
left the CPU attribution -- the half #235 was built for -- empty.

WHY A SUPERVISOR AND NOT A LOOP. The counters `dist/metrics --raw` reads belong to a PROCESS, and the
harness starts a NEW client for every leg. So the target pid changes ~28 times in a matrix run, and
the counters reset with it. This discovers the live client, streams from it until it exits, and then
discovers the next one -- which is also why the pid is a column in the output rather than a fact about
the file.

WHAT IT DOES NOT DO, deliberately: it does not read /proc itself. Every counter in the output comes
from dist/metrics, i.e. from ClientBusyReader and ThreadBusyReader, so the fdinfo dedup rule, the
comm-parsing rule and the lseek/EINVAL rule have exactly one implementation. A Python reader beside
them would be a second place that has to learn each correction.

AN ABSENT TARGET IS A ROW, NOT A SILENCE. Between legs there is no client, and a series that simply
stops writing cannot distinguish that from a sampler that died. Those cycles write
`sampler.target UNAVAILABLE` with the reason, which is the same absent-vs-zero rule the readers
themselves keep -- one directory further out.

AMBIGUITY REFUSES RATHER THAN GUESSES. If more than one process matches, no sample is taken and the
row says which pids collided. Sampling "the first one" would produce a perfectly plausible series
about the wrong process, and nothing downstream could tell.

Usage:
    metrics-sample.py --out run.tsv [--every 1.0] [--for 3600] [--match sbinit-perf.config]
    metrics-sample.py --selftest
Output: TSV -- epoch_ns, monotonic_ns, pid, key, value, detail -- appended line-buffered, so a
consumer (or a kill -TERM) never truncates a partial row. Values are CUMULATIVE counters where the
key ends `_total`; see the raw-mode banner in source/metrics/metrics_main.cpp.
"""
import argparse
import os
import pathlib
import signal
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
HEADER = "epoch_ns\tmonotonic_ns\tpid\tkey\tvalue\tdetail\n"
UNAVAILABLE = "UNAVAILABLE"

EXIT_OK = 0
EXIT_NOTHING = 1
EXIT_USAGE = 2

_stop = {"now": False, "child": None}


def _request_stop(*_):
    _stop["now"] = True
    child = _stop["child"]
    if child is not None:
        # Terminated here rather than after the loop notices, because the loop is blocked in
        # readline() on this child's stdout and will not notice anything until it returns. Killing it
        # is what makes readline return, so this IS the wake-up.
        try:
            child.terminate()
        except OSError:
            pass


def discover(comm, match, proc_root="/proc"):
    """Pids whose comm matches exactly and whose command line contains `match`.

    TWO CONDITIONS, because either alone is wrong here. comm is truncated to 15 characters by the
    kernel and says nothing about which starbound this is -- the Director's live game and the
    harness's client are the same nine letters. The cmdline substring is what separates them, and the
    caller supplies it because the caller is what knows (lever-matrix passes its own boot config).

    Returns a sorted list, so a caller sees ALL the candidates rather than whichever one readdir
    happened to yield first.
    """
    found = []
    try:
        entries = os.listdir(proc_root)
    except OSError:
        return found
    for entry in entries:
        if not entry.isdigit():
            continue
        base = os.path.join(proc_root, entry)
        try:
            with open(os.path.join(base, "comm")) as fh:
                if fh.read().strip() != comm:
                    continue
            if match:
                with open(os.path.join(base, "cmdline"), "rb") as fh:
                    # NUL-separated, and the separators matter: joining with a space would let a
                    # substring match spill across two arguments and claim a process nobody meant.
                    if match.encode() not in fh.read().replace(b"\0", b" "):
                        continue
        except OSError:
            # The process went away between the listing and the open. Normal traffic under /proc,
            # and not a failed match -- there is simply nothing to match against any more.
            continue
        found.append(int(entry))
    return sorted(found)


def unavailable_row(reason):
    """A row saying the sampler ran and had no subject. pid 0 because there is no pid to name."""
    return f"{time.time_ns()}\t{time.monotonic_ns()}\t0\tsampler.target\t{UNAVAILABLE}\t{reason}\n"


def stream_from(metrics, pid, every, fh):
    """Run dist/metrics --raw against one pid, appending its rows. Returns how many rows were written.

    The child's own header is DROPPED. It emits one because each invocation is a complete artefact on
    its own; this file is a concatenation of many, and repeating the header mid-stream would give a
    consumer a row whose value column reads "value".
    """
    cmd = [str(metrics), "--pid", str(pid), "--raw", "--every", str(every)]
    rows = 0
    with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                          text=True, bufsize=1) as child:
        _stop["child"] = child
        try:
            if _stop["now"]:
                child.terminate()
            for line in child.stdout:
                if line == HEADER:
                    continue
                fh.write(line)
                rows += 1
        finally:
            _stop["child"] = None
            try:
                child.terminate()
            except OSError:
                pass
    return rows


def _sleep_until(when, deadline):
    """Sleep to an ABSOLUTE instant, in slices, so SIGTERM is noticed within a tenth of a second.

    Absolute rather than "sleep the interval": the cycle's own work already consumed part of it, and
    adding a full interval on top would let this loop's cadence drift away from the one it declares.
    """
    while not _stop["now"] and time.monotonic() < when and time.monotonic() < deadline:
        time.sleep(0.05)


def run(out_path, metrics, comm, match, every, seconds, proc_root="/proc"):
    signal.signal(signal.SIGTERM, _request_stop)
    signal.signal(signal.SIGINT, _request_stop)

    deadline = time.monotonic() + seconds
    total = 0
    with open(out_path, "w", buffering=1) as fh:
        fh.write(HEADER)
        while not _stop["now"] and time.monotonic() < deadline:
            started = time.monotonic()
            pids = discover(comm, match, proc_root)
            if len(pids) == 1:
                total += stream_from(metrics, pids[0], every, fh)
                # RESPAWN IS FLOORED AT THE SAMPLE CADENCE. A child that exits immediately -- the
                # binary is missing, the target lost its fds the instant it was found -- otherwise
                # sends this loop straight back to discover() and it spins. Found by injection: a
                # fake reader that printed and exited produced 500 relaunches in 0.3 seconds. A
                # sampler burning a core is not a passive observer of the thing it is measuring.
                _sleep_until(started + every, deadline)
                continue
            if not pids:
                fh.write(unavailable_row(f"no process with comm '{comm}'"
                                         + (f" and '{match}' in its command line" if match else "")))
            else:
                fh.write(unavailable_row(f"{len(pids)} processes match ({', '.join(map(str, pids))}); "
                                         "refusing to guess which one the run meant"))
            _sleep_until(started + every, deadline)
    # A run that never got a row from a reader is not a measured run of nothing, and the exit code
    # has to say so -- the file it wrote is all UNAVAILABLE rows and looks identical to a busy one
    # only in its line count.
    return EXIT_OK if total else EXIT_NOTHING


def selftest():
    import tempfile
    fails = []

    with tempfile.TemporaryDirectory() as d:
        # A fake /proc with three processes: the target, a same-named one the cmdline filter must
        # reject, and a differently-named one the comm filter must reject.
        proc = os.path.join(d, "proc")
        for pid, comm, cmdline in ((111, "starbound", "dist/starbound\0-bootconfig\0harness/sbinit-perf.config"),
                                   (222, "starbound", "dist/starbound\0-bootconfig\0/home/apnex/live.config"),
                                   (333, "chrome", "chrome\0--sbinit-perf.config")):
            os.makedirs(os.path.join(proc, str(pid)))
            open(os.path.join(proc, str(pid), "comm"), "w").write(comm + "\n")
            open(os.path.join(proc, str(pid), "cmdline"), "w").write(cmdline)
        os.makedirs(os.path.join(proc, "not-a-pid"))

        # 1. Both conditions are load-bearing, and each must be shown to reject on its own.
        if discover("starbound", "sbinit-perf.config", proc) != [111]:
            fails.append("discovery did not select exactly the process matching comm AND cmdline")
        if discover("starbound", "", proc) != [111, 222]:
            fails.append("dropping the cmdline filter did not widen the match")
        if discover("chrome", "sbinit-perf.config", proc) != [333]:
            fails.append("the comm filter is not being applied independently")

        # 2. AMBIGUITY REFUSES. Two candidates must produce no sample and a row naming both -- the arm
        #    that stops a plausible series about the wrong process.
        fake = os.path.join(d, "fake-metrics")
        open(fake, "w").write(
            "#!/bin/sh\n"
            "echo 'epoch_ns\tmonotonic_ns\tpid\tkey\tvalue\tdetail'\n"
            "echo \"1\t2\t$2\tcpu.owner.frame.busy_ns_total\t7\t-\"\n")
        os.chmod(fake, 0o755)
        amb = os.path.join(d, "amb.tsv")
        run(amb, fake, "starbound", "", 0.05, 0.3, proc)
        rows = [l.split("\t") for l in open(amb).read().splitlines()[1:] if l.strip()]
        if not rows or not all(r[4] == UNAVAILABLE for r in rows):
            fails.append("two matching processes did not refuse; something was sampled")
        elif not all("refusing to guess" in r[5] for r in rows):
            fails.append("the ambiguity refusal did not say why")

        # 3. NO TARGET WRITES A ROW, NOT A SILENCE. An empty file and a file of UNAVAILABLE rows say
        #    different things -- sampler died vs sampler ran and had no subject -- and only the second
        #    is survivable.
        empty = os.path.join(d, "none.tsv")
        run(empty, fake, "no-such-comm", "", 0.05, 0.2, proc)
        lines = open(empty).read().splitlines()
        rows = [l.split("\t") for l in lines[1:] if l.strip()]
        if lines[0] + "\n" != HEADER:
            fails.append("the file does not open with the declared header")
        if not rows:
            fails.append("a cycle with no target wrote nothing at all")
        elif not all(r[4] == UNAVAILABLE and r[3] == "sampler.target" for r in rows):
            fails.append("a cycle with no target wrote something other than an UNAVAILABLE row")
        elif any(r[4] == "0" for r in rows):
            fails.append("an absent target reached the series as a zero")

        # 4. ONE HEADER, however many children ran. The child emits its own because each invocation is
        #    a complete artefact; a concatenation of them must not carry a row whose value is "value".
        one = os.path.join(d, "one.tsv")
        run(one, fake, "starbound", "sbinit-perf.config", 0.05, 0.3, proc)
        text = open(one).read()
        if text.count(HEADER) != 1:
            fails.append(f"the header appears {text.count(HEADER)} times, not once")
        data = [l.split("\t") for l in text.splitlines()[1:] if l.strip()]
        if not data:
            fails.append("streaming from a matched process produced no rows")
        elif not all(len(r) == 6 for r in data):
            fails.append("rows are not (epoch_ns, monotonic_ns, pid, key, value, detail)")
        elif not all(r[2] == "111" for r in data):
            fails.append("the pid column does not carry the process the row was read from")
        # 4b. RESPAWN IS FLOORED. The fake reader prints one row and exits, so this measures how fast
        #     the supervisor comes back for another. Unfloored it managed 500 relaunches in these
        #     0.3 seconds; the cadence permits at most 0.3/0.05 = 6, and 12 is 2x that. A sampler
        #     that spins is not the passive observer the whole design rests on.
        if len(data) > 12:
            fails.append(f"an instantly-exiting reader was relaunched {len(data)} times in 0.3s at a "
                         "0.05s cadence -- the respawn floor is not holding")

        # 5. THE STAMP MUST BE JOINABLE. Epoch nanoseconds since 1970, not a monotonic clock -- a
        #    series stamped with time.monotonic_ns() would join against nothing and look plausible
        #    doing it. Same arm pmu-engine-sample.py carries, for the same reason.
        stamp = int(unavailable_row("x").split("\t")[0])
        if abs(stamp / 1e9 - time.time()) > 2:
            fails.append("the epoch column is not the wall clock these stamps claim to be")

        # 6. A run that read nothing must not exit 0. The file it wrote has rows in it, so a caller
        #    checking only the line count cannot tell it apart from a measured one.
        if run(os.path.join(d, "rc.tsv"), fake, "no-such-comm", "", 0.05, 0.15, proc) == EXIT_OK:
            fails.append("a run that never reached a reader still exited OK")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  metrics_sample selftest: 7/7 arms ok (both filters reject, ambiguity refuses, an absent "
          "target is a row not a silence, one header, respawn is floored, the stamp is wall clock, "
          "nothing measured is not OK)")
    return EXIT_OK


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", help="TSV path (required unless --selftest)")
    ap.add_argument("--metrics", default=str(REPO / "dist" / "metrics"),
                    help="the sovereign reader binary")
    ap.add_argument("--comm", default="starbound", help="exact /proc/<pid>/comm of the target")
    ap.add_argument("--match", default="",
                    help="substring the target's command line must contain; how the harness client is "
                         "told apart from a live game")
    ap.add_argument("--every", type=float, default=1.0,
                    help="seconds between samples; short enough to resolve a telemetry interval")
    ap.add_argument("--for", dest="seconds", type=float, default=3600.0,
                    help="stop after this many seconds (SIGTERM also stops cleanly)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.out:
        print("metrics-sample: --out is required", file=sys.stderr)
        return EXIT_USAGE
    if not os.access(args.metrics, os.X_OK):
        # NOT a silent degradation to an empty series. Without the binary there is no sovereign
        # reading to be had, and a file of UNAVAILABLE rows would blame the process being measured
        # for a build that never happened.
        print(f"metrics-sample: no executable at {args.metrics} -- build it:\n"
              f"  VCPKG_ROOT=/root/vcpkg cmake --build build/linux-release-clang --target metrics -j 8",
              file=sys.stderr)
        return EXIT_USAGE
    return run(args.out, args.metrics, args.comm, args.match, args.every, args.seconds)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
