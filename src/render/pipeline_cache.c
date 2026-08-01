#include "ps2re/render/pipeline_cache.h"
#include "ps2re/render/gs_emulation.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Shader Loading ──────────────────────────────────── */

static VkShaderModule load_shader(VkDevice device, const u8* code, size_t size)
{
    VkShaderModuleCreateInfo ci = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = size,
            .pCode    = (const u32*)code,
    };
    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, NULL, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

static VkShaderModule load_spirv_file(VkDevice device, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return VK_NULL_HANDLE;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return VK_NULL_HANDLE; }

    u8* buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return VK_NULL_HANDLE; }

    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    VkShaderModule module = load_shader(device, buf, (size_t)sz);
    free(buf);
    return module;
}

/* ── Descriptor Layouts ──────────────────────────────── */

static Result create_descriptor_layouts(PipelineCache* pc)
{
    /* Layout 0: Scene UBO + optional bone SSBO */
    VkDescriptorSetLayoutBinding bindings_0[] = {
            /* binding 0: SceneUBO */
            [0] = {
                    .binding         = 0,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT |
                                       VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            /* binding 1: BoneBuffer (SSBO) or GSStateUBO */
            [1] = {
                    .binding         = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT |
                                       VK_SHADER_STAGE_FRAGMENT_BIT,
            },
    };

    VkDescriptorSetLayoutCreateInfo lci = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings    = bindings_0,
    };
    if (vkCreateDescriptorSetLayout(pc->device, &lci, NULL,
                                    &pc->descriptor_layouts[0]) != VK_SUCCESS)
        return ERR_GPU;

    /* Layout 1: Texture sampler */
    VkDescriptorSetLayoutBinding bindings_1[] = {
            [0] = {
                    .binding         = 0,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
    };

    lci.bindingCount = 1;
    lci.pBindings    = bindings_1;
    if (vkCreateDescriptorSetLayout(pc->device, &lci, NULL,
                                    &pc->descriptor_layouts[1]) != VK_SUCCESS)
        return ERR_GPU;

    /* Layout 2: Compute storage buffers (physics, skinning, particles) */
    VkDescriptorSetLayoutBinding bindings_2[] = {
            [0] = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            [1] = { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            [2] = { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            [3] = { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            [4] = { .binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
    };

    lci.bindingCount = 5;
    lci.pBindings    = bindings_2;
    if (vkCreateDescriptorSetLayout(pc->device, &lci, NULL,
                                    &pc->descriptor_layouts[2]) != VK_SUCCESS)
        return ERR_GPU;

    pc->num_descriptor_layouts = 3;

    /* Shared pipeline layout with push constants */
    VkPushConstantRange push_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                          VK_SHADER_STAGE_FRAGMENT_BIT |
                          VK_SHADER_STAGE_COMPUTE_BIT,
            .offset     = 0,
            .size       = 128,  /* enough for all push constants */
    };

    VkPipelineLayoutCreateInfo plci = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = (u32)pc->num_descriptor_layouts,
            .pSetLayouts            = pc->descriptor_layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push_range,
    };

    return vkCreatePipelineLayout(pc->device, &plci, NULL,
                                  &pc->shared_layout) == VK_SUCCESS
           ? OK : ERR_GPU;
}

/* ── Pipeline Creation ───────────────────────────────── */

static VkPipeline create_graphics_pipeline(
        PipelineCache* pc,
        const GSState* gs,
        VkShaderModule vert,
        VkShaderModule frag)
{
    /* Stages */
    VkPipelineShaderStageCreateInfo stages[2] = {
            [0] = {
                    .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vert,
                    .pName  = "main",
            },
            [1] = {
                    .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = frag,
                    .pName  = "main",
            },
    };

    /* Topology from GS prim type */
    VkPrimitiveTopology topology;
    switch (gs->prim.prim_type) {
        case GS_PRIM_POINT:     topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
        case GS_PRIM_LINE:      topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
        case GS_PRIM_TRI:       topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case GS_PRIM_TRI_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
        case GS_PRIM_TRI_FAN:   topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; break;
        case GS_PRIM_SPRITE:    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        default:                topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
    }

    VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = topology,
    };

    /* Vertex input — VU1Vertex layout */
    VkVertexInputBindingDescription binding = {
            .binding   = 0,
            .stride    = sizeof(f32) * 9 + sizeof(u8) * 4, /* pos4+n3+uv2+col4 */
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attrs[] = {
            { .location = 0, .binding = 0,
                    .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 0 },
            { .location = 1, .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 16 },
            { .location = 2, .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT, .offset = 28 },
            { .location = 3, .binding = 0,
                    .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 36 },
    };

    VkPipelineVertexInputStateCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = 4,
            .pVertexAttributeDescriptions    = attrs,
    };

    /* Viewport/scissor — dynamic */
    VkPipelineViewportStateCreateInfo vp = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
    };

    /* Rasterizer */
    VkPipelineRasterizationStateCreateInfo raster = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode    = VK_CULL_MODE_NONE,  /* PS2 didn't cull by default */
            .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth   = 1.0f,
    };

    /* Multisampling */
    VkPipelineMultisampleStateCreateInfo ms = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    /* Depth stencil — from GS TEST register */
    VkPipelineDepthStencilStateCreateInfo ds = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = gs->test.zte ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp   = gs->test.ztst == 0 ? VK_COMPARE_OP_NEVER :
                                gs->test.ztst == 1 ? VK_COMPARE_OP_ALWAYS :
                                gs->test.ztst == 2 ? VK_COMPARE_OP_GREATER_OR_EQUAL :
                                VK_COMPARE_OP_GREATER,
    };

    /* Blend — from GS ALPHA register + PRIM.abe */
    VkPipelineColorBlendAttachmentState blend_attach = {
            .blendEnable = gs->prim.abe ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = gs_blend_factor_to_vk(gs->alpha.A),
            .dstColorBlendFactor = gs_blend_factor_to_vk(gs->alpha.B),
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = gs_blend_factor_to_vk(gs->alpha.C),
            .dstAlphaBlendFactor = gs_blend_factor_to_vk(gs->alpha.D),
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = 0xF,
    };

    VkPipelineColorBlendStateCreateInfo blend = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable   = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments    = &blend_attach,
    };

    /* Dynamic state */
    VkDynamicState dyn_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates    = dyn_states,
    };

    /* Create pipeline */
    VkGraphicsPipelineCreateInfo pci = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &vi,
            .pInputAssemblyState = &ia,
            .pViewportState      = &vp,
            .pRasterizationState = &raster,
            .pMultisampleState   = &ms,
            .pDepthStencilState  = &ds,
            .pColorBlendState    = &blend,
            .pDynamicState       = &dyn,
            .layout              = pc->shared_layout,
            .renderPass          = pc->render_pass,
            .subpass             = 0,
    };

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(pc->device, pc->vulkan_cache,
                                  1, &pci, NULL, &pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return pipeline;
}

