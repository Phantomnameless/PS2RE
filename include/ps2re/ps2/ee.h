#ifndef PS2RE_EE_H
#define PS2RE_EE_H

#include "ps2re/types.h"
#include "ps2re/async/task_graph.h"

/*
 * EE (Emotion Engine) replacement.
 *
 * The PS2 EE did:
 *   - Game logic, AI, scripting
 *   - Physics (simple)
 *   - Setup VU0/VU1 programs and data
 *   - DMA management
 *
 * ARM64 replacement:
 *   - Game logic runs on main thread
 *   - Physics/Particles delegated to NEON compute (VU0 replacement)
 *   - VU1 setup delegated to build_draw_commands task
 *   - No DMA management (unified memory)
 */

typedef struct {
    /* Game state */
    f32 delta_time;
    u64 frame_number;
    f32 game_time;

    /* Camera */
    ALIGN16 f32 view_matrix[16];
    ALIGN16 f32 proj_matrix[16];
    ALIGN16 f32 vp_matrix[16];

    /* Scene graph pointers (set by game logic) */
    void* scene_root;

    /* Task outputs (consumed by downstream tasks) */
    struct DrawCommand* draw_commands;
    s32                 num_draw_commands;
} EEContext;

/* Task functions — these are nodes in the task graph */
void ee_task_update_logic(void* data);
void ee_task_build_draw_commands(void* data);

#endif /* PS2RE_EE_H */