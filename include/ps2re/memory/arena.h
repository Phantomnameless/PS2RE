#ifndef PS2RE_ARENA_H
#define PS2RE_ARENA_H

#include "ps2re/types.h"

/*
 * Bump/linear allocator — the backbone of frame-local allocation.
 *
 * Strategy:
 *   - Allocate large pages from OS (mmap)
 *   - Bump pointer forward on each alloc (single ADD instruction)
 *   - Reset entire arena at frame start (single pointer reset)
 *   - No free, no fragmentation, no locking
 *
 * This replaces malloc/free for ALL per-frame data.
 * The PS2 had no dynamic allocation — everything was statically placed.
 * This arena gives us dynamic-size allocation with static-speed.
 */

typedef struct ArenaPage {
    struct ArenaPage* next;
    u8*               base;
    size_t            size;
    size_t            used;
} ArenaPage;

typedef struct Arena {
    ArenaPage* current;
    ArenaPage* pages;
    size_t     page_size;
    u64        total_allocated;
} Arena;

Result arena_init(Arena* arena, size_t page_size);
void   arena_destroy(Arena* arena);
void*  arena_alloc(Arena* arena, size_t size);
void*  arena_alloc_aligned(Arena* arena, size_t size, size_t align);
void   arena_reset(Arena* arena);     /* rewind to start, keep pages */
void   arena_free_all(Arena* arena);  /* return pages to OS */

/* Convenience macros */
#define ARENA_NEW(arena, type) \
    ((type*)arena_alloc_aligned((arena), sizeof(type), alignof(type)))
#define ARENA_ARRAY(arena, type, count) \
    ((type*)arena_alloc_aligned((arena), sizeof(type) * (count), alignof(type)))

#endif /* PS2RE_ARENA_H */