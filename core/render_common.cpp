#include "render_common.h"

#include "platform.h"
#include "shaders.h"

namespace tsb {

bool GpuMesh::create(const MeshData& m) {
    indexCount = m.indices.count;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, m.verts.count * sizeof(float), m.verts.data, GL_STATIC_DRAW);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.indices.count * sizeof(u16), m.indices.data, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (const void*)12);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return gl::check("GpuMesh::create");
}

void GpuMesh::destroy() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    vao = vbo = ibo = 0;
}

void bind_instance_attribs(GLuint buffer, u32 stride, u32 firstInstance, GLuint firstLocation,
                           const InstanceAttrib* attribs, u32 count) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    size_t base = (size_t)firstInstance * stride;
    for (u32 i = 0; i < count; ++i) {
        GLuint loc = firstLocation + i;
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, attribs[i].size, attribs[i].type, attribs[i].normalized, (GLsizei)stride,
                              (const void*)(base + attribs[i].offset));
        glVertexAttribDivisor(loc, 1);
    }
}

static bool has_ext(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) return true;
    }
    return false;
}

bool PostFx::init() {
    // RGBA16F is colour-renderable on GLES 3.0 with EXT_color_buffer_half_float
    // or EXT_color_buffer_float (practically all ES3 GPUs and ANGLE/D3D11).
    m_hdr = has_ext("GL_EXT_color_buffer_half_float") || has_ext("GL_EXT_color_buffer_float");
    GLenum ifmt = m_hdr ? GL_RGBA16F : GL_RGB10_A2;
    GLenum type = m_hdr ? GL_HALF_FLOAT : GL_UNSIGNED_INT_2_10_10_10_REV;
    bool ok = gl::target_create(m_scene, kRenderWidth, kRenderHeight, ifmt, GL_RGBA, type, true);
    if (!ok && m_hdr) {
        plat::log_line("RGBA16F target failed, falling back to RGB10_A2");
        gl::target_destroy(m_scene);
        m_hdr = false;
        ifmt = GL_RGB10_A2;
        type = GL_UNSIGNED_INT_2_10_10_10_REV;
        ok = gl::target_create(m_scene, kRenderWidth, kRenderHeight, ifmt, GL_RGBA, type, true);
    }
    if (!ok) return false;
    int w = kRenderWidth / 2, h = kRenderHeight / 2;
    for (int i = 0; i < 3; ++i) {
        if (!gl::target_create(m_bloom[i], w, h, ifmt, GL_RGBA, type, false)) return false;
        if (!gl::target_create(m_tmp[i], w, h, ifmt, GL_RGBA, type, false)) return false;
        w /= 2;
        h /= 2;
    }
    if (!gl::target_create(m_final, kRenderWidth, kRenderHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false))
        return false;
    m_bright = gl::program("bright", shaders::FULLSCREEN_VS, shaders::BRIGHT_FS);
    m_down = gl::program("downsample", shaders::FULLSCREEN_VS, shaders::DOWNSAMPLE_FS);
    m_blur = gl::program("blur", shaders::FULLSCREEN_VS, shaders::BLUR_FS);
    m_composite = gl::program("composite", shaders::FULLSCREEN_VS, shaders::COMPOSITE_FS);
    m_present = gl::program("present", shaders::FULLSCREEN_VS, shaders::PRESENT_FS);
    glGenVertexArrays(1, &m_vao);
    return m_bright && m_down && m_blur && m_composite && m_present && gl::check("PostFx::init");
}

void PostFx::destroy() {
    gl::target_destroy(m_scene);
    gl::target_destroy(m_final);
    for (int i = 0; i < 3; ++i) {
        gl::target_destroy(m_bloom[i]);
        gl::target_destroy(m_tmp[i]);
    }
    GLuint progs[] = {m_bright, m_down, m_blur, m_composite, m_present};
    for (GLuint p : progs)
        if (p) glDeleteProgram(p);
    m_bright = m_down = m_blur = m_composite = m_present = 0;
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
}

