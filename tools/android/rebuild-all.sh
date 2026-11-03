#!/bin/bash
# Rebuilds the add-on into both APKs and the AppImage from one source folder.
set -e
SRC=${1:?add-on source folder}
H=$(cd "$(dirname "$0")/.." && pwd)
for a in arm64 arm; do bash "$H/android/build-addon.sh" $a "$SRC" | tail -2; done
bash "$H/android/repack-apk.sh" arm64 | head -1
bash "$H/android/repack-apk.sh" arm | head -1
DIRECTTUNER="$SRC" bash "$H/linux/repack-appimage.sh" | tail -2
