#!/bin/bash
# Checks each APK carries the add-on and is signed with the existing key.
W=$HOME/android-tools; BT=$(ls -d $W/android-sdk-linux/build-tools/* | tail -1)
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}
for A in arm64-v8a armeabi-v7a; do
  APK=$W/kodi/kodiapp-$A-release.apk; echo "== $A $(stat -c %s $APK) bytes"
  unzip -l $APK | grep -E "lib/.*/libpvr.directtuner.so|assets/addons/pvr.directtuner/addon.xml|lib/.*/libperipheral" | awk '{print $1, $4}'
  unzip -p $APK assets/addons/pvr.directtuner/addon.xml | grep -oE 'library_android="[^"]*"|<platform>[^<]*'
  $BT/apksigner verify --print-certs $APK 2>/dev/null | grep -E "SHA-256 digest" | head -1
  [ -f $W/kodiapp-$A-release.apk.ac45 ] && $BT/apksigner verify --print-certs $W/kodiapp-$A-release.apk.ac45 2>/dev/null | grep -E "SHA-256 digest" | head -1
  $BT/aapt dump badging $APK 2>/dev/null | grep -E "^package|sdkVersion|native-code" | cut -c1-140
done
