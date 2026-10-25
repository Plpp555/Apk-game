#include "bench_spec.h"
#include "shaders.h"
#include "sim_asteroids.h"
#include "tests.h"

namespace tsb {

namespace {

class AsteroidTest : public GraphicsTest {
public:
    const char* id() const override { return "gt1"; }

    bool init(Gpu& gpu) override {
        m_gpu = &gpu;
        if (!m_sim.init(kAsteroidCount)) return false;
        for (u32 shape = 0; shape < AsteroidSim::SHAPES; ++shape) {
            for (u32 lod = 0; lod < AsteroidSim::LODS; ++lod) {
                MeshData md;
                mesh_asteroid(md, 3 - (int)lod, 0xA57E0000u + shape * 7919u);
                if (!m_mesh[shape][lod].create(md)) return false;
            }
        }
        MeshData star;
        mesh_icosphere(star, 4);
        if (!m_star.create(star)) return false;
        m_prog = gl::program("asteroid", shaders::ASTEROID_VS, shaders::ASTEROID_FS, shaders::NOISE);
        m_starProg = gl::program("star", shaders::STAR_VS, shaders::STAR_FS, shaders::NOISE);
        if (!m_prog || !m_starProg) return false;
        return m_ring.create(GL_ARRAY_BUFFER, m_sim.instance_bytes());
    }

    void shutdown() override {
        for (auto& row : m_mesh)
            for (auto& m : row) m.destroy();
        m_star.destroy();
        if (m_prog) glDeleteProgram(m_prog);
        if (m_starProg) glDeleteProgram(m_starProg);
        m_prog = m_starProg = 0;
        m_ring.destroy();
        m_sim.release();
    }

    void reset_sim() override { m_sim.reset(); }

    void begin_sim(u32 frame, int slot, JobSystem& jobs) override {
        void* dst = m_ring.map(slot);
        m_sim.build(m_chain, frame, dst, &m_frames[slot]);
        jobs.dispatch(&m_chain);
    }

    void end_sim(int slot, JobSystem& jobs) override {
        jobs.wait(&m_chain);
        m_ring.unmap(slot);
    }

    void render(u32 frame, int slot) override {
        const AsteroidFrame& f = m_frames[slot];
        Gpu& gpu = *m_gpu;
        gpu.post.begin_scene(0.0f, 0.0f, 0.0f);
        gpu.sky.draw_space(f.cam, f.time, v3(0.05f, 0.09f, 0.22f), v3(0.28f, 0.08f, 0.2f), 0.9f);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);

        glUseProgram(m_prog);
        set_mat4(m_prog, "uViewProj", f.cam.viewProj);
        set_v3(m_prog, "uCamPos", f.cam.pos);
        set_v3(m_prog, "uStarColor", v3(2.4f, 2.0f, 1.5f));
        set_v3(m_prog, "uAmbient", v3(0.02f, 0.025f, 0.045f));
        set_f(m_prog, "uFogDensity", 0.0016f);
        set_v3(m_prog, "uFogColor", v3(0.012f, 0.016f, 0.03f));

        static const InstanceAttrib attribs[3] = {
            {4, GL_FLOAT, GL_FALSE, 0}, {4, GL_FLOAT, GL_FALSE, 16}, {4, GL_UNSIGNED_BYTE, GL_TRUE, 32}};
        const u32 stride = (u32)sizeof(AsteroidInstance);
        m_items = 0;
        // LOD-major order: near (detailed) rocks first for early depth rejection
        for (u32 lod = 0; lod < AsteroidSim::LODS; ++lod) {
            for (u32 shape = 0; shape < AsteroidSim::SHAPES; ++shape) {
                u32 b = shape * AsteroidSim::LODS + lod;
                u32 n = f.bucketCount[b];
                if (!n) continue;
                GpuMesh& mesh = m_mesh[shape][lod];
                glBindVertexArray(mesh.vao);
                bind_instance_attribs(m_ring.buf[slot], stride, f.bucketStart[b], 2, attribs, 3);
                glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh.indexCount, GL_UNSIGNED_SHORT, nullptr,
                                        (GLsizei)n);
                m_items += n;
            }
        }
        // Moon (instance slot after all asteroids)
        glBindVertexArray(m_mesh[0][0].vao);
        bind_instance_attribs(m_ring.buf[slot], stride, m_sim.count(), 2, attribs, 3);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m_mesh[0][0].indexCount, GL_UNSIGNED_SHORT, nullptr, 1);

        // Central star
        glUseProgram(m_starProg);
        set_mat4(m_starProg, "uViewProj", f.cam.viewProj);
        glUniform4f(gl::uloc(m_starProg, "uPosRadius"), 0.0f, 0.0f, 0.0f, 14.0f);
        set_v3(m_starProg, "uCamPos", f.cam.pos);
        set_f(m_starProg, "uTime", f.time);
        glBindVertexArray(m_star.vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)m_star.indexCount, GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);

        gpu.post.finish(frame, 1.0f, 0.8f);
    }

    u64 sim_hash() const override { return m_sim.hash(); }
    u32 last_draw_items() const override { return m_items; }

private:
    Gpu* m_gpu = nullptr;
    AsteroidSim m_sim;
    JobChain m_chain;
    AsteroidFrame m_frames[gl::StreamRing::SLOTS];
    gl::StreamRing m_ring;
    GpuMesh m_mesh[AsteroidSim::SHAPES][AsteroidSim::LODS];
    GpuMesh m_star;
    GLuint m_prog = 0, m_starProg = 0;
    u32 m_items = 0;
};

}  // namespace

GraphicsTest* create_gt1_asteroids() { return new AsteroidTest(); }

}  // namespace tsb
