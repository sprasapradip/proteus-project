#!/usr/bin/env bash
# Builds an Arduino Uno sketch into an Intel HEX file without the Arduino IDE.
# The same script runs on my PC, in WSL and in GitHub Actions, so the HEX
# files in this repo can always be rebuilt the same way.
#
# Needs: avr-gcc, avr-libc, binutils-avr, git
#   sudo apt-get install gcc-avr avr-libc binutils-avr
#
# Usage:
#   tools/build_firmware.sh <sketch.ino> <out.hex> [extra compiler flags...]
#
# Libraries: list them, one per line, in a file called libs.txt next to the
# sketch. Names from ArduinoCore-avr (SPI, SoftwareSerial, EEPROM, Wire) work
# directly. Anything else must be pinned in the LIBRARIES table below.
set -euo pipefail

if [ $# -lt 2 ]; then
  echo "usage: $0 <sketch.ino> <out.hex> [extra flags]" >&2
  exit 2
fi

SKETCH="$(realpath "$1")"
OUT_HEX="$2"
shift 2
EXTRA_FLAGS=("$@")

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="${FIRMWARE_CACHE:-$REPO/.cache}"
CORE_TAG="1.8.6"
CORE="$CACHE/ArduinoCore-avr-$CORE_TAG"

# name -> "git url|tag". Pinned so a rebuild next year gives the same HEX.
declare -A LIBRARIES=(
  [MD_Parola]="https://github.com/MajicDesigns/MD_Parola.git|v3.7.7"
  [MD_MAX72XX]="https://github.com/MajicDesigns/MD_MAX72XX.git|v3.5.1"
  [LiquidCrystal]="https://github.com/arduino-libraries/LiquidCrystal.git|1.0.7"
  [Servo]="https://github.com/arduino-libraries/Servo.git|1.2.2"
)

MCU=atmega328p
COMMON="-mmcu=$MCU -DF_CPU=16000000UL -DARDUINO=10819 -DARDUINO_AVR_UNO -DARDUINO_ARCH_AVR -Os -g \
  -ffunction-sections -fdata-sections -flto -fno-fat-lto-objects"
CFLAGS="$COMMON -std=gnu11"
CXXFLAGS="$COMMON -std=gnu++11 -fpermissive -fno-exceptions -fno-threadsafe-statics -Wno-error=narrowing"

mkdir -p "$CACHE"
if [ ! -d "$CORE/cores/arduino" ]; then
  git -c advice.detachedHead=false clone -q --depth 1 --branch "$CORE_TAG" https://github.com/arduino/ArduinoCore-avr.git "$CORE"
fi

INCLUDES=(-I"$CORE/cores/arduino" -I"$CORE/variants/standard")
SOURCES=()

# Core archive, built once and reused.
CORE_OBJ="$CACHE/core-$CORE_TAG"
if [ ! -f "$CORE_OBJ/core.a" ]; then
  mkdir -p "$CORE_OBJ"
  for f in "$CORE"/cores/arduino/*.c; do
    avr-gcc $CFLAGS "${INCLUDES[@]}" -c "$f" -o "$CORE_OBJ/$(basename "$f").o"
  done
  for f in "$CORE"/cores/arduino/*.cpp; do
    avr-g++ $CXXFLAGS "${INCLUDES[@]}" -c "$f" -o "$CORE_OBJ/$(basename "$f").o" 2>/dev/null
  done
  avr-gcc -x assembler-with-cpp $COMMON "${INCLUDES[@]}" \
    -c "$CORE/cores/arduino/wiring_pulse.S" -o "$CORE_OBJ/wiring_pulse.S.o"
  avr-gcc-ar rcs "$CORE_OBJ/core.a" "$CORE_OBJ"/*.o
fi

add_library_dir() {
  local dir="$1"
  local src="$dir"
  [ -d "$dir/src" ] && src="$dir/src"
  INCLUDES+=(-I"$src")
  # Top-level sources plus avr/ and utility/ subfolders. Servo also ships
  # sam/, samd/, ... which must not be compiled for the Uno.
  local d f
  for d in "$src" "$src/avr" "$src/utility"; do
    [ -d "$d" ] || continue
    for f in "$d"/*.c "$d"/*.cpp; do
      [ -f "$f" ] && SOURCES+=("$f")
    done
  done
}

LIBS_FILE="$(dirname "$SKETCH")/libs.txt"
if [ -f "$LIBS_FILE" ]; then
  while read -r lib; do
    lib="${lib%%#*}"; lib="$(echo "$lib" | xargs)"
    [ -z "$lib" ] && continue
    if [ -d "$CORE/libraries/$lib" ]; then
      add_library_dir "$CORE/libraries/$lib"
    elif [ -n "${LIBRARIES[$lib]:-}" ]; then
      url="${LIBRARIES[$lib]%%|*}"; tag="${LIBRARIES[$lib]##*|}"
      dest="$CACHE/libs/$lib-$tag"
      [ -d "$dest" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "$url" "$dest"
      add_library_dir "$dest"
    else
      echo "unknown library '$lib' (add it to LIBRARIES in $0)" >&2
      exit 1
    fi
  done < "$LIBS_FILE"
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# An .ino is C++ with an implicit Arduino.h include.
{ echo '#include <Arduino.h>'; echo "#line 1 \"$(basename "$SKETCH")\""; cat "$SKETCH"; } > "$WORK/sketch.cpp"

OBJS=()
avr-g++ $CXXFLAGS -Wall -Wextra "${INCLUDES[@]}" -I"$(dirname "$SKETCH")" "${EXTRA_FLAGS[@]}" \
  -c "$WORK/sketch.cpp" -o "$WORK/sketch.o"
OBJS+=("$WORK/sketch.o")
i=0
for f in "${SOURCES[@]}"; do
  i=$((i + 1))
  case "$f" in
    *.c)   avr-gcc $CFLAGS "${INCLUDES[@]}" -c "$f" -o "$WORK/lib$i.o" 2>/dev/null ;;
    *.cpp) avr-g++ $CXXFLAGS "${INCLUDES[@]}" -c "$f" -o "$WORK/lib$i.o" 2>/dev/null ;;
  esac
  OBJS+=("$WORK/lib$i.o")
done

ELF="${OUT_HEX%.hex}.elf"
mkdir -p "$(dirname "$OUT_HEX")"
avr-gcc -mmcu=$MCU -Os -flto -fuse-linker-plugin -Wl,--gc-sections \
  -o "$ELF" "${OBJS[@]}" "$CORE_OBJ/core.a" -lm
avr-objcopy -O ihex -R .eeprom "$ELF" "$OUT_HEX"

SIZE="$(avr-size -C --mcu=$MCU "$ELF" | grep -E 'Program|Data' | awk '{print $1, $2, $3, $4, $5}' | tr '\n' ' ')"
echo "built $(realpath --relative-to="$REPO" "$OUT_HEX"): $SIZE"
