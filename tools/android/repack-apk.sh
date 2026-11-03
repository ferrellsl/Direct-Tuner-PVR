#!/bin/bash
# Rebuilds only the APK from cached Kodi builds, after build-addon.sh installed the add-on.
# Usage: repack-apk.sh arm64|arm
set -e
WORK=${WORK:-$HOME/android-tools}
case "${1:-arm64}" in
  arm64) ABI=arm64-v8a; BUILD=$WORK/kodi-build ;;
  arm) ABI=armeabi-v7a; BUILD=$WORK/kodi-build-arm ;;
esac
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}
. "$WORK/signing/signing.env"
cd "$BUILD"
cp -p "$WORK/kodi/kodiapp-$ABI-release.apk" "$WORK/kodiapp-$ABI-release.apk.ac45" 2>/dev/null || true
make apk > "$WORK/repack-$ABI.log" 2>&1
ls -la "$WORK/kodi/kodiapp-$ABI-release.apk"
unzip -l "$WORK/kodi/kodiapp-$ABI-release.apk" | grep -E "directtuner" | head -4
