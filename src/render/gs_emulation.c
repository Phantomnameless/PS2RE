#include "ps2re/render/gs_emulation.h"
#include "ps2re/render/renderer.h"   /* ← Renderer completo */

VkBlendFactor gs_blend_factor_to_vk(GSBlendFactor f)
{
    switch (f) {
        case GS_BLEND_ZERO: return VK_BLEND_FACTOR_ZERO;
        case GS_BLEND_CS:   return VK_BLEND_FACTOR_SRC_COLOR;
        case GS_BLEND_CD:   return VK_BLEND_FACTOR_DST_COLOR;
        case GS_BLEND_FIX:  return VK_BLEND_FACTOR_CONSTANT_COLOR;
    }
    return VK_BLEND_FACTOR_ZERO;
}

VkCompareOp gs_compare_to_vk(u8 cmp)
{
    switch (cmp) {
        case 0: return VK_COMPARE_OP_NEVER;
        case 1: return VK_COMPARE_OP_ALWAYS;
        case 2: return VK_COMPARE_OP_LESS;
        case 3: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case 4: return VK_COMPARE_OP_EQUAL;
        case 5: return VK_COMPARE_OP_NOT_EQUAL;
        case 6: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case 7: return VK_COMPARE_OP_GREATER;
    }
    return VK_COMPARE_OP_ALWAYS;
}

Result gs_create_pipeline(struct Renderer* r, const GSState* gs,  /* ← */
                          VkRenderPass render_pass,
                          VkPipelineLayout layout,
                          VkPipeline* out_pipeline)
{
    VkPrimitiveTopology topology;
    switch (gs->prim.prim_type) {
        case GS_PRIM_POINT:     topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
        case GS_PRIM_LINE:      topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
        case GS_PRIM_TRI:       topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        case GS_PRIM_TRI_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
        case GS_PRIM_TRI_FAN:   topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; break;
        default:                topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
    }

    VkPipelineColorBlendAttachmentState blend_attach = {
            .blendEnable         = gs->prim.abe ? VK_TRUE : VK_FALSE,
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

    VkPipelineDepthStencilStateCreateInfo depth = {
            .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable  = gs->test.zte ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp   = gs_compare_to_vk(gs->test.ztst),
            .stencilTestEnable = VK_FALSE,
    };

    VkDynamicState dynamics[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates    = dynamics,
    };

    VkPipelineInputAssemblyStateCreateInfo ia = {
            .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = topology,
    };

    VkPipelineRasterizationStateCreateInfo raster = {
            .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode    = VK_CULL_MODE_NONE,
            .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth   = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo ms = {
            .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkVertexInputBindingDescription binding = {
            .binding   = 0,
            .stride    = (u32)(sizeof(f32) * 9 + sizeof(u8) * 4),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attrs[] = {
            { 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    16 },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT,        28 },
            { 3, 0, VK_FORMAT_R8G8B8A8_UNORM,       36 },
    };

    VkPipelineVertexInputStateCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount   = 1,
            .pVertexBindingDescriptions      = &binding,
            .vertexAttributeDescriptionCount = 4,
            .pVertexAttributeDescriptions    = attrs,
    };

    VkPipelineViewportStateCreateInfo vp = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount  = 1,
    };

    /* Placeholder: full pipeline creation requires shader modules */
    (void)r;
    (void)render_pass;
    (void)layout;
    (void)out_pipeline;
    (void)depth;
    (void)blend;
    (void)dyn;
    (void)ia;
    (void)raster;
    (void)ms;
    (void)vi;
    (void)vp;
    /* vkCreateGraphicsPipelines(r->device, ..., out_pipeline); */

    return OK;
}