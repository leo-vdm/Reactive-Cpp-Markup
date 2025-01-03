#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_scale;
layout(location = 2) in vec2 frag_texture_coord;
layout(location = 3) in vec4 frag_corners;

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
    vec2 shape_size = vec2(1.0f, 1.0f) * frag_scale;

    // Calculate distance to edge.   
    float distance = individual_corner_box(frag_texture_coord - (shape_size / 2.0f), shape_size / 2.0f, frag_corners);

    // Note(Leo): No smooth implementation 
    // Todo(Leo): blanket AA method for after frame is fully rendered.
    if(distance > 0.0f)
    {
        discard;
    }
    
    out_color = vec4(frag_color, 1);
}