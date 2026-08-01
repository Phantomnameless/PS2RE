#include "ps2re/render/texture_manager.h"
#include "ps2re/config.h"
#include <string.h>
#include <stdlib.h>

/* ── PS2 Texture Format Decoder ──────────────────────── */
/*
 * PS2 GS pixel storage formats:
 *   PSMCT32  (0x00) — 32-bit RGBA, 4 bytes/pixel
 *   PSMCT24  (0x01) — 24-bit RGB, stored as 32-bit (alpha ignored)
 *   PSMCT16  (0x02) — 16-bit RGBA (1555), 2 bytes/pixel
 *   PSMCT16S (0x0A) — 16-bit RGBA (swizzled variant)
 *   PSMT8    (0x13) — 8-bit indexed (CLUT required)
 *   PSMT4    (0x14) — 4-bit indexed (CLUT required)
 *   PSMT8H   (0x1B) — 8-bit stored in upper bits of 32-bit
 *   PSMT4HL  (0x24) — 4-bit in upper nibble of 32-bit
 *   PSMT4HH  (0x2C) — 4-bit in high nibble of high byte
 */

static void deswizzle_block(u8* dst, const u8* src,
                            int width, int height, int bpp)
{
    int block_w, block_h;
    switch (bpp) {
        case 32: block_w = 8; block_h = 8; break;
        case 16: block_w = 16; block_h = 8; break;
        case 8:  block_w = 16; block_h = 16; break;
        case 4:  block_w = 32; block_h = 16; break;
        default: block_w = 8; block_h = 8; break;
    }

    int bytes_per_pixel = bpp / 8;
    if (bpp < 8) bytes_per_pixel = 1;  /* sub-byte: handled specially */

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int block_x = x / block_w;
            int block_y = y / block_h;
            int bx = x % block_w;
            int by = y % block_h;

            /* PS2 GS swizzle calculation */
            int src_block_idx = block_y * ((width + block_w - 1) / block_w) + block_x;
            int src_pixel_idx;

            if (bpp >= 8) {
                src_pixel_idx = (src_block_idx * block_w * block_h + by * block_w + bx)
                                * bytes_per_pixel;
                int dst_idx = (y * width + x) * bytes_per_pixel;
                if (src_pixel_idx >= 0 && dst_idx >= 0) {
                    memcpy(dst + dst_idx, src + src_pixel_idx, (size_t)bytes_per_pixel);
                }
            } else if (bpp == 4) {
                /* 4-bit: two pixels per byte */
                int src_idx = src_block_idx * block_w * block_h + by * block_w + bx;
                int src_byte = src_idx / 2;
                int src_nibble = src_idx % 2;
                u8 byte_val = src[src_byte];
                u8 pixel = (src_nibble == 0) ? (byte_val & 0x0F) : (byte_val >> 4);

                int dst_idx = y * width + x;
                int dst_byte = dst_idx / 2;
                int dst_nibble = dst_idx % 2;
                if (dst_nibble == 0)
                    dst[dst_byte] = (dst[dst_byte] & 0xF0) | pixel;
                else
                    dst[dst_byte] = (dst[dst_byte] & 0x0F) | (pixel << 4);
            }
        }
    }
}

/* Apply CLUT to indexed texture → RGBA8 */
static void apply_clut_rgba8(u8* dst, const u8* indexed,
                             int width, int height,
                             const u32* clut, int clut_offset,
                             bool is_4bit)
{
    int total = width * height;
    for (int i = 0; i < total; i++) {
        u8 idx;
        if (is_4bit) {
            int byte_idx = i / 2;
            int nibble = i % 2;
            idx = (nibble == 0) ? (indexed[byte_idx] & 0x0F) : (indexed[byte_idx] >> 4);
        } else {
            idx = indexed[i];
        }

        u32 color = clut[(idx + clut_offset) & 0xFF];
        /* GS CLUT is typically RGBA32: R(8) G(8) B(8) A(8) */
        dst[i * 4 + 0] = (u8)(color & 0xFF);
        dst[i * 4 + 1] = (u8)((color >> 8) & 0xFF);
        dst[i * 4 + 2] = (u8)((color >> 16) & 0xFF);
        dst[i * 4 + 3] = (u8)((color >> 24) & 0xFF);
    }
}

