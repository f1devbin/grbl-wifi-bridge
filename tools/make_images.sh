#!/usr/bin/env bash
# Renders docs/og.png (1200x630) and docs/icon.png (512x512) from tools/og.html and tools/icon.html
# with headless Chromium. Set CHROMIUM if the binary has another name.
set -euo pipefail
cd "$(dirname "$0")/.."
CHROMIUM=${CHROMIUM:-$(command -v chromium || command -v chromium-browser || command -v google-chrome)}
# A snap-packaged Chromium can only use non-hidden folders in the home directory.
WORK=$(mktemp -d "$HOME/grbl-wifi-bridge-img.XXXX")
trap 'rm -rf "$WORK"' EXIT
cp tools/og.html tools/icon.html "$WORK/"
"$CHROMIUM" --headless=new --no-sandbox --disable-gpu --hide-scrollbars --window-size=1200,630 \
  --screenshot="$WORK/og.png" "file://$WORK/og.html" >/dev/null 2>&1
"$CHROMIUM" --headless=new --no-sandbox --disable-gpu --hide-scrollbars --default-background-color=00000000 \
  --window-size=512,512 --screenshot="$WORK/icon.png" "file://$WORK/icon.html" >/dev/null 2>&1
cp "$WORK/og.png" "$WORK/icon.png" docs/
ls -l docs/og.png docs/icon.png
