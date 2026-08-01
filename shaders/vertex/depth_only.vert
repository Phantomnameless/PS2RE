#version 450
/* Depth pre-pass — writes depth only, no fragment shader work.
 * TBDR GPUs: this fills the HiZ buffer, enabling early-Z for main pass.
 * Equivalent to PS2 GS Z-buffer pre-fill. */

layout(location = 0) in vec4 a_position;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 u_mvp;
};

void main() {
    gl_Position = u_mvp * a_position;
}