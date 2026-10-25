cmake_minimum_required(VERSION 3.18)
project(TriSync3D CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Determinism: these flags are part of the benchmark specification
# (docs/SPEC.md). Without -ffp-contract=off, clang fuses a*b+c into FMA on
# ARM64 and results differ from x86 / MSVC builds.
# ---------------------------------------------------------------------------
if(MSVC)
  add_compile_options(/fp:precise /GR- /W3)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math -fno-math-errno
                      -fno-exceptions -fno-rtti -fno-threadsafe-statics
                      -Wall -Wextra -Wno-unused-parameter)
endif()

set(TSB_SIM_SOURCES
  core/strbuf.cpp
  core/detmath.cpp
  core/jobs.cpp
  core/mesh_gen.cpp
  core/sim_common.cpp
  core/sim_asteroids.cpp
  core/sim_particles.cpp
  core/sim_city.cpp
  core/sim_physics.cpp)

set(TSB_GL_SOURCES
  core/gl_util.cpp
  core/shaders.cpp
  core/render_common.cpp
  core/test_gt1.cpp
  core/test_gt2.cpp
  core/test_gt3.cpp
  core/test_cpu.cpp
  core/benchmark.cpp)

if(ANDROID OR TSB_ANDROID_OFFLINE)
  # libtrisync3d.so for the Android app. No C++ standard library is used
  # (ANDROID_STL=none); cxx_runtime.cpp provides operator new/delete.
  add_library(trisync3d SHARED
    ${TSB_SIM_SOURCES} ${TSB_GL_SOURCES}
    platform/posix/platform_posix.cpp
    platform/android/cxx_runtime.cpp
    platform/android/jni_main.cpp)
  target_compile_options(trisync3d PRIVATE -fvisibility=hidden -fPIC)
  target_link_options(trisync3d PRIVATE
    -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now
    -Wl,--build-id=sha1 -Wl,--hash-style=both -Wl,--gc-sections)
  if(TSB_ANDROID_OFFLINE)
    # Offline build (tools/build_apk_offline.sh): host clang + minimal sysroot.
    target_compile_options(trisync3d PRIVATE -fno-stack-protector)
    if(TSB_ANDROID_TRIPLE MATCHES "^aarch64")
      # No compiler-rt in the offline build: keep atomics inline.
      target_compile_options(trisync3d PRIVATE -mno-outline-atomics)
    endif()
    target_link_options(trisync3d PRIVATE -nostdlib -fuse-ld=lld -L${TSB_STUB_DIR})
    target_link_libraries(trisync3d PRIVATE GLESv3 EGL android log c)
  else()
    target_link_libraries(trisync3d PRIVATE GLESv3 EGL android log)
  endif()
else()
  # Desktop tools: determinism checker and headless EGL host.
  find_package(Threads REQUIRED)
  add_executable(simcheck ${TSB_SIM_SOURCES} platform/posix/platform_posix.cpp hosts/simcheck/simcheck_main.cpp)
  target_link_libraries(simcheck PRIVATE Threads::Threads)

  find_library(TSB_EGL_LIB EGL)
  find_library(TSB_GLES_LIB GLESv2)
  if(TSB_EGL_LIB AND TSB_GLES_LIB)
    add_executable(bench_host ${TSB_SIM_SOURCES} ${TSB_GL_SOURCES}
      platform/posix/platform_posix.cpp hosts/linux/host_main.cpp)
    target_link_libraries(bench_host PRIVATE ${TSB_EGL_LIB} ${TSB_GLES_LIB} Threads::Threads)
  endif()
endif()
