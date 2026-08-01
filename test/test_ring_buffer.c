#include "ps2re/async/ring_buffer.h"
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static void test_spsc_basic(void)
{
    printf("test_spsc_basic...");
    SPSCRing ring;
    spsc_ring_init(&ring, 16, sizeof(int));

    for (int i = 0; i < 8; i++) {
        assert(spsc_ring_push(&ring, &i) == OK);
    }
    assert(spsc_ring_count(&ring) == 8);

    for (int i = 0; i < 8; i++) {
        int val;
        assert(spsc_ring_pop(&ring, &val) == OK);
        assert(val == i);
    }
    assert(spsc_ring_empty(&ring));

    spsc_ring_destroy(&ring);
    printf(" PASS\n");
}

static void test_spsc_full_empty(void)
{
    printf("test_spsc_full_empty...");
    SPSCRing ring;
    spsc_ring_init(&ring, 4, sizeof(int));

    for (int i = 0; i < 4; i++) {
        int v = i * 10;
        assert(spsc_ring_push(&ring, &v) == OK);
    }
    assert(spsc_ring_full(&ring));

    int overflow = 99;
    assert(spsc_ring_push(&ring, &overflow) == ERR_FULL);

    spsc_ring_destroy(&ring);
    printf(" PASS\n");
}

/* Producer-consumer stress test */
#define STRESS_ITEMS 1000000

static SPSCRing stress_ring;

static void* producer_thread(void* arg)
{
    (void)arg;
    for (int i = 0; i < STRESS_ITEMS; i++) {
        while (spsc_ring_push(&stress_ring, &i) != OK)
            ;   /* spin */
    }
    return NULL;
}

static void* consumer_thread(void* arg)
{
    (void)arg;
    int prev = -1;
    for (int i = 0; i < STRESS_ITEMS; i++) {
        int val = -1;    /* ← CORRIGIDO: inicializado */
        while (spsc_ring_pop(&stress_ring, &val) != OK)
            ;   /* spin */
        assert(val == prev + 1);
        prev = val;
    }
    return NULL;
}

static void test_spsc_stress(void)
{
    printf("test_spsc_stress (%d items)...", STRESS_ITEMS);
    spsc_ring_init(&stress_ring, 1024, sizeof(int));

    pthread_t prod, cons;
    pthread_create(&prod, NULL, producer_thread, NULL);
    pthread_create(&cons, NULL, consumer_thread, NULL);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    spsc_ring_destroy(&stress_ring);
    printf(" PASS\n");
}

static void test_spsc_basic(void)
{
    printf("test_spsc_basic...");
    SPSCRing ring;
    spsc_ring_init(&ring, 16, sizeof(int));

    for (int i = 0; i < 8; i++) {
        assert(spsc_ring_push(&ring, &i) == OK);
    }
    assert(spsc_ring_count(&ring) == 8);

    for (int i = 0; i < 8; i++) {
        int val = -1;
        assert(spsc_ring_pop(&ring, &val) == OK);
        assert(val == i);
    }
    assert(spsc_ring_empty(&ring));

    spsc_ring_destroy(&ring);
    printf(" PASS\n");
}

static void test_spsc_full_empty(void)
{
    printf("test_spsc_full_empty...");
    SPSCRing ring;
    spsc_ring_init(&ring, 4, sizeof(int));

    for (int i = 0; i < 4; i++) {
        int v = i * 10;
        assert(spsc_ring_push(&ring, &v) == OK);
    }
    assert(spsc_ring_full(&ring));

    int overflow = 99;
    assert(spsc_ring_push(&ring, &overflow) == ERR_FULL);

    spsc_ring_destroy(&ring);
    printf(" PASS\n");
}

int main(void)
{
    printf("=== Ring Buffer Tests ===\n");
    test_spsc_basic();
    test_spsc_full_empty();
    test_spsc_stress();
    printf("All tests passed.\n");
    return 0;
}