#include "ps2re/ps2/gs_state.h"
#include <string.h>

void gs_state_init(GSState* gs)
{
    memset(gs, 0, sizeof(*gs));
    gs->prim.prim_type = GS_PRIM_TRI;
    gs->test.ate = false;
    gs->test.zte = true;
    gs->test.ztst = 3;  /* (default) */
    gs->colclamp = true;
}

u64 gs_compute_pipeline_hash(const GSState* gs)
{
    /* Hash the relevant state into a pipeline key.
     * Vulkan pipeline cache uses this to avoid redundant pipeline creation.
     *
     * Bits:
     *   [0-2]   prim_type
     *   [3]     iip (shading)
     *   [4]     tme (texturing)
     *   [5]     fge (fog)
     *   [6]     abe (alpha blend)
     *   [7-8]   alpha.A
     *   [9-10]  alpha.B
     *   [11-12] alpha.C
     *   [13-14] alpha.D
     *   [15]    ate (alpha test)
     *   [16-18] atst
     *   [19-20] ztst
     *   [21]    fst (tex coord mode)
     *   [22-23] tex0.tfx
     *   [24]    colclamp
     *   [25-31] psm (frame format)
     */
    u64 h = 0;
    h |= (u64)gs->prim.prim_type;
    h |= (u64)gs->prim.iip       << 3;
    h |= (u64)gs->prim.tme       << 4;
    h |= (u64)gs->prim.fge       << 5;
    h |= (u64)gs->prim.abe       << 6;
    h |= (u64)gs->alpha.A        << 7;
    h |= (u64)gs->alpha.B        << 9;
    h |= (u64)gs->alpha.C        << 11;
    h |= (u64)gs->alpha.D        << 13;
    h |= (u64)gs->test.ate       << 15;
    h |= (u64)gs->test.atst      << 16;
    h |= (u64)gs->test.ztst      << 19;
    h |= (u64)gs->prim.fst       << 21;
    h |= (u64)gs->tex0.tfx       << 22;
    h |= (u64)gs->colclamp       << 24;
    h |= (u64)gs->frame.psm      << 25;
    return h;
}

void gs_write_prim(GSState* gs, u64 value)
{
    gs->prim.prim_type = (GSPrimType)(value & 0x7);
    gs->prim.iip  = (value >> 3) & 1;
    gs->prim.tme  = (value >> 4) & 1;
    gs->prim.fge  = (value >> 5) & 1;
    gs->prim.abe  = (value >> 6) & 1;
    gs->prim.aa1  = (value >> 7) & 1;
    gs->prim.fst  = (value >> 8) & 1;
    gs->prim.ctxt = (value >> 9) & 1;
    gs->prim.fix  = (value >> 10) & 1;
    gs->pipeline_hash = gs_compute_pipeline_hash(gs);
}

void gs_write_alpha(GSState* gs, u64 value)
{
    gs->alpha.A = (GSBlendFactor)((value >> 0) & 0x3);
    gs->alpha.B = (GSBlendFactor)((value >> 2) & 0x3);
    gs->alpha.C = (GSBlendFactor)((value >> 4) & 0x3);
    gs->alpha.D = (GSBlendFactor)((value >> 6) & 0x3);
    gs->alpha.fix_alpha = (u8)((value >> 32) & 0xFF);
    gs->pipeline_hash = gs_compute_pipeline_hash(gs);
}

void gs_write_test(GSState* gs, u64 value)
{
    gs->test.ate  = (value >> 0) & 1;
    gs->test.atst = (value >> 1) & 0x7;
    gs->test.aref = (value >> 4) & 0xFF;
    gs->test.afail = (value >> 12) & 0x3;
    gs->test.date = (value >> 14) & 1;
    gs->test.datm = (value >> 15) & 1;
    gs->test.zte  = (value >> 16) & 1;
    gs->test.ztst = (value >> 17) & 0x3;
    gs->pipeline_hash = gs_compute_pipeline_hash(gs);
}

void gs_write_frame(GSState* gs, u64 value)
{
    gs->frame.fbp = (u32)((value >> 0) & 0x1FF);
    gs->frame.fbw = (u8)((value >> 16) & 0x3F);
    gs->frame.psm = (u8)((value >> 24) & 0x3F);
}

void gs_write_tex0(GSState* gs, u64 value)
{
    gs->tex0.tbp0 = (u32)((value >> 0) & 0x3FFF);
    gs->tex0.tbw  = (u16)((value >> 14) & 0x3F);
    gs->tex0.psm  = (u8)((value >> 20) & 0x3F);
    gs->tex0.tw   = (u16)((value >> 26) & 0xF);
    gs->tex0.th   = (u16)((value >> 30) & 0xF);
    gs->tex0.tcc  = (u8)((value >> 34) & 0x1);
    gs->tex0.tfx  = (u8)((value >> 35) & 0x3);
    gs->tex0.cbp  = (u32)((value >> 37) & 0x3FFF);
    gs->pipeline_hash = gs_compute_pipeline_hash(gs);
}