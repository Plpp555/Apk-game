// Platform layer for Android (NDK or offline sysroot) and Linux.
#include "../../core/platform.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

#if defined(__ANDROID__)
#include <android/log.h>
#else
#include <stdio.h>
#endif

namespace tsb {

void* mem_alloc(size_t bytes, size_t align) {
    if (align < sizeof(void*)) align = sizeof(void*);
    void* p = nullptr;
    if (posix_memalign(&p, align, bytes ? bytes : 1) != 0) return nullptr;
    return p;
}

void mem_free(void* p) { free(p); }

namespace plat {

u64 time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

void sleep_us(u32 us) {
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000u);
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, nullptr);
}

void yield_thread() { sched_yield(); }

void cpu_relax() {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

struct Thread {
    pthread_t handle;
    ThreadFn fn;
    void* arg;
};

static void* thread_trampoline(void* p) {
    Thread* t = (Thread*)p;
    t->fn(t->arg);
    return nullptr;
}

Thread* thread_start(ThreadFn fn, void* arg) {
    Thread* t = (Thread*)mem_alloc(sizeof(Thread));
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->handle, nullptr, thread_trampoline, t) != 0) {
        mem_free(t);
        return nullptr;
    }
    return t;
}

void thread_join(Thread* t) {
    if (!t) return;
    pthread_join(t->handle, nullptr);
    mem_free(t);
}

struct Mutex {
    pthread_mutex_t m;
};

Mutex* mutex_create() {
    Mutex* m = (Mutex*)mem_alloc(sizeof(Mutex));
    pthread_mutex_init(&m->m, nullptr);
    return m;
}
void mutex_destroy(Mutex* m) {
    if (!m) return;
    pthread_mutex_destroy(&m->m);
    mem_free(m);
}
void mutex_lock(Mutex* m) { pthread_mutex_lock(&m->m); }
void mutex_unlock(Mutex* m) { pthread_mutex_unlock(&m->m); }

struct Cond {
    pthread_cond_t c;
};

Cond* cond_create() {
    Cond* c = (Cond*)mem_alloc(sizeof(Cond));
    pthread_cond_init(&c->c, nullptr);
    return c;
}
void cond_destroy(Cond* c) {
    if (!c) return;
    pthread_cond_destroy(&c->c);
    mem_free(c);
}
void cond_wait(Cond* c, Mutex* m) { pthread_cond_wait(&c->c, &m->m); }
void cond_broadcast(Cond* c) { pthread_cond_broadcast(&c->c); }

void log_line(const char* msg) {
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "TriSync3D", msg);
#else
    fputs(msg, stderr);
    fputc('\n', stderr);
#endif
}

}  // namespace plat
}  // namespace tsb
