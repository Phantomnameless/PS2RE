#include "ps2re/sched/task_builder.h"
#include "ps2re/ps2/vu0_pipeline.h"
#include "ps2re/ps2/vu1_pipeline.h"
#include "ps2re/ps2/ee.h"

void build_frame_task_graph(TaskGraph* graph,
                            EEContext* ee,
                            GSState* gs,
                            GIFContext* gif)
{
    /*
     * Task dependency graph:
     *
     * Each task runs on a worker thread via work-stealing.
     * Dependencies ensure correctness without locks.
     *
     * Legend: ──→ = depends on (must wait for completion)
     *
     *   [ee_logic] ──→ [culling] ──→ [build_draw] ──→ [gpu_submit]
     *        │              │
     *        ├──→ [physics] ┤
     *        ├──→ [anim]    ├──→ [skinning] ──→ [build_draw]
     *        ├──→ [particle]┘
     *        │
     *        └──→ [audio] (independent, fire and forget)
     */

    /* ── Node 1: Game logic (EE) ─────────────────────── */
    Task* t_logic = task_graph_add(graph, ee_task_update_logic,
                                   ee, sizeof(EEContext), "ee_logic");

    /* ── Node 2: Physics (VU0 NEON) ──────────────────── */
    PhysicsBodies physics = {
            /* ... populate from game state ... */
            .gravity_y = -9.8f,
            .dt = ee->delta_time,
            .damping = 0.99f,
    };
    Task* t_physics = task_graph_add(graph, vu0_task_physics_step,
                                     &physics, sizeof(physics), "physics");
    task_graph_depend(t_logic, t_physics);

    /* ── Node 3: Particle simulation (VU0 NEON) ──────── */
    ParticleSystem particles = {
            .gravity = -4.9f,
            .dt = ee->delta_time,
    };
    Task* t_particles = task_graph_add(graph, vu0_task_simulate_particles,
                                       &particles, sizeof(particles), "particles");
    task_graph_depend(t_logic, t_particles);

    /* ── Node 4: Vertex transform (VU1 replacement) ──── */
    VU1Input vu1_in = {0};
    Task* t_vu1 = task_graph_add(graph, vu1_task_transform_vertices,
                                 &vu1_in, sizeof(vu1_in), "vu1_transform");
    task_graph_depend(t_logic, t_vu1);
    task_graph_depend(t_physics, t_vu1);   /* needs updated positions */
    task_graph_depend(t_particles, t_vu1); /* needs particle positions */

    /* ── Node 5: Build draw commands ─────────────────── */
    Task* t_draw = task_graph_add(graph, ee_task_build_draw_commands,
                                  ee, sizeof(EEContext), "build_draw");
    task_graph_depend(t_vu1, t_draw);

    /* ── Priority hints for work stealing ────────────── */
    /* Higher priority = stolen first when other threads are idle */
    task_graph_set_priority(t_logic,   10);  /* critical path */
    task_graph_set_priority(t_vu1,      8);
    task_graph_set_priority(t_draw,     9);
    task_graph_set_priority(t_physics,  5);
    task_graph_set_priority(t_particles, 5);
}