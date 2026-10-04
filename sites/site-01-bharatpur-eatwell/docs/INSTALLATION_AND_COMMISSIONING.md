# Site 1 installation and commissioning

Mahendra Highway junction at Eatwell Bakery Cafe, Bharatpur. Firmware `BRT-EATWELL` (`traffic_controller_site01.hex`).

Print this page and tick each line on site. Do it with the traffic police present.

## 1. Before starting

- [ ] Timing plan approved (traffic police Chitwan / road authority): reference no. ____________
- [ ] Pole and cabinet positions agreed with the municipality and the shop owners at each corner
- [ ] Junction width measured: ______ m (design value about 20 m)
- [ ] Highway crossing length measured: ______ m (design value about 15 m)
- [ ] If the measurements differ much, the timings were updated and the firmware rebuilt with `tools/build.sh`

## 2. Poles (near-left corner of each approach, about 1 m behind the stop line)

| Pole | Corner | Next to | Controls | Done |
|---|---|---|---|---|
| P1 | NE | Namaste Mero Mobile | Traffic from the NORTH on Mahendra Highway | [ ] |
| P2 | SE | International Courier | Traffic from the EAST side road | [ ] |
| P3 | SW | Eatwell Bakery Cafe | Traffic from the SOUTH on Mahendra Highway | [ ] |
| P4 | NW | Infotech Computer | Traffic from the WEST side road | [ ] |

- [ ] Each pole at least 0.6 m back from the kerb, lowest lamp 2.5 to 3 m above the footpath
- [ ] Each head turned to face drivers at its own stop line
- [ ] Highway repeater heads (if fitted) on the far side, wired in parallel with P1 and P3
- [ ] Pedestrian heads face across each zebra crossing, and push buttons are at reachable height

## 3. Cabinet, power and cabling

- [ ] Cabinet on its plinth at the agreed corner, doors lockable
- [ ] Supply via meter, MCB 6 A, RCCB 30 mA and the surge protector; earth pit < 5 Ω: measured ______ Ω
- [ ] Every pole and the cabinet bonded to earth
- [ ] Cables labelled at both ends (`P1-R`, `P1-Y`, `P1-G`, ...), terminal blocks in pin-table order
- [ ] Lamp feeds go through the driver board only (never straight from the Arduino), with a fuse per pole
- [ ] Battery backup charged; the controller keeps running with the mains breaker off: ______ min tested

## 4. Firmware

- [ ] Flashed `firmware/build/traffic_controller_site01.hex` (real-time, NOT `_sim_x5`)
- [ ] Serial log at 9600 baud shows:
  - [ ] `BRT-EATWELL ... boot, reset=POWER-ON`
  - [ ] `4 phases, plan=SPLIT`
  - [ ] the four arm lines (N Mahendra Hwy north P1 ... W side road west P4)
- [ ] Arduino labelled "SITE 1 BRT-EATWELL"; the spare board is flashed and labelled too
- [ ] Countdown unit flashed with `firmware/build/countdown_display.hex`, labelled "SITE 1 COUNTDOWN"
- [ ] Boot log also shows `countdown link on A5, 9600 baud`

## 5. Switch-on tests (heads covered or turned away until test 9)

| # | Test | Expected | OK |
|---|---|---|---|
| 1 | Power on | All 4 poles red for 5 s | [ ] |
| 2 | Watch one cycle | P1 green 35 s, then P2 20 s, P3 35 s, P4 20 s; yellow 4 s and all-red 3 s between each | [ ] |
| 3 | Check each green against the road | P1 green lets the north highway traffic go, P2 the east side road, and so on | [ ] |
| 4 | Pedestrian button (try each corner) | After the current green: all vehicle heads red, WALK 10 s, flashing 15 s, then DON'T WALK | [ ] |
| 5 | Night timer on | Current phase finishes, then all heads flash yellow | [ ] |
| 6 | Night timer off | All red 5 s, then the normal cycle | [ ] |
| 7 | Emergency key on | Green goes yellow, then all red and held | [ ] |
| 8 | Emergency key off | All red 5 s, then the normal cycle | [ ] |
| 9 | Uncover the heads, watch 3 cycles in real traffic | No conflicts, queues clear; note any arm that needs more green | [ ] |
| 10 | Status check | Type `s` in the serial terminal: state, phase and cycle count look right | [ ] |
| 11 | Countdown at power-on | All countdown segments light for 1 s, then each arm shows `r` and a number during start-up | [ ] |
| 12 | Countdown during a cycle | P1 shows `G 35` counting down to 1, then `Y 4`; the arm due next reaches 1 just as its green comes on | [ ] |
| 13 | Countdown in night / emergency | All four displays show `----` | [ ] |
| 14 | Countdown link cut | Unplug the link: displays go blank within 3 s, lamps keep running normally | [ ] |

## 6. Notes from switch-on

Queue observations, any timing changes made, problems found:

______________________________________________________________________

______________________________________________________________________

## 7. Sign-off

| Role | Name | Signature | Date |
|---|---|---|---|
| Installer | Pradip Subedi | | |
| Traffic police | | | |
| Road authority / municipality | | | |
