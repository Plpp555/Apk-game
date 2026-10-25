# CMake toolchain for tools/build_apk_offline.sh: host clang targeting Android
# with the minimal sysroot (no NDK). Required variables:
#   TSB_ANDROID_TRIPLE  e.g. aarch64-linux-android24
#   TSB_SYSROOT         assembled sysroot directory
#   TSB_STUB_DIR        directory with stub libc.so / libGLESv3.so / ...
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_COMPILER_TARGET ${TSB_ANDROID_TRIPLE})
set(CMAKE_SYSROOT ${TSB_SYSROOT})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TSB_ANDROID_TRIPLE TSB_SYSROOT TSB_STUB_DIR)
set(TSB_ANDROID_OFFLINE ON CACHE BOOL "Offline Android build" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
