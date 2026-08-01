#include "ps2re/async/task_graph.h"
#include <string.h>

Result task_graph_init(TaskGraph* graph, Arena* arena, s32 capacity)
{
    graph->arena    = arena;
    graph->capacity = capacity;
    graph->count    = 0;
    graph->next_id  = 0;
    graph->tasks    = arena_alloc(arena, sizeof(Task) * capacity);
    if (!graph->tasks) return ERR_NOMEM;
    return OK;
}

void task_graph_reset(TaskGraph* graph)
{
    arena_reset(graph->arena);
    graph->count   = 0;
    graph->next_id = 0;
    /* Re-reserve task array at start of arena */
    graph->tasks = arena_alloc(graph->arena,
                               sizeof(Task) * graph->capacity);
}

Task* task_graph_add(TaskGraph* graph, TaskFunc func,
                     const void* data, u32 data_size,
                     const char* debug_name)
{
    if (graph->count >= graph->capacity) return NULL;

    Task* t = &graph->tasks[graph->count++];
    memset(t, 0, sizeof(*t));

    t->func          = func;
    t->data_size     = data_size;
    t->id            = graph->next_id++;
    t->debug_name    = debug_name;
    t->priority      = 0;

    /* Copy data into arena — no malloc in hot path */
    if (data && data_size > 0) {
        t->data = arena_alloc(graph->arena, data_size);
        if (!t->data) return NULL;
        memcpy(t->data, data, data_size);
    } else {
        t->data = (void*)data;
    }

    /* Space for dependent pointers */
    t->max_dependents = TASK_GRAPH_MAX_DEPENDENTS;
    t->dependents = arena_alloc(graph->arena,
                                sizeof(Task*) * t->max_dependents);
    t->num_dependents = 0;

    atomic_store_rlx(&t->deps_remaining, 0);
    atomic_store_rlx(&t->state, (s32)TASK_PENDING);
    return t;
}

void task_graph_depend(Task* predecessor, Task* successor)
{
    /* successor depends on predecessor */
    atomic_fetch_add_rlx(&successor->deps_remaining, 1);

    /* predecessor needs to know to decrement successor on completion */
    if (predecessor->num_dependents < predecessor->max_dependents) {
        predecessor->dependents[predecessor->num_dependents++] = successor;
    }
}

void task_graph_set_priority(Task* task, s32 priority)
{
    task->priority = priority;
}

s32 task_graph_ready_count(const TaskGraph* graph)
{
    s32 count = 0;
    for (s32 i = 0; i < graph->count; i++) {
        if (atomic_load_rlx(&graph->tasks[i].state) == (s32)TASK_PENDING &&
            atomic_load_rlx(&graph->tasks[i].deps_remaining) == 0) {
            count++;
        }
    }
    return count;
}

Task* task_graph_next_ready(TaskGraph* graph)
{
    Task* best = NULL;
    s32   best_prio = INT32_MIN;

    for (s32 i = 0; i < graph->count; i++) {
        Task* t = &graph->tasks[i];
        s32 state = atomic_load_rlx(&t->state);
        s32 deps  = atomic_load_rlx(&t->deps_remaining);

        if (state == (s32)TASK_PENDING && deps == 0) {
            if (t->priority > best_prio) {
                best_prio = t->priority;
                best = t;
            }
        }
    }

    if (best) {
        atomic_store_rlx(&best->state, (s32)TASK_READY);
    }
    return best;
}

void task_graph_complete(Task* task)
{
    /* Mark done */
    atomic_store_rel(&task->state, (s32)TASK_DONE);

    /* Cascade: decrement dependents' counters */
    for (s32 i = 0; i < task->num_dependents; i++) {
        Task* dep = task->dependents[i];
        s32 remaining = atomic_fetch_sub_acq_rel(&dep->deps_remaining, 1);
        /* If remaining was 1 (now 0), dependent is ready */
        (void)remaining;
    }
}

bool task_graph_all_done(const TaskGraph* graph)
{
    for (s32 i = 0; i < graph->count; i++) {
        if (atomic_load_acq(&graph->tasks[i].state) != (s32)TASK_DONE)
            return false;
    }
    return true;
}