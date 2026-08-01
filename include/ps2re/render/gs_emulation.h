#ifndef PS2RE_GS_EMULATION_H
#define PS2RE_GS_EMULATION_H

#include "ps2re/types.h"
#include "ps2re/ps2/gs_state.h"
#include <vulkan/vulkan.h>

/* ← forward declaration em vez de incluir renderer.h (evita circular) */
struct Renderer;

VkBlendFactor gs_blend_factor_to_vk(GSBlendFactor f);
VkCompareOp   gs_compare_to_vk(u8 atst);

Result gs_create_pipeline(struct Renderer* r, const GSState* gs,  /* ← */
                          VkRenderPass render_pass,
                          VkPipelineLayout layout,
                          VkPipeline* out_pipeline);

#endif /* PS2RE_GS_EMULATION_H */