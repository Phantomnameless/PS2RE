#include "ps2re/async/ring_buffer.h"
#include <stdlib.h>
#include <string.h>

/* ── SPSC ────────────────────────────────────────────── */

Result spsc_ring_init(SPSCRing* ring, size_t capacity, size_t elem_size)
{
    if ((capacity & (capacity - 1)) != 0 || capacity == 0)
        return ERR_INVALID;

    ring->data = aligned_alloc(CACHE_LINE, capacity * elem_size);
    if (!ring->data) return ERR_NOMEM;

    ring->capacity  = capacity;
    ring->elem_size = elem_size;
    ring->mask      = capacity - 1;
    atomic_store_rlx(&ring->head, 0);
    atomic_store_rlx(&ring->tail, 0);
    return OK;
}

void spsc_ring_destroy(SPSCRing* ring)
{
    free(ring->data);
    ring->data = NULL;
}

FORCE_INLINE Result spsc_ring_push(SPSCRing* ring, const void* elem)
{
size_t h = atomic_load_rlx(&ring->head);
size_t t = atomic_load_acq(&ring->tail);

if (h - t >= ring->capacity)
return ERR_FULL;  /* no space */

memcpy(ring->data + (h & ring->mask) * ring->elem_size,
elem, ring->elem_size);

/* ARM64: store-release ensures data is visible before head update */
atomic_store_rel(&ring->head, h + 1);
return OK;
}

FORCE_INLINE Result spsc_ring_pop(SPSCRing* ring, void* elem_out)
{
size_t t = atomic_load_rlx(&ring->tail);
size_t h = atomic_load_acq(&ring->head);

if (t == h)
return ERR_EMPTY;

memcpy(elem_out,
        ring->data + (t & ring->mask) * ring->elem_size,
ring->elem_size);

/* store-release: consumer reclaims space visible to producer */
atomic_store_rel(&ring->tail, t + 1);
return OK;
}

Result spsc_ring_peek(const SPSCRing* ring, void* elem_out)
{
    size_t t = atomic_load_rlx(&ring->tail);
    size_t h = atomic_load_acq(&ring->head);
    if (t == h) return ERR_EMPTY;

    memcpy(elem_out,
           ring->data + (t & ring->mask) * ring->elem_size,
           ring->elem_size);
    return OK;
}

size_t spsc_ring_count(const SPSCRing* ring)
{
    size_t h = atomic_load_acq(&ring->head);
    size_t t = atomic_load_acq(&ring->tail);
    return h - t;
}

bool spsc_ring_full(const SPSCRing* ring)
{
    return spsc_ring_count(ring) >= ring->capacity;
}

bool spsc_ring_empty(const SPSCRing* ring)
{
    size_t h = atomic_load_acq(&ring->head);
    size_t t = atomic_load_acq(&ring->tail);
    return h == t;
}

/* ── MPSC ────────────────────────────────────────────── */

Result mpsc_ring_init(MPSCRing* ring, size_t capacity, size_t elem_size)
{
    if ((capacity & (capacity - 1)) != 0 || capacity == 0)
        return ERR_INVALID;

    ring->data = aligned_alloc(CACHE_LINE, capacity * elem_size);
    if (!ring->data) return ERR_NOMEM;

    ring->capacity  = capacity;
    ring->elem_size = elem_size;
    ring->mask      = capacity - 1;
    atomic_store_rlx(&ring->head, 0);
    atomic_store_rlx(&ring->tail, 0);
    return OK;
}

void mpsc_ring_destroy(MPSCRing* ring)
{
    free(ring->data);
    ring->data = NULL;
}

Result mpsc_ring_push(MPSCRing* ring, const void* elem)
{
    size_t h, slot;
    do {
        h = atomic_load_acq(&ring->head);
        size_t t = atomic_load_acq(&ring->tail);
        if (h - t >= ring->capacity)
            return ERR_FULL;
        slot = h;
        /* CAS: multiple producers contend here */
    } while (!atomic_cas_weak_acq(&ring->head, &h, h + 1));

    memcpy(ring->data + (slot & ring->mask) * ring->elem_size,
           elem, ring->elem_size);
    return OK;
}

Result mpsc_ring_pop(MPSCRing* ring, void* elem_out)
{
    size_t t = atomic_load_rlx(&ring->tail);
    size_t h = atomic_load_acq(&ring->head);
    if (t == h) return ERR_EMPTY;

    memcpy(elem_out,
           ring->data + (t & ring->mask) * ring->elem_size,
           ring->elem_size);

    atomic_store_rel(&ring->tail, t + 1);
    return OK;
}