#!/usr/bin/env bash
# Builds the traffic controller HEX files with the repo's shared, pinned
# toolchain script (tools/build_firmware.sh).
#
# Usage:
#   tools/build_hex.sh                 # SITE_ID 1..8, real time
#   SCALE=20 tools/build_hex.sh        # 5x faster timing for Proteus demos (site 1)
#   SCALE=20 SITE=5 tools/build_hex.sh
#   FAULT_TEST=1 tools/build_hex.sh    # test build that forces a conflict at 8 s
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(cd "$HERE/../.." && pwd)/tools/build_firmware.sh"
SKETCH="$HERE/firmware/traffic_controller/traffic_controller.ino"
OUT="$HERE/firmware/build"
SCALE="${SCALE:-100}"
SITE="${SITE:-1}"

if [ "${FAULT_TEST:-0}" = "1" ]; then
  "$BUILD" "$SKETCH" "$OUT/traffic_controller_site01_faulttest.hex" \
    -DSITE_ID=1 -DTIME_SCALE_PERCENT=20 -DTEST_INJECT_CONFLICT_SEC=8
  rm -f "$OUT/traffic_controller_site01_faulttest.hex"   # never ship this one
elif [ "$SCALE" = "100" ]; then
  for s in 1 2 3 4 5 6 7 8; do
    "$BUILD" "$SKETCH" "$OUT/traffic_controller_site0$s.hex" -DSITE_ID=$s
  done
else
  "$BUILD" "$SKETCH" "$OUT/traffic_controller_site0${SITE}_sim_x$((100 / SCALE)).hex" \
    -DSITE_ID=$SITE -DTIME_SCALE_PERCENT=$SCALE
fi
