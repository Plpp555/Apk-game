#include "shaders.h"

namespace tsb {
namespace shaders {

// Integer hash + value noise, bit-compatible with core/rng.h (hash32/lattice).
// Prepended to fragment shaders that need it (see gl::program fsPrefix).
const char* NOISE = R"(
uint hash_u(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}
float lattice(ivec3 p, uint seed) {
    uint h = hash_u(uint(p.x) * 0x8da6b343u ^ uint(p.y) * 0xd8163841u ^ uint(p.z) * 0xcb1ab31fu ^ seed);
    return float(h >> 8u) * (1.0 / 8388608.0) - 1.0;
}
float vnoise(vec3 p, uint seed) {
    vec3 fl = floor(p);
    ivec3 i = ivec3(fl);
    vec3 f = p - fl;
    f = f * f * (3.0 - 2.0 * f);
    float c000 = lattice(i, seed);
    float c100 = lattice(i + ivec3(1, 0, 0), seed);
    float c010 = lattice(i + ivec3(0, 1, 0), seed);
    float c110 = lattice(i + ivec3(1, 1, 0), seed);
    float c001 = lattice(i + ivec3(0, 0, 1), seed);
    float c101 = lattice(i + ivec3(1, 0, 1), seed);
    float c011 = lattice(i + ivec3(0, 1, 1), seed);
    float c111 = lattice(i + ivec3(1, 1, 1), seed);
    return mix(mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
               mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y), f.z);
}
float fbm4(vec3 p, uint seed) {
    float s = 0.0;
    float a = 0.5;
    for (int k = 0; k < 4; ++k) {
        s += a * vnoise(p, seed + uint(k));
        p = p * 2.02 + vec3(1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s;
}
)";

const char* FULLSCREEN_VS = R"(
out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* SPACE_SKY_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamFwd;
uniform vec2 uTanHalf;
uniform vec3 uNebulaA;
uniform vec3 uNebulaB;
uniform float uNebula;
uniform float uTime;

float star_layer(vec3 d, float scale, uint seed, float thresh) {
    vec3 q = d * scale;
    vec3 c = floor(q);
    ivec3 ic = ivec3(c);
    float h = lattice(ic, seed) * 0.5 + 0.5;
    if (h < thresh) return 0.0;
    vec3 jitter = vec3(lattice(ic, seed + 11u), lattice(ic, seed + 23u), lattice(ic, seed + 37u)) * 0.3 + 0.5;
    float dist = length(q - (c + jitter));
    float b = (h - thresh) / (1.0 - thresh);
    return b * b * 6.0 * max(1.0 - dist * 7.0, 0.0);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 d = normalize(uCamFwd + uCamRight * (ndc.x * uTanHalf.x) + uCamUp * (ndc.y * uTanHalf.y));
    float s = star_layer(d, 180.0, 101u, 0.86) + star_layer(d, 420.0, 202u, 0.93) * 0.6;
    float n = fbm4(d * 2.2 + vec3(0.0, uTime * 0.01, 0.0), 7u);
    float n2 = fbm4(d * 4.5 + vec3(3.0), 13u);
    float dens = clamp(n * 1.3 + 0.42, 0.0, 1.0);
    dens = dens * dens * dens;
    vec3 neb = mix(uNebulaA, uNebulaB, clamp(n2 * 1.5 + 0.5, 0.0, 1.0)) * dens * uNebula;
    vec3 starCol = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.85, 0.7), clamp(n2 + 0.5, 0.0, 1.0));
    oColor = vec4(neb + starCol * s * (1.0 - dens * 0.7), 1.0);
}
)";

