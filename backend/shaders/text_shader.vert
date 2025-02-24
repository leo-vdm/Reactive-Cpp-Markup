#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_texture_coord;

// Note(Leo): Instance measurements (except for depth) are in pixels which we then convert to relative screen sizes 
layout(location = 2) in vec3 instance_offsets;
layout(location = 3) in vec3 instance_color;
layout(location = 4) in vec2 instance_scale;
layout(location = 5) in uvec3 instance_glyph_offset;
layout(location = 6) in uvec2 instance_glyph_size;


layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_texture_coord;
layout(location = 2) out flat uvec2 glyph_size;
layout(location = 3) out flat uvec3 glyph_offset;

layout( push_constant ) uniform constants
{
	vec2 screen_size;
} PushConstants;


void main()
{
    vec2 scaled_positions = in_position * (instance_scale / PushConstants.screen_size);
    vec3 converted_offsets = instance_offsets / vec3(PushConstants.screen_size, 1.0f);
    gl_Position = vec4(converted_offsets, 1.0f) + vec4(scaled_positions, 0.0f, 1.0f);
    
    frag_color = instance_color;
    frag_texture_coord = in_texture_coord;
    glyph_offset = instance_glyph_offset;
    glyph_size = instance_glyph_size;
}