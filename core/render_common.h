// GPU-side building blocks shared by all graphics tests.
#pragma once

#include "gl_util.h"
#include "mesh_gen.h"
#include "sim_common.h"

namespace tsb {

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    u32 indexCount = 0;
    bool create(const MeshData& m);
    void destroy();
};

// Binds per-instance attributes starting at `firstLocation` for an instance
// stream; used before each instanced draw (GLES 3.0 has no baseInstance).
struct InstanceAttrib {
    GLint size;
    GLenum type;
    GLboolean normalized;
    u32 offset;
};
void bind_instance_attribs(GLuint buffer, u32 stride, u32 firstInstance, GLuint firstLocation,
                           const InstanceAttrib* attribs, u32 count);

// Offscreen HDR scene target + bloom chain + tone-mapped RGBA8 output.
// Every platform renders at exactly kRenderWidth x kRenderHeight.
class PostFx {
public:
    bool init();
    void destroy();
    bool hdr() const { return m_hdr; }
    void begin_scene(float r, float g, float b);
    void finish(u32 frame, float exposure, float bloomStrength);
    void present(int winW, int winH);

private:
    void draw_fullscreen();
    gl::Target m_scene, m_final;
    gl::Target m_bloom[3], m_tmp[3];
    GLuint m_vao = 0;
    GLuint m_bright = 0, m_down = 0, m_blur = 0, m_composite = 0, m_present = 0;
    bool m_hdr = false;
};

class SkyRenderer {
public:
    bool init();
    void destroy();
    void draw_space(const CameraState& cam, float time, V3 nebulaA, V3 nebulaB, float nebula);
    void draw_city(const CameraState& cam, float time, V3 moonDir);

private:
    void set_camera(GLuint prog, const CameraState& cam);
    GLuint m_vao = 0;
    GLuint m_space = 0, m_city = 0;
};

// State shared by all tests while the benchmark runs.
struct Gpu {
    gl::Caps caps;
    PostFx post;
    SkyRenderer sky;
};

inline void set_mat4(GLuint prog, const char* name, const M4& m) {
    glUniformMatrix4fv(gl::uloc(prog, name), 1, GL_FALSE, m.m);
}
inline void set_v3(GLuint prog, const char* name, V3 v) { glUniform3f(gl::uloc(prog, name), v.x, v.y, v.z); }
inline void set_f(GLuint prog, const char* name, float f) { glUniform1f(gl::uloc(prog, name), f); }
inline void set_i(GLuint prog, const char* name, int i) { glUniform1i(gl::uloc(prog, name), i); }

}  // namespace tsb
