#!/usr/bin/env bash
# tools/screendump.sh — capture the NT4 guest framebuffer to PNG via the QEMU
# monitor (setup.md §3). This is how Claude "sees" the real render.
#   ./tools/screendump.sh [out.png]
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-vm/nt4.png}"
MON_HOST=127.0.0.1
MON_PORT=5555
mkdir -p "$(dirname "$OUT")"

# QEMU 7.1+ writes PNG directly. Fall back to PPM + magick if needed.
if printf 'screendump %s -f png\n' "$OUT" | nc -w1 "$MON_HOST" "$MON_PORT" >/dev/null 2>&1 \
   && [[ -s "$OUT" ]]; then
  echo "wrote $OUT"
else
  PPM="${OUT%.png}.ppm"
  printf 'screendump %s\n' "$PPM" | nc -w1 "$MON_HOST" "$MON_PORT" >/dev/null
  magick "$PPM" "$OUT"
  echo "wrote $OUT (via ppm)"
fi
