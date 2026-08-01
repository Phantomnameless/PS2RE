#ifndef PS2RE_RING_BUFFER_H
#define PS2RE_RING_BUFFER_H

#include "ps2re/types.h"

/*
 * Lock-free SPSC (Single-Producer Single-Consumer) ring buffer.
 *
 * Cache-line padded head/tail to eliminate false sharing.
 * Capacity MUST be power of 2 for mask-based modular arithmetic.
 *
 * Memory ordering:
 *   Producer: store-rel on head (publishes data)
 *   Consumer: load-acq on head (sees published data)
 *   Consumer: store-rel on tail (reclaims space)
 *   Producer: load-acq on tail (sees reclaimed space)
 */

typedef ALIGN_CACHE struct {
    u8*    data;
    size_t capacity;     /* power of 2 */
    size_t elem_size;
    size_t mask;         /* capacity - 1 */

    ALIGN_CACHE _Atomic(size_t) head;  /* producer writes here */
    ALIGN_CACHE _Atomic(size_t) tail;  /* consumer reads from here */
} SPSCRing;

Result  spsc_ring_init(SPSCRing* ring, size_t capacity, size_t elem_size);
void    spsc_ring_destroy(SPSCRing* ring);
Result  spsc_ring_push(SPSCRing* ring, const void* elem);
Result  spsc_ring_pop(SPSCRing* ring, void* elem_out);
Result  spsc_ring_peek(const SPSCRing* ring, void* elem_out);
size_t  spsc_ring_count(const SPSCRing* ring);
bool    spsc_ring_full(const SPSCRing* ring);
bool    spsc_ring_empty(const SPSCRing* ring);

/*
 * Lock-free MPSC (Multi-Producer Single-Consumer) ring buffer.
 * Uses CAS on head for multi-producer safety.
 */

typedef ALIGN_CACHE struct {
    u8*    data;
    size_t capacity;
    size_t elem_size;
    size_t mask;

    ALIGN_CACHE _Atomic(size_t) head;  /* CAS contention point */
    ALIGN_CACHE _Atomic(size_t) tail;  /* single consumer */
} MPSCRing;

Result  mpsc_ring_init(MPSCRing* ring, size_t capacity, size_t elem_size);
void    mpsc_ring_destroy(MPSCRing* ring);
Result  mpsc_ring_push(MPSCRing* ring, const void* elem);
Result  mpsc_ring_pop(MPSCRing* ring, void* elem_out);

#endif /* PS2RE_RING_BUFFER_H */