const char* CITY_SKY_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamFwd;
uniform vec2 uTanHalf;
uniform vec3 uMoonDir;
uniform float uTime;

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 d = normalize(uCamFwd + uCamRight * (ndc.x * uTanHalf.x) + uCamUp * (ndc.y * uTanHalf.y));
    float h = clamp(d.y, -0.2, 1.0);
    vec3 horizon = vec3(0.22, 0.08, 0.16);
    vec3 zenith = vec3(0.005, 0.01, 0.03);
    vec3 col = mix(horizon, zenith, pow(clamp(h * 2.5, 0.0, 1.0), 0.6));
    float clouds = fbm4(vec3(d.xz / max(d.y, 0.08) * 0.8, uTime * 0.02), 31u);
    col += vec3(0.10, 0.04, 0.09) * clamp(clouds + 0.2, 0.0, 1.0) * clamp(1.0 - h * 2.0, 0.0, 1.0);
    float m = max(dot(d, uMoonDir), 0.0);
    col += vec3(0.9, 0.95, 1.0) * (pow(m, 900.0) * 6.0 + pow(m, 30.0) * 0.08);
    oColor = vec4(col, 1.0);
}
)";

const char* ASTEROID_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 iPosScale;
layout(location = 3) in vec4 iQuat;
layout(location = 4) in vec4 iColor;
uniform mat4 uViewProj;
out vec3 vWorld;
out vec3 vNrm;
out vec3 vObj;
out vec3 vColor;

vec3 qrot(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void main() {
    vec3 wp = qrot(iQuat, aPos * iPosScale.w) + iPosScale.xyz;
    vWorld = wp;
    vNrm = qrot(iQuat, aNrm);
    vObj = aPos * 2.5 + vec3(iColor.a * 37.0);
    vColor = iColor.rgb;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

const char* ASTEROID_FS = R"(
in vec3 vWorld;
in vec3 vNrm;
in vec3 vObj;
in vec3 vColor;
out vec4 oColor;
uniform vec3 uCamPos;
uniform vec3 uStarColor;
uniform vec3 uAmbient;
uniform float uFogDensity;
uniform vec3 uFogColor;

void main() {
    vec3 N = normalize(vNrm);
    float n1 = vnoise(vObj * 0.8, 3u);
    float n2 = vnoise(vObj * 3.1 + vec3(5.1), 5u);
    float h = n1 * 0.7 + n2 * 0.3;
    // Derivative bump mapping (Mikkelsen 2010)
    vec3 dpdx = dFdx(vWorld);
    vec3 dpdy = dFdy(vWorld);
    float dhx = dFdx(h);
    float dhy = dFdy(h);
    vec3 r1 = cross(dpdy, N);
    vec3 r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    vec3 grad = sign(det) * (dhx * r1 + dhy * r2);
    vec3 Nb = normalize(abs(det) * N - 0.22 * grad);
    if (!(dot(Nb, Nb) > 0.0)) Nb = N;

    vec3 albedo = vColor * (0.75 + 0.25 * n1) * (0.92 + 0.12 * n2);
    vec3 toStar = -vWorld;
    float dist = length(toStar);
    vec3 L = toStar / max(dist, 1e-3);
    float ndl = max(dot(Nb, L), 0.0);
    float atten = 9000.0 / (dist * dist + 4000.0);
    vec3 V = normalize(uCamPos - vWorld);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(Nb, H), 0.0), 32.0) * 0.12;
    vec3 col = albedo * (uStarColor * ndl * atten + uAmbient) + uStarColor * spec * atten;
    float rim = 1.0 - max(dot(Nb, V), 0.0);
    col += uAmbient * 2.0 * rim * rim * rim;
    float fog = 1.0 - exp(-length(uCamPos - vWorld) * uFogDensity);
    oColor = vec4(mix(col, uFogColor, fog), 1.0);
}
)";

