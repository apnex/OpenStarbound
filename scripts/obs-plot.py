#!/usr/bin/env python3
"""Draw the joined busy model: CPU per owner and GPU per engine, one dimensionless axis, per leg.

"DIRECTLY PLOTTED" WAS A CLAUSE OF THE GOAL, not a nicety. Director, 2026-08-07: "both GPU and CPU
metrics, correctly attributed, as a stream of time series data that can be DIRECTLY PLOTTED and
compared against every single lever." A per-interval stream nobody looks at is a file, not an
instrument.

WHAT MAKES ONE Y AXIS LEGITIMATE HERE, since it is the question a reader should ask first. Both halves
are dimensionless utilisation ratios, and both use a denominator of ONE unit of hardware:
    GPU   busy time / wall time, for one ENGINE      -- cannot exceed 1
    CPU   busy time / wall time, for one CORE        -- CAN exceed 1; an owner with four busy
                                                       threads reads 4.0, and the axis says `cores`
Normalising CPU to all sixteen cores would be correct system utilisation and would squash every
pattern flat against the floor. The objection to a shared axis was only ever to SUMMING the two, which
this never does.

THE RULES THIS RENDERER KEEPS, each of which is a way a plot can lie:

  A GAP IS A GAP. If an interval has no value for a series, the line BREAKS. It is not joined across
  and it is not drawn at zero. obs-join reports three distinct reasons a counter could not be
  differenced -- pid changed, counter fell, fewer than two samples -- and every one of them means
  "unknown", never "idle".

  NOTHING IS INTERPOLATED. Points sit at the instants they were measured and segments join CONSECUTIVE
  MEASURED points only. Interpolating a rate invents work the hardware never did; it is the same rule
  busyDelta encodes for counters that move backwards.

  EVERY SERIES CARRIES ITS METHOD. A re-differenced cumulative counter and a mean of sub-window rates
  are not the same kind of number -- one survives re-windowing and the other does not -- so the legend
  states which each is. Two lines that look alike and were made by different arithmetic is exactly the
  confusion the campaign has been removing.

  THE RESIDUAL GETS ITS OWN STRIP, AND ITS ZERO LINE. cpu.attribution.residual_cores is signed and
  sits near zero; plotted on a 0..1 axis it would be an invisible flat line, and its SIGN is the only
  evidence available that the gap between the whole and its parts is tick quantisation rather than a
  leak. Its own axis is always symmetric about zero.

  AN EMPTY JOIN DRAWS NOTHING AND SAYS SO. A cheerful empty chart is the most convincing wrong picture
  available.

Usage:
    obs-plot.py <label.joined.json> [more.joined.json ...] --out plot.html
    obs-plot.py <run-dir> --out plot.html          # every *.joined.json under it
    obs-plot.py --selftest
Exit: 0 ok, 1 nothing plottable, 2 usage.
"""
import argparse
import glob
import html
import json
import os
import statistics as st
import sys

EXIT_OK, EXIT_NOTHING, EXIT_USAGE = 0, 1, 2

# THE SERIES PALETTE ENCODES THE THESIS: two hardware resources, two families, on one axis. A rainbow
# of twelve arbitrary colours would say the twelve lines are twelve unrelated things; they are not.
# Cool teals are CPU owners -- parts of one whole. Warm brass is GPU -- the project's existing accent,
# carried from spec-artifact.css so the Director's three artefacts read as one project. The process
# whole is near-ink and heavier, because it is what the parts must sum to. The residual is the risk
# colour, because it is the falsifiability check.
SERIES = [
    # (key,                                    label,                     colour,   width, family)
    ("cpu.process.busy_cores",                 "process (the whole)",     "ink",    2.4, "cpu"),
    ("cpu.owner.frame.busy_cores",             "owner: frame",            "#1f6f8b", 1.8, "cpu"),
    ("cpu.owner.sim.busy_cores",               "owner: sim",              "#2d9596", 1.8, "cpu"),
    ("cpu.owner.lighting.busy_cores",          "owner: lighting",         "#5fa8a0", 1.6, "cpu"),
    ("cpu.owner.gl.busy_cores",                "owner: gl",               "#7fb8c9", 1.6, "cpu"),
    ("cpu.owner.unknown.busy_cores",           "owner: unknown",          "#9aa7b5", 1.6, "cpu"),
    ("cpu.frame.work.wall_fraction",           "frame work (in-process)", "#4a7fa5", 1.6, "cpu"),
    ("gpu.engine.render.busy_ratio",           "engine render (client)",  "#8a5f24", 2.0, "gpu"),
    ("gpu.engine.rcs0-busy.busy_ratio.pmu",    "engine rcs0 (device)",    "#c08a3e", 2.0, "gpu"),
]
RESIDUAL = "cpu.attribution.residual_cores"

