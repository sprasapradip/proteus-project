# Site 1 bill of materials

For Narayangarh Pulchowk, the Narayani bridge chowk: one cabinet, four signal poles and four countdown displays. Quantities are for this junction. Prices aren't included because they change, so get quotes locally.

## Controller cabinet

| # | Item | Qty | Notes |
|---|---|---|---|
| 1 | Arduino Uno R3 (genuine or good-quality clone), flashed with `traffic_controller_site01.hex` | 1 + 1 spare | Label both "SITE 1 NGH-PULCHOWK" |
| 2 | Lamp driver board, 14 channels: logic-level MOSFET (IRLZ44N or similar) per channel, 100 Ω gate resistor, 10 kΩ gate pull-down, TVS diode per output | 1 | The pull-downs make all lamps go dark if the Arduino fails |
| 3 | Enclosure, IP65, about 600 × 400 × 200 mm, lockable, with a filtered vent | 1 | On a 300 mm concrete plinth |
| 4 | DIN rail and terminal blocks | 1 set (about 40 terminals) | Laid out in the same order as the pin table |
| 5 | MCB 6 A + RCCB 30 mA | 1 each | |
| 6 | Type 2 surge protection device, 230 V | 1 | |
| 7 | 24 V DC SMPS, 10 A (240 W), Mean Well class | 1 | Load is about 100 W with every head lit, so this leaves margin for repeaters |
| 8 | Battery backup: 2 × 12 V 40 Ah AGM in series + 24 V charger | 1 set | About 4 to 5 hours at 100 W, to cover load shedding |
| 9 | DC-DC buck converter, 24 V to 9 V, 1 A | 1 | Feeds the Arduino VIN, kept separate from the lamp supply |
| 10 | 24 h digital timer (contact output) | 1 | Night mode on A0, suggested 23:00 to 05:00 |
| 11 | Key switch, 2-position | 1 | Emergency all-red hold on A1, inside the cabinet |
| 12 | 100 nF capacitors | 4 | One per input at the terminal block |
| 13 | Earth pit with electrode, < 5 Ω | 1 | Bond the cabinet and all poles to it |

## Poles and signal heads

| # | Item | Qty | Notes |
|---|---|---|---|
| 14 | Signal pole, galvanised, 3.5 to 4 m, with base frame | 4 | P1 NE (Pokhara bus park road), P2 SE (Birendra Campus / Tandi), P3 SW (Rampur road), P4 NW (Narayani bridge) |
| 15 | Vehicle signal head, 300 mm, 3 aspects (R/Y/G), 24 V DC LED | 4 | One per pole, facing its approach |
| 16 | Repeater head, 300 mm, 3 aspects (recommended for the highway) | 2 | On the far side for the east and west highway approaches, wired in parallel with P2 and P4 |
| 17 | Pedestrian head, 200 mm, red man / green man, 24 V DC LED | 8 | Two per corner, one facing each crossing. All in parallel on A3/A4 |
| 18 | Pedestrian push button, IP65 | 4 | One per corner, all in parallel on A2 |

## Countdown displays

| # | Item | Qty | Notes |
|---|---|---|---|
| 18a | Arduino Uno R3 for the countdown unit, flashed with `countdown_display.hex` | 1 + 1 spare | Label "SITE 1 COUNTDOWN" |
| 18b | MAX7219 driver (DIP-24 or a ready module) + 10 kΩ ISET resistor, 100 nF and 10 µF decoupling | 2 | U1 drives N and E, U2 drives S and W |
| 18c | Countdown display, 4 digits (letter + 3 digits), weatherproof, high-brightness LED | 4 | One per pole, under the signal head. For field units with big digits, use a constant-current LED driver board per digit in place of bare 7-segment parts |
| 18d | RS485 transceiver module (MAX485) | 2 or more | One at the controller's A5, one at each countdown unit. Needed for any link over a few metres |
| 18e | Shielded twisted pair for the link | Same route as the lamp cable | |

## Cable and civil work

| # | Item | Qty | Notes |
|---|---|---|---|
| 19 | Armoured cable, 7 core × 1.5 mm² | Measure on site (ring + 4 risers + 20 % spare) | R, Y, G, WALK, DON'T WALK, common, earth |
| 20 | Shielded cable, 2 core, for the push buttons | Same route | |
| 21 | HDPE duct, 100 mm | Measure on site | One crossing under each arm, as a ring (see the layout) |
| 22 | Draw pits at each pole base | 4 | |
| 23 | Cable labels | 1 set | For example `P3-G` at both ends |

## Tools for commissioning

Laptop with a USB cable and a serial terminal (9600 baud), a multimeter, avrdude or the Arduino IDE, and a tape measure for the junction and crossing widths.
