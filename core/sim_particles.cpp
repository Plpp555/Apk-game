#include "sim_particles.h"

namespace tsb {

static const float kAttractorGM = 900.0f;
static const float kAttractorSoft = 30.0f;
static const float kSwirl = 6.0f;
static const float kWave = 3.0f;
static const float kDrag = 0.985f;  // per frame

bool ParticleSim::init(u32 count) {
    m_count = count;
    m_chunkSize = (count + CHUNKS - 1) / CHUNKS;
    m_px = alloc_array<float>(count);
    m_py = alloc_array<float>(count);
    m_pz = alloc_array<float>(count);
    m_vx = alloc_array<float>(count);
    m_vy = alloc_array<float>(count);
    m_vz = alloc_array<float>(count);
    m_life = alloc_array<float>(count);
    m_maxLife = alloc_array<float>(count);
    m_spawnCount = alloc_array<u32>(count);
    reset();
    return true;
}

void ParticleSim::release() {
    float* arrays[] = {m_px, m_py, m_pz, m_vx, m_vy, m_vz, m_life, m_maxLife};
    for (float* a : arrays) mem_free(a);
    mem_free(m_spawnCount);
    m_px = m_py = m_pz = m_vx = m_vy = m_vz = m_life = m_maxLife = nullptr;
    m_spawnCount = nullptr;
    m_count = 0;
}

void ParticleSim::spawn(u32 i) {
    Pcg32 r;
    r.seed(hash32(i * 0x9E3779B9u + m_spawnCount[i] * 0x85EBCA6Bu + 0x1234567u), i);
    float ang = r.unit() * dm::TWO_PI;
    float rad = 48.0f + 10.0f * r.unit();
    float c = dm::cos(ang), s = dm::sin(ang);
    m_px[i] = rad * c;
    m_py[i] = (r.unit() - 0.5f) * 6.0f;
    m_pz[i] = rad * s;
    float vt = 10.0f + 6.0f * r.unit();
    float vin = 3.0f * r.unit();
    m_vx[i] = -s * vt - c * vin;
    m_vy[i] = (r.unit() - 0.5f) * 4.0f;
    m_vz[i] = c * vt - s * vin;
    float life = 3.0f + 5.0f * r.unit();
    m_life[i] = life;
    m_maxLife[i] = life;
    m_spawnCount[i]++;
}

void ParticleSim::reset() {
    // Start from a developed swirling disc (later respawns use the emitter ring).
    Pcg32 r;
    r.seed(0x9E8017ull, 11);
    for (u32 i = 0; i < m_count; ++i) {
        m_spawnCount[i] = 0;
        float ang = r.unit() * dm::TWO_PI;
        float u = r.unit();
        float rad = 6.0f + 74.0f * u * u;
        float y0 = r.unit();
        float y1 = r.unit();
        float c = dm::cos(ang), s = dm::sin(ang);
        m_px[i] = rad * c;
        m_py[i] = (y0 + y1 - 1.0f) * (3.0f + 0.12f * rad);
        m_pz[i] = rad * s;
        float vt = 6.0f + 10.0f * r.unit();
        m_vx[i] = -s * vt;
        m_vy[i] = (r.unit() - 0.5f) * 3.0f;
        m_vz[i] = c * vt;
        float life = 3.0f + 5.0f * r.unit();
        m_maxLife[i] = life;
        m_life[i] = life * (0.05f + 0.95f * r.unit());  // stagger respawns
    }
}

void ParticleSim::camera_at(u32 frame, CameraState& cam) {
    float t = frame_time(frame);
    float k = 0.85f + 0.15f * dm::sin(0.07f * t);
    float a = 0.05f * t + 0.3f;
    V3 eye = v3(110.0f * dm::cos(a) * k, 30.0f + 12.0f * dm::sin(0.11f * t), 110.0f * dm::sin(a) * k);
    V3 target = v3(0.0f, 5.0f * dm::sin(0.13f * t), 0.0f);
    camera_build(cam, eye, target, 55.0f * (dm::PI / 180.0f), 0.5f, 600.0f);
}

void ParticleSim::build(JobChain& chain, u32 frame, void* dst, ParticleFrame* out) {
    m_out = out;
    m_dst = (ParticleInstance*)dst;
    float t = frame_time(frame);
    out->time = t;
    camera_at(frame, out->cam);
    for (u32 k = 0; k < ParticleFrame::ATTRACTORS; ++k) {
        float fk = (float)k;
        out->attractor[k] = v3(38.0f * dm::sin(0.37f * t * (1.0f + 0.2f * fk) + 1.3f * fk),
                               14.0f * dm::sin(0.53f * t + 2.1f * fk),
                               38.0f * dm::cos(0.29f * t * (1.0f + 0.15f * fk) + 0.7f * fk));
    }
    chain.reset();
    chain.add(job_simulate, this, CHUNKS);
}

void ParticleSim::job_simulate(void* ctx, u32 chunk) {
    ParticleSim* s = (ParticleSim*)ctx;
    const ParticleFrame* f = s->m_out;
    const float dt = kFrameDt;
    const float t = f->time;
    static const float palette[4][3] = {
        {1.0f, 0.25f, 0.7f}, {0.2f, 0.7f, 1.0f}, {1.0f, 0.55f, 0.15f}, {0.6f, 0.3f, 1.0f}};
    u32 begin = chunk * s->m_chunkSize;
    u32 end = tmin(begin + s->m_chunkSize, s->m_count);
    ParticleInstance* dst = s->m_dst;
    for (u32 i = begin; i < end; ++i) {
        V3 p = v3(s->m_px[i], s->m_py[i], s->m_pz[i]);
        V3 v = v3(s->m_vx[i], s->m_vy[i], s->m_vz[i]);
        V3 a = v3(0.0f, 0.0f, 0.0f);
        for (u32 k = 0; k < ParticleFrame::ATTRACTORS; ++k) {
            V3 d = f->attractor[k] - p;
            float d2 = dot(d, d) + kAttractorSoft;
            float inv = kAttractorGM / (d2 * dm::sqrt(d2));
            a = a + d * inv;
        }
        float rxz = dm::sqrt(p.x * p.x + p.z * p.z);
        float sw = kSwirl / (rxz + 8.0f);
        a.x = a.x - p.z * sw;
        a.z = a.z + p.x * sw;
        a.y = a.y + kWave * dm::sin(p.x * 0.07f + t * 0.9f) * dm::cos(p.z * 0.07f - t * 0.6f);
        v = (v + a * dt) * kDrag;
        p = p + v * dt;
        s->m_px[i] = p.x;
        s->m_py[i] = p.y;
        s->m_pz[i] = p.z;
        s->m_vx[i] = v.x;
        s->m_vy[i] = v.y;
        s->m_vz[i] = v.z;
        float life = s->m_life[i] - dt;
        s->m_life[i] = life;
        if (life <= 0.0f || dot(p, p) > 40000.0f) {
            s->spawn(i);
            p = v3(s->m_px[i], s->m_py[i], s->m_pz[i]);
            v = v3(s->m_vx[i], s->m_vy[i], s->m_vz[i]);
            life = s->m_life[i];
        }
        float maxLife = s->m_maxLife[i];
        float age = 1.0f - life / maxLife;
        float speed = length(v);
        float heat = dm::clampf(speed * (1.0f / 28.0f), 0.0f, 1.0f);
        heat = heat * heat;
        const float* pc = palette[i & 3];
        float fade = dm::minf(1.0f, life * 2.0f) * dm::minf(1.0f, (maxLife - life) * 4.0f);
        ParticleInstance inst;
        inst.px = p.x;
        inst.py = p.y;
        inst.pz = p.z;
        inst.size = 0.6f + 1.8f * age;
        inst.color = pack_rgba8(dm::lerp(pc[0], 0.8f, heat), dm::lerp(pc[1], 0.9f, heat), dm::lerp(pc[2], 1.0f, heat),
                                fade * 0.55f);
        dst[i] = inst;
    }
}

u64 ParticleSim::hash() const {
    Fnv64 h;
    h.floats(m_px, m_count);
    h.floats(m_py, m_count);
    h.floats(m_pz, m_count);
    h.floats(m_vx, m_count);
    h.floats(m_vy, m_count);
    h.floats(m_vz, m_count);
    h.floats(m_life, m_count);
    for (u32 i = 0; i < m_count; ++i) h.u32v(m_spawnCount[i]);
    return h.h;
}

}  // namespace tsb