# How each series was DERIVED. T10 of the TSSA observability deltas, made visible: a metric must state
# its method, not only its clock.
METHOD = {
    "cpu": "re-differenced counter",
    "gpu": "re-differenced counter",
    "pmu": "mean of sub-window rates",
    "inproc": "in-process total / wall",
}


def method_of(key):
    if key.endswith(".pmu"):
        return METHOD["pmu"]
    if key == "cpu.frame.work.wall_fraction":
        return METHOD["inproc"]
    return METHOD["cpu"]


def load(paths):
    """-> [leg dict]. A file that is not a join is skipped LOUDLY rather than drawn as an empty leg."""
    legs, skipped = [], []
    for p in paths:
        try:
            d = json.load(open(p))
        except (OSError, ValueError) as e:
            skipped.append((p, str(e)))
            continue
        if "intervals" not in d:
            skipped.append((p, "no `intervals` key -- not an obs-join artefact"))
            continue
        d["_path"] = p
        legs.append(d)
    return legs, skipped


def series_of(leg):
    """-> ({key: [(t_seconds_from_leg_start, value, interval_index)]}, t_max).

    THE INTERVAL INDEX TRAVELS WITH EVERY POINT, and it is not decoration. Dropping the intervals that
    had no value leaves a list whose neighbours may be far apart in time, and a renderer holding only
    (t, v) cannot tell "the next interval" from "four intervals later". It would then join them, which
    is the interpolation this whole model refuses. The index is what lets the pen lift.
    """
    ivs = [iv for iv in leg.get("intervals", []) if "axis" in iv and iv.get("tStartEpoch") is not None]
    if not ivs:
        return {}, 0.0
    t0 = min(iv["tStartEpoch"] for iv in ivs)
    out, tmax = {}, 0.0
    for iv in ivs:
        # The MIDPOINT of the interval, not its start: every value here is a rate OVER the interval,
        # and pinning a rate to one edge would shift the whole series by half a cadence against the
        # sovereign samples, which are instants.
        t = ((iv["tStartEpoch"] + iv.get("tEndEpoch", iv["tStartEpoch"])) / 2.0) - t0
        tmax = max(tmax, t)
        idx = iv.get("index")
        for key, value in iv["axis"].items():
            if isinstance(value, (int, float)):
                out.setdefault(key, []).append((t, float(value), idx))
    for pts in out.values():
        pts.sort()
    return out, tmax


def _path(pts, x, y):
    """An SVG path with BREAKS. Only points from CONSECUTIVE intervals are joined.

    A single `M ... L ... L` chain over the surviving points would stride across every hole and invent
    values that were never measured -- and it would look exactly like a clean measurement, which is
    what makes it worth a rule. Where the interval index jumps, the pen lifts and a new subpath starts.
    """
    if not pts:
        return ""
    out, prev = [], None
    for t, v, idx in pts:
        cmd = "L" if (prev is not None and idx is not None and idx == prev + 1) else "M"
        out.append(f"{cmd} {x(t):.2f} {y(v):.2f}")
        prev = idx
    return " ".join(out)


