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

VkVertexInputBindingDescription vk_get_binding_description(opaque_instance input)
{
    VkVertexInputBindingDescription binding_description = {};

    binding_description.binding = 1;
    binding_description.stride = sizeof(opaque_instance);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    return binding_description;
}

VkVertexInputBindingDescription vk_get_binding_description(transparent_instance input)
{
    VkVertexInputBindingDescription binding_description = {};

    binding_description.binding = 1;
    binding_description.stride = sizeof(transparent_instance);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    return binding_description;
}


VkVertexInputBindingDescription vk_get_binding_description(text_instance input)
{
    VkVertexInputBindingDescription binding_description = {};

    binding_description.binding = 1;
    binding_description.stride = sizeof(text_instance);
    binding_description.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    return binding_description;
}

const VkVertexInputAttributeDescription opaque_instance_input_attribute_descriptions[] = 
{
    // Vertex Members
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position)},
    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, texture_coord)},
    
    // Instance Members
    {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(opaque_instance, offsets)},
    {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(opaque_instance, color)},
    {4, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(opaque_instance, scale)},
    {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(opaque_instance, corners)}
};

VkVertexInputAttributeDescription* vk_get_attribute_descriptions(opaque_instance input, int* len)
{
    if(len)
    {
        *len = sizeof(opaque_instance_input_attribute_descriptions)/sizeof(VkVertexInputAttributeDescription);
    }
    return (VkVertexInputAttributeDescription*)opaque_instance_input_attribute_descriptions;
}

const VkVertexInputAttributeDescription transparent_instance_input_attribute_descriptions[] = 
{
    // Vertex Members
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position)},
    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, texture_coord)},
    
    // Instance Members
    {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(transparent_instance, offsets)},
    {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(transparent_instance, color)},
    {4, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(transparent_instance, scale)},
    {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(transparent_instance, corners)},
    {6, 1, VK_FORMAT_R32_SINT, offsetof(transparent_instance, image_index)}
};

VkVertexInputAttributeDescription* vk_get_attribute_descriptions(transparent_instance input, int* len)
{
    if(len)
    {
        *len = sizeof(transparent_instance_input_attribute_descriptions)/sizeof(VkVertexInputAttributeDescription);
    }
    return (VkVertexInputAttributeDescription*)transparent_instance_input_attribute_descriptions;
}


const VkVertexInputAttributeDescription text_instance_input_attribute_descriptions[] = 
{
    // Vertex Members
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position)},
    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, texture_coord)},
    
    // Instance Members
    {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(text_instance, offsets)},
    {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(text_instance, color)},
    {4, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(text_instance, scale)},
    {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(text_instance, instance_glyph_offset)},
    {6, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(text_instance, instance_glyph_size)},
};

VkVertexInputAttributeDescription* vk_get_attribute_descriptions(text_instance input, int* len)
{
    if(len)
    {
        *len = sizeof(text_instance_input_attribute_descriptions)/sizeof(VkVertexInputAttributeDescription);
    }
    return (VkVertexInputAttributeDescription*)text_instance_input_attribute_descriptions;
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