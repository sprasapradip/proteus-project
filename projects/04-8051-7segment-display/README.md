# 8051 multiplexed 7-segment display

An 80C51 microcontroller driving two 4-digit common-cathode 7-segment displays (7SEG-MPX4-CC), with 100 Ω segment resistors. Multiplexing means only one digit is on at any moment. The code cycles through the digits fast enough (about 1 ms each) that your eye sees all of them lit.

## Files

| File | What it is |
|---|---|
| `proteus/8051-7segment-multiplexed.pdsprj` | 80C51 + 2 × 7SEG-MPX4-CC schematic |

## Missing firmware

The schematic loads its program from `Objects\sevenSegmentMultiplexed.hex`, relative to the project. That HEX file (and its source) were never committed in 2023, so the simulation currently has no program to run.

To bring it back:

1. Write the program in Keil µVision or SDCC. Segment data goes out on the port wired to the resistors, and digit select on the port wired to the common pins. Check the schematic to see which ports those are.
2. Build it and save the HEX as `proteus/Objects/sevenSegmentMultiplexed.hex`.
3. Open the project and run it. The 80C51 picks the file up automatically.

If you have the original source on your PC, add it under `firmware/` and commit it, so the project is complete again.