def nice_step(span):
    """A gridline step that yields ~4-6 lines at ANY magnitude.

    Fixed buckets were the first attempt and they broke on the residual: its span is ~0.015, a 0.25
    step put exactly one gridline on the chart, and the axis then had no scale at all. A chart whose
    gridlines depend on the quantity being in a range someone anticipated is a chart that silently
    stops carrying a scale for the series that most needs one.
    """
    if span <= 0:
        return 1.0
    import math
    raw = span / 5.0
    mag = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if raw <= m * mag:
            return m * mag
    return 10 * mag


def chart(series, tmax, keys, height=300, pad_l=54, pad_r=14, pad_t=14, pad_b=26,
          width=980, symmetric=False, unit="cores / engine-fraction"):
    """One SVG chart. `symmetric` forces a zero-centred y range, for the signed residual."""
    vals = [v for k in keys for _, v, _i in series.get(k, [])]
    if not vals:
        return ""
    if symmetric:
        m = max(0.005, max(abs(v) for v in vals) * 1.25)
        lo, hi = -m, m
    else:
        lo, hi = 0.0, max(0.05, max(vals) * 1.15)
    span = hi - lo or 1.0
    tspan = tmax or 1.0

    def x(t):
        return pad_l + (t / tspan) * (width - pad_l - pad_r)

    def y(v):
        return pad_t + (1.0 - (v - lo) / span) * (height - pad_t - pad_b)

    o = [f'<svg viewBox="0 0 {width} {height}" class="chart" role="img" '
         f'aria-label="busy over time, {unit}">']
    # Gridlines at readable intervals, and ALWAYS at 1.0 when it is in range: one core, one engine.
    step = nice_step(span)
    g = lo - (lo % step)
    while g <= hi + 1e-9:
        if lo - 1e-9 <= g <= hi + 1e-9:
            cls = "grid one" if abs(g - 1.0) < 1e-9 else ("grid zero" if abs(g) < 1e-9 else "grid")
            o.append(f'<line class="{cls}" x1="{pad_l}" x2="{width-pad_r}" '
                     f'y1="{y(g):.2f}" y2="{y(g):.2f}"/>')
            o.append(f'<text class="ytick" x="{pad_l-8}" y="{y(g)+3.5:.2f}">{g:.2f}</text>')
        g += step
    # Time ticks every ~10s, labelled in seconds from the leg's own start.
    tstep = 10.0 if tspan <= 90 else 30.0
    tk = 0.0
    while tk <= tspan + 1e-9:
        o.append(f'<line class="grid vert" x1="{x(tk):.2f}" x2="{x(tk):.2f}" '
                 f'y1="{pad_t}" y2="{height-pad_b}"/>')
        o.append(f'<text class="xtick" x="{x(tk):.2f}" y="{height-pad_b+15}">{tk:.0f}s</text>')
        tk += tstep
    for key, label, colour, w, family in SERIES + [(RESIDUAL, "residual", "risk", 1.8, "cpu")]:
        if key not in keys or key not in series:
            continue
        stroke = "var(--ink)" if colour == "ink" else ("var(--risk)" if colour == "risk" else colour)
        o.append(f'<path class="line" d="{_path(series[key], x, y)}" stroke="{stroke}" '
                 f'stroke-width="{w}"/>')
        # THE ENDPOINT IS MARKED, so a one-point series is visible at all. A lone measured value draws
        # no line segment, and a series that exists but cannot be drawn reads exactly like one that
        # was never measured.
        for t, v, _i in series[key]:
            o.append(f'<circle class="pt" cx="{x(t):.2f}" cy="{y(v):.2f}" r="1.9" fill="{stroke}"/>')
    o.append("</svg>")
    return "".join(o)


