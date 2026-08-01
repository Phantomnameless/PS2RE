#ifndef PS2RE_CONFIG_H
#define PS2RE_CONFIG_H

/* ── Frame pipeline ──────────────────────────────────── */
#define MAX_FRAMES_IN_FLIGHT     3     /* triple buffering */
#define MAX_TASKS_PER_FRAME      4096
#define MAX_DRAW_CALLS_PER_FRAME 8192

/* ── Async system ────────────────────────────────────── */
#define WORKER_THREAD_COUNT      4     /* match big cores */
#define RING_BUFFER_DEFAULT_SIZE 4096  /* must be power of 2 */
#define TASK_GRAPH_MAX_DEPENDENTS 8

/* ── Memory ──────────────────────────────────────────── */
#define ARENA_PAGE_SIZE          (2 * 1024 * 1024)  /* 2MB pages */
#define FRAME_ARENA_SIZE         (16 * 1024 * 1024) /* 16MB per frame */
#define POOL_SLAB_SIZE           (64 * 1024)        /* 64KB slabs */

/* ── Rendering ───────────────────────────────────────── */
#define MAX_RENDER_PASSES        8
#define MAX_SUBPASSES_PER_PASS   4
#define MAX_ATTACHMENTS          4
#define MAX_DESCRIPTOR_SETS      4
#define TEXTURE_CACHE_SIZE_MB    64
#define MAX_TEXTURES             2048
#define MAX_VERTEX_BUFFERS       64

/* ── PS2 subsystem ───────────────────────────────────── */
#define VU1_MAX_VERTICES         65536
#define VU1_MAX_MICROPROGRAM     256   /* words, same as PS2 */
#define GS_MAX_PRIM_PER_FRAME    65536
#define DMA_RING_SIZE            8192  /* QW entries */
#define GIF_TAG_FIFO_SIZE        1024

/* ── Display ─────────────────────────────────────────── */
#define DEFAULT_WIDTH            1920
#define DEFAULT_HEIGHT           1080
#define TARGET_FPS               60

#endif /* PS2RE_CONFIG_H */