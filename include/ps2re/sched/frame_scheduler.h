#ifndef PS2RE_FRAME_SCHEDULER_H
#define PS2RE_FRAME_SCHEDULER_H

#include "ps2re/types.h"
#include "ps2re/async/task_graph.h"
#include "ps2re/async/work_queue.h"
#include "ps2re/async/fence.h"
#include "ps2re/async/ring_buffer.h"
#include "ps2re/memory/arena.h"
#include "ps2re/render/renderer.h"
#include "ps2re/ps2/ee.h"
#include "ps2re/ps2/gif_channel.h"   /* ← GIFContext */
#include "ps2re/config.h"

typedef struct FrameData {
    Arena       arena;
    TaskGraph   task_graph;
    EEContext   ee;
    GSState     gs;
    GIFContext  gif;
    CPUFence    gpu_done;
    s32         frame_index;
} FrameData;

typedef struct FrameScheduler {
    FrameData       frames[MAX_FRAMES_IN_FLIGHT];
    WorkQueue       workers;
    Renderer*       renderer;

    s32             current;
    u64             frame_number;
    f64             target_dt;
    f64             accumulator;

    f64             frame_times[128];
    s32             frame_time_idx;
} FrameScheduler;

Result frame_scheduler_init(FrameScheduler* sched, Renderer* renderer);
void   frame_scheduler_destroy(FrameScheduler* sched);
void   frame_scheduler_run_frame(FrameScheduler* sched, f64 delta_real);
bool   frame_scheduler_should_quit(const FrameScheduler* sched);

#endif /* PS2RE_FRAME_SCHEDULER_H */