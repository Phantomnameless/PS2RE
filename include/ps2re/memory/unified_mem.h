#ifndef PS2RE_UNIFIED_MEM_H
#define PS2RE_UNIFIED_MEM_H

#include "ps2re/types.h"
#include <vulkan/vulkan.h>

typedef struct MemoryBlock {
    VkDeviceMemory  memory;
    VkDeviceSize    size;
    VkDeviceSize    used;
    u8*             mapped;    /* ← u8* em vez de void* p/ aritmética */
    u32             type_bits;
} MemoryBlock;

typedef struct UnifiedMem {
    VkDevice         device;
    VkPhysicalDevice physical;
    MemoryBlock*     blocks;
    s32              num_blocks;
    s32              max_blocks;
    VkDeviceSize     block_size;
} UnifiedMem;

Result unified_mem_init(UnifiedMem* mem, VkDevice device,
                        VkPhysicalDevice physical,
                        VkDeviceSize default_block_size);
void   unified_mem_destroy(UnifiedMem* mem);

Result unified_mem_alloc(UnifiedMem* mem, VkDeviceSize size,
                         VkDeviceSize align, u32 memory_type_bits,
                         VkDeviceMemory* out_memory,
                         VkDeviceSize* out_offset,
                         void** out_mapped);

#endif /* PS2RE_UNIFIED_MEM_H */