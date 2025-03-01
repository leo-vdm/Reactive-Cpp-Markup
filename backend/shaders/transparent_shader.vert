#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_texture_coord;

layout(location = 2) in vec3 instance_offsets;
layout(location = 3) in vec4 instance_color;
layout(location = 4) in vec2 instance_scale;
layout(location = 5) in vec4 instance_corners;
layout(location = 6) in int image_id;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out float shape_aspect_ratio;
layout(location = 2) out vec2 frag_texture_coord;
layout(location = 3) out vec4 frag_corners;
layout(location = 4) out flat int frag_image_index; 


layout( push_constant ) uniform constants
{
	vec2 screen_size;
	vec2 atlas_size;
} PushConstants;


void main() {
    // Note(Leo): multiplying by 4.0f is due to 
    // 1. vulkan using -1 to 1 for relative coords meaning that our 0 - 1 scale needs to be multiplied
    // by two and have 1 subtracted from it.
    // 2. our quads being sized from 0 to 1 aswell meaning they are half the screen size so we must multiply by
    // 2 again.
    vec2 scaled_positions = in_position * (instance_scale / PushConstants.screen_size) * vec2(4.0f);
    scaled_positions -= vec2(1.0f);
    vec3 converted_offsets = instance_offsets / vec3(PushConstants.screen_size, 1.0f) * vec3(4.0f, 4.0f, 1.0f);
    converted_offsets -= vec3(1.0f, 1.0f, 0.0f);
    gl_Position = vec4(converted_offsets, 1.0f) + vec4(scaled_positions, 0.0f, 1.0f);
    
    shape_aspect_ratio = instance_scale.x / instance_scale.y;
    
    frag_color = instance_color;
    frag_texture_coord = in_texture_coord * vec2(shape_aspect_ratio, 1.0f);
    frag_corners = instance_corners / instance_scale.y;
    frag_image_index = image_id;
}