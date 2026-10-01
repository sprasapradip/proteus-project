#!/usr/bin/env bash
# Builds firmware/build/*.hex without the Arduino IDE.
# Needs: avr-gcc, avr-libc, binutils-avr, git
#   sudo apt-get install gcc-avr avr-libc binutils-avr
#
# Usage:
#   tools/build_hex.sh            # builds SITE_ID 1..8, real time
#   SCALE=20 tools/build_hex.sh   # 5x faster timing for Proteus demos
#   SCALE=20 SITE=5 tools/build_hex.sh
#   FAULT_TEST=1 tools/build_hex.sh  # test build that forces a conflict at 8 s
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$ROOT/firmware/traffic_controller/traffic_controller.ino"
OUT="$ROOT/firmware/build"
WORK="${WORK:-$(mktemp -d)}"
CORE="${ARDUINO_CORE:-$WORK/ArduinoCore-avr}"
SCALE="${SCALE:-100}"
SIM_SITE="${SITE:-1}"
FAULT_TEST="${FAULT_TEST:-0}"

if [ ! -d "$CORE/cores/arduino" ]; then
  git clone -q --depth 1 https://github.com/arduino/ArduinoCore-avr.git "$CORE"
fi

MCU=atmega328p
CFLAGS_COMMON="-mmcu=$MCU -DF_CPU=16000000UL -DARDUINO=10819 -DARDUINO_AVR_UNO -DARDUINO_ARCH_AVR \
  -Os -g -ffunction-sections -fdata-sections -flto -fno-fat-lto-objects -Wall -Wextra \
  -I$CORE/cores/arduino -I$CORE/variants/standard"
CXXFLAGS="$CFLAGS_COMMON -std=gnu++11 -fpermissive -fno-exceptions -fno-threadsafe-statics -Wno-error=narrowing"
CFLAGS="$CFLAGS_COMMON -std=gnu11"

# Build the Arduino core once.
COREOBJ="$WORK/core"
mkdir -p "$COREOBJ"
if [ ! -f "$COREOBJ/core.a" ]; then
  for f in "$CORE"/cores/arduino/*.c; do
    avr-gcc $CFLAGS -c "$f" -o "$COREOBJ/$(basename "$f").o"
  done
  for f in "$CORE"/cores/arduino/*.cpp; do
    avr-g++ $CXXFLAGS -c "$f" -o "$COREOBJ/$(basename "$f").o" 2>/dev/null
  done
  avr-gcc -x assembler-with-cpp $CFLAGS_COMMON -c "$CORE/cores/arduino/wiring_pulse.S" \
    -o "$COREOBJ/wiring_pulse.S.o"
  avr-gcc-ar rcs "$COREOBJ/core.a" "$COREOBJ"/*.o
fi

mkdir -p "$OUT"
build_one() {
  local site="$1" name="$2" defs="${3:-}"
  local cpp="$WORK/sketch_$name.cpp"
  { echo '#include <Arduino.h>'; cat "$SKETCH"; } > "$cpp"
  sed -i "s/^#define SITE_ID .*/#define SITE_ID $site/; s/^#define TIME_SCALE_PERCENT .*/#define TIME_SCALE_PERCENT $SCALE/" "$cpp"
  avr-g++ $CXXFLAGS $defs -c "$cpp" -o "$WORK/$name.o"
  avr-gcc -mmcu=$MCU -Os -flto -fuse-linker-plugin -Wl,--gc-sections \
    -o "$WORK/$name.elf" "$WORK/$name.o" "$COREOBJ/core.a" -lm
  avr-objcopy -O ihex -R .eeprom "$WORK/$name.elf" "$OUT/$name.hex"
  cp "$WORK/$name.elf" "$OUT/$name.elf"
  echo "== $name"
  avr-size -C --mcu=$MCU "$WORK/$name.elf" | grep -E "Program|Data"
}

if [ "$FAULT_TEST" = "1" ]; then
  SCALE=20
  build_one 1 "traffic_controller_site01_faulttest" "-DTEST_INJECT_CONFLICT_SEC=8"
  rm -f "$OUT/traffic_controller_site01_faulttest.hex"   # never ship this one
elif [ "$SCALE" = "100" ]; then
  for s in 1 2 3 4 5 6 7 8; do build_one "$s" "traffic_controller_site0$s"; done
else
  build_one "$SIM_SITE" "traffic_controller_site0${SIM_SITE}_sim_x$((100 / SCALE))"
fi