/* ── Select shader variant from GS state ─────────────── */

static void select_shaders(const PipelineCache* pc, const GSState* gs,
                           VkShaderModule* out_vert,
                           VkShaderModule* out_frag)
{
    /* Vertex shader selection:
     *   - Skinned mesh (if bone weights present) → vert_skinned
     *   - Sprite/point with billboard → vert_particle
     *   - Depth-only (fast path) → vert_depth
     *   - Default → vert_basic
     */
    *out_vert = pc->vert_basic;
    *out_frag = pc->frag_basic;

    /* Fragment shader selection based on GS state */
    if (!gs->prim.tme) {
        /* No texturing → basic color + fog */
        *out_frag = pc->frag_basic;
    } else if (gs->test.ate) {
        /* Alpha test enabled → alpha test shader */
        *out_frag = pc->frag_alpha;
    } else {
        /* Textured, no alpha test */
        *out_frag = pc->frag_texture;
    }
}

/* ── Public API ──────────────────────────────────────── */

Result pipeline_cache_init(PipelineCache* pc, VkDevice device,
                           VkRenderPass render_pass)
{
    memset(pc, 0, sizeof(*pc));
    pc->device      = device;
    pc->render_pass = render_pass;
    pc->entry_count = 0;

    /* Vulkan pipeline cache */
    VkPipelineCacheCreateInfo cci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    if (vkCreatePipelineCache(device, &cci, NULL,
                              &pc->vulkan_cache) != VK_SUCCESS)
        return ERR_GPU;

        /* Load shaders from SPIR-V files */
#define LOAD_MOD(dst, name) do { \
        pc->dst = load_spirv_file(device, "spirv/" name); \
        if (!pc->dst) { \
            fprintf(stderr, "WARN: shader not found: spirv/" name "\n"); \
        } \
    } while (0)

    LOAD_MOD(vert_basic,    "gs_passthrough.vert.spv");
    LOAD_MOD(vert_skinned,  "gs_skinned.vert.spv");
    LOAD_MOD(vert_particle, "gs_particle.vert.spv");
    LOAD_MOD(vert_depth,    "depth_only.vert.spv");
    LOAD_MOD(frag_basic,    "gs_basic.frag.spv");
    LOAD_MOD(frag_texture,  "gs_texture.frag.spv");
    LOAD_MOD(frag_alpha,    "gs_alpha.frag.spv");
    LOAD_MOD(frag_depth,    "depth_only.frag.spv");

