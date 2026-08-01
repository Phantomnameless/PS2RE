#ifndef PS2RE_POOL_H
#define PS2RE_POOL_H

#include "ps2re/types.h"

/*
 * Fixed-size object pool — for frequently allocated/freed objects.
 * Free list inside pre-allocated slab. O(1) alloc and free.
 *
 * Used for: DrawCommand, Task closures, temporary math objects.
 * Zero fragmentation, zero system calls after initialization.
 */

typedef struct PoolSlot {
    struct PoolSlot* next;
} PoolSlot;

typedef struct Pool {
    u8*        slab;
    PoolSlot*  free_list;
    size_t     elem_size;   /* actual size = max(elem_size, sizeof(PoolSlot)) */
    size_t     capacity;
    size_t     allocated;
} Pool;

Result pool_init(Pool* pool, size_t elem_size, size_t capacity);
void   pool_destroy(Pool* pool);
void*  pool_alloc(Pool* pool);
void   pool_free(Pool* pool, void* ptr);
size_t pool_available(const Pool* pool);

#endif /* PS2RE_POOL_H */