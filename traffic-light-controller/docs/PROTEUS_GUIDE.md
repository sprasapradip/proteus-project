# Running the controller in Proteus

Tested with Proteus 8.13 or newer (Arduino support built in). Older 8.x versions also work if you use the HEX file method.

## About the .pdsprj in this repo

`proteus/traffic-junction-4way.pdsprj` is my 2023 traffic light schematic with the new firmware placed inside it as `main.ino`. Proteus saves the schematic as a binary file, and I couldn't edit it outside Proteus, so the wiring in that file is still the old wiring.

So open it, compare it with the pin table below, and move any wires that don't match. Once it matches, save it and commit it. After that the file is correct for good. If the old drawing is too different, it's quicker to draw a fresh one using the steps in the next section. That takes about 15 minutes.

## Drawing the schematic from scratch

1. New Project, then Schematic (default template), no PCB, no firmware project. We load a HEX instead, which keeps it simple.
2. Pick devices (press `P`) and add:
   - `ARDUINO UNO` (built-in "Arduino Uno R3" or the TEP library one you already have) x1
   - `LED-RED` x5, `LED-YELLOW` x4, `LED-GREEN` x5. That's 4 heads with 3 LEDs each, plus WALK (green) and DON'T WALK (red).
   - `RES` x14, set to 220R
   - `BUTTON` x3 (night, emergency, pedestrian). For night and emergency, `SWITCH` is easier because it stays put.
   - A `GROUND` terminal
3. Place the LEDs as 4 vertical heads around the Arduino: red at the top, yellow in the middle, green at the bottom. Put North at the top of the sheet, East on the right, and so on. It makes the simulation much easier to read.
4. Wire each output pin through a 220R resistor to the LED anode, and the LED cathode to GND:

   | Arduino | LED |
   |---|---|
   | D2, D3, D4 | North red, yellow, green |
   | D5, D6, D7 | East red, yellow, green |
   | D8, D9, D10 | South red, yellow, green |
   | D11, D12, D13 | West red, yellow, green |
   | A3 | WALK (green LED) |
   | A4 | DON'T WALK (red LED) |

5. Inputs: one side of each switch goes to the pin and the other side to GND. You don't need resistors because the firmware turns on the internal pull-ups.

   | Arduino | Switch |
   |---|---|
   | A0 | Night mode |
   | A1 | Emergency hold |
   | A2 | Pedestrian push button |

6. Optional: add a `VIRTUAL TERMINAL` (Instruments). Connect its RXD to Arduino D1/TX and TXD to D0/RX, and set it to 9600 baud. You'll see every state change printed, and you can type `s` for a status report.

The finished drawing should look like `images/02_wiring_diagram.png`.

## Loading the firmware

Use the HEX method. It works on every Proteus version.

1. Double-click the Arduino.
2. In **Program File**, browse to `firmware/build/traffic_controller_site01_sim_x5.hex`.
3. Set **Clock Frequency** to 16 MHz. My old 2023 file had 240 MHz there, which makes the timing wrong.
4. Click Run.

The `_sim_x5` files run every timer 5 times faster, so one full cycle takes 26 s instead of 130 s. For a real-time demo, use `traffic_controller_site01.hex`.

Using the built-in compiler instead (Proteus 8.13+, Source Code tab): replace the contents of `main.ino` with `firmware/traffic_controller/traffic_controller.ino` and build. If you want the faster timing, set `TIME_SCALE_PERCENT` to 20 first.

## What you should see

1. All 4 heads red for 5 s.
2. North green, then yellow, then everything red, then East green, and so on around the junction.
3. Press the pedestrian button. When the current green ends, all heads stay red and WALK lights, then flashes, then DON'T WALK comes back.
4. Close the night switch. The current phase finishes, then all heads flash yellow. Open it again and the junction goes all-red for 5 s before starting over.
5. Close the emergency switch. The green head goes yellow, then everything is red until you release it.

If two greens ever light up together, something is wired to the wrong pin. On real hardware the conflict monitor would latch FAULT, but in Proteus an LED on the wrong pin just looks wrong. Check the table again.

## Opposing plan demo

Load `traffic_controller_site05_sim_x5.hex`. North and South go green together, then East and West. Compare it with `images/03b_phase_timing_opposing.png`.
