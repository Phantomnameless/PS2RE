#include "ps2re/memory/unified_mem.h"
#include <string.h>

static u32 find_memory_type(VkPhysicalDevice physical,
                            u32 type_bits,
                            VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);

    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if (type_bits & (1u << i)) return i;
    }
    return UINT32_MAX;
}

Result unified_mem_init(UnifiedMem* mem, VkDevice device,
                        VkPhysicalDevice physical,
                        VkDeviceSize default_block_size)
{
    mem->device      = device;
    mem->physical    = physical;
    mem->block_size  = default_block_size;
    mem->max_blocks  = 32;
    mem->num_blocks  = 0;
    mem->blocks = (MemoryBlock*)calloc(     /* ← cast explícito */
            (size_t)mem->max_blocks, sizeof(MemoryBlock));
    return mem->blocks ? OK : ERR_NOMEM;
}

void unified_mem_destroy(UnifiedMem* mem)
{
    for (s32 i = 0; i < mem->num_blocks; i++) {
        if (mem->blocks[i].mapped) {
            vkUnmapMemory(mem->device, mem->blocks[i].memory);
        }
        vkFreeMemory(mem->device, mem->blocks[i].memory, NULL);
    }
    free(mem->blocks);
    mem->blocks = NULL;
}

static Result alloc_new_block(UnifiedMem* mem, u32 type_bits,
                              VkDeviceSize min_size)
{
    if (mem->num_blocks >= mem->max_blocks) return ERR_NOMEM;

    VkDeviceSize sz = mem->block_size;
    if (sz < min_size) sz = min_size;

    u32 mem_type_idx = find_memory_type(
            mem->physical, type_bits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type_idx == UINT32_MAX) return ERR_GPU;

    VkMemoryAllocateInfo ai = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = sz,
            .memoryTypeIndex = mem_type_idx,
    };

    MemoryBlock* blk = &mem->blocks[mem->num_blocks];
    VkResult vr = vkAllocateMemory(mem->device, &ai, NULL, &blk->memory);
    if (vr != VK_SUCCESS) return ERR_GPU;

    blk->size      = sz;
    blk->used      = 0;
    blk->type_bits = type_bits;
    blk->mapped    = NULL;

    void* mapped = NULL;
    vr = vkMapMemory(mem->device, blk->memory, 0, sz, 0, &mapped);
    blk->mapped = (u8*)mapped;  /* ← cast para u8* */
    if (vr != VK_SUCCESS) blk->mapped = NULL;

    mem->num_blocks++;
    return OK;
}

Result unified_mem_alloc(UnifiedMem* mem, VkDeviceSize size,
                         VkDeviceSize align, u32 memory_type_bits,
                         VkDeviceMemory* out_memory,
                         VkDeviceSize* out_offset,
                         void** out_mapped)
{
    for (s32 i = 0; i < mem->num_blocks; i++) {
        MemoryBlock* blk = &mem->blocks[i];
        if (blk->type_bits != memory_type_bits) continue;

        VkDeviceSize aligned = (blk->used + align - 1) & ~(align - 1);
        if (aligned + size <= blk->size) {
            *out_memory = blk->memory;
            *out_offset = aligned;
            if (out_mapped && blk->mapped) {
                *out_mapped = (void*)(blk->mapped + aligned);  /* ← */
            }
            blk->used = aligned + size;
            return OK;
        }
    }

    TRY(alloc_new_block(mem, memory_type_bits, size + align));
    MemoryBlock* blk = &mem->blocks[mem->num_blocks - 1];
    *out_memory = blk->memory;
    *out_offset = 0;
    if (out_mapped && blk->mapped) {
        *out_mapped = (void*)blk->mapped;  /* ← */
    }
    blk->used = size;
    return OK;
}