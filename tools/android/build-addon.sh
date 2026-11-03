#!/bin/bash
# Builds the add-on with Kodi's Android depends and installs it where Kodi's "make apk" picks it up.
# Usage: build-addon.sh arm64|arm <source folder>
set -e
case "${1:-arm64}" in
  arm64) HOST=aarch64-linux-android ;;
  arm) HOST=arm-linux-androideabi ;;
  *) echo "arm64 or arm"; exit 1 ;;
esac
SRC=${2:?add-on source folder}
WORK=${WORK:-$HOME/android-tools}
DEPENDS=$WORK/xbmc-depends/$HOST-21-release
B=$WORK/directtuner-build-$HOST
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$DEPENDS/share/Toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX="$DEPENDS" -DKODI_INCLUDE_DIR="$WORK/kodi/xbmc/addons/kodi-dev-kit/include" >/dev/null
cmake --build "$B" --target pvr.directtuner
cmake --install "$B"
ls -la "$DEPENDS/lib/kodi/addons/pvr.directtuner"
grep -oE 'library_[a-z]+="[^"]*"|<platform>[^<]*' "$DEPENDS/share/kodi/addons/pvr.directtuner/addon.xml"
