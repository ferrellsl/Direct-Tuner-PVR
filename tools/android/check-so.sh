#!/bin/bash
NDK=~/android-tools/android-sdk-linux/ndk/21.4.7075529/toolchains/llvm/prebuilt/linux-x86_64
for H in aarch64-linux-android arm-linux-androideabi; do
  SO=~/android-tools/xbmc-depends/$H-21-release/lib/kodi/addons/pvr.directtuner/libpvr.directtuner.so
  [ -f $SO ] || continue
  echo "== $H"; $NDK/bin/llvm-readelf -d $SO | grep NEEDED
  U=$($NDK/bin/llvm-nm -D --undefined-only $SO | grep -c filesystem); echo "undefined filesystem symbols: $U"
  LIBCXX=$NDK/sysroot/usr/lib/$H/libc++_shared.so
  for s in $($NDK/bin/llvm-nm -D --undefined-only $SO | awk '{print $2}' | grep filesystem | head -50); do $NDK/bin/llvm-nm -D --defined-only $LIBCXX | grep -q " $s$" || echo "MISSING $s"; done | head -5
  $NDK/bin/llvm-nm -D --defined-only $SO | grep -c " ADDON_"
done
