#pragma once

#include "base.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace tsb {

#if defined(_MSC_VER) && !defined(__clang__)
inline u32 atomic_load(const volatile u32* p) {
    u32 v = *p;
    _ReadWriteBarrier();
    return v;
}
inline void atomic_store(volatile u32* p, u32 v) {
    _ReadWriteBarrier();
    _InterlockedExchange((volatile long*)p, (long)v);
}
inline u32 atomic_add(volatile u32* p, u32 v) {  // returns previous value
    return (u32)_InterlockedExchangeAdd((volatile long*)p, (long)v);
}
inline u32 atomic_sub(volatile u32* p, u32 v) { return (u32)_InterlockedExchangeAdd((volatile long*)p, -(long)v); }
inline u64 atomic_add64(volatile u64* p, u64 v) {
    return (u64)_InterlockedExchangeAdd64((volatile long long*)p, (long long)v);
}
inline u64 atomic_load64(const volatile u64* p) {
    return (u64)_InterlockedCompareExchange64((volatile long long*)p, 0, 0);
}
inline void atomic_store64(volatile u64* p, u64 v) { _InterlockedExchange64((volatile long long*)p, (long long)v); }
inline u64 atomic_load64_acq(const volatile u64* p) {
    return (u64)_InterlockedCompareExchange64((volatile long long*)p, 0, 0);
}
inline void atomic_store64_rel(volatile u64* p, u64 v) { _InterlockedExchange64((volatile long long*)p, (long long)v); }
inline bool atomic_cas64(volatile u64* p, u64 expected, u64 desired) {
    return (u64)_InterlockedCompareExchange64((volatile long long*)p, (long long)desired, (long long)expected) ==
           expected;
}
#else
inline u32 atomic_load(const volatile u32* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void atomic_store(volatile u32* p, u32 v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
inline u32 atomic_add(volatile u32* p, u32 v) { return __atomic_fetch_add(p, v, __ATOMIC_ACQ_REL); }
inline u32 atomic_sub(volatile u32* p, u32 v) { return __atomic_fetch_sub(p, v, __ATOMIC_ACQ_REL); }
inline u64 atomic_add64(volatile u64* p, u64 v) { return __atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
inline u64 atomic_load64(const volatile u64* p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
inline void atomic_store64(volatile u64* p, u64 v) { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
inline u64 atomic_load64_acq(const volatile u64* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void atomic_store64_rel(volatile u64* p, u64 v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
inline bool atomic_cas64(volatile u64* p, u64 expected, u64 desired) {
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
#endif

}  // namespace tsb
