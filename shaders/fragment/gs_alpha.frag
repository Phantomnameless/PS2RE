#version 450
/* Alpha test emulation — PS2 GS TEST register */

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;

layout(set = 0, binding = 0) uniform AlphaTestUBO {
    uint u_atst;    /* compare function */
    uint u_aref;    /* reference value */
    uint u_afail;   /* fail action */
};

layout(set = 1, binding = 0) uniform sampler2D u_texture;

layout(location = 0) out vec4 frag_color;

void main() {
    vec4 tex = texture(u_texture, v_uv);
    float alpha = tex.a * 255.0;
    float ref = float(u_aref);

    bool pass = false;
    switch (u_atst) {
        case 0u: pass = false;              break; /* NEVER */
        case 1u: pass = true;               break; /* ALWAYS */
        case 2u: pass = (alpha < ref);      break; /* LESS */
        case 3u: pass = (alpha <= ref);     break; /* LEQUAL */
        case 4u: pass = (alpha == ref);     break; /* EQUAL */
        case 5u: pass = (alpha >= ref);     break; /* GEQUAL */
        case 6u: pass = (alpha > ref);      break; /* GREATER */
        case 7u: pass = (alpha != ref);     break; /* NOTEQUAL */
    }

    if (!pass) {
        if (u_afail == 0u) discard;         /* KEEP = discard pixel */
        /* FB_ONLY / ZB_ONLY handled via stencil */
        discard;
    }

    frag_color = tex * v_color;
}