/* Minimal bionic <stdlib.h> subset (offline build only). */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* p, size_t size);
void free(void* p);
int posix_memalign(void** out, size_t alignment, size_t size);
void abort(void) __attribute__((__noreturn__));
#ifdef __cplusplus
}
#endif
