#include "sim_city.h"

#include "sim_particles.h"  // ParticleInstance layout reused for car spheres

namespace tsb {

const float CitySim::kPitch = 14.0f;
const float CitySim::kNear = 0.5f;
const float CitySim::kFar = 520.0f;

static const i32 kDirX[4] = {1, 0, -1, 0};
static const i32 kDirZ[4] = {0, 1, 0, -1};

static inline u32 next_rand(u32& state) {
    state = hash32(state + 0x9E3779B9u);
    return state;
}

u32 CitySim::dst_bytes(int which) const {
    switch (which) {
        case DST_BUILDINGS: return m_buildingCount * (u32)sizeof(BuildingInstance);
        case DST_LIGHTS: return LIGHTS * 32;  // std140: vec4 posRad[256] + vec4 color[256]
        case DST_CLUSTERS: return CLUSTERS * (MAX_PER_CLUSTER + 1);
        case DST_CARS: return LIGHTS * (u32)sizeof(ParticleInstance);
    }
    return 0;
}

bool CitySim::init() {
    m_buildingCount = BLOCKS * BLOCKS * 4;
    m_buildings = alloc_array<BuildingInstance>(m_buildingCount);
    m_bucket = alloc_array<u8>(m_buildingCount);
    m_table.init(BUILDING_CHUNKS, CityFrame::BUCKETS);
    // Exponential depth slices, identical to the GPU formula
    float ratio = kFar / kNear;
    float log2Ratio = 0.0f;
    {
        // log2(ratio) via bisection on exp2 (deterministic, init-time only)
        float lo = 0.0f, hi = 32.0f;
        for (int it = 0; it < 60; ++it) {
            float mid = 0.5f * (lo + hi);
            if (dm::exp2(mid) < ratio) lo = mid;
            else hi = mid;
        }
        log2Ratio = 0.5f * (lo + hi);
    }
    for (u32 s = 0; s <= SLICES; ++s) m_sliceZ[s] = kNear * dm::exp2(log2Ratio * (float)s / (float)SLICES);

    // Static city layout
    Pcg32 rng;
    rng.seed(0xC17E5EEDull, 3);
    u32 n = 0;
    for (u32 bz = 0; bz < BLOCKS; ++bz) {
        for (u32 bx = 0; bx < BLOCKS; ++bx) {
            for (u32 lot = 0; lot < 4; ++lot) {
                BuildingInstance& b = m_buildings[n++];
                float ox = (float)bx * kPitch + 2.5f + 2.25f + (float)(lot & 1) * 4.5f;
                float oz = (float)bz * kPitch + 2.5f + 2.25f + (float)(lot >> 1) * 4.5f;
                float jx = rng.range(0.0f, 0.8f), jz = rng.range(0.0f, 0.8f);
                b.cx = ox;
                b.cz = oz;
                b.sx = 3.8f - jx;
                b.sz = 3.8f - jz;
                float dx = ox - 280.0f, dz = oz - 280.0f;
                float dn = dm::clampf(dm::sqrt(dx * dx + dz * dz) / 300.0f, 0.0f, 1.0f);
                float u = rng.unit();
                float h = 5.0f + 52.0f * (1.0f - dn) * (1.0f - dn) * (0.3f + 0.7f * u * dm::sqrt(u)) + rng.range(0.0f, 4.0f);
                if (rng.unit() < 0.08f) h = 0.0f;  // plazas
                b.height = h;
                b.seed = rng.unit();
                b.pad0 = b.pad1 = 0.0f;
            }
        }
    }
    reset();
    return true;
}

void CitySim::release() {
    mem_free(m_buildings);
    mem_free(m_bucket);
    m_buildings = nullptr;
    m_bucket = nullptr;
    m_table.release();
}

void CitySim::reset() {
    static const float palette[6][3] = {{1.0f, 0.85f, 0.6f}, {1.0f, 0.15f, 0.1f}, {0.1f, 0.9f, 1.0f},
                                        {1.0f, 0.2f, 0.85f}, {1.0f, 0.6f, 0.1f}, {0.3f, 0.4f, 1.0f}};
    Pcg32 rng;
    rng.seed(0xCA25ull, 5);
    for (u32 i = 0; i < LIGHTS; ++i) {
        Light& L = m_lights[i];
        L.ix = (i32)rng.below(BLOCKS + 1);
        L.iz = (i32)rng.below(BLOCKS + 1);
        L.dir = (i32)rng.below(4);
        // Never start pointing out of the city
        i32 nx = L.ix + kDirX[L.dir], nz = L.iz + kDirZ[L.dir];
        if (nx < 0 || nx > BLOCKS || nz < 0 || nz > BLOCKS) L.dir = (L.dir + 2) & 3;
        L.prog = rng.unit() * kPitch;
        L.flying = rng.unit() < 0.4f ? 1u : 0u;
        L.speed = L.flying ? rng.range(12.0f, 24.0f) : rng.range(7.0f, 13.0f);
        L.baseY = L.flying ? rng.range(7.0f, 32.0f) : 0.7f;
        L.radius = L.flying ? 20.0f : 14.0f;
        L.intensity = rng.range(10.0f, 18.0f);
        const float* c = palette[rng.below(6)];
        L.r = c[0];
        L.g = c[1];
        L.b = c[2];
        L.lane = rng.unit() < 0.5f ? 1.2f : -1.2f;
        L.rng = rng.next();
        L.x = L.y = L.z = 0.0f;
    }
    m_roll = Fnv64();
}

void CitySim::slice_params(float& scale, float& bias) {
    // slice = SLICES * log2(z / near) / log2(far / near)
    // Computed once on the host; precision differences only matter exactly on slice borders.
    float l2far = 0.0f, l2near = 0.0f;
    float lo = -8.0f, hi = 16.0f;
    for (int it = 0; it < 60; ++it) {
        float mid = 0.5f * (lo + hi);
        if (dm::exp2(mid) < kFar) lo = mid;
        else hi = mid;
    }
    l2far = 0.5f * (lo + hi);
    lo = -8.0f;
    hi = 16.0f;
    for (int it = 0; it < 60; ++it) {
        float mid = 0.5f * (lo + hi);
        if (dm::exp2(mid) < kNear) lo = mid;
        else hi = mid;
    }
    l2near = 0.5f * (lo + hi);
    scale = (float)SLICES / (l2far - l2near);
    bias = -l2near * scale;
}

void CitySim::camera_at(u32 frame, CameraState& cam) {
    static const float pts[13][3] = {{140, 18, 70},  {140, 14, 200}, {140, 22, 330}, {150, 72, 410}, {250, 16, 420},
                                     {360, 26, 420}, {412, 74, 400}, {420, 16, 300}, {420, 24, 190}, {405, 72, 145},
                                     {300, 15, 140}, {190, 22, 140}, {145, 70, 115}};
    const i32 n = 13;
    float t = frame_time(frame);
    V3 res[2];
    for (int k = 0; k < 2; ++k) {
        float u = t * 0.33f + (k ? 0.35f : 0.0f);
        i32 seg = dm::floori(u);
        float lt = u - (float)seg;
        i32 i1 = seg % n;
        i32 i0 = (i1 + n - 1) % n, i2 = (i1 + 1) % n, i3 = (i1 + 2) % n;
        V3 p0 = v3(pts[i0][0], pts[i0][1], pts[i0][2]);
        V3 p1 = v3(pts[i1][0], pts[i1][1], pts[i1][2]);
        V3 p2 = v3(pts[i2][0], pts[i2][1], pts[i2][2]);
        V3 p3 = v3(pts[i3][0], pts[i3][1], pts[i3][2]);
        res[k] = catmull_rom(p0, p1, p2, p3, lt);
    }
    V3 target = res[1] - v3(0.0f, 6.0f, 0.0f);
    camera_build(cam, res[0], target, 65.0f * (dm::PI / 180.0f), kNear, kFar);
}

void CitySim::build(JobChain& chain, u32 frame, void* const* dst, CityFrame* out) {
    m_out = out;
    for (int i = 0; i < DST_COUNT; ++i) m_dst[i] = dst[i];
    out->time = frame_time(frame);
    camera_at(frame, out->cam);
    chain.reset();
    chain.add(job_update, this, LIGHT_CHUNKS + BUILDING_CHUNKS);
    chain.add(job_bin, this, 1 + SLICES);
    chain.add(job_scatter, this, BUILDING_CHUNKS);
}

void CitySim::update_light(u32 i, float t) {
    Light& L = m_lights[i];
    L.prog = L.prog + L.speed * kFrameDt;
    while (L.prog >= kPitch) {
        L.prog = L.prog - kPitch;
        L.ix += kDirX[L.dir];
        L.iz += kDirZ[L.dir];
        u32 r = next_rand(L.rng) % 10u;
        i32 want = r < 6u ? L.dir : (r < 8u ? (L.dir + 1) & 3 : (L.dir + 3) & 3);
        const i32 order[4] = {want, (want + 1) & 3, (want + 3) & 3, (want + 2) & 3};
        for (int k = 0; k < 4; ++k) {
            i32 nx = L.ix + kDirX[order[k]], nz = L.iz + kDirZ[order[k]];
            if (nx >= 0 && nx <= BLOCKS && nz >= 0 && nz <= BLOCKS) {
                L.dir = order[k];
                break;
            }
        }
    }
    float dx = (float)kDirX[L.dir], dz = (float)kDirZ[L.dir];
    L.x = (float)L.ix * kPitch + dx * L.prog - dz * L.lane;
    L.z = (float)L.iz * kPitch + dz * L.prog + dx * L.lane;
    L.y = L.flying ? L.baseY + 1.5f * dm::sin(t * 0.9f + (float)i) : L.baseY;

    // Upload: std140 vec4 posRad[256]; vec4 color[256]
    float* ubo = (float*)m_dst[DST_LIGHTS];
    ubo[i * 4 + 0] = L.x;
    ubo[i * 4 + 1] = L.y;
    ubo[i * 4 + 2] = L.z;
    ubo[i * 4 + 3] = L.radius;
    ubo[LIGHTS * 4 + i * 4 + 0] = L.r * L.intensity;
    ubo[LIGHTS * 4 + i * 4 + 1] = L.g * L.intensity;
    ubo[LIGHTS * 4 + i * 4 + 2] = L.b * L.intensity;
    ubo[LIGHTS * 4 + i * 4 + 3] = 0.0f;

    // Conservative cluster range (view-space AABB of the light sphere)
    const CameraState& cam = m_out->cam;
    LightRange& rg = m_range[i];
    rg.valid = 0;
    V4 cv = m4_mul_v4(cam.view, v4(L.x, L.y, L.z, 1.0f));
    float zc = -cv.z;
    float zmin = zc - L.radius, zmax = zc + L.radius;
    if (zmax <= kNear || zmin >= kFar) return;
    u32 s0 = 0, s1 = SLICES - 1;
    while (s0 < SLICES - 1 && m_sliceZ[s0 + 1] <= zmin) s0++;
    while (s1 > 0 && m_sliceZ[s1] >= zmax) s1--;
    if (s1 < s0) return;
    i32 tx0 = 0, tx1 = TILES_X - 1, ty0 = 0, ty1 = TILES_Y - 1;
    if (zmin > kNear) {
        float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
        for (int c = 0; c < 8; ++c) {
            float x = cv.x + ((c & 1) ? L.radius : -L.radius);
            float y = cv.y + ((c & 2) ? L.radius : -L.radius);
            float z = cv.z + ((c & 4) ? L.radius : -L.radius);
            float w = -z;
            float nx = cam.proj.m[0] * x / w;
            float ny = cam.proj.m[5] * y / w;
            minX = dm::minf(minX, nx);
            maxX = dm::maxf(maxX, nx);
            minY = dm::minf(minY, ny);
            maxY = dm::maxf(maxY, ny);
        }
        if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) return;
        tx0 = tclamp(dm::floori((minX * 0.5f + 0.5f) * (float)TILES_X), 0, TILES_X - 1);
        tx1 = tclamp(dm::floori((maxX * 0.5f + 0.5f) * (float)TILES_X), 0, TILES_X - 1);
        ty0 = tclamp(dm::floori((minY * 0.5f + 0.5f) * (float)TILES_Y), 0, TILES_Y - 1);
        ty1 = tclamp(dm::floori((maxY * 0.5f + 0.5f) * (float)TILES_Y), 0, TILES_Y - 1);
    }
    rg.s0 = (u8)s0;
    rg.s1 = (u8)s1;
    rg.tx0 = (u8)tx0;
    rg.tx1 = (u8)tx1;
    rg.ty0 = (u8)ty0;
    rg.ty1 = (u8)ty1;
    rg.valid = 1;
}

