#include "sim_asteroids.h"

namespace tsb {

static const float kGM = 60000.0f;       // central star
static const float kMoonGM = 1500.0f;    // perturbing moon
static const float kMoonSoft = 100.0f;   // softening (units^2)
static const float kMoonRadius = 150.0f;
const float AsteroidSim::kMoonScale = 7.0f;

static void* alloc_f(u32 n) { return alloc_array<float>(n); }

bool AsteroidSim::init(u32 count) {
    m_count = count;
    m_chunkSize = (count + CHUNKS - 1) / CHUNKS;
    m_px = (float*)alloc_f(count);
    m_py = (float*)alloc_f(count);
    m_pz = (float*)alloc_f(count);
    m_vx = (float*)alloc_f(count);
    m_vy = (float*)alloc_f(count);
    m_vz = (float*)alloc_f(count);
    m_qx = (float*)alloc_f(count);
    m_qy = (float*)alloc_f(count);
    m_qz = (float*)alloc_f(count);
    m_qw = (float*)alloc_f(count);
    m_wx = (float*)alloc_f(count);
    m_wy = (float*)alloc_f(count);
    m_wz = (float*)alloc_f(count);
    m_scale = (float*)alloc_f(count);
    m_color = alloc_array<u32>(count);
    m_bucket = alloc_array<u8>(count);
    m_table.init(CHUNKS, BUCKETS);
    reset();
    return true;
}

void AsteroidSim::release() {
    float* arrays[] = {m_px, m_py, m_pz, m_vx, m_vy, m_vz, m_qx, m_qy, m_qz, m_qw, m_wx, m_wy, m_wz, m_scale};
    for (float* a : arrays) mem_free(a);
    mem_free(m_color);
    mem_free(m_bucket);
    m_table.release();
    m_px = nullptr;
    m_count = 0;
}

void AsteroidSim::reset() {
    Pcg32 rng;
    rng.seed(0x5EED0001ull, 1);
    for (u32 i = 0; i < m_count; ++i) {
        // Radius distribution: denser in the middle of the belt
        float u0 = rng.unit();
        float u1 = rng.unit();
        float u = (u0 + u1) * 0.5f;
        float r = 70.0f + 120.0f * u;
        float theta = rng.unit() * dm::TWO_PI;
        float h0 = rng.unit();
        float h1 = rng.unit();
        float h2 = rng.unit();
        float thick = (h0 + h1 + h2 - 1.5f) * (2.0f + 0.04f * r);
        float c = dm::cos(theta), s = dm::sin(theta);
        m_px[i] = r * c;
        m_py[i] = thick;
        m_pz[i] = r * s;
        float vc = dm::sqrt(kGM / r) * (1.0f + (rng.unit() - 0.5f) * 0.06f);
        float vr = (rng.unit() - 0.5f) * 1.0f;
        m_vx[i] = -s * vc + c * vr;
        m_vz[i] = c * vc + s * vr;
        m_vy[i] = (rng.unit() - 0.5f) * 0.6f;
        // Orientation and spin
        V3 axis = normalize(rand_v3(rng, -1.0f, 1.0f));
        Quat q = quat_axis_angle(axis, rng.unit() * dm::TWO_PI);
        m_qx[i] = q.x;
        m_qy[i] = q.y;
        m_qz[i] = q.z;
        m_qw[i] = q.w;
        V3 spinAxis = normalize(rand_v3(rng, -1.0f, 1.0f));
        float spin = rng.range(0.2f, 1.8f);
        m_wx[i] = spinAxis.x * spin;
        m_wy[i] = spinAxis.y * spin;
        m_wz[i] = spinAxis.z * spin;
        float su = rng.unit();
        m_scale[i] = 0.25f + 2.4f * su * su * su;
        float tone = rng.range(0.65f, 1.05f);
        float warm = rng.range(-0.06f, 0.08f);
        u32 seedByte = rng.next() & 0xffu;
        m_color[i] = pack_rgba8(0.46f * tone + warm, 0.41f * tone, 0.37f * tone - warm * 0.5f, 0.0f) |
                     (seedByte << 24);
    }
    m_visHash = Fnv64();
}

V3 AsteroidSim::moon_pos(float t) {
    float w = dm::sqrt(kGM / (kMoonRadius * kMoonRadius * kMoonRadius));
    float a = w * t + 1.0f;
    return v3(kMoonRadius * dm::cos(a), 3.0f * dm::sin(0.3f * t), kMoonRadius * dm::sin(a));
}

void AsteroidSim::camera_at(u32 frame, CameraState& cam) {
    float t = frame_time(frame);
    float rc = 150.0f + 12.0f * dm::sin(0.09f * t);
    float tc = 0.4f - 0.07f * t;  // against the orbital flow: rocks stream towards the camera
    V3 eye = v3(rc * dm::cos(tc), 16.0f + 7.0f * dm::sin(0.21f * t), rc * dm::sin(tc));
    float tt = tc - 0.55f;
    float rt = rc - 55.0f + 15.0f * dm::sin(0.13f * t);
    V3 target = v3(rt * dm::cos(tt), -2.0f + 2.0f * dm::sin(0.17f * t), rt * dm::sin(tt));
    camera_build(cam, eye, target, 60.0f * (dm::PI / 180.0f), 0.5f, 800.0f);
}

void AsteroidSim::build(JobChain& chain, u32 frame, void* dst, AsteroidFrame* out) {
    m_out = out;
    m_dst = (AsteroidInstance*)dst;
    out->time = frame_time(frame);
    camera_at(frame, out->cam);
    out->moonPos = moon_pos(out->time);
    chain.reset();
    chain.add(job_simulate, this, CHUNKS);
    chain.add(job_prefix, this, 1);
    chain.add(job_write, this, CHUNKS);
}

void AsteroidSim::job_simulate(void* ctx, u32 chunk) {
    AsteroidSim* s = (AsteroidSim*)ctx;
    const AsteroidFrame* f = s->m_out;
    const float dt = kFrameDt;
    const V3 moon = f->moonPos;
    const V3 cam = f->cam.pos;
    const Frustum& fr = f->cam.frustum;
    u32* counts = s->m_table.chunk_counts(chunk);
    for (u32 b = 0; b < BUCKETS; ++b) counts[b] = 0;

    u32 begin = chunk * s->m_chunkSize;
    u32 end = tmin(begin + s->m_chunkSize, s->m_count);
    for (u32 i = begin; i < end; ++i) {
        V3 p = v3(s->m_px[i], s->m_py[i], s->m_pz[i]);
        V3 v = v3(s->m_vx[i], s->m_vy[i], s->m_vz[i]);
        // Gravity: central star + softened moon
        float r2 = dot(p, p);
        float r = dm::sqrt(r2);
        V3 a = p * (-kGM / (r2 * r));
        V3 dm_ = moon - p;
        float d2 = dot(dm_, dm_) + kMoonSoft;
        float d = dm::sqrt(d2);
        a = a + dm_ * (kMoonGM / (d2 * d));
        // Symplectic Euler
        v = v + a * dt;
        p = p + v * dt;
        s->m_px[i] = p.x;
        s->m_py[i] = p.y;
        s->m_pz[i] = p.z;
        s->m_vx[i] = v.x;
        s->m_vy[i] = v.y;
        s->m_vz[i] = v.z;
        // Spin
        Quat q = {s->m_qx[i], s->m_qy[i], s->m_qz[i], s->m_qw[i]};
        q = quat_integrate(q, v3(s->m_wx[i], s->m_wy[i], s->m_wz[i]), dt);
        s->m_qx[i] = q.x;
        s->m_qy[i] = q.y;
        s->m_qz[i] = q.z;
        s->m_qw[i] = q.w;
        // Culling + LOD
        float sc = s->m_scale[i];
        V3 toCam = p - cam;
        float dist = length(toCam);
        u8 bucket = 0xff;
        if (dist < 520.0f && dist < 900.0f * sc && frustum_sphere(fr, p, sc * 1.8f)) {
            u32 lod = dist < 24.0f + 10.0f * sc ? 0u : (dist < 70.0f + 25.0f * sc ? 1u : 2u);
            u32 shape = i & (SHAPES - 1);
            bucket = (u8)(shape * LODS + lod);
            counts[bucket]++;
        }
        s->m_bucket[i] = bucket;
    }
}

void AsteroidSim::job_prefix(void* ctx, u32) {
    AsteroidSim* s = (AsteroidSim*)ctx;
    AsteroidFrame* f = s->m_out;
    f->visible = s->m_table.prefix(f->bucketStart, f->bucketCount);
    for (u32 b = 0; b < BUCKETS; ++b) s->m_visHash.u32v(f->bucketCount[b]);
    // Moon instance lives after all asteroids
    AsteroidInstance& m = s->m_dst[s->m_count];
    m.px = f->moonPos.x;
    m.py = f->moonPos.y;
    m.pz = f->moonPos.z;
    m.scale = kMoonScale;
    Quat q = quat_axis_angle(v3(0.0f, 1.0f, 0.0f), f->time * 0.1f);
    m.qx = q.x;
    m.qy = q.y;
    m.qz = q.z;
    m.qw = q.w;
    m.color = pack_rgba8(0.55f, 0.53f, 0.5f, 0.5f);
}

void AsteroidSim::job_write(void* ctx, u32 chunk) {
    AsteroidSim* s = (AsteroidSim*)ctx;
    u32 offs[BUCKETS];
    for (u32 b = 0; b < BUCKETS; ++b) offs[b] = s->m_table.offsets[chunk * BUCKETS + b];
    u32 begin = chunk * s->m_chunkSize;
    u32 end = tmin(begin + s->m_chunkSize, s->m_count);
    AsteroidInstance* dst = s->m_dst;
    for (u32 i = begin; i < end; ++i) {
        u8 b = s->m_bucket[i];
        if (b == 0xff) continue;
        AsteroidInstance inst;
        inst.px = s->m_px[i];
        inst.py = s->m_py[i];
        inst.pz = s->m_pz[i];
        inst.scale = s->m_scale[i];
        inst.qx = s->m_qx[i];
        inst.qy = s->m_qy[i];
        inst.qz = s->m_qz[i];
        inst.qw = s->m_qw[i];
        inst.color = s->m_color[i];
        dst[offs[b]++] = inst;
    }
}

u64 AsteroidSim::hash() const {
    Fnv64 h = m_visHash;
    h.floats(m_px, m_count);
    h.floats(m_py, m_count);
    h.floats(m_pz, m_count);
    h.floats(m_vx, m_count);
    h.floats(m_vy, m_count);
    h.floats(m_vz, m_count);
    h.floats(m_qx, m_count);
    h.floats(m_qy, m_count);
    h.floats(m_qz, m_count);
    h.floats(m_qw, m_count);
    return h.h;
}

}  // namespace tsb
