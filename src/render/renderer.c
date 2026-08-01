#include "ps2re/render/renderer.h"
#include "ps2re/config.h"
#include <string.h>
#include <stdlib.h>

/* ── Instance & Device ───────────────────────────────── */

static Result select_device(Renderer* r)
{
    u32 count = 0;
    vkEnumeratePhysicalDevices(r->instance, &count, NULL);
    if (count == 0) return ERR_GPU;

    VkPhysicalDevice* devices = (VkPhysicalDevice*)alloca(  /* ← cast explícito */
            count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(r->instance, &count, devices);

    r->physical = devices[0];
    for (u32 i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            r->physical = devices[i];
            break;
        }
    }

    u32 qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical, &qcount, NULL);
    VkQueueFamilyProperties* qprops = (VkQueueFamilyProperties*)alloca(
            qcount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical, &qcount, qprops);

    r->graphics_family = UINT32_MAX;
    r->compute_family  = UINT32_MAX;

    for (u32 i = 0; i < qcount; i++) {
        if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            r->graphics_family = i;
        }
        if ((qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            r->compute_family = i;
        }
    }

    if (r->graphics_family == UINT32_MAX) return ERR_GPU;

    r->queue_same_family = (r->compute_family == UINT32_MAX);
    if (r->queue_same_family) r->compute_family = r->graphics_family;

    return OK;
}

static Result create_device(Renderer* r)
{
    f32 priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2];
    int num_queues = 0;

    queues[num_queues++] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = r->graphics_family,
            .queueCount       = 1,
            .pQueuePriorities = &priority,
    };

    if (!r->queue_same_family) {
        queues[num_queues++] = (VkDeviceQueueCreateInfo){
                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = r->compute_family,
                .queueCount       = 1,
                .pQueuePriorities = &priority,
        };
    }

    /* Extensions for ARM64 mobile GPUs */
    const char* extensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo ci = {
            .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount    = (u32)num_queues,
            .pQueueCreateInfos       = queues,
            .enabledExtensionCount   = 1,
            .ppEnabledExtensionNames = extensions,
    };

    VkResult vr = vkCreateDevice(r->physical, &ci, NULL, &r->device);
    if (vr != VK_SUCCESS) return ERR_GPU;

    vkGetDeviceQueue(r->device, r->graphics_family, 0, &r->graphics_queue);
    vkGetDeviceQueue(r->device, r->compute_family, 0, &r->compute_queue);

    return OK;
}

/* ── Render Pass (TBDR-optimized) ────────────────────── */

static Result create_render_pass(Renderer* r)
{
    VkAttachmentDescription attachments[2] = {
            /* Color: swapchain image */
            [0] = {
                    .format         = r->swap_format,
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,  /* needed for display */
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
            /* Depth: transient (stays in tile memory on TBDR) */
            [1] = {
                    .format         = VK_FORMAT_D24_UNORM_S8_UINT,
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,  /* TBDR: no writeback! */
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
    };

    /* CRITICAL for TBDR: depth DONT_CARE store = zero external bandwidth for depth */
    /* This is how BotW saved fill-rate on Wii U/Switch */

    VkAttachmentReference color_ref = {
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference depth_ref = {
            .attachment = 1,
            .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &color_ref,
            .pDepthStencilAttachment = &depth_ref,
    };

    VkSubpassDependency dep = {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo rp = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 2,
            .pAttachments    = attachments,
            .subpassCount    = 1,
            .pSubpasses      = &subpass,
            .dependencyCount = 1,
            .pDependencies   = &dep,
    };

    return vkCreateRenderPass(r->device, &rp, NULL,
                              &r->main_render_pass) == VK_SUCCESS
           ? OK : ERR_GPU;
}

/* ── Per-Frame Resources ─────────────────────────────── */

static Result create_frame_resources(Renderer* r)
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkCommandPoolCreateInfo pool_ci = {
                .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = r->graphics_family,
        };
        TRY(vkCreateCommandPool(r->device, &pool_ci, NULL,
                                &r->cmd_pools[i]) == VK_SUCCESS ? OK : ERR_GPU);

        VkCommandBufferAllocateInfo alloc = {
                .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool        = r->cmd_pools[i],
                .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
        };
        TRY(vkAllocateCommandBuffers(r->device, &alloc,
                                     &r->cmd_bufs[i]) == VK_SUCCESS ? OK : ERR_GPU);

        VkSemaphoreCreateInfo sem = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        vkCreateSemaphore(r->device, &sem, NULL, &r->image_available[i]);
        vkCreateSemaphore(r->device, &sem, NULL, &r->render_finished[i]);

        VkFenceCreateInfo fence = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,  /* start signaled */
        };
        vkCreateFence(r->device, &fence, NULL, &r->in_flight_fences[i]);
    }
    return OK;
}

