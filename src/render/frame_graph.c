#include "ps2re/render/frame_graph.h"
#include <string.h>

Result frame_graph_init(FrameGraph* fg, VkDevice device, Arena* arena)
{
    memset(fg, 0, sizeof(*fg));
    fg->device = device;
    fg->arena  = arena;
    return OK;
}

void frame_graph_reset(FrameGraph* fg)
{
    /* Only reset transient Vulkan objects created during compile */
    for (s32 i = 0; i < fg->pass_count; i++) {
        FGPass* p = &fg->passes[i];
        if (p->framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(fg->device, p->framebuffer, NULL);
            p->framebuffer = VK_NULL_HANDLE;
        }
        if (p->render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(fg->device, p->render_pass, NULL);
            p->render_pass = VK_NULL_HANDLE;
        }
    }

    fg->pass_count     = 0;
    fg->resource_count = 0;
}

/* ── Resource Registration ───────────────────────────── */

u32 frame_graph_add_image(FrameGraph* fg, const char* name,
                          VkFormat format, VkExtent2D extent,
                          bool transient, bool cleared)
{
    if (fg->resource_count >= 64) return UINT32_MAX;

    u32 id = (u32)fg->resource_count++;
    FGResource* res = &fg->resources[id];

    memset(res, 0, sizeof(*res));
    res->type            = FG_RESOURCE_IMAGE;
    res->id              = id;
    res->image.format    = format;
    res->image.extent    = extent;
    res->image.transient = transient;
    res->image.cleared   = cleared;
    res->image.access    = FG_ACCESS_NONE;

    (void)name;  /* debug name storage omitted for brevity */
    return id;
}

u32 frame_graph_add_buffer(FrameGraph* fg, const char* name,
                           VkDeviceSize size)
{
    if (fg->resource_count >= 64) return UINT32_MAX;

    u32 id = (u32)fg->resource_count++;
    FGResource* res = &fg->resources[id];

    memset(res, 0, sizeof(*res));
    res->type       = FG_RESOURCE_BUFFER;
    res->id         = id;
    res->buffer.size = size;
    res->buffer.access = FG_ACCESS_NONE;

    (void)name;
    return id;
}

/* ── Pass Registration ───────────────────────────────── */

FGPass* frame_graph_add_pass(FrameGraph* fg, const char* name,
                             FGPassType type)
{
    if (fg->pass_count >= MAX_RENDER_PASSES) return NULL;

    FGPass* pass = &fg->passes[fg->pass_count++];
    memset(pass, 0, sizeof(*pass));

    pass->name             = name;
    pass->type             = type;
    pass->depth_attachment = UINT32_MAX;
    pass->render_pass      = VK_NULL_HANDLE;
    pass->framebuffer      = VK_NULL_HANDLE;
    return pass;
}

void frame_pass_reads(FGPass* pass, u32 resource_id, FGAccess access)
{
    if (pass->input_count >= 8) return;
    pass->input_ids[pass->input_count]     = resource_id;
    pass->input_access[pass->input_count]  = access;
    pass->input_count++;
}

void frame_pass_writes(FGPass* pass, u32 resource_id, FGAccess access)
{
    if (pass->output_count >= 8) return;
    pass->output_ids[pass->output_count]     = resource_id;
    pass->output_access[pass->output_count]  = access;
    pass->output_count++;
}

void frame_pass_color_target(FGPass* pass, u32 resource_id)
{
    if (pass->color_attachment_count >= 4) return;
    pass->color_attachments[pass->color_attachment_count++] = resource_id;
    frame_pass_writes(pass, resource_id, FG_ACCESS_COLOR_WRITE);
}

void frame_pass_depth_target(FGPass* pass, u32 resource_id)
{
    pass->depth_attachment = resource_id;
    frame_pass_writes(pass, resource_id, FG_ACCESS_DEPTH_WRITE);
}

void frame_pass_execute(FGPass* pass, FGPassExecute exec, void* user_data)
{
    pass->execute   = exec;
    pass->user_data = user_data;
}

/* ── Compile — Create Vulkan objects ─────────────────── */

