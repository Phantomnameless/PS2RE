#include "ps2re/ps2/gif_channel.h"
#include <string.h>

void gif_context_init(GIFContext* ctx, int max_verts)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->vertex_max = max_verts < 256 ? max_verts : 256;
    ctx->rgba[3] = 1.0f;  /* default alpha */
}

void gif_reset_vertices(GIFContext* ctx)
{
    ctx->vertex_count = 0;
}

void gif_process_tag(GIFContext* ctx, GSState* gs,
                     u64 tag_lo, u64 tag_hi,
                     const u64* data, int nloop)
{
    (void)tag_hi;

    /* Parse GIF tag (simplified) */
    int nreg = (int)((tag_lo >> 60) & 0xF);
    if (nreg == 0) nreg = 16;

    /* Register list from tag */
    u8 regs[16];
    for (int i = 0; i < nreg; i++) {
        regs[i] = (u8)((tag_lo >> (64 + i * 4)) & 0xF);
        /* For 128-bit tag: reg field is bits [64..127] in the upper 64 */
    }

    /* Process NLOOP data packets */
    int reg_idx = 0;
    for (int loop = 0; loop < nloop; loop++) {
        for (int r = 0; r < nreg; r++) {
            u64 value = data[loop * nreg + r];
            GIFReg reg = (GIFReg)regs[r];

            switch (reg) {
                case GIF_REG_PRIM:
                    gs_write_prim(gs, value);
                    break;

                case GIF_REG_RGBAQ:
                    ctx->rgba[0] = (f32)(value & 0xFF) / 255.0f;
                    ctx->rgba[1] = (f32)((value >> 8) & 0xFF) / 255.0f;
                    ctx->rgba[2] = (f32)((value >> 16) & 0xFF) / 255.0f;
                    ctx->rgba[3] = (f32)((value >> 24) & 0xFF) / 255.0f;
                    break;

                case GIF_REG_ST:
                    ctx->st[0] = *(f32*)&value;
                    ctx->st[1] = (f32)(value >> 32);
                    break;

                case GIF_REG_UV:
                    ctx->uv[0] = (u16)(value & 0x3FFF);
                    ctx->uv[1] = (u16)((value >> 16) & 0x3FFF);
                    break;

                case GIF_REG_XYZ2:
                case GIF_REG_XYZ3: {
                    if (ctx->vertex_count >= ctx->vertex_max) break;

                    int idx = ctx->vertex_count++;
                    /* Fixed-point to float: xyz2 format = 12.4 */
                    u16 ix = (u16)(value & 0xFFFF);
                    u16 iy = (u16)((value >> 16) & 0xFFFF);
                    u32 iz = (u32)(value >> 32);

                    ctx->vertices[idx].pos[0] = (f32)ix / 16.0f;
                    ctx->vertices[idx].pos[1] = (f32)iy / 16.0f;
                    ctx->vertices[idx].pos[2] = (f32)iz / 16777216.0f;
                    ctx->vertices[idx].pos[3] = 1.0f;

                    memcpy(ctx->vertices[idx].color, ctx->rgba, sizeof(f32) * 4);
                    ctx->vertices[idx].uv[0] = (f32)ctx->uv[0] / 16.0f;
                    ctx->vertices[idx].uv[1] = (f32)ctx->uv[1] / 16.0f;
                    ctx->vertices[idx].fog = ctx->fog;
                    break;
                }

                case GIF_REG_FOG:
                    ctx->fog = (f32)((value >> 56) & 0xFF) / 255.0f;
                    break;

                case GIF_REG_ALPHA:
                    gs_write_alpha(gs, value);
                    break;

                case GIF_REG_TEST:
                    gs_write_test(gs, value);
                    break;

                case GIF_REG_FRAME:
                    gs_write_frame(gs, value);
                    break;

                case GIF_REG_TEX0_1:
                    gs_write_tex0(gs, value);
                    break;

                default:
                    break;
            }
        }
    }
}