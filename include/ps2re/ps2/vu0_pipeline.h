#ifndef PS2RE_VU0_PIPELINE_H
#define PS2RE_VU0_PIPELINE_H

#include "ps2re/types.h"
#include "ps2re/math/vec4_neon.h"
#include "ps2re/math/mat4_neon.h"
#include "ps2re/async/task_graph.h"

/*
 * VU0 replacement — NEON/SVE2 compute pipeline.
 *
 * PS2 VU0: 4 FMAC + 1 FDIV @ 294MHz, 16KB I$ + 16KB D$
 * ARM64 NEON: FMA @ 2.5GHz, 64KB L1, unlimited program size
 *
 * The VU0 was used for:
 *   - Vertex skinning (bone matrix blend + transform)
 *   - Particle simulation (position + velocity + gravity)
 *   - Simple physics (collision response)
 *   - Cloth/spring simulation
 *
 * Each of these becomes a task in the graph.
 */

/* ── Skinning ────────────────────────────────────────── */
#define MAX_BONES 128

typedef struct {
    mat4  bone_matrices[MAX_BONES];
    int   num_bones;
} SkeletonPose;

typedef struct SkinningInput {
    /* Input vertices (SoA for SIMD) */
    VertexBatchSoA*  rest_pose;
    /* Bone weights: 4 per vertex (index + weight) */
    u16*             bone_indices;   /* count * 4 */
    f32*             bone_weights;   /* count * 4 */
    int              vertex_count;
    /* Output */
    VertexBatchSoA*  output;
    SkeletonPose*    pose;
} SkinningInput;

void vu0_task_skin_vertices(void* data);

/* ── Particles ───────────────────────────────────────── */
typedef struct {
    f32* pos_x;    f32* pos_y;    f32* pos_z;
    f32* vel_x;    f32* vel_y;    f32* vel_z;
    f32* life;     f32* max_life;
    f32* size;
    int  count;
    f32  gravity;
    f32  dt;
} ParticleSystem;

void vu0_task_simulate_particles(void* data);

/* ── Physics ─────────────────────────────────────────── */
typedef struct {
    f32* pos_x;    f32* pos_y;    f32* pos_z;
    f32* vel_x;    f32* vel_y;    f32* vel_z;
    f32* inv_mass;
    int  count;
    f32  gravity_y;
    f32  dt;
    f32  damping;
} PhysicsBodies;

void vu0_task_physics_step(void* data);

#endif /* PS2RE_VU0_PIPELINE_H */