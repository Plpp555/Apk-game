// Deterministic float math.
//
// Every platform must produce bit-identical simulation results, so the core
// never calls the platform libm (sinf/cosf/expf differ between bionic, MSVC
// and glibc). Only IEEE-754 correctly rounded operations are used:
//   + - * / and sqrt, plus conversions between float and int32.
// Transcendental functions are fixed polynomials evaluated in a fixed order.
//
// Build requirements (set by CMakeLists.txt, documented in docs/SPEC.md):
//   clang/gcc : -ffp-contract=off -fno-fast-math   (no FMA fusion)
//   MSVC      : /fp:precise  (contractions are off by default since VS2022)
#pragma once

#include "base.h"

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#include <math.h>
#endif

namespace tsb {
namespace dm {

static const float PI = 3.14159265358979323846f;
static const float TWO_PI = 6.28318530717958647692f;
static const float HALF_PI = 1.57079632679489661923f;
static const float INV_TWO_PI = 0.15915494309189533577f;

inline float sqrt(float x) {
#if defined(_MSC_VER) && !defined(__clang__)
    return ::sqrtf(x);
#else
    return __builtin_sqrtf(x);
#endif
}

inline float abs(float x) { return x < 0.0f ? -x : x; }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// floor for |x| < 2^31 using only conversions (no libm, no rounding-mode dependence).
inline float floor(float x) {
    if (x >= 2147483520.0f || x <= -2147483520.0f) return x;
    float t = (float)(i32)x;  // truncation toward zero
    return (t > x) ? t - 1.0f : t;
}
inline i32 floori(float x) {
    x = clampf(x, -2147483520.0f, 2147483520.0f);
    i32 t = (i32)x;
    return ((float)t > x) ? t - 1 : t;
}
inline float fract(float x) { return x - floor(x); }

// sin: Cody-Waite reduction to [-pi, pi], fold to [-pi/2, pi/2], degree-11 odd polynomial.
inline float sin(float x) {
    const float C1 = 6.28125f;                 // 2*pi high part (exact, 8 bits)
    const float C2 = 1.9353071795864769253e-3f;  // 2*pi - C1
    float k = floor(x * INV_TWO_PI + 0.5f);
    float r = x - k * C1;
    r = r - k * C2;
    if (r > HALF_PI) r = PI - r;
    else if (r < -HALF_PI) r = -PI - r;
    float r2 = r * r;
    float p = -2.5052108385441718775e-8f;
    p = p * r2 + 2.7557319223985890653e-6f;
    p = p * r2 - 1.9841269841269841270e-4f;
    p = p * r2 + 8.3333333333333333333e-3f;
    p = p * r2 - 1.6666666666666666667e-1f;
    return r + r * r2 * p;
}
inline float cos(float x) { return sin(x + HALF_PI); }

// exp2 for moderate ranges (|x| < 126): integer part via exponent bits, fraction by polynomial.
inline float exp2(float x) {
    x = clampf(x, -126.0f, 126.0f);
    float fi = floor(x);
    float f = x - fi;
    float p = 1.5353362e-4f;
    p = p * f + 1.3398874e-3f;
    p = p * f + 9.6184197e-3f;
    p = p * f + 5.5503406e-2f;
    p = p * f + 2.4022650e-1f;
    p = p * f + 6.9314720e-1f;
    p = p * f + 1.0f;
    u32 expBits = (u32)((i32)fi + 127) << 23;
    float scale;
    memcpy(&scale, &expBits, 4);
    return p * scale;
}
inline float exp(float x) { return exp2(x * 1.44269504088896340736f); }

inline u32 float_bits(float f) {
    u32 u;
    memcpy(&u, &f, 4);
    return u;
}

}  // namespace dm

struct V3 {
    float x, y, z;
};
inline V3 v3(float x, float y, float z) {
    V3 r = {x, y, z};
    return r;
}
inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator-(V3 a) { return v3(-a.x, -a.y, -a.z); }
inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
inline V3 operator*(V3 a, V3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float length(V3 a) { return dm::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) {
    float l2 = dot(a, a);
    if (l2 < 1.0e-20f) return v3(0.0f, 1.0f, 0.0f);
    return a * (1.0f / dm::sqrt(l2));
}
inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }

struct V4 {
    float x, y, z, w;
};
inline V4 v4(float x, float y, float z, float w) {
    V4 r = {x, y, z, w};
    return r;
}

// Quaternion (x, y, z, w)
struct Quat {
    float x, y, z, w;
};
inline Quat quat_mul(Quat a, Quat b) {
    Quat r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}
inline Quat quat_normalize(Quat q) {
    float l2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    float inv = 1.0f / dm::sqrt(l2);
    Quat r = {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    return r;
}
inline Quat quat_axis_angle(V3 axis, float angle) {
    float s = dm::sin(angle * 0.5f);
    Quat q = {axis.x * s, axis.y * s, axis.z * s, dm::cos(angle * 0.5f)};
    return q;
}
// q' = q + 0.5 * dt * q * (0, wBody) ; renormalized
inline Quat quat_integrate(Quat q, V3 wBody, float dt) {
    Quat w = {wBody.x, wBody.y, wBody.z, 0.0f};
    Quat d = quat_mul(q, w);
    float h = 0.5f * dt;
    Quat r = {q.x + d.x * h, q.y + d.y * h, q.z + d.z * h, q.w + d.w * h};
    return quat_normalize(r);
}

// Column-major 4x4 matrix (OpenGL convention), m[col*4 + row].
struct M4 {
    float m[16];
};
M4 m4_identity();
M4 m4_mul(const M4& a, const M4& b);
M4 m4_perspective(float fovYRadians, float aspect, float zNear, float zFar);
M4 m4_look_at(V3 eye, V3 target, V3 up);
M4 m4_inverse_rigid(const M4& m);  // inverse for rotation+translation matrices
V4 m4_mul_v4(const M4& a, V4 v);

// Plane: dot(n, p) + d >= 0 means inside.
struct Plane {
    float nx, ny, nz, d;
};
struct Frustum {
    Plane p[6];
};
Frustum frustum_from_matrix(const M4& viewProj);
inline bool frustum_sphere(const Frustum& f, V3 c, float r) {
    for (int i = 0; i < 6; ++i) {
        const Plane& pl = f.p[i];
        if (pl.nx * c.x + pl.ny * c.y + pl.nz * c.z + pl.d < -r) return false;
    }
    return true;
}

// Catmull-Rom spline through points (uniform parameterisation).
V3 catmull_rom(V3 p0, V3 p1, V3 p2, V3 p3, float t);

inline u32 pack_rgba8(float r, float g, float b, float a) {
    u32 R = (u32)(dm::clampf(r, 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 G = (u32)(dm::clampf(g, 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 B = (u32)(dm::clampf(b, 0.0f, 1.0f) * 255.0f + 0.5f);
    u32 A = (u32)(dm::clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
    return R | (G << 8) | (B << 16) | (A << 24);
}

}  // namespace tsb
