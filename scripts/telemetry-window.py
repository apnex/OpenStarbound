#!/usr/bin/env python3
"""Difference two cumulative telemetry snapshots into a per-frame cost table.

Every counter and timer in a snapshot is CUMULATIVE since process start, so a single snapshot reports the
average over the whole run -- including world load, shader compilation and atlas warm-up. Those dominate the
early seconds and quietly drag every per-pass number toward a value that describes no moment of actual play.
Differencing two snapshots taken inside the steady state gives the cost over THAT window and nothing else.

Timers carry {count, total, mean, min, max}. `mean` is the cumulative mean and is NOT windowable; the windowed
mean must be recomputed as delta_total / delta_count. Reading `mean` directly is the mistake this script exists
to prevent -- it looks like an answer and is one for the wrong interval.

  telemetry-window.py <snapshot-dir> [--label NAME] [--first N] [--last N] [--json OUT]

Defaults to the first and last snapshot in the directory. Both are dropped if there are enough others: the
first is closest to load, the last can be a partial interval truncated by the kill.
"""
import argparse
import json
import os
import sys

# Presentation order. The GPU frame is a whole (render.frame.gpu_span_us) and a set of parts; showing them
# together is the point -- the campaign's central finding was that the parts did not add up to the whole.
GROUPS = [
    ("GPU -- whole frame", ["render.frame.gpu_span_us"]),
    ("GPU -- render passes", [
        "render.pass.environment.gpu_us", "render.pass.environment.compose.gpu_us",
        "render.pass.parallax.gpu_us", "render.pass.parallax.compose.gpu_us",
        "render.pass.world.gpu_us", "render.pass.compose.gpu_us",
        "render.pass.interface.gpu_us", "render.frame.clear.gpu_us", "render.frame.blit.gpu_us",
    ]),
    ("GPU -- lighting", [
        "lighting.gpu.spread.gpu_us", "lighting.gpu.point.gpu_us",
        "lighting.gpu.compose.gpu_us", "lighting.gpu.upscale.gpu_us",
    ]),
    ("CPU -- render thread", [
        "render.frame.us", "render.world.painter.us", "render.interface.us", "lighting.gpu.cpu_cost.us",
    ]),
    ("CPU -- lighting", [
        "lighting.cpu.total.us", "lighting.cpu.gather.us", "lighting.cpu.spread.us",
        "lighting.cpu.point.us", "lighting.cpu.post.us", "lighting.upload.us",
    ]),
    ("CPU -- server tick", [
        "tick.server.compute.us", "tick.server.commit.us", "tick.server.publish.us", "tick.server.sync.us",
    ]),
]


def load(path):
    with open(path) as f:
        return json.load(f)


