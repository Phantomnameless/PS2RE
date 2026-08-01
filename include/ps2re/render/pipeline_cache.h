#ifndef PS2RE_PIPELINE_CACHE_H
#define PS2RE_PIPELINE_CACHE_H

#include "ps2re/types.h"
#include "ps2re/config.h"
#include "ps2re/ps2/gs_state.h"
#include <vulkan/vulkan.h>

#define PIPELINE_CACHE_BUCKETS  256
#define PIPELINE_CACHE_MAX      512

typedef struct PipelineEntry {
    u64                      key;
    VkPipeline               pipeline;
    VkPipelineLayout         layout;
    u64                      last_used_frame;
    struct PipelineEntry*    next;
} PipelineEntry;

typedef struct PipelineCache {
    VkDevice             device;
    VkPipelineCache      vulkan_cache;
    VkRenderPass         render_pass;
    VkDescriptorSetLayout descriptor_layouts[4];
    s32                  num_descriptor_layouts;
    VkPipelineLayout     shared_layout;

    VkShaderModule       vert_basic;
    VkShaderModule       vert_skinned;
    VkShaderModule       vert_particle;
    VkShaderModule       vert_depth;
    VkShaderModule       frag_basic;
    VkShaderModule       frag_texture;
    VkShaderModule       frag_alpha;
    VkShaderModule       frag_depth;

    PipelineEntry*       buckets[PIPELINE_CACHE_BUCKETS];
    PipelineEntry        entries[PIPELINE_CACHE_MAX];
    s32                  entry_count;

    void*                cache_data;
    size_t               cache_data_size;
    s32                  dirty;        /* ← bool→s32 */
} PipelineCache;

Result      pipeline_cache_init(PipelineCache* pc, VkDevice device,
                                VkRenderPass render_pass);
void        pipeline_cache_destroy(PipelineCache* pc);
VkPipeline  pipeline_cache_get(PipelineCache* pc, const GSState* gs,
                               u64 frame_number);
void        pipeline_cache_prewarm(PipelineCache* pc,
                                   const GSState* common_states, s32 count);
Result      pipeline_cache_save(const PipelineCache* pc, const char* path);
Result      pipeline_cache_load(PipelineCache* pc, const char* path);
s32         pipeline_cache_count(const PipelineCache* pc);
f64         pipeline_cache_hit_rate(const PipelineCache* pc);

#endif /* PS2RE_PIPELINE_CACHE_H */