#!/bin/bash
# Installs the add-on into the cached AppDir and rebuilds only the AppImage (no Kodi rebuild).
set -e
WORK=${WORK:-$HOME/linux-kodi}; APPDIR=$WORK/AppDir
DIRECTTUNER=${DIRECTTUNER:?set DIRECTTUNER to the add-on source folder}
cmake -S "$DIRECTTUNER" -B "$WORK/directtuner-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DKODI_INCLUDE_DIR="$WORK/kodi/xbmc/addons/kodi-dev-kit/include" >/dev/null
cmake --build "$WORK/directtuner-build" --target pvr.directtuner | tail -1
DESTDIR="$APPDIR" cmake --install "$WORK/directtuner-build" | tail -3
PY=$(python3 -c 'import sys; print("python%d.%d" % sys.version_info[:2])')
BIN=$(cd "$APPDIR/usr/lib/kodi" && ls kodi-x11 kodi.bin 2>/dev/null | head -1)
cd "$WORK"
EXTRA=(); for f in "$APPDIR/usr/lib/kodi/kodi-xrandr"; do [ -f "$f" ] && EXTRA+=(--executable "$f"); done
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 LDAI_OUTPUT=Kodi-21.3-AC4-x86_64.AppImage
./linuxdeploy-x86_64.AppImage --appdir "$APPDIR" --executable "$APPDIR/usr/lib/kodi/$BIN" "${EXTRA[@]}" \
  --deploy-deps-only "$APPDIR/usr/lib/kodi/addons" --deploy-deps-only "$APPDIR/usr/lib/$PY/lib-dynload" \
  --desktop-file "$APPDIR/usr/share/applications/kodi.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/kodi.png" \
  --custom-apprun "$WORK/AppRun" --output appimage > "$WORK/linuxdeploy.log" 2>&1
ls -la "$WORK/Kodi-21.3-AC4-x86_64.AppImage"
ls "$APPDIR/usr/lib/kodi/addons/pvr.directtuner" "$APPDIR/usr/share/kodi/addons/pvr.directtuner"
grep -o 'library_linux="[^"]*"' "$APPDIR/usr/share/kodi/addons/pvr.directtuner/addon.xml"
