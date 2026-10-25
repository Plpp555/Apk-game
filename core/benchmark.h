// Benchmark runner: owns the job system, sequences the tests, measures, scores.
// Platform hosts (Android, Windows, Linux) create a GLES 3.0 context, then call
// tick() in a loop on the GL thread and present when tick() returns true.
#pragma once

#include "bench_spec.h"
#include "jobs.h"
#include "render_common.h"
#include "sim_physics.h"
#include "test_cpu.h"
#include "tests.h"

namespace tsb {

struct BenchOptions {
    BenchMode mode = MODE_STANDARD;
    u32 cpuCount = 1;        // logical CPUs reported by the OS
    u32 workerThreads = 0;   // 0 = cpuCount - 1 (the GL thread also helps)
    bool present = true;     // draw a preview to the window (max 20 Hz)
    u32 frameOverride = 0;   // testing only: marks the result as unofficial
    u32 testMask = 0xF;      // bit 0..2: GT1..GT3, bit 3: CPU
    const char* platformName = "unknown";
};

enum Stage {
    STAGE_IDLE = 0,
    STAGE_LOADING,
    STAGE_WARMUP,
    STAGE_RUNNING,
    STAGE_CPU_SINGLE,
    STAGE_CPU_MULTI,
    STAGE_DONE,
    STAGE_FAILED,
    STAGE_ABORTED
};

enum Verify { VERIFY_UNKNOWN = 0, VERIFY_MATCH = 1, VERIFY_MISMATCH = 2 };

struct GtResult {
    bool ran = false;
    u32 frames = 0;
    double seconds = 0, fps = 0, lowFps = 0, avgFrameMs = 0;
    double cpuBusyMs = 0;  // worker + render-thread job time per frame (all cores)
    double gpuMs = -1;     // EXT_disjoint_timer_query average, -1 if unavailable
    double simWaitMs = 0, fenceWaitMs = 0, submitMs = 0;
    double avgItems = 0;
    u64 hash = 0;
    Verify verify = VERIFY_UNKNOWN;
};

struct CpuResult {
    bool ran = false;
    u32 stSteps = 0, mtSteps = 0;
    double stSeconds = 0, mtSeconds = 0, stRate = 0, mtRate = 0, scaling = 0, utilization = 0;
    u64 hash = 0, hashSingle = 0, hashMultiAtSingle = 0;
    bool threadsConsistent = false;
    Verify verify = VERIFY_UNKNOWN;
};

class Benchmark {
public:
    enum { GT_COUNT = 3 };

    bool init(gl::GetProcFn getProc, const BenchOptions& opt);
    void shutdown();
    void start();
    void abort();
    // The output window went away (app paused, surface recreated): cancel the
    // test in progress; the next tick() re-runs it from the beginning, so its
    // timing is never polluted. Completed tests are kept.
    void interrupt();
    u32 interruptions() const { return m_interruptions; }
    // One unit of work (a frame, or a ~50 ms slice of the CPU test).
    // Returns true when a preview was drawn to framebuffer 0 and should be presented.
    bool tick(int winW, int winH);

    Stage stage() const { return m_stage; }
    int test_index() const { return m_testIndex; }
    u32 frame_index() const {
        return (m_stage == STAGE_CPU_SINGLE || m_stage == STAGE_CPU_MULTI) ? m_physStep : m_frame;
    }
    u32 frame_total() const;
    bool finished() const { return m_stage >= STAGE_DONE; }
    const gl::Caps& caps() const { return m_gpu.caps; }
    void status_json(StrBuf& sb) const;
    void result_json(StrBuf& sb) const;

    double graphics_score() const;
    double cpu_score() const;
    double overall_score() const;

private:
    u32 gt_frames() const;
    u32 gt_warmup() const;
    void kick(u32 frame);
    bool frame_step(int winW, int winH);
    bool cpu_slice(int winW, int winH);
    void finish_gt();
    void next_test();
    void fail(const char* why);
    bool official() const;

    BenchOptions m_opt;
    Gpu m_gpu;
    JobSystem m_jobs;
    Stage m_stage = STAGE_IDLE;
    int m_testIndex = 0;  // 0..2 GT, 3 CPU
    GraphicsTest* m_test = nullptr;
    int m_pendingSlot = -1;  // slot whose worker chain is dispatched but not yet finished
    u32 m_interruptions = 0;
    gl::FrameFences m_fences;
    gl::GpuTimer m_timer;

    // Graphics test measurement
    u32 m_frame = 0;
    u64 m_t0 = 0, m_lastFrameEnd = 0, m_lastPresent = 0;
    u64 m_simWaitNs = 0, m_fenceWaitNs = 0, m_submitNs = 0, m_busy0 = 0;
    u64 m_itemSum = 0;
    Array<u32> m_frameUs;
    // Live status
    u64 m_fpsWindowStart = 0;
    u32 m_fpsWindowFrames = 0;
    double m_liveFps = 0;

    // CPU test
    PhysicsSim m_phys;
    CpuPreview m_preview;
    JobChain m_physChain;
    u32 m_physStep = 0;
    u64 m_physNs = 0, m_physBusy0 = 0;
    u32 m_previewFrame = 0;

    GtResult m_gt[GT_COUNT];
    CpuResult m_cpu;
    char m_error[160];
};

const char* test_name(int index);

}  // namespace tsb
