#!/usr/bin/env python3
"""Continuous i915 per-ENGINE busy series, timestamped so it can be joined to a measurement window.

WHY THIS EXISTS AND WHY IT IS NOT scripts/pmu-render-busy.py. That script answers "how busy is the
render engine right now" in one number and exits -- the right shape for a spot check and the wrong
shape for attribution. Used as spot samples across a matrix on 2026-08-07 it produced a per-lever
table that did not survive its own noise control: 6-8 samples of 10s inside each 90s leg, against a
LIVE sim whose own baseline moved 12.30 / 8.16 / 9.46 % across three repeats. A 4.13pp baseline
spread cannot resolve a 1pp lever. The instrument was sound; the sampling was not.

WHAT THE INSTRUMENT IS FOR. render.frame.gpu_span_us reports the frame period whether the render
engine is 0.22% busy or 32.93% busy -- measured, three ways, same day. Every *.gpu_us number the
engine emits is elapsed GPU TIMELINE. Engine BUSY is only observable from outside the process, so
this is not a convenience: it is the only place that quantity exists.

CALIBRATION, so the numbers below have a scale. Measured on this host 2026-08-07:
    idle desktop, nothing running      0.213 %      <- the floor is NOT zero
    game, all caches on, Desert Town   0.223 %      <- inside the floor
    game, all caches on, Ark Ruins     9.97  %
    game, all caches OFF, Desert Town 32.93  %
    glxgears, uncapped, 1280x720      88.67  %      <- 416x working range
A single 6-second idle sample read 0.018% and was briefly mistaken for the floor; it is not. One
sample is not a distribution, and the floor is what a lever's delta has to clear.

ENGINES. rcs0 (render) carries everything here: ccs0 (compute), bcs0 (blitter) and vecs0
(video-enhance) read EXACTLY 0.000000 in every condition measured, including under glxgears. They
are sampled on a slow cadence anyway, as a control -- "the work is not hiding on another engine" is
a claim, and a claim needs an instrument.

AN UNAVAILABLE COUNTER IS NOT A ZERO. pmu-render-busy.py exits 3 rather than printing 0 when the PMU
is missing or unprivileged; this preserves that distinction in the series, writing UNAVAILABLE rather
than 0.000000. A blind counter that reads zero is indistinguishable from an idle GPU, which is the
absent-vs-zero defect wearing a different hat.

Usage:
    pmu-engine-sample.py --for 3600 --out run.tsv     # sample until told to stop, or the bound
    pmu-engine-sample.py --selftest                   # prove the shape and the UNAVAILABLE path
Output: TSV -- epoch, utc, engine, busy_pct -- one row per sample, appended line-buffered so a
consumer (or a kill -TERM) never truncates a partial row.
"""
import argparse
import datetime
import importlib.util
import os
import pathlib
import signal
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
RENDER_ENGINE = "rcs0-busy"
CONTROL_ENGINES = ["ccs0-busy", "bcs0-busy", "vecs0-busy"]

EXIT_OK = 0
EXIT_USAGE = 2
EXIT_UNAVAILABLE = 3


