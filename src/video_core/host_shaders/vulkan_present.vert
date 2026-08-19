// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec2 frag_tex_coord;

layout (push_constant, std140) uniform DrawInfo {
    vec4 screen_rect;
    vec4 texcoords;
    vec4 framebuffer_transform;
    vec4 i_resolution;
    vec4 o_resolution;
    int screen_id_l;
    int screen_id_r;
    int layer;
    int reverse_interlaced;
    int orientation;
};

void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    vec2 pixel_position = mix(screen_rect.xy, screen_rect.zw, corner);
    vec2 position = pixel_position * framebuffer_transform.xy + framebuffer_transform.zw;
    gl_Position = vec4(position, 0.0, 1.0);

    if (orientation == 0) {
        frag_tex_coord = vec2(mix(texcoords.w, texcoords.y, corner.y),
                              mix(texcoords.x, texcoords.z, corner.x));
    } else if (orientation == 1) {
        frag_tex_coord = vec2(mix(texcoords.w, texcoords.y, corner.x),
                              mix(texcoords.z, texcoords.x, corner.y));
    } else if (orientation == 2) {
        frag_tex_coord = vec2(mix(texcoords.y, texcoords.w, corner.y),
                              mix(texcoords.z, texcoords.x, corner.x));
    } else {
        frag_tex_coord = vec2(mix(texcoords.y, texcoords.w, corner.x),
                              mix(texcoords.x, texcoords.z, corner.y));
    }
}
