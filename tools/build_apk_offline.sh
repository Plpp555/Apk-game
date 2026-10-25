#!/usr/bin/env bash
# Builds the TriSync 3D APK WITHOUT the Android SDK/NDK.
#
# Uses only Ubuntu/Debian packages:
#   clang lld cmake ninja-build python3 openjdk (javac)
#   aapt dalvik-exchange zipalign apksigner android-sdk-platform-23
#   libegl-dev libgles-dev android-libnativehelper-dev
# The native library is compiled with host clang against the minimal bionic
# sysroot in tools/android-sysroot and linked against stub libraries that list
# the Android system symbols it imports. The Java side is compiled against the
# API 23 android.jar (the app only uses APIs <= 23; newer ones via reflection).
#
# With Android Studio / the NDK, build android/ with Gradle instead; both builds
# compile the same sources with the same flags (CMakeLists.txt).
#
# Usage: tools/build_apk_offline.sh            -> build/offline/TriSync3D-<version>.apk
#   ABIS="arm64-v8a x86_64" OUT=... ANDROID_JAR=... may be overridden.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${OUT:-$ROOT/build/offline}"
ANDROID_JAR="${ANDROID_JAR:-/usr/lib/android-sdk/platforms/android-23/android.jar}"
ABIS="${ABIS:-arm64-v8a x86_64}"
APP="$ROOT/android/app"
PACKAGE="com.plpp555.trisync3d"
MIN_SDK="$(sed -n "s/.*minSdk \([0-9]*\).*/\1/p" "$APP/build.gradle")"
TARGET_SDK="$(sed -n "s/.*targetSdk \([0-9]*\).*/\1/p" "$APP/build.gradle")"
VERSION_CODE="$(sed -n "s/.*versionCode \([0-9]*\).*/\1/p" "$APP/build.gradle")"
VERSION_NAME="$(sed -n "s/.*versionName '\([^']*\)'.*/\1/p" "$APP/build.gradle")"
KEYSTORE="$ROOT/tools/keystore/trisync-debug.jks"

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "missing tool: $1 (install: $2)" >&2; exit 1; }
}
need clang "clang"
need ld.lld "lld"
need llvm-readelf "llvm"
need llvm-strip "llvm"
need cmake "cmake"
need ninja "ninja-build"
need python3 "python3"
need javac "openjdk-17-jdk-headless"
need aapt2 "aapt"
need dalvik-exchange "dalvik-exchange"
need zipalign "zipalign"
need apksigner "apksigner"
[ -f "$ANDROID_JAR" ] || { echo "android.jar not found: $ANDROID_JAR (install android-sdk-platform-23)" >&2; exit 1; }
for h in /usr/include/EGL/egl.h /usr/include/GLES3/gl3.h /usr/include/KHR/khrplatform.h /usr/include/android/jni.h; do
    [ -f "$h" ] || { echo "missing header $h (install libegl-dev libgles-dev android-libnativehelper-dev)" >&2; exit 1; }
done

echo "== TriSync 3D $VERSION_NAME (code $VERSION_CODE), minSdk $MIN_SDK, targetSdk $TARGET_SDK, ABIs: $ABIS"
rm -rf "$OUT"
mkdir -p "$OUT/apk/lib"

# ---------------------------------------------------------------- sysroot
SYSROOT="$OUT/sysroot"
mkdir -p "$SYSROOT/usr/include"
cp -r "$ROOT/tools/android-sysroot/usr/include/." "$SYSROOT/usr/include/"
for d in EGL GLES2 GLES3 KHR; do cp -r "/usr/include/$d" "$SYSROOT/usr/include/"; done
cp /usr/include/android/jni.h "$SYSROOT/usr/include/jni.h"

