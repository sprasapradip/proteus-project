#!/usr/bin/env python3
"""Draws the wiring diagrams and reference schematics in projects/*/images/.

    pip install matplotlib schemdraw
    python3 tools/make_diagrams.py

The traffic light controller has its own, bigger script in its tools/ folder.
"""
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import schemdraw
import schemdraw.elements as elm
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle

ROOT = Path(__file__).resolve().parent.parent / "projects"

INK, MUTED, BOARD = "#1d232b", "#6b7280", "#0b7a75"
RED, YEL, GRN, BLU, ORG, PUR, GRY = ("#d62828", "#f4b400", "#2a9d3f", "#1f6feb",
                                     "#e76f51", "#7b2cbf", "#6b7280")


def out(project, name):
    d = ROOT / project / "images"
    d.mkdir(parents=True, exist_ok=True)
    return d / name


# ---------------------------------------------------------------------------
#  Arduino wiring diagrams
# ---------------------------------------------------------------------------
def wiring(project, fname, title, left, right, note=""):
    """left/right: lists of (pin, label, colour). Drawn either side of the board."""
    rows = max(len(left), len(right))
    h = max(7.0, rows * 0.9 + 3)
    fig, ax = plt.subplots(figsize=(13, h * 0.62))
    ax.set_xlim(0, 26)
    ax.set_ylim(0, h)
    ax.axis("off")

    ax.add_patch(FancyBboxPatch((10, 1.0), 6, h - 2.2, boxstyle="round,pad=0.1",
                                fc=BOARD, ec=INK, lw=1.5))
    ax.text(13, h - 1.7, "ARDUINO UNO", ha="center", va="center", color="white",
            fontsize=13, fontweight="bold")

    def side(items, x_pin, x_end, align):
        step = (h - 3.4) / max(len(items), 1)
        y = h - 2.6
        for pin, label, col in items:
            ax.text(x_pin, y, pin, ha="left" if align == "L" else "right", va="center",
                    color="white", fontsize=10, family="monospace")
            xs = (10, x_end) if align == "L" else (16, x_end)
            ax.plot(xs, (y, y), color=col, lw=2.4, solid_capstyle="round")
            ax.add_patch(Circle((x_end, y), 0.16, fc=col, ec=INK, lw=0.8))
            ax.text(x_end + (-0.35 if align == "L" else 0.35), y, label,
                    ha="right" if align == "L" else "left", va="center", fontsize=10, color=INK)
            y -= step

    side(left, 10.2, 7.2, "L")
    side(right, 15.8, 18.8, "R")
    ax.set_title(title, fontsize=14, color=INK, pad=10)
    if note:
        ax.text(13, 0.35, note, ha="center", fontsize=9, color=MUTED)
    fig.savefig(out(project, fname), dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", out(project, fname).relative_to(ROOT.parent))


def all_wiring():
    wiring("05-led-matrix-scrolling-display", "wiring.png",
           "LED matrix scrolling display: Arduino + MAX7219 chain",
           [("5V", "MAX7219 VCC (all modules)", RED), ("GND", "MAX7219 GND", INK)],
           [("D10", "CS / LOAD", ORG), ("D11", "DIN (first module)", BLU),
            ("D13", "CLK", PUR), ("D0/D1", "USB serial, 57600 baud", GRY)],
           "DOUT of each module goes to DIN of the next. Power big chains from a separate 5 V supply.")

    wiring("06-gas-smoke-detector-gsm", "wiring.png",
           "Gas / smoke detector v2 wiring",
           [("A0", "MQ-2 analog out", ORG), ("A1", "MQ-3 analog out", ORG),
            ("D3", "RESET / MUTE button to GND", GRY),
            ("D10", "SIM900 TX", BLU), ("D11", "SIM900 RX", BLU)],
           [("D6", "servo signal (gas valve)", PUR), ("D8", "2N5551 base (1k) -> relay -> fan", RED),
            ("D9", "buzzer", YEL), ("D12", "green OK LED (220R)", GRN),
            ("D13", "red alarm LED (220R)", RED)],
           "SIM900 needs its own 4 V / 2 A supply. Share GND only. Fit a flyback diode across the relay coil.")

    wiring("08-water-tank-level-controller", "wiring.png",
           "Water tank level controller wiring",
           [("D2", "tank probe 25 %", BLU), ("D3", "tank probe 50 %", BLU),
            ("D4", "tank probe 75 %", BLU), ("D5", "tank probe 100 %", BLU),
            ("D6", "sump low-level probe", BLU), ("D7", "AUTO / MANUAL switch", GRY),
            ("D10", "start / stop / reset button", GRY)],
           [("D8", "relay driver -> pump contactor", RED), ("D9", "buzzer", YEL),
            ("D13", "status LED", GRN), ("A0", "LCD RS", PUR), ("A1", "LCD EN", PUR),
            ("A2..A5", "LCD D4..D7", PUR)],
           "Probes read LOW when water touches them. In the field use a probe interface "
           "(transistor + AC excitation) to stop electrode corrosion.")

    wiring("09-smart-street-light", "wiring.png",
           "Smart street light wiring",
           [("A0", "LDR to 5V, 10k to GND", ORG), ("D2", "PIR sensor OUT", BLU),
            ("D4", "TEST button to GND", GRY)],
           [("D9", "PWM -> 100R -> MOSFET gate (IRLZ44N)", RED),
            ("D13", "status LED", GRN)],
           "MOSFET low-side switches the LED module. Use a constant-current LED driver with a PWM "
           "dim input for real street lamps.")

    wiring("10-dc-power-energy-meter", "wiring.png",
           "DC power & energy meter wiring",
           [("A0", "100k / 22k divider from V+", ORG), ("A1", "ACS712-05B OUT", ORG),
            ("D7", "button to GND", GRY)],
           [("D12", "LCD RS", PUR), ("D11", "LCD EN", PUR), ("D5 D4 D3 D2", "LCD D4..D7", PUR),
            ("D8", "relay driver -> load", RED), ("D9", "buzzer", YEL), ("D13", "trip LED", RED)],
           "Put a 5.1 V zener across the 22k resistor so a wrong input can't reach the ADC.")


# ---------------------------------------------------------------------------
#  Reference schematics (analog projects)
# ---------------------------------------------------------------------------
def save_schematic(d, project, fname):
    path = out(project, fname)
    d.save(str(path), dpi=150)
    print("wrote", path.relative_to(ROOT.parent))


def psu():
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2.5, fontsize=11)
        t = d.add(elm.Transformer(t1=4, t2=4).label("230 / 12 V", loc="top"))
        d.add(elm.Line().left().at(t.p1).length(2))
        d.add(elm.SourceSin().down().toy(t.p2).label("230 V AC\n50 Hz", loc="top", ofst=0.3))
        d.add(elm.Line().right().tox(t.p2))
        # Bridge: AC on W/E, DC + on N, - on S
        d.add(elm.Line().right().at(t.s1).length(1.2))
        br = d.add(elm.Rectifier(w=2.4, h=2.4).anchor("W").label("4 x 1N4007", loc="left", ofst=(-0.2, 1.6)))
        d.add(elm.Wire("-|").at(t.s2).to(br.E).linewidth(2))
        d.add(elm.Line().up().at(br.N).length(1.0))
        d.add(elm.Line().right().length(3.2))
        plus = d.here
        c1 = d.add(elm.Capacitor(polar=True).down().length(4.4).label("C1\n1000 µF\n25 V", loc="bot"))
        gnd_y = c1.end[1]
        d.add(elm.Wire("-|").at(br.S).to(c1.end))
        d.add(elm.Line().right().at(plus).length(1.5))
        reg = d.add(elm.VoltageRegulator().right().anchor("in").label("7805", loc="top"))
        d.add(elm.Line().down().at(reg.gnd).toy(gnd_y))
        d.add(elm.Line().right().at(reg.out).length(1.5))
        c2 = d.add(elm.Capacitor().down().toy(gnd_y).label("C2\n100 nF", loc="bot"))
        d.add(elm.Line().right().at(c2.start).length(2))
        c3 = d.add(elm.Capacitor(polar=True).down().toy(gnd_y).label("C3\n10 µF", loc="bot"))
        d.add(elm.Line().left().at(c3.end).tox(c1.end))
        d.add(elm.Line().right().at(c3.start).length(1.5))
        d.add(elm.Dot(open=True).label("+5 V", loc="right"))
        d.add(elm.Line().right().at(c3.end).length(1.5))
        d.add(elm.Dot(open=True).label("0 V", loc="right"))
        save_schematic(d, "01-5v-regulated-power-supply", "reference-schematic.png")