def window(a, b):
    """Delta between two snapshots. Timers -> (count, total); counters -> scalar delta; gauges -> last value."""
    out = {"timers": {}, "counters": {}, "gauges": {}}
    for name, vb in b.get("timers", {}).items():
        va = a.get("timers", {}).get(name, {"count": 0, "total": 0})
        dc = vb.get("count", 0) - va.get("count", 0)
        dt = vb.get("total", 0) - va.get("total", 0)
        if dc > 0:
            out["timers"][name] = {"count": dc, "total": dt, "mean": dt / dc,
                                   "max": vb.get("max", 0)}   # max is a running high-water mark, not windowable
    for name, vb in b.get("counters", {}).items():
        d = vb - a.get("counters", {}).get(name, 0)
        if d:
            out["counters"][name] = d
    out["gauges"] = b.get("gauges", {})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("snapdir")
    ap.add_argument("--label", default="profile")
    ap.add_argument("--first", type=int, default=None, help="index of the opening snapshot")
    ap.add_argument("--last", type=int, default=None, help="index of the closing snapshot")
    ap.add_argument("--json", default=None, help="also write the windowed result here")
    args = ap.parse_args()

    files = sorted(f for f in os.listdir(args.snapdir) if f.endswith(".json"))
    if len(files) < 2:
        print(f"need >=2 snapshots in {args.snapdir}, found {len(files)}", file=sys.stderr)
        return 1

    # Trim the ends when we can afford to: the first snapshot sits closest to world load, and the last may be a
    # partial interval cut short by the kill.
    lo = args.first if args.first is not None else (1 if len(files) >= 4 else 0)
    hi = args.last if args.last is not None else (len(files) - 2 if len(files) >= 4 else len(files) - 1)
    a, b = load(os.path.join(args.snapdir, files[lo])), load(os.path.join(args.snapdir, files[hi]))
    w = window(a, b)

    # THE DENOMINATOR IS render.frame.us, NOT render.frame.gpu_span_us.
    #
    # GL timer-query results are retrieved without blocking, so a frame whose query has not resolved by
    # readback time contributes no gpu_span sample. In the first trial run gpu_span sampled 7194 of 10800
    # frames -- 67%. Dividing pass totals by the SAMPLE count instead of the FRAME count inflated every
    # per-frame figure by ~1.5x and made the parts sum to 119% of the whole, which is not a finding about the
    # renderer, it is a finding about the arithmetic. render.frame.us is recorded unconditionally, once per
    # in-world frame, so it is the honest denominator.
    frames = w["timers"].get("render.frame.us", {}).get("count", 0)
    span = w["timers"].get("render.frame.gpu_span_us")
    span_n = span["count"] if span else 0
    if not frames:
        frames = span_n

    cover = f", gpu_span sampled {span_n} ({100.0*span_n/frames:.0f}%)" if frames else ""
    print(f"\n=== {args.label} === window: {files[lo]} -> {files[hi]}  "
          f"({hi - lo} intervals, {frames} frames{cover})\n")
    print(f"  {'metric':<42} {'calls':>8} {'µs/call':>10} {'µs/frame':>10}  {'per-frame share':>16}")
    print(f"  {'-'*42} {'-'*8} {'-'*10} {'-'*10}  {'-'*16}")

    # The span's per-frame cost is its mean over the frames it SAMPLED -- extrapolating its total across
    # unsampled frames would understate it by the same 1.5x factor, in the other direction.
    span_per_frame = span["mean"] if span else 0.0

    for title, keys in GROUPS:
        rows = [(k, w["timers"][k]) for k in keys if k in w["timers"]]
        if not rows:
            continue
        print(f"\n  [{title}]")
        for k, v in rows:
            per_frame = (v["mean"] if k == "render.frame.gpu_span_us"
                         else v["total"] / frames if frames else 0.0)
            # A pass that runs on only some frames (a refresh-gated cache, a lighting recompute) has
            # count < frames. Quoting its µs/call as if it were per-frame is how a gated pass gets billed for
            # work it did not do; both columns are printed so the two can never be confused.
            share = ""
            if span_per_frame and k != "render.frame.gpu_span_us" and ".gpu_us" in k:
                share = f"{100.0 * per_frame / span_per_frame:5.1f}% of span"
            print(f"  {k:<42} {v['count']:>8} {v['mean']:>10.1f} {per_frame:>10.1f}  {share:>16}")

    if frames:
        parts = sum(v["total"] for k, v in w["timers"].items()
                    if k.endswith(".gpu_us") and k != "render.frame.gpu_span_us") / frames
        print(f"\n  GPU accounted: {parts:8.1f} µs/frame of {span_per_frame:.1f} µs span "
              f"({100.0 * parts / span_per_frame if span_per_frame else 0:.1f}%) "
              f"-- unattributed {span_per_frame - parts:.1f} µs/frame")
        print(f"  implied uncapped framerate: {1e6 / span_per_frame if span_per_frame else 0:.1f} fps (GPU-bound)")

    if w["counters"]:
        print("\n  [counters over the window]")
        for k in sorted(w["counters"]):
            print(f"  {k:<42} {w['counters'][k]:>8}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"label": args.label, "window": [files[lo], files[hi]], "frames": frames, **w}, f, indent=2)
        print(f"\n  wrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
