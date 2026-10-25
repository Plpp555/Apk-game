// OpenGL ES 3.0 helpers shared by all platforms.
// Android: native GLES3. Windows / Windows 10 Mobile: the same API through
// ANGLE (GLES3 -> Direct3D 11), so shaders and passes stay identical.
#pragma once

#include <GLES3/gl3.h>

#include "base.h"

// EXT_disjoint_timer_query (optional, informational GPU timing only)
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

namespace tsb {
namespace gl {

typedef void* (*GetProcFn)(const char* name);

struct Caps {
    char vendor[128];
    char renderer[160];
    char version[128];
    char glsl[96];
    bool timerQuery;
    int maxTextureSize;
};

bool init(GetProcFn getProc, Caps& caps);
bool check(const char* where);  // logs and returns false on GL error

GLuint program(const char* name, const char* vsBody, const char* fsBody, const char* fsPrefix = nullptr);
GLint uloc(GLuint prog, const char* name);

struct Target {
    GLuint fbo = 0;
    GLuint color = 0;
    GLuint depth = 0;
    int w = 0, h = 0;
};
bool target_create(Target& t, int w, int h, GLenum internalFormat, GLenum format, GLenum type, bool withDepth);
void target_destroy(Target& t);

// Ring of N buffers for streaming per-frame data written by worker threads.
// A slot is mapped (unsynchronized; reuse is guarded by FrameFences) while the
// GPU still reads the other slots. Falls back to CPU staging + glBufferSubData
// when mapping is unavailable.
struct StreamRing {
    enum { SLOTS = 3 };
    GLenum target = 0;
    GLuint buf[SLOTS] = {0, 0, 0};
    void* staging[SLOTS] = {nullptr, nullptr, nullptr};
    u32 bytes = 0;
    bool useStaging = false;

    bool create(GLenum target, u32 bytes);
    void destroy();
    void* map(int slot);
    void unmap(int slot);
};

struct FrameFences {
    GLsync sync[StreamRing::SLOTS] = {0, 0, 0};
    u64 wait(int slot);  // returns ns spent waiting
    void signal(int slot);
    void destroy();
};

// GPU frame timer using EXT_disjoint_timer_query (non-blocking readback).
struct GpuTimer {
    enum { RING = 4 };
    GLuint q[RING] = {0, 0, 0, 0};
    bool pending[RING] = {false, false, false, false};
    u32 head = 0;
    double sumMs = 0.0;
    u32 samples = 0;
    bool enabled = false;

    void create();
    void destroy();
    void begin();
    void end();
    void poll();
    void reset_stats() {
        sumMs = 0.0;
        samples = 0;
    }
};

}  // namespace gl
}  // namespace tsb