def leg_block(leg):
    series, tmax = series_of(leg)
    label = leg.get("label") or os.path.basename(leg["_path"])
    ivs = [iv for iv in leg.get("intervals", []) if "axis" in iv]
    total = len(leg.get("intervals", []))

    if not series:
        return (f'<section class="leg"><h2>{html.escape(str(label))}</h2>'
                f'<p class="empty">NOTHING PLOTTABLE. {total} interval(s) in the artefact, none '
                f'carrying a joined axis. Either the series and the samples are from different '
                f'sessions, or no sampler was running.</p></section>')

    main_keys = [k for k, *_ in SERIES if k in series]
    rows = []
    for key, lab, colour, _w, family in SERIES:
        if key not in series:
            continue
        vs = [v for _, v, _i in series[key]]
        stroke = "var(--ink)" if colour == "ink" else colour
        rows.append(
            f'<tr><td><span class="swatch" style="background:{stroke}"></span>'
            f'<span class="lab">{html.escape(lab)}</span><br>'
            f'<code class="key">{html.escape(key)}</code></td>'
            f'<td class="chip {family}">{html.escape(method_of(key))}</td>'
            f'<td class="num">{st.mean(vs):.4f}</td><td class="num">{min(vs):.4f}</td>'
            f'<td class="num">{max(vs):.4f}</td><td class="num">{len(vs)}</td></tr>')

    # COVERAGE, because the denominator is the span actually measured and a reader must be able to see
    # how much of each interval that was.
    cov = []
    for iv in ivs:
        for k, v in iv.get("sovereign", {}).get("keys", {}).items():
            if isinstance(v, dict) and "coveredS" in v and iv.get("durationS"):
                cov.append(v["coveredS"] / iv["durationS"])
                break
    cov_txt = (f"{100*st.mean(cov):.1f}% of each interval covered by sovereign samples"
               if cov else "sovereign coverage not recorded")

    src = leg.get("sources", {})
    have = [n for n in ("series", "sovereign", "pmu") if src.get(n)]
    missing = [n for n in ("series", "sovereign", "pmu") if not src.get(n)]

    resid = ""
    if RESIDUAL in series:
        vs = [v for _, v, _i in series[RESIDUAL]]
        resid = (
            '<div class="strip"><div class="striphead"><h3>attribution residual</h3>'
            '<p>process CPU busy <em>minus</em> the sum over live-thread owners. Signed on purpose: '
            'the negative half is the only evidence available that this gap is per-thread tick '
            'quantisation rather than a leak. Both signs here means noise.</p>'
            f'<p class="stat"><code>{RESIDUAL}</code> &nbsp; mean {st.mean(vs):+.4f} &nbsp; '
            f'min {min(vs):+.4f} &nbsp; max {max(vs):+.4f}</p></div>'
            + chart(series, tmax, [RESIDUAL], height=140, symmetric=True) + '</div>')

    return f"""<section class="leg">
  <header class="leghead">
    <h2>{html.escape(str(label))}</h2>
    <dl class="facts">
      <div><dt>intervals joined</dt><dd>{len(ivs)} of {total}</dd></div>
      <div><dt>window</dt><dd>{tmax:.0f}s</dd></div>
      <div><dt>sources</dt><dd>{", ".join(have) or "none"}{
        '<span class="absent"> &middot; absent: ' + ", ".join(missing) + '</span>' if missing else ''}</dd></div>
      <div><dt>sovereign coverage</dt><dd>{html.escape(cov_txt)}</dd></div>
    </dl>
  </header>
  {chart(series, tmax, main_keys)}
  <div class="scroll"><table>
    <thead><tr><th>series</th><th>method</th><th>mean</th><th>min</th><th>max</th><th>n</th></tr></thead>
    <tbody>{"".join(rows)}</tbody>
  </table></div>
  {resid}
</section>"""


