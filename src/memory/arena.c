#include "ps2re/memory/arena.h"
#include <sys/mman.h>
#include <string.h>

static ArenaPage* arena_new_page(Arena* arena, size_t min_size)
{
    size_t sz = arena->page_size;
    if (sz < min_size) sz = min_size;
    sz = (sz + 4095) & ~(size_t)4095;

    void* mem = mmap(NULL, sz, PROT_READ | PROT_WRITE,  /* ← void* */
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return NULL;

    ArenaPage* page = (ArenaPage*)mem;
    page->next = arena->pages;
    page->base = (u8*)mem + sizeof(ArenaPage);   /* ← cast explícito */
    page->size = sz - sizeof(ArenaPage);
    page->used = 0;
    arena->pages = page;
    arena->current = page;
    arena->total_allocated += sz;
    return page;
}

Result arena_init(Arena* arena, size_t page_size)
{
    arena->pages           = NULL;
    arena->current         = NULL;
    arena->page_size       = page_size;
    arena->total_allocated = 0;
    if (!arena_new_page(arena, page_size)) return ERR_NOMEM;
    return OK;
}

void arena_destroy(Arena* arena)
{
    arena_free_all(arena);
}

void* arena_alloc(Arena* arena, size_t size)
{
    return arena_alloc_aligned(arena, size, 16);
}

void* arena_alloc_aligned(Arena* arena, size_t size, size_t align)
{
    if (align == 0) align = 1;              /* ← safety */
    ArenaPage* page = arena->current;
    if (!page) return NULL;                  /* ← null check */

    size_t offset = (page->used + align - 1) & ~(align - 1);

    if (offset + size > page->size) {
        size_t needed = size + align;
        page = arena_new_page(arena, needed > arena->page_size
                                     ? needed : arena->page_size);
        if (!page) return NULL;
        offset = 0;
    }

    void* ptr = page->base + offset;
    page->used = offset + size;
    return ptr;
}

void arena_reset(Arena* arena)
{
    ArenaPage* p = arena->pages;
    while (p) {
        p->used = 0;
        p = p->next;
    }
    arena->current = arena->pages;
}

void arena_free_all(Arena* arena)
{
    ArenaPage* p = arena->pages;
    while (p) {
        ArenaPage* next = p->next;
        size_t page_mem = p->size + sizeof(ArenaPage);
        munmap(p, page_mem);
        p = next;
    }
    arena->pages           = NULL;
    arena->current         = NULL;
    arena->total_allocated = 0;
}