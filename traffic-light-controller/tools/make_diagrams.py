#!/usr/bin/env python3
"""Draws the PNG diagrams in images/. Run: python3 tools/make_diagrams.py

Needs matplotlib (pip install matplotlib). Timings are taken from the same
values as the SITES table in the firmware, so keep them in sync if you edit.
"""
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Rectangle

OUT = Path(__file__).resolve().parent.parent / "images"
OUT.mkdir(exist_ok=True)

RED, YEL, GRN = "#d62828", "#f4b400", "#2a9d3f"
ROAD, KERB, INK, MUTED, PANEL = "#4a4e57", "#c9ccd1", "#1d232b", "#6b7280", "#f3f4f6"
ACCENT = "#1f6feb"

# name, plan, N, E, S, W, yellow, all-red, ped walk, ped flash
SITES = [
    ("SITE-01", "SPLIT", 30, 25, 30, 25, 3, 2, 12, 6),
    ("SITE-02", "SPLIT", 30, 25, 30, 25, 3, 2, 12, 6),
    ("SITE-03", "SPLIT", 25, 25, 25, 25, 3, 2, 12, 6),
    ("SITE-04", "SPLIT", 25, 25, 25, 25, 3, 2, 12, 6),
    ("SITE-05", "OPPOSING", 35, 30, 35, 30, 4, 2, 15, 6),
    ("SITE-06", "OPPOSING", 35, 30, 35, 30, 4, 2, 15, 6),
    ("SITE-07", "SPLIT", 20, 20, 20, 20, 3, 2, 10, 5),
    ("SITE-08", "SPLIT", 20, 20, 20, 20, 3, 2, 10, 5),
]


def cycle_seconds(site):
    _, plan, n, e, s, w, y, ar, *_ = site
    if plan == "SPLIT":
        return n + e + s + w + 4 * (y + ar)
    return max(n, s) + max(e, w) + 2 * (y + ar)


def save(fig, name):
    fig.savefig(OUT / name, dpi=160, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", OUT / name)


def signal_head(ax, x, y, rot=0, scale=1.0, lit=None):
    """Small 3-aspect head. rot=0 vertical, rot=90 horizontal."""
    w, h = 0.55 * scale, 1.5 * scale
    if rot == 90:
        w, h = h, w
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                                boxstyle="round,pad=0.03,rounding_size=0.12",
                                fc="#111", ec="#000", zorder=6))
    colors = [RED, YEL, GRN]
    for i, c in enumerate(colors):
        off = (i - 1) * 0.45 * scale
        cx, cy = (x, y - off) if rot == 0 else (x + off, y)
        on = lit is None or lit == c
        ax.add_patch(Circle((cx, cy), 0.17 * scale, fc=c if on else "#333",
                            ec="none", zorder=7))


