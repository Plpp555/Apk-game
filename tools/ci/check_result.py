#!/usr/bin/env python3
"""Validates a TriSync 3D result JSON (from the app or hosts/linux).

Checks that the run completed, every test produced a frame rate, the CPU
single/multi-thread results are bit-identical, and (with --require-match)
that every simulation hash matches core/reference_hashes.h.
"""
import json
import sys


def main():
    path = sys.argv[1]
    require_match = "--require-match" in sys.argv[2:]
    with open(path, encoding="utf-8") as f:
        r = json.load(f)
    errors = []
    gpu = r.get("gpu", {})
    print(f"TriSync 3D {r.get('version')} [{r.get('mode')}] platform={r.get('platform')} "
          f"official={r.get('official')} threads={r.get('threads')}")
    print(f"GPU: {gpu.get('renderer')} | {gpu.get('version')} | hdr={gpu.get('hdr')}")
    if r.get("stage") != "done":
        errors.append(f"stage is {r.get('stage')!r} (error: {r.get('error')!r})")
    s = r.get("scores", {})
    print(f"Scores: overall={s.get('overall')} graphics={s.get('graphics')} cpu={s.get('cpu')} "
          f"cpuSingle={s.get('cpuSingle')}")
    for t in r.get("tests", []):
        print(f"  {t['id']}: {t['fps']:.2f} fps (1% low {t['lowFps']:.2f}), frame {t['frameMs']:.1f} ms, "
              f"cpu jobs {t['cpuMs']:.2f} ms, gpu {t['gpuMs']:.2f} ms, bound={t['bound']}, "
              f"hash={t['hash']} verify={t['verify']}")
        if not t.get("ran") or t.get("fps", 0) <= 0:
            errors.append(f"{t['id']} did not run")
        if require_match and t.get("verify") != "match":
            errors.append(f"{t['id']} hash verify={t.get('verify')}")
    c = r.get("cpu", {})
    print(f"  cpu: ST {c.get('stRate', 0) / 1e6:.2f} M/s, MT {c.get('mtRate', 0) / 1e6:.2f} M/s, "
          f"scaling x{c.get('scaling')}, threadsConsistent={c.get('threadsConsistent')}, "
          f"hash={c.get('hash')} verify={c.get('verify')}")
    if not c.get("ran"):
        errors.append("cpu test did not run")
    if not c.get("threadsConsistent"):
        errors.append("single-thread and multi-thread CPU results differ")
    if require_match and c.get("verify") != "match":
        errors.append(f"cpu hash verify={c.get('verify')}")
    if errors:
        for e in errors:
            print(f"FAIL: {e}")
        sys.exit(1)
    print("PASS")


if __name__ == "__main__":
    main()
