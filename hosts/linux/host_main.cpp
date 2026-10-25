// Headless Linux host (EGL pbuffer, Mesa or any GLES 3.0 driver).
// Runs the exact same Benchmark as the Android app; used for development,
// CI smoke tests and screenshots. It is also the template for the Windows
// host (EGL via ANGLE): only window creation differs.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../core/benchmark.h"
#include "../../core/platform.h"

using namespace tsb;

static void* get_proc(const char* name) { return (void*)eglGetProcAddress(name); }

static void write_ppm(const char* path, int w, int h) {
    unsigned char* px = (unsigned char*)malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE* f = fopen(path, "wb");
    if (!f) {
        free(px);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
    fclose(f);
    free(px);
    fprintf(stderr, "wrote %s\n", path);
}

int main(int argc, char** argv) {
    BenchOptions opt;
    opt.platformName = "linux-egl";
    opt.cpuCount = (u32)sysconf(_SC_NPROCESSORS_ONLN);
    const char* dumpDir = nullptr;
    const char* jsonOut = nullptr;
    int winW = 960, winH = 540;
    int interruptAt = -1;  // testing: simulate a lost window after N ticks
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--quick")) opt.mode = MODE_QUICK;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) opt.frameOverride = (u32)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mask") && i + 1 < argc) opt.testMask = (u32)strtoul(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) opt.workerThreads = (u32)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dumpDir = argv[++i];
        else if (!strcmp(argv[i], "--json") && i + 1 < argc) jsonOut = argv[++i];
        else if (!strcmp(argv[i], "--no-present")) opt.present = false;
        else if (!strcmp(argv[i], "--interrupt-at") && i + 1 < argc) interruptAt = atoi(argv[++i]);
        else {
            fprintf(stderr,
                    "usage: %s [--quick] [--frames N] [--mask 0xF] [--threads N] [--dump DIR] [--json FILE] "
                    "[--no-present] [--interrupt-at TICK]\n",
                    argv[0]);
            return 1;
        }
    }

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        fprintf(stderr, "eglInitialize failed\n");
        return 1;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                              EGL_RED_SIZE,     8,               EGL_GREEN_SIZE,      8,
                              EGL_BLUE_SIZE,    8,               EGL_ALPHA_SIZE,      8,
                              EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) {
        fprintf(stderr, "no ES3 pbuffer config\n");
        return 1;
    }
    const EGLint pbAttr[] = {EGL_WIDTH, winW, EGL_HEIGHT, winH, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        fprintf(stderr, "context creation failed\n");
        return 1;
    }

    Benchmark* bench = new Benchmark();
    if (!bench->init(get_proc, opt)) return 1;
    fprintf(stderr, "GL_RENDERER: %s\nGL_VERSION: %s\n", bench->caps().renderer, bench->caps().version);
    bench->start();
    int lastDumped = -1;
    static char status[1024];
    u64 lastStatus = 0;
    int ticks = 0;
    while (!bench->finished()) {
        if (ticks++ == interruptAt) bench->interrupt();
        int ti = bench->test_index();
        bool half = bench->frame_index() * 2 >= bench->frame_total();
        bool present = bench->tick(winW, winH);
        if (present) {
            if (dumpDir && ti != lastDumped && half) {
                char path[512];
                snprintf(path, sizeof(path), "%s/test%d.ppm", dumpDir, ti);
                write_ppm(path, winW, winH);
                lastDumped = ti;
            }
            eglSwapBuffers(dpy, surf);
        }
        u64 now = plat::time_ns();
        if (now - lastStatus > 2000000000ull) {
            StrBuf sb(status, sizeof(status));
            bench->status_json(sb);
            fprintf(stderr, "%s\n", status);
            lastStatus = now;
        }
    }
    static char result[16384];
    StrBuf sb(result, sizeof(result));
    bench->result_json(sb);
    printf("%s\n", result);
    fprintf(stderr, "interruptions: %u\n", bench->interruptions());
    if (jsonOut) {
        FILE* f = fopen(jsonOut, "w");
        if (f) {
            fputs(result, f);
            fclose(f);
        }
    }
    int rc = bench->stage() == STAGE_DONE ? 0 : 2;
    bench->shutdown();
    delete bench;
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    return rc;
}
