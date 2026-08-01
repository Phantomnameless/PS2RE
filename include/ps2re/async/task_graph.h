#ifndef PS2RE_TASK_GRAPH_H
#define PS2RE_TASK_GRAPH_H

#include "ps2re/types.h"
#include "ps2re/memory/arena.h"

/*
 * DAG-based task graph for frame-parallel async execution.
 *
 * Architecture:
 *   1. Tasks are allocated from a per-frame arena (zero-cost allocation)
 *   2. Each task has an atomic dependency counter
 *   3. When a task completes, it decrements dependents' counters
 *   4. Dependents with counter==0 become READY → pushed to work queue
 *   5. Work-stealing scheduler distributes across ARM64 cores
 *
 * This replaces the PS2's manual EE→VIF→VU→GIF pipeline with
 * automatic dependency resolution. No manual synchronization needed.
 */

typedef void (*TaskFunc)(void* data);

typedef enum {
    TASK_PENDING  = 0,  /* waiting on dependencies */
    TASK_READY    = 1,  /* all deps satisfied, queued */
    TASK_RUNNING  = 2,  /* executing on a worker */
    TASK_DONE     = 3,  /* completed */
} TaskState;

typedef struct Task {
    TaskFunc       func;
    void*          data;            /* closure data */
    u32            data_size;       /* for inline copy into arena */

    _Atomic(s32)   deps_remaining;  /* decremented by predecessors */
    _Atomic(s32)   state;

    struct Task**  dependents;      /* tasks that depend on THIS */
    s32            num_dependents;
    s32            max_dependents;

    s32            id;              /* unique per frame */
    s32            priority;        /* higher = scheduled first */
    const char*    debug_name;      /* for profiling */
} Task;

typedef struct TaskGraph {
    Task*   tasks;
    s32     capacity;
    s32     count;
    Arena*  arena;              /* per-frame bump allocator */
    s32     next_id;
} TaskGraph;

/* ── Lifecycle ───────────────────────────────────────── */
Result task_graph_init(TaskGraph* graph, Arena* arena, s32 capacity);
void   task_graph_reset(TaskGraph* graph);

/* ── Building ────────────────────────────────────────── */
Task*  task_graph_add(TaskGraph* graph, TaskFunc func,
                      const void* data, u32 data_size,
                      const char* debug_name);
void   task_graph_depend(Task* predecessor, Task* successor);
void   task_graph_set_priority(Task* task, s32 priority);

/* ── Execution ───────────────────────────────────────── */
s32    task_graph_ready_count(const TaskGraph* graph);
Task*  task_graph_next_ready(TaskGraph* graph);
void   task_graph_complete(Task* task);
bool   task_graph_all_done(const TaskGraph* graph);

#endif /* PS2RE_TASK_GRAPH_H */