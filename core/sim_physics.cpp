#include "sim_physics.h"

namespace tsb {

const float PhysicsSim::kBoxX = 40.0f;
const float PhysicsSim::kBoxY = 48.0f;
const float PhysicsSim::kBoxZ = 40.0f;

static const float kDt = 1.0f / 240.0f;
static const float kStiffness = 6000.0f;
static const float kDamping = 30.0f;
static const float kGravity = 9.81f;
static const float kMaxSpeed = 25.0f;
static const float kRadiusMin = 0.35f;
static const float kRadiusMax = 0.55f;
static const float kStirRadius = 5.0f;

bool PhysicsSim::init(u32 count) {
    m_count = count;
    m_chunkSize = (count + CHUNKS - 1) / CHUNKS;
    for (int b = 0; b < 2; ++b) {
        m_px[b] = alloc_array<float>(count);
        m_py[b] = alloc_array<float>(count);
        m_pz[b] = alloc_array<float>(count);
        m_vx[b] = alloc_array<float>(count);
        m_vy[b] = alloc_array<float>(count);
        m_vz[b] = alloc_array<float>(count);
    }
    m_r = alloc_array<float>(count);
    m_cell = 2.0f * kRadiusMax;
    m_invCell = 1.0f / m_cell;
    m_gx = (i32)(kBoxX * m_invCell) + 1;
    m_gy = (i32)(kBoxY * m_invCell) + 1;
    m_gz = (i32)(kBoxZ * m_invCell) + 1;
    u32 cells = (u32)(m_gx * m_gy * m_gz);
    m_cellOf = alloc_array<u32>(count);
    m_slabHist = alloc_array<u32>(CHUNKS * (u32)m_gx);
    m_slabOffset = alloc_array<u32>(CHUNKS * (u32)m_gx);
    m_slabStart = alloc_array<u32>((u32)m_gx + 1);
    m_slabItems = alloc_array<u32>(count);
    m_sorted = alloc_array<u32>(count);
    m_cellStart = alloc_array<u32>(cells);
    m_cellCount = alloc_array<u16>(cells);
    reset();
    return true;
}

void PhysicsSim::release() {
    for (int b = 0; b < 2; ++b) {
        mem_free(m_px[b]);
        mem_free(m_py[b]);
        mem_free(m_pz[b]);
        mem_free(m_vx[b]);
        mem_free(m_vy[b]);
        mem_free(m_vz[b]);
        m_px[b] = m_py[b] = m_pz[b] = m_vx[b] = m_vy[b] = m_vz[b] = nullptr;
    }
    mem_free(m_r);
    mem_free(m_cellOf);
    mem_free(m_slabHist);
    mem_free(m_slabOffset);
    mem_free(m_slabStart);
    mem_free(m_slabItems);
    mem_free(m_sorted);
    mem_free(m_cellStart);
    mem_free(m_cellCount);
    m_r = nullptr;
    m_cellOf = m_slabHist = m_slabOffset = m_slabStart = m_slabItems = m_sorted = m_cellStart = nullptr;
    m_cellCount = nullptr;
    m_count = 0;
}

void PhysicsSim::reset() {
    Pcg32 rng;
    rng.seed(0xB0B5ull, 9);
    const float spacing = 1.15f;
    const u32 perRow = (u32)((kBoxX - 2.0f) / spacing);
    m_cur = 0;
    m_step = 0;
    for (u32 i = 0; i < m_count; ++i) {
        u32 ix = i % perRow;
        u32 iz = (i / perRow) % perRow;
        u32 iy = i / (perRow * perRow);
        m_px[0][i] = 1.0f + spacing * (float)ix + rng.range(-0.08f, 0.08f);
        m_pz[0][i] = 1.0f + spacing * (float)iz + rng.range(-0.08f, 0.08f);
        m_py[0][i] = 2.0f + spacing * (float)iy + rng.range(-0.08f, 0.08f);
        m_vx[0][i] = rng.range(-1.0f, 1.0f);
        m_vy[0][i] = rng.range(-1.0f, 1.0f);
        m_vz[0][i] = rng.range(-1.0f, 1.0f);
        m_r[i] = rng.range(kRadiusMin, kRadiusMax);
    }
    m_stir = stirrer_pos();
    m_stirVel = v3(0.0f, 0.0f, 0.0f);
}

float PhysicsSim::stirrer_radius() const { return kStirRadius; }

V3 PhysicsSim::stirrer_pos() const {
    float t = (float)m_step * kDt;
    float a = 1.1f * t;
    return v3(20.0f + 11.0f * dm::cos(a), 4.5f + 1.5f * dm::sin(2.3f * t), 20.0f + 11.0f * dm::sin(a));
}

void PhysicsSim::build(JobChain& chain) {
    chain.reset();
    chain.add(job_cells, this, CHUNKS);
    chain.add(job_prefix, this, 1);
    chain.add(job_scatter, this, CHUNKS);
    chain.add(job_sort, this, (u32)m_gx);
    chain.add(job_forces, this, CHUNKS);
}

void PhysicsSim::advance() {
    m_cur ^= 1;
    m_step++;
}

void PhysicsSim::job_cells(void* ctx, u32 chunk) {
    PhysicsSim* s = (PhysicsSim*)ctx;
    const float* px = s->m_px[s->m_cur];
    const float* py = s->m_py[s->m_cur];
    const float* pz = s->m_pz[s->m_cur];
    u32* hist = s->m_slabHist + chunk * (u32)s->m_gx;
    for (i32 k = 0; k < s->m_gx; ++k) hist[k] = 0;
    u32 begin = chunk * s->m_chunkSize;
    u32 end = tmin(begin + s->m_chunkSize, s->m_count);
    for (u32 i = begin; i < end; ++i) {
        i32 cx = tclamp(dm::floori(px[i] * s->m_invCell), 0, s->m_gx - 1);
        i32 cy = tclamp(dm::floori(py[i] * s->m_invCell), 0, s->m_gy - 1);
        i32 cz = tclamp(dm::floori(pz[i] * s->m_invCell), 0, s->m_gz - 1);
        s->m_cellOf[i] = (u32)((cx * s->m_gy + cy) * s->m_gz + cz);
        hist[cx]++;
    }
}

void PhysicsSim::job_prefix(void* ctx, u32) {
    PhysicsSim* s = (PhysicsSim*)ctx;
    u32 run = 0;
    for (i32 sl = 0; sl < s->m_gx; ++sl) {
        s->m_slabStart[sl] = run;
        for (u32 c = 0; c < CHUNKS; ++c) {
            s->m_slabOffset[c * (u32)s->m_gx + (u32)sl] = run;
            run += s->m_slabHist[c * (u32)s->m_gx + (u32)sl];
        }
    }
    s->m_slabStart[s->m_gx] = run;
    // Moving obstacle for this step (analytic, finite-difference velocity)
    V3 now = s->stirrer_pos();
    s->m_stirVel = (now - s->m_stir) * (1.0f / kDt);
    if (s->m_step == 0) s->m_stirVel = v3(0.0f, 0.0f, 0.0f);
    s->m_stir = now;
}

void PhysicsSim::job_scatter(void* ctx, u32 chunk) {
    PhysicsSim* s = (PhysicsSim*)ctx;
    u32 offs[128];
    u32 gx = (u32)s->m_gx;
    for (u32 k = 0; k < gx; ++k) offs[k] = s->m_slabOffset[chunk * gx + k];
    u32 slabSize = (u32)(s->m_gy * s->m_gz);
    u32 begin = chunk * s->m_chunkSize;
    u32 end = tmin(begin + s->m_chunkSize, s->m_count);
    for (u32 i = begin; i < end; ++i) {
        u32 slab = s->m_cellOf[i] / slabSize;
        s->m_slabItems[offs[slab]++] = i;
    }
}

void PhysicsSim::job_sort(void* ctx, u32 slab) {
    PhysicsSim* s = (PhysicsSim*)ctx;
    u32 slabSize = (u32)(s->m_gy * s->m_gz);
    u32 cellBase = slab * slabSize;
    u32 a = s->m_slabStart[slab], b = s->m_slabStart[slab + 1];
    u32* cstart = s->m_cellStart + cellBase;
    u16* ccount = s->m_cellCount + cellBase;
    for (u32 c = 0; c < slabSize; ++c) ccount[c] = 0;
    for (u32 k = a; k < b; ++k) ccount[s->m_cellOf[s->m_slabItems[k]] - cellBase]++;
    u32 run = a;
    for (u32 c = 0; c < slabSize; ++c) {
        cstart[c] = run;
        run += ccount[c];
    }
    // Stable scatter: re-use cstart as a cursor, then restore it.
    for (u32 k = a; k < b; ++k) {
        u32 id = s->m_slabItems[k];
        u32 lc = s->m_cellOf[id] - cellBase;
        s->m_sorted[cstart[lc]++] = id;
    }
    for (u32 c = 0; c < slabSize; ++c) cstart[c] -= ccount[c];
}

void PhysicsSim::job_forces(void* ctx, u32 chunk) {
    PhysicsSim* s = (PhysicsSim*)ctx;
    const int cur = s->m_cur, nxt = cur ^ 1;
    const float* px = s->m_px[cur];
    const float* py = s->m_py[cur];
    const float* pz = s->m_pz[cur];
    const float* vx = s->m_vx[cur];
    const float* vy = s->m_vy[cur];
    const float* vz = s->m_vz[cur];
    const float* rad = s->m_r;
    const i32 gx = s->m_gx, gy = s->m_gy, gz = s->m_gz;
    const V3 stir = s->m_stir, stirVel = s->m_stirVel;
    u32 begin = chunk * s->m_chunkSize;
    u32 end = tmin(begin + s->m_chunkSize, s->m_count);
    for (u32 i = begin; i < end; ++i) {
        V3 p = v3(px[i], py[i], pz[i]);
        V3 v = v3(vx[i], vy[i], vz[i]);
        float ri = rad[i];
        V3 f = v3(0.0f, -kGravity, 0.0f);
        u32 cell = s->m_cellOf[i];
        i32 cz = (i32)(cell % (u32)gz);
        i32 cy = (i32)((cell / (u32)gz) % (u32)gy);
        i32 cx = (i32)(cell / (u32)(gz * gy));
        for (i32 dx = -1; dx <= 1; ++dx) {
            i32 nx = cx + dx;
            if (nx < 0 || nx >= gx) continue;
            for (i32 dy = -1; dy <= 1; ++dy) {
                i32 ny = cy + dy;
                if (ny < 0 || ny >= gy) continue;
                for (i32 dz = -1; dz <= 1; ++dz) {
                    i32 nz = cz + dz;
                    if (nz < 0 || nz >= gz) continue;
                    u32 nc = (u32)((nx * gy + ny) * gz + nz);
                    u32 k0 = s->m_cellStart[nc], k1 = k0 + s->m_cellCount[nc];
                    for (u32 k = k0; k < k1; ++k) {
                        u32 j = s->m_sorted[k];
                        if (j == i) continue;
                        V3 d = v3(px[j] - p.x, py[j] - p.y, pz[j] - p.z);
                        float rs = ri + rad[j];
                        float d2 = dot(d, d);
                        if (d2 >= rs * rs || d2 < 1.0e-12f) continue;
                        float dist = dm::sqrt(d2);
                        V3 n = d * (1.0f / dist);
                        float overlap = rs - dist;
                        float vn = (vx[j] - v.x) * n.x + (vy[j] - v.y) * n.y + (vz[j] - v.z) * n.z;
                        f = f + n * (-kStiffness * overlap + kDamping * vn);
                    }
                }
            }
        }
        // Walls (inward normals)
        float ov;
        ov = ri - p.x;
        if (ov > 0.0f) f.x = f.x + kStiffness * ov - kDamping * v.x;
        ov = p.x + ri - kBoxX;
        if (ov > 0.0f) f.x = f.x - kStiffness * ov - kDamping * v.x;
        ov = ri - p.y;
        if (ov > 0.0f) f.y = f.y + kStiffness * ov - kDamping * v.y;
        ov = p.y + ri - kBoxY;
        if (ov > 0.0f) f.y = f.y - kStiffness * ov - kDamping * v.y;
        ov = ri - p.z;
        if (ov > 0.0f) f.z = f.z + kStiffness * ov - kDamping * v.z;
        ov = p.z + ri - kBoxZ;
        if (ov > 0.0f) f.z = f.z - kStiffness * ov - kDamping * v.z;
        // Moving stirrer sphere
        V3 ds = p - stir;
        float dd2 = dot(ds, ds);
        float rr = kStirRadius + ri;
        if (dd2 < rr * rr && dd2 > 1.0e-12f) {
            float dd = dm::sqrt(dd2);
            V3 n = ds * (1.0f / dd);
            float vn = dot(v - stirVel, n);
            f = f + n * (kStiffness * (rr - dd) - kDamping * vn);
        }
        // Integrate (semi-implicit Euler, unit mass)
        v = v + f * kDt;
        float sp2 = dot(v, v);
        if (sp2 > kMaxSpeed * kMaxSpeed) v = v * (kMaxSpeed / dm::sqrt(sp2));
        p = p + v * kDt;
        s->m_px[nxt][i] = p.x;
        s->m_py[nxt][i] = p.y;
        s->m_pz[nxt][i] = p.z;
        s->m_vx[nxt][i] = v.x;
        s->m_vy[nxt][i] = v.y;
        s->m_vz[nxt][i] = v.z;
    }
}

u64 PhysicsSim::hash() const {
    Fnv64 h;
    h.floats(m_px[m_cur], m_count);
    h.floats(m_py[m_cur], m_count);
    h.floats(m_pz[m_cur], m_count);
    h.floats(m_vx[m_cur], m_count);
    h.floats(m_vy[m_cur], m_count);
    h.floats(m_vz[m_cur], m_count);
    h.u32v(m_step);
    return h.h;
}

}  // namespace tsb