def _load_pmu():
    """Reuse pmu-render-busy.py's syscall machinery rather than re-deriving it.

    Deliberately an IMPORT, not a copy. That script carries two hard-won corrections -- the counter
    publishes lazily so it must be polled across the window rather than read at the endpoints, and
    the first read after open() delivers everything accumulated while nobody held the counter. A
    second implementation would have to rediscover both.
    """
    spec = importlib.util.spec_from_file_location("pmu", REPO / "scripts" / "pmu-render-busy.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def measure(pmu, event, window):
    """Busy ratio for one engine over `window` seconds, or None if the counter is unavailable."""
    try:
        fd = pmu.open_counter(event)
    except Exception:
        return None
    try:
        t0 = time.time()
        pmu.sample(fd)
        while time.time() - t0 < pmu.WARMUP_SECONDS:
            time.sleep(pmu.POLL_SECONDS)
            pmu.sample(fd)
        start_busy, start_wall = pmu.sample(fd), time.time()
        while time.time() - start_wall < window:
            time.sleep(pmu.POLL_SECONDS)
            busy = pmu.sample(fd)
        wall = time.time() - start_wall
        return 100.0 * (busy - start_busy) / (wall * 1e9)
    finally:
        os.close(fd)


def run(window, seconds, control_every, out_path):
    pmu = _load_pmu()
    stop = {"now": False}
    signal.signal(signal.SIGTERM, lambda *_: stop.__setitem__("now", True))
    signal.signal(signal.SIGINT, lambda *_: stop.__setitem__("now", True))

    deadline = time.time() + seconds
    n = 0
    with open(out_path, "w", buffering=1) as fh:
        fh.write("epoch\tutc\tengine\tbusy_pct\n")
        while not stop["now"] and time.time() < deadline:
            # The control sweep runs FIRST on cycle 0, so a series that is killed early still carries
            # at least one reading of every engine rather than only the one we expected to matter.
            events = [RENDER_ENGINE]
            if n % control_every == 0:
                events = CONTROL_ENGINES + [RENDER_ENGINE]
            for ev in events:
                if stop["now"] or time.time() >= deadline:
                    break
                epoch = time.time()
                utc = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).strftime("%H:%M:%S")
                v = measure(pmu, ev, window)
                cell = "UNAVAILABLE" if v is None else f"{v:.6f}"
                fh.write(f"{epoch:.3f}\t{utc}\t{ev}\t{cell}\n")
            n += 1
    return EXIT_OK


def selftest():
    fails = []
    pmu = _load_pmu()

    # 1. A bogus event must come back None -- NOT 0.0. This is the arm that matters: a counter that
    #    cannot be opened reading as "0% busy" is indistinguishable from an idle GPU.
    if measure(pmu, "no-such-engine-busy", 0.05) is not None:
        fails.append("an unopenable counter did not report as unavailable")

    # 2. ...and it must reach the file as UNAVAILABLE, not as a number a consumer would average in.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "s.tsv")
        saved = globals()["CONTROL_ENGINES"], globals()["RENDER_ENGINE"]
        globals()["CONTROL_ENGINES"] = []
        globals()["RENDER_ENGINE"] = "no-such-engine-busy"
        try:
            run(window=0.05, seconds=0.6, control_every=1, out_path=p)
            rows = [l.split("\t") for l in open(p).read().splitlines()[1:] if l.strip()]
        finally:
            globals()["CONTROL_ENGINES"], globals()["RENDER_ENGINE"] = saved
        if not rows:
            fails.append("no rows were written at all")
        elif not all(r[-1] == "UNAVAILABLE" for r in rows):
            fails.append("an unavailable counter reached the series as a number")
        elif not all(len(r) == 4 and float(r[0]) > 0 for r in rows):
            fails.append("rows are not (epoch, utc, engine, value) with a real epoch")

    # 3. The epoch must be joinable: seconds since the unix epoch, not a monotonic clock. A series
    #    stamped with time.monotonic() would join against nothing and look plausible doing it.
    if abs(time.time() - datetime.datetime.now(datetime.timezone.utc).timestamp()) > 2:
        fails.append("time.time() is not the wall clock these stamps claim to be")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  pmu_engine_sample selftest: 3/3 arms ok (unavailable is not zero, rows are joinable, "
          "the stamp is wall clock)")
    return EXIT_OK


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--for", dest="seconds", type=float, default=3600.0,
                    help="stop after this many seconds (SIGTERM also stops cleanly)")
    ap.add_argument("--window", type=float, default=1.0,
                    help="seconds per sample; short enough to resolve a leg, long enough to be stable")
    ap.add_argument("--control-every", type=int, default=30,
                    help="sweep the non-render engines once every N cycles")
    ap.add_argument("--out", help="TSV path (required unless --selftest)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if not args.out:
        print("pmu-engine-sample: --out is required", file=sys.stderr)
        return EXIT_USAGE
    return run(args.window, args.seconds, max(1, args.control_every), args.out)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
