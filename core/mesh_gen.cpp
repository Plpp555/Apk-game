#include "mesh_gen.h"

#include "rng.h"

namespace tsb {

namespace {

// Edge -> midpoint index map (open addressing).
struct EdgeMap {
    u32* keys;
    u16* vals;
    u32 mask;
    explicit EdgeMap(u32 capacityPow2) {
        keys = alloc_array<u32>(capacityPow2);
        vals = alloc_array<u16>(capacityPow2);
        for (u32 i = 0; i < capacityPow2; ++i) keys[i] = 0xffffffffu;
        mask = capacityPow2 - 1;
    }
    ~EdgeMap() {
        mem_free(keys);
        mem_free(vals);
    }
    bool find(u32 key, u16& out) const {
        u32 h = hash32(key) & mask;
        while (keys[h] != 0xffffffffu) {
            if (keys[h] == key) {
                out = vals[h];
                return true;
            }
            h = (h + 1) & mask;
        }
        return false;
    }
    void insert(u32 key, u16 v) {
        u32 h = hash32(key) & mask;
        while (keys[h] != 0xffffffffu) h = (h + 1) & mask;
        keys[h] = key;
        vals[h] = v;
    }
};

void icosphere_dirs(Array<V3>& dirs, Array<u16>& tris, int subdiv) {
    const float t = 1.6180339887498948482f;
    const float base[12][3] = {{-1, t, 0}, {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                               {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
    const u16 faces[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                              {11, 10, 2}, {10, 7, 6}, {7, 1, 8},   {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                              {3, 8, 9},  {4, 9, 5},  {2, 4, 11},  {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
    dirs.clear();
    tris.clear();
    for (int i = 0; i < 12; ++i) dirs.push(normalize(v3(base[i][0], base[i][1], base[i][2])));
    for (int i = 0; i < 20; ++i)
        for (int k = 0; k < 3; ++k) tris.push(faces[i][k]);

    for (int s = 0; s < subdiv; ++s) {
        Array<u16> next;
        next.reserve(tris.count * 4);
        EdgeMap map(8192);
        u32 triCount = tris.count / 3;
        for (u32 f = 0; f < triCount; ++f) {
            u16 v[3] = {tris[f * 3 + 0], tris[f * 3 + 1], tris[f * 3 + 2]};
            u16 m[3];
            for (int e = 0; e < 3; ++e) {
                u16 a = v[e], b = v[(e + 1) % 3];
                u32 key = a < b ? ((u32)a << 16) | b : ((u32)b << 16) | a;
                if (!map.find(key, m[e])) {
                    m[e] = (u16)dirs.count;
                    dirs.push(normalize((dirs[a] + dirs[b]) * 0.5f));
                    map.insert(key, m[e]);
                }
            }
            const u16 nt[12] = {v[0], m[0], m[2], v[1], m[1], m[0], v[2], m[2], m[1], m[0], m[1], m[2]};
            for (int k = 0; k < 12; ++k) next.push(nt[k]);
        }
        tris.clear();
        for (u32 i = 0; i < next.count; ++i) tris.push(next[i]);
    }
}

void build_smooth(MeshData& out, const Array<V3>& pos, const Array<u16>& tris) {
    Array<V3> nrm;
    nrm.resize(pos.count);
    for (u32 f = 0; f + 2 < tris.count; f += 3) {
        V3 a = pos[tris[f]], b = pos[tris[f + 1]], c = pos[tris[f + 2]];
        V3 n = cross(b - a, c - a);  // area weighted
        nrm[tris[f]] = nrm[tris[f]] + n;
        nrm[tris[f + 1]] = nrm[tris[f + 1]] + n;
        nrm[tris[f + 2]] = nrm[tris[f + 2]] + n;
    }
    out.verts.clear();
    out.indices.clear();
    for (u32 i = 0; i < pos.count; ++i) out.add_vertex(pos[i], normalize(nrm[i]));
    for (u32 i = 0; i < tris.count; ++i) out.indices.push(tris[i]);
}

}  // namespace

void mesh_icosphere(MeshData& out, int subdiv) {
    Array<V3> dirs;
    Array<u16> tris;
    icosphere_dirs(dirs, tris, subdiv);
    out.verts.clear();
    out.indices.clear();
    for (u32 i = 0; i < dirs.count; ++i) out.add_vertex(dirs[i], dirs[i]);
    for (u32 i = 0; i < tris.count; ++i) out.indices.push(tris[i]);
}

float asteroid_radius(V3 dir, u32 seed) {
    // Low-frequency lumps
    float r = 1.0f;
    float amp = 0.28f, freq = 1.3f;
    for (int o = 0; o < 4; ++o) {
        r = r + amp * value_noise3(dir * freq + v3(3.1f, 1.7f, 4.9f) * (float)o, seed + (u32)o * 977u);
        amp = amp * 0.5f;
        freq = freq * 2.1f;
    }
    // Craters: spherical-cap dents with a raised rim
    Pcg32 rng;
    rng.seed(seed, 0x0c4a7e45u);
    for (int c = 0; c < 6; ++c) {
        V3 cd = normalize(rand_v3(rng, -1.0f, 1.0f));
        float size = rng.range(0.25f, 0.55f);
        float d = 1.0f - dot(dir, cd);  // 0 at crater centre
        float x = d / (size * size);
        if (x < 1.0f) {
            float bowl = (1.0f - x) * (1.0f - x);
            r = r - 0.12f * bowl;
        } else if (x < 1.6f) {
            float rim = (x - 1.0f) / 0.6f;
            r = r + 0.03f * (1.0f - rim) * rim * 4.0f;
        }
    }
    return dm::clampf(r, 0.55f, 1.35f);
}

void mesh_asteroid(MeshData& out, int subdiv, u32 seed) {
    Array<V3> dirs;
    Array<u16> tris;
    icosphere_dirs(dirs, tris, subdiv);
    // Squash/stretch so shapes are not all round
    Pcg32 rng;
    rng.seed(seed, 0x51a9e0b3u);
    V3 stretch = rand_v3(rng, v3(0.75f, 0.65f, 0.8f), v3(1.25f, 1.0f, 1.3f));
    Array<V3> pos;
    pos.resize(dirs.count);
    for (u32 i = 0; i < dirs.count; ++i) pos[i] = dirs[i] * asteroid_radius(dirs[i], seed) * stretch;
    build_smooth(out, pos, tris);
}

void mesh_box(MeshData& out) {
    out.verts.clear();
    out.indices.clear();
    struct Face {
        V3 n, u, v;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (int f = 0; f < 6; ++f) {
        const Face& fc = faces[f];
        u16 base = (u16)out.vertex_count();
        for (int k = 0; k < 4; ++k) {
            float su = (k == 1 || k == 2) ? 0.5f : -0.5f;
            float sv = (k >= 2) ? 0.5f : -0.5f;
            V3 p = fc.n * 0.5f + fc.u * su + fc.v * sv;
            p.y = p.y + 0.5f;  // y in [0, 1]
            out.add_vertex(p, fc.n);
        }
        const u16 idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; ++k) out.indices.push((u16)(base + idx[k]));
    }
}

void mesh_plane(MeshData& out, float h) {
    out.verts.clear();
    out.indices.clear();
    V3 n = v3(0, 1, 0);
    out.add_vertex(v3(-h, 0, -h), n);
    out.add_vertex(v3(-h, 0, h), n);
    out.add_vertex(v3(h, 0, h), n);
    out.add_vertex(v3(h, 0, -h), n);
    const u16 idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; ++k) out.indices.push(idx[k]);
}

}  // namespace tsb
