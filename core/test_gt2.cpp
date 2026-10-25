#include "bench_spec.h"
#include "shaders.h"
#include "sim_particles.h"
#include "tests.h"

namespace tsb {

namespace {

class ParticleTest : public GraphicsTest {
public:
    const char* id() const override { return "gt2"; }

    bool init(Gpu& gpu) override {
        m_gpu = &gpu;
        if (!m_sim.init(kParticleCount)) return false;
        m_prog = gl::program("particle", shaders::PARTICLE_VS, shaders::PARTICLE_FS);
        m_sphereProg = gl::program("sphere", shaders::SPHERE_VS, shaders::SPHERE_FS);
        if (!m_prog || !m_sphereProg) return false;
        glGenVertexArrays(1, &m_vao);
        MeshData sphere;
        mesh_icosphere(sphere, 3);
        if (!m_sphere.create(sphere)) return false;
        glGenBuffers(1, &m_attractorBuf);
        glBindBuffer(GL_ARRAY_BUFFER, m_attractorBuf);
        glBufferData(GL_ARRAY_BUFFER, sizeof(ParticleInstance) * ParticleFrame::ATTRACTORS, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return m_ring.create(GL_ARRAY_BUFFER, m_sim.instance_bytes());
    }

    void shutdown() override {
        if (m_prog) glDeleteProgram(m_prog);
        if (m_sphereProg) glDeleteProgram(m_sphereProg);
        m_prog = m_sphereProg = 0;
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
        if (m_attractorBuf) glDeleteBuffers(1, &m_attractorBuf);
        m_attractorBuf = 0;
        m_sphere.destroy();
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
        const ParticleFrame& f = m_frames[slot];
        Gpu& gpu = *m_gpu;
        gpu.post.begin_scene(0.0f, 0.0f, 0.0f);
        gpu.sky.draw_space(f.cam, f.time, v3(0.18f, 0.05f, 0.25f), v3(0.02f, 0.12f, 0.25f), 1.5f);

        // Attractor cores (opaque, depth-writing)
        ParticleInstance cores[ParticleFrame::ATTRACTORS];
        static const u32 coreColors[4] = {0xffb04dffu, 0xffffb34du, 0xff4d9cffu, 0xffff4de0u};
        for (u32 k = 0; k < ParticleFrame::ATTRACTORS; ++k) {
            cores[k].px = f.attractor[k].x;
            cores[k].py = f.attractor[k].y;
            cores[k].pz = f.attractor[k].z;
            cores[k].size = 1.6f;
            cores[k].color = coreColors[k];
        }
        glBindBuffer(GL_ARRAY_BUFFER, m_attractorBuf);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(cores), cores);
        static const InstanceAttrib inst[2] = {{4, GL_FLOAT, GL_FALSE, 0}, {4, GL_UNSIGNED_BYTE, GL_TRUE, 16}};
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glUseProgram(m_sphereProg);
        set_mat4(m_sphereProg, "uViewProj", f.cam.viewProj);
        set_v3(m_sphereProg, "uCamPos", f.cam.pos);
        set_v3(m_sphereProg, "uLightDir", v3(0.0f, 1.0f, 0.0f));
        set_f(m_sphereProg, "uEmissive", 1.0f);
        glBindVertexArray(m_sphere.vao);
        bind_instance_attribs(m_attractorBuf, (u32)sizeof(ParticleInstance), 0, 2, inst, 2);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m_sphere.indexCount, GL_UNSIGNED_SHORT, nullptr,
                                ParticleFrame::ATTRACTORS);

        // Particles: additive, depth-tested, no depth writes
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glUseProgram(m_prog);
        set_mat4(m_prog, "uViewProj", f.cam.viewProj);
        set_v3(m_prog, "uCamRight", f.cam.right);
        set_v3(m_prog, "uCamUp", f.cam.up);
        set_f(m_prog, "uIntensity", 0.05f);
        glBindVertexArray(m_vao);
        bind_instance_attribs(m_ring.buf[slot], (u32)sizeof(ParticleInstance), 0, 0, inst, 2);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)m_sim.count());
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);

        gpu.post.finish(frame, 0.9f, 1.0f);
    }

    u64 sim_hash() const override { return m_sim.hash(); }
    u32 last_draw_items() const override { return m_sim.count(); }

private:
    Gpu* m_gpu = nullptr;
    ParticleSim m_sim;
    JobChain m_chain;
    ParticleFrame m_frames[gl::StreamRing::SLOTS];
    gl::StreamRing m_ring;
    GLuint m_prog = 0, m_sphereProg = 0, m_vao = 0, m_attractorBuf = 0;
    GpuMesh m_sphere;
};

}  // namespace

GraphicsTest* create_gt2_particles() { return new ParticleTest(); }

}  // namespace tsb
