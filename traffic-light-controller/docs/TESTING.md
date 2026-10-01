# Testing

I test in three stages: the simulator on a PC, a bench test with LEDs, and then the junction itself.

## 1. Automatic simulator test (Linux / WSL)

This runs the actual compiled firmware inside simavr, the same machine code that goes on the Uno. It presses the inputs on a timeline and checks the outputs every simulated millisecond.

```bash
sudo apt-get install gcc-avr avr-libc binutils-avr simavr libsimavr-dev libelf-dev
cd traffic-light-controller

SCALE=20 tools/build_hex.sh              # split plan, site 1
SCALE=20 SITE=5 tools/build_hex.sh       # opposing plan, site 5

gcc tools/test/sim_test.c -o /tmp/sim_test -lsimavr -lelf
/tmp/sim_test firmware/build/traffic_controller_site01_sim_x5.elf
/tmp/sim_test firmware/build/traffic_controller_site05_sim_x5.elf opposing
```

The build script keeps the `.elf` files next to the HEX files, and the test needs them. Each run takes about a minute.

It checks these things all the time:

- no two conflicting greens (in split mode only one green at a time; in opposing mode N/S never green together with E/W)
- green never lit together with red or yellow on the same head
- WALK never on while any vehicle head is green or yellow
- WALK and DON'T WALK never on together

And it runs this scenario:

| Step | Input | Expected |
|---|---|---|
| 1 | none | every approach gets green, no WALK |
| 2 | pedestrian button | WALK appears |
| 3 | night switch on | flashing yellow, no green |
| 4 | night switch off | normal cycle again |
| 5 | emergency on | all red for the whole hold |
| 6 | emergency off | traffic resumes |

Last result on my machine:

```
[1] normal cycle
  every approach received green                           ok
  no WALK without a request                               ok
[2] pedestrian request
  WALK shown after request                                ok
[3] night mode
  flashing yellow, no green                               ok
[4] night mode off
  returns to normal through all-red                       ok
[5] emergency hold
  all red for the whole hold                              ok
[6] emergency released
  traffic resumes after release                           ok

Result: PASS (0 problems)
```

Both the split (site 1) and opposing (site 5) builds pass.

### Proving the fault latch

A normal build never produces a conflict, so the monitor never gets exercised. There's a separate test build for that. It asks for green on every arm 8 s after boot:

```bash
FAULT_TEST=1 tools/build_hex.sh
/tmp/sim_test firmware/build/traffic_controller_site01_faulttest.elf fault
```

```
[F] injected conflict
  no green after the conflict                             ok
  all heads flashing red                                  ok
  stays latched after inputs toggle                       ok
```

The bad pattern never reaches the pins, because the check runs before the write. The script deletes the HEX for this build so nobody can flash it to a junction by mistake.

Run it again after every change to the sketch. If it fails, don't flash that build to a junction.

## 2. Bench test

Build the circuit from `images/02_wiring_diagram.png` on a breadboard, with the real driver board if you have it. Leave it running overnight with the serial log saved to a file:

```bash
# Linux
stty -F /dev/ttyUSB0 9600 raw && cat /dev/ttyUSB0 | tee bench_$(date +%F).log
```

Next morning, check:

- no `reset=WATCHDOG` or `BROWN-OUT` lines after the first boot
- no `FAULT` lines
- the cycle count in `s` status roughly matches the hours (site 1: about 27 cycles per hour)

The read-back check only catches hard faults, such as a damaged pin or a dead short on the board. A weak short through a resistor won't trip it, because the AVR output is strong enough to hold its level. Use the fault test build above to see the FAULT behaviour. Real lamp failure detection needs current sensing (see the end of `INSTALLATION_GUIDE.md`).

## 3. On site

Follow section 8 of `INSTALLATION_GUIDE.md`, and write the result in `SITE_REGISTER.md`.
