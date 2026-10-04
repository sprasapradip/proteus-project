#!/usr/bin/env bash
# Builds the Site 1 HEX files (controller, 5x controller, countdown display) with the repo's pinned toolchain.
#   sites/site-01-bharatpur-eatwell/tools/build.sh
set -euo pipefail
SITE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$SITE/../.." && pwd)"
SKETCH="$SITE/firmware/traffic_controller_site01/traffic_controller_site01.ino"
OUT="$SITE/firmware/build"
"$REPO/tools/build_firmware.sh" "$SKETCH" "$OUT/traffic_controller_site01.hex"
"$REPO/tools/build_firmware.sh" "$SKETCH" "$OUT/traffic_controller_site01_sim_x5.hex" -DTIME_SCALE_PERCENT=20
"$REPO/tools/build_firmware.sh" "$SITE/firmware/countdown_display/countdown_display.ino" "$OUT/countdown_display.hex"