/* ── Init/Destroy ────────────────────────────────────── */

Result renderer_init(Renderer* r, void* window_handle)
{
    memset(r, 0, sizeof(*r));
    r->frame_count = 0;

    TRY(create_instance(r));
    TRY(select_device(r));
    TRY(create_device(r));

    /* Swapchain, depth, render pass, framebuffers, etc.
     * (window surface creation omitted for brevity — platform-specific) */

    TRY(unified_mem_init(&r->memory, r->device, r->physical,
                         64 * 1024 * 1024));  /* 64MB blocks */
    TRY(create_render_pass(r));
    TRY(create_frame_resources(r));

    return OK;
}

void renderer_destroy(Renderer* r)
{
    vkDeviceWaitIdle(r->device);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(r->device, r->in_flight_fences[i], NULL);
        vkDestroySemaphore(r->device, r->render_finished[i], NULL);
        vkDestroySemaphore(r->device, r->image_available[i], NULL);
        vkDestroyCommandPool(r->device, r->cmd_pools[i], NULL);
    }

    vkDestroyRenderPass(r->device, r->main_render_pass, NULL);
    unified_mem_destroy(&r->memory);
    vkDestroyDevice(r->device, NULL);
    vkDestroyInstance(r->instance, NULL);
}

/* ── Frame Submission ────────────────────────────────── */

Result renderer_begin_frame(Renderer* r)
{
    int idx = (int)(r->frame_count % MAX_FRAMES_IN_FLIGHT);

    /* Wait for this frame slot to be free (GPU finished with it) */
    vkWaitForFences(r->device, 1, &r->in_flight_fences[idx],
                    VK_TRUE, UINT64_MAX);
    vkResetFences(r->device, 1, &r->in_flight_fences[idx]);

    /* Acquire next swapchain image */
    VkResult vr = vkAcquireNextImageKHR(
            r->device, r->swapchain, UINT64_MAX,
            r->image_available[idx], VK_NULL_HANDLE,
            &r->current_swap_index);
    if (vr != VK_SUCCESS) return ERR_GPU;

    /* Begin command buffer */
    vkResetCommandPool(r->device, r->cmd_pools[idx], 0);
    VkCommandBufferBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(r->cmd_bufs[idx], &bi);

    return OK;
}

Result renderer_end_frame(Renderer* r)
{
    int idx = (int)(r->frame_count % MAX_FRAMES_IN_FLIGHT);
    VkCommandBuffer cmd = r->cmd_bufs[idx];

    vkEndCommandBuffer(cmd);

    /* Submit with semaphore synchronization (GPU-side, no CPU wait) */
    VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &r->image_available[idx],
            .pWaitDstStageMask    = &wait_stage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &r->render_finished[idx],
    };

    vkQueueSubmit(r->graphics_queue, 1, &si, r->in_flight_fences[idx]);

    /* Present */
    VkPresentInfoKHR pi = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &r->render_finished[idx],
            .swapchainCount     = 1,
            .pSwapchains        = &r->swapchain,
            .pImageIndices      = &r->current_swap_index,
    };
    vkQueuePresentKHR(r->graphics_queue, &pi);

    r->frame_count++;
    return OK;
}

Result renderer_wait_idle(Renderer* r)
{
    return vkDeviceWaitIdle(r->device) == VK_SUCCESS ? OK : ERR_GPU;
}

/* ── GS Primitive Draw ───────────────────────────────── */

void renderer_draw_gs_prim(struct Renderer* r, const GSState* gs,
                           const void* vertices, int vertex_count)
{
    (void)r;
    (void)gs;
    (void)vertices;
    (void)vertex_count;
}