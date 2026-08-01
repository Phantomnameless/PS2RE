#include "ps2re/async/task_graph.h"
#include "ps2re/async/work_queue.h"
#include "ps2re/memory/arena.h"
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>

#define TEST_ARENA_SIZE (4 * 1024 * 1024)

static _Atomic(int) task_counter = 0;

        static void count_task(void* data)
{
    (void)data;
    atomic_fetch_add_explicit(&task_counter, 1, memory_order_relaxed);
}

static _Atomic(int) execution_order[8] = {0};
        static _Atomic(int) order_idx = 0;

static void ordered_task(void* data)
{
    int id = *(int*)data;
    int idx = atomic_fetch_add_explicit(&order_idx, 1, memory_order_relaxed);
    atomic_store_explicit(&execution_order[idx], id, memory_order_relaxed);
}

static void test_basic_graph(void)
{
    printf("test_basic_graph...");
    Arena arena;
    arena_init(&arena, TEST_ARENA_SIZE);

    TaskGraph graph;
    task_graph_init(&graph, &arena, 64);

    /* Add 4 independent tasks */
    for (int i = 0; i < 4; i++) {
        Task* t = task_graph_add(&graph, count_task, NULL, 0, "count");
        assert(t != NULL);
    }

    /* All should have 0 deps → all immediately ready */
    assert(task_graph_ready_count(&graph) == 4);

    /* Simulate completion */
    for (int i = 0; i < graph.count; i++) {
        Task* t = task_graph_next_ready(&graph);
        assert(t != NULL);
        t->func(t->data);
        task_graph_complete(t);
    }

    assert(task_graph_all_done(&graph));
    assert(atomic_load_explicit(&task_counter, memory_order_relaxed) == 4);

    arena_destroy(&arena);
    printf(" PASS\n");
}

static void test_dependencies(void)
{
    printf("test_dependencies...");
    Arena arena;
    arena_init(&arena, TEST_ARENA_SIZE);

    TaskGraph graph;
    task_graph_init(&graph, &arena, 64);
    atomic_store_explicit(&order_idx, 0, memory_order_relaxed);

    int ids[] = {0, 1, 2, 3};

    /* A → B → C → D (linear chain) */
    Task* a = task_graph_add(&graph, ordered_task, &ids[0], sizeof(int), "A");
    Task* b = task_graph_add(&graph, ordered_task, &ids[1], sizeof(int), "B");
    Task* c = task_graph_add(&graph, ordered_task, &ids[2], sizeof(int), "C");
    Task* d = task_graph_add(&graph, ordered_task, &ids[3], sizeof(int), "D");

    task_graph_depend(a, b);  /* B waits for A */
    task_graph_depend(b, c);  /* C waits for B */
    task_graph_depend(c, d);  /* D waits for C */

    /* Only A should be ready */
    assert(task_graph_ready_count(&graph) == 1);

    /* Execute in order */
    for (int i = 0; i < 4; i++) {
        Task* t = task_graph_next_ready(&graph);
        assert(t != NULL);
        t->func(t->data);
        task_graph_complete(t);
    }

    assert(task_graph_all_done(&graph));

    /* Verify order: A=0, B=1, C=2, D=3 */
    assert(atomic_load_explicit(&execution_order[0], memory_order_relaxed) == 0);
    assert(atomic_load_explicit(&execution_order[1], memory_order_relaxed) == 1);
    assert(atomic_load_explicit(&execution_order[2], memory_order_relaxed) == 2);
    assert(atomic_load_explicit(&execution_order[3], memory_order_relaxed) == 3);

    arena_destroy(&arena);
    printf(" PASS\n");
}

static void test_diamond(void)
{
    printf("test_diamond...");
    Arena arena;
    arena_init(&arena, TEST_ARENA_SIZE);

    TaskGraph graph;
    task_graph_init(&graph, &arena, 64);

    /*
     *     A
     *    / \
     *   B   C    (B and C can run in parallel)
     *    \ /
     *     D
     */

    int zero = 0;
    Task* a = task_graph_add(&graph, count_task, NULL, 0, "A");
    Task* b = task_graph_add(&graph, count_task, NULL, 0, "B");
    Task* c = task_graph_add(&graph, count_task, NULL, 0, "C");
    Task* d = task_graph_add(&graph, count_task, NULL, 0, "D");

    task_graph_depend(a, b);
    task_graph_depend(a, c);
    task_graph_depend(b, d);
    task_graph_depend(c, d);

    /* Only A ready */
    assert(task_graph_ready_count(&graph) == 1);

    /* Execute A */
    Task* t = task_graph_next_ready(&graph);
    assert(t == a);
    task_graph_complete(t);

    /* Now B and C should both be ready */
    assert(task_graph_ready_count(&graph) == 2);

    /* Execute B and C (order doesn't matter) */
    t = task_graph_next_ready(&graph);
    task_graph_complete(t);
    t = task_graph_next_ready(&graph);
    task_graph_complete(t);

    /* Now D should be ready */
    assert(task_graph_ready_count(&graph) == 1);
    t = task_graph_next_ready(&graph);
    assert(t == d);
    task_graph_complete(t);

    assert(task_graph_all_done(&graph));

    arena_destroy(&arena);
    printf(" PASS\n");
}

int main(void)
{
    printf("=== Task Graph Tests ===\n");
    test_basic_graph();
    test_dependencies();
    test_diamond();
    printf("All tests passed.\n");
    return 0;
}