def flasher():
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2.5, fontsize=11)
        op = d.add(elm.Opamp(flip=True).label("LM741", loc="center", ofst=(-0.3, 0)))
        # + input (top) with bias divider and positive feedback
        d.add(elm.Line().left().at(op.in2).length(1.0))
        q = d.add(elm.Dot())
        d.add(elm.Line().left().length(1.5))
        p_ = d.add(elm.Dot())
        d.add(elm.Resistor().up().at(p_.center).length(2.5).label("R1 100k", loc="top"))
        d.add(elm.Vdd().label("+9 V"))
        d.add(elm.Resistor().down().at(p_.center).length(5.5).label("R2 100k", loc="top"))
        g1 = d.add(elm.Ground())
        d.add(elm.Line().right().at(op.out).length(1.0))
        o = d.add(elm.Dot())
        d.add(elm.Line().up().length(2.6))
        d.add(elm.Resistor().left().tox(q.center).label("R3 100k (positive feedback)"))
        d.add(elm.Line().down().toy(q.center))
        # - input (bottom): timing network
        d.add(elm.Line().left().at(op.in1).length(1.0))
        d.add(elm.Line().down().length(1.6))
        m = d.add(elm.Dot())
        d.add(elm.Capacitor().down().toy(g1.start).label("C 100 nF", loc="bot"))
        d.add(elm.Ground())
        d.add(elm.Line().down().at(o.center).toy(m.center))
        d.add(elm.Resistor().left().tox(m.center).label("Rf 1 MΩ", loc="bot"))
        # Output LED
        d.add(elm.Line().right().at(o.center).length(1.0))
        d.add(elm.Resistor().right().label("R5 1k"))
        d.add(elm.LED().down().toy(g1.start).label("LED", loc="bot"))
        d.add(elm.Ground())
        save_schematic(d, "02-opamp-led-flasher", "reference-schematic.png")


