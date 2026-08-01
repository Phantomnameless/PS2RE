#ifndef PS2RE_FENCE_H
#define PS2RE_FENCE_H

#include "ps2re/types.h"
#include <stdatomic.h>       /* ← para clangd */
#include <pthread.h>

typedef struct {
    pthread_mutex_t  mu;
    pthread_cond_t   cv;
    _Atomic(int)     signaled;   /* ← bool→int (compat clang) */
    _Atomic(u64)     value;      /* ← era não-atomic, data race */
} CPUFence;

Result cpu_fence_init(CPUFence* fence);
void   cpu_fence_destroy(CPUFence* fence);
void   cpu_fence_signal(CPUFence* fence, u64 value);
void   cpu_fence_wait(CPUFence* fence, u64 value);
bool   cpu_fence_is_ready(const CPUFence* fence, u64 value);

#endif /* PS2RE_FENCE_H */