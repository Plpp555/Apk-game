// Job system: a fixed pool of worker threads executing "chains" of phases.
//
// A chain is an ordered list of phases. Each phase is split into a fixed number
// of chunks; chunk boundaries never depend on the thread count, so results are
// identical whether 1 or 16 threads run them (determinism requirement).
// The thread finishing the last chunk of a phase opens the next phase, so a
// whole simulation frame (simulate -> prefix sums -> scatter) runs on the
// workers without the render thread being involved. The render thread only
// dispatches a chain and later waits for it, helping with chunks while it waits.
//
// Chains are reused every frame. Each phase has a 64-bit claim word holding
// (dispatch generation << 32 | next chunk); a worker that still holds a chain
// from an older dispatch can never claim a chunk of a newer one.
#pragma once

#include "atomics.h"
#include "platform.h"

namespace tsb {

typedef void (*JobFn)(void* ctx, u32 chunk);

struct JobPhase {
    JobFn fn;
    void* ctx;
    u32 chunks;
    volatile u64 claim;      // (generation << 32) | next chunk index
    volatile u32 remaining;  // chunks not yet finished
};

struct JobChain {
    enum { MAX_PHASES = 8 };
    static const u64 CLOSED = 0xffffffffull;

    JobPhase phases[MAX_PHASES];
    u32 phaseCount = 0;
    u32 gen = 0;
    volatile u32 phaseIndex = 0;
    volatile u32 done = 1;

    JobChain() {
        for (u32 i = 0; i < MAX_PHASES; ++i) phases[i].claim = CLOSED;
    }
    // Only call when the chain is not running (after wait()).
    void reset() {
        for (u32 i = 0; i < MAX_PHASES; ++i) atomic_store64_rel(&phases[i].claim, CLOSED);
        phaseCount = 0;
    }
    void add(JobFn fn, void* ctx, u32 chunks) {
        JobPhase& p = phases[phaseCount++];
        p.fn = fn;
        p.ctx = ctx;
        p.chunks = chunks ? chunks : 1;
    }
};

class JobSystem {
public:
    bool start(u32 workerCount);
    void stop();
    u32 worker_count() const { return m_workerCount; }

    // Asynchronous: workers start immediately; the caller continues.
    void dispatch(JobChain* chain);
    // Wait for completion; the calling thread executes chunks while waiting.
    void wait(JobChain* chain);
    // Run the chain on the calling thread only (single-thread CPU test).
    void run_inline(JobChain* chain);

    // Total busy time spent inside job functions (all threads, ns).
    u64 busy_ns() const { return atomic_load64(&m_busyNs); }

private:
    static void worker_entry(void* arg);
    void worker_loop();
    // Returns true if a chunk was executed.
    bool try_execute(JobChain* chain, u32 gen);
    void finish_chunk(JobChain* chain, u32 phase);

    plat::Thread* m_threads[64];
    u32 m_workerCount = 0;
    plat::Mutex* m_mutex = nullptr;
    plat::Cond* m_wake = nullptr;
    plat::Cond* m_doneCond = nullptr;
    JobChain* m_active = nullptr;  // guarded by m_mutex
    u32 m_activeGen = 0;           // guarded by m_mutex
    volatile u32 m_epoch = 0;
    volatile u32 m_quit = 0;
    volatile u64 m_busyNs = 0;
};

}  // namespace tsb
