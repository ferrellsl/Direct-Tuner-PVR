#!/bin/bash
# Copies the built APKs and AppImage to a release folder and prints each one's add-on version.
OUT=${1:?release folder}
W=$HOME/android-tools; BT=$(ls -d $W/android-sdk-linux/build-tools/* | tail -1)
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}
for A in arm64-v8a:arm64 armeabi-v7a:armv7; do
  APK=$W/kodi/kodiapp-${A%%:*}-release.apk
  echo "${A#*:}: $(unzip -p $APK assets/addons/pvr.directtuner/addon.xml | grep -o 'version="21[^"]*"' | head -1) $(unzip -l $APK | grep -c libpvr.directtuner.so) lib, $($BT/apksigner verify --print-certs $APK 2>/dev/null | grep -o 'SHA-256 digest: .\{8\}')"
  cp $APK "$OUT/Kodi-21.3-AC4-${A#*:}.apk"
done
cp $HOME/linux-kodi/Kodi-21.3-AC4-x86_64.AppImage "$OUT/"
echo "AppImage: $(grep -o 'version="21[^"]*"' $HOME/linux-kodi/AppDir/usr/share/kodi/addons/pvr.directtuner/addon.xml | head -1)"
