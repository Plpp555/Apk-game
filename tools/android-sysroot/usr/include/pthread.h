/* Minimal bionic <pthread.h> subset (offline build only).
 * Type layouts follow bionic's <bits/pthread_types.h>. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long pthread_t;
typedef long pthread_mutexattr_t;
typedef long pthread_condattr_t;
typedef struct {
    uint32_t flags;
    void* stack_base;
    size_t stack_size;
    size_t guard_size;
    int32_t sched_policy;
    int32_t sched_priority;
#if defined(__LP64__)
    char __reserved[16];
#endif
} pthread_attr_t;
typedef struct {
#if defined(__LP64__)
    int32_t __private[10];
#else
    int32_t __private[1];
#endif
} pthread_mutex_t;
typedef struct {
#if defined(__LP64__)
    int32_t __private[12];
#else
    int32_t __private[1];
#endif
} pthread_cond_t;

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
int pthread_join(pthread_t thread, void** ret);
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);
int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* c);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
int pthread_cond_broadcast(pthread_cond_t* c);
int pthread_cond_signal(pthread_cond_t* c);
#ifdef __cplusplus
}
#endif