CSS = """
:root{
  /* Neutrals inherited from scripts/spec-artifact.css so the TSSA, the board and this instrument read
     as one project. The SERIES colours are this page's own and are set inline: cool teals are CPU
     owners (parts of one whole), warm brass is GPU (the project's existing accent). Two hardware
     resources, two families, one axis -- which is the whole thesis of the page. */
  --ground:#f1f3f7; --surface:#fbfbfd; --raise:#e6e9f1; --sink:#e9ecf3;
  --ink:#14161d; --muted:#525869; --faint:#7b8195;
  --rule:#d7dae5; --rule-soft:#e4e7ef;
  --accent:#8a5f24; --accent-soft:#f3ecdf;
  --cpu-soft:#e2eef1; --gpu-soft:#f3ecdf;
  --risk:#a8462f;
  --grid:#dfe3ec; --grid-one:#b9c0cf;
  --shadow:0 1px 2px rgba(20,22,29,.05), 0 10px 28px -16px rgba(20,22,29,.28);
  --sans:system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
  --mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,"Liberation Mono",monospace;
}
@media (prefers-color-scheme:dark){
  :root:not([data-theme="light"]){
    --ground:#101217; --surface:#171a21; --raise:#1e222b; --sink:#141821;
    --ink:#e6e8ee; --muted:#98a0b2; --faint:#798094;
    --rule:#272c37; --rule-soft:#1f242d;
    --accent:#d29a52; --accent-soft:#2a2216;
    --cpu-soft:#16262b; --gpu-soft:#2a2216;
    --risk:#e08060;
    --grid:#232833; --grid-one:#3b4250;
    --shadow:0 1px 2px rgba(0,0,0,.45), 0 12px 32px -18px rgba(0,0,0,.8);
  }
}
:root[data-theme="dark"]{
  --ground:#101217; --surface:#171a21; --raise:#1e222b; --sink:#141821;
  --ink:#e6e8ee; --muted:#98a0b2; --faint:#798094;
  --rule:#272c37; --rule-soft:#1f242d;
  --accent:#d29a52; --accent-soft:#2a2216;
  --cpu-soft:#16262b; --gpu-soft:#2a2216;
  --risk:#e08060;
  --grid:#232833; --grid-one:#3b4250;
  --shadow:0 1px 2px rgba(0,0,0,.45), 0 12px 32px -18px rgba(0,0,0,.8);
}
*{box-sizing:border-box}
body{margin:0;background:var(--ground);color:var(--ink);font-family:var(--sans);
     font-size:16px;line-height:1.6;overflow-x:hidden}
main{max-width:78rem;margin:0 auto;padding:0 1.15rem 6rem}
.hero{padding:3.5rem 0 1.75rem;border-bottom:1px solid var(--rule);margin-bottom:2rem}
.eyebrow{font-family:var(--mono);font-size:.67rem;letter-spacing:.12em;text-transform:uppercase;
         color:var(--faint);margin:0 0 .8rem}
h1{font-weight:660;font-size:clamp(1.7rem,4vw,2.5rem);line-height:1.1;letter-spacing:-.022em;
   margin:0;text-wrap:balance;max-width:22ch}
.standfirst{margin:1.1rem 0 0;max-width:68ch;font-size:1.02rem;color:var(--muted)}
.standfirst em{color:var(--ink);font-style:normal;font-weight:620;
               box-shadow:inset 0 -.4em 0 var(--accent-soft)}
.rules{margin:1.6rem 0 0;padding:.9rem 1.1rem;background:var(--sink);border-left:2px solid var(--accent);
       border-radius:0 5px 5px 0;max-width:72ch}
.rules p{margin:0 0 .5rem;font-size:.87rem;color:var(--muted)}
.rules p:last-child{margin:0}
.rules strong{color:var(--ink)}
.leg{margin:2.75rem 0 0;padding:1.4rem 1.25rem 1.25rem;background:var(--surface);
     border:1px solid var(--rule);border-radius:7px;box-shadow:var(--shadow)}
.leghead{display:flex;flex-wrap:wrap;gap:.75rem 2rem;align-items:baseline;
         justify-content:space-between;margin-bottom:1rem}
h2{font-family:var(--mono);font-size:1.02rem;font-weight:620;margin:0;letter-spacing:-.01em}
h3{font-size:.92rem;font-weight:640;margin:0 0 .35rem}
.facts{display:flex;flex-wrap:wrap;gap:.3rem 1.5rem;margin:0}
.facts div{margin:0}
.facts dt{font-family:var(--mono);font-size:.6rem;letter-spacing:.1em;text-transform:uppercase;
          color:var(--faint);margin:0}
.facts dd{margin:0;font-size:.83rem;color:var(--muted);font-variant-numeric:tabular-nums}
.absent{color:var(--risk)}
.chart{display:block;width:100%;height:auto;margin:.4rem 0 .9rem}
.grid{stroke:var(--grid);stroke-width:1}
.grid.one{stroke:var(--grid-one);stroke-dasharray:4 3}
.grid.zero{stroke:var(--grid-one)}
.grid.vert{stroke:var(--grid);opacity:.55}
.line{fill:none;stroke-linejoin:round;stroke-linecap:round}
.pt{opacity:.85}
.ytick,.xtick{font-family:var(--mono);font-size:9.5px;fill:var(--faint)}
.ytick{text-anchor:end}
.xtick{text-anchor:middle}
.scroll{overflow-x:auto}
table{border-collapse:collapse;width:100%;min-width:34rem;font-size:.83rem;
      font-variant-numeric:tabular-nums}
thead th{font-family:var(--mono);font-size:.62rem;letter-spacing:.09em;text-transform:uppercase;
         color:var(--faint);font-weight:640;text-align:left;padding:.45rem .7rem;
         border-bottom:1px solid var(--rule);white-space:nowrap}
thead th:nth-child(n+3){text-align:right}
tbody td{padding:.45rem .7rem;border-bottom:1px solid var(--rule-soft);vertical-align:top}
tbody tr:last-child td{border-bottom:0}
tbody tr:hover{background:var(--sink)}
.num{text-align:right;white-space:nowrap}
.swatch{display:inline-block;width:.62rem;height:.62rem;border-radius:2px;margin-right:.45rem;
        vertical-align:baseline}
.lab{font-weight:600}
.key{font-family:var(--mono);font-size:.72rem;color:var(--faint);background:none;padding:0}
.chip{font-family:var(--mono);font-size:.63rem;letter-spacing:.03em;white-space:nowrap;
      color:var(--muted)}
.chip.cpu{background:var(--cpu-soft)}
.chip.gpu{background:var(--gpu-soft)}
.chip{padding:.15em .45em;border-radius:3px;display:inline-block}
.strip{margin-top:1.1rem;padding-top:1rem;border-top:1px solid var(--rule-soft)}
.striphead p{margin:0 0 .4rem;font-size:.83rem;color:var(--muted);max-width:70ch}
.stat{font-family:var(--mono);font-size:.78rem;color:var(--muted)}
.stat code{background:none;padding:0}
.empty{color:var(--risk);font-size:.9rem;max-width:70ch}
.foot{margin-top:2.5rem;padding-top:1.1rem;border-top:1px solid var(--rule);
      font-size:.78rem;color:var(--faint);max-width:74ch}
.foot code{font-family:var(--mono);background:var(--raise);padding:.1em .3em;border-radius:3px}
@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
"""


