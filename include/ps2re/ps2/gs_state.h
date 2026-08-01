#ifndef PS2RE_GS_STATE_H
#define PS2RE_GS_STATE_H

#include "ps2re/types.h"

/*
 * GS (Graphics Synthesizer) state machine.
 *
 * Maps PS2 GS registers to Vulkan pipeline states.
 *
 * PS2 GS registers: (each is a 64-bit value set via GIF)
 *   PRIM  — primitive type, shading, texturing, fog, alpha, AA
 *   RGBAQ — vertex color (flat shade)
 *   ST    — texture coordinate
 *   UV    — texture coordinate (for sprites)
 *   XYZ2  — screen position (fixed-point)
 *   FOG   — fog coordinate
 *   TEX0  — texture buffer info, CLUT
 *   TEX1  — texture LOD
 *   CLAMP — texture wrap mode
 *   TEST  — alpha/depth test modes
 *   FBA   — frame buffer alpha
 *   FRAME — framebuffer base, format
 *   ZBUF  — z-buffer base, format
 *   PABE  — per-pixel alpha
 *   COLCLAMP — color clamp mode
 */

/* ── PRIM register ───────────────────────────────────── */
typedef enum {
    GS_PRIM_POINT       = 0,
    GS_PRIM_LINE        = 1,
    GS_PRIM_TRI         = 2,
    GS_PRIM_TRI_STRIP   = 3,
    GS_PRIM_TRI_FAN     = 4,
    GS_PRIM_SPRITE      = 5,
} GSPrimType;

typedef struct {
    GSPrimType prim_type;
    bool       iip;         /* flat(0) / gouraud(1) shading */
    bool       tme;         /* texture mapping enable */
    bool       fge;         /* fog enable */
    bool       abe;         /* alpha blending enable */
    bool       aa1;         /* antialiasing (line only) */
    bool       fst;         /* texture coords: ST(0) / UV(1) */
    bool       ctxt;        /* context 0 or 1 */
    bool       fix;         /* fragment value control */
} GSPrim;

/* ── Alpha blend register ────────────────────────────── */
typedef enum {
    GS_BLEND_ZERO    = 0,
    GS_BLEND_CS      = 1,  /* source color */
    GS_BLEND_CD      = 2,  /* dest color */
    GS_BLEND_FIX     = 3,  /* fixed alpha */
} GSBlendFactor;

typedef struct {
    GSBlendFactor A, B, C, D;
    u8            fix_alpha;   /* 0x00-0x80 */
} GSAlpha;

/* ── Test register ───────────────────────────────────── */
typedef struct {
    bool ate;           /* alpha test enable */
    u8   atst;          /* 0=NEVER, 1=ALWAYS, 2=LESS, 3=LEQUAL, ... */
    u8   aref;          /* alpha reference */
    u8   afail;         /* 0=KEEP, 1=FB_ONLY, 2=ZB_ONLY, 3=RGB_ONLY */
    bool date;          /* destination alpha test */
    bool datm;          /* destination alpha test mode */
    bool zte;           /* depth test enable (always on in practice) */
    u8   ztst;          /* 0=NEVER, 1=ALWAYS, 2=GEQUAL, 3=GREATER */
} GSTest;

/* ── Frame/Z-buffer ──────────────────────────────────── */
typedef struct {
    u32  fbp;           /* framebuffer base pointer (in 2048-byte units) */
    u8   fbw;           /* framebuffer width (in 64-pixel units) */
    u8   psm;           /* pixel storage format */
    u32  zbp;           /* z-buffer base pointer */
    u8   zmsk;          /* z-buffer write mask */
    u8   zpsm;          /* z-buffer format */
} GSFrame;

/* ── Texture ─────────────────────────────────────────── */
typedef struct {
    u32  tbp0;          /* texture base pointer */
    u16  tbw;           /* texture buffer width */
    u8   psm;           /* pixel format */
    u16  tw, th;        /* width/height as power of 2 */
    u8   tcc;           /* color component: RGB(0) / RGBA(1) */
    u8   tfx;           /* function: MODULATE(0)/DECAL(1)/HIGHLIGHT(2) */
    u32  cbp;           /* CLUT buffer pointer */
    u8   cpsm;          /* CLUT format */
    u8   csm;           /* CLUT storage mode */
    u8   csa;           /* CLUT offset */
    u8   cld;           /* CLUT load control */
} GSTex0;

/* ── Complete GS State ───────────────────────────────── */
typedef struct GSState {
    GSPrim  prim;
    GSAlpha alpha;
    GSTest  test;
    GSFrame frame;
    GSFrame frame1;     /* context 1 */
    GSTex0  tex0;
    GSTex0  tex0_1;     /* context 1 */
    f32     fog;
    bool    fba;        /* frame buffer alpha */
    bool    colclamp;   /* color clamp (1=clamp, 0=no clamp) */
    bool    pabe;       /* per-pixel alpha blending */

    /* Derived Vulkan pipeline key (precomputed) */
    u64     pipeline_hash;
} GSState;

void gs_state_init(GSState* gs);
u64  gs_compute_pipeline_hash(const GSState* gs);

/* Write register — called from GIF path processing */
void gs_write_prim(GSState* gs, u64 value);
void gs_write_alpha(GSState* gs, u64 value);
void gs_write_test(GSState* gs, u64 value);
void gs_write_frame(GSState* gs, u64 value);
void gs_write_tex0(GSState* gs, u64 value);

#endif /* PS2RE_GS_STATE_H */