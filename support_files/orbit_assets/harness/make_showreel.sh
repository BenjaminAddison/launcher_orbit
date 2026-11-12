#!/bin/bash
# Renders the showreel (title, launch, home, themes, Wi-Fi, install) into media/orbit/ with
# the desktop build of src/orbit: a 2x GIF, a 4x MP4, a looping title card GIF and a still
# cover, e.g. for M5Burner.
# Needs g++, python3 with Pillow and numpy, and ffmpeg for the MP4.
set -e
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../../.." && pwd)
OUT=$R/media/orbit
TMP=$(mktemp -d)
trap 'rm -rf "${TMP:?}"' EXIT
mkdir -p "$OUT"
"$D/build.sh" "$TMP"
for f in reel-title reel-launch film-home reel-themes film-wifi film-install reel-outro; do
    mkdir -p "$TMP/$f"
    "$TMP/harness" --film "$TMP/$f" 2 "$f" > /dev/null
done
MP4=-
command -v ffmpeg > /dev/null && MP4=$OUT/showreel.mp4
# 30 fps clips: [first, last) frame of each
python3 "$D/reel.py" "$OUT/showreel.gif" "$MP4" 2 \
    "$TMP/reel-title:0:84" "$TMP/reel-launch:92:216" "$TMP/film-home:0:128" "$TMP/reel-themes:0:160" \
    "$TMP/film-wifi:0:133" "$TMP/film-install:0:223" "$TMP/reel-outro:0:90"
# title card on its own: one rocket pass (6 s), dissolving back to its opening frame to loop
python3 "$D/reel.py" "$OUT/title.gif" - 2 "$TMP/reel-title:0:180" "$TMP/reel-title:0:8"
python3 "$D/rgb2png.py" 2 "$TMP" "$TMP/reel-title/f0052.rgb565"
mv "$TMP/f0052.png" "$OUT/cover.png"
