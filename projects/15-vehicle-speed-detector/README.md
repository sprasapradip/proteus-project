# Vehicle speed detector with SLOW DOWN sign

Two infrared beams across the road, exactly 1 m apart. A vehicle breaks beam A, then beam B, and the time between them gives its speed. Over the limit, a SLOW DOWN sign flashes and the driver sees their speed. It's made for school zones, hospital roads and busy bazaars, where a sign showing your own speed slows people down more than a fixed board does.

![wiring](images/wiring.png)

## Accuracy

Both beams are on hardware interrupts and timed in microseconds. At 60 km/h a car takes 60 ms to cover 1 m, and the timer resolution is 4 µs. So the measurement error is under 0.01 %, and in practice the beam spacing you measure on site sets the accuracy. Measure it to the centimetre and put it in `BEAM_GAP_M`.

speed (km/h) = gap (m) / time (s) × 3.6

## What it handles

- Both directions: A then B, or B then A. The direction is logged.
- Long vehicles: a bus blocks both beams at once. The next measurement only starts after both beams have been clear for 0.3 s, so one bus is one record.
- Pedestrians and animals: if only one beam breaks, it times out after 2 s and is ignored.
- Noise: both beams "breaking" at the same instant would mean an impossible speed. Anything over 200 km/h is rejected.
- Over the limit (40 km/h by default): the SLOW DOWN sign flashes for 5 s with short beeps, and the green OK lamp goes off.
- Statistics on the LCD: last speed, vehicle count, violations, average speed. The RESET button clears them.

Each vehicle is logged as CSV over USB, which is ready for a traffic survey:

```
VEHICLE,seconds,direction,kmh,over_limit
VEHICLE,184,A>B,36.0,0
VEHICLE,191,B>A,60.0,1
```

## Files

| File | What it is |
|---|---|
| `firmware/speed_detector/speed_detector.ino` | Firmware |
| `firmware/build/speed_detector.hex` | Build |
| `test/sim_test.c` | Simulator test |

## Building it in Proteus

1. Add `ARDUINO UNO`, `LM016L`, `LED-GREEN`, `LED-RED` (the sign), `BUZZER`, `BUTTON`, and 2 × `BUTTON` or `LOGICSTATE` to stand in for the beams.
2. Wire it as in `images/wiring.png`. Set the Arduino's Program File to `firmware/build/speed_detector.hex`.
3. Run it. Press beam A, then beam B, quickly. With two `PULSE` generators set 60 ms apart you get exactly 60 km/h.

Save it as `proteus/speed-detector.pdsprj` and commit it.

## In the field

- Use modulated IR beam pairs (or laser break-beam modules) rated for outdoor use. Plain IR LEDs get blinded by sunlight.
- Mount them at bumper height (40 to 60 cm), on rigid posts so the gap can't change.
- This is a speed awareness sign, not certified enforcement equipment. Fines need a type-approved and calibrated device.

## Testing

The test covers a car at 36 km/h (A>B), a motorbike at 60 km/h (B>A, flagged) with the warning timing out, a 12 m bus counted once at 20 km/h, a pedestrian on one beam, a glitch on both beams, normal operation afterwards, and the counter reset.
