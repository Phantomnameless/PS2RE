#ifndef PS2RE_VU1_PIPELINE_H
#define PS2RE_VU1_PIPELINE_H

#include "ps2re/types.h"
#include "ps2re/math/mat4_neon.h"
#include "ps2re/async/task_graph.h"

/*
 * VU1 replacement — vertex transform + clip pipeline.
 *
 * PS2 VU1 limitations eliminated:
 *   - 16KB InstrMem → unlimited shader complexity
 *   - 16KB DataMem → batch entire meshes
 *   - 2-3 cycle stalls → zero stalls (OoO execution)
 *   - Manual GIF tagging → automatic Vulkan command recording
 *   - PATH3 stalls → async GPU submission
 *
 * Pipeline:
 *   1. Transform vertices (MVP × position) — NEON batch
 *   2. Clip test (6 frustum planes) — NEON batch
 *   3. Perspective divide + viewport — NEON batch
 *   4. Output to Vulkan vertex buffer (memory-mapped, no copy)
 */

typedef struct VU1Vertex {
    f32 pos[4];      /* x, y, z, w (clip space) */
    f32 normal[3];
    f32 uv[2];
    u8  color[4];    /* RGBA8 */
} VU1Vertex;

typedef struct VU1Output {
    f32* screen_x;   /* viewport-transformed */
    f32* screen_y;
    f32* depth;
    u32* clip_flags;
    int  count;
    bool any_clipped;
} VU1Output;

typedef struct VU1Input {
    /* Raw vertex data */
    const void*  vertex_data;    /* VU1Vertex[] */
    int          vertex_count;
    int          vertex_stride;

    /* Transform matrices */
    mat4         mvp;
    mat4         model;

    /* Fog parameters (PS2 GS fog) */
    f32          fog_start;
    f32          fog_end;

    /* Viewport */
    f32          vp_x, vp_y, vp_w, vp_h;

    /* Output */
    VU1Output    output;
} VU1Input;

void vu1_task_transform_vertices(void* data);
void vu1_task_clip_assembly(void* data);

#endif /* PS2RE_VU1_PIPELINE_H */