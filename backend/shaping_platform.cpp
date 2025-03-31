#include "arena.h"
#include "platform.h"
#include "DOM.h"

struct size_axis 
{
    Measurement set_measurement;
    Measurement min_measurement; // Must be in px or %
    Measurement max_measurement; // Must be in px or %
    
    // Calculated
    int current;
};

struct shape_axis 
{
    size_axis width;
    size_axis height;
};


struct shape_clipping
{
    ClipStyle horizontal;
    uint16_t left_scroll; // How far in pixels we have scrolled from the left of the clipped region
    ClipStyle vertical;    
    uint16_t top_scroll; // How far in pixels we have scrolled from the top of the clipped region
};

struct bounding_box 
{
    float x, y, width, height;
};

// Controls how an element lays out its children
enum class LayoutDirection
{
    NONE, // For elements that cant have chilren
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
    shape_axis sizing;    
    Padding padding;
    Margin margin;
    
    bounding_box bounds;
    LayoutElement* children;
    uint16_t child_count;
    
    union 
    {
        struct
        {
            shape_color color;
            Corners corners;
            shape_clipping clipping;
            TextWrapping wrapping; // Controls how child text should be wrapped
        } NORMAL;
        struct
        {
            shape_color_transparent color;
            Corners corners;
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
            Corners corners;
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

void convert_element_style(InFlightStyle* in, LayoutElement* target)
{
    // Note(Leo): Bake in any pixel values while were here
    memcpy(&target->sizing.width.set_measurement, &in->width, sizeof(Measurement));
    if(in->width.type == MeasurementType::PIXELS)
    {
        target->sizing.width.current = in->width.size;
    }
    memcpy(&target->sizing.height.set_measurement, &in->height, sizeof(Measurement));
    if(in->height.type == MeasurementType::PIXELS)
    {
        target->sizing.height.current = in->height.size;
    }

    memcpy(&target->margin, &in->margin, sizeof(Margin)); 
    memcpy(&target->padding, &in->padding, sizeof(Padding));
    
    target->display = in->display;
    
    switch(target->type)
    {
        case(LayoutElementType::NORMAL):
            {
                memcpy(&target->NORMAL.corners, &in->corners, sizeof(Corners));
                // Note(Leo): This relies on StyleColor and shape_color using same sized components and being in rgba order
                //          (we are under-copying from the style's color since we dont want the alpha component)
                static_assert(sizeof(shape_color) < sizeof(StyleColor));
                memcpy(&target->NORMAL.color, &in->corners, sizeof(shape_color));
                target->NORMAL.clipping.horizontal = in->horizontal_clipping;
                target->NORMAL.clipping.vertical = in->vertical_clipping;
                target->NORMAL.wrapping = in->wrapping;
            }
            break;
        case(LayoutElementType::NORMAL_TRANSPARENT):
            {
                memcpy(&target->NORMAL_TRANSPARENT.corners, &in->corners, sizeof(Corners)); 
                // Note(Leo): These two should be the same
                static_assert(sizeof(shape_color_transparent) == sizeof(StyleColor));
                memcpy(&target->NORMAL_TRANSPARENT.color, &in->color, sizeof(StyleColor)); 
                target->NORMAL_TRANSPARENT.clipping.horizontal = in->horizontal_clipping;
                target->NORMAL_TRANSPARENT.clipping.vertical = in->vertical_clipping;
                target->NORMAL_TRANSPARENT.wrapping = in->wrapping;
            }
            break;
        case(LayoutElementType::IMAGE):
            {
                memcpy(&target->IMAGE.corners, &in->corners, sizeof(Corners)); 
            }
            break;
        case(LayoutElementType::TEXT):
            {
                // Note(Leo): Text cant have transparency.
                static_assert(sizeof(shape_color) < sizeof(StyleColor));
                memcpy(&target->TEXT.text_color, &in->text_color, sizeof(shape_color));
                target->TEXT.font_id = in->font_id;
                target->TEXT.font_size = in->font_size;
            }
            break;
        default:
            break;
    }
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
        
        // Converting element type to layout element type
        switch(curr->type)
        {
            case(ElementType::VDIV):
            case(ElementType::HDIV):
            case(ElementType::ROOT):
            case(ElementType::GRID):
                {
                    if(curr->working_style.color.a >= 1.0f)
                    {
                        converted->type = LayoutElementType::NORMAL;
                    }
                    else
                    {
                        converted->type = LayoutElementType::NORMAL_TRANSPARENT;
                    }
                    
                    if(curr->type == ElementType::VDIV || curr->type == ElementType::ROOT)
                    {
                        converted->dir = LayoutDirection::VERTICAL;
                    }
                    else if(curr->type == ElementType::HDIV)
                    {
                        converted->dir = LayoutDirection::HORIZONTAL;
                    }
                    else 
                    {
                        converted->dir = LayoutDirection::GRID;
                    }
                    convert_element_style(&curr->working_style, converted);
                }
                break;
            case(ElementType::CUSTOM):
                {
                    // Note(Leo): Custom element is just a parent for the root of the component, we flatten it here by skipping it
                    // and replacing it with its root
                    Element* comp_root = curr->first_child;
                    assert(comp_root->type == ElementType::ROOT);
                    converted->element_id = comp_root->id;
                    if(comp_root->working_style.color.a >= 1.0f)
                    {
                        converted->type = LayoutElementType::NORMAL;
                    }
                    else
                    {
                        converted->type = LayoutElementType::NORMAL_TRANSPARENT;
                    }
                    converted->dir = LayoutDirection::VERTICAL;
                    convert_element_style(&comp_root->working_style, converted);
                }
                break;
            case(ElementType::TEXT):
                {
                    converted->type = LayoutElementType::TEXT;
                    converted->dir = LayoutDirection::NONE;
                    convert_element_style(&curr->working_style, converted);
                }
                break;
            case(ElementType::IMG):
                {
                    converted->type = LayoutElementType::IMAGE;
                    converted->dir = LayoutDirection::NONE;
                    convert_element_style(&curr->working_style, converted);
                }
                break;
            default:
                assert(0);
                break;
        }
        
        curr = curr->next_sibling;
    }
    
    *child_count = count; 
}

// Note(Leo): Explanation for first pass
// First pass walks up from the leafs in the tree.
// We measure text using our font platform but dont do any wrapping yet.
// Combine sibling text elements



void shape_first_pass()
{
//    if(element->type == LayoutElementType::NORMAL, )
//    bool has_horizontal_scroll  
//    bool has_vertical_scroll
    
}

void shape_second_pass(LayoutElement* element)
{

}

void shape_final_pass(LayoutElement* element)
{
    
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