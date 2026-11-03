#!/bin/bash
# Builds the Linux add-on. Usage: build.sh <kodi-dev-kit include folder> [build folder]
set -e
HERE=$(cd "$(dirname "$0")/../.." && pwd)
INC=${1:?kodi-dev-kit include folder}
B=${2:-$HOME/dtpvr-build}
cmake -S "$HERE" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKODI_INCLUDE_DIR="$INC" >/dev/null
cmake --build "$B"
"$B/directtuner_tests"