# ---------------------------------------------------------------------------
def junction_layout():
    fig, ax = plt.subplots(figsize=(9, 9))
    ax.set_xlim(-12, 12)
    ax.set_ylim(-12, 12)
    ax.set_aspect("equal")
    ax.axis("off")

    ax.add_patch(Rectangle((-12, -12), 24, 24, fc="#e8efe4", zorder=0))
    ax.add_patch(Rectangle((-3, -12), 6, 24, fc=ROAD, zorder=1))
    ax.add_patch(Rectangle((-12, -3), 24, 6, fc=ROAD, zorder=1))
    for k in range(-12, 12, 2):
        if abs(k) > 3:
            ax.plot([0, 0], [k, k + 1], color="white", lw=2, zorder=2)
            ax.plot([k, k + 1], [0, 0], color="white", lw=2, zorder=2)
    # stop lines and zebra crossings
    # Stop lines sit before the zebra, across the incoming (left) lane only.
    for sx, sy, w, h in [(0, 5.6, 3, 0.25), (-3, -5.85, 3, 0.25),
                         (5.6, -3, 0.25, 3), (-5.85, 0, 0.25, 3)]:
        ax.add_patch(Rectangle((sx, sy), w, h, fc="white", zorder=3))
    for i in range(6):
        ax.add_patch(Rectangle((-3 + i, 4.0), 0.5, 1.2, fc="white", zorder=3))
        ax.add_patch(Rectangle((-3 + i, -5.2), 0.5, 1.2, fc="white", zorder=3))
        ax.add_patch(Rectangle((4.0, -3 + i), 1.2, 0.5, fc="white", zorder=3))
        ax.add_patch(Rectangle((-5.2, -3 + i), 1.2, 0.5, fc="white", zorder=3))

    # Nepal drives on the LEFT, so each incoming stream uses the lane on its
    # own left: southbound traffic (from the north) runs on the east half.
    arrows = [((1.5, 11), (1.5, 6.2), "from NORTH"),
              ((-1.5, -11), (-1.5, -6.2), "from SOUTH"),
              ((11, -1.5), (6.2, -1.5), "from EAST"),
              ((-11, 1.5), (-6.2, 1.5), "from WEST")]
    for (x1, y1), (x2, y2), label in arrows:
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>",
                                     mutation_scale=22, lw=3, color="#ffd166",
                                     zorder=4))

    # One pole per corner, head facing the approach that stops beside it.
    poles = [(4.6, 5.6, "P1", "North approach", 0),
             (-4.6, -5.6, "P3", "South approach", 0),
             (5.6, -4.6, "P2", "East approach", 90),
             (-5.6, 4.6, "P4", "West approach", 90)]
    for x, y, tag, label, rot in poles:
        ax.add_patch(Circle((x, y), 0.35, fc="#999", ec=INK, zorder=5))
        hx, hy = (x + 1.1, y) if rot == 0 else (x, y + 1.1)
        if tag in ("P3",):
            hx = x - 1.1
        if tag in ("P2",):
            hy = y - 1.1
        signal_head(ax, hx, hy, rot=rot, scale=0.9)
        tx = {"P1": (7.6, 7.2), "P3": (-7.6, -7.4), "P2": (8.2, -7.0), "P4": (-8.2, 7.0)}[tag]
        ax.text(*tx, f"{tag}\n{label}", ha="center", va="center", fontsize=10,
                color=INK, fontweight="bold", zorder=8,
                bbox=dict(boxstyle="round,pad=0.35", fc="white", ec=KERB))

    # Controller cabinet
    ax.add_patch(FancyBboxPatch((6.6, 3.6), 2.6, 1.6, boxstyle="round,pad=0.05",
                                fc=ACCENT, ec=INK, zorder=5))
    ax.text(7.9, 4.4, "Controller\ncabinet", ha="center", va="center",
            color="white", fontsize=9, fontweight="bold", zorder=6)
    duct = [(6.6, 4.4), (4.6, 4.4), (4.6, -4.6), (-4.6, -4.6), (-4.6, 4.6), (4.6, 4.6)]
    xs, ys = zip(*duct)
    ax.plot(xs, ys, ls="--", color=ACCENT, lw=1.5, zorder=3)

    ax.text(0, 11.4, "N", ha="center", fontsize=18, fontweight="bold", color=INK)
    ax.text(0, -11.9, "S", ha="center", fontsize=18, fontweight="bold", color=INK)
    ax.text(11.4, 0, "E", va="center", fontsize=18, fontweight="bold", color=INK)
    ax.text(-11.9, 0, "W", va="center", fontsize=18, fontweight="bold", color=INK)
    ax.set_title('"+" cross road: 4 signal poles, 1 controller (left-hand traffic)',
                 fontsize=13, color=INK, pad=12)
    ax.text(0, -12.9, "Yellow arrows = incoming lane. Dashed blue = underground duct "
            "from cabinet to every pole.", ha="center", fontsize=9, color=MUTED)
    save(fig, "01_junction_layout.png")


