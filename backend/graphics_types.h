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
    vec3 color;
    vec2 texture_coord;
};

struct UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 projection;
};

VkVertexInputBindingDescription vk_get_binding_description(vertex input); 
VkVertexInputAttributeDescription* vk_get_attribute_descriptions(vertex input, int* len = NULL);

void Identity(mat2* matrix);
void Identity(mat3* matrix);
void Identity(mat4* matrix);