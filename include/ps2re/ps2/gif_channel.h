#ifndef PS2RE_GIF_CHANNEL_H
#define PS2RE_GIF_CHANNEL_H

#include "ps2re/types.h"
#include "ps2re/ps2/gs_state.h"

/*
 * GIF (Graphics Interface) replacement.
 *
 * PS2 GIF: 128-bit tag system with PATH1/2/3 routing.
 *   - PATH1: VU1 direct (fastest, VU1→GS)
 *   - PATH2: EE direct (EE→GS, slow)
 *   - PATH3: DMA (bulk transfer)
 *   - Tag overhead: 12-20 cycles per tag
 *   - PATH3 stalls when GS is busy
 *
 * ARM64 replacement: Direct Vulkan command recording.
 *   - No tag processing overhead
 *   - No path routing
 *   - No stalls (async command buffer)
 *   - Draw calls batched in command buffer
 */

typedef enum {
    GIF_REG_PRIM   = 0x00,
    GIF_REG_RGBAQ  = 0x01,
    GIF_REG_ST     = 0x02,
    GIF_REG_UV     = 0x03,
    GIF_REG_XYZ2   = 0x04,
    GIF_REG_XYZ3   = 0x05,
    GIF_REG_FOG    = 0x0A,
    GIF_REG_TEX0_1 = 0x06,
    GIF_REG_TEX0_2 = 0x07,
    GIF_REG_CLAMP  = 0x08,
    GIF_REG_ALPHA  = 0x42,
    GIF_REG_TEST   = 0x47,
    GIF_REG_FRAME  = 0x4C,
    GIF_REG_ZBUF   = 0x4E,
    GIF_REG_FBA    = 0x49,
} GIFReg;

typedef struct {
    /* Current primitive state (accumulated from GIF data) */
    f32  rgba[4];
    f32  st[2];
    u16  uv[2];
    f32  fog;

    /* Submitted vertex buffer */
    struct {
        f32 pos[4];
        f32 color[4];
        f32 uv[2];
        f32 fog;
    } vertices[256];
    int  vertex_count;
    int  vertex_max;
} GIFContext;

void gif_context_init(GIFContext* ctx, int max_verts);
void gif_reset_vertices(GIFContext* ctx);

/* Process a GIF tag (emulated) — called from VIF/DMA */
void gif_process_tag(GIFContext* ctx, GSState* gs,
                     u64 tag_lo, u64 tag_hi,
                     const u64* data, int nloop);

#endif /* PS2RE_GIF_CHANNEL_H */