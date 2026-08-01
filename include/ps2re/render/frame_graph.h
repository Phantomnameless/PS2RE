#ifndef PS2RE_FRAME_GRAPH_H
#define PS2RE_FRAME_GRAPH_H

#include "ps2re/types.h"
#include "ps2re/config.h"
#include "ps2re/memory/arena.h"
#include <vulkan/vulkan.h>

/*
 * Frame graph — automatic render pass ordering and resource aliasing.
 *
 * Replaces the PS2's manual GS framebuffer management.
 *
 * PS2 GS had fixed eDRAM layout:
 *   - You manually chose FBP (framebuffer pointer), ZBP (z-buffer pointer)
 *   - Multi-pass effects required manual eDRAM copy/swap
 *   - Each pass cost eDRAM bandwidth
 *
 * ARM64 Frame Graph:
 *   - Declare passes with inputs/outputs
 *   - Graph auto-schedules: merges compatible passes
 *   - Transient attachments stay in tile memory (zero bandwidth)
 *   - Resource aliasing: same memory for non-overlapping passes
 *
 * Inspired by: Frostbite's frame graph, Vulkan render pass composition.
 *
 * Typical PS2 game frame graph:
 *
 *   [Depth Pre-pass] → [Shadow Map] → [Main Opaque] → [Transparent] → [Post FX] → [UI]
 *
 * On TBDR:
 *   - Depth pre-pass fills HiZ → early fragment test in main pass
 *   - Shadow map: small render pass, stays in tile memory
 *   - Main opaque: TBDR does hidden surface removal → zero overdraw
 *   - Transparent: sorted per-tile
 *   - Post FX: compute shader (no render pass needed)
 *   - UI: simple overlay pass
 */

/* ── Resource types ──────────────────────────────────── */

typedef enum {
    FG_RESOURCE_BUFFER,
    FG_RESOURCE_IMAGE,
} FGResourceType;

typedef enum {
    FG_ACCESS_NONE           = 0,
    FG_ACCESS_VERTEX_READ    = 1 << 0,
    FG_ACCESS_FRAGMENT_READ  = 1 << 1,
    FG_ACCESS_FRAGMENT_WRITE = 1 << 2,
    FG_ACCESS_DEPTH_READ     = 1 << 3,
    FG_ACCESS_DEPTH_WRITE    = 1 << 4,
    FG_ACCESS_COMPUTE_READ   = 1 << 5,
    FG_ACCESS_COMPUTE_WRITE  = 1 << 6,
    FG_ACCESS_TRANSFER_READ  = 1 << 7,
    FG_ACCESS_TRANSFER_WRITE = 1 << 8,
    FG_ACCESS_COLOR_READ     = 1 << 9,
    FG_ACCESS_COLOR_WRITE    = 1 << 10,
} FGAccess;

typedef struct {
    FGResourceType type;
    u32            id;       /* unique resource identifier */

    union {
        struct {
            VkFormat        format;
            VkExtent2D      extent;
            u32             usage;         /* VkImageUsageFlags */
            FGAccess        access;        /* current access state */
            bool            transient;     /* stays in tile memory */
            bool            cleared;       /* clear on first use */
        } image;

        struct {
            VkDeviceSize    size;
            u32             usage;         /* VkBufferUsageFlags */
            FGAccess        access;
        } buffer;
    };

    /* Vulkan handles (resolved during compilation) */
    union {
        VkImage        image;
        VkBuffer       buffer;
    } handle;
    VkDeviceMemory   memory;
    VkImageView      view;
} FGResource;

/* ── Pass ────────────────────────────────────────────── */

typedef enum {
    FG_PASS_RENDER,
    FG_PASS_COMPUTE,
    FG_PASS_TRANSFER,
} FGPassType;

typedef void (*FGPassExecute)(VkCommandBuffer cmd, void* user_data);

typedef struct {
    const char*    name;
    FGPassType     type;

    /* Resource dependencies */
    u32            input_ids[8];          /* read resources */
    FGAccess       input_access[8];
    s32            input_count;

    u32            output_ids[8];         /* write resources */
    FGAccess       output_access[8];
    s32            output_count;

    /* For render passes: which resources are attachments */
    u32            color_attachments[4];
    s32            color_attachment_count;
    u32            depth_attachment;       /* UINT32_MAX if none */

    /* Execution callback */
    FGPassExecute  execute;
    void*          user_data;

    /* Resolved state */
    VkRenderPass   render_pass;
    VkFramebuffer  framebuffer;
    bool           resolved;
} FGPass;

/* ── Frame Graph ─────────────────────────────────────── */

typedef struct FrameGraph {
    FGPass     passes[MAX_RENDER_PASSES];
    s32        pass_count;

    FGResource resources[64];
    s32        resource_count;

    Arena*     arena;        /* per-frame allocation */
    VkDevice   device;
} FrameGraph;

/* Lifecycle */
Result frame_graph_init(FrameGraph* fg, VkDevice device, Arena* arena);
void   frame_graph_reset(FrameGraph* fg);

/* Building */
u32    frame_graph_add_image(FrameGraph* fg, const char* name,
                             VkFormat format, VkExtent2D extent,
                             bool transient, bool cleared);
u32    frame_graph_add_buffer(FrameGraph* fg, const char* name,
                              VkDeviceSize size);

FGPass* frame_graph_add_pass(FrameGraph* fg, const char* name,
                             FGPassType type);
void    frame_pass_reads(FGPass* pass, u32 resource_id, FGAccess access);
void    frame_pass_writes(FGPass* pass, u32 resource_id, FGAccess access);
void    frame_pass_color_target(FGPass* pass, u32 resource_id);
void    frame_pass_depth_target(FGPass* pass, u32 resource_id);
void    frame_pass_execute(FGPass* pass, FGPassExecute exec, void* user_data);

/* Compilation — auto-schedule, create render passes, resolve barriers */
Result frame_graph_compile(FrameGraph* fg);

/* Execution — record all passes into command buffer */
Result frame_graph_execute(const FrameGraph* fg, VkCommandBuffer cmd);

#endif /* PS2RE_FRAME_GRAPH_H */