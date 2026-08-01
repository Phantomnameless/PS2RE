#include "ps2re/ps2/vif_unpack.h"
#include <string.h>

Result vif_unpack_to_float4(void* dst, const void* src,
                            VIFFormat fmt, int count,
                            u32 write_mask, f32 scale)
{
    float* out = (float*)dst;
    const u8* in = (const u8*)src;

    switch (fmt) {
        case VIF_FMT_V4_32: {
            /* Already float32 — direct copy or masked write */
            const f32* src_f = (const f32*)src;
            for (int i = 0; i < count; i++) {
                float* o = out + i * 4;
                const f32* s = src_f + i * 4;
                if (write_mask & 1) o[0] = s[0];
                if (write_mask & 2) o[1] = s[1];
                if (write_mask & 4) o[2] = s[2];
                if (write_mask & 8) o[3] = s[3];
            }
            break;
        }

        case VIF_FMT_V4_16: {
            /* 16-bit signed integer → float with scale */
            const s16* src_s = (const s16*)src;
            f32 s = scale / 32768.0f;
            for (int i = 0; i < count; i++) {
                float* o = out + i * 4;
                if (write_mask & 1) o[0] = (f32)src_s[i*4+0] * s;
                if (write_mask & 2) o[1] = (f32)src_s[i*4+1] * s;
                if (write_mask & 4) o[2] = (f32)src_s[i*4+2] * s;
                if (write_mask & 8) o[3] = (f32)src_s[i*4+3] * s;
            }
            break;
        }

        case VIF_FMT_V4_8: {
            /* 8-bit unsigned → float with scale */
            f32 s = scale / 255.0f;
            for (int i = 0; i < count; i++) {
                float* o = out + i * 4;
                if (write_mask & 1) o[0] = (f32)in[i*4+0] * s;
                if (write_mask & 2) o[1] = (f32)in[i*4+1] * s;
                if (write_mask & 4) o[2] = (f32)in[i*4+2] * s;
                if (write_mask & 8) o[3] = (f32)in[i*4+3] * s;
            }
            break;
        }

        case VIF_FMT_V2_32: {
            const f32* src_f = (const f32*)src;
            for (int i = 0; i < count; i++) {
                float* o = out + i * 4;
                if (write_mask & 1) o[0] = src_f[i*2+0];
                if (write_mask & 2) o[1] = src_f[i*2+1];
            }
            break;
        }

        default:
            return ERR_INVALID;
    }

    return OK;
}