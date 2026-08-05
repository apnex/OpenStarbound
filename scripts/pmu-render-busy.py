#!/usr/bin/env python3
"""System-wide i915 render-engine busy ratio, read straight from the PMU.

THIS IS THE SECOND INSTRUMENT, AND ITS ONLY JOB IS TO BE INDEPENDENT. It duplicates what
source/metrics/StarEngineBusyReader.cpp does, deliberately, in another language against the raw
syscall -- because scripts/metrics-mutual-check.sh compares the C++ per-client reader against a PMU
reader, and if that PMU reader were the C++ one the check would be the metrics component agreeing
with itself. An instrument checked against itself is how `render.pass.parallax.gpu_us` reported a
4.04x step that the GPU never did: everything that could have contradicted it shared its
assumptions.

So: no dist/metrics, no libstar, no perf(1). ctypes to perf_event_open, one number on stdout.

CONTRACT
  stdout   the busy RATIO (busy_ns / wall_ns) and nothing else, on success
  stderr   the reason, on failure
  exit 0   measured
  exit 3   could not measure (no i915, no event, no privilege) -- NOT a measurement of zero
  exit 2   usage error

perf_event_open against the i915 PMU is a SYSTEM-WIDE open (pid=-1), which is exactly what
perf_event_paranoid gates. This host sits at 2, so the unprivileged path is exit 3 and the caller
must treat it as "did not run", never as "agreed".
"""

import argparse
import ctypes
import os
import pathlib
import platform
import sys
import time

PMU_DIR = pathlib.Path("/sys/bus/event_source/devices/i915")
EVENT = "rcs0-busy"

EXIT_OK = 0
EXIT_USAGE = 2
EXIT_UNAVAILABLE = 3

# THE COUNTER IS PUBLISHED LAZILY, AND READING IT ONLY AT THE ENDPOINTS READS IT WRONG. Measured on
# this host against a steady ~25-37% render load, sampling every 0.5s: four of forty intervals
# advanced by exactly ZERO nanoseconds, and the interval after each one advanced by twice the
# expected amount. The busy time is not lost, it is DELIVERED LATE -- so a two-read window that ends
# inside a stale interval under-reports by however much has not been published yet.
#
# It is not a fixed kernel period, it is staleness relative to the reader: at a 0.05s interval the
# zero-advance reads were 10% and never ran more than two deep, at 0.2s they were 6% and never more
# than two deep. Reading throughout the window therefore bounds the endpoint error at roughly one
# POLL_SECONDS of busy time (~7ms at 35% load), instead of leaving it unbounded. Two-read 8-second
# windows measured 0.0% and 50% of the truth on 2 of 20 runs before this was added.
POLL_SECONDS = 0.02

# AND THE FIRST READ AFTER open() IS NOT A ZERO. perf starts the counter at zero and accumulates the
# difference between consecutive publications, so the first publication after an open delivers
# everything that accumulated while nobody held the counter: 2.74 SECONDS of busy time arrived in
# the first 0.5s interval of one trace. Sampling across a warm-up lands that catch-up BEFORE t0,
# where it belongs, rather than inside the window as a fictitious burst.
WARMUP_SECONDS = 0.30

# perf_event_open has no glibc wrapper, so it is reached by number. The kernel reads attr.size and
# copies min(size, its own sizeof) while requiring any excess to be zero, so a zeroed 128-byte
# buffer is accepted by every kernel that has the syscall at all -- older ones ignore the tail,
# newer ones read fields we deliberately leave at zero.
SYSCALL_PERF_EVENT_OPEN = {"x86_64": 298, "aarch64": 241}
PERF_ATTR_SIZE = 128
OFF_TYPE, OFF_SIZE, OFF_CONFIG = 0, 4, 8


class Unavailable(Exception):
    """Something in the chain to the counter is missing. Says which, and never returns a number."""


def read_attr(path):
    try:
        return path.read_text().strip()
    except OSError as e:
        raise Unavailable(f"cannot read {path}: {e.strerror}")


def parse_config(text, path):
    """The event file is a perf term list. Parsed as a list, not sliced after 'config=': a kernel
    that adds a second term must not have it absorbed into the value of the one term read here."""
    for term in text.split(","):
        term = term.strip()
        if not term.startswith("config="):
            continue
        try:
            return int(term[len("config="):], 0)  # base 0: the kernel writes "0x..."
        except ValueError:
            raise Unavailable(f"{path} carries an unparseable config term: {term!r}")
    raise Unavailable(f"{path} carries no config= term: {text!r}")


