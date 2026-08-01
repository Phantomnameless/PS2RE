#include "ps2re/memory/pool.h"
#include <stdlib.h>
#include <string.h>

Result pool_init(Pool* pool, size_t elem_size, size_t capacity)
{
    if (elem_size < sizeof(PoolSlot))
        elem_size = sizeof(PoolSlot);

    pool->elem_size = elem_size;
    pool->capacity  = capacity;
    pool->allocated = 0;
    pool->slab = aligned_alloc(CACHE_LINE, elem_size * capacity);
    if (!pool->slab) return ERR_NOMEM;

    /* Build free list */
    pool->free_list = NULL;
    for (size_t i = 0; i < capacity; i++) {
        PoolSlot* slot = (PoolSlot*)(pool->slab + i * elem_size);
        slot->next = pool->free_list;
        pool->free_list = slot;
    }
    return OK;
}

void pool_destroy(Pool* pool)
{
    free(pool->slab);
    pool->slab = NULL;
}

void* pool_alloc(Pool* pool)
{
    if (!pool->free_list) return NULL;

    PoolSlot* slot = pool->free_list;
    pool->free_list = slot->next;
    pool->allocated++;
    return (void*)slot;
}

void pool_free(Pool* pool, void* ptr)
{
    if (!ptr) return;
    PoolSlot* slot = (PoolSlot*)ptr;
    slot->next = pool->free_list;
    pool->free_list = slot;
    pool->allocated--;
}

size_t pool_available(const Pool* pool)
{
    return pool->capacity - pool->allocated;
}