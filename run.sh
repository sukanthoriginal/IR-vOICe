#!/usr/bin/env bash
# Run raspivoice against the IR USB camera (= /dev/video0, image_source=2).
# Any extra flags you pass are forwarded to the binary (e.g. ./run.sh -p -o out.wav).
set -euo pipefail

cd "$(dirname "$0")/raspivoice"

if [ ! -x ./Release/raspivoice ]; then
    echo "raspivoice binary not found. Run ../setup.sh first." >&2
    exit 1
fi

if [ ! -e /dev/video0 ]; then
    echo "Warning: /dev/video0 does not exist. Is the IR camera plugged in?" >&2
fi

# Default audio card = 2 (USB / 3.5mm earphones on Sukanth's Pi 4 setup).
# Override by passing your own -a flag, e.g. `./run.sh -a1` for HDMI/monitor.
# getopt is last-wins, so a user-supplied -a overrides the default below.
exec ./Release/raspivoice -s2 -v -a2 "$@"
