// TriSync 3D Benchmark - shared core
// Basic types and containers. The core deliberately avoids the C++ standard
// library so that the exact same code (and therefore the exact same math)
// builds on Android (NDK or freestanding clang), Windows (MSVC) and
// Windows 10 Mobile (UWP/ARM) without depending on platform libm/STL quirks.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace tsb {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

template <class T> inline T tmin(T a, T b) { return a < b ? a : b; }
template <class T> inline T tmax(T a, T b) { return a > b ? a : b; }
template <class T> inline T tclamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> inline void tswap(T& a, T& b) { T t = a; a = b; b = t; }

// Implemented by the platform layer.
void* mem_alloc(size_t bytes, size_t align = 64);
void mem_free(void* p);

template <class T> inline T* alloc_array(size_t n) {
    T* p = (T*)mem_alloc(sizeof(T) * (n ? n : 1));
    memset(p, 0, sizeof(T) * (n ? n : 1));
    return p;
}

// Growable array for trivially copyable types only.
template <class T> struct Array {
    T* data = nullptr;
    u32 count = 0;
    u32 capacity = 0;

    Array() {}
    ~Array() { release(); }
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    void reserve(u32 n) {
        if (n <= capacity) return;
        T* nd = (T*)mem_alloc(sizeof(T) * n);
        if (count) memcpy(nd, data, sizeof(T) * count);
        mem_free(data);
        data = nd;
        capacity = n;
    }
    void resize(u32 n) {
        reserve(n);
        if (n > count) memset(data + count, 0, sizeof(T) * (n - count));
        count = n;
    }
    void push(const T& v) {
        if (count == capacity) reserve(capacity ? capacity * 2 : 16);
        data[count++] = v;
    }
    void clear() { count = 0; }
    void release() {
        mem_free(data);
        data = nullptr;
        count = capacity = 0;
    }
    T& operator[](u32 i) { return data[i]; }
    const T& operator[](u32 i) const { return data[i]; }
};

// Small helper so the core never needs libc formatting (snprintf differs
// between platforms for floats).
struct StrBuf {
    char* buf;
    u32 cap;
    u32 len;

    StrBuf(char* storage, u32 capacity) : buf(storage), cap(capacity), len(0) {
        if (cap) buf[0] = 0;
    }
    void clear() {
        len = 0;
        if (cap) buf[0] = 0;
    }
    StrBuf& s(const char* str);
    StrBuf& c(char ch);
    StrBuf& i(i64 v);
    StrBuf& u(u64 v);
    StrBuf& hex64(u64 v);
    StrBuf& f(double v, int decimals);
    StrBuf& json_str(const char* str);  // quoted + escaped
    const char* c_str() const { return buf; }
};

}  // namespace tsb
