// Minimal C++ runtime for libtrisync3d.so. The library is built without a C++
// standard library (ANDROID_STL=none / offline build), so it provides the few
// symbols the compiler expects: operator new/delete and __cxa_pure_virtual.
#include <stddef.h>
#include <stdlib.h>

void* operator new(size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}
void* operator new[](size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

extern "C" void __cxa_pure_virtual() { abort(); }