#undef LOAD_MOD

    TRY(create_descriptor_layouts(pc));

    pc->dirty = false;
    pc->cache_data = NULL;
    pc->cache_data_size = 0;

    return OK;
}

void pipeline_cache_destroy(PipelineCache* pc)
{
    vkDeviceWaitIdle(pc->device);

    for (s32 i = 0; i < pc->entry_count; i++) {
        if (pc->entries[i].pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(pc->device, pc->entries[i].pipeline, NULL);
        }
    }

#define DESTROY_MOD(name) \
        if (pc->name) vkDestroyShaderModule(pc->device, pc->name, NULL)

    DESTROY_MOD(vert_basic);
    DESTROY_MOD(vert_skinned);
    DESTROY_MOD(vert_particle);
    DESTROY_MOD(vert_depth);
    DESTROY_MOD(frag_basic);
    DESTROY_MOD(frag_texture);
    DESTROY_MOD(frag_alpha);
    DESTROY_MOD(frag_depth);

#undef DESTROY_MOD

    vkDestroyPipelineLayout(pc->device, pc->shared_layout, NULL);

    for (s32 i = 0; i < pc->num_descriptor_layouts; i++) {
        vkDestroyDescriptorSetLayout(pc->device, pc->descriptor_layouts[i], NULL);
    }

    /* Save cache data before destroying */
    if (pc->vulkan_cache) {
        vkGetPipelineCacheData(pc->device, pc->vulkan_cache,
                               &pc->cache_data_size, NULL);
        if (pc->cache_data_size > 0) {
            pc->cache_data = realloc(pc->cache_data, pc->cache_data_size);
            vkGetPipelineCacheData(pc->device, pc->vulkan_cache,
                                   &pc->cache_data_size, pc->cache_data);
        }
        vkDestroyPipelineCache(pc->device, pc->vulkan_cache, NULL);
    }

    free(pc->cache_data);
}

