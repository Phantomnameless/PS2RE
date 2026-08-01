#ifndef PS2RE_TASK_BUILDER_H
#define PS2RE_TASK_BUILDER_H

#include "ps2re/async/task_graph.h"
#include "ps2re/ps2/ee.h"
#include "ps2re/ps2/gs_state.h"
#include "ps2re/ps2/gif_channel.h"

/*
 * Builds the task DAG for each frame.
 *
 * This is where the "asyncrono" is defined:
 * the dependency graph determines what can run in parallel.
 *
 * Frame task graph:
 *
 *   ┌─ input_read ──────────────────┐
 *   │                                │
 *   ├─ ee_update_logic ──┬─ physics  ├─ build_draw_cmds ── gpu_submit ── present
 *   │                     ├─ particles│
 *   │                     └─ skinning │
 *   ├─ audio_mix ────────────────────┘
 *   └─ culling ─────────────────────┘
 *
 *   physics, particles, skinning = NEON compute (VU0 replacement)
 *   culling = NEON batch (VU1 pre-pass)
 *   build_draw_cmds = CPU (EE + GIF)
 *   gpu_submit = Vulkan command buffer
 */

void build_frame_task_graph(TaskGraph* graph,
                            EEContext* ee,
                            GSState* gs,
                            GIFContext* gif);

#endif /* PS2RE_TASK_BUILDER_H */