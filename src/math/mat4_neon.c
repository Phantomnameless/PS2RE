#include "ps2re/math/mat4_neon.h"

void mat4_transform_batch_soa(const mat4* RESTRICT mvp,
                              VertexBatchSoA* RESTRICT batch)
{
    /* Pre-load matrix columns */
    float32x4_t mc0 = vld1q_f32((const float*)&mvp->col[0]);
    float32x4_t mc1 = vld1q_f32((const float*)&mvp->col[1]);
    float32x4_t mc2 = vld1q_f32((const float*)&mvp->col[2]);
    float32x4_t mc3 = vld1q_f32((const float*)&mvp->col[3]);

    /* Extract individual column elements for broadcast */
    float m00 = vgetq_lane_f32(mc0, 0), m10 = vgetq_lane_f32(mc0, 1);
    float m20 = vgetq_lane_f32(mc0, 2), m30 = vgetq_lane_f32(mc0, 3);
    float m01 = vgetq_lane_f32(mc1, 0), m11 = vgetq_lane_f32(mc1, 1);
    float m21 = vgetq_lane_f32(mc1, 2), m31 = vgetq_lane_f32(mc1, 3);
    float m02 = vgetq_lane_f32(mc2, 0), m12 = vgetq_lane_f32(mc2, 1);
    float m22 = vgetq_lane_f32(mc2, 2), m32 = vgetq_lane_f32(mc2, 3);
    float m03 = vgetq_lane_f32(mc3, 0), m13 = vgetq_lane_f32(mc3, 1);
    float m23 = vgetq_lane_f32(mc3, 2), m33 = vgetq_lane_f32(mc3, 3);

    float32x4_t row0_0 = vdupq_n_f32(m00);
    float32x4_t row0_1 = vdupq_n_f32(m10);
    float32x4_t row0_2 = vdupq_n_f32(m20);
    float32x4_t row0_3 = vdupq_n_f32(m30);

    float32x4_t row1_0 = vdupq_n_f32(m01);
    float32x4_t row1_1 = vdupq_n_f32(m11);
    float32x4_t row1_2 = vdupq_n_f32(m21);
    float32x4_t row1_3 = vdupq_n_f32(m31);

    float32x4_t row2_0 = vdupq_n_f32(m02);
    float32x4_t row2_1 = vdupq_n_f32(m12);
    float32x4_t row2_2 = vdupq_n_f32(m22);
    float32x4_t row2_3 = vdupq_n_f32(m32);

    float32x4_t row3_0 = vdupq_n_f32(m03);
    float32x4_t row3_1 = vdupq_n_f32(m13);
    float32x4_t row3_2 = vdupq_n_f32(m23);
    float32x4_t row3_3 = vdupq_n_f32(m33);

    int count = batch->count;
    int i = 0;

    /* Process 4 vertices per iteration (one NEON register = 4 floats) */
    for (; i + 4 <= count; i += 4) {
        PREFETCH_R(batch->x + i + 16);
        PREFETCH_R(batch->y + i + 16);
        PREFETCH_R(batch->z + i + 16);
        PREFETCH_R(batch->w + i + 16);

        float32x4_t vx = vld1q_f32(batch->x + i);
        float32x4_t vy = vld1q_f32(batch->y + i);
        float32x4_t vz = vld1q_f32(batch->z + i);
        float32x4_t vw = vld1q_f32(batch->w + i);

        /* out_x = m00*x + m10*y + m20*z + m30*w */
        float32x4_t ox = vmulq_f32(row0_0, vx);
        ox = vfmaq_f32(ox, row0_1, vy);
        ox = vfmaq_f32(ox, row0_2, vz);
        ox = vfmaq_f32(ox, row0_3, vw);

        float32x4_t oy = vmulq_f32(row1_0, vx);
        oy = vfmaq_f32(oy, row1_1, vy);
        oy = vfmaq_f32(oy, row1_2, vz);
        oy = vfmaq_f32(oy, row1_3, vw);

        float32x4_t oz = vmulq_f32(row2_0, vx);
        oz = vfmaq_f32(oz, row2_1, vy);
        oz = vfmaq_f32(oz, row2_2, vz);
        oz = vfmaq_f32(oz, row2_3, vw);

        float32x4_t ow = vmulq_f32(row3_0, vx);
        ow = vfmaq_f32(ow, row3_1, vy);
        ow = vfmaq_f32(ow, row3_2, vz);
        ow = vfmaq_f32(ow, row3_3, vw);

        vst1q_f32(batch->out_x + i, ox);
        vst1q_f32(batch->out_y + i, oy);
        vst1q_f32(batch->out_z + i, oz);
        vst1q_f32(batch->out_w + i, ow);

        PREFETCH_W(batch->out_x + i + 16);
        PREFETCH_W(batch->out_y + i + 16);

        /* Frustum clip test — scalar fallback (SIMD comparison possible too) */
        for (int j = 0; j < 4; j++) {
            float tx = vgetq_lane_f32(ox, j);
            float ty = vgetq_lane_f32(oy, j);
            float tz = vgetq_lane_f32(oz, j);
            float tw = vgetq_lane_f32(ow, j);

            u32 f = 0;
            if (tx < -tw) f |= CLIP_LEFT;
            if (tx >  tw) f |= CLIP_RIGHT;
            if (ty < -tw) f |= CLIP_BOTTOM;
            if (ty >  tw) f |= CLIP_TOP;
            if (tz <   0) f |= CLIP_NEAR;
            if (tz >  tw) f |= CLIP_FAR;
            batch->clip_flags[i + j] = f;
        }
    }

    /* Remainder (scalar) */
    for (; i < count; i++) {
        float vx = batch->x[i], vy = batch->y[i];
        float vz = batch->z[i], vw = batch->w[i];

        float ox = m00*vx + m10*vy + m20*vz + m30*vw;
        float oy = m01*vx + m11*vy + m21*vz + m31*vw;
        float oz = m02*vx + m12*vy + m22*vz + m32*vw;
        float ow = m03*vx + m13*vy + m23*vz + m33*vw;

        batch->out_x[i] = ox;
        batch->out_y[i] = oy;
        batch->out_z[i] = oz;
        batch->out_w[i] = ow;

        u32 f = 0;
        if (ox < -ow) f |= CLIP_LEFT;
        if (ox >  ow) f |= CLIP_RIGHT;
        if (oy < -ow) f |= CLIP_BOTTOM;
        if (oy >  ow) f |= CLIP_TOP;
        if (oz <   0) f |= CLIP_NEAR;
        if (oz >  ow) f |= CLIP_FAR;
        batch->clip_flags[i] = f;
    }
}