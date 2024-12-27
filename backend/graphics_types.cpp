#include <vulkan/vulkan.h>
#include "graphics_types.h"
#include <cassert>
#include <cstring>

// Note(Leo): Input here is just for the c++ type matcher so that the function can always have the same name but gives a 
// description based on the type given to input
VkVertexInputBindingDescription vk_get_binding_description(vertex input) 
{
    VkVertexInputBindingDescription binding_description = {};

    binding_description.binding = 0;
    binding_description.stride = sizeof(vertex);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return binding_description;
}

const VkVertexInputAttributeDescription vertex_input_attribute_descriptions[] = 
{
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position)},
    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, color)},
    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, texture_coord)}
};

VkVertexInputAttributeDescription* vk_get_attribute_descriptions(vertex input, int* len)
{
    if(len)
    {
        *len = sizeof(vertex_input_attribute_descriptions)/sizeof(VkVertexInputAttributeDescription);
    }
    return (VkVertexInputAttributeDescription*)vertex_input_attribute_descriptions;
}

const float mat2_identity[2][2] = {{1, 0}, {0, 1}};
const float mat3_identity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const float mat4_identity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};


void Identity(mat2* matrix)
{
    assert(matrix);
    memcpy(matrix, mat2_identity, sizeof(mat2));
}
void Identity(mat3* matrix)
{
    assert(matrix);
    memcpy(matrix, mat3_identity, sizeof(mat3));
}
void Identity(mat4* matrix)
{
    assert(matrix);
    memcpy(matrix, mat4_identity, sizeof(mat4));
}