/* Minimal bionic <time.h> subset (offline build only). */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long time_t; /* __kernel_time_t */
typedef int clockid_t;
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
int clock_gettime(clockid_t clock, struct timespec* ts);
int nanosleep(const struct timespec* req, struct timespec* rem);
#ifdef __cplusplus
}
#endif
