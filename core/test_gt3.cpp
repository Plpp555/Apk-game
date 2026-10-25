#include "bench_spec.h"
#include "shaders.h"
#include "sim_city.h"
#include "sim_particles.h"
#include "tests.h"

namespace tsb {

namespace {

class CityTest : public GraphicsTest {
public:
    const char* id() const override { return "gt3"; }

    bool init(Gpu& gpu) override {
        m_gpu = &gpu;
        if (!m_sim.init()) return false;
        m_buildingProg = gl::program("city_building", shaders::BUILDING_VS, shaders::CITY_FS, shaders::NOISE);
        m_groundProg = gl::program("city_ground", shaders::GROUND_VS, shaders::CITY_FS, shaders::NOISE);
        m_sphereProg = gl::program("sphere", shaders::SPHERE_VS, shaders::SPHERE_FS);
        if (!m_buildingProg || !m_groundProg || !m_sphereProg) return false;
        GLuint progs[2] = {m_buildingProg, m_groundProg};
        for (GLuint p : progs) {
            GLuint blockIndex = glGetUniformBlockIndex(p, "LightBlock");
            if (blockIndex == GL_INVALID_INDEX) return false;
            glUniformBlockBinding(p, blockIndex, 0);
        }
        MeshData box, plane, sphere;
        mesh_box(box);
        mesh_plane(plane, 420.0f);
        mesh_icosphere(sphere, 1);
        if (!m_box.create(box) || !m_plane.create(plane) || !m_sphere.create(sphere)) return false;

        bool ok = m_buildings.create(GL_ARRAY_BUFFER, m_sim.dst_bytes(CitySim::DST_BUILDINGS));
        ok = ok && m_lights.create(GL_UNIFORM_BUFFER, m_sim.dst_bytes(CitySim::DST_LIGHTS));
        ok = ok && m_clusters.create(GL_PIXEL_UNPACK_BUFFER, m_sim.dst_bytes(CitySim::DST_CLUSTERS));
        ok = ok && m_cars.create(GL_ARRAY_BUFFER, m_sim.dst_bytes(CitySim::DST_CARS));
        if (!ok) return false;
        glGenTextures(gl::StreamRing::SLOTS, m_clusterTex);
        for (int i = 0; i < gl::StreamRing::SLOTS; ++i) {
            glBindTexture(GL_TEXTURE_2D, m_clusterTex[i]);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8UI, CitySim::CLUSTERS, CitySim::MAX_PER_CLUSTER + 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        CitySim::slice_params(m_sliceScale, m_sliceBias);
        return gl::check("CityTest::init");
    }

    void shutdown() override {
        GLuint progs[3] = {m_buildingProg, m_groundProg, m_sphereProg};
        for (GLuint p : progs)
            if (p) glDeleteProgram(p);
        m_buildingProg = m_groundProg = m_sphereProg = 0;
        m_box.destroy();
        m_plane.destroy();
        m_sphere.destroy();
        m_buildings.destroy();
        m_lights.destroy();
        m_clusters.destroy();
        m_cars.destroy();
        if (m_clusterTex[0]) glDeleteTextures(gl::StreamRing::SLOTS, m_clusterTex);
        for (int i = 0; i < gl::StreamRing::SLOTS; ++i) m_clusterTex[i] = 0;
        m_sim.release();
    }

    void reset_sim() override { m_sim.reset(); }

    void begin_sim(u32 frame, int slot, JobSystem& jobs) override {
        void* dst[CitySim::DST_COUNT];
        dst[CitySim::DST_BUILDINGS] = m_buildings.map(slot);
        dst[CitySim::DST_LIGHTS] = m_lights.map(slot);
        dst[CitySim::DST_CLUSTERS] = m_clusters.map(slot);
        dst[CitySim::DST_CARS] = m_cars.map(slot);
        m_sim.build(m_chain, frame, dst, &m_frames[slot]);
        jobs.dispatch(&m_chain);
    }

    void end_sim(int slot, JobSystem& jobs) override {
        jobs.wait(&m_chain);
        m_buildings.unmap(slot);
        m_lights.unmap(slot);
        m_clusters.unmap(slot);
        m_cars.unmap(slot);
    }

    void setup_city_program(GLuint p, const CityFrame& f, int slot, int material) {
        glUseProgram(p);
        set_mat4(p, "uViewProj", f.cam.viewProj);
        set_mat4(p, "uView", f.cam.view);
        glUniform2f(gl::uloc(p, "uTileSize"), (float)kRenderWidth / (float)CitySim::TILES_X,
                    (float)kRenderHeight / (float)CitySim::TILES_Y);
        set_f(p, "uSliceScale", m_sliceScale);
        set_f(p, "uSliceBias", m_sliceBias);
        set_v3(p, "uCamPos", f.cam.pos);
        set_v3(p, "uMoonDir", kMoonDir);
        set_v3(p, "uFogColor", v3(0.05f, 0.025f, 0.06f));
        set_f(p, "uBlockPitch", CitySim::kPitch);
        set_i(p, "uMaterial", material);
        set_i(p, "uClusters", 0);
    }

    void render(u32 frame, int slot) override {
        const CityFrame& f = m_frames[slot];
        Gpu& gpu = *m_gpu;

        // Light-cluster texture for this frame from its pixel-unpack buffer (async upload)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_clusters.buf[slot]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_clusterTex[slot]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CitySim::CLUSTERS, CitySim::MAX_PER_CLUSTER + 1, GL_RED_INTEGER,
                        GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_lights.buf[slot]);

        gpu.post.begin_scene(0.0f, 0.0f, 0.0f);
        gpu.sky.draw_city(f.cam, f.time, kMoonDir);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_clusterTex[slot]);