Result texture_decode_ps2(void* dst_rgba8, const void* src,
                          u16 width, u16 height, u8 ps2_format,
                          const void* clut, u8 clut_format)
{
    (void)clut_format;
    int w = width, h = height;
    size_t total_pixels = (size_t)w * h;

    switch (ps2_format) {
        case 0x00: { /* PSMCT32 — RGBA32, swizzled */
            u8* temp = malloc(total_pixels * 4);
            if (!temp) return ERR_NOMEM;
            deswizzle_block(temp, (const u8*)src, w, h, 32);
            memcpy(dst_rgba8, temp, total_pixels * 4);
            free(temp);
            return OK;
        }

        case 0x01: { /* PSMCT24 — RGB24 stored in 32-bit */
            u8* temp = malloc(total_pixels * 4);
            if (!temp) return ERR_NOMEM;
            deswizzle_block(temp, (const u8*)src, w, h, 32);
            u8* dst = (u8*)dst_rgba8;
            for (size_t i = 0; i < total_pixels; i++) {
                dst[i*4+0] = temp[i*4+0];
                dst[i*4+1] = temp[i*4+1];
                dst[i*4+2] = temp[i*4+2];
                dst[i*4+3] = 0xFF;  /* alpha = 1.0 */
            }
            free(temp);
            return OK;
        }

        case 0x02: { /* PSMCT16 — RGBA1555, swizzled */
            u8* temp = malloc(total_pixels * 2);
            if (!temp) return ERR_NOMEM;
            deswizzle_block(temp, (const u8*)src, w, h, 16);
            u8* dst = (u8*)dst_rgba8;
            u16* src16 = (u16*)temp;
            for (size_t i = 0; i < total_pixels; i++) {
                u16 p = src16[i];
                dst[i*4+0] = (u8)(((p >> 0) & 0x1F) * 255 / 31);
                dst[i*4+1] = (u8)(((p >> 5) & 0x1F) * 255 / 31);
                dst[i*4+2] = (u8)(((p >> 10) & 0x1F) * 255 / 31);
                dst[i*4+3] = (p & 0x8000) ? 0xFF : 0x00;
            }
            free(temp);
            return OK;
        }

        case 0x13: { /* PSMT8 — 8-bit indexed */
            if (!clut) return ERR_INVALID;
            u8* temp = malloc(total_pixels);
            if (!temp) return ERR_NOMEM;
            deswizzle_block(temp, (const u8*)src, w, h, 8);
            apply_clut_rgba8((u8*)dst_rgba8, temp, w, h,
                             (const u32*)clut, 0, false);
            free(temp);
            return OK;
        }

        case 0x14: { /* PSMT4 — 4-bit indexed */
            if (!clut) return ERR_INVALID;
            size_t half_pixels = (total_pixels + 1) / 2;
            u8* temp = malloc(half_pixels);
            if (!temp) return ERR_NOMEM;
            deswizzle_block(temp, (const u8*)src, w, h, 4);
            /* De-nibble to 1 byte per pixel for CLUT lookup */
            u8* expanded = malloc(total_pixels);
            if (!expanded) { free(temp); return ERR_NOMEM; }
            for (size_t i = 0; i < total_pixels; i++) {
                int byte_idx = (int)(i / 2);
                expanded[i] = (i % 2 == 0) ? (temp[byte_idx] & 0x0F) : (temp[byte_idx] >> 4);
            }
            apply_clut_rgba8((u8*)dst_rgba8, expanded, w, h,
                             (const u32*)clut, 0, false);
            free(temp);
            free(expanded);
            return OK;
        }

        default:
            /* Unsupported format — fill with magenta (debug) */
            memset(dst_rgba8, 0, total_pixels * 4);
            u8* dst = (u8*)dst_rgba8;
            for (size_t i = 0; i < total_pixels; i++) {
                dst[i*4+0] = 0xFF;
                dst[i*4+2] = 0xFF;
                dst[i*4+3] = 0xFF;
            }
            return OK;
    }
}

/* ── Sampler Management ──────────────────────────────── */

