#ifndef PS2RE_TYPES_H
#define PS2RE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>

/* ── Fixed-width aliases ─────────────────────────────── */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;
typedef float     f32;
typedef double    f64;

/* ── Alignment helpers ───────────────────────────────── */
#define CACHE_LINE       64
#define ALIGN_CACHE      __attribute__((aligned(CACHE_LINE)))
#define ALIGN16          __attribute__((aligned(16)))
#define ALIGN64          __attribute__((aligned(64)))
#define LIKELY(x)        __builtin_expect(!!(x), 1)
#define UNLIKELY(x)      __builtin_expect(!!(x), 0)
#define PREFETCH_R(ptr)  __builtin_prefetch((ptr), 0, 3)
#define PREFETCH_W(ptr)  __builtin_prefetch((ptr), 1, 3)
#define RESTRICT         __restrict
#define FORCE_INLINE     static inline __attribute__((always_inline))
#define NORETURN         __attribute__((noreturn))

/* ── Atomics shortcuts ───────────────────────────────── */
#include <stdatomic.h>

#define atomic_load_rlx(x)      atomic_load_explicit((x), memory_order_relaxed)
#define atomic_load_acq(x)      atomic_load_explicit((x), memory_order_acquire)
#define atomic_store_rlx(x, v)  atomic_store_explicit((x), (v), memory_order_relaxed)
#define atomic_store_rel(x, v)  atomic_store_explicit((x), (v), memory_order_release)
#define atomic_fetch_add_rlx(x, v) atomic_fetch_add_explicit((x), (v), memory_order_relaxed)
#define atomic_fetch_sub_acq_rel(x, v) atomic_fetch_sub_explicit((x), (v), memory_order_acq_rel)
#define atomic_cas_weak_acq(x, exp, des) \
    atomic_compare_exchange_weak_explicit((x), (exp), (des), \
        memory_order_acquire, memory_order_relaxed)

/* ── Result type ─────────────────────────────────────── */
typedef enum {
    OK = 0,
    ERR_NOMEM,
    ERR_BUSY,
    ERR_FULL,
    ERR_EMPTY,
    ERR_INVALID,
    ERR_TIMEOUT,
    ERR_GPU,
    ERR_IO,
} Result;

#define TRY(expr) do { Result _r = (expr); if (_r != OK) return _r; } while (0)

#endif /* PS2RE_TYPES_H */