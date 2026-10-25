// Android host: a dedicated native render thread owns the EGL context and runs
// the shared Benchmark. Java only forwards the Surface and polls JSON status,
// so no JNI calls happen on the render thread.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "../../core/benchmark.h"
#include "../../core/platform.h"

using namespace tsb;

namespace {

enum WindowCmd { WIN_NONE = 0, WIN_SET, WIN_RELEASE };

struct App {
    plat::Mutex* mtx;
    plat::Cond* cv;
    plat::Thread* thread;
    bool started;
    // Requests from Java (guarded by mtx)
    WindowCmd windowCmd;
    ANativeWindow* pendingWindow;
    bool windowAck;
    bool startReq;
    int startMode;
    int startFrames;  // 0 = spec frame counts; >0 = smoke-test override (unofficial)
    bool abortReq;
    bool running;
    bool threadDead;
    // Published to Java (guarded by mtx)
    char status[1024];
    char result[16384];
    bool resultReady;
    char gpuInfo[768];
    u32 cpuCount;
};

App g_app;

struct RenderState {
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface pbuffer;
    EGLSurface winSurf;
    ANativeWindow* win;
};

void* get_proc(const char* name) { return (void*)eglGetProcAddress(name); }

void publish_gpu_info(const char* error) {
    StrBuf sb(g_app.gpuInfo, sizeof(g_app.gpuInfo));
    if (error) {
        sb.s("{\"ready\":false,\"error\":").json_str(error).s("}");
        return;
    }
    sb.s("{\"ready\":true,\"vendor\":").json_str((const char*)glGetString(GL_VENDOR));
    sb.s(",\"renderer\":").json_str((const char*)glGetString(GL_RENDERER));
    sb.s(",\"version\":").json_str((const char*)glGetString(GL_VERSION));
    sb.s(",\"glsl\":").json_str((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION)).s("}");
}

bool egl_init(RenderState& rs) {
    rs.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (rs.dpy == EGL_NO_DISPLAY || !eglInitialize(rs.dpy, nullptr, nullptr)) return false;
    const EGLint EGL_OPENGL_ES3_BIT_KHR_ = 0x40;
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES3_BIT_KHR_,
                            EGL_SURFACE_TYPE,
                            EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_DEPTH_SIZE,
                            0,
                            EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(rs.dpy, attrs, &rs.cfg, 1, &n) || n < 1) return false;
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    rs.ctx = eglCreateContext(rs.dpy, rs.cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (rs.ctx == EGL_NO_CONTEXT) return false;
    const EGLint pbAttrs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    rs.pbuffer = eglCreatePbufferSurface(rs.dpy, rs.cfg, pbAttrs);
    if (rs.pbuffer == EGL_NO_SURFACE) return false;
    rs.winSurf = EGL_NO_SURFACE;
    rs.win = nullptr;
    return eglMakeCurrent(rs.dpy, rs.pbuffer, rs.pbuffer, rs.ctx) == EGL_TRUE;
}

void clear_window(RenderState& rs) {
    if (rs.winSurf == EGL_NO_SURFACE) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    EGLint w = 0, h = 0;
    eglQuerySurface(rs.dpy, rs.winSurf, EGL_WIDTH, &w);
    eglQuerySurface(rs.dpy, rs.winSurf, EGL_HEIGHT, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.04f, 0.045f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(rs.dpy, rs.winSurf);
}

void release_window(RenderState& rs) {
    if (rs.winSurf != EGL_NO_SURFACE) {
        eglMakeCurrent(rs.dpy, rs.pbuffer, rs.pbuffer, rs.ctx);
        eglDestroySurface(rs.dpy, rs.winSurf);
        rs.winSurf = EGL_NO_SURFACE;
    }
    if (rs.win) {
        ANativeWindow_release(rs.win);
        rs.win = nullptr;
    }
}

void set_window(RenderState& rs, ANativeWindow* win) {
    release_window(rs);
    if (!win) return;
    EGLint format = 0;
    eglGetConfigAttrib(rs.dpy, rs.cfg, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(win, 0, 0, format);
    rs.winSurf = eglCreateWindowSurface(rs.dpy, rs.cfg, win, nullptr);
    if (rs.winSurf == EGL_NO_SURFACE) {
        ANativeWindow_release(win);
        return;
    }
    rs.win = win;
    eglMakeCurrent(rs.dpy, rs.winSurf, rs.winSurf, rs.ctx);
    eglSwapInterval(rs.dpy, 0);  // preview presents are throttled to 20 Hz anyway
    clear_window(rs);
}

void publish_result(Benchmark* bench) {
    StrBuf sb(g_app.result, sizeof(g_app.result));
    bench->result_json(sb);
    g_app.resultReady = true;
}

void render_main(void*) {
    RenderState rs;
    if (!egl_init(rs)) {
        plat::mutex_lock(g_app.mtx);
        publish_gpu_info("EGL / OpenGL ES 3.0 initialisation failed");
        g_app.threadDead = true;  // nobody will acknowledge window requests any more
        if (g_app.pendingWindow) ANativeWindow_release(g_app.pendingWindow);
        g_app.pendingWindow = nullptr;
        g_app.running = false;
        plat::cond_broadcast(g_app.cv);
        plat::mutex_unlock(g_app.mtx);
        plat::log_line("EGL init failed");
        return;
    }
    plat::mutex_lock(g_app.mtx);
    publish_gpu_info(nullptr);
    plat::mutex_unlock(g_app.mtx);

    Benchmark* bench = nullptr;
    char statusTmp[1024];
    for (;;) {
        // Collect requests
        plat::mutex_lock(g_app.mtx);
        // Sleep unless there is a request, or a benchmark that can run (it needs the window).
        while ((!bench || rs.winSurf == EGL_NO_SURFACE) && g_app.windowCmd == WIN_NONE && !g_app.startReq &&
               !g_app.abortReq)
            plat::cond_wait(g_app.cv, g_app.mtx);
        WindowCmd cmd = g_app.windowCmd;
        ANativeWindow* newWin = g_app.pendingWindow;
        bool start = g_app.startReq;
        int mode = g_app.startMode;
        int frames = g_app.startFrames;
        bool abortNow = g_app.abortReq;
        g_app.windowCmd = WIN_NONE;
        g_app.pendingWindow = nullptr;
        g_app.startReq = false;
        g_app.abortReq = false;
        plat::mutex_unlock(g_app.mtx);

        if (abortNow && bench) {
            plat::log_line("Stop requested: aborting benchmark");
            bench->abort();
            plat::mutex_lock(g_app.mtx);
            publish_result(bench);
            g_app.running = false;
            plat::mutex_unlock(g_app.mtx);
            bench->shutdown();
            delete bench;
            bench = nullptr;
        }
        if (cmd != WIN_NONE) {
            // A lost or replaced window cancels only the test in progress; it is
            // re-run from the start once a window is available again.
            if (bench && rs.winSurf != EGL_NO_SURFACE) {
                plat::log_line(cmd == WIN_RELEASE ? "Surface destroyed during run: pausing"
                                                  : "Surface replaced during run: restarting current test");
                bench->interrupt();
            }
            if (cmd == WIN_RELEASE) release_window(rs);
            if (cmd == WIN_SET) {
                set_window(rs, newWin);
                plat::log_line(rs.winSurf != EGL_NO_SURFACE ? "Surface attached" : "Surface attach failed");
            }
            plat::mutex_lock(g_app.mtx);
            g_app.windowAck = true;
            g_app.running = bench != nullptr;
            if (bench && rs.winSurf == EGL_NO_SURFACE) {
                StrBuf st(g_app.status, sizeof(g_app.status));
                st.s("{\"stage\":\"paused\"}");
            }
            plat::cond_broadcast(g_app.cv);
            plat::mutex_unlock(g_app.mtx);
        }

        if (start && (bench || rs.winSurf == EGL_NO_SURFACE)) {
            // Cannot start without a window (or already running): report back to Java.
            plat::mutex_lock(g_app.mtx);
            g_app.running = bench != nullptr;
            if (!bench) {
                StrBuf st(g_app.status, sizeof(g_app.status));
                st.s("{\"stage\":\"failed\",\"error\":\"no surface\"}");
            }
            plat::mutex_unlock(g_app.mtx);
        } else if (start) {
            BenchOptions opt;
            opt.mode = mode == 1 ? MODE_QUICK : MODE_STANDARD;
            opt.cpuCount = g_app.cpuCount;
            opt.platformName = "android";
            opt.frameOverride = frames > 0 ? (u32)frames : 0;
            bench = new Benchmark();
            if (bench->init(get_proc, opt)) {
                bench->start();
                plat::log_line(opt.frameOverride ? "Benchmark started (frame override, unofficial)"
                                                 : "Benchmark started");
            }
            plat::mutex_lock(g_app.mtx);
            g_app.resultReady = false;
            g_app.running = true;
            plat::mutex_unlock(g_app.mtx);
        }

        if (bench && rs.winSurf != EGL_NO_SURFACE) {
            EGLint w = 0, h = 0;
            eglQuerySurface(rs.dpy, rs.winSurf, EGL_WIDTH, &w);
            eglQuerySurface(rs.dpy, rs.winSurf, EGL_HEIGHT, &h);
            if (!bench->finished() && bench->tick(w, h)) {
                eglSwapBuffers(rs.dpy, rs.winSurf);
            }
            StrBuf sb(statusTmp, sizeof(statusTmp));
            bench->status_json(sb);
            plat::mutex_lock(g_app.mtx);
            memcpy(g_app.status, statusTmp, sb.len + 1);
            if (bench->finished()) {
                publish_result(bench);
                g_app.running = false;
            }
            plat::mutex_unlock(g_app.mtx);
            if (bench->finished()) {
                bench->shutdown();
                delete bench;
                bench = nullptr;
            }
        }
    }
}

jstring to_jstring(JNIEnv* env, const char* s) { return env->NewStringUTF(s); }

}  // namespace

extern "C" {

JNIEXPORT void JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeInit(JNIEnv*, jclass, jint cpuCount) {
    if (g_app.started) return;
    g_app.mtx = plat::mutex_create();
    g_app.cv = plat::cond_create();
    g_app.cpuCount = cpuCount > 0 ? (u32)cpuCount : 1;
    StrBuf st(g_app.status, sizeof(g_app.status));
    st.s("{\"stage\":\"idle\"}");
    StrBuf gi(g_app.gpuInfo, sizeof(g_app.gpuInfo));
    gi.s("{\"ready\":false}");
    g_app.started = true;
    g_app.thread = plat::thread_start(render_main, nullptr);
}

JNIEXPORT void JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeSetSurface(JNIEnv* env, jclass, jobject surface) {
    if (!g_app.started) return;
    ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    plat::mutex_lock(g_app.mtx);
    if (g_app.threadDead) {
        plat::mutex_unlock(g_app.mtx);
        if (win) ANativeWindow_release(win);
        return;
    }
    if (g_app.pendingWindow) ANativeWindow_release(g_app.pendingWindow);
    g_app.windowCmd = win ? WIN_SET : WIN_RELEASE;
    g_app.pendingWindow = win;
    g_app.windowAck = false;
    plat::cond_broadcast(g_app.cv);
    // surfaceDestroyed must not return before native code stopped using the surface.
    while (!g_app.windowAck && !g_app.threadDead) plat::cond_wait(g_app.cv, g_app.mtx);
    plat::mutex_unlock(g_app.mtx);
}

JNIEXPORT jboolean JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeStart(JNIEnv*, jclass, jint mode,
                                                                              jint frames) {
    if (!g_app.started) return JNI_FALSE;
    plat::mutex_lock(g_app.mtx);
    bool ok = !g_app.running && !g_app.threadDead;
    if (ok) {
        g_app.startReq = true;
        g_app.startMode = mode;
        g_app.startFrames = frames;
        g_app.resultReady = false;
        g_app.running = true;
        StrBuf st(g_app.status, sizeof(g_app.status));
        st.s("{\"stage\":\"loading\",\"progress\":0}");
        plat::cond_broadcast(g_app.cv);
    }
    plat::mutex_unlock(g_app.mtx);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeAbort(JNIEnv*, jclass) {
    if (!g_app.started) return;
    plat::mutex_lock(g_app.mtx);
    g_app.abortReq = true;
    plat::cond_broadcast(g_app.cv);
    plat::mutex_unlock(g_app.mtx);
}

JNIEXPORT jboolean JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeIsRunning(JNIEnv*, jclass) {
    if (!g_app.started) return JNI_FALSE;
    plat::mutex_lock(g_app.mtx);
    bool r = g_app.running;
    plat::mutex_unlock(g_app.mtx);
    return r ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeStatus(JNIEnv* env, jclass) {
    if (!g_app.started) return to_jstring(env, "{}");
    plat::mutex_lock(g_app.mtx);
    jstring s = to_jstring(env, g_app.status);
    plat::mutex_unlock(g_app.mtx);
    return s;
}

JNIEXPORT jstring JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeTakeResult(JNIEnv* env, jclass) {
    if (!g_app.started) return nullptr;
    plat::mutex_lock(g_app.mtx);
    jstring s = nullptr;
    if (g_app.resultReady) {
        s = to_jstring(env, g_app.result);
        g_app.resultReady = false;
    }
    plat::mutex_unlock(g_app.mtx);
    return s;
}

JNIEXPORT jstring JNICALL Java_com_plpp555_trisync3d_NativeBridge_nativeGpuInfo(JNIEnv* env, jclass) {
    if (!g_app.started) return to_jstring(env, "{}");
    plat::mutex_lock(g_app.mtx);
    jstring s = to_jstring(env, g_app.gpuInfo);
    plat::mutex_unlock(g_app.mtx);
    return s;
}

}  // extern "C"