        // Buildings, nearest distance bucket first
        setup_city_program(m_buildingProg, f, slot, 0);
        static const InstanceAttrib battr[2] = {{4, GL_FLOAT, GL_FALSE, 0}, {4, GL_FLOAT, GL_FALSE, 16}};
        glBindVertexArray(m_box.vao);
        m_items = 0;
        for (u32 b = 0; b < CityFrame::BUCKETS; ++b) {
            if (!f.bucketCount[b]) continue;
            bind_instance_attribs(m_buildings.buf[slot], (u32)sizeof(BuildingInstance), f.bucketStart[b], 2, battr, 2);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m_box.indexCount, GL_UNSIGNED_SHORT, nullptr,
                                    (GLsizei)f.bucketCount[b]);
            m_items += f.bucketCount[b];
        }

        // Ground plane
        setup_city_program(m_groundProg, f, slot, 1);
        glUniform2f(gl::uloc(m_groundProg, "uGroundOffset"), 280.0f, 280.0f);
        glBindVertexArray(m_plane.vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)m_plane.indexCount, GL_UNSIGNED_SHORT, nullptr);

        // Cars (emissive spheres at the light positions)
        static const InstanceAttrib cattr[2] = {{4, GL_FLOAT, GL_FALSE, 0}, {4, GL_UNSIGNED_BYTE, GL_TRUE, 16}};
        glUseProgram(m_sphereProg);
        set_mat4(m_sphereProg, "uViewProj", f.cam.viewProj);
        set_v3(m_sphereProg, "uCamPos", f.cam.pos);
        set_v3(m_sphereProg, "uLightDir", kMoonDir);
        set_f(m_sphereProg, "uEmissive", 1.0f);
        glBindVertexArray(m_sphere.vao);
        bind_instance_attribs(m_cars.buf[slot], (u32)sizeof(ParticleInstance), 0, 2, cattr, 2);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)m_sphere.indexCount, GL_UNSIGNED_SHORT, nullptr,
                                CitySim::LIGHTS);
        glBindVertexArray(0);

        gpu.post.finish(frame, 1.15f, 0.9f);
    }

    u64 sim_hash() const override { return m_sim.hash(); }
    u32 last_draw_items() const override { return m_items; }

private:
    const V3 kMoonDir = {0.35f, 0.55f, -0.76f};
    Gpu* m_gpu = nullptr;
    CitySim m_sim;
    JobChain m_chain;
    CityFrame m_frames[gl::StreamRing::SLOTS];
    gl::StreamRing m_buildings, m_lights, m_clusters, m_cars;
    GLuint m_clusterTex[gl::StreamRing::SLOTS] = {0, 0, 0};
    GLuint m_buildingProg = 0, m_groundProg = 0, m_sphereProg = 0;
    GpuMesh m_box, m_plane, m_sphere;
    float m_sliceScale = 1.0f, m_sliceBias = 0.0f;
    u32 m_items = 0;
};

}  // namespace

GraphicsTest* create_gt3_city() { return new CityTest(); }

}  // namespace tsb
