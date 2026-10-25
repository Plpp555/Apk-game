Minimal Android (bionic) sysroot headers for `tools/build_apk_offline.sh`.

They declare only what libtrisync3d.so uses, with bionic's ABI (type sizes and
constants from bionic's public headers). Builds with the real NDK
(Android Studio / Gradle / CI) never see these files.

The offline build script adds the Khronos EGL/GLES3/KHR headers and Android's
`jni.h` from the host (`libegl-dev`, `libgles-dev`, `android-libnativehelper-dev`).
