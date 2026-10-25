#include "test_cpu.h"

#include "shaders.h"
#include "sim_particles.h"

namespace tsb {

bool CpuPreview::init(Gpu& gpu, u32 sphereCount) {
    m_gpu = &gpu;
    m_count = sphereCount;
    m_prog = gl::program("sphere", shaders::SPHERE_VS, shaders::SPHERE_FS);
    if (!m_prog) return false;
    MeshData sphere;
    mesh_icosphere(sphere, 1);
    if (!m_sphere.create(sphere)) return false;
    u32 bytes = (sphereCount + 1) * (u32)sizeof(ParticleInstance);
    glGenBuffers(1, &m_buf);
    glBindBuffer(GL_ARRAY_BUFFER, m_buf);
    glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_staging = mem_alloc(bytes);
    return gl::check("CpuPreview::init");
}

void CpuPreview::shutdown() {
    if (m_prog) glDeleteProgram(m_prog);
    m_prog = 0;
    if (m_buf) glDeleteBuffers(1, &m_buf);
    m_buf = 0;
    m_sphere.destroy();
    mem_free(m_staging);
    m_staging = nullptr;
}

void CpuPreview::render(const PhysicsSim& sim, u32 frame) {
    ParticleInstance* inst = (ParticleInstance*)m_staging;
    const float* px = sim.pos_x();
    const float* py = sim.pos_y();
    const float* pz = sim.pos_z();
    const float* r = sim.radius();
    for (u32 i = 0; i < m_count; ++i) {
        float h = dm::clampf(py[i] * (1.0f / 14.0f), 0.0f, 1.0f);
        inst[i].px = px[i];
        inst[i].py = py[i];
        inst[i].pz = pz[i];
        inst[i].size = r[i];
        inst[i].color = pack_rgba8(0.15f + 0.85f * h, 0.45f + 0.3f * (1.0f - h), 1.0f - 0.7f * h, 1.0f);
    }
    V3 sp = sim.stirrer_pos();
    inst[m_count].px = sp.x;
    inst[m_count].py = sp.y;
    inst[m_count].pz = sp.z;
    inst[m_count].size = sim.stirrer_radius();
    inst[m_count].color = pack_rgba8(0.9f, 0.9f, 0.95f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, m_buf);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (m_count + 1) * sizeof(ParticleInstance), inst);

    CameraState cam;
    float a = 0.4f + (float)sim.step_index() * 0.0008f;
    camera_build(cam, v3(20.0f + 42.0f * dm::sin(a), 30.0f, 20.0f - 42.0f * dm::cos(a)), v3(20.0f, 5.0f, 20.0f),
                 55.0f * (dm::PI / 180.0f), 0.5f, 300.0f);

    Gpu& gpu = *m_gpu;
    gpu.post.begin_scene(0.02f, 0.025f, 0.035f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(m_prog);
    set_mat4(m_prog, "uViewProj", cam.viewProj);
    set_v3(m_prog, "uCamPos", cam.pos);
    set_v3(m_prog, "uLightDir", normalize(v3(0.4f, 0.8f, -0.3f)));
    set_f(m_prog, "uEmissive", 0.0f);
    static const InstanceAttrib attr[2] = {{4, GL_FLOAT, GL_FALSE, 0}, {4, GL_UNSIGNED_BYTE, GL_TRUE, 16}};
    glBindVertexArray(m_sphere.vao);
    bind_instance_attribs(m_buf, (u32)sizeof(ParticleInstance), 0, 2, attr, 2);
    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m_sphere.indexCount, GL_UNSIGNED_SHORT, nullptr,
                            (GLsizei)(m_count + 1));
    glBindVertexArray(0);
    gpu.post.finish(frame, 1.0f, 0.4f);
}

}  // namespace tsb