# ---------------------------------------------------------------------------
def wiring_diagram():
    fig, ax = plt.subplots(figsize=(13, 8))
    ax.set_xlim(0, 26)
    ax.set_ylim(0, 16)
    ax.axis("off")

    ax.add_patch(FancyBboxPatch((9, 2), 6, 12, boxstyle="round,pad=0.1",
                                fc="#0b7a75", ec=INK, lw=1.5))
    ax.text(12, 13.3, "ARDUINO UNO\nATmega328P", ha="center", va="center",
            color="white", fontsize=12, fontweight="bold")

    heads = [("NORTH", (2, 3, 4)), ("EAST", (5, 6, 7)),
             ("SOUTH", (8, 9, 10)), ("WEST", (11, 12, 13))]
    y = 12.0
    for name, pins in heads:
        ax.text(20.6, y - 0.9, name, fontsize=11, fontweight="bold", color=INK)
        for pin, col in zip(pins, (RED, YEL, GRN)):
            ax.text(14.8, y, f"D{pin}", ha="right", va="center", color="white",
                    fontsize=9, family="monospace")
            ax.plot([15, 17], [y, y], color=col, lw=2.2)
            ax.add_patch(Rectangle((17, y - 0.18), 1.0, 0.36, fc="white", ec=INK))
            ax.text(17.5, y, "220R", ha="center", va="center", fontsize=6)
            ax.plot([18, 19.2], [y, y], color=col, lw=2.2)
            ax.add_patch(Circle((19.5, y), 0.28, fc=col, ec=INK))
            ax.plot([19.8, 20.3], [y, y], color=INK, lw=1)
            y -= 0.62
        ax.plot([20.3, 20.3], [y + 0.62 * 3, y + 0.62], color=INK, lw=1)
        ax.text(20.45, y + 0.9, "GND", fontsize=7, color=MUTED)
        y -= 0.55

    inputs = [("A0", "Night switch / 24h timer", 10.5),
              ("A1", "Emergency key switch", 8.8),
              ("A2", "Pedestrian push button", 7.1)]
    for pin, label, yy in inputs:
        ax.text(9.2, yy, pin, va="center", color="white", fontsize=9, family="monospace")
        ax.plot([5.2, 9], [yy, yy], color=INK, lw=1.6)
        ax.plot([4.2, 4.9], [yy, yy + 0.35], color=INK, lw=1.6)
        ax.plot([3.4, 4.2], [yy, yy], color=INK, lw=1.6)
        ax.plot([3.4, 3.4], [yy, yy - 0.5], color=INK, lw=1.6)
        ax.text(3.4, yy - 0.85, "GND", ha="center", fontsize=7, color=MUTED)
        ax.text(5.6, yy + 0.35, label, fontsize=9, color=INK)
    ax.text(0.5, 11.6, "INPUTS (internal pull-up, close to GND = active)",
            fontsize=10, fontweight="bold", color=INK)

    for pin, label, yy, col in [("A3", "WALK (green man)", 4.6, GRN),
                                ("A4", "DON'T WALK (red man)", 3.4, RED)]:
        ax.text(9.2, yy, pin, va="center", color="white", fontsize=9, family="monospace")
        ax.plot([5.6, 9], [yy, yy], color=col, lw=2.2)
        ax.add_patch(Rectangle((6.4, yy - 0.18), 1.0, 0.36, fc="white", ec=INK))
        ax.text(6.9, yy, "220R", ha="center", va="center", fontsize=6)
        ax.add_patch(Circle((5.3, yy), 0.28, fc=col, ec=INK))
        ax.text(4.8, yy, label, ha="right", va="center", fontsize=9, color=INK)
    ax.text(0.5, 5.6, "PEDESTRIAN LAMPS", fontsize=10, fontweight="bold", color=INK)

    ax.text(12, 2.6, "D0/D1 = USB serial log\nA5 = spare", ha="center",
            color="white", fontsize=8)
    ax.set_title("Controller wiring (Proteus / bench version, LEDs direct on pins)",
                 fontsize=13, color=INK)
    ax.text(13, 0.5, "Field version: replace each LED + 220R with one channel of the "
            "lamp driver board (see 04_field_hardware.png). Never drive real lamps "
            "from the Arduino pins.", ha="center", fontsize=9, color=RED)
    save(fig, "02_wiring_diagram.png")


