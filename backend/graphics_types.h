#include <vulkan/vulkan.h>
#pragma once

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


struct PushConstants
{
    alignas(8) vec2 screen_size;
    alignas(4) int32_t shape_count;
    alignas(4) bool invert_horizontal_axis;
    alignas(4) bool invert_vertical_axis;
};

struct SpecializationData 
{
    int32_t render_tile_size;
};

enum class CombinedInstanceType
{
    NORMAL     = 0,
    GLYPH      = 1,
    IMAGE_TILE = 2,
};

struct combined_instance 
{
    alignas(16) vec4 bounds;
    alignas(8) vec2 shape_position;
    alignas(8) vec2 shape_size;
    
    // Note(Leo): glyphs overlap their color overtop corners since they dont need it but need sample position.
    alignas(16) vec4 corners;
    
    // Note(Leo): sample position/size is packed overtop the RGBA color of the element background allowing them to be discrimated by type
    alignas(16) vec3 sample_position; // RGB
    alignas(8) vec2 sample_size;     // A
    alignas(4) int32_t type;
};
