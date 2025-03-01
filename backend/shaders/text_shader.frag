#version 450

layout(set = 1, binding = 0) uniform sampler samp;
layout(set = 1, binding = 1) uniform utexture3D glyph_atlas;

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_texture_coord;
layout(location = 2) in flat vec2 glyph_size;
layout(location = 3) in flat vec3 glyph_offset;
layout(location = 4) in flat vec2 atlas_size;

layout(location = 0) out vec4 out_color;

void main()
{
    
    vec2 glyph_sample_offset = vec2(frag_texture_coord * glyph_size);
    
    vec3 glyph_sample_position = glyph_offset + vec3(glyph_sample_offset, 0);
    vec3 sample_position_normalized = glyph_sample_position / vec3(atlas_size, 1.0f);
    
    vec4 color = texture(usampler3D(glyph_atlas, samp), sample_position_normalized);
    uint brightness = uint(color.r);
    if(brightness < 1)
    {
        discard;
    }
    out_color = vec4(frag_color, 1.0f);
}