void CitySim::cull_buildings(u32 chunk) {
    const CameraState& cam = m_out->cam;
    u32* counts = m_table.chunk_counts(chunk);
    for (u32 b = 0; b < CityFrame::BUCKETS; ++b) counts[b] = 0;
    u32 per = m_buildingCount / BUILDING_CHUNKS;
    u32 begin = chunk * per, end = begin + per;
    for (u32 i = begin; i < end; ++i) {
        const BuildingInstance& b = m_buildings[i];
        u8 bucket = 0xff;
        if (b.height > 0.0f) {
            V3 c = v3(b.cx, b.height * 0.5f, b.cz);
            float rad = 0.5f * dm::sqrt(b.sx * b.sx + b.height * b.height + b.sz * b.sz);
            float d = length(c - cam.pos);
            if (d - rad < 480.0f && frustum_sphere(cam.frustum, c, rad)) {
                bucket = d < 70.0f ? 0 : (d < 160.0f ? 1 : (d < 300.0f ? 2 : 3));
                counts[bucket]++;
            }
        }
        m_bucket[i] = bucket;
    }
}

void CitySim::job_update(void* ctx, u32 chunk) {
    CitySim* s = (CitySim*)ctx;
    if (chunk < LIGHT_CHUNKS) {
        u32 per = LIGHTS / LIGHT_CHUNKS;
        for (u32 i = chunk * per; i < (chunk + 1) * per; ++i) s->update_light(i, s->m_out->time);
    } else {
        s->cull_buildings(chunk - LIGHT_CHUNKS);
    }
}

