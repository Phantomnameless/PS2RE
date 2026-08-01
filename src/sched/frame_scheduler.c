#include "ps2re/sched/frame_scheduler.h"
#include "ps2re/sched/task_builder.h"
#include "ps2re/platform.h"           /* ← */
#include <string.h>

Result frame_scheduler_init(FrameScheduler* sched, Renderer* renderer)
{
    memset(sched, 0, sizeof(*sched));
    sched->renderer     = renderer;
    sched->current      = 0;
    sched->frame_number = 0;
    sched->target_dt    = 1.0 / 60.0;
    sched->accumulator  = 0.0;

    for (s32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        FrameData* f = &sched->frames[i];
        f->frame_index = i;

        TRY(arena_init(&f->arena, FRAME_ARENA_SIZE));
        TRY(task_graph_init(&f->task_graph, &f->arena, MAX_TASKS_PER_FRAME));
        TRY(cpu_fence_init(&f->gpu_done));

        gs_state_init(&f->gs);
        gif_context_init(&f->gif, 256);
    }

    TRY(work_queue_init(&sched->workers, WORKER_THREAD_COUNT));
    return OK;
}

void frame_scheduler_destroy(FrameScheduler* sched)
{
    work_queue_destroy(&sched->workers);
    for (s32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        cpu_fence_destroy(&sched->frames[i].gpu_done);
        arena_destroy(&sched->frames[i].arena);
    }
}

void frame_scheduler_run_frame(FrameScheduler* sched, f64 delta_real)
{
    sched->accumulator += delta_real;

    if (sched->accumulator > sched->target_dt * 4.0) {
        sched->accumulator = sched->target_dt * 4.0;
    }

    while (sched->accumulator >= sched->target_dt) {
        sched->accumulator -= sched->target_dt;

        FrameData* frame = &sched->frames[sched->current];
        cpu_fence_wait(&frame->gpu_done, sched->frame_number);

        arena_reset(&frame->arena);
        task_graph_reset(&frame->task_graph);

        frame->ee.delta_time   = (f32)sched->target_dt;
        frame->ee.frame_number = sched->frame_number;

        build_frame_task_graph(&frame->task_graph, &frame->ee,
                               &frame->gs, &frame->gif);

        work_queue_submit_graph(&sched->workers, &frame->task_graph);

        sched->current = (sched->current + 1) % MAX_FRAMES_IN_FLIGHT;
        sched->frame_number++;
    }

    sched->frame_times[sched->frame_time_idx & 127] = delta_real;
    sched->frame_time_idx++;
}

bool frame_scheduler_should_quit(const FrameScheduler* sched)
{
    (void)sched;
    return false;
}