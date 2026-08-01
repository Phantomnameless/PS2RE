#version 450
/* Basic GS emulation — vertex color + fog */

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in float v_fog;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_mvp;
    mat4 u_model;
    vec4 u_fog_params;
    vec4 u_fog_color;
};

layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = v_color;
    frag_color.rgb = mix(u_fog_color.rgb, frag_color.rgb, v_fog);
}