def render(legs, skipped):
    blocks = "".join(leg_block(l) for l in legs)
    skipnote = ""
    if skipped:
        items = "".join(f"<li><code>{html.escape(p)}</code> — {html.escape(why)}</li>"
                        for p, why in skipped)
        skipnote = (f'<div class="rules"><p><strong>{len(skipped)} input(s) were not plotted.</strong>'
                    f' Named rather than dropped: a file that vanishes from a report reads like a file '
                    f'that agreed with it.</p><ul>{items}</ul></div>')
    return f"""<title>Busy model — CPU and GPU on one axis</title>
<style>{CSS}</style>
<main>
<header class="hero">
  <p class="eyebrow">OpenStarbound · observability · task #254</p>
  <h1>The busy model, on one axis</h1>
  <p class="standfirst">CPU busy per owner and GPU busy per engine, measured from outside the process,
  joined to the engine's own per-interval telemetry on the <em>epoch clock they share</em>. One
  dimensionless axis, because both halves are busy time over wall time against one unit of
  hardware — one core, one engine.</p>
  <div class="rules">
    <p><strong>A gap is a gap.</strong> Where a series has no value the line breaks. It is never
    joined across and never drawn at zero: an unresolved counter means <em>unknown</em>, not idle.</p>
    <p><strong>Nothing is interpolated.</strong> Points sit where they were measured; segments join
    consecutive measured points only.</p>
    <p><strong>Every series states its method.</strong> A re-differenced cumulative counter and a mean
    of sub-window rates are different kinds of number — one survives re-windowing, the other does
    not.</p>
    <p><strong>CPU can exceed 1.0 and GPU cannot.</strong> An owner running four busy threads reads
    4.0 cores; an engine cannot be more than fully busy. The dashed line marks 1.0.</p>
  </div>
  {skipnote}
</header>
{blocks}
<p class="foot">Generated by <code>scripts/obs-plot.py</code> from the artefacts
<code>scripts/obs-join.py</code> writes per leg. The joined JSON is the evidence; this page is a
reading of it, and re-derivable from it at any time.</p>
</main>"""