# ---------------------------------------------------------------------------
def phase_timing(site, fname):
    name, plan, n, e, s, w, y, ar, *_ = site
    greens = {"N": n, "E": e, "S": s, "W": w}
    fig, ax = plt.subplots(figsize=(13, 4.2))
    rows = ["N", "E", "S", "W"]
    if plan == "SPLIT":
        phases = [["N"], ["E"], ["S"], ["W"]]
    else:
        phases = [["N", "S"], ["E", "W"]]

    t = 0
    segments = {r: [] for r in rows}
    marks = []
    for ph in phases:
        g = max(greens[a] for a in ph)
        for r in rows:
            if r in ph:
                segments[r] += [(t, g, GRN), (t + g, y, YEL), (t + g + y, ar, RED)]
            else:
                segments[r] += [(t, g + y + ar, RED)]
        marks.append((t, "+".join(ph)))
        t += g + y + ar

    for i, r in enumerate(rows):
        yy = len(rows) - 1 - i
        for start, dur, col in segments[r]:
            ax.broken_barh([(start, dur)], (yy - 0.35, 0.7), facecolors=col,
                           edgecolor="white", linewidth=0.8)
            if dur >= 4:
                ax.text(start + dur / 2, yy, f"{dur}s", ha="center", va="center",
                        color="white" if col != YEL else INK, fontsize=8)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([{"N": "North", "E": "East", "S": "South", "W": "West"}[r]
                        for r in reversed(rows)])
    ax.set_xlim(0, t)
    ax.set_ylim(-0.6, 4.0)
    ax.set_xlabel("seconds into cycle")
    for x, lab in marks:
        ax.axvline(x, color=MUTED, lw=0.6, ls=":")
        ax.text(x + 0.5, len(rows) - 0.35, f"phase {lab}", fontsize=8, color=MUTED)
    for sp in ("top", "right"):
        ax.spines[sp].set_visible(False)
    ax.set_title(f"{name}: {plan} plan, cycle = {t} s "
                 f"(yellow {y}s, all-red {ar}s on every change)", color=INK)
    save(fig, fname)