void CitySim::bin_slice(u32 slice) {
    u8* tex = (u8*)m_dst[DST_CLUSTERS];
    u8 cnt[TILES_X * TILES_Y];
    memset(cnt, 0, sizeof(cnt));
    u32 base = slice * TILES_X * TILES_Y;
    u32 total = 0;
    for (u32 i = 0; i < LIGHTS; ++i) {
        const LightRange& rg = m_range[i];
        if (!rg.valid || slice < rg.s0 || slice > rg.s1) continue;
        for (u32 ty = rg.ty0; ty <= rg.ty1; ++ty) {
            for (u32 tx = rg.tx0; tx <= rg.tx1; ++tx) {
                u32 c = tx + ty * TILES_X;
                total++;
                if (cnt[c] < MAX_PER_CLUSTER) {
                    tex[(1 + cnt[c]) * CLUSTERS + base + c] = (u8)i;
                    cnt[c]++;
                }
            }
        }
    }
    memcpy(tex + base, cnt, sizeof(cnt));
    m_sliceAssign[slice] = total;
}

void CitySim::job_bin(void* ctx, u32 chunk) {
    CitySim* s = (CitySim*)ctx;
    if (chunk > 0) {
        s->bin_slice(chunk - 1);
        return;
    }
    CityFrame* f = s->m_out;
    f->visibleBuildings = s->m_table.prefix(f->bucketStart, f->bucketCount);
    // Car spheres follow the lights
    ParticleInstance* cars = (ParticleInstance*)s->m_dst[DST_CARS];
    for (u32 i = 0; i < LIGHTS; ++i) {
        const Light& L = s->m_lights[i];
        ParticleInstance& c = cars[i];
        c.px = L.x;
        c.py = L.y;
        c.pz = L.z;
        c.size = L.flying ? 0.55f : 0.4f;
        c.color = pack_rgba8(L.r, L.g, L.b, 1.0f);
    }
}

