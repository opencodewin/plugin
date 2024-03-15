#pragma once
#include <imvk_mat_shader.h>

#define SHADER_PARAM \
" \n\
layout (push_constant) uniform parameter \n\
{ \n\
    int w; \n\
    int h; \n\
    int cstep; \n\
    int in_format; \n\
    int in_type; \n\
\n\
    int w2; \n\
    int h2; \n\
    int cstep2; \n\
    int in_format2; \n\
    int in_type2; \n\
\n\
    int out_w; \n\
    int out_h; \n\
    int out_cstep; \n\
    int out_format; \n\
    int out_type; \n\
\n\
    float progress; \n\
    float intensity; \n\
    int passes; \n\
} p; \
"

#define SHADER_MAIN \
" \n\
sfpvec4 transition(vec2 uv) \n\
{ \n\
    sfpvec4 c1 = sfpvec4(0.0); \n\
    sfpvec4 c2 = sfpvec4(0.0); \n\
\n\
    float disp = p.intensity * (0.5 - distance(0.5, p.progress)); \n\
    for (int xi = 0; xi < p.passes; xi++) \n\
    { \n\
        float x = float(xi) / float(p.passes) - 0.5; \n\
        for (int yi = 0; yi < p.passes; yi++) \n\
        { \n\
            float y = float(yi) / float(p.passes) - 0.5; \n\
            vec2 v = vec2(x,y); \n\
            float d = disp; \n\
            vec2 point = uv + d * v; \n\
            c1 += load_rgba(int(point.x * (p.w - 1)), int((1.f - point.y) * (p.h - 1)), p.w, p.h, p.cstep, p.in_format, p.in_type); \n\
            c2 += load_rgba_src2(int(point.x * (p.w2 - 1)), int((1.f - point.y) * (p.h2 - 1)), p.w2, p.h2, p.cstep2, p.in_format2, p.in_type2); \n\
        } \n\
    } \n\
    c1 /= sfp(p.passes * p.passes); \n\
    c2 /= sfp(p.passes * p.passes); \n\
    return mix(c1, c2, sfp(p.progress)); \n\
} \n\
\n\
void main() \n\
{ \n\
    ivec2 uv = ivec2(gl_GlobalInvocationID.xy); \n\
    if (uv.x >= p.out_w || uv.y >= p.out_h) \n\
        return; \n\
    vec2 point = vec2(float(uv.x) / float(p.out_w - 1), 1.f - float(uv.y) / float(p.out_h - 1)); \n\
    sfpvec4 result = transition(point); \n\
    store_rgba(result, uv.x, uv.y, p.out_w, p.out_h, p.out_cstep, p.out_format, p.out_type); \n\
} \
"

static const char LinearBlur_data[] = 
SHADER_HEADER
SHADER_PARAM
SHADER_INPUT_OUTPUT_DATA
R"(
layout (binding =  8) readonly buffer src2_int8       { uint8_t   src2_data_int8[]; };
layout (binding =  9) readonly buffer src2_int16      { uint16_t  src2_data_int16[]; };
layout (binding = 10) readonly buffer src2_float16    { float16_t src2_data_float16[]; };
layout (binding = 11) readonly buffer src2_float32    { float     src2_data_float32[]; };
)"
SHADER_LOAD_RGBA
SHADER_LOAD_RGBA_NAME(src2)
SHADER_STORE_RGBA
SHADER_MAIN
;
