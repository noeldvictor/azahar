// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Anime4K v4.0.1 DoG algorithm:
// Copyright (c) 2019-2021 bloc97
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to including this notice in all copies or substantial portions.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Mobile screen-space port of Anime4K v4.0.1's non-CNN DoG upscaler. The original separable
// intermediate passes are fused into a 3x3 Gaussian neighborhood to avoid full-frame temporary
// images and their bandwidth cost on tile-based mobile GPUs.

//? #version 430 core

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D color_texture;

uniform vec4 i_resolution;
uniform vec4 o_resolution;
uniform int layer;

float GetLuma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    const float upscale_threshold = 1.2;
    const float strength = 0.8;
    vec4 center = texture(color_texture, frag_tex_coord);

    // Match Anime4K's upscale-only condition. Plain presentation is both cheaper and cleaner when
    // the screen is at native size or being downscaled.
    if (o_resolution.x <= i_resolution.x * upscale_threshold ||
        o_resolution.y <= i_resolution.y * upscale_threshold) {
        color = center;
        return;
    }

    vec2 pt = i_resolution.zw;
    float tl = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(-1.0, -1.0)).rgb);
    float tc = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(0.0, -1.0)).rgb);
    float tr = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(1.0, -1.0)).rgb);
    float ml = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(-1.0, 0.0)).rgb);
    float mc = GetLuma(center.rgb);
    float mr = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(1.0, 0.0)).rgb);
    float bl = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(-1.0, 1.0)).rgb);
    float bc = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(0.0, 1.0)).rgb);
    float br = GetLuma(texture(color_texture, frag_tex_coord + pt * vec2(1.0, 1.0)).rgb);

    // Normalized 3x3 Gaussian (sigma 1) plus Anime4K's local-neighborhood clamp suppresses the
    // halos that a conventional unsharp mask creates around dark anime line art.
    float gaussian = mc * 0.20417996 + (tc + ml + mr + bc) * 0.12384140 +
                     (tl + tr + bl + br) * 0.07511360;
    float neighborhood_min = min(min(min(tl, tc), min(tr, ml)),
                                 min(min(mc, mr), min(min(bl, bc), br)));
    float neighborhood_max = max(max(max(tl, tc), max(tr, ml)),
                                 max(max(mc, mr), max(max(bl, bc), br)));
    float corrected_luma = clamp(mc + (mc - gaussian) * strength, neighborhood_min,
                                 neighborhood_max);

    color = vec4(clamp(center.rgb + (corrected_luma - mc), 0.0, 1.0), center.a);
}
