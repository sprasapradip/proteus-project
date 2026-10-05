#!/usr/bin/env python3
"""Draws the Site 1 layout, timing plan and countdown wiring into ../images/.

    python3 sites/site-01-narayangarh-pulchowk/tools/make_images.py

The layout is drawn North up. Not to scale.
"""
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, FancyBboxPatch, Polygon, Rectangle

OUT = Path(__file__).resolve().parent.parent / "images"
OUT.mkdir(exist_ok=True)

INK, MUTED = "#1d232b", "#6b7280"
RED, YEL, GRN, BLU = "#d62828", "#f4b400", "#2a9d3f", "#1f6feb"
ROAD, ROAD_HWY, VERGE, BUILD = "#555a63", "#4a4e57", "#e7ecdf", "#d9d2c3"

# Site 1 timing (same numbers as the firmware profile)
GREEN = {"N": 30, "E": 35, "S": 20, "W": 35}
YELLOW, ALL_RED, PED_WALK, PED_FLASH = 4, 3, 10, 15
ARM = {"N": "North: Pokhara bus park", "E": "East: Birendra Campus / Tandi", "S": "South: Rampur", "W": "West: Narayani bridge"}


def head(ax, x, y, horizontal, lit=None, s=1.0):
    """3-aspect signal head."""
    w, h = (1.7 * s, 0.62 * s) if horizontal else (0.62 * s, 1.7 * s)
    ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h, boxstyle="round,pad=0.04,rounding_size=0.12",
                                fc="#111", ec="#000", zorder=8))
    for i, c in enumerate((RED, YEL, GRN)):
        off = (i - 1) * 0.5 * s
        cx, cy = (x + off, y) if horizontal else (x, y - off)
        ax.add_patch(Circle((cx, cy), 0.18 * s, fc=c if lit in (None, c) else "#333", ec="none", zorder=9))


def label(ax, x, y, text, ha="center", va="center", size=9.5, weight="normal", color=INK, box=True):
    kw = dict(boxstyle="round,pad=0.3", fc="white", ec="#c9ccd1", lw=0.8) if box else None
    ax.text(x, y, text, ha=ha, va=va, fontsize=size, fontweight=weight, color=color, zorder=12, bbox=kw)