# ---------------------------------------------------------------------------
def field_hardware():
    fig, ax = plt.subplots(figsize=(14, 7.5))
    ax.set_xlim(0, 28)
    ax.set_ylim(0, 15)
    ax.axis("off")

    def box(x, y, w, h, title, body, fc=PANEL):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.12",
                                    fc=fc, ec=INK, lw=1.2))
        ax.text(x + w / 2, y + h - 0.45, title, ha="center", va="top",
                fontsize=10, fontweight="bold", color=INK)
        ax.text(x + w / 2, y + h - 1.15, body, ha="center", va="top",
                fontsize=8, color=INK, linespacing=1.4)

    def arrow(p, q, label="", col=INK, lpos=None):
        ax.add_patch(FancyArrowPatch(p, q, arrowstyle="-|>", mutation_scale=14,
                                     lw=1.6, color=col))
        if label:
            lx, ly = lpos or ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2 + 0.3)
            ax.text(lx, ly, label, ha="center", fontsize=8, color=col)

    box(0.4, 9.5, 4.6, 4.3, "230 V AC mains",
        "NEA supply\nenergy meter\nmain isolator")
    box(6.0, 9.5, 4.8, 4.3, "Protection",
        "Type 2 SPD (surge)\nMCB 6 A + RCCB 30 mA\nearth pit < 5 ohm")
    box(11.8, 9.5, 5.2, 4.3, "Power",
        "12 V or 24 V DC SMPS\n(Mean Well class)\nbattery + charger\n(4-6 h backup)")
    box(18.0, 9.5, 4.6, 4.3, "Logic supply",
        "DC-DC buck to 7-9 V\ninto Arduino VIN\n(isolated from\nlamp ground noise)")
    box(23.4, 9.5, 4.2, 4.3, "Cabinet",
        "IP65 steel/ABS\nfan + filter\nlockable door\nDIN rail")

    box(6.0, 2.2, 6.0, 5.6, "ARDUINO UNO",
        "this firmware\nSITE_ID 1..8\nwatchdog + conflict\nmonitor\n\nUSB = service log",
        fc="#d9f2f1")
    box(13.6, 2.2, 6.2, 5.6, "Lamp driver board",
        "14 channels\nlogic MOSFET per channel\n(IRLZ44N) for 12/24 V DC\nOR zero-cross SSR\nfor 230 V AC aspects\nfuse per pole\n(no mechanical relays)")
    box(21.4, 2.2, 6.2, 5.6, "4 signal poles",
        "P1 North  P2 East\nP3 South  P4 West\n300 mm LED aspects\n7-core armoured cable\nper pole + earth")
    box(0.4, 2.2, 4.4, 5.6, "Field inputs",
        "night: 24 h timer\nor photocell\nemergency: key sw.\npedestrian: push\nbuttons (IP65)")

    arrow((5.0, 11.6), (6.0, 11.6))
    arrow((10.8, 11.6), (11.8, 11.6))
    arrow((17.0, 11.6), (18.0, 11.6))
    arrow((20.3, 9.5), (9.0, 7.8), "logic 7-9 V", lpos=(11.6, 8.6))
    arrow((14.4, 9.5), (16.7, 7.8), "lamp 12/24 V", col=RED, lpos=(17.6, 8.8))
    arrow((4.8, 5.0), (6.0, 5.0), "A0-A2")
    arrow((12.0, 5.0), (13.6, 5.0), "D2-D13, A3-A4")
    arrow((19.8, 5.0), (21.4, 5.0), "lamp feeds", col=RED)

    ax.set_title("Field installation: one cabinet per junction (x8 sites)",
                 fontsize=13, color=INK)
    ax.text(14, 0.6, "The Arduino only switches the driver inputs. All lamp current "
            "flows through the driver board and its own fused supply.",
            ha="center", fontsize=9, color=MUTED)
    save(fig, "04_field_hardware.png")


# ---------------------------------------------------------------------------
def state_machine():
    fig, ax = plt.subplots(figsize=(13, 7.5))
    ax.set_xlim(-0.5, 26)
    ax.set_ylim(0, 14)
    ax.axis("off")
    nodes = {
        "STARTUP_RED": (3, 11, RED), "GREEN": (9, 11, GRN), "YELLOW": (15, 11, YEL),
        "ALL_RED": (21, 11, RED), "PED_WALK": (21, 6, GRN), "PED_FLASH": (15, 6, GRN),
        "PED_CLEAR": (9, 6, RED), "NIGHT_FLASH": (3, 6, YEL),
        "EMERGENCY": (3, 1.5, RED), "FAULT": (21, 1.5, "#7a1f1f"),
    }
    for k, (x, y, c) in nodes.items():
        ax.add_patch(FancyBboxPatch((x - 2.1, y - 0.7), 4.2, 1.4,
                                    boxstyle="round,pad=0.1", fc=c, ec=INK))
        ax.text(x, y, k, ha="center", va="center", fontsize=9, fontweight="bold",
                color=INK if c == YEL else "white")

    def arrow(p, q, rad=0.0):
        ax.add_patch(FancyArrowPatch(p, q, arrowstyle="-|>", mutation_scale=14,
                                     lw=1.3, color=INK,
                                     connectionstyle=f"arc3,rad={rad}"))

    def poly(points):
        xs, ys = zip(*points[:-1])
        ax.plot(xs, ys, color=INK, lw=1.3)
        arrow(points[-2], points[-1])

    def label(x, y, text, **kw):
        ax.text(x, y, text, ha="center", fontsize=8, color=MUTED, **kw)

    arrow((5.2, 11), (6.8, 11));    label(6.0, 11.35, "5 s")
    arrow((11.2, 11), (12.8, 11));  label(12.0, 9.75, "green time / emergency / night")
    arrow((17.2, 11), (18.8, 11));  label(18.0, 11.35, "yellow time")
    arrow((20.0, 11.8), (10.0, 11.8), rad=0.18)
    label(15.0, 13.55, "next phase (no request pending)")
    arrow((21, 10.2), (21, 6.8));   label(22.2, 8.9, "ped\nrequest")
    arrow((18.8, 6), (17.2, 6));    label(18.0, 6.35, "walk time")
    arrow((12.8, 6), (11.2, 6));    label(12.0, 6.35, "flash time")
    arrow((9, 6.8), (9, 10.2));     label(10.0, 8.0, "next\nphase")

    # ALL_RED -> night / emergency via a bus line on the left
    poly([(20.2, 10.2), (20.2, 8.9), (-0.1, 8.9), (-0.1, 6), (0.8, 6)])
    poly([(-0.1, 6), (-0.1, 1.5), (0.8, 1.5)])
    label(15.5, 9.15, "from ALL_RED:  emergency input -> EMERGENCY,  night input -> NIGHT_FLASH")

    arrow((3, 6.8), (3, 10.2));     label(3.9, 7.6, "night\noff")
    poly([(5.2, 1.5), (6.0, 1.5), (6.0, 9.7), (4.6, 9.7), (4.6, 10.2)])
    label(7.0, 4.2, "released")
    arrow((3, 5.2), (3, 2.3));      label(4.0, 3.5, "emergency\ninput")

    ax.text(21, 3.0, "any state: conflicting command or\noutput read-back error"
            "\n-> FAULT (latched, all heads flash red)", ha="center",
            fontsize=8, color="#7a1f1f")
    ax.set_title("Controller state machine", fontsize=13, color=INK)
    save(fig, "05_state_machine.png")


