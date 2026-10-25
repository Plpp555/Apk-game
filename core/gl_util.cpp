#include "gl_util.h"

#include "platform.h"

namespace tsb {
namespace gl {

typedef void(GL_APIENTRY* PFN_GenQueries)(GLsizei, GLuint*);
typedef void(GL_APIENTRY* PFN_DeleteQueries)(GLsizei, const GLuint*);
typedef void(GL_APIENTRY* PFN_BeginQuery)(GLenum, GLuint);
typedef void(GL_APIENTRY* PFN_EndQuery)(GLenum);
typedef void(GL_APIENTRY* PFN_GetQueryObjectuiv)(GLuint, GLenum, GLuint*);
typedef void(GL_APIENTRY* PFN_GetQueryObjectui64v)(GLuint, GLenum, GLuint64*);

static PFN_GenQueries s_genQueries;
static PFN_DeleteQueries s_deleteQueries;
static PFN_BeginQuery s_beginQuery;
static PFN_EndQuery s_endQuery;
static PFN_GetQueryObjectuiv s_getQueryObjectuiv;
static PFN_GetQueryObjectui64v s_getQueryObjectui64v;
static bool s_timerQuery = false;

static void copy_str(char* dst, u32 cap, const GLubyte* src) {
    const char* s = src ? (const char*)src : "unknown";
    u32 i = 0;
    for (; s[i] && i + 1 < cap; ++i) dst[i] = s[i];
    dst[i] = 0;
}

static bool has_extension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) return true;
    }
    return false;
}

bool init(GetProcFn getProc, Caps& caps) {
    copy_str(caps.vendor, sizeof(caps.vendor), glGetString(GL_VENDOR));
    copy_str(caps.renderer, sizeof(caps.renderer), glGetString(GL_RENDERER));
    copy_str(caps.version, sizeof(caps.version), glGetString(GL_VERSION));
    copy_str(caps.glsl, sizeof(caps.glsl), glGetString(GL_SHADING_LANGUAGE_VERSION));
    caps.maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.maxTextureSize);
    s_timerQuery = false;
    if (getProc && has_extension("GL_EXT_disjoint_timer_query")) {
        s_genQueries = (PFN_GenQueries)getProc("glGenQueriesEXT");
        s_deleteQueries = (PFN_DeleteQueries)getProc("glDeleteQueriesEXT");
        s_beginQuery = (PFN_BeginQuery)getProc("glBeginQueryEXT");
        s_endQuery = (PFN_EndQuery)getProc("glEndQueryEXT");
        s_getQueryObjectuiv = (PFN_GetQueryObjectuiv)getProc("glGetQueryObjectuivEXT");
        s_getQueryObjectui64v = (PFN_GetQueryObjectui64v)getProc("glGetQueryObjectui64vEXT");
        s_timerQuery = s_genQueries && s_deleteQueries && s_beginQuery && s_endQuery && s_getQueryObjectuiv &&
                       s_getQueryObjectui64v;
    }
    caps.timerQuery = s_timerQuery;
    // Clear any error left over from extension probing
    while (glGetError() != GL_NO_ERROR) {
    }
    return true;
}

bool check(const char* where) {
    GLenum e = glGetError();
    if (e == GL_NO_ERROR) return true;
    char tmp[160];
    StrBuf sb(tmp, sizeof(tmp));
    sb.s("GL error 0x").hex64(e).s(" at ").s(where);
    plat::log_line(sb.c_str());
    while (glGetError() != GL_NO_ERROR) {
    }
    return false;
}

static const char* kHeaderVS =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n";
static const char* kHeaderFS =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "precision highp usampler2D;\n";

