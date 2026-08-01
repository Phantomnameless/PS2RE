#ifndef PS2RE_VIF_UNPACK_H
#define PS2RE_VIF_UNPACK_H

#include "ps2re/types.h"

/*
 * VIF (VPU Interface) unpack replacement.
 *
 * PS2 VIF decoded command streams from DMA into VU memory.
 * Formats: V4-32, V4-16, V4-8, V3-32, V2-16, ...
 * Each unpack had masking and offset options.
 *
 * ARM64 replacement: Direct format conversion with NEON.
 * No intermediate VU memory — data goes directly to Vulkan buffers.
 */

typedef enum {
    VIF_FMT_V4_32 = 0,
    VIF_FMT_V4_16 = 1,
    VIF_FMT_V4_8  = 2,
    VIF_FMT_V3_32 = 3,
    VIF_FMT_V3_16 = 4,
    VIF_FMT_V2_32 = 5,
    VIF_FMT_V2_16 = 6,
} VIFFormat;

/* Unpack data from compressed format to float4 */
Result vif_unpack_to_float4(
        void*       dst,            /* float[4] * count */
        const void* src,            /* packed source data */
        VIFFormat   fmt,
        int         count,
        u32         write_mask,     /* which components to write (xyzw) */
        f32         scale           /* integer scale factor */
);

#endif /* PS2RE_VIF_UNPACK_H */