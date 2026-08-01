#version 450
/* Particle billboarding — replaces VU1 sprite generation */

layout(location = 0) in vec4 a_position;   /* center + size */
layout(location = 1) in vec4 a_color;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_vp;
    vec3 u_camera_right;
    vec3 u_camera_up;
};

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

/* Vertex index: 0,1,2,3 → quad corners */
void main() {
    vec3 center = a_position.xyz;
    float size = a_position.w;

    /* Billboard: expand quad in camera-facing plane */
    int idx = gl_VertexIndex % 4;
    vec2 offset = vec2(
        float((idx == 0 || idx == 3) ? -1 : 1),
        float((idx == 0 || idx == 1) ? -1 : 1)
    );

    vec3 world_pos = center
        + u_camera_right * offset.x * size
        + u_camera_up    * offset.y * size;

    gl_Position = u_vp * vec4(world_pos, 1.0);
    v_color = a_color;
    v_uv = offset * 0.5 + 0.5;
}