const char* STAR_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uViewProj;
uniform vec4 uPosRadius;
out vec3 vDir;
out vec3 vWorld;
void main() {
    vDir = aNrm;
    vec3 wp = uPosRadius.xyz + aPos * uPosRadius.w;
    vWorld = wp;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

const char* STAR_FS = R"(
in vec3 vDir;
in vec3 vWorld;
out vec4 oColor;
uniform vec3 uCamPos;
uniform float uTime;
void main() {
    vec3 N = normalize(vDir);
    float n = fbm4(N * 3.0 + vec3(uTime * 0.05, uTime * 0.03, 0.0), 77u);
    float g = fbm4(N * 9.0 - vec3(0.0, uTime * 0.08, 0.0), 91u);
    vec3 V = normalize(uCamPos - vWorld);
    float limb = pow(max(dot(N, V), 0.0), 0.4);
    vec3 col = mix(vec3(1.0, 0.35, 0.05), vec3(1.0, 0.92, 0.65), clamp(n + 0.55 + g * 0.3, 0.0, 1.0));
    oColor = vec4(col * (5.0 + 3.0 * g) * (0.45 + 0.55 * limb), 1.0);
}
)";

const char* PARTICLE_VS = R"(
layout(location = 0) in vec4 iPosSize;
layout(location = 1) in vec4 iColor;
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
out vec2 vCorner;
out vec4 vColor;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vCorner = c;
    vColor = iColor;
    vec3 wp = iPosSize.xyz + (uCamRight * c.x + uCamUp * c.y) * iPosSize.w;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

const char* PARTICLE_FS = R"(
in vec2 vCorner;
in vec4 vColor;
out vec4 oColor;
uniform float uIntensity;
void main() {
    float d2 = dot(vCorner, vCorner);
    float a = max(1.0 - d2, 0.0);
    a = a * a;
    float core = max(1.0 - d2 * 6.0, 0.0);
    vec3 c = vColor.rgb * (a + core * 1.5) * vColor.a * uIntensity;
    oColor = vec4(c, 0.0);
}
)";

const char* SPHERE_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 iPosRad;
layout(location = 3) in vec4 iColor;
uniform mat4 uViewProj;
out vec3 vNrm;
out vec3 vWorld;
out vec4 vColor;
void main() {
    vec3 wp = iPosRad.xyz + aPos * iPosRad.w;
    vWorld = wp;
    vNrm = aNrm;
    vColor = iColor;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

const char* SPHERE_FS = R"(
in vec3 vNrm;
in vec3 vWorld;
in vec4 vColor;
out vec4 oColor;
uniform vec3 uLightDir;
uniform vec3 uCamPos;
uniform float uEmissive;
void main() {
    vec3 N = normalize(vNrm);
    vec3 V = normalize(uCamPos - vWorld);
    float ndl = max(dot(N, uLightDir), 0.0);
    vec3 H = normalize(uLightDir + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0);
    vec3 lit = vColor.rgb * (ndl * 1.2 + 0.12) + vec3(spec * 0.5);
    vec3 emi = vColor.rgb * (2.0 + 6.0 * pow(max(dot(N, V), 0.0), 2.0));
    oColor = vec4(mix(lit, emi, uEmissive), 1.0);
}
)";

