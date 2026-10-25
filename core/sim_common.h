// Shared definitions for the deterministic simulations (no GL in sim_* files,
// so the same code runs in the headless verification tool).
#pragma once

#include "detmath.h"
#include "jobs.h"
#include "rng.h"

namespace tsb {

// Fixed internal render resolution used for scoring on every platform.
static const int kRenderWidth = 1920;
static const int kRenderHeight = 1080;
static const float kRenderAspect = 1920.0f / 1080.0f;
// Simulation timestep: every frame advances the world by exactly 1/60 s,
// independent of how fast the device renders.
static const float kFrameDt = 1.0f / 60.0f;

inline float frame_time(u32 frame) { return (float)frame * kFrameDt; }

struct CameraState {
    V3 pos, fwd, right, up;
    M4 view, proj, viewProj;
    float fovY, aspect, nearZ, farZ;
    float tanHalfY;
    Frustum frustum;
};
void camera_build(CameraState& c, V3 eye, V3 target, float fovY, float nearZ, float farZ);

// Per-chunk bucket counting with deterministic prefix sums. Used to compact
// visible instances into contiguous per-mesh ranges without atomics.
struct BucketTable {
    enum { MAX_BUCKETS = 16 };
    u32 chunks = 0;
    u32 buckets = 0;
    u32* counts = nullptr;   // [chunk * buckets + bucket]
    u32* offsets = nullptr;  // [chunk * buckets + bucket]

    void init(u32 nChunks, u32 nBuckets) {
        chunks = nChunks;
        buckets = nBuckets;
        counts = alloc_array<u32>(nChunks * nBuckets);
        offsets = alloc_array<u32>(nChunks * nBuckets);
    }
    void release() {
        mem_free(counts);
        mem_free(offsets);
        counts = offsets = nullptr;
    }
    u32* chunk_counts(u32 c) { return counts + c * buckets; }
    // Bucket-major prefix sum; returns total. start/count must hold `buckets` entries.
    u32 prefix(u32* start, u32* count) {
        u32 run = 0;
        for (u32 b = 0; b < buckets; ++b) {
            start[b] = run;
            for (u32 c = 0; c < chunks; ++c) {
                offsets[c * buckets + b] = run;
                run += counts[c * buckets + b];
            }
            count[b] = run - start[b];
        }
        return run;
    }
};

}  // namespace tsb