def open_counter(event):
    """Returns an fd for the system-wide i915 counter, or raises Unavailable naming which link
    of the chain broke: the PMU, the event, the unit, or the privilege."""
    pmu_type = read_attr(PMU_DIR / "type")
    if not pmu_type.isdigit():
        raise Unavailable(f"{PMU_DIR}/type holds no PMU type number: {pmu_type!r}")

    event_path = PMU_DIR / "events" / event
    config = parse_config(read_attr(event_path), event_path)

    # The result of this script is a RATIO OF TIMES, so the counter has to be a time. The C++ reader
    # decides that from the event's name suffix; this one asks sysfs. Two rules for one fact, on
    # purpose -- a name-based rule and a driver-declared one disagreeing is a thing worth learning
    # about, and neither can be wrong in the other's way.
    unit = read_attr(event_path.with_name(event + ".unit"))
    if unit != "ns":
        raise Unavailable(f"i915 event {event!r} is declared in {unit!r}, not nanoseconds: a ratio "
                          f"of it against wall-clock nanoseconds would not be a busy fraction")

    machine = platform.machine()
    if machine not in SYSCALL_PERF_EVENT_OPEN:
        raise Unavailable(f"no perf_event_open syscall number known for {machine}")

    attr = (ctypes.c_ubyte * PERF_ATTR_SIZE)()
    ctypes.memset(attr, 0, PERF_ATTR_SIZE)
    ctypes.c_uint32.from_buffer(attr, OFF_TYPE).value = int(pmu_type)
    ctypes.c_uint32.from_buffer(attr, OFF_SIZE).value = PERF_ATTR_SIZE
    ctypes.c_uint64.from_buffer(attr, OFF_CONFIG).value = config

    libc = ctypes.CDLL(None, use_errno=True)
    libc.syscall.restype = ctypes.c_long
    # pid=-1, cpu=0: the i915 PMU counts a DEVICE, not a task, and rejects a per-task open outright.
    fd = libc.syscall(ctypes.c_long(SYSCALL_PERF_EVENT_OPEN[machine]), ctypes.byref(attr),
                      ctypes.c_int(-1), ctypes.c_int(0), ctypes.c_int(-1), ctypes.c_ulong(0))
    if fd < 0:
        err = ctypes.get_errno()
        try:
            paranoid = read_attr(pathlib.Path("/proc/sys/kernel/perf_event_paranoid"))
        except Unavailable:
            paranoid = "unreadable"
        raise Unavailable(f"perf_event_open({event}, type {pmu_type}, config {config:#x}) failed: "
                          f"{os.strerror(err)} -- perf_event_paranoid is {paranoid}, and a "
                          f"system-wide PMU open needs <=0 or CAP_PERFMON")
    return int(fd)


def sample(fd):
    data = os.read(fd, 8)
    if len(data) != 8:
        raise Unavailable(f"short read from the i915 counter: {len(data)} of 8 bytes")
    return int.from_bytes(data, sys.byteorder)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--for", dest="seconds", type=float, default=5.0,
                    help="sampling window in seconds (default 5)")
    args = ap.parse_args(argv)
    if not (0 < args.seconds <= 3600):
        print(f"pmu-render-busy: --for expects seconds in (0, 3600], got {args.seconds}",
              file=sys.stderr)
        return EXIT_USAGE

    fd = None
    try:
        fd = open_counter(EVENT)

        # Drain the open-time catch-up. Its size is however long the counter went unread, which is
        # not a quantity this process can know, so it is discarded rather than corrected.
        warm_end = time.monotonic_ns() + int(WARMUP_SECONDS * 1e9)
        while time.monotonic_ns() < warm_end:
            sample(fd)
            time.sleep(POLL_SECONDS)

        # The clock is read immediately AFTER each sample, not around the pair: the counter is
        # latched by the read, so end-of-read is the instant the value belongs to, and bracketing
        # would fold this process's own syscall time into one end of the window only.
        busy0 = sample(fd)
        t0 = time.monotonic_ns()
        end = t0 + int(args.seconds * 1e9)
        busy1, t1 = busy0, t0
        while t1 < end:
            time.sleep(min(POLL_SECONDS, (end - t1) / 1e9))
            busy1 = sample(fd)
            t1 = time.monotonic_ns()
    except Unavailable as e:
        print(f"pmu-render-busy: {e}", file=sys.stderr)
        return EXIT_UNAVAILABLE
    finally:
        if fd is not None:
            os.close(fd)

    wall_ns = t1 - t0
    busy_ns = busy1 - busy0
    # Both guards report unavailability rather than clamping. A counter that went backwards, or a
    # window with no duration, is a reading this script cannot stand behind -- and a clamped zero
    # is indistinguishable from an idle GPU, which is the confusion this whole component exists to
    # end (see StarBusyReading.hpp).
    if wall_ns <= 0:
        print(f"pmu-render-busy: non-positive window: {wall_ns} ns", file=sys.stderr)
        return EXIT_UNAVAILABLE
    if busy_ns < 0:
        print(f"pmu-render-busy: the i915 counter moved backwards ({busy0} -> {busy1})",
              file=sys.stderr)
        return EXIT_UNAVAILABLE

    print(f"{busy_ns / wall_ns:.6f}")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
