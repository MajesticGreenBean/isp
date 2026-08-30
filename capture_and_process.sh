#!/usr/bin/env bash
#
# Capture a frame on the Pi and run it through the ISP.
#
#   ./capture_and_process.sh                       defaults
#   ./capture_and_process.sh -n 5 -o scene.raw     5 frames, keep the first as scene.raw
#   DARK=my_dark.raw ./capture_and_process.sh      different dark frame
#   Both halves run on the Pi. Build ../pi_capture first.
#
# SIMPLEST SETUP:
# Have a ../pi_capture dir: https://github.com/MajesticGreenBean/pi_capture

set -euo pipefail

CAPTURE=${CAPTURE:-../pi_capture/capture}
ISP=${ISP:-./build/isp}
DARK=${DARK:-black_packed000.raw}
RAW=${RAW:-frame.raw}
OUT=${OUT:-out.pgm}
FRAMES=${FRAMES:-1}
ILLUMINANT=${ILLUMINANT:-}

usage() {
    cat <<EOF
Usage: $(basename "$0") [-n frames] [-r raw] [-o out] [-d dark] [-k kelvin]

  -n  frames to capture, first one is kept   (default ${FRAMES})
  -r  raw file the capture writes            (default ${RAW})
  -o  output base name for the ISP           (default ${OUT})
  -d  dark frame for black level             (default ${DARK})
  -k  illuminant colour temperature          (default: ISP's own)

Overridable by environment: CAPTURE, ISP, DARK, RAW, OUT, FRAMES, ILLUMINANT
EOF
}

while getopts "n:r:o:d:k:h" opt; do
    case "$opt" in
        n) FRAMES=$OPTARG ;;
        r) RAW=$OPTARG ;;
        o) OUT=$OPTARG ;;
        d) DARK=$OPTARG ;;
        k) ILLUMINANT=$OPTARG ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

for f in "$CAPTURE" "$ISP"; do
    if [[ ! -x "$f" ]]; then
        echo "error: '$f' is missing or not executable. Build it first" >&2
        exit 1
    fi
done

if [[ ! -f "$DARK" ]]; then
    echo "error: dark frame '$DARK' not found." >&2
    echo "       cap the lens and run: $CAPTURE /dev/video0 1 $DARK" >&2
    exit 1
fi

echo "=== capture ==="
"$CAPTURE" /dev/video0 "$FRAMES" "$RAW"

echo
echo "=== isp ==="
isp_args=(--packed "$RAW" "$OUT" --dark "$DARK")
if [[ -n "$ILLUMINANT" ]]; then
    isp_args+=(--illuminant "$ILLUMINANT")
fi
"$ISP" "${isp_args[@]}"
