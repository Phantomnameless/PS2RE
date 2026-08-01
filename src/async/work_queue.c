#include "ps2re/async/work_queue.h"
#include "ps2re/config.h"
#include "ps2re/platform.h"
#include <stdlib.h>
#include <string.h>

/* ── Chase-Lev Deque ─────────────────────────────────── */

static Result deque_init(WorkerDeque* d, s64 capacity)
{
    d->buffer = aligned_alloc(CACHE_LINE, (size_t)capacity * sizeof(Task*));
    if (!d->buffer) return ERR_NOMEM;
    memset(d->buffer, 0, (size_t)capacity * sizeof(Task*));
    d->buffer_mask = capacity - 1;
    atomic_init(&d->top, (s64)0);
    atomic_init(&d->bottom, (s64)0);
    return OK;
}

static void deque_destroy(WorkerDeque* d)
{
    free(d->buffer);
    d->buffer = NULL;
}

static void deque_push(WorkerDeque* d, Task* task)
{
    s64 b = atomic_load_explicit(&d->bottom, memory_order_relaxed);
    s64 t = atomic_load_explicit(&d->top, memory_order_acquire);
    s64 size = b - t;

    if (size > d->buffer_mask) return;

    d->buffer[b & d->buffer_mask] = task;

    __asm__ volatile("dmb ishst" ::: "memory");
    atomic_store_explicit(&d->bottom, b + 1, memory_order_release);
}

static Task* deque_pop(WorkerDeque* d)
{
    s64 b = atomic_load_explicit(&d->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&d->bottom, b, memory_order_relaxed);

    __asm__ volatile("dmb ish" ::: "memory");

    s64 t = atomic_load_explicit(&d->top, memory_order_relaxed);
    Task* task = NULL;

    if (t <= b) {
        task = d->buffer[b & d->buffer_mask];
        if (t == b) {
            if (!atomic_compare_exchange_weak_explicit(
                    &d->top, &t, t + 1,
                    memory_order_acquire, memory_order_relaxed)) {
                task = NULL;
            }
            atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
        }
    } else {
        atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
    }
    return task;
}

static Task* deque_steal(WorkerDeque* d)
{
    s64 t = atomic_load_explicit(&d->top, memory_order_acquire);
    __asm__ volatile("dmb ish" ::: "memory");
    s64 b = atomic_load_explicit(&d->bottom, memory_order_acquire);

    if (t < b) {
        Task* task = d->buffer[t & d->buffer_mask];
        if (atomic_compare_exchange_weak_explicit(
                &d->top, &t, t + 1,
                memory_order_acquire, memory_order_relaxed)) {
            return task;
        }
    }
    return NULL;
}

/* ── Worker Thread ───────────────────────────────────── */

static void* worker_thread(void* arg);

Result work_queue_init(WorkQueue* wq, s32 num_workers)
{
    if (num_workers > MAX_WORKERS) num_workers = MAX_WORKERS;
    wq->num_workers  = num_workers;
    wq->active_graph = NULL;
    atomic_init(&wq->shutdown, 0);

    PS2RE_BARRIER_INIT(&wq->frame_barrier, NULL, (unsigned)(num_workers + 1));

    for (s32 i = 0; i < num_workers; i++) {
        TRY(deque_init(&wq->deques[i], 4096));
        pthread_create(&wq->threads[i], NULL, worker_thread, wq);
        ps2re_set_thread_affinity(wq->threads[i], i);  /* ← */
    }
    return OK;
}

void work_queue_destroy(WorkQueue* wq)
{
    work_queue_shutdown(wq);
    for (s32 i = 0; i < wq->num_workers; i++) {
        pthread_join(wq->threads[i], NULL);
        deque_destroy(&wq->deques[i]);
    }
    PS2RE_BARRIER_DESTROY(&wq->frame_barrier);  /* ← */
}

void work_queue_submit_graph(WorkQueue* wq, TaskGraph* graph)
{
    wq->active_graph = graph;

    for (s32 i = 0; i < graph->count; i++) {
        Task* t = &graph->tasks[i];
        if (atomic_load_explicit(&t->deps_remaining, memory_order_relaxed) == 0 &&
            atomic_load_explicit(&t->state, memory_order_relaxed) == (s32)TASK_PENDING) {
            atomic_store_explicit(&t->state, (s32)TASK_READY, memory_order_relaxed);
            s32 worker = i % wq->num_workers;
            deque_push(&wq->deques[worker], t);
        }
    }

    PS2RE_BARRIER_WAIT(&wq->frame_barrier);  /* ← */
}

void work_queue_wait_frame(WorkQueue* wq)
{
    PS2RE_BARRIER_WAIT(&wq->frame_barrier);  /* ← */
}

void work_queue_shutdown(WorkQueue* wq)
{
    atomic_store_explicit(&wq->shutdown, 1, memory_order_release);
}

Task* work_queue_steal_or_pop(WorkQueue* wq, s32 worker_id)
{
    Task* t = deque_pop(&wq->deques[worker_id]);
    if (t) return t;

    s32 start = (worker_id + 1) % wq->num_workers;
    for (s32 i = 0; i < wq->num_workers - 1; i++) {
        s32 victim = (start + i) % wq->num_workers;
        t = deque_steal(&wq->deques[victim]);
        if (t) return t;
    }
    return NULL;
}

static void* worker_thread(void* arg)
{
    WorkQueue* wq = (WorkQueue*)arg;

    /* Determine our worker ID */
    s32 my_id = 0;
    pthread_t self = pthread_self();
    for (s32 i = 0; i < wq->num_workers; i++) {
        if (pthread_equal(self, wq->threads[i])) {
            my_id = i;
            break;
        }
    }

    while (!atomic_load_explicit(&wq->shutdown, memory_order_acquire)) {
        PS2RE_BARRIER_WAIT(&wq->frame_barrier);  /* ← CORRIGIDO */

        TaskGraph* graph = wq->active_graph;
        if (!graph) continue;

        while (!task_graph_all_done(graph)) {
            Task* task = work_queue_steal_or_pop(wq, my_id);

            if (!task) {
                for (volatile int spin = 0; spin < 64; spin++)
                    ps2re_spin_hint();     /* ← era inline asm */
                ps2re_yield();             /* ← era sched_yield */
                continue;
            }

            atomic_store_explicit(&task->state, (s32)TASK_RUNNING,
                                  memory_order_relaxed);
            task->func(task->data);
            task_graph_complete(task);
        }

        PS2RE_BARRIER_WAIT(&wq->frame_barrier);  /* ← */
    }
    return NULL;
}