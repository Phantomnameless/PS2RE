#ifndef PS2RE_WORK_QUEUE_H
#define PS2RE_WORK_QUEUE_H

#include "ps2re/types.h"
#include "ps2re/platform.h"   /* ← barrier + affinity */
#include "ps2re/async/task_graph.h"
#include <stdatomic.h>        /* ←  */
#include <pthread.h>

#define MAX_WORKERS 16

typedef ALIGN_CACHE struct {
    _Atomic(s64) top;
    _Atomic(s64) bottom;
    Task** buffer;
    s64    buffer_mask;
} WorkerDeque;

typedef struct WorkQueue {
    WorkerDeque          deques[MAX_WORKERS];
    s32                  num_workers;
    pthread_t            threads[MAX_WORKERS];
    TaskGraph*           active_graph;

    ALIGN_CACHE _Atomic(int) shutdown;   /* ← bool→int */

    PS2RE_BARRIER_T      frame_barrier;  /* ← era pthread_barrier_t */
} WorkQueue;

Result work_queue_init(WorkQueue* wq, s32 num_workers);
void   work_queue_destroy(WorkQueue* wq);
void   work_queue_submit_graph(WorkQueue* wq, TaskGraph* graph);
void   work_queue_wait_frame(WorkQueue* wq);
void   work_queue_shutdown(WorkQueue* wq);
Task*  work_queue_steal_or_pop(WorkQueue* wq, s32 worker_id);

#endif /* PS2RE_WORK_QUEUE_H */