def scr():
    with schemdraw.Drawing(show=False) as d:
        d.config(unit=2.5, fontsize=11)
        d.add(elm.BatteryCell().up().label("12 V", loc="left"))
        top = d.here
        d.add(elm.Switch().right().label("S1 RESET\n(latched, closed)"))
        d.add(elm.Resistor().right().label("220 Ω"))
        d.add(elm.LED().right().label("LED"))
        d.add(elm.Line().right().length(1))
        scr = d.add(elm.SCR().down().label("SCR", loc="bot"))
        d.add(elm.Line().down().length(1))
        d.add(elm.Ground())
        d.add(elm.Line().left().tox(top))
        d.add(elm.Line().up().length(1).toy(top.y - 2.5))
        d.add(elm.Line().down().at(scr.gate).length(0.01))
        d.add(elm.Resistor().left().at(scr.gate).label("10 kΩ", loc="bot"))
        d.add(elm.Button().left().label("S2 TRIGGER", loc="bot"))
        d.add(elm.Line().up().toy(top))
        save_schematic(d, "03-scr-latch-circuit", "reference-schematic.png")


if __name__ == "__main__":
    all_wiring()
    for f in (psu, flasher, scr):
        try:
            f()
        except Exception as e:  # keep going so one drawing never blocks the rest
            print("FAILED", f.__name__, e)
