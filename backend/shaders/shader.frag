#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform sampler samp;
layout(set = 1, binding = 1) uniform texture2D textures[30];

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 image_color = texture(sampler2D(textures[1], samp), fragTexCoord); 
    outColor = mix(vec4(fragColor, 1), image_color, vec4(image_color.a));
}