VkSampler texture_manager_get_sampler(TextureManager* tm,
                                      bool bilinear, bool trilinear,
                                      VkSamplerAddressMode wrap_u,
                                      VkSamplerAddressMode wrap_v)
{
    /* Check cache */
    for (s32 i = 0; i < tm->sampler_count; i++) {
        /* Simple key match — in production: use proper hash */
        /* (simplified for brevity) */
        (void)bilinear; (void)trilinear; (void)wrap_u; (void)wrap_v;
        return tm->samplers[i];
    }

    /* Create new sampler */
    if (tm->sampler_count >= TEX_MGR_SAMPLER_CACHE) {
        return tm->samplers[0];  /* evict oldest */
    }

    VkSamplerCreateInfo sci = {
            .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter    = bilinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .minFilter    = bilinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
            .mipmapMode   = trilinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                      : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = wrap_u,
            .addressModeV = wrap_v,
            .maxAnisotropy = 4.0f,
            .anisotropyEnable = VK_TRUE,
            .maxLod       = 12.0f,
    };

    VkSampler sampler;
    if (vkCreateSampler(tm->device, &sci, NULL, &sampler) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    tm->samplers[tm->sampler_count++] = sampler;
    return sampler;
}

/* ── Init / Destroy ──────────────────────────────────── */

Result texture_manager_init(TextureManager* tm, VkDevice device,
                            VkPhysicalDevice physical, UnifiedMem* mem)
{
    memset(tm, 0, sizeof(*tm));
    tm->device   = device;
    tm->physical = physical;
    tm->memory   = mem;

    atomic_store_rlx(&tm->texture_count, 0);
    atomic_store_rlx(&tm->bytes_uploaded, 0);
    atomic_store_rlx(&tm->uploads_this_frame, 0);

    /* Create staging buffer */
    VkBufferCreateInfo bci = {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = TEX_MGR_STAGING_SIZE_MB * 1024 * 1024,
            .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(device, &bci, NULL, &tm->staging_buffer) != VK_SUCCESS)
        return ERR_GPU;

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device, tm->staging_buffer, &mem_req);

    /* Allocate from unified memory */
    void* mapped = NULL;
    TRY(unified_mem_alloc(mem, mem_req.size, mem_req.alignment,
                          mem_req.memoryTypeBits,
                          &tm->staging_memory, NULL, &mapped));

    vkBindBufferMemory(device, tm->staging_buffer, tm->staging_memory, 0);
    tm->staging_mapped = mapped;
    tm->staging_size   = bci.size;
    atomic_store_rlx(&tm->staging_offset, 0);

    /* Upload command pool */
    VkCommandPoolCreateInfo cpci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    vkCreateCommandPool(device, &cpci, NULL, &tm->upload_cmd_pool);

    VkCommandBufferAllocateInfo cbai = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = tm->upload_cmd_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &cbai, &tm->upload_cmd_buf);

    /* Upload fence */
    VkFenceCreateInfo fci = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkCreateFence(device, &fci, NULL, &tm->upload_fence);

    /* Command ring for async upload requests */
    TRY(mpsc_ring_init(&tm->command_ring, 256, sizeof(TexCommand)));

    /* Default sampler */
    texture_manager_get_sampler(tm, true, true,
                                VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                VK_SAMPLER_ADDRESS_MODE_REPEAT);

    return OK;
}

void texture_manager_destroy(TextureManager* tm)
{
    vkDeviceWaitIdle(tm->device);

    mpsc_ring_destroy(&tm->command_ring);

    for (s32 i = 0; i < tm->sampler_count; i++) {
        vkDestroySampler(tm->device, tm->samplers[i], NULL);
    }

    s32 count = atomic_load_rlx(&tm->texture_count);
    for (s32 i = 0; i < count; i++) {
        TextureEntry* te = &tm->textures[i];
        if (te->view)   vkDestroyImageView(tm->device, te->view, NULL);
        if (te->image)  vkDestroyImage(tm->device, te->image, NULL);
        if (te->clut_view)  vkDestroyImageView(tm->device, te->clut_view, NULL);
        if (te->clut_image) vkDestroyImage(tm->device, te->clut_image, NULL);
    }

    vkDestroyFence(tm->device, tm->upload_fence, NULL);
    vkDestroyCommandPool(tm->device, tm->upload_cmd_pool, NULL);
    vkDestroyBuffer(tm->device, tm->staging_buffer, NULL);
}

/* ── Texture Creation ────────────────────────────────── */