static VkImageUsageFlags access_to_image_usage(FGAccess access)
{
    VkImageUsageFlags usage = 0;
    if (access & FG_ACCESS_COLOR_READ)    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (access & FG_ACCESS_COLOR_WRITE)   usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (access & FG_ACCESS_DEPTH_READ)    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (access & FG_ACCESS_DEPTH_WRITE)   usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (access & FG_ACCESS_FRAGMENT_READ) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (access & FG_ACCESS_COMPUTE_READ)  usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (access & FG_ACCESS_COMPUTE_WRITE) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (access & FG_ACCESS_TRANSFER_READ) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (access & FG_ACCESS_TRANSFER_WRITE)usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return usage;
}

static VkImageLayout access_to_layout(FGAccess access)
{
    if (access & FG_ACCESS_COLOR_WRITE)  return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (access & FG_ACCESS_DEPTH_WRITE)  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (access & FG_ACCESS_FRAGMENT_READ) return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (access & FG_ACCESS_COMPUTE_READ) return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (access & FG_ACCESS_COMPUTE_WRITE)return VK_IMAGE_LAYOUT_GENERAL;
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

static VkAccessFlags access_to_vk_access(FGAccess access)
{
    VkAccessFlags flags = 0;
    if (access & FG_ACCESS_VERTEX_READ)    flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (access & FG_ACCESS_FRAGMENT_READ)  flags |= VK_ACCESS_SHADER_READ_BIT;
    if (access & FG_ACCESS_FRAGMENT_WRITE) flags |= VK_ACCESS_SHADER_WRITE_BIT;
    if (access & FG_ACCESS_DEPTH_READ)     flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (access & FG_ACCESS_DEPTH_WRITE)    flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (access & FG_ACCESS_COMPUTE_READ)   flags |= VK_ACCESS_SHADER_READ_BIT;
    if (access & FG_ACCESS_COMPUTE_WRITE)  flags |= VK_ACCESS_SHADER_WRITE_BIT;
    if (access & FG_ACCESS_COLOR_READ)     flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (access & FG_ACCESS_COLOR_WRITE)    flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    return flags;
}

static VkPipelineStageFlags access_to_stage(FGAccess access, FGPassType pass_type)
{
    if (pass_type == FG_PASS_COMPUTE)
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (pass_type == FG_PASS_TRANSFER)
        return VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkPipelineStageFlags stages = 0;
    if (access & (FG_ACCESS_VERTEX_READ))
        stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (access & (FG_ACCESS_FRAGMENT_READ | FG_ACCESS_FRAGMENT_WRITE))
        stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (access & (FG_ACCESS_DEPTH_READ | FG_ACCESS_DEPTH_WRITE))
        stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    if (access & (FG_ACCESS_COLOR_READ | FG_ACCESS_COLOR_WRITE))
        stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (access & (FG_ACCESS_COMPUTE_READ | FG_ACCESS_COMPUTE_WRITE))
        stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    return stages;
}

Result frame_graph_compile(FrameGraph* fg)
{
    for (s32 p = 0; p < fg->pass_count; p++) {
        FGPass* pass = &fg->passes[p];

        if (pass->type != FG_PASS_RENDER) continue;

        /* Create render pass for this pass */
        VkAttachmentDescription attachments[5]; /* max 4 color + 1 depth */
        VkAttachmentReference color_refs[4];
        VkAttachmentReference depth_ref = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
        u32 attachment_count = 0;

        /* Color attachments */
        for (s32 i = 0; i < pass->color_attachment_count; i++) {
            u32 res_id = pass->color_attachments[i];
            FGResource* res = &fg->resources[res_id];

            bool is_first_use = (p == 0);  /* simplified */
            FGAccess prev_access = FG_ACCESS_NONE;
            /* Find previous pass that wrote this resource */
            for (s32 pp = 0; pp < p; pp++) {
                for (s32 o = 0; o < fg->passes[pp].output_count; o++) {
                    if (fg->passes[pp].output_ids[o] == res_id) {
                        prev_access = fg->passes[pp].output_access[o];
                        is_first_use = false;
                    }
                }
            }

            attachments[attachment_count] = (VkAttachmentDescription){
                    .format         = res->image.format,
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = res->image.cleared
                                      ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                      : VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = is_first_use
                                      ? VK_IMAGE_LAYOUT_UNDEFINED
                                      : access_to_layout(prev_access),
                    .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };

            color_refs[i] = (VkAttachmentReference){
                    .attachment = attachment_count,
                    .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            };
            attachment_count++;
        }

        /* Depth attachment */
        if (pass->depth_attachment != UINT32_MAX) {
            FGResource* dres = &fg->resources[pass->depth_attachment];

            attachments[attachment_count] = (VkAttachmentDescription){
                    .format         = dres->image.format,
                    .samples        = VK_SAMPLE_COUNT_1_BIT,
                    .loadOp         = dres->image.cleared
                                      ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                      : VK_ATTACHMENT_LOAD_OP_LOAD,
                    /* TBDR OPTIMIZATION: store DONT_CARE if depth not needed later.
                     * This keeps depth entirely in tile memory — zero external bandwidth.
                     * This is THE trick that made BotW run on Wii U/Switch. */
                    .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                    .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };

            depth_ref = (VkAttachmentReference){
                    .attachment = attachment_count,
                    .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            };
            attachment_count++;
        }

        /* Subpass */
        VkSubpassDescription subpass = {
                .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount    = (u32)pass->color_attachment_count,
                .pColorAttachments       = pass->color_attachment_count > 0
                                           ? color_refs : NULL,
                .pDepthStencilAttachment = pass->depth_attachment != UINT32_MAX
                                           ? &depth_ref : NULL,
        };

        /* Subpass dependency — automatic barrier */
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

        VkRenderPassCreateInfo rp_ci = {
                .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = attachment_count,
                .pAttachments    = attachments,
                .subpassCount    = 1,
                .pSubpasses      = &subpass,
                .dependencyCount = 1,
                .pDependencies   = &dep,
        };

        if (vkCreateRenderPass(fg->device, &rp_ci, NULL,
                               &pass->render_pass) != VK_SUCCESS)
            return ERR_GPU;

        pass->resolved = true;
    }

    return OK;
}

/* ── Execute — Record into command buffer ────────────── */

Result frame_graph_execute(const FrameGraph* fg, VkCommandBuffer cmd)
{
    for (s32 p = 0; p < fg->pass_count; p++) {
        const FGPass* pass = &fg->passes[p];

        if (pass->type == FG_PASS_COMPUTE) {
            /* Compute pass — no render pass needed */
            if (pass->execute) {
                pass->execute((VkCommandBuffer)cmd, pass->user_data);
            }
            continue;
        }

        if (pass->type == FG_PASS_TRANSFER) {
            /* Transfer pass — barriers + copy */
            if (pass->execute) {
                pass->execute((VkCommandBuffer)cmd, pass->user_data);
            }
            continue;
        }

        /* Render pass */
        if (pass->render_pass == VK_NULL_HANDLE) continue;

        /* Image barriers for inputs */
        for (s32 i = 0; i < pass->input_count; i++) {
            u32 res_id = pass->input_ids[i];
            FGResource* res = &fg->resources[res_id];
            if (res->type != FG_RESOURCE_IMAGE) continue;

            VkImageMemoryBarrier barrier = {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask       = 0,
                    .dstAccessMask       = access_to_vk_access(pass->input_access[i]),
                    .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout           = access_to_layout(pass->input_access[i]),
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = res->handle.image,
                    .subresourceRange    = {
                            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel   = 0,
                            .levelCount     = 1,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                    },
            };

            vkCmdPipelineBarrier(
                    cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    access_to_stage(pass->input_access[i], pass->type),
                    0, 0, NULL, 0, NULL, 1, &barrier);
        }

        /* Begin render pass */
        VkClearValue clears[5];
        s32 clear_count = 0;
        for (s32 i = 0; i < pass->color_attachment_count; i++) {
            clears[clear_count++].color = (VkClearColorValue){{0, 0, 0, 1}};
        }
        if (pass->depth_attachment != UINT32_MAX) {
            clears[clear_count++].depthStencil =
                    (VkClearDepthStencilValue){1.0f, 0};
        }

        /* Use pass framebuffer if available, otherwise null (caller provides) */
        VkRenderPassBeginInfo rpbi = {
                .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass      = pass->render_pass,
                .framebuffer     = pass->framebuffer,
                .renderArea      = {{0, 0}, {1920, 1080}},  /* dynamic */
                .clearValueCount = (u32)clear_count,
                .pClearValues    = clears,
        };

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        /* Execute pass callback */
        if (pass->execute) {
            pass->execute((VkCommandBuffer)cmd, pass->user_data);
        }

        vkCmdEndRenderPass(cmd);
    }

    return OK;
}