void CitySim::job_scatter(void* ctx, u32 chunk) {
    CitySim* s = (CitySim*)ctx;
    if (chunk == 0) {
        for (u32 sl = 0; sl < SLICES; ++sl) s->m_roll.u32v(s->m_sliceAssign[sl]);
        s->m_roll.u32v(s->m_out->visibleBuildings);
    }
    u32 offs[CityFrame::BUCKETS];
    for (u32 b = 0; b < CityFrame::BUCKETS; ++b) offs[b] = s->m_table.offsets[chunk * CityFrame::BUCKETS + b];
    u32 per = s->m_buildingCount / BUILDING_CHUNKS;
    BuildingInstance* dst = (BuildingInstance*)s->m_dst[DST_BUILDINGS];
    for (u32 i = chunk * per; i < (chunk + 1) * per; ++i) {
        u8 b = s->m_bucket[i];
        if (b == 0xff) continue;
        dst[offs[b]++] = s->m_buildings[i];
    }
}

u64 CitySim::hash() const {
    Fnv64 h = m_roll;
    for (u32 i = 0; i < LIGHTS; ++i) {
        const Light& L = m_lights[i];
        h.f32v(L.x);
        h.f32v(L.y);
        h.f32v(L.z);
        h.f32v(L.prog);
        h.u32v((u32)L.ix);
        h.u32v((u32)L.iz);
        h.u32v((u32)L.dir);
        h.u32v(L.rng);
    }
    return h.h;
}

}  // namespace tsb
