#ifndef PS2RE_TEXTURE_MANAGER_H
#define PS2RE_TEXTURE_MANAGER_H

#include "ps2re/types.h"
#include "ps2re/memory/unified_mem.h"
#include "ps2re/ps2/gs_state.h"
#include "ps2re/async/ring_buffer.h"
#include <stdatomic.h>              /* ← */
#include <vulkan/vulkan.h>

#define TEX_MGR_MAX_TEXTURES      2048
#define TEX_MGR_MAX_MIP_LEVELS    12
#define TEX_MGR_STAGING_SIZE_MB   32
#define TEX_MGR_SAMPLER_CACHE     16

typedef enum {
    TEX_STATE_EMPTY = 0,
    TEX_STATE_LOADING,
    TEX_STATE_READY,
    TEX_STATE_INVALIDATED,
} TexState;

typedef struct TextureEntry {
    u32             id;
    u32             ps2_tbp;
    u16             width;
    u16             height;
    u8              mip_levels;
    VkFormat        format;
    _Atomic(s32)    state;

    VkImage         image;
    VkImageView     view;
    VkDeviceMemory  memory;
    VkSampler       sampler;

    void*           staging_ptr;
    VkDeviceSize    staging_offset;
    VkDeviceSize    staging_size;

    u64             last_used_frame;
    u32             access_count;

    bool            has_clut;
    u32             clut_cbp;
    u8              clut_psm;
    VkImage         clut_image;
    VkImageView     clut_view;
} TextureEntry;

typedef enum {
    TEX_CMD_UPLOAD,
    TEX_CMD_DOWNLOAD,
    TEX_CMD_INVALIDATE,
    TEX_CMD_GENERATE_MIPMAPS,
} TexCommandType;

typedef struct TexCommand {
    TexCommandType type;
    u32            texture_id;
    const void*    data;
    u32            width;
    u32            height;
    VkFormat       format;
    u32            mip_level;
} TexCommand;

typedef struct TextureManager {
    VkDevice            device;
    VkPhysicalDevice    physical;
    UnifiedMem*         memory;

    TextureEntry        textures[TEX_MGR_MAX_TEXTURES];
    _Atomic(s32)        texture_count;

    VkBuffer            staging_buffer;
    VkDeviceMemory      staging_memory;
    void*               staging_mapped;
    VkDeviceSize        staging_size;
    _Atomic(u64)        staging_offset;   /* ← VkDeviceSize→u64 */

    VkSampler           samplers[TEX_MGR_SAMPLER_CACHE];
    s32                 sampler_count;

    MPSCRing            command_ring;

    VkCommandPool       upload_cmd_pool;
    VkCommandBuffer     upload_cmd_buf;
    VkFence             upload_fence;

    _Atomic(u64)        bytes_uploaded;
    _Atomic(u32)        uploads_this_frame;
} TextureManager;

Result texture_manager_init(TextureManager* tm, VkDevice device,
                            VkPhysicalDevice physical, UnifiedMem* mem);
void   texture_manager_destroy(TextureManager* tm);
void   texture_manager_begin_frame(TextureManager* tm, u64 frame_number);
Result texture_manager_process_uploads(TextureManager* tm);
u32    texture_manager_create(TextureManager* tm, u32 ps2_tbp,
                              u16 width, u16 height, VkFormat format);
Result texture_manager_upload(TextureManager* tm, u32 texture_id,
                              const void* data, u32 mip_level);
void   texture_manager_bind(TextureManager* tm, u32 texture_id,
                            u64 frame_number);
Result texture_manager_set_clut(TextureManager* tm, u32 texture_id,
                                const void* clut_data, u8 clut_format);
Result texture_decode_ps2(void* dst_rgba8, const void* src,
                          u16 width, u16 height, u8 ps2_format,
                          const void* clut, u8 clut_format);
VkSampler texture_manager_get_sampler(TextureManager* tm,
                                      bool bilinear, bool trilinear,
                                      VkSamplerAddressMode wrap_u,
                                      VkSamplerAddressMode wrap_v);
void   texture_manager_evict(TextureManager* tm, VkDeviceSize needed);

#endif /* PS2RE_TEXTURE_MANAGER_H */