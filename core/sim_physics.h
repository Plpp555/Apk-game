// CPU test "Physics": discrete-element sphere simulation with a uniform grid.
// Every step is a 5-phase job chain:
//   1. cell index + slab histogram (parallel)   2. prefix sums (serial)
//   3. scatter into slabs (parallel)            4. per-slab counting sort (parallel)
//   5. contact forces + integration (parallel, Jacobi: reads step n, writes n+1)
// No atomics and fixed chunking, so single-thread and multi-thread runs must
// produce bit-identical states. The benchmark verifies this on every run.
#pragma once

#include "sim_common.h"

namespace tsb {

class PhysicsSim {
public:
    enum { CHUNKS = 64 };

    bool init(u32 count);
    void release();
    void reset();
    u32 count() const { return m_count; }
    u32 step_index() const { return m_step; }

    // Build the chain for one step. Call advance() after the chain completed.
    void build(JobChain& chain);
    void advance();
    u64 hash() const;

    // Current state access (for the preview renderer)
    const float* pos_x() const { return m_px[m_cur]; }
    const float* pos_y() const { return m_py[m_cur]; }
    const float* pos_z() const { return m_pz[m_cur]; }
    const float* radius() const { return m_r; }
    V3 stirrer_pos() const;
    float stirrer_radius() const;
    static const float kBoxX, kBoxY, kBoxZ;

private:
    static void job_cells(void* ctx, u32 chunk);
    static void job_prefix(void* ctx, u32 chunk);
    static void job_scatter(void* ctx, u32 chunk);
    static void job_sort(void* ctx, u32 slab);
    static void job_forces(void* ctx, u32 chunk);

    u32 m_count = 0;
    u32 m_chunkSize = 0;
    u32 m_step = 0;
    int m_cur = 0;
    float* m_px[2] = {nullptr, nullptr};
    float* m_py[2] = {nullptr, nullptr};
    float* m_pz[2] = {nullptr, nullptr};
    float* m_vx[2] = {nullptr, nullptr};
    float* m_vy[2] = {nullptr, nullptr};
    float* m_vz[2] = {nullptr, nullptr};
    float* m_r = nullptr;

    // Grid
    i32 m_gx = 0, m_gy = 0, m_gz = 0;
    float m_cell = 1.0f, m_invCell = 1.0f;
    u32* m_cellOf = nullptr;      // per sphere: global cell id
    u32* m_slabHist = nullptr;    // [chunk * gx + slab]
    u32* m_slabOffset = nullptr;  // [chunk * gx + slab]
    u32* m_slabStart = nullptr;   // [gx + 1]
    u32* m_slabItems = nullptr;   // sphere ids grouped by slab
    u32* m_sorted = nullptr;      // sphere ids sorted by cell
    u32* m_cellStart = nullptr;   // per cell
    u16* m_cellCount = nullptr;   // per cell

    V3 m_stir;
    V3 m_stirVel;
};

}  // namespace tsb