const char* BUILDING_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec4 iA;
layout(location = 3) in vec4 iB;
uniform mat4 uViewProj;
uniform mat4 uView;
out vec3 vWorld;
out vec3 vNrm;
out float vViewZ;
flat out float vSeed;
void main() {
    vec3 wp = vec3(iA.x + aPos.x * iA.z, aPos.y * iB.x, iA.y + aPos.z * iA.w);
    vWorld = wp;
    vNrm = aNrm;
    vSeed = iB.y;
    vViewZ = -(uView * vec4(wp, 1.0)).z;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

const char* GROUND_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
uniform mat4 uViewProj;
uniform mat4 uView;
uniform vec2 uGroundOffset;
out vec3 vWorld;
out vec3 vNrm;
out float vViewZ;
flat out float vSeed;
void main() {
    vec3 wp = aPos + vec3(uGroundOffset.x, 0.0, uGroundOffset.y);
    vWorld = wp;
    vNrm = aNrm;
    vSeed = 0.0;
    vViewZ = -(uView * vec4(wp, 1.0)).z;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

const char* CITY_FS = R"(
in vec3 vWorld;
in vec3 vNrm;
in float vViewZ;
flat in float vSeed;
out vec4 oColor;

// Light data: 256x2 RGBA32F texture, row 0 = position/radius, row 1 = colour.
// (A 256-entry uniform block exceeds the default-uniform limit on some drivers.)
uniform highp sampler2D uLights;
uniform highp usampler2D uClusters;
uniform vec2 uTileSize;
uniform float uSliceScale;
uniform float uSliceBias;
uniform vec3 uCamPos;
uniform vec3 uMoonDir;
uniform vec3 uFogColor;
uniform float uBlockPitch;
uniform int uMaterial;

vec3 cluster_lights(vec3 wp, vec3 N, vec3 V, vec3 albedo, float rough) {
    ivec2 tile = ivec2(gl_FragCoord.xy / uTileSize);
    tile = clamp(tile, ivec2(0), ivec2(15, 7));
    int slice = clamp(int(log2(max(vViewZ, 0.01)) * uSliceScale + uSliceBias), 0, 15);
    int cid = tile.x + tile.y * 16 + slice * 128;
    int cnt = int(texelFetch(uClusters, ivec2(cid, 0), 0).r);
    float a = rough * rough;
    float a2 = a * a;
    vec3 acc = vec3(0.0);
    for (int k = 0; k < cnt; ++k) {
        int li = int(texelFetch(uClusters, ivec2(cid, k + 1), 0).r);
        vec4 pr = texelFetch(uLights, ivec2(li, 0), 0);
        vec3 Lv = pr.xyz - wp;
        float d2 = dot(Lv, Lv);
        float r2 = pr.w * pr.w;
        if (d2 >= r2) continue;
        float invD = inversesqrt(d2);
        vec3 L = Lv * invD;
        float w = 1.0 - d2 / r2;
        float att = w * w / (1.0 + d2 * 0.08);
        float ndl = max(dot(N, L), 0.0);
        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float dd = ndh * ndh * (a2 - 1.0) + 1.0;
        float D = a2 / (3.14159265 * dd * dd);
        acc += texelFetch(uLights, ivec2(li, 1), 0).rgb * att * ndl * (albedo * 0.31830989 + vec3(D * 0.2));
    }
    return acc;
}

void main() {
    vec3 N = normalize(vNrm);
    vec3 V = normalize(uCamPos - vWorld);
    vec3 albedo;
    float rough;
    vec3 emissive = vec3(0.0);
    if (uMaterial == 0) {
        vec2 fc = abs(N.x) > 0.5 ? vec2(vWorld.z, vWorld.y) : vec2(vWorld.x, vWorld.y);
        vec2 cell = fc / vec2(1.6, 3.0);
        vec2 ci = floor(cell);
        vec2 cf = cell - ci;
        float win = step(0.18, cf.x) * step(cf.x, 0.82) * step(0.25, cf.y) * step(cf.y, 0.8);
        uint wh = hash_u(uint(int(ci.x) + 8192) * 73856093u ^ uint(int(ci.y) + 8192) * 19349663u ^
                         uint(vSeed * 4096.0));
        float lit = (wh & 255u) < 64u ? 1.0 : 0.0;
        vec3 wcol = (wh & 256u) != 0u ? vec3(1.0, 0.72, 0.42) : vec3(0.5, 0.72, 1.0);
        albedo = mix(vec3(0.10, 0.10, 0.11), vec3(0.02, 0.025, 0.03), win);
        rough = mix(0.75, 0.12, win);
        emissive = wcol * (win * lit * (1.1 + 0.6 * float((wh >> 9u) & 3u)));
        if (N.y > 0.5) {
            albedo = vec3(0.05);
            rough = 0.9;
            emissive = vec3(0.0);
        }
    } else {
        vec2 g = vWorld.xz;
        vec2 m = mod(g, uBlockPitch);
        vec2 ds = min(m, vec2(uBlockPitch) - m);
        float street = step(min(ds.x, ds.y), 2.5);
        float dashX = step(ds.x, 0.07) * step(0.5, fract(g.y * 0.2)) * step(2.5, ds.y);
        float dashY = step(ds.y, 0.07) * step(0.5, fract(g.x * 0.2)) * step(2.5, ds.x);
        float mark = max(dashX, dashY);
        float puddle = clamp(vnoise(vec3(g * 0.15, 0.0), 41u) * 2.0 + 0.3, 0.0, 1.0);
        albedo = mix(vec3(0.07, 0.07, 0.075), vec3(0.025), street);
        albedo = mix(albedo, vec3(0.55, 0.5, 0.3), mark);
        rough = mix(0.7, mix(0.35, 0.08, puddle), street);
    }
    vec3 col = albedo * (vec3(0.025, 0.03, 0.05) + vec3(0.08, 0.09, 0.14) * max(dot(N, uMoonDir), 0.0));
    col += cluster_lights(vWorld, N, V, albedo, rough);
    col += emissive;
    float fd = length(uCamPos - vWorld);
    float fog = 1.0 - exp(-fd * 0.0045);
    oColor = vec4(mix(col, uFogColor, fog), 1.0);
}
)";

const char* BRIGHT_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform float uThreshold;
void main() {
    vec3 c = texture(uSrc, vUV + uTexel * vec2(-1.0, -1.0)).rgb;
    c += texture(uSrc, vUV + uTexel * vec2(1.0, -1.0)).rgb;
    c += texture(uSrc, vUV + uTexel * vec2(-1.0, 1.0)).rgb;
    c += texture(uSrc, vUV + uTexel * vec2(1.0, 1.0)).rgb;
    c *= 0.25;
    float br = max(c.r, max(c.g, c.b));
    float k = max(br - uThreshold, 0.0) / max(br, 1e-4);
    oColor = vec4(min(c * k, vec3(64.0)), 1.0);
}
)";

