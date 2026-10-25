// Deterministic random numbers and hashing (integer-only, identical everywhere).
#pragma once

#include "detmath.h"

namespace tsb {

// PCG32 (O'Neill), XSH-RR variant.
struct Pcg32 {
    u64 state;
    u64 inc;

    void seed(u64 initState, u64 seq) {
        state = 0u;
        inc = (seq << 1u) | 1u;
        next();
        state += initState;
        next();
    }
    u32 next() {
        u64 old = state;
        state = old * 6364136223846793005ULL + inc;
        u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
        u32 rot = (u32)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }
    // [0, 1) with 24 bits of precision (exact float conversion)
    float unit() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
    u32 below(u32 n) { return (u32)(((u64)next() * (u64)n) >> 32); }
};

// IMPORTANT: never put more than one RNG call in a single expression or
// argument list. C++ leaves the evaluation order of function arguments and
// operands unspecified (gcc: right-to-left, clang: left-to-right, MSVC: varies),
// which silently changes results between compilers. Use these helpers.
inline V3 rand_v3(Pcg32& r, float lo, float hi) {
    float x = r.range(lo, hi);
    float y = r.range(lo, hi);
    float z = r.range(lo, hi);
    return v3(x, y, z);
}
inline V3 rand_v3(Pcg32& r, V3 lo, V3 hi) {
    float x = r.range(lo.x, hi.x);
    float y = r.range(lo.y, hi.y);
    float z = r.range(lo.z, hi.z);
    return v3(x, y, z);
}

// 32-bit integer hash (lowbias32, Wellons)
inline u32 hash32(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float hash_unit(u32 x) { return (float)(hash32(x) >> 8) * (1.0f / 16777216.0f); }

// FNV-1a 64-bit, used for verification hashes of simulation state.
struct Fnv64 {
    u64 h = 0xcbf29ce484222325ULL;
    void u32v(u32 v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (u64)((v >> (i * 8)) & 0xffu);
            h *= 0x100000001b3ULL;
        }
    }
    void f32v(float f) { u32v(dm::float_bits(f)); }
    void floats(const float* p, u32 n) {
        for (u32 i = 0; i < n; ++i) f32v(p[i]);
    }
};

// 3D value noise on an integer lattice (deterministic, used for procedural meshes
// and particle fields on the CPU).
inline float lattice(i32 x, i32 y, i32 z, u32 seed) {
    u32 h = hash32((u32)x * 0x8da6b343u ^ (u32)y * 0xd8163841u ^ (u32)z * 0xcb1ab31fu ^ seed);
    return (float)(h >> 8) * (1.0f / 8388608.0f) - 1.0f;  // [-1, 1)
}
inline float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }
inline float value_noise3(V3 p, u32 seed) {
    i32 ix = dm::floori(p.x), iy = dm::floori(p.y), iz = dm::floori(p.z);
    float fx = smooth01(p.x - (float)ix), fy = smooth01(p.y - (float)iy), fz = smooth01(p.z - (float)iz);
    float c000 = lattice(ix, iy, iz, seed), c100 = lattice(ix + 1, iy, iz, seed);
    float c010 = lattice(ix, iy + 1, iz, seed), c110 = lattice(ix + 1, iy + 1, iz, seed);
    float c001 = lattice(ix, iy, iz + 1, seed), c101 = lattice(ix + 1, iy, iz + 1, seed);
    float c011 = lattice(ix, iy + 1, iz + 1, seed), c111 = lattice(ix + 1, iy + 1, iz + 1, seed);
    float x00 = dm::lerp(c000, c100, fx), x10 = dm::lerp(c010, c110, fx);
    float x01 = dm::lerp(c001, c101, fx), x11 = dm::lerp(c011, c111, fx);
    float y0 = dm::lerp(x00, x10, fy), y1 = dm::lerp(x01, x11, fy);
    return dm::lerp(y0, y1, fz);
}

}  // namespace tsb
