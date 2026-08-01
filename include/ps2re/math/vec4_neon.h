#ifndef PS2RE_VEC4_NEON_H
#define PS2RE_VEC4_NEON_H

#include "ps2re/types.h"
#include <arm_neon.h>

typedef float32x4_t vec4;

#define VEC4_ZERO    vdupq_n_f32(0.0f)
#define VEC4_ONE     vdupq_n_f32(1.0f)
#define VEC4_HALF    vdupq_n_f32(0.5f)

FORCE_INLINE vec4 v4_load(const float* p)         { return vld1q_f32(p); }
FORCE_INLINE void v4_store(float* p, vec4 v)      { vst1q_f32(p, v); }
FORCE_INLINE vec4 v4_splat(float s)                { return vdupq_n_f32(s); }
FORCE_INLINE vec4 v4_set(float x, float y, float z, float w)
{
    float ALIGN16 tmp[4] = {x, y, z, w};
    return vld1q_f32(tmp);
}

FORCE_INLINE vec4 v4_add(vec4 a, vec4 b)           { return vaddq_f32(a, b); }
FORCE_INLINE vec4 v4_sub(vec4 a, vec4 b)           { return vsubq_f32(a, b); }
FORCE_INLINE vec4 v4_mul(vec4 a, vec4 b)           { return vmulq_f32(a, b); }
FORCE_INLINE vec4 v4_fma(vec4 a, vec4 b, vec4 c)   { return vfmaq_f32(a, b, c); }
FORCE_INLINE vec4 v4_neg(vec4 a)                   { return vnegq_f32(a); }
FORCE_INLINE vec4 v4_min(vec4 a, vec4 b)           { return vminq_f32(a, b); }
FORCE_INLINE vec4 v4_max(vec4 a, vec4 b)           { return vmaxq_f32(a, b); }
FORCE_INLINE vec4 v4_abs(vec4 a)                   { return vabsq_f32(a); }
FORCE_INLINE vec4 v4_rcp(vec4 a)                   { return vdivq_f32(VEC4_ONE, a); }
FORCE_INLINE vec4 v4_rsqrt(vec4 a)                 { return vrsqrteq_f32(a); }

/* ── BUG CORRIGIDO: v4_dot4 — OK, não tinha erro ────── */
FORCE_INLINE vec4 v4_dot4(vec4 a, vec4 b)
{
float32x4_t prod = vmulq_f32(a, b);
float32x2_t sum  = vadd_f32(vget_low_f32(prod), vget_high_f32(prod));
sum = vpadd_f32(sum, sum);
return vdupq_lane_f32(sum, 0);
}

/* ── BUG CORRIGIDO: v4_dot3 ────────────────────────────
 * ANTES: float32x2_t sum = vadd_f32(vget_low_f32(prod), vget_high_f32(sum));
 *        BUG: 'sum' usada antes de ser definida (usa valor lixo)
 * DEPOIS: float32x2_t sum = vadd_f32(vget_low_f32(prod), vget_high_f32(prod));
 */
FORCE_INLINE vec4 v4_dot3(vec4 a, vec4 b)
{
float32x4_t prod = vmulq_f32(a, b);
/* Zero w contribution */
float ALIGN16 mask[4] = {-1.0f, -1.0f, -1.0f, 0.0f};
prod = vmulq_f32(prod, vld1q_f32(mask));
float32x2_t sum = vadd_f32(vget_low_f32(prod), vget_high_f32(prod)); /* ← CORRIGIDO */
sum = vpadd_f32(sum, sum);
return vdupq_lane_f32(sum, 0);
}

/* ── BUG CORRIGIDO: v4_cross ───────────────────────────
 * ANTES: vextq_f32(a, a, 3) → rotação errada, inclui 'w' no resultado
 * DEPOIS: cálculo scalar correto para cross product 3D
 */
FORCE_INLINE vec4 v4_cross(vec4 a, vec4 b)
{
float ax = vgetq_lane_f32(a, 0);
float ay = vgetq_lane_f32(a, 1);
float az = vgetq_lane_f32(a, 2);
float bx = vgetq_lane_f32(b, 0);
float by = vgetq_lane_f32(b, 1);
float bz = vgetq_lane_f32(b, 2);

return v4_set(ay * bz - az * by,
        az * bx - ax * bz,
        ax * by - ay * bx,
0.0f);
}

FORCE_INLINE vec4 v4_normalize3(vec4 a)
{
vec4 len2 = v4_dot3(a, a);
vec4 inv_len = vrsqrteq_f32(len2);
/* Newton-Raphson refinement */
inv_len = vmulq_f32(inv_len, vrsqrtsq_f32(vmulq_f32(len2, inv_len), inv_len));
return vmulq_f32(a, inv_len);
}

FORCE_INLINE vec4 v4_lerp(vec4 a, vec4 b, float t)
{
return v4_fma(a, v4_sub(b, a), v4_splat(t));
}

FORCE_INLINE float v4_hmin(vec4 a)
{
    float32x2_t mn = vpmin_f32(vget_low_f32(a), vget_high_f32(a));
    mn = vpmin_f32(mn, mn);
    return vget_lane_f32(mn, 0);
}

FORCE_INLINE float v4_hmax(vec4 a)
{
    float32x2_t mx = vpmax_f32(vget_low_f32(a), vget_high_f32(a));
    mx = vpmax_f32(mx, mx);
    return vget_lane_f32(mx, 0);
}

#define CLIP_LEFT   (1u << 0)
#define CLIP_RIGHT  (1u << 1)
#define CLIP_BOTTOM (1u << 2)
#define CLIP_TOP    (1u << 3)
#define CLIP_NEAR   (1u << 4)
#define CLIP_FAR    (1u << 5)

FORCE_INLINE u32 v4_clip_flags(vec4 pos)
{
float x = vgetq_lane_f32(pos, 0);
float y = vgetq_lane_f32(pos, 1);
float z = vgetq_lane_f32(pos, 2);
float w = vgetq_lane_f32(pos, 3);

u32 flags = 0;
if (x < -w) flags |= CLIP_LEFT;
if (x >  w) flags |= CLIP_RIGHT;
if (y < -w) flags |= CLIP_BOTTOM;
if (y >  w) flags |= CLIP_TOP;
if (z <  0) flags |= CLIP_NEAR;
if (z >  w) flags |= CLIP_FAR;
return flags;
}

#endif /* PS2RE_VEC4_NEON_H */