const char* DOWNSAMPLE_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform vec2 uTexel;
void main() {
    vec3 c = texture(uSrc, vUV + uTexel * vec2(-1.0, -1.0)).rgb;
    c += texture(uSrc, vUV + uTexel * vec2(1.0, -1.0)).rgb;
    c += texture(uSrc, vUV + uTexel * vec2(-1.0, 1.0)).rgb;
    c += texture(uSrc, vUV + uTexel * vec2(1.0, 1.0)).rgb;
    oColor = vec4(c * 0.25, 1.0);
}
)";

const char* BLUR_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
uniform vec2 uDir;
void main() {
    vec3 c = texture(uSrc, vUV).rgb * 0.2270270270;
    c += (texture(uSrc, vUV + uDir * 1.3846153846).rgb + texture(uSrc, vUV - uDir * 1.3846153846).rgb) * 0.3162162162;
    c += (texture(uSrc, vUV + uDir * 3.2307692308).rgb + texture(uSrc, vUV - uDir * 3.2307692308).rgb) * 0.0702702703;
    oColor = vec4(c, 1.0);
}
)";

const char* COMPOSITE_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uScene;
uniform sampler2D uBloom1;
uniform sampler2D uBloom2;
uniform sampler2D uBloom3;
uniform float uExposure;
uniform float uBloomStrength;
uniform float uFrame;

uint hash_u(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    vec3 b = texture(uBloom1, vUV).rgb * 0.5 + texture(uBloom2, vUV).rgb * 0.3 + texture(uBloom3, vUV).rgb * 0.2;
    c = (c + b * uBloomStrength) * uExposure;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    vec2 v = vUV - 0.5;
    c *= 1.0 - dot(v, v) * 0.55;
    c = pow(c, vec3(1.0 / 2.2));
    uint h = hash_u(uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u + uint(uFrame) * 26699u);
    c += (float(h & 255u) / 255.0 - 0.5) / 255.0;
    oColor = vec4(c, 1.0);
}
)";

const char* PRESENT_FS = R"(
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSrc;
void main() {
    oColor = vec4(texture(uSrc, vUV).rgb, 1.0);
}
)";

}  // namespace shaders
}  // namespace tsb
