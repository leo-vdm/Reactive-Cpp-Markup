#include <vulkan/vulkan.h>


struct vec2 
{
    union
    {
        float x;
        float u;
    };

    union
    {
        float y;
        float v;
    };
};

struct vec3
{
    union
    {
        float x;
        float u;
        float r;
    };

    union
    {
        float y;
        float v;
        float g;
    };
    

    union
    {
        float z;
        float w;
        float b;
    };
};

struct vec4
{
    union
    {
        float r;
    };

    union
    {
        float g;
    };
    
    union
    {
        float b;
    };
    
    union
    {
        float a;
    };
};

struct ivec2 
{
    union
    {
        int x;
        int u;
    };

    union
    {
        int y;
        int v;
    };
};

struct ivec3 
{
    union
    {
        int x;
        int u;
    };

    union
    {
        int y;
        int v;
    };
    

    union
    {
        int z;
        int w;
    };
};

struct uvec2 
{
    union
    {
        uint32_t x;
        uint32_t u;
    };

    union
    {
        uint32_t y;
        uint32_t v;
    };
};

struct uvec3 
{
    union
    {
        uint32_t x;
        uint32_t u;
    };

    union
    {
        uint32_t y;
        uint32_t v;
    };
    

    union
    {
        uint32_t z;
        uint32_t w;
    };
};

struct bvec2 
{
    union
    {
        bool x;
        bool u;
    };

    union
    {
        bool y;
        bool v;
    };
};

struct bvec3 
{
    union
    {
        bool x;
        bool u;
    };

    union
    {
        bool y;
        bool v;
    };
    union
    {
        bool z;
        bool w;
    };
};

struct mat2
{
    union
    {
    float m[2][2];
    float e[4];
    };
};

struct mat3
{
    union
    {
    float m[3][3];
    float e[9];
    };
};

struct mat4
{
    union
    {
    float m[4][4];
    float e[16];
    };
};

struct vertex
{
    vec2 position;
    vec2 texture_coord;
};

//struct opaque_vertex
//{
//    vec2 position;
//    vec2 texture_coord;
//};
//
//struct transparent_vertex
//{
//    vec2 position;
//    vec2 texture_coord;
//};

struct opaque_instance
{
    vec3 offsets;
    vec3 color;
    vec2 scale;
    vec4 corners;
};

struct transparent_instance
{
    vec3 offsets;
    vec4 color;
    vec2 scale;
    vec4 corners;
    int32_t image_index;
};

struct text_instance
{
    vec3 offsets;
    vec3 color;
    vec2 scale;
    vec3 instance_glyph_offset;
    vec2 instance_glyph_size;
};

struct UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 projection;
};

struct PushConstants
{
    vec2 screen_size;
    vec2 atlas_size;
};

VkVertexInputBindingDescription vk_get_binding_description(vertex input);
VkVertexInputBindingDescription vk_get_binding_description(opaque_instance input); 
VkVertexInputBindingDescription vk_get_binding_description(transparent_instance input); 
VkVertexInputBindingDescription vk_get_binding_description(text_instance input);

VkVertexInputAttributeDescription* vk_get_attribute_descriptions(opaque_instance input, int* len = NULL);
VkVertexInputAttributeDescription* vk_get_attribute_descriptions(transparent_instance input, int* len = NULL);
VkVertexInputAttributeDescription* vk_get_attribute_descriptions(text_instance input, int* len);

void Identity(mat2* matrix);
void Identity(mat3* matrix);
void Identity(mat4* matrix);