void PostFx::draw_fullscreen() {
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostFx::begin_scene(float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_scene.fbo);
    glViewport(0, 0, kRenderWidth, kRenderHeight);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(r, g, b, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostFx::finish(u32 frame, float exposure, float bloomStrength) {
    // The depth buffer is not needed after the scene pass: let tilers skip the store.
    const GLenum discard[1] = {GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, discard);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glActiveTexture(GL_TEXTURE0);

    // Bright pass: scene (full res) -> bloom[0] (half res)
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloom[0].fbo);
    glViewport(0, 0, m_bloom[0].w, m_bloom[0].h);
    glUseProgram(m_bright);
    glBindTexture(GL_TEXTURE_2D, m_scene.color);
    set_i(m_bright, "uSrc", 0);
    glUniform2f(gl::uloc(m_bright, "uTexel"), 1.0f / (float)kRenderWidth, 1.0f / (float)kRenderHeight);
    set_f(m_bright, "uThreshold", 1.0f);
    draw_fullscreen();

    for (int lvl = 0; lvl < 3; ++lvl) {
        gl::Target& b = m_bloom[lvl];
        if (lvl > 0) {
            gl::Target& src = m_bloom[lvl - 1];
            glBindFramebuffer(GL_FRAMEBUFFER, b.fbo);
            glViewport(0, 0, b.w, b.h);
            glUseProgram(m_down);
            glBindTexture(GL_TEXTURE_2D, src.color);
            set_i(m_down, "uSrc", 0);
            glUniform2f(gl::uloc(m_down, "uTexel"), 1.0f / (float)src.w, 1.0f / (float)src.h);
            draw_fullscreen();
        }
        // Separable blur: b -> tmp (horizontal) -> b (vertical)
        glUseProgram(m_blur);
        set_i(m_blur, "uSrc", 0);
        glBindFramebuffer(GL_FRAMEBUFFER, m_tmp[lvl].fbo);
        glViewport(0, 0, b.w, b.h);
        glBindTexture(GL_TEXTURE_2D, b.color);
        glUniform2f(gl::uloc(m_blur, "uDir"), 1.0f / (float)b.w, 0.0f);
        draw_fullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, b.fbo);
        glBindTexture(GL_TEXTURE_2D, m_tmp[lvl].color);
        glUniform2f(gl::uloc(m_blur, "uDir"), 0.0f, 1.0f / (float)b.h);
        draw_fullscreen();
    }

    // Composite + tone mapping -> final RGBA8
    glBindFramebuffer(GL_FRAMEBUFFER, m_final.fbo);
    glViewport(0, 0, kRenderWidth, kRenderHeight);
    glUseProgram(m_composite);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_scene.color);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_bloom[0].color);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_bloom[1].color);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_bloom[2].color);
    set_i(m_composite, "uScene", 0);
    set_i(m_composite, "uBloom1", 1);
    set_i(m_composite, "uBloom2", 2);
    set_i(m_composite, "uBloom3", 3);
    set_f(m_composite, "uExposure", exposure);
    set_f(m_composite, "uBloomStrength", m_hdr ? bloomStrength : bloomStrength * 0.5f);
    set_f(m_composite, "uFrame", (float)(frame & 1023u));
    draw_fullscreen();
    glActiveTexture(GL_TEXTURE0);
}

void PostFx::present(int winW, int winH) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Letterbox 16:9
    int w = winW, h = (winW * kRenderHeight) / kRenderWidth;
    if (h > winH) {
        h = winH;
        w = (winH * kRenderWidth) / kRenderHeight;
    }
    glViewport((winW - w) / 2, (winH - h) / 2, w, h);
    glUseProgram(m_present);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_final.color);
    set_i(m_present, "uSrc", 0);
    draw_fullscreen();
}

bool SkyRenderer::init() {
    m_space = gl::program("space_sky", shaders::FULLSCREEN_VS, shaders::SPACE_SKY_FS, shaders::NOISE);
    m_city = gl::program("city_sky", shaders::FULLSCREEN_VS, shaders::CITY_SKY_FS, shaders::NOISE);
    glGenVertexArrays(1, &m_vao);
    return m_space && m_city && gl::check("SkyRenderer::init");
}

void SkyRenderer::destroy() {
    if (m_space) glDeleteProgram(m_space);
    if (m_city) glDeleteProgram(m_city);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_space = m_city = m_vao = 0;
}

void SkyRenderer::set_camera(GLuint prog, const CameraState& cam) {
    set_v3(prog, "uCamRight", cam.right);
    set_v3(prog, "uCamUp", cam.up);
    set_v3(prog, "uCamFwd", cam.fwd);
    glUniform2f(gl::uloc(prog, "uTanHalf"), cam.tanHalfY * cam.aspect, cam.tanHalfY);
}

void SkyRenderer::draw_space(const CameraState& cam, float time, V3 nebulaA, V3 nebulaB, float nebula) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(m_space);
    set_camera(m_space, cam);
    set_v3(m_space, "uNebulaA", nebulaA);
    set_v3(m_space, "uNebulaB", nebulaB);
    set_f(m_space, "uNebula", nebula);
    set_f(m_space, "uTime", time);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void SkyRenderer::draw_city(const CameraState& cam, float time, V3 moonDir) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(m_city);
    set_camera(m_city, cam);
    set_v3(m_city, "uMoonDir", moonDir);
    set_f(m_city, "uTime", time);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace tsb
