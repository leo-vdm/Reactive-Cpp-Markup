#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform sampler samp;
layout(set = 1, binding = 1) uniform texture2D textures[30];

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_scale;
layout(location = 2) in vec2 frag_texture_coord;
layout(location = 3) in vec4 frag_corners;
layout(location = 4) in flat int frag_image_index; 


layout(location = 0) out vec4 out_color;

float rounded_box(vec2 centre_position, vec2 size, float radius)
{
    return length(max(abs(centre_position) - size + radius, 0.0)) - radius;
}

float individual_corner_box(vec2 centre_position, vec2 measurements, vec4 radii)
{
    radii.xy = (centre_position.x > 0.0) ? radii.xy : radii.zw;
    radii.x  = (centre_position.y > 0.0) ? radii.x  : radii.y;
    vec2 q = abs(centre_position) - measurements + radii.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radii.x;
}

void main()
{
    // The pixel space scale of the rectangle.
    vec2 size = vec2(1.0f, 1.0f) * frag_scale;
    
    // Calculate distance to edge.   
    float distance = individual_corner_box(frag_texture_coord.xy - (size/2.0f), size / 2.0f, frag_corners);
   
    vec4 sampled_color = (frag_image_index != 0) ? texture(sampler2D(textures[frag_image_index], samp), frag_texture_coord) : frag_color;   
    
    // Note(Leo): No smooth implementation 
    // Todo(Leo): blanket AA method for after frame is fully rendered.
    if(distance > 0.0f)
    {
        discard;
    }
    
    vec4 quad_color = distance > 0.0f ? vec4(0.0f) : sampled_color;
    out_color = quad_color;
}