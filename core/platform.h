// Platform abstraction. Each OS provides one implementation:
//   platform/posix/platform_posix.cpp   Android + Linux (pthreads)
//   platform/windows/platform_win32.cpp Windows desktop + UWP (planned)
#pragma once

#include "base.h"

namespace tsb {
namespace plat {

u64 time_ns();  // monotonic clock
void sleep_us(u32 us);
void yield_thread();
void cpu_relax();  // spin-wait hint

struct Thread;
typedef void (*ThreadFn)(void* arg);
Thread* thread_start(ThreadFn fn, void* arg);
void thread_join(Thread* t);

struct Mutex;
Mutex* mutex_create();
void mutex_destroy(Mutex* m);
void mutex_lock(Mutex* m);
void mutex_unlock(Mutex* m);

struct Cond;
Cond* cond_create();
void cond_destroy(Cond* c);
void cond_wait(Cond* c, Mutex* m);
void cond_broadcast(Cond* c);

void log_line(const char* msg);

}  // namespace plat

struct ScopedLock {
    plat::Mutex* m;
    explicit ScopedLock(plat::Mutex* mm) : m(mm) { plat::mutex_lock(m); }
    ~ScopedLock() { plat::mutex_unlock(m); }
};

inline double ns_to_ms(u64 ns) { return (double)ns * 1.0e-6; }
inline double ns_to_s(u64 ns) { return (double)ns * 1.0e-9; }

}  // namespace tsb
