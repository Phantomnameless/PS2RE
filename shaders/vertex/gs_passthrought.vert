#version 450
/* Basic passthrough — replaces VU1 microcode for unlit geometry */

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
layout(location = 3) in vec4 a_color;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_mvp;
    mat4 u_model;
    vec4 u_fog_params;     /* x=start, y=end, z=enable, w=unused */
    vec4 u_fog_color;
};

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out float v_fog;

void main() {
    gl_Position = u_mvp * a_position;

    v_color = a_color;
    v_uv = a_texcoord;

    /* Fog (PS2 GS fog — linear) */
    if (u_fog_params.z > 0.0) {
        float dist = length((u_model * a_position).xyz);
        v_fog = clamp((u_fog_params.y - dist) /
                      (u_fog_params.y - u_fog_params.x), 0.0, 1.0);
    } else {
        v_fog = 1.0;
    }
}