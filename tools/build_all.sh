#!/usr/bin/env bash
# Rebuilds every HEX file in the repo. Run from anywhere.
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
B="$R/tools/build_firmware.sh"
P="$R/projects"

echo "== 05 LED matrix scrolling display"
"$B" "$P/05-led-matrix-scrolling-display/firmware/Parola_Scrolling/Parola_Scrolling.ino" \
     "$P/05-led-matrix-scrolling-display/firmware/build/Parola_Scrolling.hex"
# The Proteus file loads the HEX from this exact name, next to the .pdsprj.
cp "$P/05-led-matrix-scrolling-display/firmware/build/Parola_Scrolling.hex" \
   "$P/05-led-matrix-scrolling-display/proteus/Arduino_Code.ino.hex"

echo "== 06 gas / smoke detector"
G="$P/06-gas-smoke-detector-gsm/firmware"
"$B" "$G/gas_detector/gas_detector.ino" "$G/build/gas_detector.hex"
"$B" "$G/gas_detector/gas_detector.ino" "$G/build/gas_detector_sim_x10.hex" \
     -DTIME_SCALE_PERCENT=10 '-DALERT_PHONE_1="+9779800000000"'

echo "== 07 traffic light controller"
"$P/07-traffic-light-controller/tools/build_hex.sh"
SCALE=20 "$P/07-traffic-light-controller/tools/build_hex.sh"
SCALE=20 SITE=5 "$P/07-traffic-light-controller/tools/build_hex.sh"

echo "== 08 water tank level controller"
W="$P/08-water-tank-level-controller/firmware"
"$B" "$W/water_level_controller/water_level_controller.ino" "$W/build/water_level_controller.hex"
"$B" "$W/water_level_controller/water_level_controller.ino" "$W/build/water_level_controller_sim_x10.hex" \
     -DTIME_SCALE_PERCENT=10

echo "== 09 smart street light"
S="$P/09-smart-street-light/firmware"
"$B" "$S/smart_street_light/smart_street_light.ino" "$S/build/smart_street_light.hex"
"$B" "$S/smart_street_light/smart_street_light.ino" "$S/build/smart_street_light_sim_x10.hex" \
     -DTIME_SCALE_PERCENT=10

echo "== 10 DC power & energy meter"
M="$P/10-dc-power-energy-meter/firmware"
"$B" "$M/dc_power_meter/dc_power_meter.ino" "$M/build/dc_power_meter.hex"

echo "all firmware built"
