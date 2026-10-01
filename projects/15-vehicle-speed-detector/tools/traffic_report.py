#!/usr/bin/env python3
"""Traffic survey report from the speed detector's serial log.

Capture the USB serial output to a file (any serial terminal, or
`cat /dev/ttyUSB0 > survey.log` on Linux), then:

    python3 traffic_report.py survey.log                 # prints the summary
    python3 traffic_report.py survey.log -o report.png   # plus a one-page chart

Only the VEHICLE lines are used; everything else in the log is ignored.
`--demo` writes a synthetic one-day log (clearly marked as such) so you can
try the report without hardware.

Copyright (c) 2023-2026 Pradip Subedi. All rights reserved. See LICENSE.
"""
import argparse
import random
import sys
from collections import Counter

FIELDS = ["time", "direction", "kmh", "class", "length_m", "headway_s", "over_limit", "tailgating"]

INK, MUTED, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
SERIES = "#2a78d6"      # one series per chart: one hue, no legend needed


def parse(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("VEHICLE,") or line.startswith("VEHICLE,time"):
                continue
            parts = line.split(",")[1:]
            if len(parts) != len(FIELDS):
                continue
            r = dict(zip(FIELDS, parts))
            try:
                r["kmh"] = float(r["kmh"])
                r["length_m"] = float(r["length_m"])
                r["over_limit"] = r["over_limit"] == "1"
                r["tailgating"] = r["tailgating"] == "1"
            except ValueError:
                continue
            r["hour"] = None
            t = r["time"]
            if t.startswith("D") and " " in t:            # "D0 08:30:06"
                r["hour"] = int(t.split()[1][:2])
            rows.append(r)
    return rows


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    k = (len(s) - 1) * p
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def summary(rows, limit):
    speeds = [r["kmh"] for r in rows]
    classes = Counter(r["class"] for r in rows)
    lines = [
        f"Vehicles            {len(rows)}",
        f"  bikes / cars / heavy / unknown   {classes['BIKE']} / {classes['CAR']} / "
        f"{classes['HEAVY']} / {classes['?']}",
        f"Average speed       {sum(speeds) / len(speeds):.1f} km/h" if speeds else "Average speed       -",
        f"Median (p50)        {percentile(speeds, 0.50):.1f} km/h",
        f"85th percentile     {percentile(speeds, 0.85):.1f} km/h",
        f"Top speed           {max(speeds):.1f} km/h" if speeds else "Top speed           -",
        f"Over the limit      {sum(r['over_limit'] for r in rows)} "
        f"({100 * sum(r['over_limit'] for r in rows) / max(len(rows), 1):.0f} %)",
        f"Tailgating          {sum(r['tailgating'] for r in rows)}",
    ]
    if limit:
        p85 = percentile(speeds, 0.85)
        verdict = ("the limit matches how people drive" if p85 <= limit + 5 else
                   "most drivers ignore the limit: consider traffic calming or enforcement")
        lines.append(f"Posted limit        {limit} km/h -> {verdict}")
    return "\n".join(lines)


def chart(rows, limit, out, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    speeds = [r["kmh"] for r in rows]
    p85 = percentile(speeds, 0.85)
    fig = plt.figure(figsize=(12, 7.5), facecolor=SURFACE)
    gs = fig.add_gridspec(2, 2, height_ratios=[1.25, 1], hspace=0.45, wspace=0.25)

    def style(ax, ttl):
        ax.set_facecolor(SURFACE)
        ax.set_title(ttl, loc="left", fontsize=12, color=INK, pad=10)
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
        for s in ("left", "bottom"):
            ax.spines[s].set_color(GRID)
        ax.tick_params(colors=MUTED, labelsize=9)
        ax.yaxis.grid(True, color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)

    # 1. Speed distribution with the limit and the 85th percentile.
    ax = fig.add_subplot(gs[0, :])
    bins = list(range(0, int(max(speeds + [60])) // 5 * 5 + 10, 5))
    ax.hist(speeds, bins=bins, color=SERIES, edgecolor=SURFACE, linewidth=2)
    style(ax, "Speed distribution (vehicles per 5 km/h)")
    ax.set_xlabel("km/h", color=MUTED, fontsize=9)
    top = ax.get_ylim()[1]
    # Limit label sits left of its line, 85th percentile right of its line,
    # so the two never collide even when the values are close.
    for x, label, side in ((limit, f"limit {limit} ", "right"), (p85, f" 85th percentile {p85:.0f}", "left")):
        if x:
            ax.axvline(x, color=INK, linewidth=1.2, linestyle=(0, (4, 3)))
            ax.text(x, top * 0.95, label, color=INK, fontsize=9, ha=side, va="top",
                    bbox=dict(boxstyle="square,pad=0.15", fc=SURFACE, ec="none"))

    # 2. Vehicle classes.
    ax = fig.add_subplot(gs[1, 0])
    classes = Counter(r["class"] for r in rows)
    names = [n for n in ("BIKE", "CAR", "HEAVY", "?") if classes[n]]
    vals = [classes[n] for n in names]
    bars = ax.bar([n.title() if n != "?" else "Unknown" for n in names], vals, color=SERIES, width=0.55)
    style(ax, "Vehicles by class")
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v}", ha="center", va="bottom", fontsize=9, color=INK)

    # 3. Flow by hour (needs the clock set with TIME).
    ax = fig.add_subplot(gs[1, 1])
    hours = Counter(r["hour"] for r in rows if r["hour"] is not None)
    style(ax, "Vehicles per hour")
    if hours:
        xs = list(range(24))
        ax.bar(xs, [hours.get(h, 0) for h in xs], color=SERIES, width=0.75)
        ax.set_xticks([0, 6, 12, 18, 23])
        ax.set_xlabel("hour of day", color=MUTED, fontsize=9)
    else:
        ax.text(0.5, 0.5, "clock not set (send TIME hh:mm:ss)", ha="center", va="center",
                transform=ax.transAxes, color=MUTED)
        ax.set_xticks([])
        ax.set_yticks([])

    over = sum(r["over_limit"] for r in rows)
    fig.suptitle(title, x=0.06, ha="left", fontsize=15, color=INK, y=0.985)
    fig.text(0.06, 0.935, f"{len(rows)} vehicles,  average {sum(speeds) / len(speeds):.1f} km/h,  "
             f"{over} over the limit ({100 * over / len(rows):.0f} %),  "
             f"{sum(r['tailgating'] for r in rows)} tailgating", fontsize=10, color=MUTED)
    fig.savefig(out, dpi=140, facecolor=SURFACE, bbox_inches="tight")
    print("wrote", out)


def demo(path):
    """A made-up day of traffic on a 40 km/h bazaar road. NOT real data."""
    rnd = random.Random(7)
    lines = ["# SYNTHETIC DEMO DATA - not a real survey",
             "VEHICLE,time,direction,kmh,class,length_m,headway_s,over_limit,tailgating"]
    profile = [3, 2, 1, 1, 2, 6, 18, 42, 60, 48, 40, 38, 41, 37, 35, 39, 47, 58, 52, 33, 22, 14, 8, 5]
    for hour, n in enumerate(profile):
        for i in range(n):
            cls = rnd.choices(["BIKE", "CAR", "HEAVY"], [55, 38, 7])[0]
            base = {"BIKE": 37, "CAR": 34, "HEAVY": 27}[cls] + (6 if hour < 6 or hour > 21 else 0)
            kmh = max(8.0, rnd.gauss(base, 7))
            length = {"BIKE": 1.9, "CAR": 4.3, "HEAVY": 10.5}[cls] + rnd.uniform(-0.3, 0.6)
            limit = 30 if cls == "HEAVY" else 40
            head = rnd.uniform(0.8, 30)
            tail = head < 2 and kmh >= 20
            sec = int(3600 * (i + rnd.random()) / max(n, 1))
            lines.append(f"VEHICLE,D0 {hour:02d}:{sec // 60:02d}:{sec % 60:02d},"
                         f"{rnd.choice(['A>B', 'B>A'])},{kmh:.1f},{cls},{length:.1f},{head:.2f},"
                         f"{int(kmh > limit)},{int(tail)}")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote synthetic demo log", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log", help="serial log file (or the file to create with --demo)")
    ap.add_argument("-o", "--out", help="write a PNG report")
    ap.add_argument("--limit", type=int, default=40, help="posted limit in km/h (default 40)")
    ap.add_argument("--title", default="Traffic speed survey")
    ap.add_argument("--demo", action="store_true", help="create a synthetic log first")
    a = ap.parse_args()
    if a.demo:
        demo(a.log)
    rows = parse(a.log)
    if not rows:
        sys.exit("no VEHICLE lines found in " + a.log)
    print(summary(rows, a.limit))
    if a.out:
        chart(rows, a.limit, a.out, a.title)


if __name__ == "__main__":
    main()
