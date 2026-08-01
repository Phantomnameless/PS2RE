#ifndef PS2RE_MAT4_NEON_H
#define PS2RE_MAT4_NEON_H

#include "ps2re/math/vec4_neon.h"

/*
 * 4×4 matrix using 4 column vectors.
 * Column-major to match Vulkan/OpenGL convention.
 *
 * PS2 VU1 did MVP*vertex in 10-20 cycles with pipeline stalls.
 * ARM64 NEON does it in 4 FMA instructions = 4 cycles, no stalls.
 * For batches: SoA layout processes 4 vertices simultaneously.
 */

typedef struct ALIGN16 {
    vec4 col[4];  /* column-major: col[0] = first column */
} mat4;

/* Load/Store */
FORCE_INLINE mat4 mat4_load(const float* m)
{
    mat4 out;
    out.col[0] = vld1q_f32(m + 0);
    out.col[1] = vld1q_f32(m + 4);
    out.col[2] = vld1q_f32(m + 8);
    out.col[3] = vld1q_f32(m + 12);
    return out;
}

FORCE_INLINE void mat4_store(float* m, mat4 mat)
{
    vst1q_f32(m + 0,  mat.col[0]);
    vst1q_f32(m + 4,  mat.col[1]);
    vst1q_f32(m + 8,  mat.col[2]);
    vst1q_f32(m + 12, mat.col[3]);
}

/* Identity */
FORCE_INLINE mat4 mat4_identity(void)
{
    mat4 m;
    m.col[0] = v4_set(1, 0, 0, 0);
    m.col[1] = v4_set(0, 1, 0, 0);
    m.col[2] = v4_set(0, 0, 1, 0);
    m.col[3] = v4_set(0, 0, 0, 1);
    return m;
}

/* Matrix × Vector (THE hot path — replaces VU1 MVP transform) */
FORCE_INLINE vec4 mat4_mul_vec4(mat4 m, vec4 v)
{
/* Broadcast each component, FMA each column */
vec4 xxxx = vdupq_laneq_f32(v, 0);
vec4 yyyy = vdupq_laneq_f32(v, 1);
vec4 zzzz = vdupq_laneq_f32(v, 2);
vec4 wwww = vdupq_laneq_f32(v, 3);

vec4 result = vmulq_f32(m.col[0], xxxx);
result = vfmaq_f32(result, m.col[1], yyyy);
result = vfmaq_f32(result, m.col[2], zzzz);
result = vfmaq_f32(result, m.col[3], wwww);
return result;
}

/* Matrix × Matrix */
FORCE_INLINE mat4 mat4_mul(mat4 a, mat4 b)
{
mat4 out;
out.col[0] = mat4_mul_vec4(a, b.col[0]);
out.col[1] = mat4_mul_vec4(a, b.col[1]);
out.col[2] = mat4_mul_vec4(a, b.col[2]);
out.col[3] = mat4_mul_vec4(a, b.col[3]);
return out;
}

/* Perspective projection (PS2-style with [0,w] depth) */
FORCE_INLINE mat4 mat4_perspective(float fov_rad, float aspect,
                                   float z_near, float z_far)
{
    float f = 1.0f / tanf(fov_rad * 0.5f);
    float dz = z_far - z_near;

    mat4 m = {{0}};
    m.col[0] = v4_set(f / aspect, 0, 0, 0);
    m.col[1] = v4_set(0, f, 0, 0);
    m.col[2] = v4_set(0, 0, z_far / dz, 1);
    m.col[3] = v4_set(0, 0, -(z_far * z_near) / dz, 0);
    return m;
}

/* Look-at (view matrix) */
FORCE_INLINE mat4 mat4_lookat(vec4 eye, vec4 target, vec4 up)
{
vec4 fwd  = v4_normalize3(v4_sub(target, eye));
vec4 side = v4_normalize3(v4_cross(fwd, up));
vec4 new_up = v4_cross(side, fwd);

mat4 m;
m.col[0] = v4_set(vgetq_lane_f32(side, 0),
        vgetq_lane_f32(new_up, 0),
        vgetq_lane_f32(v4_neg(fwd), 0), 0);
m.col[1] = v4_set(vgetq_lane_f32(side, 1),
        vgetq_lane_f32(new_up, 1),
        vgetq_lane_f32(v4_neg(fwd), 1), 0);
m.col[2] = v4_set(vgetq_lane_f32(side, 2),
        vgetq_lane_f32(new_up, 2),
        vgetq_lane_f32(v4_neg(fwd), 2), 0);
m.col[3] = v4_set(-v4_hmin(v4_dot3(side, eye)),
-v4_hmin(v4_dot3(new_up, eye)),
v4_hmin(v4_dot3(fwd, eye)), 1);
return m;
}

/* Translation */
FORCE_INLINE mat4 mat4_translate(float x, float y, float z)
{
    mat4 m = mat4_identity();
    m.col[3] = v4_set(x, y, z, 1);
    return m;
}

/* Scale */
FORCE_INLINE mat4 mat4_scale(float sx, float sy, float sz)
{
    mat4 m = {{0}};
    m.col[0] = v4_set(sx, 0, 0, 0);
    m.col[1] = v4_set(0, sy, 0, 0);
    m.col[2] = v4_set(0, 0, sz, 0);
    m.col[3] = v4_set(0, 0, 0, 1);
    return m;
}

/* ── Batch Transform (SoA) — The VU1 killer ──────────── */
/*
 * Transform N vertices by same matrix. SoA layout = 4 vertices per NEON op.
 * This is how you get 500M+ polygons/sec on ARM64.
 */

typedef struct {
    float* x;   /* N floats */
    float* y;
    float* z;
    float* w;
    float* out_x;
    float* out_y;
    float* out_z;
    float* out_w;
    u32*   clip_flags;
    int    count;
} VertexBatchSoA;

void mat4_transform_batch_soa(const mat4* RESTRICT mvp,
                              VertexBatchSoA* RESTRICT batch);

#endif /* PS2RE_MAT4_NEON_H */