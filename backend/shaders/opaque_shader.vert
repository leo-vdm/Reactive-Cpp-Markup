#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_texture_coord;

layout(location = 2) in vec3 instance_offsets;
layout(location = 3) in vec3 instance_color;
layout(location = 4) in vec2 instance_scale;
layout(location = 5) in vec4 instance_corners;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_scale;
layout(location = 2) out vec2 frag_texture_coord;
layout(location = 3) out vec4 frag_corners;

void main() {
    //gl_Position = ubo.proj * ubo.view * ubo.model * vec4(in_position, 0.5f, 1.0f);
    vec2 scaled_positions = in_position * instance_scale;
    
    gl_Position =  ubo.proj * (vec4(instance_offsets, 1.0f) + vec4(scaled_positions, 0.0f, 1.0f));
    frag_color = instance_color;
    frag_texture_coord = in_texture_coord * instance_scale;
    frag_corners = instance_corners;
    frag_scale = instance_scale;
}