// GT2 "Nebula Storm": 131k particles integrated on the CPU (attractors, swirl
// field, life cycle) and streamed to the GPU every frame (~2.6 MB/frame) for
// additive, fill-rate heavy billboard rendering + bloom.
#pragma once

#include "sim_common.h"

namespace tsb {

struct ParticleInstance {  // 20 bytes
    float px, py, pz, size;
    u32 color;  // RGBA8 (alpha = brightness)
};

struct ParticleFrame {
    enum { ATTRACTORS = 4 };
    CameraState cam;
    float time;
    V3 attractor[ATTRACTORS];
};

class ParticleSim {
public:
    enum { CHUNKS = 64 };

    bool init(u32 count);
    void release();
    void reset();
    u32 count() const { return m_count; }
    u32 instance_bytes() const { return m_count * (u32)sizeof(ParticleInstance); }

    void build(JobChain& chain, u32 frame, void* dst, ParticleFrame* out);
    u64 hash() const;

    static void camera_at(u32 frame, CameraState& cam);

private:
    static void job_simulate(void* ctx, u32 chunk);
    void spawn(u32 i);

    u32 m_count = 0;
    u32 m_chunkSize = 0;
    float *m_px = nullptr, *m_py = nullptr, *m_pz = nullptr;
    float *m_vx = nullptr, *m_vy = nullptr, *m_vz = nullptr;
    float *m_life = nullptr, *m_maxLife = nullptr;
    u32* m_spawnCount = nullptr;

    ParticleFrame* m_out = nullptr;
    ParticleInstance* m_dst = nullptr;
};

}  // namespace tsb