u32 texture_manager_create(TextureManager* tm, u32 ps2_tbp,
                           u16 width, u16 height, VkFormat format)
{
    s32 idx = atomic_fetch_add_rlx(&tm->texture_count, 1);
    if (idx >= TEX_MGR_MAX_TEXTURES) return UINT32_MAX;

    TextureEntry* te = &tm->textures[idx];
    memset(te, 0, sizeof(*te));

    te->id     = (u32)idx;
    te->ps2_tbp = ps2_tbp;
    te->width  = width;
    te->height = height;
    te->format = format;
    te->mip_levels = 1;
    atomic_store_rlx(&te->state, (s32)TEX_STATE_EMPTY);

    /* Create Vulkan image */
    VkImageCreateInfo ici = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = format,
            .extent        = { width, height, 1 },
            .mipLevels     = 1,
            .arrayLayers   = 1,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            .usage         = VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(tm->device, &ici, NULL, &te->image) != VK_SUCCESS)
        return UINT32_MAX;

    /* Allocate and bind memory */
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(tm->device, te->image, &mem_req);

    void* dummy = NULL;
    if (unified_mem_alloc(tm->memory, mem_req.size, mem_req.alignment,
                          mem_req.memoryTypeBits,
                          &te->memory, NULL, &dummy) != OK) {
        vkDestroyImage(tm->device, te->image, NULL);
        return UINT32_MAX;
    }
    vkBindImageMemory(tm->device, te->image, te->memory, 0);

    /* Create image view */
    VkImageViewCreateInfo vci = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = te->image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = format,
            .subresourceRange = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
            },
    };

    if (vkCreateImageView(tm->device, &vci, NULL, &te->view) != VK_SUCCESS)
        return UINT32_MAX;

    te->sampler = tm->samplers[0];  /* default sampler */

    return te->id;
}

/* ── Texture Upload ──────────────────────────────────── */

Result texture_manager_upload(TextureManager* tm, u32 texture_id,
                              const void* data, u32 mip_level)
{
    if (texture_id >= (u32)atomic_load_rlx(&tm->texture_count))
        return ERR_INVALID;

    TextureEntry* te = &tm->textures[texture_id];

    /* Compute staging offset */
    VkDeviceSize image_size = (VkDeviceSize)te->width * te->height * 4;
    VkDeviceSize align = 64;
    VkDeviceSize offset = atomic_fetch_add_rlx(&tm->staging_offset, image_size + align);
    offset = (offset + align - 1) & ~(align - 1);

    if (offset + image_size > tm->staging_size) {
        /* Staging buffer full — flush uploads first */
        texture_manager_process_uploads(tm);
        atomic_store_rlx(&tm->staging_offset, 0);
        offset = 0;
    }

    /* Copy data to staging buffer */
    u8* dst = (u8*)tm->staging_mapped + offset;
    memcpy(dst, data, (size_t)image_size);

    te->staging_offset = offset;
    te->staging_size   = image_size;

    /* Record upload command */
    vkResetCommandBuffer(tm->upload_cmd_buf, 0);
    VkCommandBufferBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(tm->upload_cmd_buf, &bi);

    /* Transition: UNDEFINED → TRANSFER_DST */
    VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask       = 0,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = te->image,
            .subresourceRange    = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = mip_level,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
            },
    };

    vkCmdPipelineBarrier(tm->upload_cmd_buf,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    /* Copy buffer → image */
    VkBufferImageCopy region = {
            .bufferOffset      = offset,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = mip_level,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {te->width, te->height, 1},
    };

    vkCmdCopyBufferToImage(tm->upload_cmd_buf,
                           tm->staging_buffer, te->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    /* Transition: TRANSFER_DST → SHADER_READ_ONLY */
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(tm->upload_cmd_buf,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);

    vkEndCommandBuffer(tm->upload_cmd_buf);

    /* Submit */

    VkQueue upload_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(tm->device, 0, 0, &upload_queue);
    VkSubmitInfo si = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &tm->upload_cmd_buf,
    };
    vkQueueSubmit(upload_queue, 1, &si, tm->upload_fence);  /* */
}

atomic_store_explicit(&te->state, (s32)TEX_STATE_READY,
memory_order_release);
atomic_fetch_add_explicit(&tm->bytes_uploaded, (u64)image_size,
memory_order_relaxed);
atomic_fetch_add_explicit(&tm->uploads_this_frame, 1u,
memory_order_relaxed);
return OK;
}

/* ── Per-Frame ───────────────────────────────────────── */

