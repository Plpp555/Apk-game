// GT3 "Neon City": GPU-heavy clustered forward shading. The CPU animates 256
// moving lights (cars), bins them into a 16x8x16 cluster grid (Forward+ with
// CPU light assignment, a common mobile technique), and culls/sorts 6400
// buildings front-to-back. The GPU evaluates GGX lighting per pixel.
#pragma once

#include "sim_common.h"

namespace tsb {

struct BuildingInstance {  // 32 bytes
    float cx, cz, sx, sz;
    float height, seed, pad0, pad1;
};

struct CityFrame {
    enum { BUCKETS = 4 };
    CameraState cam;
    float time;
    u32 bucketStart[BUCKETS];
    u32 bucketCount[BUCKETS];
    u32 visibleBuildings;
};

class CitySim {
public:
    enum {
        BLOCKS = 40,
        LIGHTS = 256,
        TILES_X = 16,
        TILES_Y = 8,
        SLICES = 16,
        CLUSTERS = TILES_X * TILES_Y * SLICES,  // 2048
        MAX_PER_CLUSTER = 32,
        BUILDING_CHUNKS = 32,
        LIGHT_CHUNKS = 4,
    };
    static const float kPitch;
    static const float kNear;
    static const float kFar;

    // Destination buffers written each frame
    enum { DST_BUILDINGS = 0, DST_LIGHTS = 1, DST_CLUSTERS = 2, DST_CARS = 3, DST_COUNT = 4 };

    bool init();
    void release();
    void reset();
    u32 building_count() const { return m_buildingCount; }
    u32 dst_bytes(int which) const;

    void build(JobChain& chain, u32 frame, void* const* dst, CityFrame* out);
    u64 hash() const;

    static void camera_at(u32 frame, CameraState& cam);
    // GPU slice formula: slice = log2(viewZ) * scale + bias
    static void slice_params(float& scale, float& bias);

private:
    struct Light {
        float x, y, z, prog;
        float speed, baseY, radius, intensity;
        float r, g, b, lane;
        i32 ix, iz, dir;
        u32 rng;
        u32 flying;
    };
    struct LightRange {
        u8 valid, s0, s1, tx0, tx1, ty0, ty1, pad;
    };

    static void job_update(void* ctx, u32 chunk);    // lights + building culling
    static void job_bin(void* ctx, u32 chunk);       // prefix (chunk 0) + light binning per slice
    static void job_scatter(void* ctx, u32 chunk);   // building instance compaction
    void update_light(u32 i, float t);
    void cull_buildings(u32 chunk);
    void bin_slice(u32 slice);

    u32 m_buildingCount = 0;
    BuildingInstance* m_buildings = nullptr;
    u8* m_bucket = nullptr;
    BucketTable m_table;
    Light m_lights[LIGHTS];
    LightRange m_range[LIGHTS];
    float m_sliceZ[SLICES + 1];
    u32 m_sliceAssign[SLICES];
    Fnv64 m_roll;

    CityFrame* m_out = nullptr;
    void* m_dst[DST_COUNT] = {nullptr, nullptr, nullptr, nullptr};
};

}  // namespace tsb
