#include "jobs.h"

namespace tsb {

static const u64 kSpinNs = 40000;  // spin 40us before sleeping (phase hand-offs are short)
static const u32 kNotOpen = 0xffffffffu;

static inline u64 claim_word(u32 gen, u32 next) { return ((u64)gen << 32) | (u64)next; }

bool JobSystem::start(u32 workerCount) {
    if (workerCount > 64) workerCount = 64;
    m_mutex = plat::mutex_create();
    m_wake = plat::cond_create();
    m_doneCond = plat::cond_create();
    m_quit = 0;
    m_epoch = 0;
    m_active = nullptr;
    m_workerCount = 0;
    for (u32 i = 0; i < workerCount; ++i) {
        plat::Thread* t = plat::thread_start(worker_entry, this);
        if (!t) break;
        m_threads[m_workerCount++] = t;
    }
    return true;
}

void JobSystem::stop() {
    if (!m_mutex) return;
    plat::mutex_lock(m_mutex);
    m_quit = 1;
    m_epoch = m_epoch + 1;
    plat::cond_broadcast(m_wake);
    plat::mutex_unlock(m_mutex);
    for (u32 i = 0; i < m_workerCount; ++i) plat::thread_join(m_threads[i]);
    m_workerCount = 0;
    plat::cond_destroy(m_wake);
    plat::cond_destroy(m_doneCond);
    plat::mutex_destroy(m_mutex);
    m_mutex = nullptr;
}

void JobSystem::worker_entry(void* arg) { ((JobSystem*)arg)->worker_loop(); }

void JobSystem::worker_loop() {
    u32 seen = 0;
    for (;;) {
        // Short spin: phase transitions inside a frame happen within microseconds.
        u64 spinStart = plat::time_ns();
        while (atomic_load(&m_epoch) == seen && plat::time_ns() - spinStart < kSpinNs) plat::cpu_relax();

        plat::mutex_lock(m_mutex);
        while (m_epoch == seen && !m_quit) plat::cond_wait(m_wake, m_mutex);
        seen = m_epoch;
        JobChain* chain = m_active;
        u32 gen = m_activeGen;
        u32 quit = m_quit;
        plat::mutex_unlock(m_mutex);
        if (quit) return;
        if (!chain) continue;
        while (try_execute(chain, gen)) {
        }
    }
}

bool JobSystem::try_execute(JobChain* chain, u32 gen) {
    u32 p = atomic_load(&chain->phaseIndex);
    if (p >= (u32)JobChain::MAX_PHASES) return false;
    JobPhase& ph = chain->phases[p];
    for (;;) {
        u64 w = atomic_load64_acq(&ph.claim);
        if ((u32)(w >> 32) != gen) return false;  // stale chain or phase of another dispatch
        u32 n = (u32)w;
        if (n >= ph.chunks) return false;         // not open yet, or fully claimed
        if (atomic_cas64(&ph.claim, w, w + 1)) {
            u64 t0 = plat::time_ns();
            ph.fn(ph.ctx, n);
            atomic_add64(&m_busyNs, plat::time_ns() - t0);
            finish_chunk(chain, p);
            return true;
        }
    }
}

void JobSystem::finish_chunk(JobChain* chain, u32 p) {
    if (atomic_sub(&chain->phases[p].remaining, 1) != 1) return;
    // Last chunk of phase p finished: open the next phase (or complete the chain).
    u32 np = p + 1;
    if (np < chain->phaseCount) atomic_store64_rel(&chain->phases[np].claim, claim_word(chain->gen, 0));
    atomic_store(&chain->phaseIndex, np);
    plat::mutex_lock(m_mutex);
    if (np >= chain->phaseCount) {
        atomic_store(&chain->done, 1);
        if (m_active == chain) m_active = nullptr;
    } else {
        m_epoch = m_epoch + 1;
        plat::cond_broadcast(m_wake);
    }
    plat::cond_broadcast(m_doneCond);
    plat::mutex_unlock(m_mutex);
}

void JobSystem::dispatch(JobChain* chain) {
    if (!chain->phaseCount) {
        chain->done = 1;
        return;
    }
    u32 gen = chain->gen + 1;
    if (gen == 0) gen = 1;
    chain->gen = gen;
    chain->phaseIndex = 0;
    chain->done = 0;
    for (u32 i = chain->phaseCount; i-- > 0;) {
        chain->phases[i].remaining = chain->phases[i].chunks;
        atomic_store64_rel(&chain->phases[i].claim, claim_word(gen, i == 0 ? 0 : kNotOpen));
    }
    plat::mutex_lock(m_mutex);
    m_active = chain;
    m_activeGen = gen;
    m_epoch = m_epoch + 1;
    plat::cond_broadcast(m_wake);
    plat::mutex_unlock(m_mutex);
}

void JobSystem::wait(JobChain* chain) {
    u32 gen = chain->gen;
    while (!atomic_load(&chain->done)) {
        if (try_execute(chain, gen)) continue;
        u32 phase = atomic_load(&chain->phaseIndex);
        u64 spinStart = plat::time_ns();
        while (!atomic_load(&chain->done) && atomic_load(&chain->phaseIndex) == phase &&
               plat::time_ns() - spinStart < kSpinNs)
            plat::cpu_relax();
        plat::mutex_lock(m_mutex);
        while (!chain->done && chain->phaseIndex == phase) plat::cond_wait(m_doneCond, m_mutex);
        plat::mutex_unlock(m_mutex);
    }
}

void JobSystem::run_inline(JobChain* chain) {
    u64 t0 = plat::time_ns();
    for (u32 p = 0; p < chain->phaseCount; ++p) {
        JobPhase& ph = chain->phases[p];
        for (u32 k = 0; k < ph.chunks; ++k) ph.fn(ph.ctx, k);
    }
    atomic_add64(&m_busyNs, plat::time_ns() - t0);
    chain->phaseIndex = chain->phaseCount;
    chain->done = 1;
}

}  // namespace tsb
