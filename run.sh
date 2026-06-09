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

exec ./Release/raspivoice -s2 -v "$@"
