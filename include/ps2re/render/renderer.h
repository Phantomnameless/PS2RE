#ifndef PS2RE_RENDERER_H
#define PS2RE_RENDERER_H

#include "ps2re/types.h"
#include "ps2re/config.h"          /* ←: MAX_FRAMES_IN_FLIGHT */
#include "ps2re/memory/unified_mem.h"
#include "ps2re/ps2/gs_state.h"
#include <vulkan/vulkan.h>

#define RENDER_MAX_SWAPCHAIN_IMAGES 4

typedef struct Renderer {           /* ←: struct tag */
    VkInstance       instance;
    VkPhysicalDevice physical;
    VkDevice         device;
    VkQueue          graphics_queue;
    VkQueue          compute_queue;
    u32              graphics_family;
    u32              compute_family;
    s32              queue_same_family;

    VkSurfaceKHR     surface;
    VkSwapchainKHR   swapchain;
    VkImage          swap_images[RENDER_MAX_SWAPCHAIN_IMAGES];
    VkImageView      swap_views[RENDER_MAX_SWAPCHAIN_IMAGES];
    VkFormat         swap_format;
    VkExtent2D       swap_extent;
    u32              swap_image_count;
    u32              current_swap_index;

    VkCommandPool    cmd_pools[MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer  cmd_bufs[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore      image_available[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore      render_finished[MAX_FRAMES_IN_FLIGHT];
    VkFence          in_flight_fences[MAX_FRAMES_IN_FLIGHT];

    VkImage          depth_image;
    VkImageView      depth_view;
    VkDeviceMemory   depth_memory;

    VkRenderPass     main_render_pass;
    VkFramebuffer    framebuffers[RENDER_MAX_SWAPCHAIN_IMAGES];

    UnifiedMem       memory;

    u64              frame_count;
} Renderer;

Result renderer_init(Renderer* r, void* window_handle);
void   renderer_destroy(Renderer* r);
Result renderer_begin_frame(Renderer* r);
Result renderer_end_frame(Renderer* r);
Result renderer_wait_idle(Renderer* r);

void renderer_draw_gs_prim(Renderer* r, const GSState* gs,
                           const void* vertices, int vertex_count);

#endif /* PS2RE_RENDERER_H */