static GLuint compile(GLenum type, const char* name, const char* prefix, const char* body) {
    GLuint s = glCreateShader(type);
    const char* srcs[3] = {type == GL_VERTEX_SHADER ? kHeaderVS : kHeaderFS, prefix ? prefix : "", body};
    glShaderSource(s, 3, srcs, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetShaderInfoLog(s, sizeof(log) - 1, &len, log);
        log[len] = 0;
        char tmp[1400];
        StrBuf sb(tmp, sizeof(tmp));
        sb.s("Shader compile failed: ").s(name).s(type == GL_VERTEX_SHADER ? " (VS)\n" : " (FS)\n").s(log);
        plat::log_line(sb.c_str());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint program(const char* name, const char* vsBody, const char* fsBody, const char* fsPrefix) {
    GLuint vs = compile(GL_VERTEX_SHADER, name, nullptr, vsBody);
    GLuint fs = compile(GL_FRAGMENT_SHADER, name, fsPrefix, fsBody);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei len = 0;
        glGetProgramInfoLog(p, sizeof(log) - 1, &len, log);
        log[len] = 0;
        char tmp[1400];
        StrBuf sb(tmp, sizeof(tmp));
        sb.s("Program link failed: ").s(name).s("\n").s(log);
        plat::log_line(sb.c_str());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

GLint uloc(GLuint prog, const char* name) { return glGetUniformLocation(prog, name); }

bool target_create(Target& t, int w, int h, GLenum internalFormat, GLenum format, GLenum type, bool withDepth) {
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    if (withDepth) {
        glGenRenderbuffers(1, &t.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    }
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        char tmp[96];
        StrBuf sb(tmp, sizeof(tmp));
        sb.s("Framebuffer incomplete: 0x").hex64(st);
        plat::log_line(sb.c_str());
        return false;
    }
    return check("target_create");
}

void target_destroy(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.color) glDeleteTextures(1, &t.color);
    if (t.depth) glDeleteRenderbuffers(1, &t.depth);
    t.fbo = t.color = t.depth = 0;
}

bool StreamRing::create(GLenum tgt, u32 size) {
    target = tgt;
    bytes = size;
    glGenBuffers(SLOTS, buf);
    for (int i = 0; i < SLOTS; ++i) {
        glBindBuffer(target, buf[i]);
        glBufferData(target, size, nullptr, GL_STREAM_DRAW);
        staging[i] = nullptr;
    }
    glBindBuffer(target, 0);
    useStaging = false;
    return check("StreamRing::create");
}

void StreamRing::destroy() {
    if (buf[0]) glDeleteBuffers(SLOTS, buf);
    for (int i = 0; i < SLOTS; ++i) {
        buf[i] = 0;
        mem_free(staging[i]);
        staging[i] = nullptr;
    }
}

void* StreamRing::map(int slot) {
    if (!useStaging) {
        glBindBuffer(target, buf[slot]);
        void* p = glMapBufferRange(target, 0, bytes,
                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        glBindBuffer(target, 0);
        if (p) return p;
        // Mapping unsupported or failed: switch permanently to the staging path.
        while (glGetError() != GL_NO_ERROR) {
        }
        plat::log_line("glMapBufferRange failed; using glBufferSubData staging path");
        useStaging = true;
    }
    if (!staging[slot]) staging[slot] = mem_alloc(bytes);
    return staging[slot];
}

void StreamRing::unmap(int slot) {
    glBindBuffer(target, buf[slot]);
    if (useStaging) {
        glBufferSubData(target, 0, bytes, staging[slot]);
    } else {
        glUnmapBuffer(target);
    }
    glBindBuffer(target, 0);
}

u64 FrameFences::wait(int slot) {
    if (!sync[slot]) return 0;
    u64 t0 = plat::time_ns();
    for (;;) {
        GLenum r = glClientWaitSync(sync[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED || r == GL_WAIT_FAILED) break;
    }
    glDeleteSync(sync[slot]);
    sync[slot] = 0;
    return plat::time_ns() - t0;
}

void FrameFences::signal(int slot) {
    if (sync[slot]) glDeleteSync(sync[slot]);
    sync[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void FrameFences::destroy() {
    for (int i = 0; i < StreamRing::SLOTS; ++i) {
        if (sync[i]) glDeleteSync(sync[i]);
        sync[i] = 0;
    }
}

void GpuTimer::create() {
    enabled = s_timerQuery;
    if (!enabled) return;
    s_genQueries(RING, q);
    for (int i = 0; i < RING; ++i) pending[i] = false;
    head = 0;
    reset_stats();
}

void GpuTimer::destroy() {
    if (enabled && q[0]) s_deleteQueries(RING, q);
    for (int i = 0; i < RING; ++i) q[i] = 0;
    enabled = false;
}

void GpuTimer::begin() {
    if (!enabled) return;
    poll();
    if (pending[head]) return;  // ring full; skip sampling this frame
    s_beginQuery(GL_TIME_ELAPSED_EXT, q[head]);
}

void GpuTimer::end() {
    if (!enabled || pending[head]) return;
    s_endQuery(GL_TIME_ELAPSED_EXT);
    pending[head] = true;
    head = (head + 1) % RING;
}

void GpuTimer::poll() {
    if (!enabled) return;
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    for (int i = 0; i < RING; ++i) {
        if (!pending[i]) continue;
        GLuint avail = 0;
        s_getQueryObjectuiv(q[i], GL_QUERY_RESULT_AVAILABLE, &avail);
        if (!avail) continue;
        GLuint64 ns = 0;
        s_getQueryObjectui64v(q[i], GL_QUERY_RESULT, &ns);
        pending[i] = false;
        if (!disjoint) {
            sumMs += (double)ns * 1.0e-6;
            samples++;
        }
    }
}

}  // namespace gl
}  // namespace tsb
