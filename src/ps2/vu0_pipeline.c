#include "ps2re/ps2/vu0_pipeline.h"
#include <string.h>

/* ── Skinning Task ───────────────────────────────────── */
/*
 * PS2 VU0 did skinning in ~20-30 cycles/vertex with stalls.
 * ARM64 NEON: ~4-6 cycles/vertex, no stalls, batch of 4.
 *
 * Per vertex: blend 4 bone matrices, multiply by position.
 * = 4 × (4×4 matrix blend) + 1 × (4×4 × 4-vector) transform
 * = ~16 FMA (blend) + 4 FMA (transform) = 20 FMA
 * At 4-wide SIMD, 5 iterations per vertex.
 */

void vu0_task_skin_vertices(void* data)
{
    SkinningInput* in = (SkinningInput*)data;
    int count = in->vertex_count;
    SkeletonPose* pose = in->pose;

    for (int i = 0; i < count; i++) {
        PREFETCH_R(in->bone_indices + (i + 4) * 4);
        PREFETCH_R(in->bone_weights + (i + 4) * 4);

        /* Load 4 bone indices and weights */
        u16 bi[4] = {
                in->bone_indices[i * 4 + 0],
                in->bone_indices[i * 4 + 1],
                in->bone_indices[i * 4 + 2],
                in->bone_indices[i * 4 + 3],
        };
        f32 bw[4] = {
                in->bone_weights[i * 4 + 0],
                in->bone_weights[i * 4 + 1],
                in->bone_weights[i * 4 + 2],
                in->bone_weights[i * 4 + 3],
        };

        /* Blend bone matrices: M = bw[0]*B[bi[0]] + bw[1]*B[bi[1]] + ... */
        mat4 blended;
        for (int c = 0; c < 4; c++) {
            blended.col[c] = vmulq_n_f32(pose->bone_matrices[bi[0]].col[c], bw[0]);
            blended.col[c] = vfmaq_n_f32(blended.col[c],
                                         pose->bone_matrices[bi[1]].col[c], bw[1]);
            blended.col[c] = vfmaq_n_f32(blended.col[c],
                                         pose->bone_matrices[bi[2]].col[c], bw[2]);
            blended.col[c] = vfmaq_n_f32(blended.col[c],
                                         pose->bone_matrices[bi[3]].col[c], bw[3]);
        }

        /* Transform position */
        vec4 pos = v4_set(in->rest_pose->x[i],
                          in->rest_pose->y[i],
                          in->rest_pose->z[i],
                          in->rest_pose->w[i]);

        vec4 out_pos = mat4_mul_vec4(blended, pos);

        in->output->x[i] = vgetq_lane_f32(out_pos, 0);
        in->output->y[i] = vgetq_lane_f32(out_pos, 1);
        in->output->z[i] = vgetq_lane_f32(out_pos, 2);
        in->output->w[i] = vgetq_lane_f32(out_pos, 3);
    }
}

/* ── Particle Simulation ─────────────────────────────── */
/*
 * PS2 VU0 particles: ~10 cycles/particle with stalls.
 * ARM64 NEON: ~2 cycles/particle (4-wide, no stalls).
 *
 * Simple Euler integration:
 *   vel += gravity * dt
 *   pos += vel * dt
 *   life -= dt
 */

void vu0_task_simulate_particles(void* data)
{
    ParticleSystem* ps = (ParticleSystem*)data;

    float32x4_t grav = vdupq_n_f32(ps->gravity * ps->dt);
    float32x4_t dt   = vdupq_n_f32(ps->dt);

    int i = 0;
    for (; i + 4 <= ps->count; i += 4) {
        PREFETCH_R(ps->vel_y + i + 16);
        PREFETCH_R(ps->pos_y + i + 16);
        PREFETCH_R(ps->life + i + 16);

        float32x4_t vy = vld1q_f32(ps->vel_y + i);
        float32x4_t px = vld1q_f32(ps->pos_x + i);
        float32x4_t py = vld1q_f32(ps->pos_y + i);
        float32x4_t pz = vld1q_f32(ps->pos_z + i);
        float32x4_t vx = vld1q_f32(ps->vel_x + i);
        float32x4_t vz = vld1q_f32(ps->vel_z + i);
        float32x4_t life = vld1q_f32(ps->life + i);

        /* vel.y += gravity * dt */
        vy = vaddq_f32(vy, grav);

        /* pos += vel * dt */
        px = vfmaq_f32(px, vx, dt);
        py = vfmaq_f32(py, vy, dt);
        pz = vfmaq_f32(pz, vz, dt);

        /* life -= dt */
        life = vsubq_f32(life, dt);

        vst1q_f32(ps->vel_y + i, vy);
        vst1q_f32(ps->pos_x + i, px);
        vst1q_f32(ps->pos_y + i, py);
        vst1q_f32(ps->pos_z + i, pz);
        vst1q_f32(ps->life + i, life);
    }

    /* Remainder */
    for (; i < ps->count; i++) {
        ps->vel_y[i] += ps->gravity * ps->dt;
        ps->pos_x[i] += ps->vel_x[i] * ps->dt;
        ps->pos_y[i] += ps->vel_y[i] * ps->dt;
        ps->pos_z[i] += ps->vel_z[i] * ps->dt;
        ps->life[i]  -= ps->dt;
    }
}

/* ── Physics Step ────────────────────────────────────── */
void vu0_task_physics_step(void* data)
{
    PhysicsBodies* pb = (PhysicsBodies*)data;

    float32x4_t g  = vdupq_n_f32(pb->gravity_y * pb->dt);
    float32x4_t dt = vdupq_n_f32(pb->dt);
    float32x4_t damp = vdupq_n_f32(pb->damping);

    int i = 0;
    for (; i + 4 <= pb->count; i += 4) {
        float32x4_t inv_m = vld1q_f32(pb->inv_mass + i);
        float32x4_t vy    = vld1q_f32(pb->vel_y + i);

        /* vel.y += gravity * inv_mass * dt (skip if infinite mass) */
        float32x4_t accel = vmulq_f32(g, inv_m);
        vy = vfmaq_f32(vy, accel, dt);

        /* damping */
        vy = vmulq_f32(vy, damp);

        /* integrate position */
        float32x4_t px = vld1q_f32(pb->pos_x + i);
        float32x4_t py = vld1q_f32(pb->pos_y + i);
        float32x4_t pz = vld1q_f32(pb->pos_z + i);
        float32x4_t vx = vld1q_f32(pb->vel_x + i);
        float32x4_t vz = vld1q_f32(pb->vel_z + i);

        px = vfmaq_f32(px, vx, dt);
        py = vfmaq_f32(py, vy, dt);
        pz = vfmaq_f32(pz, vz, dt);

        vst1q_f32(pb->vel_y + i, vy);
        vst1q_f32(pb->pos_x + i, px);
        vst1q_f32(pb->pos_y + i, py);
        vst1q_f32(pb->pos_z + i, pz);
    }
}