# ---------------------------------------------------------------- native
for abi in $ABIS; do
    case "$abi" in
        arm64-v8a) triple="aarch64-linux-android$MIN_SDK" ;;
        x86_64) triple="x86_64-linux-android$MIN_SDK" ;;
        *) echo "unsupported ABI for the offline build: $abi (use Gradle + NDK)" >&2; exit 1 ;;
    esac
    echo "== native $abi ($triple)"
    stubs="$OUT/stubs/$abi"
    mkdir -p "$stubs"
    for list in "$ROOT"/tools/android-sysroot/stubs/*.txt; do
        lib="$(basename "$list" .txt)"
        sed 's/.*/void &(void) {}/' "$list" > "$stubs/$lib.c"
        clang --target="$triple" -shared -nostdlib -fPIC -fno-builtin -w \
            -Wl,-soname,"$lib.so" -o "$stubs/$lib.so" "$stubs/$lib.c"
    done
    cmake -S "$ROOT" -B "$OUT/native-$abi" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/android-offline.toolchain.cmake" \
        -DTSB_ANDROID_TRIPLE="$triple" -DTSB_SYSROOT="$SYSROOT" -DTSB_STUB_DIR="$stubs" >/dev/null
    ninja -C "$OUT/native-$abi" trisync3d
    mkdir -p "$OUT/apk/lib/$abi"
    llvm-strip --strip-unneeded -o "$OUT/apk/lib/$abi/libtrisync3d.so" "$OUT/native-$abi/libtrisync3d.so"
    python3 "$ROOT/tools/check_android_elf.py" "$OUT/apk/lib/$abi/libtrisync3d.so" "$ROOT/tools/android-sysroot/stubs"
done

# ---------------------------------------------------------------- resources
echo "== resources"
# Gradle injects the package name from build.gradle (namespace); do the same here.
sed "s|<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">|<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\" package=\"$PACKAGE\">|" \
    "$APP/src/main/AndroidManifest.xml" > "$OUT/AndroidManifest.xml"
grep -q "package=\"$PACKAGE\"" "$OUT/AndroidManifest.xml" || { echo "failed to set manifest package" >&2; exit 1; }
aapt2 compile --dir "$APP/src/main/res" -o "$OUT/res.zip"
mkdir -p "$OUT/gen"
aapt2 link -o "$OUT/base.apk" -I "$ANDROID_JAR" --manifest "$OUT/AndroidManifest.xml" \
    --java "$OUT/gen" --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" \
    --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" "$OUT/res.zip"

# ---------------------------------------------------------------- java
echo "== java"
mkdir -p "$OUT/classes"
find "$APP/src/main/java" "$OUT/gen" -name '*.java' > "$OUT/sources.txt"
javac -source 8 -target 8 -Xlint:-options -encoding UTF-8 -bootclasspath "$ANDROID_JAR" \
    -d "$OUT/classes" @"$OUT/sources.txt"
dalvik-exchange --dex --min-sdk-version="$MIN_SDK" --output="$OUT/apk/classes.dex" "$OUT/classes"

# ---------------------------------------------------------------- package
echo "== package"
python3 - "$OUT/base.apk" "$OUT/apk" "$OUT/unaligned.apk" <<'PY'
import os, sys, zipfile
base, extra, out = sys.argv[1:4]
with zipfile.ZipFile(base) as src, zipfile.ZipFile(out, "w") as dst:
    for info in src.infolist():          # keeps resources.arsc STORED
        dst.writestr(info, src.read(info.filename))
    for dirpath, _, files in os.walk(extra):
        for name in sorted(files):
            path = os.path.join(dirpath, name)
            arc = os.path.relpath(path, extra).replace(os.sep, "/")
            dst.write(path, arc, compress_type=zipfile.ZIP_DEFLATED)
PY
zipalign -p -f 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"
APK="$OUT/TriSync3D-$VERSION_NAME.apk"
apksigner sign --ks "$KEYSTORE" --ks-pass pass:trisync-debug --ks-key-alias trisync \
    --key-pass pass:trisync-debug --min-sdk-version "$MIN_SDK" --out "$APK" "$OUT/aligned.apk"
apksigner verify --min-sdk-version "$MIN_SDK" "$APK"
echo "== done: $APK ($(du -h "$APK" | cut -f1))"
