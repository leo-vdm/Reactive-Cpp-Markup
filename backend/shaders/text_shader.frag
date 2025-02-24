#version 450

layout(set = 1, binding = 0) uniform sampler samp;
layout(set = 1, binding = 1) uniform texture3D glyph_atlas;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_texture_coord;
layout(location = 2) in flat uvec2 glyph_size;
layout(location = 3) in flat uvec3 glyph_offset;

layout(location = 0) out vec4 out_color;

void main()
{
    
    uvec2 glyph_sample_offset = uvec2(frag_texture_coord * glyph_size);
    
    uvec3 glyph_sample_position = glyph_offset + uvec3(glyph_sample_offset, 0);
    
    vec4 color = texture(sampler3D(glyph_atlas, samp), glyph_sample_position);
    uint brightness = int(color.r);
    
    out_color = brightness > 120 ? vec4(1.0f, 0.0f, 0.0f, 1.0f) : vec4(0.0f, 0.0f, 1.0f, 1.0f);
}