#!/usr/bin/env bash
# Build the OOMWOO Arduino sketches with arduino-cli (the same thing the IDE
# does), using this folder as the sketchbook: hardware/ adds the OOMWOO board
# variants on top of WCH's core, libraries/ holds the OOMWOO library.
#
#   arduino/build.sh                 # U28 for the robot board
#   arduino/build.sh evt             # U28 for the CH32L103C8T6 evaluation board
#   arduino/build.sh robot --upload  # and flash it with the WCH-LinkE
#
# Needs arduino-cli with WCH's core ("WCH:ch32v"): mcu/scripts/setup.sh --arduino
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
target="${1:-robot}"
shift || true
export ARDUINO_DIRECTORIES_USER="$here"

python3 "$here/../tools/sync_arduino_lib.py" --check
arduino-cli compile \
  --fqbn "oomwoo:ch32v:u28:target=$target" \
  --output-dir "$here/build/u28-$target" \
  --warnings default \
  "$@" \
  "$here/libraries/OOMWOO/examples/U28"
