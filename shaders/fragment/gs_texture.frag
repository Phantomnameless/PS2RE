#version 450
/* Textured GS emulation — replaces GS TEX0 + color modulation */

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in float v_fog;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_mvp;
    mat4 u_model;
    vec4 u_fog_params;
    vec4 u_fog_color;
};

layout(set = 0, binding = 1) uniform GSStateUBO {
    uint u_tfx;          /* 0=MODULATE, 1=DECAL, 2=HIGHLIGHT */
    uint u_alpha_mode;
    float u_fba;         /* frame buffer alpha */
};

layout(set = 1, binding = 0) uniform sampler2D u_texture;

layout(location = 0) out vec4 frag_color;

void main() {
    vec4 tex = texture(u_texture, v_uv);

    /* GS texture function (TEX0.tfx) */
    switch (u_tfx) {
        case 0u: /* MODULATE: Cs * Cd */
            frag_color = tex * v_color;
            break;
        case 1u: /* DECAL: Cs */
            frag_color = tex;
            break;
        case 2u: /* HIGHLIGHT: Cs*Cd + Cs.a */
            frag_color = tex * v_color;
            frag_color.rgb += tex.aaa;
            break;
        default:
            frag_color = tex * v_color;
            break;
    }

    /* FBA (PS2 frame buffer alpha) */
    frag_color.a = max(frag_color.a, u_fba);

    /* Fog */
    frag_color.rgb = mix(u_fog_color.rgb, frag_color.rgb, v_fog);
}