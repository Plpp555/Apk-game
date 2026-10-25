// GT1 "Asteroid Belt": N-body-lite orbital simulation + frustum culling + LOD
// selection on the CPU; instanced rendering with per-pixel shading on the GPU.
#pragma once

#include "sim_common.h"

namespace tsb {

struct AsteroidInstance {  // 36 bytes, read directly by the vertex shader
    float px, py, pz, scale;
    float qx, qy, qz, qw;
    u32 color;  // RGBA8, alpha = per-object seed
};

struct AsteroidFrame {
    enum { SHAPES = 4, LODS = 3, BUCKETS = SHAPES * LODS };
    CameraState cam;
    float time;
    V3 moonPos;
    u32 bucketStart[BUCKETS];
    u32 bucketCount[BUCKETS];
    u32 visible;
};

class AsteroidSim {
public:
    enum { CHUNKS = 64, SHAPES = AsteroidFrame::SHAPES, LODS = AsteroidFrame::LODS, BUCKETS = AsteroidFrame::BUCKETS };
    static const float kMoonScale;

    bool init(u32 count);
    void release();
    void reset();
    u32 count() const { return m_count; }
    // Instances: count (visible, bucketed) + 1 (moon, at index count).
    u32 instance_bytes() const { return (m_count + 1) * (u32)sizeof(AsteroidInstance); }

    // Fill `chain` with the work that advances the world to frame `frame`
    // and writes visible instances to dst. Results land in *out.
    void build(JobChain& chain, u32 frame, void* dst, AsteroidFrame* out);
    u64 hash() const;

    static void camera_at(u32 frame, CameraState& cam);
    static V3 moon_pos(float t);

private:
    static void job_simulate(void* ctx, u32 chunk);
    static void job_prefix(void* ctx, u32 chunk);
    static void job_write(void* ctx, u32 chunk);

    u32 m_count = 0;
    u32 m_chunkSize = 0;
    float *m_px = nullptr, *m_py = nullptr, *m_pz = nullptr, *m_vx = nullptr, *m_vy = nullptr, *m_vz = nullptr;
    float *m_qx = nullptr, *m_qy = nullptr, *m_qz = nullptr, *m_qw = nullptr;
    float *m_wx = nullptr, *m_wy = nullptr, *m_wz = nullptr, *m_scale = nullptr;
    u32* m_color = nullptr;
    u8* m_bucket = nullptr;
    BucketTable m_table;
    Fnv64 m_visHash;

    // Per-build context
    AsteroidFrame* m_out = nullptr;
    AsteroidInstance* m_dst = nullptr;
};

}  // namespace tsb
