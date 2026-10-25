#include "benchmark.h"

#include "platform.h"
#include "reference_hashes.h"

namespace tsb {

static const u64 kPresentIntervalNs = 50000000ull;   // preview at most 20 Hz
static const u64 kCpuSliceNs = 50000000ull;          // CPU test status granularity

static const char* const kTestIds[4] = {"gt1", "gt2", "gt3", "cpu"};
static const char* const kTestNames[4] = {"Asteroid Belt", "Nebula Storm", "Neon City", "CPU Physics"};

const char* test_name(int index) { return (index >= 0 && index < 4) ? kTestNames[index] : ""; }

static const ReferenceHash* find_reference(const char* id) {
    for (const ReferenceHash& r : kReferenceHashes)
        if (strcmp(r.id, id) == 0) return &r;
    return nullptr;
}

static Verify verify_hash(const char* id, BenchMode mode, bool overridden, u64 h) {
    if (overridden) return VERIFY_UNKNOWN;
    const ReferenceHash* r = find_reference(id);
    if (!r) return VERIFY_UNKNOWN;
    u64 ref = mode == MODE_STANDARD ? r->standard : r->quick;
    return ref == h ? VERIFY_MATCH : VERIFY_MISMATCH;
}

static void sort_u32(u32* a, u32 n) {  // shell sort, n <= few thousand
    static const u32 gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
    for (u32 g : gaps) {
        for (u32 i = g; i < n; ++i) {
            u32 v = a[i];
            u32 j = i;
            while (j >= g && a[j - g] > v) {
                a[j] = a[j - g];
                j -= g;
            }
            a[j] = v;
        }
    }
}

bool Benchmark::init(gl::GetProcFn getProc, const BenchOptions& opt) {
    m_opt = opt;
    m_error[0] = 0;
    gl::init(getProc, m_gpu.caps);
    if (!m_gpu.post.init() || !m_gpu.sky.init()) {
        fail("GPU resource creation failed");
        return false;
    }
    u32 workers = opt.workerThreads ? opt.workerThreads : (opt.cpuCount > 1 ? opt.cpuCount - 1 : 1);
    m_jobs.start(workers);
    m_timer.create();
    m_frameUs.reserve(kGtFrames[MODE_STANDARD] + 16);
    char tmp[256];
    StrBuf sb(tmp, sizeof(tmp));
    sb.s("TriSync3D ").s(kBenchVersion).s(" renderer=").s(m_gpu.caps.renderer).s(" workers=").u(workers);
    sb.s(m_gpu.post.hdr() ? " hdr=RGBA16F" : " hdr=RGB10A2(fallback)");
    plat::log_line(sb.c_str());
    m_stage = STAGE_IDLE;
    return true;
}

void Benchmark::shutdown() {
    abort();
    m_preview.shutdown();
    m_phys.release();
    m_fences.destroy();
    m_timer.destroy();
    m_gpu.sky.destroy();
    m_gpu.post.destroy();
    m_jobs.stop();
}

void Benchmark::fail(const char* why) {
    StrBuf sb(m_error, sizeof(m_error));
    sb.s(why);
    plat::log_line(why);
    m_stage = STAGE_FAILED;
}

void Benchmark::start() {
    for (int i = 0; i < GT_COUNT; ++i) m_gt[i] = GtResult();
    m_cpu = CpuResult();
    m_testIndex = -1;
    next_test();
}

void Benchmark::abort() {
    if (finished()) return;
    if (m_test) {
        // Workers may still be writing into a mapped slot: finish that chain first.
        if (m_pendingSlot >= 0) m_test->end_sim(m_pendingSlot, m_jobs);
        m_pendingSlot = -1;
        glFinish();
        m_test->shutdown();
        delete m_test;
        m_test = nullptr;
    }
    m_preview.shutdown();
    m_phys.release();
    m_fences.destroy();
    m_stage = STAGE_ABORTED;
}

u32 Benchmark::frame_total() const {
    if (m_stage == STAGE_CPU_SINGLE) return kPhysStepsST[m_opt.mode];
    if (m_stage == STAGE_CPU_MULTI) return kPhysStepsMT[m_opt.mode];
    return m_stage == STAGE_WARMUP ? gt_warmup() : gt_frames();
}

u32 Benchmark::gt_frames() const { return m_opt.frameOverride ? m_opt.frameOverride : kGtFrames[m_opt.mode]; }
u32 Benchmark::gt_warmup() const {
    return m_opt.frameOverride ? tmin<u32>(m_opt.frameOverride, kGtWarmup[m_opt.mode]) : kGtWarmup[m_opt.mode];
}

void Benchmark::next_test() {
    for (;;) {
        m_testIndex++;
        if (m_testIndex >= 4) {
            m_stage = STAGE_DONE;
            plat::log_line("Benchmark finished");
            return;
        }
        if (m_opt.testMask & (1u << m_testIndex)) break;
    }
    m_stage = STAGE_LOADING;
}

void Benchmark::kick(u32 frame) {
    int slot = (int)(frame % gl::StreamRing::SLOTS);
    m_fenceWaitNs += m_fences.wait(slot);
    m_test->begin_sim(frame, slot, m_jobs);
    m_pendingSlot = slot;
}

bool Benchmark::tick(int winW, int winH) {
    switch (m_stage) {
        case STAGE_LOADING: {
            if (m_testIndex < GT_COUNT) {
                GraphicsTest* t = m_testIndex == 0 ? create_gt1_asteroids()
                                                   : (m_testIndex == 1 ? create_gt2_particles() : create_gt3_city());
                if (!t->init(m_gpu)) {
                    t->shutdown();
                    delete t;
                    fail("Test initialisation failed");
                    return false;
                }
                m_test = t;
                m_test->reset_sim();
                glFinish();
                m_stage = STAGE_WARMUP;
                m_frame = 1;
                m_lastPresent = 0;
                m_lastFrameEnd = plat::time_ns();
                m_fpsWindowStart = m_lastFrameEnd;
                m_fpsWindowFrames = 0;
                m_liveFps = 0;
                kick(1);
            } else {
                if (!m_phys.init(kPhysicsCount) || !m_preview.init(m_gpu, kPhysicsCount)) {
                    fail("CPU test initialisation failed");
                    return false;
                }
                m_cpu = CpuResult();
                m_cpu.ran = true;
                m_phys.reset();
                m_physStep = 0;
                m_physNs = 0;
                m_physBusy0 = m_jobs.busy_ns();
                m_lastPresent = 0;
                m_stage = STAGE_CPU_SINGLE;
            }
            return false;
        }
        case STAGE_WARMUP:
        case STAGE_RUNNING: return frame_step(winW, winH);
        case STAGE_CPU_SINGLE:
        case STAGE_CPU_MULTI: return cpu_slice(winW, winH);
        default: return false;
    }
}

bool Benchmark::frame_step(int winW, int winH) {
    const bool warm = m_stage == STAGE_WARMUP;
    const u32 last = warm ? gt_warmup() : gt_frames();
    const u32 f = m_frame;
    const int slot = (int)(f % gl::StreamRing::SLOTS);

    u64 t0 = plat::time_ns();
    m_test->end_sim(slot, m_jobs);
    m_pendingSlot = -1;
    u64 t1 = plat::time_ns();
    m_simWaitNs += t1 - t0;
    if (f < last) kick(f + 1);
    u64 t2 = plat::time_ns();
    m_timer.begin();
    m_test->render(f, slot);
    m_timer.end();
    m_fences.signal(slot);
    glFlush();
    u64 t3 = plat::time_ns();
    m_submitNs += t3 - t2;
    m_itemSum += m_test->last_draw_items();

    bool presented = false;
    if (m_opt.present && winW > 0 && winH > 0 && (t3 - m_lastPresent >= kPresentIntervalNs || f == last)) {
        m_gpu.post.present(winW, winH);
        m_lastPresent = t3;
        presented = true;
    }

    u64 now = plat::time_ns();
    if (!warm) {
        u64 dt = now - m_lastFrameEnd;
        m_frameUs.push((u32)tmin<u64>(dt / 1000u, 0xffffffffu));
    }
    m_lastFrameEnd = now;
    m_fpsWindowFrames++;
    if (now - m_fpsWindowStart >= 500000000ull) {
        m_liveFps = (double)m_fpsWindowFrames / ns_to_s(now - m_fpsWindowStart);
        m_fpsWindowStart = now;
        m_fpsWindowFrames = 0;
    }
    m_frame++;

    if (f == last) {
        if (warm) {
            // Warm-up done: drain the GPU, rewind the simulation, start timing.
            glFinish();
            m_fences.destroy();
            m_test->reset_sim();
            m_stage = STAGE_RUNNING;
            m_frame = 1;
            m_simWaitNs = m_fenceWaitNs = m_submitNs = 0;
            m_itemSum = 0;
            m_frameUs.clear();
            m_timer.poll();
            m_timer.reset_stats();
            m_busy0 = m_jobs.busy_ns();
            m_t0 = plat::time_ns();
            m_lastFrameEnd = m_t0;
            kick(1);
        } else {
            finish_gt();
        }
    }
    return presented;
}

void Benchmark::finish_gt() {
    glFinish();  // all GPU work of the measured frames is inside the timed region
    u64 tEnd = plat::time_ns();
    GtResult& r = m_gt[m_testIndex];
    r.ran = true;
    r.frames = gt_frames();
    r.seconds = ns_to_s(tEnd - m_t0);
    r.fps = r.seconds > 0 ? (double)r.frames / r.seconds : 0;
    r.avgFrameMs = r.seconds * 1000.0 / (double)r.frames;
    double perFrame = 1.0 / (double)r.frames;
    r.cpuBusyMs = ns_to_ms(m_jobs.busy_ns() - m_busy0) * perFrame;
    r.simWaitMs = ns_to_ms(m_simWaitNs) * perFrame;
    r.fenceWaitMs = ns_to_ms(m_fenceWaitNs) * perFrame;
    r.submitMs = ns_to_ms(m_submitNs) * perFrame;
    r.avgItems = (double)m_itemSum * perFrame;
    m_timer.poll();
    r.gpuMs = m_timer.samples ? m_timer.sumMs / (double)m_timer.samples : -1.0;
    // "1% low": 99th percentile frame time
    if (m_frameUs.count) {
        sort_u32(m_frameUs.data, m_frameUs.count);
        u32 idx = (m_frameUs.count * 99) / 100;
        if (idx >= m_frameUs.count) idx = m_frameUs.count - 1;
        double us = (double)m_frameUs[idx];
        r.lowFps = us > 0 ? 1.0e6 / us : 0;
    }
    r.hash = m_test->sim_hash();
    r.verify = verify_hash(kTestIds[m_testIndex], m_opt.mode, m_opt.frameOverride != 0, r.hash);

    char tmp[200];
    StrBuf sb(tmp, sizeof(tmp));
    sb.s(kTestIds[m_testIndex]).s(": ").f(r.fps, 2).s(" fps, hash ").hex64(r.hash);
    sb.s(r.verify == VERIFY_MATCH ? " (match)" : (r.verify == VERIFY_MISMATCH ? " (MISMATCH)" : ""));
    plat::log_line(sb.c_str());

    m_test->shutdown();
    delete m_test;
    m_test = nullptr;
    m_fences.destroy();
    next_test();
}

bool Benchmark::cpu_slice(int winW, int winH) {
    const bool single = m_stage == STAGE_CPU_SINGLE;
    const u32 target = single ? kPhysStepsST[m_opt.mode] : kPhysStepsMT[m_opt.mode];
    const u32 stSteps = kPhysStepsST[m_opt.mode];
    u64 sliceStart = plat::time_ns();
    while (m_physStep < target && plat::time_ns() - sliceStart < kCpuSliceNs) {
        u64 t0 = plat::time_ns();
        m_phys.build(m_physChain);
        if (single) {
            m_jobs.run_inline(&m_physChain);
        } else {
            m_jobs.dispatch(&m_physChain);
            m_jobs.wait(&m_physChain);
        }
        m_phys.advance();
        m_physNs += plat::time_ns() - t0;
        m_physStep++;
        if (!single && m_physStep == stSteps) m_cpu.hashMultiAtSingle = m_phys.hash();  // outside timed span
    }
    m_liveFps = m_physNs ? (double)m_physStep / ns_to_s(m_physNs) : 0;  // steps per second

    if (m_physStep >= target) {
        const double work = (double)kPhysicsCount * (double)target;
        if (single) {
            m_cpu.stSteps = target;
            m_cpu.stSeconds = ns_to_s(m_physNs);
            m_cpu.stRate = work / m_cpu.stSeconds;
            m_cpu.hashSingle = m_phys.hash();
            m_phys.reset();
            m_physStep = 0;
            m_physNs = 0;
            m_physBusy0 = m_jobs.busy_ns();
            m_stage = STAGE_CPU_MULTI;
        } else {
            m_cpu.mtSteps = target;
            m_cpu.mtSeconds = ns_to_s(m_physNs);
            m_cpu.mtRate = work / m_cpu.mtSeconds;
            m_cpu.scaling = m_cpu.stRate > 0 ? m_cpu.mtRate / m_cpu.stRate : 0;
            double threads = (double)(m_jobs.worker_count() + 1);
            m_cpu.utilization = ns_to_s(m_jobs.busy_ns() - m_physBusy0) / (m_cpu.mtSeconds * threads);
            m_cpu.hash = m_phys.hash();
            m_cpu.threadsConsistent = m_cpu.hashSingle == m_cpu.hashMultiAtSingle;
            m_cpu.verify = verify_hash("cpu", m_opt.mode, false, m_cpu.hash);
            char tmp[200];
            StrBuf sb(tmp, sizeof(tmp));
            sb.s("cpu: ST ").f(m_cpu.stRate * 1e-6, 2).s(" M/s, MT ").f(m_cpu.mtRate * 1e-6, 2).s(" M/s, ST==MT ");
            sb.s(m_cpu.threadsConsistent ? "yes" : "NO");
            plat::log_line(sb.c_str());
            m_preview.shutdown();
            m_phys.release();
            next_test();
            return false;
        }
    }

    u64 now = plat::time_ns();
    if (m_opt.present && winW > 0 && winH > 0 && now - m_lastPresent >= kPresentIntervalNs) {
        m_preview.render(m_phys, m_previewFrame++);
        m_gpu.post.present(winW, winH);
        m_lastPresent = plat::time_ns();
        return true;
    }
    return false;
}

double Benchmark::graphics_score() const {
    double inv = 0;
    for (int i = 0; i < GT_COUNT; ++i) {
        if (!m_gt[i].ran || m_gt[i].fps <= 0) return 0;
        inv += 1.0 / m_gt[i].fps;
    }
    return kGraphicsScoreScale * (double)GT_COUNT / inv;
}

double Benchmark::cpu_score() const { return m_cpu.ran ? m_cpu.mtRate / kCpuScoreDivisor : 0; }

double Benchmark::overall_score() const {
    double g = graphics_score(), c = cpu_score();
    if (g <= 0 || c <= 0) return 0;
    return 1.0 / (kWeightGraphics / g + kWeightCpu / c);
}

bool Benchmark::official() const {
    if (m_opt.mode != MODE_STANDARD || m_opt.frameOverride || m_opt.testMask != 0xF) return false;
    for (int i = 0; i < GT_COUNT; ++i)
        if (m_gt[i].verify == VERIFY_MISMATCH) return false;
    return m_cpu.verify != VERIFY_MISMATCH && m_cpu.threadsConsistent;
}

static const char* stage_name(Stage s) {
    switch (s) {
        case STAGE_IDLE: return "idle";
        case STAGE_LOADING: return "loading";
        case STAGE_WARMUP: return "warmup";
        case STAGE_RUNNING: return "running";
        case STAGE_CPU_SINGLE: return "cpu_single";
        case STAGE_CPU_MULTI: return "cpu_multi";
        case STAGE_DONE: return "done";
        case STAGE_FAILED: return "failed";
        case STAGE_ABORTED: return "aborted";
    }
    return "unknown";
}

static const char* verify_name(Verify v) {
    return v == VERIFY_MATCH ? "match" : (v == VERIFY_MISMATCH ? "mismatch" : "unknown");
}

void Benchmark::status_json(StrBuf& sb) const {
    u32 done = 0, total = 0;
    double progress = 0;
    for (int i = 0; i < 4; ++i)
        if (m_opt.testMask & (1u << i)) total++;
    for (int i = 0; i < m_testIndex && i < 4; ++i)
        if (m_opt.testMask & (1u << i)) done++;
    double part = 0;
    u32 frame = 0, frames = 0;
    if (m_stage == STAGE_WARMUP || m_stage == STAGE_RUNNING) {
        frames = m_stage == STAGE_WARMUP ? gt_warmup() : gt_frames();
        frame = m_frame > frames ? frames : m_frame;
        part = m_stage == STAGE_RUNNING ? (double)frame / (double)frames : 0;
    } else if (m_stage == STAGE_CPU_SINGLE || m_stage == STAGE_CPU_MULTI) {
        u32 st = kPhysStepsST[m_opt.mode], mt = kPhysStepsMT[m_opt.mode];
        frames = m_stage == STAGE_CPU_SINGLE ? st : mt;
        frame = m_physStep;
        part = m_stage == STAGE_CPU_SINGLE ? 0.5 * frame / st : 0.5 + 0.5 * frame / mt;
    }
    if (m_stage == STAGE_DONE) progress = 1.0;
    else if (total) progress = ((double)done + part) / (double)total;
    int ti = m_testIndex < 0 ? 0 : (m_testIndex > 3 ? 3 : m_testIndex);
    sb.s("{\"stage\":").json_str(stage_name(m_stage));
    sb.s(",\"test\":").json_str(kTestIds[ti]);
    sb.s(",\"testName\":").json_str(kTestNames[ti]);
    sb.s(",\"testIndex\":").i(ti);
    sb.s(",\"frame\":").u(frame).s(",\"frames\":").u(frames);
    sb.s(",\"fps\":").f(m_liveFps, 1);
    sb.s(",\"progress\":").f(progress, 4);
    sb.s(",\"error\":").json_str(m_error);
    sb.s("}");
}

void Benchmark::result_json(StrBuf& sb) const {
    sb.s("{\"version\":").json_str(kBenchVersion);
    sb.s(",\"mode\":").json_str(m_opt.mode == MODE_STANDARD ? "standard" : "quick");
    sb.s(",\"official\":").s(official() ? "true" : "false");
    sb.s(",\"platform\":").json_str(m_opt.platformName);
    sb.s(",\"stage\":").json_str(stage_name(m_stage));
    sb.s(",\"gpu\":{\"vendor\":").json_str(m_gpu.caps.vendor);
    sb.s(",\"renderer\":").json_str(m_gpu.caps.renderer);
    sb.s(",\"version\":").json_str(m_gpu.caps.version);
    sb.s(",\"hdr\":").s(m_gpu.post.hdr() ? "true" : "false");
    sb.s(",\"timerQuery\":").s(m_gpu.caps.timerQuery ? "true" : "false").s("}");
    sb.s(",\"threads\":").u(m_jobs.worker_count() + 1);
    sb.s(",\"resolution\":\"").i(kRenderWidth).s("x").i(kRenderHeight).s("\"");
    sb.s(",\"scores\":{\"overall\":").f(overall_score(), 0);
    sb.s(",\"graphics\":").f(graphics_score(), 0);
    sb.s(",\"cpu\":").f(cpu_score(), 0);
    sb.s(",\"cpuSingle\":").f(m_cpu.ran ? m_cpu.stRate / kCpuScoreDivisor : 0, 0).s("}");
    sb.s(",\"tests\":[");
    for (int i = 0; i < GT_COUNT; ++i) {
        const GtResult& r = m_gt[i];
        if (i) sb.c(',');
        double frameMs = r.avgFrameMs > 0 ? r.avgFrameMs : 1;
        const char* bound = "balanced";
        if (r.simWaitMs > 0.1 * frameMs && r.simWaitMs > r.fenceWaitMs) bound = "cpu";
        else if (r.fenceWaitMs > 0.1 * frameMs || (r.gpuMs > 0.85 * frameMs)) bound = "gpu";
        else if (r.submitMs > 0.6 * frameMs) bound = "driver";
        sb.s("{\"id\":").json_str(kTestIds[i]).s(",\"name\":").json_str(kTestNames[i]);
        sb.s(",\"ran\":").s(r.ran ? "true" : "false");
        sb.s(",\"frames\":").u(r.frames).s(",\"seconds\":").f(r.seconds, 3);
        sb.s(",\"fps\":").f(r.fps, 2).s(",\"lowFps\":").f(r.lowFps, 2);
        sb.s(",\"frameMs\":").f(r.avgFrameMs, 3).s(",\"cpuMs\":").f(r.cpuBusyMs, 3);
        sb.s(",\"gpuMs\":").f(r.gpuMs, 3).s(",\"simWaitMs\":").f(r.simWaitMs, 3);
        sb.s(",\"fenceWaitMs\":").f(r.fenceWaitMs, 3).s(",\"submitMs\":").f(r.submitMs, 3);
        sb.s(",\"items\":").f(r.avgItems, 0).s(",\"bound\":").json_str(bound);
        sb.s(",\"hash\":\"").hex64(r.hash).s("\",\"verify\":").json_str(verify_name(r.verify)).s("}");
    }
    sb.s("],\"cpu\":{\"ran\":").s(m_cpu.ran ? "true" : "false");
    sb.s(",\"stSteps\":").u(m_cpu.stSteps).s(",\"mtSteps\":").u(m_cpu.mtSteps);
    sb.s(",\"stSeconds\":").f(m_cpu.stSeconds, 3).s(",\"mtSeconds\":").f(m_cpu.mtSeconds, 3);
    sb.s(",\"stRate\":").f(m_cpu.stRate, 0).s(",\"mtRate\":").f(m_cpu.mtRate, 0);
    sb.s(",\"scaling\":").f(m_cpu.scaling, 2).s(",\"utilization\":").f(m_cpu.utilization, 3);
    sb.s(",\"threadsConsistent\":").s(m_cpu.threadsConsistent ? "true" : "false");
    sb.s(",\"hash\":\"").hex64(m_cpu.hash).s("\",\"verify\":").json_str(verify_name(m_cpu.verify)).s("}");
    sb.s(",\"error\":").json_str(m_error);
    sb.s("}");
}

}  // namespace tsb