VkPipeline pipeline_cache_get(PipelineCache* pc, const GSState* gs,
                              u64 frame_number)
{
    u64 key = gs->pipeline_hash;

    /* Hash lookup */
    u32 bucket = (u32)(key & (PIPELINE_CACHE_BUCKETS - 1));
    PipelineEntry* entry = pc->buckets[bucket];

    while (entry) {
        if (entry->key == key) {
            entry->last_used_frame = frame_number;
            return entry->pipeline;  /* CACHE HIT */
        }
        entry = entry->next;
    }

    /* CACHE MISS — create new pipeline */
    if (pc->entry_count >= PIPELINE_CACHE_MAX) {
        /* Evict LRU */
        u64 oldest_frame = UINT64_MAX;
        s32 oldest_idx = 0;
        for (s32 i = 0; i < pc->entry_count; i++) {
            if (pc->entries[i].last_used_frame < oldest_frame) {
                oldest_frame = pc->entries[i].last_used_frame;
                oldest_idx = i;
            }
        }
        vkDestroyPipeline(pc->device, pc->entries[oldest_idx].pipeline, NULL);
        /* Remove from chain */
        /* (simplified: just reuse slot) */
        entry = &pc->entries[oldest_idx];
        /* Remove old chain link */
        u32 old_bucket = (u32)(entry->key & (PIPELINE_CACHE_BUCKETS - 1));
        PipelineEntry** pp = &pc->buckets[old_bucket];
        while (*pp && *pp != entry) pp = &(*pp)->next;
        if (*pp == entry) *pp = entry->next;
    } else {
        entry = &pc->entries[pc->entry_count++];
    }

    /* Select shaders */
    VkShaderModule vert, frag;
    select_shaders(pc, gs, &vert, &frag);

    if (!vert || !frag) return VK_NULL_HANDLE;

    /* Create pipeline */
    VkPipeline pipeline = create_graphics_pipeline(pc, gs, vert, frag);
    if (pipeline == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    /* Store in cache */
    entry->key             = key;
    entry->pipeline        = pipeline;
    entry->layout          = pc->shared_layout;
    entry->last_used_frame = frame_number;

    /* Insert into hash chain */
    entry->next       = pc->buckets[bucket];
    pc->buckets[bucket] = entry;
    pc->dirty = true;

    return pipeline;
}

void pipeline_cache_prewarm(PipelineCache* pc, const GSState* common_states,
                            s32 count)
{
    for (s32 i = 0; i < count; i++) {
        pipeline_cache_get(pc, &common_states[i], 0);
    }
}

Result pipeline_cache_save(const PipelineCache* pc, const char* path)
{
    size_t data_size = 0;
    vkGetPipelineCacheData(pc->device, pc->vulkan_cache, &data_size, NULL);
    if (data_size == 0) return OK;

    void* data = malloc(data_size);
    if (!data) return ERR_NOMEM;

    vkGetPipelineCacheData(pc->device, pc->vulkan_cache, &data_size, data);

    FILE* f = fopen(path, "wb");
    if (!f) { free(data); return ERR_IO; }

    /* Write header: magic + version + size */
    u32 magic = 0x50533252;  /* "PS2R" */
    u32 version = 1;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&data_size, sizeof(size_t), 1, f);
    fwrite(data, 1, data_size, f);
    fclose(f);
    free(data);
    return OK;
}

Result pipeline_cache_load(PipelineCache* pc, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return ERR_IO;

    u32 magic, version;
    size_t data_size;
    fread(&magic, 4, 1, f);
    fread(&version, 4, 1, f);
    fread(&data_size, sizeof(size_t), 1, f);

    if (magic != 0x50533252 || version != 1) {
        fclose(f);
        return ERR_INVALID;
    }

    void* data = malloc(data_size);
    if (!data) { fclose(f); return ERR_NOMEM; }

    fread(data, 1, data_size, f);
    fclose(f);

    /* Destroy old cache, merge loaded data */
    vkDestroyPipelineCache(pc->device, pc->vulkan_cache, NULL);

    VkPipelineCacheCreateInfo cci = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .initialDataSize = data_size,
            .pInitialData    = data,
    };

    VkResult vr = vkCreatePipelineCache(pc->device, &cci, NULL,
                                        &pc->vulkan_cache);
    free(data);
    return vr == VK_SUCCESS ? OK : ERR_GPU;
}

s32 pipeline_cache_count(const PipelineCache* pc)
{
    return pc->entry_count;
}

f64 pipeline_cache_hit_rate(const PipelineCache* pc)
{
    /* In production: track hits/misses with atomic counters */
    (void)pc;
    return 0.0;  /* placeholder */
}