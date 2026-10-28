#!/bin/bash
# Renders the README screenshots and animations into media/orbit/ with the desktop
# build of src/orbit, so they are the firmware's own drawing code and sample data.
# Needs g++ and python3 with Pillow.
set -e
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../../.." && pwd)
OUT=$R/media/orbit
TMP=$(mktemp -d)
trap 'rm -rf "${TMP:?}"' EXIT
mkdir -p "$OUT"
"$D/build.sh" "$TMP"
"$TMP/harness" --gallery "$TMP"
STILLS="home-app home-charge sd sd-actions download wifi-password online-hub catalog firmware webui usb
        partitions partition-resize settings themes charge error copy rename app"
python3 "$D/rgb2png.py" 2 "$OUT" $(for s in $STILLS; do echo "$TMP/$s.rgb565"; done)
for f in home launch install wifi; do
    mkdir -p "$TMP/film-$f"
    "$TMP/harness" --film "$TMP/film-$f" 3 "film-$f" > /dev/null
    python3 "$D/frames2gif.py" "$TMP/film-$f" "$OUT/$f.gif" 2 50 0 999 1200
done
mkdir -p "$TMP/themes"
for i in 0 1 2 3 4 5 6; do cp "$TMP/theme-$i.rgb565" "$TMP/themes/f000$i.rgb565"; done
python3 "$D/frames2gif.py" "$TMP/themes" "$OUT/theme-cycle.gif" 2 1000
