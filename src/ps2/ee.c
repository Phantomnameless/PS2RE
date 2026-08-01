#include "ps2re/ps2/ee.h"
#include "ps2re/math/mat4_neon.h"
#include <string.h>

/* ── Game Logic Task ─────────────────────────────────── */
/* Runs on main thread or worker — depends on game state */
void ee_task_update_logic(void* data)
{
    EEContext* ctx = (EEContext*)data;

    /* This replaces EE's main loop:
     *   - Process input
     *   - Update entity positions
     *   - Run AI state machines
     *   - Update physics parameters
     *   - Compute camera matrices
     */

    ctx->delta_time = 1.0f / 60.0f;  /* fixed timestep */
    ctx->frame_number++;
    ctx->game_time += ctx->delta_time;

    /* Compute VP matrix (EE did this with COP2/VU0) */
    mat4 view = mat4_load(ctx->view_matrix);
    mat4 proj = mat4_load(ctx->proj_matrix);
    mat4 vp   = mat4_mul(proj, view);
    mat4_store(ctx->vp_matrix, vp);
}

/* ── Build Draw Commands ─────────────────────────────── */
/* Waits for: culling, animation, skinning */
/* Produces: draw command buffer for GPU submission */
void ee_task_build_draw_commands(void* data)
{
    EEContext* ctx = (EEContext*)data;

    /* This replaces:
     *   EE → VIF → GIF tag assembly
     *
     * Instead of manually packing GIF tags (128-bit with format fields),
     * we build Vulkan draw commands directly.
     *
     * The GIF had:
     *   - Tag format (REG, FLG, NLOOP, EOP, PRE, PRIM, ...) = 128 bits
     *   - Per-tag overhead: 12-20 cycles
     *   - PATH3 stalls when GS busy
     *
     * Vulkan draw commands:
     *   - vkCmdDrawIndexed: ~20 bytes, no per-command overhead
     *   - Batched: hundreds of draws per command buffer
     *   - Async: command buffer submitted, CPU continues
     */

    /* ctx->draw_commands already populated by culling/animation tasks */
    /* Nothing to do here — the command buffer was built inline */
}