def selftest():
    fails = []

    def leg(axis_by_interval, label="t"):
        ivs = []
        for i, ax in enumerate(axis_by_interval):
            ivs.append({"index": i, "tStartEpoch": 100.0 + i * 5, "tEndEpoch": 105.0 + i * 5,
                        "durationS": 5.0, "axis": ax} if ax is not None
                       else {"index": i, "unjoinable": "no epoch stamps"})
        return {"label": label, "intervals": ivs, "sources": {"series": "s", "sovereign": "v"},
                "_path": "t.joined.json"}

    K = "cpu.owner.frame.busy_cores"

    # 1. A GAP BREAKS THE LINE. Middle interval has no value for K: the path must contain TWO subpaths
    #    (two `M` commands), not one continuous chain across the hole.
    s, tmax = series_of(leg([{K: 0.2}, {"cpu.owner.sim.busy_cores": 0.1}, {K: 0.3}]))
    svg = chart(s, tmax, [K])
    d = svg.split(f'class="line" d="')[1].split('"')[0]
    if d.count("M ") != 2:
        fails.append(f"a hole in a series did not break the line ({d.count('M ')} subpath(s), want 2)")
    if "0.00" in d.split("L")[0] and False:
        pass

    # 2. A MISSING VALUE IS NOT PLOTTED AS ZERO. The series must contribute exactly 2 points, and
    #    neither of them 0 -- the failure mode is a line that dives to the floor and reads as idle.
    if len(s[K]) != 2 or any(v == 0.0 for _, v, _i in s[K]):
        fails.append("a missing interval reached the series as a point, or as a zero")

    # 3. AN UNJOINABLE INTERVAL CONTRIBUTES NOTHING. obs-join emits these for profiles predating the
    #    epoch stamps; drawing them would place a leg at the wrong place on the axis.
    s2, _ = series_of(leg([{K: 0.2}, None, {K: 0.4}]))
    if len(s2[K]) != 2:
        fails.append("an unjoinable interval contributed a point")

    # 4. NOTHING IS INTERPOLATED: the number of plotted points equals the number of measured values,
    #    never more. A resampler that filled the hole would show 3.
    if sum(len(v) for v in s.values()) != 3:
        fails.append("the renderer invented points that were not measured")

    # 5. THE RESIDUAL'S AXIS IS SYMMETRIC ABOUT ZERO. An all-positive residual plotted on its own
    #    min..max range would look one-sided and hide the fact that the quantity is signed at all.
    s3, t3 = series_of(leg([{RESIDUAL: 0.004}, {RESIDUAL: 0.006}, {RESIDUAL: 0.005}]))
    sym = chart(s3, t3, [RESIDUAL], height=140, symmetric=True)
    if 'class="grid zero"' not in sym:
        fails.append("the residual chart drew no zero line")
    ticks = [float(t.split(">")[1].split("<")[0]) for t in sym.split('class="ytick"')[1:]]
    if not (min(ticks) < 0 < max(ticks)):
        fails.append(f"the residual axis is not symmetric about zero (ticks {ticks})")

    # 6. EVERY SERIES STATES A METHOD, and the two GPU sources state DIFFERENT ones -- the whole point
    #    of carrying it. A shared label would reassert the equivalence obs-join refuses to assume.
    if method_of("gpu.engine.rcs0-busy.busy_ratio.pmu") == method_of("gpu.engine.render.busy_ratio"):
        fails.append("the PMU and the per-client reader claim the same method")
    if method_of("cpu.frame.work.wall_fraction") == method_of("cpu.owner.frame.busy_cores"):
        fails.append("the in-process and sovereign CPU series claim the same method")

    # 7. AN EMPTY JOIN DRAWS NOTHING AND SAYS SO. Captured: a cheerful empty chart is the most
    #    convincing wrong picture available.
    block = leg_block(leg([None, None]))
    if "NOTHING PLOTTABLE" not in block or "<svg" in block:
        fails.append("a leg with no joined interval still produced a chart")

    # 8. A FILE THAT IS NOT A JOIN IS NAMED, NOT SILENTLY DROPPED.
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        bad = os.path.join(td, "x.joined.json")
        open(bad, "w").write('{"label":"x"}')
        legs, skipped = load([bad])
        if legs or len(skipped) != 1:
            fails.append("a file with no intervals was loaded as a leg, or dropped without a word")
        if "were not plotted" not in render([], skipped):
            fails.append("a skipped input did not reach the page")

    # 9. THE PAGE PAINTS ITS OWN GROUND AND DEFINES ITS PALETTE OUTSIDE ANY THEME BLOCK. An artifact
    #    whose colours exist only inside a media query renders one theme's text on the other's ground.
    page = render([leg([{K: 0.3}])], [])
    if "background:var(--ground)" not in page.replace(" ", ""):
        fails.append("body does not paint an explicit background token")
    root_block = CSS.split(":root{", 1)[1].split("}", 1)[0]
    for tok in ("--ink", "--ground", "--risk", "--grid"):
        if tok not in root_block:
            fails.append(f"{tok} is not defined on bare :root -- it would be absent in system theme")

    for f in fails:
        print(f"  FAIL: {f}")
    if fails:
        return 1
    print("  obs_plot selftest: 9/9 arms ok (a gap breaks the line, a hole is not a zero, an "
          "unjoinable interval draws nothing, no point is invented, the residual axis is symmetric, "
          "methods differ per source, an empty join says so, a skipped input is named, the palette "
          "survives system theme)")
    return EXIT_OK


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("inputs", nargs="*", help="*.joined.json files, or a directory of them")
    ap.add_argument("--out", help="HTML path to write")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.inputs or not args.out:
        print("obs-plot: need input(s) and --out", file=sys.stderr)
        return EXIT_USAGE

    paths = []
    for i in args.inputs:
        paths.extend(sorted(glob.glob(os.path.join(i, "*.joined.json"))) if os.path.isdir(i) else [i])
    if not paths:
        print(f"obs-plot: no *.joined.json under {args.inputs}", file=sys.stderr)
        return EXIT_NOTHING

    legs, skipped = load(paths)
    plottable = sum(1 for l in legs if series_of(l)[0])
    with open(args.out, "w") as fh:
        fh.write(render(legs, skipped))
    for p, why in skipped:
        print(f"  !! not plotted: {p} -- {why}")
    print(f"  wrote {args.out} -- {plottable} of {len(paths)} leg(s) plottable")
    if not plottable:
        # The page still exists and says what is wrong on its face; the EXIT CODE has to say it too,
        # or a caller that only checks the status treats a page of refusals as a result.
        print("obs-plot: FAIL -- not one leg carried a joined interval. Nothing was plotted.")
        return EXIT_NOTHING
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
