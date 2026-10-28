#!/bin/bash
# Desktop build of the firmware's Orbit UI (src/orbit, bridge excluded) with the
# scripted harness: scenarios on a fixed 60 fps clock, frames written as RGB565.
D=$(cd "$(dirname "$0")" && pwd)
O=$(cd "$D/../../../src/orbit" && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
SRCS=$(ls "$O"/orbit_*.cpp | grep -v bridge)
g++ -std=gnu++17 -O2 -DORBIT_NATIVE -Wall -Wno-unused-function -Wno-format-truncation -I"$O" -I"$D" \
  $SRCS "$D/plat.cpp" "$D/shots.cpp" -o "$OUT/harness"
