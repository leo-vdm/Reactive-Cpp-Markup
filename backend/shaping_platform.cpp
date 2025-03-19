#include "arena.h"
#include "platform.h"
#include "DOM.h"

enum class SizingType
{
    FIT, // Shrinks the element around its content size
    GROW, // Expand to fill available space in the parent, sharing it with other grow elements proportionaltely to their 
          // weight values.
    PERCENT, // Expects a 0-1 range. Sizes based on the parent's size minus padding 
    PIXELS,
};

enum class DisplayType
{
    NORMAL, // Parent controls where this element is placed
    MANUAL, // Element manually places itself inide its parent, Element will no longer affect the size of its siblings or 
            // parent
    HIDDEN, // Element and all its children are currently not displayed.
};

struct size_axis 
{
    union {
        struct
        {
            float min;
            float max;
        } min_max;
        
        float percent;
        
        float weight;
    } size;
    
    SizingType type;
};

struct shape_padding 
{
    uint16_t left;
    uint16_t rigth;
    uint16_t top;
    uint16_t bottom;
};

struct shape_margin 
{
    uint16_t left;
    uint16_t rigth;
    uint16_t top;
    uint16_t bottom;
};

// Corner radii in px
struct shape_corners
{
    uint16_t top_left;
    uint16_t top_right;
    uint16_t bottom_left;
    uint16_t bottom_right;
};

struct shape_axis 
{
    size_axis width;
    size_axis height;
};

enum class TextWrapping
{
    WORDS, // Text is wrapped but words are kept together 
    CHARS, // Text is wrapped but in arbitrary positions
    NONE, // Text will not wrap and will overflow
};

enum class ClipStyle
{
    HIDDEN, // Just hide clipped region. Clipped region can still be scrolled through internal means
    SCROLL, // Hide clipped region and show a scroll bar
};

struct shape_clipping
{
    ClipStyle vertical_clip;    
    uint16_t top_scroll; // How far in pixels we have scrolled from the top of the clipped region
    ClipStyle horizontal_clip;
    uint16_t left_scroll; // How far in pixels we have scrolled from the left of the clipped region
};

struct bounding_box 
{
    float x, y, width, height;   
};

// Controls how an element lays out its children
enum class LayoutDirection
{
    VERTICAL,
    HORIZONTAL,
    GRID,
};

enum class LayoutElementType
{
    NORMAL, // Normal div types
    NORMAL_TRANSPARENT, // Same as normal but color has a non opaque alpha
    TEXT,
    IMAGE,
    END,
};

struct LayoutStyle 
{
    shape_axis sizing;    
    shape_padding padding;
    shape_margin margin;
};

struct shape_color 
{
    float r, g, b;  
};

struct shape_color_transparent
{
    float r, g, b, a;
};

struct LayoutElement
{
    uint32_t element_id;
    LayoutElementType type;
    LayoutDirection dir;
    DisplayType display;
    LayoutStyle style;
    bounding_box bounds;
    
    LayoutElement* children;
    uint16_t child_count;
    
    union 
    {
        struct
        {
            shape_color color;
            shape_corners corners;
            shape_clipping clipping;
            TextWrapping wrapping; // Controls how child text should be wrapped
        } NORMAL;
        struct
        {
            shape_color_transparent color;
            shape_corners corners;
            shape_clipping clipping;
            TextWrapping wrapping; // Controls how child text should be wrapped
        } NORMAL_TRANSPARENT;
        struct
        {
            shape_color text_color;
            uint16_t font_id;
            uint16_t font_size;     
            char* text_content;
        } TEXT;
        struct 
        {
            shape_corners corners;
            uint16_t image_id;
            uint16_t source_width;
            uint16_t source_height;
        } IMAGE;
    };
};

struct shaping_context 
{
    Arena* shape_arena;
    bounding_box curr_bounding;
};

#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

// Returns true if two bounding boxes intersect and optionally returns the intersection region
bool boxes_intersect(bounding_box* first, bounding_box* second, bounding_box* region = NULL)
{
    // Note(Leo): The Y-axis here is increasing downwards from the top of the screen
    int first_right = first->x + first->width;
    int second_right = second->x + second->width;
    int first_bottom = first->y + first->height;
    int second_bottom = second->y + second->height;
    if(first_right <= second->x || second_right <= first->x || first_bottom <= second->y || second_bottom <= first->y )
    {
        if(region)
        {
            *region = {};
        }
        return false;
    }
    region->x = MAX(first->x, second->x); 
    region->y = MAX(first->y, second->y); 
    region->width = MIN(first_right, second_right) - region->x;
    region->height = MIN(first_bottom, second_bottom) - region->y;
    return true;
}

// 'unpacks' the children of a parent element into a contiguous array of LayoutElements and puts the pointer to the
// first child into first_child and the count of children into child_count.
void unpack(shaping_context* context, Element* first_child, LayoutElement** first_unpacked_child, uint16_t* child_count)
{
    uint16_t count = 0;
    // Note(Leo): Assumes that we will get the next ID and that all allocations will be contiguous
    *first_unpacked_child = (LayoutElement*)context->shape_arena->next_address; 
    
    Element* curr = first_child;
    while(curr)
    {
        count++;
        LayoutElement* converted = (LayoutElement*)Push(context->shape_arena, sizeof(LayoutElement));
        converted->element_id = curr->id;
        curr = curr->next_sibling;
    }
    
    *child_count = count; 
}

void ShapingPlatformShape(Element* root_element, Arena* shape_arena)
{
    shaping_context context = {};
    
    shape_arena->alloc_size = sizeof(LayoutElement);
    
    context.shape_arena = shape_arena;
    
    LayoutElement* converted_root;
    uint16_t unpacked_count; // The amount of elements remaining that have been unpacked as children but have
    // not unpacked their children
    unpack(&context, root_element, &converted_root, &unpacked_count);
    assert(unpacked_count == 1); // Root shouldnt have any siblings
    
    LayoutElement* curr_element = (LayoutElement*)context.shape_arena->mapped_address; 
    
    // Note(Leo): Explanation: 
    // We first unpack root as a child element, which creates it as the first LayoutElement in the shape_arena.
    // Then we iterate over the shape arena. Since the unpacked root is the first (and only) element in the shape arena
    // we unpack its children. To do that we have to get its corresponding element from the dom, which is done by using the
    // elemet_id that was gotten when we unpacked it.
    // When we unpack its children they are converted to layout elements and contiguously allocated onto the shape_arena. 
    // We also add the number if children that root had onto the unpacked count which serves as a running tally of how many
    // children weve allocated and converted but have not called unpack on. We continue doing this until we have unpacked
    // all children.
    while(unpacked_count)
    {
        // Note(Leo): This relies on elements being indexed into the elements arena by their id
        Element* unpack_parent = root_element + curr_element->element_id;
        
        unpack(&context, unpack_parent->first_child, &(curr_element->children), &(curr_element->child_count));
        unpacked_count += curr_element->child_count;
        
        curr_element++;
        unpacked_count--;
    }
}