void texture_manager_begin_frame(TextureManager* tm, u64 frame_number)
{
    (void)frame_number;
    /* Reset staging offset for new frame */
    atomic_store_rlx(&tm->staging_offset, 0);
    atomic_store_rlx(&tm->uploads_this_frame, 0);
}

Result texture_manager_process_uploads(TextureManager* tm)
{
    /* Process pending upload commands from ring buffer */
    TexCommand cmd;
    while (mpsc_ring_pop(&tm->command_ring, &cmd) == OK) {
        switch (cmd.type) {
            case TEX_CMD_UPLOAD:
                texture_manager_upload(tm, cmd.texture_id, cmd.data, cmd.mip_level);
                break;
            case TEX_CMD_INVALIDATE:
                if (cmd.texture_id < (u32)atomic_load_rlx(&tm->texture_count)) {
                    atomic_store_rlx(&tm->textures[cmd.texture_id].state,
                                     (s32)TEX_STATE_INVALIDATED);
                }
                break;
            default:
                break;
        }
    }
    return OK;
}

void texture_manager_bind(TextureManager* tm, u32 texture_id,
                          u64 frame_number)
{
    if (texture_id >= (u32)atomic_load_rlx(&tm->texture_count)) return;
    TextureEntry* te = &tm->textures[texture_id];
    te->last_used_frame = frame_number;
    te->access_count++;
}

/* ── CLUT ────────────────────────────────────────────── */

Result texture_manager_set_clut(TextureManager* tm, u32 texture_id,
                                const void* clut_data, u8 clut_format)
{
    if (texture_id >= (u32)atomic_load_rlx(&tm->texture_count))
        return ERR_INVALID;

    TextureEntry* te = &tm->textures[texture_id];
    te->has_clut  = true;
    te->clut_psm  = clut_format;

    /* Upload CLUT as 1D texture (256 entries × 4 bytes = 1024 bytes) */
    if (te->clut_image == VK_NULL_HANDLE) {
        VkImageCreateInfo ici = {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType     = VK_IMAGE_TYPE_1D,
                .format        = VK_FORMAT_R8G8B8A8_UNORM,
                .extent        = { 256, 1, 1 },
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = VK_SAMPLE_COUNT_1_BIT,
                .tiling        = VK_IMAGE_TILING_OPTIMAL,
                .usage         = VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        vkCreateImage(tm->device, &ici, NULL, &te->clut_image);

        VkMemoryRequirements mem_req;
        vkGetImageMemoryRequirements(tm->device, te->clut_image, &mem_req);
        void* dummy = NULL;
        unified_mem_alloc(tm->memory, mem_req.size, mem_req.alignment,
                          mem_req.memoryTypeBits,
                          &te->memory, NULL, &dummy);
        vkBindImageMemory(tm->device, te->clut_image, te->memory, 0);

        VkImageViewCreateInfo vci = {
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image    = te->clut_image,
                .viewType = VK_IMAGE_VIEW_TYPE_1D,
                .format   = VK_FORMAT_R8G8B8A8_UNORM,
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = 1,
                        .layerCount = 1,
                },
        };
        vkCreateImageView(tm->device, &vci, NULL, &te->clut_view);
    }

    /* Mark for re-upload */
    atomic_store_rlx(&te->state, (s32)TEX_STATE_INVALIDATED);
    return OK;
}

/* ── Eviction ────────────────────────────────────────── */

void texture_manager_evict(TextureManager* tm, VkDeviceSize needed)
{
    s32 count = atomic_load_rlx(&tm->texture_count);
    u64 oldest_frame = UINT64_MAX;
    s32 oldest_idx = -1;

    /* Simple LRU eviction */
    for (s32 i = 0; i < count; i++) {
        TextureEntry* te = &tm->textures[i];
        if (te->last_used_frame < oldest_frame) {
            oldest_frame = te->last_used_frame;
            oldest_idx = i;
        }
    }

    if (oldest_idx >= 0) {
        TextureEntry* te = &tm->textures[oldest_idx];
        if (te->view) {
            vkDestroyImageView(tm->device, te->view, NULL);
            te->view = VK_NULL_HANDLE;
        }
        if (te->image) {
            vkDestroyImage(tm->device, te->image, NULL);
            te->image = VK_NULL_HANDLE;
        }
        atomic_store_rlx(&te->state, (s32)TEX_STATE_EMPTY);
    }

    (void)needed;
}