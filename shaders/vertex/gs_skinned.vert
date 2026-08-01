#version 450
/* Skinned mesh — replaces VU0 bone matrix blending + VU1 transform */

layout(location = 0) in vec4  a_position;
layout(location = 1) in vec3  a_normal;
layout(location = 2) in vec2  a_texcoord;
layout(location = 3) in vec4  a_color;
layout(location = 4) in uvec4 a_bone_indices;
layout(location = 5) in vec4  a_bone_weights;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_mvp;
    mat4 u_model;
    vec4 u_fog_params;
    vec4 u_fog_color;
};

layout(set = 0, binding = 1) readonly buffer BoneBuffer {
    mat4 u_bones[];  /* MAX_BONES matrices */
};

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out float v_fog;

void main() {
    /* Blend bone matrices (PS2 VU0 did this per-vertex on COP2) */
    mat4 skin =
        a_bone_weights.x * u_bones[a_bone_indices.x] +
        a_bone_weights.y * u_bones[a_bone_indices.y] +
        a_bone_weights.z * u_bones[a_bone_indices.z] +
        a_bone_weights.w * u_bones[a_bone_indices.w];

    vec4 skinned_pos = skin * a_position;
    vec3 skinned_norm = mat3(skin) * a_normal;

    gl_Position = u_mvp * skinned_pos;
    v_color = a_color;
    v_uv = a_texcoord;
    v_normal = normalize(skinned_norm);

    float dist = length((u_model * skinned_pos).xyz);
    v_fog = clamp((u_fog_params.y - dist) /
                  (u_fog_params.y - u_fog_params.x), 0.0, 1.0);
}