def layout():
    fig, ax = plt.subplots(figsize=(11, 11))
    ax.set_xlim(-26, 26)
    ax.set_ylim(-26, 26)
    ax.set_aspect("equal")
    ax.axis("off")

    ax.add_patch(Rectangle((-26, -26), 52, 52, fc=VERGE, zorder=0))
    HW, SR = 4.5, 3.4            # half widths: Mahendra Highway (E-W), N-S roads

    # Narayani river on the west side, with the bridge carrying the highway
    ax.add_patch(Rectangle((-26, -26), 6.5, 52, fc="#7fb7a8", zorder=1))
    ax.text(-22.75, 15, "NARAYANI\nRIVER", ha="center", va="center", fontsize=9, color="white",
            fontweight="bold", rotation=90, zorder=2)

    # Building blocks in the four corners
    for x0, x1 in [(-19.5, -SR - 1.2), (SR + 1.2, 26)]:
        for y0, y1 in [(HW + 1.2, 26), (-26, -HW - 1.2)]:
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, fc=BUILD, ec="#c8bfae", lw=0.8, zorder=1))

    # Roads
    ax.add_patch(Rectangle((-26, -HW), 52, 2 * HW, fc=ROAD_HWY, zorder=2))
    ax.add_patch(Rectangle((-SR, -26), 2 * SR, 52, fc=ROAD, zorder=2))
    ax.add_patch(Rectangle((-SR, -HW), 2 * SR, 2 * HW, fc=ROAD_HWY, zorder=3))
    for y in (HW + 0.15, -HW - 0.15):           # bridge rails
        ax.plot([-26, -19.5], [y, y], color="#d9dde2", lw=2.2, zorder=3)
    for k in range(-26, 26, 3):
        if abs(k) > SR + 2.6 and abs(k + 1.6) > SR + 2.6:
            ax.plot([k, k + 1.6], [0, 0], color="white", lw=2, zorder=4)
        if abs(k) > HW + 2.6 and abs(k + 1.6) > HW + 2.6:
            ax.plot([0, 0], [k, k + 1.6], color="white", lw=2, zorder=4)

    # Zebra crossings
    for i in range(7):
        y = -HW + 0.6 + i * (2 * HW - 1.2) / 6
        for x in (SR + 0.6, -SR - 2.0):
            ax.add_patch(Rectangle((x, y - 0.3), 1.4, 0.6, fc="white", zorder=4))
    for i in range(5):
        x = -SR + 0.6 + i * (2 * SR - 1.2) / 4
        for y in (HW + 0.6, -HW - 2.0):
            ax.add_patch(Rectangle((x - 0.3, y), 0.6, 1.4, fc="white", zorder=4))

    # Left-hand traffic, North up: eastbound uses the north half of the
    # highway, westbound the south half; southbound the east half of the N-S
    # road, northbound the west half. Stop lines only across incoming lanes.
    ax.plot([-SR - 2.6, -SR - 2.6], [0, HW], color="white", lw=3.5, zorder=5)    # from West
    ax.plot([SR + 2.6, SR + 2.6], [-HW, 0], color="white", lw=3.5, zorder=5)     # from East
    ax.plot([0, SR], [HW + 2.6, HW + 2.6], color="white", lw=3.5, zorder=5)      # from North
    ax.plot([-SR, 0], [-HW - 2.6, -HW - 2.6], color="white", lw=3.5, zorder=5)   # from South

    arrow = dict(arrowstyle="-|>", mutation_scale=20, lw=2.6, color=YEL, zorder=6)
    ax.add_patch(FancyArrowPatch((-18, HW / 2), (-SR - 4.2, HW / 2), **arrow))    # from West
    ax.add_patch(FancyArrowPatch((24, -HW / 2), (SR + 4.2, -HW / 2), **arrow))    # from East
    ax.add_patch(FancyArrowPatch((SR / 2, 24), (SR / 2, HW + 4.2), **arrow))      # from North
    ax.add_patch(FancyArrowPatch((-SR / 2, -24), (-SR / 2, -HW - 4.2), **arrow))  # from South

    # Poles: near-left corner of each approach (left-hand traffic)
    poles = {
        "P1": ((SR + 1.4, HW + 1.4), True),      # NE corner, traffic from the North
        "P2": ((SR + 1.4, -HW - 1.4), False),    # SE corner, traffic from the East
        "P3": ((-SR - 1.4, -HW - 1.4), True),    # SW corner, traffic from the South
        "P4": ((-SR - 1.4, HW + 1.4), False),    # NW corner, traffic from the West
    }
    for tag, ((x, y), horiz) in poles.items():
        ax.add_patch(Circle((x, y), 0.55, fc="#9aa0a6", ec=INK, lw=1, zorder=8))
        dx = 0 if not horiz else (1.9 if x > 0 else -1.9)
        dy = 0 if horiz else (1.9 if y > 0 else -1.9)
        head(ax, x + dx, y + dy, horiz)
        ax.add_patch(Rectangle((x - 0.25, y - 0.25), 0.5, 0.5, fc=BLU, ec="none", zorder=9))  # push button

    label(ax, 14.5, 13.2, "P1  traffic from the NORTH\n(Pokhara bus park road)\nNE corner", size=9.5)
    label(ax, 14.5, -13.2, "P2  traffic from the EAST\n(Birendra Campus / Tandi)\nSE corner", size=9.5)
    label(ax, -11.5, -13.2, "P3  traffic from the SOUTH\n(Rampur road)\nSW corner", size=9.5)
    label(ax, -11.5, 13.2, "P4  traffic from the WEST\n(off the Narayani bridge)\nNW corner", size=9.5)

    # Controller cabinet (SW corner beside P3, position to be agreed on site)
    ax.add_patch(FancyBboxPatch((-11.5, -HW - 4.0), 3.2, 1.8, boxstyle="round,pad=0.05", fc=BLU, ec=INK, zorder=9))
    ax.text(-9.9, -HW - 3.1, "Cabinet", ha="center", va="center", color="white", fontsize=8.5,
            fontweight="bold", zorder=10)
    duct = [(-8.3, -HW - 3.1), (-SR - 1.4, -HW - 3.1), (-SR - 1.4, -HW - 1.4), (SR + 1.4, -HW - 1.4),
            (SR + 1.4, HW + 1.4), (-SR - 1.4, HW + 1.4), (-SR - 1.4, -HW - 1.4)]
    xs, ys = zip(*duct)
    ax.plot(xs, ys, ls="--", color=BLU, lw=1.4, zorder=7)

    # Arm names
    label(ax, 0, 24.6, "NORTH  to Pokhara bus park", size=11, weight="bold")
    label(ax, 0, -24.6, "SOUTH  to Rampur", size=11, weight="bold")
    ax.text(25, -HW - 1.3, "EAST  Mahendra Highway\nto Birendra Campus / Tandi", fontsize=10.5,
            fontweight="bold", color=INK, zorder=12, ha="right", va="top",
            bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="#c9ccd1"))
    ax.text(-19.0, HW + 4.2, "WEST  Mahendra Highway\nto the Narayani bridge", fontsize=10,
            fontweight="bold", color=INK, zorder=12, ha="left", va="bottom",
            bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="#c9ccd1"))

    # Compass, North up
    cx, cy = 21.0, 21.0
    ax.add_patch(Circle((cx, cy), 3.1, fc="white", ec=INK, lw=1.2, zorder=10))
    ax.add_patch(Polygon([(cx, cy + 2.6), (cx - 0.7, cy - 0.6), (cx + 0.7, cy - 0.6)], fc=RED, ec="none", zorder=11))
    for t, (dx, dy) in {"N": (0, 3.9), "S": (0, -3.9), "E": (3.9, 0), "W": (-3.9, 0)}.items():
        ax.text(cx + dx, cy + dy, t, ha="center", va="center", fontsize=11, fontweight="bold", zorder=11,
                color=RED if t == "N" else INK)

    ax.set_title("Site 1  -  Narayangarh Pulchowk (Narayani bridge chowk), Chitwan",
                 fontsize=13.5, color=INK, pad=12)
    fig.text(0.5, 0.075, "North at the top. Not to scale.\nYellow = incoming lane (left-hand traffic). "
             "Grey circle = pole, blue square = pedestrian button, dashed blue = duct ring.",
             ha="center", fontsize=9.5, color=MUTED)
    fig.savefig(OUT / "site01-layout.png", dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", OUT / "site01-layout.png")


def timing():
    rows = ["N", "E", "S", "W"]
    fig, ax = plt.subplots(figsize=(13, 4.4))
    t = 0
    seg = {r: [] for r in rows}
    marks = []
    for ph in rows:
        g = GREEN[ph]
        for r in rows:
            if r == ph:
                seg[r] += [(t, g, GRN), (t + g, YELLOW, YEL), (t + g + YELLOW, ALL_RED, RED)]
            else:
                seg[r] += [(t, g + YELLOW + ALL_RED, RED)]
        marks.append((t, ph))
        t += g + YELLOW + ALL_RED
    for i, r in enumerate(rows):
        yy = len(rows) - 1 - i
        for start, dur, col in seg[r]:
            ax.broken_barh([(start, dur)], (yy - 0.35, 0.7), facecolors=col, edgecolor="white", linewidth=0.8)
            if dur >= 4:
                ax.text(start + dur / 2, yy, f"{dur}s", ha="center", va="center",
                        color="white" if col != YEL else INK, fontsize=8.5)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([ARM[r] for r in reversed(rows)])
    ax.set_xlim(0, t)
    ax.set_ylim(-0.6, 4.0)
    ax.set_xlabel("seconds into cycle")
    for x, ph in marks:
        ax.axvline(x, color=MUTED, lw=0.6, ls=":")
        ax.text(x + 0.6, 3.62, f"phase {ph}", fontsize=8.5, color=MUTED)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.set_title(f"Site 1 timing plan: split phasing, cycle {t} s  "
                 f"(yellow {YELLOW} s, all-red {ALL_RED} s; pedestrian walk {PED_WALK} s + flash {PED_FLASH} s on request)",
                 color=INK, fontsize=11.5)
    fig.savefig(OUT / "site01-timing.png", dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", OUT / "site01-timing.png")


def seg7(ax, x, y, text, color, h=1.0):
    """Draws a 4-character 7-segment readout (digits, space, G, Y, r, -)."""
    SEGS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd",
            "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcfgd", " ": "", "-": "g",
            "G": "acdef", "Y": "bcdfg", "r": "eg"}
    w = 0.55 * h
    ax.add_patch(FancyBboxPatch((x - 0.15, y - 0.2), len(text) * (w + 0.25) + 0.1, h + 0.4,
                                boxstyle="round,pad=0.05", fc="#101214", ec="#000", zorder=3))
    for i, ch in enumerate(text):
        x0 = x + i * (w + 0.25)
        lines = {"a": ((x0, y + h), (x0 + w, y + h)), "b": ((x0 + w, y + h), (x0 + w, y + h / 2)),
                 "c": ((x0 + w, y + h / 2), (x0 + w, y)), "d": ((x0, y), (x0 + w, y)),
                 "e": ((x0, y), (x0, y + h / 2)), "f": ((x0, y + h / 2), (x0, y + h)),
                 "g": ((x0, y + h / 2), (x0 + w, y + h / 2))}
        for k, (p0, p1) in lines.items():
            on = k in SEGS[ch]
            ax.plot([p0[0], p1[0]], [p0[1], p1[1]], color=color if on else "#262a2e",
                    lw=3.2, solid_capstyle="round", zorder=4)


def countdown_wiring():
    fig, ax = plt.subplots(figsize=(13, 7.6))
    ax.set_xlim(0, 26)
    ax.set_ylim(0, 15.2)
    ax.axis("off")

    def box(x, y, w, h, title, lines, fc="#eef3fb", ec=BLU):
        ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.1,rounding_size=0.25",
                                    fc=fc, ec=ec, lw=1.4, zorder=1))
        ax.text(x + w / 2, y + h - 0.45, title, ha="center", va="center", fontsize=11, fontweight="bold", color=INK)
        for i, t in enumerate(lines):
            ax.text(x + 0.3, y + h - 1.15 - i * 0.55, t, ha="left", va="center", fontsize=9, color=INK,
                    family="monospace")

    def wire(p, q, text="", color=INK, dy=0.18):
        ax.annotate("", xy=q, xytext=p, arrowprops=dict(arrowstyle="-|>", color=color, lw=1.6), zorder=2)
        if text:
            ax.text((p[0] + q[0]) / 2, (p[1] + q[1]) / 2 + dy, text, ha="center", va="bottom", fontsize=8.8,
                    color=color)

    box(0.4, 7.2, 5.6, 6.6, "Traffic controller", ["Arduino Uno #1", "traffic_controller_",
        "  site01(_sim_x5).hex", "", "D2-D13  lamps", "A0-A4   inputs, ped", "A5      countdown out",
        "GND"], fc="#fdf1f1", ec=RED)
    box(8.3, 7.2, 5.6, 6.6, "Countdown unit", ["Arduino Uno #2", "countdown_display.hex", "",
        "D0 RX   link in", "D11     DIN  (U1+U2)", "D13     CLK  (U1+U2)", "D10     LOAD U1",
        "D9      LOAD U2", "D7      link LED"], fc="#eefaf0", ec=GRN)
    wire((6.0, 9.4), (8.3, 9.4), "")
    ax.text(7.15, 10.0, "A5 -> D0 (RX)\n9600 baud", ha="center", va="bottom", fontsize=8.8, color=INK)
    wire((6.0, 8.2), (8.3, 8.2), "GND - GND", color=MUTED)
    box(0.4, 0.5, 13.5, 5.6, "", [], fc="#fafafa", ec="#c9ccd1")
    ax.text(7.15, 5.6, "Optional: VIRTUAL TERMINAL on A5 shows the raw frames", ha="center", fontsize=9.5,
            color=MUTED)
    ax.text(0.8, 4.6, "$CD,G,G030,R037,R079,R106*77", fontsize=10.5, family="monospace", color=INK)
    rows = [("state", "U start, G, Y, A all-red, W/P/C walk, N night, E emerg., F fault"),
            ("G030", "green, 30 s left"), ("R037", "red, own green starts in 37 s"),
            ("R---  F---  X---", "no time: emergency / night flash / fault"), ("*77", "XOR checksum")]
    for i, (k, v) in enumerate(rows):
        ax.text(0.8, 3.7 - i * 0.62, k, fontsize=9, family="monospace", color=BLU)
        ax.text(4.6, 3.7 - i * 0.62, v, fontsize=9, color=INK)

    # MAX7219 chips and displays
    for j, (chip, arms) in enumerate([("U1 MAX7219", (("N  Pokhara bus park, P1", "G 30", GRN),
                                                      ("E  Birendra Campus / Tandi, P2", "r 37", RED))),
                                      ("U2 MAX7219", (("S  Rampur, P3", "r 79", RED),
                                                      ("W  Narayani bridge, P4", "r106", RED)))]):
        y0 = 8.2 - j * 6.6
        ax.add_patch(FancyBboxPatch((15.3, y0), 2.6, 5.4, boxstyle="round,pad=0.08", fc="#20242a", ec="#000",
                                    zorder=1))
        ax.text(16.6, y0 + 4.9, chip, ha="center", color="white", fontsize=9.5, fontweight="bold")
        for k, t in enumerate(["DIN", "CLK", "LOAD", "ISET-10k-5V"]):
            ax.text(15.45, y0 + 4.1 - k * 0.5, t, color="#cfd3d8", fontsize=8, family="monospace")
        ax.text(15.45, y0 + 1.6, "SEG A-G, DP\nto both displays", color="#cfd3d8", fontsize=7.6, va="top")
        for k, (name, txt, col) in enumerate(arms):
            yy = y0 + 3.2 - k * 2.9
            seg7(ax, 19.6, yy, txt, col, h=1.3)
            ax.text(19.45, yy + 1.85, name, fontsize=9, color=INK)
            ax.text(25.9, yy + 0.55, f"DIG{k * 4}-{k * 4 + 3}", fontsize=8, color=MUTED, ha="right")
            wire((17.9, yy + 0.65), (19.35, yy + 0.65), color=MUTED)
        wire((13.9, 11.0 - j * 1.4), (15.3, y0 + 3.6), color=GRN)
        ax.text(14.45, (11.0 - j * 1.4 + y0 + 3.6) / 2 + (0.5 if j == 0 else -0.3),
                "D11, D13,\n" + ("D10" if j == 0 else "D9"), ha="center", fontsize=8.2, color=GRN)

    ax.set_title("Site 1 countdown display: wiring and link format (4-digit common-cathode displays, "
                 "7SEG-MPX4-CC in Proteus)", fontsize=12, color=INK)
    fig.savefig(OUT / "site01-countdown-wiring.png", dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", OUT / "site01-countdown-wiring.png")


if __name__ == "__main__":
    layout()
    timing()
    countdown_wiring()
