#!/usr/bin/env bash
# Runs every simulator test against freshly built firmware.
#   sudo apt-get install gcc-avr avr-libc binutils-avr simavr libsimavr-dev libelf-dev
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
B="$R/tools/build_firmware.sh"
P="$R/projects"
OUT="$R/.cache/test-build"
mkdir -p "$OUT"
FAILED=()

run() {  # name, test.c, elf, [extra args]
  local name="$1" src="$2" elf="$3"; shift 3
  echo; echo "################ $name"
  gcc -O1 -I "$R/tools/sim" "$src" -o "$OUT/$name.bin" -lsimavr -lelf -lm
  if ! "$OUT/$name.bin" "$elf" "$@"; then FAILED+=("$name"); fi
}

T="$P/07-traffic-light-controller"
SCALE=20 "$T/tools/build_hex.sh" >/dev/null
SCALE=20 SITE=5 "$T/tools/build_hex.sh" >/dev/null
FAULT_TEST=1 "$T/tools/build_hex.sh" >/dev/null
run traffic-split    "$T/test/sim_test.c" "$T/firmware/build/traffic_controller_site01_sim_x5.elf"
run traffic-opposing "$T/test/sim_test.c" "$T/firmware/build/traffic_controller_site05_sim_x5.elf" opposing
run traffic-fault    "$T/test/sim_test.c" "$T/firmware/build/traffic_controller_site01_faulttest.elf" fault

G="$P/06-gas-smoke-detector-gsm"
"$B" "$G/firmware/gas_detector/gas_detector.ino" "$OUT/gas.hex" \
     -DTIME_SCALE_PERCENT=10 '-DALERT_PHONE_1="+9779800000000"' >/dev/null
run gas-detector "$G/test/sim_test.c" "$OUT/gas.elf"

W="$P/08-water-tank-level-controller"
"$B" "$W/firmware/water_level_controller/water_level_controller.ino" "$OUT/water.hex" \
     -DTIME_SCALE_PERCENT=10 >/dev/null
run water-level "$W/test/sim_test.c" "$OUT/water.elf"

S="$P/09-smart-street-light"
"$B" "$S/firmware/smart_street_light/smart_street_light.ino" "$OUT/street.hex" \
     -DTIME_SCALE_PERCENT=10 -DEVENING_SEC=300 >/dev/null
run street-light "$S/test/sim_test.c" "$OUT/street.elf"

M="$P/10-dc-power-energy-meter"
"$B" "$M/firmware/dc_power_meter/dc_power_meter.ino" "$OUT/meter.hex" >/dev/null
run dc-meter "$M/test/sim_test.c" "$OUT/meter.elf"

echo
if [ ${#FAILED[@]} -eq 0 ]; then
  echo "ALL SIMULATOR TESTS PASSED"
else
  echo "FAILED: ${FAILED[*]}"
  exit 1
fi
