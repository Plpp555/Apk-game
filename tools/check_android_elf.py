#!/usr/bin/env python3
"""Sanity checks for libtrisync3d.so built by tools/build_apk_offline.sh.

Verifies what the Android dynamic linker and Play policies require:
  * DT_NEEDED only lists Android system libraries
  * every undefined symbol is exported by those libraries (stub lists)
  * no text relocations, non-executable stack, 16 KB page-aligned LOAD segments
  * the JNI entry points are exported
Usage: check_android_elf.py LIB.so STUB_LIST_DIR
"""
import os
import re
import subprocess
import sys

ALLOWED_NEEDED = {"libc.so", "libGLESv3.so", "libEGL.so", "libandroid.so", "liblog.so", "libm.so", "libdl.so"}
REQUIRED_EXPORTS = [
    "Java_com_plpp555_trisync3d_NativeBridge_nativeInit",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeSetSurface",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeStart",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeAbort",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeIsRunning",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeStatus",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeTakeResult",
    "Java_com_plpp555_trisync3d_NativeBridge_nativeGpuInfo",
]


def readelf(*args):
    return subprocess.run(["llvm-readelf", *args], check=True, capture_output=True, text=True).stdout


def main():
    lib, stub_dir = sys.argv[1], sys.argv[2]
    errors = []

    dyn = readelf("--dynamic", lib)
    needed = set(re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dyn))
    bad = needed - ALLOWED_NEEDED
    if bad:
        errors.append(f"unexpected DT_NEEDED: {sorted(bad)}")
    if "TEXTREL" in dyn:
        errors.append("text relocations present")
    if "(SONAME)" not in dyn:
        errors.append("missing DT_SONAME")

    ph = readelf("--program-headers", lib)
    for line in ph.splitlines():
        cols = line.split()
        if cols and cols[0] == "LOAD":
            align = int(cols[-1], 16)
            if align < 16384:
                errors.append(f"LOAD segment alignment {align:#x} < 16 KB")
        if cols and cols[0] == "GNU_STACK" and "E" in cols[-2]:
            errors.append("executable stack")

    known = set()
    for name in os.listdir(stub_dir):
        if name.endswith(".txt"):
            with open(os.path.join(stub_dir, name)) as f:
                known.update(s.strip() for s in f if s.strip())

    syms = readelf("--dyn-syms", "--wide", lib)
    undefined, exported = set(), set()
    for line in syms.splitlines():
        cols = line.split()
        if len(cols) < 8 or not cols[0].rstrip(":").isdigit():
            continue
        name = cols[7].split("@")[0]
        if cols[6] == "UND":
            if name:
                undefined.add(name)
        elif cols[4] in ("GLOBAL", "WEAK") and cols[5] == "DEFAULT":
            exported.add(name)
    unknown = sorted(undefined - known)
    if unknown:
        errors.append(f"undefined symbols not provided by Android system libraries: {unknown}")
    missing = [s for s in REQUIRED_EXPORTS if s not in exported]
    if missing:
        errors.append(f"missing JNI exports: {missing}")

    if errors:
        for e in errors:
            print(f"ERROR {lib}: {e}")
        sys.exit(1)
    print(f"OK {os.path.basename(lib)}: NEEDED={sorted(needed)}, {len(undefined)} imports, "
          f"{len(exported)} exports, 16KB-aligned")


if __name__ == "__main__":
    main()
