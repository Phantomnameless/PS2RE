#include "ps2re/memory/arena.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void test_basic_alloc(void)
{
    printf("test_basic_alloc...");
    Arena arena;
    arena_init(&arena, 4096);

    void* a = arena_alloc(&arena, 64);
    void* b = arena_alloc(&arena, 128);
    void* c = arena_alloc(&arena, 256);
    assert(a && b && c);
    assert(b > a);  /* monotonically increasing */
    assert(c > b);

    arena_destroy(&arena);
    printf(" PASS\n");
}

static void test_reset_reuse(void)
{
    printf("test_reset_reuse...");
    Arena arena;
    arena_init(&arena, 4096);

    void* first = arena_alloc(&arena, 256);
    arena_reset(&arena);
    void* second = arena_alloc(&arena, 256);

    /* After reset, allocation should reuse same memory */
    assert(first == second);

    arena_destroy(&arena);
    printf(" PASS\n");
}

static void test_page_overflow(void)
{
    printf("test_page_overflow...");
    Arena arena;
    arena_init(&arena, 1024);  /* small pages */

    /* Allocate more than one page */
    for (int i = 0; i < 10; i++) {
        void* p = arena_alloc(&arena, 256);
        assert(p != NULL);
        memset(p, 0xAB, 256);
    }

    assert(arena.total_allocated > 1024);  /* multiple pages allocated */

    arena_destroy(&arena);
    printf(" PASS\n");
}

static void test_alignment(void)
{
    printf("test_alignment...");
    Arena arena;
    arena_init(&arena, 4096);

    void* p1 = arena_alloc_aligned(&arena, 13, 64);
    void* p2 = arena_alloc_aligned(&arena, 7, 256);

    assert(((uintptr_t)p1 & 63) == 0);
    assert(((uintptr_t)p2 & 255) == 0);

    arena_destroy(&arena);
    printf(" PASS\n");
}

int main(void)
{
    printf("=== Arena Tests ===\n");
    test_basic_alloc();
    test_reset_reuse();
    test_page_overflow();
    test_alignment();
    printf("All tests passed.\n");
    return 0;
}