#include "detmath.h"

namespace tsb {

M4 m4_identity() {
    M4 r;
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

M4 m4_mul(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; ++c) {
        for (int rr = 0; rr < 4; ++rr) {
            float s = a.m[0 * 4 + rr] * b.m[c * 4 + 0];
            s = s + a.m[1 * 4 + rr] * b.m[c * 4 + 1];
            s = s + a.m[2 * 4 + rr] * b.m[c * 4 + 2];
            s = s + a.m[3 * 4 + rr] * b.m[c * 4 + 3];
            r.m[c * 4 + rr] = s;
        }
    }
    return r;
}

V4 m4_mul_v4(const M4& a, V4 v) {
    V4 r;
    r.x = a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w;
    r.y = a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w;
    r.z = a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w;
    r.w = a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w;
    return r;
}

M4 m4_perspective(float fovY, float aspect, float zNear, float zFar) {
    float half = fovY * 0.5f;
    float f = dm::cos(half) / dm::sin(half);
    M4 r;
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

M4 m4_look_at(V3 eye, V3 target, V3 up) {
    V3 f = normalize(target - eye);
    V3 s = normalize(cross(f, up));
    V3 u = cross(s, f);
    M4 r = m4_identity();
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

M4 m4_inverse_rigid(const M4& m) {
    M4 r = m4_identity();
    // transpose rotation
    for (int c = 0; c < 3; ++c)
        for (int rr = 0; rr < 3; ++rr) r.m[c * 4 + rr] = m.m[rr * 4 + c];
    V3 t = v3(m.m[12], m.m[13], m.m[14]);
    r.m[12] = -(r.m[0] * t.x + r.m[4] * t.y + r.m[8] * t.z);
    r.m[13] = -(r.m[1] * t.x + r.m[5] * t.y + r.m[9] * t.z);
    r.m[14] = -(r.m[2] * t.x + r.m[6] * t.y + r.m[10] * t.z);
    return r;
}

static Plane make_plane(float a, float b, float c, float d) {
    float inv = 1.0f / dm::sqrt(a * a + b * b + c * c);
    Plane p = {a * inv, b * inv, c * inv, d * inv};
    return p;
}

Frustum frustum_from_matrix(const M4& m) {
    // rows of the matrix
    float r0[4] = {m.m[0], m.m[4], m.m[8], m.m[12]};
    float r1[4] = {m.m[1], m.m[5], m.m[9], m.m[13]};
    float r2[4] = {m.m[2], m.m[6], m.m[10], m.m[14]};
    float r3[4] = {m.m[3], m.m[7], m.m[11], m.m[15]};
    Frustum f;
    f.p[0] = make_plane(r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]);  // left
    f.p[1] = make_plane(r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]);  // right
    f.p[2] = make_plane(r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]);  // bottom
    f.p[3] = make_plane(r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);  // top
    f.p[4] = make_plane(r3[0] + r2[0], r3[1] + r2[1], r3[2] + r2[2], r3[3] + r2[3]);  // near
    f.p[5] = make_plane(r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]);  // far
    return f;
}

V3 catmull_rom(V3 p0, V3 p1, V3 p2, V3 p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float a = -0.5f * t3 + t2 - 0.5f * t;
    float b = 1.5f * t3 - 2.5f * t2 + 1.0f;
    float c = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float d = 0.5f * t3 - 0.5f * t2;
    return p0 * a + p1 * b + p2 * c + p3 * d;
}

}  // namespace tsb
