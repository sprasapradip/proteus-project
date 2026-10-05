#!/usr/bin/env bash
# Runs the traffic controller safety test against the Site 1 firmware
# (normal cycle, pedestrian, night flash, emergency, conflict every ms,
# and the forced-conflict FAULT latch), then the countdown test (controller
# A5 link wired to the display unit). Writes test/test-report.txt.
#   sudo apt-get install gcc-avr avr-libc binutils-avr simavr libsimavr-dev libelf-dev
set -euo pipefail
SITE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$SITE/../.." && pwd)"
SKETCH="$SITE/firmware/traffic_controller_site01/traffic_controller_site01.ino"
WORK="$REPO/.cache/site01-test"
mkdir -p "$WORK"
"$REPO/tools/build_firmware.sh" "$SKETCH" "$WORK/site01_sim_x5.hex" -DTIME_SCALE_PERCENT=20 >/dev/null
"$REPO/tools/build_firmware.sh" "$SKETCH" "$WORK/site01_fault.hex" -DTIME_SCALE_PERCENT=20 -DTEST_INJECT_CONFLICT_SEC=8 >/dev/null
"$REPO/tools/build_firmware.sh" "$SITE/firmware/countdown_display/countdown_display.ino" "$WORK/countdown_display.hex" >/dev/null
gcc -O1 -I "$REPO/tools/sim" "$REPO/projects/07-traffic-light-controller/test/sim_test.c" -o "$WORK/sim_test" -lsimavr -lelf
gcc -O1 "$SITE/test/countdown_test.c" -o "$WORK/countdown_test" -lsimavr -lelf
CLEAN='s/\x1b//g; s/\[[0-9;]*m//g'
{
  echo "Site 1 (Narayangarh Pulchowk, Narayani bridge chowk) - simulator test"
  echo "Firmware: firmware/traffic_controller_site01/traffic_controller_site01.ino"
  echo
  echo "=== normal operation (5x speed build) ==="
  "$WORK/sim_test" "$WORK/site01_sim_x5.elf" | sed 's/\x1b//g; s/\[[0-9;]*m//g' | tr -d '\r' | sed 's/\.\.$//'
  echo
  echo "=== forced conflict must latch FAULT ==="
  "$WORK/sim_test" "$WORK/site01_fault.elf" fault | sed 's/\x1b//g; s/\[[0-9;]*m//g' | tr -d '\r' | sed 's/\.\.$//'
  echo
  echo "=== countdown link and display unit (both boards wired together) ==="
  "$WORK/countdown_test" "$WORK/site01_sim_x5.elf" "$WORK/countdown_display.elf" | grep -v '^Loaded' | sed "$CLEAN"
  echo
  echo "=== countdown during FAULT ==="
  "$WORK/countdown_test" "$WORK/site01_fault.elf" "$WORK/countdown_display.elf" fault | grep -v '^Loaded' | sed "$CLEAN"
} | tee "$SITE/test/test-report.txt"
grep -c "Result: PASS" "$SITE/test/test-report.txt" | grep -q 4 && echo "SITE 1: ALL TESTS PASSED"