# ---------------------------------------------------------------------------
def sites_overview():
    fig, ax = plt.subplots(figsize=(13, 6.5))
    ax.set_xlim(0, 4)
    ax.set_ylim(0, 2)
    ax.axis("off")
    for i, site in enumerate(SITES):
        col, row = i % 4, 1 - i // 4
        x, y = col + 0.05, row + 0.07
        ax.add_patch(FancyBboxPatch((x, y), 0.9, 0.84, boxstyle="round,pad=0.02",
                                    fc=PANEL, ec=KERB))
        cx, cy = x + 0.24, y + 0.42
        ax.add_patch(Rectangle((cx - 0.05, cy - 0.2), 0.1, 0.4, fc=ROAD))
        ax.add_patch(Rectangle((cx - 0.2, cy - 0.05), 0.4, 0.1, fc=ROAD))
        for dx, dy in [(0.09, 0.09), (-0.09, -0.09), (0.09, -0.09), (-0.09, 0.09)]:
            ax.add_patch(Circle((cx + dx, cy + dy), 0.022, fc=GRN, ec=INK, lw=0.5))
        name, plan, n, e, s, w, yl, ar, pw, pf = site
        ax.text(x + 0.47, y + 0.66, f"{name}\nSITE_ID = {i + 1}", fontsize=10,
                fontweight="bold", color=INK, linespacing=1.3)
        ax.text(x + 0.47, y + 0.16,
                f"plan: {plan}\ngreen N/E/S/W:\n{n}/{e}/{s}/{w} s\n"
                f"yellow {yl}s, all-red {ar}s\nped walk {pw}s\n"
                f"cycle {cycle_seconds(site)} s",
                fontsize=8, color=INK, va="bottom", linespacing=1.3)
    ax.set_title("8 installation sites: same hardware, same firmware, different SITE_ID",
                 fontsize=13, color=INK)
    save(fig, "06_sites_overview.png")


if __name__ == "__main__":
    junction_layout()
    wiring_diagram()
    phase_timing(SITES[0], "03_phase_timing_split.png")
    phase_timing(SITES[4], "03b_phase_timing_opposing.png")
    field_hardware()
    state_machine()
    sites_overview()
