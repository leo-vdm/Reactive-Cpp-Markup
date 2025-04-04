#include "arena.h"
#include "platform.h"
#include "DOM.h"

/*
struct size_axis 
{
    Measurement desired;
    Measurement min_measurement; // Must be in px or %
    Measurement max_measurement; // Must be in px or %
    
    // Calculated
    float current;
};
*/
/*
struct shape_axis 
{
    size_axis width;
    size_axis height;
};
*/

struct size_axis
{
    union
    {
        struct
        {
            Measurement desired;
            Measurement min; // Must be in px or %
            Measurement max; // Must be in px or %             
            
            Measurement margin1; // Must be in px, % or grow             
            Measurement margin2; // Must be in px, % or grow             
            
            Measurement padding1; // Must be in px or %
            Measurement padding2; // Must be in px or %
        };
        
        Measurement measures[7];
    };
    
    // Calculated
    float current;
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
    TEXT_COMBINED,
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
            FontHandle font_id;
            uint16_t font_size;     
            char* text_content;
        } TEXT;
        struct
        {
            FontPlatformShapedGlyph* first_glyph;
            uint32_t glyph_count;
        } TEXT_COMBINED;
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
    Arena* layout_element_arena;
    bounding_box curr_bounding;
};

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
    
    memcpy(&target->sizing.width.desired, &in->width, sizeof(Measurement));
    memcpy(&target->sizing.width.min, &in->min_width, sizeof(Measurement));
    memcpy(&target->sizing.width.max, &in->max_width, sizeof(Measurement));

    memcpy(&target->sizing.width.margin1, &in->margin.left, sizeof(Measurement));
    memcpy(&target->sizing.width.margin2, &in->margin.right, sizeof(Measurement));
    
    memcpy(&target->sizing.width.padding1, &in->padding.left, sizeof(Measurement));
    memcpy(&target->sizing.width.padding2, &in->padding.right, sizeof(Measurement));


    memcpy(&target->sizing.height.desired, &in->height, sizeof(Measurement));
    memcpy(&target->sizing.height.min, &in->min_height, sizeof(Measurement));
    memcpy(&target->sizing.height.max, &in->max_height, sizeof(Measurement));

    memcpy(&target->sizing.height.margin1, &in->margin.top, sizeof(Measurement));
    memcpy(&target->sizing.height.margin2, &in->margin.bottom, sizeof(Measurement));
    
    memcpy(&target->sizing.height.padding1, &in->padding.top, sizeof(Measurement));
    memcpy(&target->sizing.height.padding2, &in->padding.bottom, sizeof(Measurement));

    
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
    *first_unpacked_child = (LayoutElement*)context->layout_element_arena->next_address; 
    
    Element* curr = first_child;
    while(curr)
    {
        count++;
        LayoutElement* converted = (LayoutElement*)Push(context->layout_element_arena, sizeof(LayoutElement));
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
                    converted->TEXT.text_content = curr->Text.temporal_text;
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

void sanitize_size_axis(size_axis* parent, size_axis* child)
{
    // Checking all disalowed measuremnt combinations
    if(parent->desired.type == MeasurementType::FIT && child->desired.type == MeasurementType::PERCENT)
    {
        child->desired.type = MeasurementType::FIT; 
    }
    else if(parent->desired.type == MeasurementType::FIT && child->desired.type == MeasurementType::GROW)
    {
        child->desired.type = MeasurementType::FIT; 
    }
    
    // Note(Leo): min/max must be in pixels or grow if parent desired type is fit since it doesnt make sense to try and 
    //            calculate otherwise (circular dependency of the parent on the childs actual clipped size).
    if(parent->desired.type == MeasurementType::FIT)
    {
        if(child->min.type !=  MeasurementType::PIXELS)
        {
            child->min.type = MeasurementType::PIXELS;
            child->min.size = 0.0f;
        }
        if(child->max.type !=  MeasurementType::PIXELS)
        {
            child->min.type = MeasurementType::PIXELS;
            child->min.size = 0.0f;
        }
    }
    
    // Note(Leo): Margin measurements must be pixels if parent desired type is fit.
    //            IF they are % or grow just set them to 0px
    if(parent->desired.type == MeasurementType::FIT)
    {
        if(child->margin1.type != MeasurementType::PIXELS)
        {
            child->margin1.type = MeasurementType::PIXELS;
            child->margin1.size = 0.0f;
        }
        if(child->margin2.type != MeasurementType::PIXELS)
        {
            child->margin2.type = MeasurementType::PIXELS;
            child->margin2.size = 0.0f;
        }
    }
    
    // Bake values if we can
    if(parent->desired.type == MeasurementType::PIXELS)
    {
        if(child->desired.type == MeasurementType::PERCENT)
        {
            // Note(Leo): Size for a percentage should be normalized
            child->desired.size = child->desired.size * parent->desired.size;
            child->desired.type = MeasurementType::PIXELS;
        }
        if(child->min.type == MeasurementType::PERCENT)
        {
            child->min.size = child->min.size * parent->desired.size;
            child->min.type = MeasurementType::PIXELS; 
        }
        if(child->max.type == MeasurementType::PERCENT)
        {
            child->max.size = child->max.size * parent->desired.size;
            child->max.type = MeasurementType::PIXELS;
        }
    }
    
    // Note(Leo): Padding measurements must be in pixels if an element is desired type fit.
    //            Padding is based on the size of the element itself.
    if(child->desired.type == MeasurementType::FIT || child->desired.type == MeasurementType::GROW)
    {
        if(child->padding1.type != MeasurementType::PIXELS)
        {
            child->padding1.type = MeasurementType::PIXELS;
            child->padding1.size = 0.0f;
        }
        if(child->padding2.type != MeasurementType::PIXELS)
        {
            child->padding2.type = MeasurementType::PIXELS;
            child->padding2.size = 0.0f;
        }
    }
    else if(child->desired.type == MeasurementType::PIXELS) // Bake padding sizes if we can
    {
        if(child->padding1.type != MeasurementType::PERCENT)
        {
            child->padding1.type = MeasurementType::PIXELS;
            child->padding1.size = child->padding1.size * child->desired.size;
        }
        if(child->padding2.type != MeasurementType::PERCENT)
        {
            child->padding2.type = MeasurementType::PIXELS;
            child->padding2.size = child->padding2.size * child->desired.size;
        }
    }
}

// Note(Leo): Explanation for first pass
// First pass checks all the children of the given element for erroneous sizing and passes down known sizes (pixels or calculated 
// %)
// Also combines sibling text elements and measures + wraps them if it can
void shape_first_pass(shaping_context* context, LayoutElement* parent)
{
    MeasurementType p_width_type = parent->sizing.width.desired.type;
    MeasurementType p_height_type = parent->sizing.height.desired.type;
    
    float p_width = parent->sizing.width.desired.type == MeasurementType::PIXELS ? parent->sizing.width.desired.size : 0.0f;
    float p_height = parent->sizing.height.desired.type == MeasurementType::PIXELS ? parent->sizing.height.desired.size : 0.0f;
    
    LayoutDirection dir = parent->dir;
    LayoutElement* target = parent->children + parent->child_count; 
    LayoutElement* curr_child = parent->children;
    
    // Todo(Leo): Is this correct?
    while(curr_child < target)
    {
        MeasurementType width_type = curr_child->sizing.width.desired.type;
        MeasurementType height_type = curr_child->sizing.height.desired.type;
        
        // Check if this is a text child
        if(curr_child->type == LayoutElementType::TEXT)
        {
            // Note(Leo): There cant be more text children than total children in the parent so we wont underallocate here
            char** text_contents = (char**)AllocScratch(parent->child_count*sizeof(char*));
            FontHandle* text_fonts = (FontHandle*)AllocScratch(parent->child_count*sizeof(char*));
            uint16_t* font_sizes = (uint16_t*)AllocScratch(parent->child_count*sizeof(char*));
            
            // Aggregate text children
            int text_sibling_count = 0;
            LayoutElement* curr_text = curr_child;
            // Line height will be the height of the largest font
            int line_height = 0;
            while(curr_text < target && curr_child->type == LayoutElementType::TEXT)
            {
                text_contents[text_sibling_count] = (curr_child + text_sibling_count)->TEXT.text_content;
                text_fonts[text_sibling_count] = (curr_child + text_sibling_count)->TEXT.font_id;
                font_sizes[text_sibling_count] = (curr_child + text_sibling_count)->TEXT.font_size;
            
                line_height = MAX(line_height, (curr_child + text_sibling_count)->TEXT.font_size);
                
                text_sibling_count++;
                curr_text++;
            }
            
            // Try to wrap
            // Note(Leo): Automatic Wrapping only happens if the parent has a set width or a set max-width
            //            fit and grow will not wrap unless max-width is set and text is the only child/layout dir is vertical.
            //            '\n' is an exception since its a manual newline and will always be respected.
            uint32_t wrapping_point = 0;
            bool only_text_children = parent->child_count == text_sibling_count;
            if(p_width_type == MeasurementType::PIXELS && (parent->dir == LayoutDirection::VERTICAL || only_text_children))
            {
                wrapping_point = (uint32_t)parent->sizing.width.desired.size;
            } 
            else if(parent->sizing.width.max.type == MeasurementType::PIXELS && (parent->dir == LayoutDirection::VERTICAL || only_text_children))
            {
                wrapping_point = (uint32_t)parent->sizing.width.max.size;
            }
            else // Cant wrap 
            {
                wrapping_point = 0;
            }
            
            // Shape text
            uint32_t required_width = 0;
            FontPlatformShapedText result = {};
            FontPlatformShapeMixed(context->shape_arena, &result, text_contents, text_fonts, font_sizes, text_sibling_count, wrapping_point, line_height);
            
            // Convert current TEXT element to a combined text
            curr_child->type = LayoutElementType::TEXT_COMBINED;
            curr_child->TEXT_COMBINED.first_glyph = result.first_glyph; 
            curr_child->TEXT_COMBINED.glyph_count = result.glyph_count; 
            curr_child->sizing.width.current = (float)result.required_width;
            curr_child->sizing.height.current = (float)result.required_height;

            // Note(Leo): These may be more convenient for some calculations that loop over children than .current
            curr_child->sizing.width.desired.size = curr_child->sizing.width.current;
            curr_child->sizing.width.desired.type = MeasurementType::PIXELS;
            
            curr_child->sizing.height.desired.size = curr_child->sizing.height.current;
            curr_child->sizing.height.desired.type = MeasurementType::PIXELS;
            
            DeAllocScratch(font_sizes);
            DeAllocScratch(text_fonts);
            DeAllocScratch(text_contents);
            
            // Skip outer iteration over the other text siblings which have been combined into this one.
            curr_child = curr_child + text_sibling_count;
            continue;
        }
        
        sanitize_size_axis(&parent->sizing.width, &curr_child->sizing.width);
        sanitize_size_axis(&parent->sizing.height, &curr_child->sizing.height);
        
        curr_child++;
    }
}

// Calculates the extra size required for this child and adds it to the parent axis's calculated field
// Note(Leo): is_vertical indicates whether we are sizing the height axis or the width axis.
void grow_parent_axis(LayoutElement* parent_element, LayoutElement* child_element, bool is_vertical)
{
    size_axis* parent = is_vertical ? &parent_element->sizing.height : &parent_element->sizing.width;
    size_axis* child = is_vertical ? &child_element->sizing.height : &child_element->sizing.width;
    
    // Dont need any sizing calculations if parent is fixed size.
    if(parent->desired.type == MeasurementType::PIXELS)
    {
        parent->current = parent->desired.size;
        return;
    }
    
    // Note(Leo): Doing the vertical axis sizing with a horizontal layout parent is the same formula
    //            as doing the horzontal axis with a vertical layout parent. 
    //            Doing the vertical axis sizing with a vertical layout is the same formula as matching
    //            horizontal axis and layout 
    LayoutDirection dir = parent_element->dir;
    
    bool additive_dir = false; 
    if(dir == LayoutDirection::HORIZONTAL && is_vertical)
    {
        additive_dir = false;
    }
    else if(dir == LayoutDirection::VERTICAL && !is_vertical)
    {
        additive_dir = false;
    }
    else 
    {
        additive_dir = true;
    }
    
    float margin_size = 0;
    if(child->margin1.type == MeasurementType::PIXELS)
    {
        margin_size += child->margin1.size;
    }
    if(child->margin2.type == MeasurementType::PIXELS)
    {
        margin_size += child->margin2.size;
    }
    
    // Note(Leo): No children should have the 'fit' type left at this point, they should all be either grow, pixels or percent
    // Note(Leo): Children should have their desired sizing already clipped with their min and max sizing if they arent %
    //            If they have % values still to calculate those will be done in the third pass and the child will be clipped then 
    if(child->desired.type == MeasurementType::GROW || child->desired.type == MeasurementType::PERCENT)
    {
        if(additive_dir)
        {
            parent->current += child->current + margin_size;
        }
        else // Note(Leo): display type manual can do this branch aswell when its implemented
        {
            parent->current = MAX(parent->current, child->current + margin_size);
        }
    }
    else
    {
        assert(child->desired.type == MeasurementType::PIXELS);
        if(additive_dir)
        {
            parent->current += child->desired.size + margin_size;
        }
        else
        {
            parent->current = MAX(parent->current, child->desired.size + margin_size);
        }
    }
}

// Gets the final size the parent will request for the second pass. is_vertical indicates which axis is being sized.
void size_parent_axis(LayoutElement* parent_element, bool is_vertical)
{
    size_axis* parent = is_vertical ? &parent_element->sizing.height : &parent_element->sizing.width;
    
    // Note(Leo): Padding only expands the size of FIT and GROW elements but doesnt affect the size of PERCENT or PIXEL sized elements 
    if(parent->desired.type == MeasurementType::PIXELS || parent->desired.type == MeasurementType::PERCENT)
    {
        // Cant really do anything if we dont know the parent size so just pass up the current size
        return;
    }
    
    if(parent->padding1.type == MeasurementType::PIXELS)
    {
        parent->current += parent->padding1.size;
    }
    if(parent->padding2.type == MeasurementType::PIXELS)
    {
        parent->current += parent->padding2.size;
    }
    
    if(parent->desired.type == MeasurementType::FIT)
    {
        parent->desired.type = MeasurementType::PIXELS;
        parent->desired.size = parent->current;

        // Clip the parent if we know how large it wants to be
        if(parent->min.type == MeasurementType::PIXELS)
        {
            parent->desired.size = MAX(parent->desired.size, parent->min.size);
            parent->min.size = 0.0f;
        }
        if(parent->max.type == MeasurementType::PIXELS)
        {
            // Note(Leo): max of 0 means no max
            if(parent->max.size > 0.0f)
            {
                parent->desired.size = MIN(parent->desired.size, parent->max.size);
            }
            parent->max.size = 0.0f;
        }
    }
    else if(parent->desired.type == MeasurementType::GROW)
    {
        // Grows dont get smaller so clip its min if we know it
        if(parent->min.type == MeasurementType::PIXELS)
        {
            parent->current = MAX(parent->current, parent->min.size);
            parent->min.size = 0.0f;
        }
        // Only clip the grow if its already larger than its max. Otherwise it has to be clipped in the third pass
        // once its given a chance to grow
        // If the grow is already at its max size it cant grow further so we can change its type
        if(parent->max.type == MeasurementType::PIXELS && parent->max.size <= parent->current)
        {
            parent->desired.size = parent->max.size; 
            parent->desired.type = MeasurementType::PIXELS; 
            parent->current = parent->desired.size;
            parent->max.size = 0.0f;
        }
    }
}

// Note(Leo): Explanation of second pass
// Second pass is from the bottom of the tree upwards.
// Text sizes are hoisted up so that fit elements can calculate a size and grow elements can calculate their minimum size
// Grow behaviour works such that if a child of grow is % sized or has a % min/max size the childs calculated size is used
// instead to get the grow's min size. % sized margins are ignored entirely for calculating the grow's min size.
// % padding of a grow parent is also ignored when calculating its width.
// The grow parent's min/max size are ignored until the third pass when its actual grown size can be calculated.
void shape_second_pass(shaping_context* context, LayoutElement* parent)
{
    LayoutDirection dir = parent->dir;
    LayoutElement* target = parent->children + parent->child_count; 
    LayoutElement* curr_child = parent->children;
    
    // Todo(Leo): Is this correct?
    while(curr_child < target)
    {
        if(curr_child->type != LayoutElementType::TEXT) // We need to skip all the non-combined text
        {
            grow_parent_axis(parent, curr_child, false); // Width sizing
            grow_parent_axis(parent, curr_child, true);  // Height sizing
        }
        
        curr_child++;
    }
    
    size_parent_axis(parent, false); // Width sizing
    size_parent_axis(parent, true); // Height sizing
    
}

// Calculates the final size of the child axis (if it isnt a grow)
void size_child_axis(LayoutElement* parent_element, LayoutElement* child_element, bool is_vertical)
{
    size_axis* parent = is_vertical ? &parent_element->sizing.height : &parent_element->sizing.width;
    size_axis* child = is_vertical ? &child_element->sizing.height : &child_element->sizing.width;
    float p_size = parent->desired.size;
    float p_accumulated = 0.0f;
    
    // Elements with % sized measures need to calculated
    if(child->desired.type == MeasurementType::PERCENT)
    {
        child->desired.size = child->desired.size * p_size;
        child->desired.type = MeasurementType::PIXELS;
    }
    if(child->max.type == MeasurementType::PERCENT)
    {
        child->max.size = child->max.size * p_size;
        child->max.type = MeasurementType::PIXELS;
        // Clip element
        if(child->desired.type == MeasurementType::PIXELS)
        {
            child->desired.size = MIN(child->max.size, child->desired.size);
        }
    }
    if(child->min.type == MeasurementType::PERCENT)
    {
        child->min.size = child->min.size * p_size;
        child->min.type = MeasurementType::PIXELS;
        // Clip element
        if(child->desired.type == MeasurementType::PIXELS)
        {
            child->desired.size = MAX(child->min.size, child->desired.size);
        }
    }
    
    if(child->margin1.type == MeasurementType::PERCENT)
    {
        child->margin1.size = child->margin1.size * p_size;
        child->margin1.type = MeasurementType::PIXELS;
    }
    if(child->margin2.type == MeasurementType::PERCENT)
    {
        child->margin2.size = child->margin2.size * p_size;
        child->margin2.type = MeasurementType::PIXELS;
    }
    
    return; 
}

// Adds together all the child's known axis sizes and returns the total
float accumulate_child_axis(size_axis* child)
{
    float p_size_accumulated = 0.0f;
    
    if(child->desired.type == MeasurementType::PIXELS)
    {
        p_size_accumulated += child->desired.size;
    }
    if(child->margin1.type == MeasurementType::PIXELS)
    {
        p_size_accumulated += child->margin1.size;
    }
    if(child->margin2.type == MeasurementType::PIXELS)
    {
        p_size_accumulated += child->margin2.size;
    }
    
    return p_size_accumulated;
}

// Note(Leo): This assumes all the growth measures receive growth_space worth of room, if we want them to have different weights this has to change
// Adds grow_size to all the grow measuer in the child
void grow_child(size_axis* child, float growth_space)
{
    if(child->desired.type == MeasurementType::GROW)
    {
        // Clipping and growing the grow element
        child->desired.size = MAX(child->min.size, child->current + growth_space);
        if(child->max.size > 0.0f)
        {
            child->desired.size = MIN(child->desired.size, child->max.size);
        }
        child->desired.type = MeasurementType::PIXELS;
    }
    if(child->margin1.type == MeasurementType::GROW)
    {
        child->margin1.type = MeasurementType::PIXELS;
        child->margin1.size = growth_space; 
    }
    if(child->margin2.type == MeasurementType::GROW)
    {
        child->margin2.type = MeasurementType::PIXELS;
        child->margin2.size = growth_space; 
    }
}

// Note(Leo): Explanation of final pass
// Final pass is another downward one.
// All required sizes should be known now. Parents find any grow/percent sized
// children and calculate their sizes. 
// Once all child sizes are calculated parents will place them relative to themselves to get the final screen coordinates.
// During placing parents cull any out of view elements and their entire subtree
void shape_final_pass(shaping_context* context, LayoutElement* parent)
{
    LayoutDirection dir = parent->dir;
    
    // Note(Leo): All elements should be pixel sized by the time this function gets to them.
    assert(parent->sizing.width.desired.type == MeasurementType::PIXELS);
    assert(parent->sizing.width.padding1.type == MeasurementType::PIXELS && parent->sizing.width.padding2.type == MeasurementType::PIXELS);
    assert(parent->sizing.height.desired.type == MeasurementType::PIXELS);
    assert(parent->sizing.height.padding1.type == MeasurementType::PIXELS && parent->sizing.height.padding2.type == MeasurementType::PIXELS);

    // Note(Leo): Children can only occupy width - padding pixels of space in the parent
    float p_width = parent->sizing.width.desired.size - (parent->sizing.width.padding1.size + parent->sizing.width.padding2.size);
    float p_width_accumulated = 0;
    
    float p_height = parent->sizing.height.desired.size - (parent->sizing.height.padding1.size + parent->sizing.height.padding2.size);
    float p_height_accumulated = 0;
    
    LayoutElement** revisit_width_elements = (LayoutElement**)AllocScratch(parent->child_count*sizeof(LayoutElement*));
    uint32_t revisit_width_element_count = 0;
    
    LayoutElement** revisit_height_elements = (LayoutElement**)AllocScratch(parent->child_count*sizeof(LayoutElement*));
    uint32_t revisit_height_element_count = 0;
    
    // Used to keep track of how many ways growth space gets shared.
    uint32_t grow_width_meaure_count = 0;
    uint32_t grow_height_meaure_count = 0;
    
    LayoutElement* children = parent->children;
    
    for(int i = 0; i < parent->child_count; i++)
    {
        if(children[i].type == LayoutElementType::TEXT) // We need to skip all the non-combined text
        {
            continue;
        }
        
        size_child_axis(parent, &children[i], false);
        size_child_axis(parent, &children[i], true);
        
        // Adding element widths to the parent accumulation
        if(dir == LayoutDirection::HORIZONTAL)
        {
            // Widths add together for a horiontal layout
            p_width_accumulated += accumulate_child_axis(&children[i].sizing.width);

            // For a horizontal layout height-grow elements get all the remaining space in the height of their parent
            // but that is an individual quantity for each of them to calculate
            p_height_accumulated = 0.0f;
        }
        else
        {
            assert(dir == LayoutDirection::VERTICAL);

            // For a vertical layout width-grow elements get all the remaining space in the width of their parent
            // but that is an individual quantity for each of them to calculate
            p_width_accumulated = 0.0f;
            
            // Heights add together for a vertical layout
            p_height_accumulated += accumulate_child_axis(&children[i].sizing.height);
        }
        
        // Elements with grow sized measures need to get revisited.
        if(children[i].sizing.width.desired.type == MeasurementType::GROW || children[i].sizing.width.margin1.type == MeasurementType::GROW || children[i].sizing.width.margin2.type == MeasurementType::GROW)
        {
            if(children[i].sizing.width.desired.type == MeasurementType::GROW)
            {
                grow_width_meaure_count++;
            }
            if(children[i].sizing.width.margin1.type == MeasurementType::GROW)
            {
                grow_width_meaure_count++;
            }
            if(children[i].sizing.width.margin2.type == MeasurementType::GROW)
            {
                grow_width_meaure_count++;
            }
            
            // Widths need to be summed together in horizontal layouts to find the remaining space in the parent
            if(dir == LayoutDirection::HORIZONTAL)
            {
                p_width_accumulated += children[i].sizing.width.current;
                revisit_width_elements[revisit_width_element_count] = &children[i];
                revisit_width_element_count++;
            }
            else // In a vertical layout we can immediately grow width measures
            {
                assert(dir == LayoutDirection::VERTICAL);
                
                // width-grow space in a vertical layout is the remaining space either side of the element
                float child_size = accumulate_child_axis(&children[i].sizing.width);
                float growth_space = (p_width - child_size) / grow_width_meaure_count;
                growth_space = MAX(growth_space, 0.0f);
                grow_width_meaure_count = 0; // Reset so other element can know how many growth measurements they have
                
                grow_child(&children[i].sizing.width, growth_space);
            }
        }
        if((children[i].sizing.height.desired.type == MeasurementType::GROW || children[i].sizing.height.margin1.type == MeasurementType::GROW || children[i].sizing.height.margin2.type == MeasurementType::GROW))
        {
            if(children[i].sizing.height.desired.type == MeasurementType::GROW)
            {
                grow_height_meaure_count++;
            }
            if(children[i].sizing.height.margin1.type == MeasurementType::GROW)
            {
                grow_height_meaure_count++;
            }
            if(children[i].sizing.height.margin2.type == MeasurementType::GROW)
            {
                grow_height_meaure_count++;
            }
            
            // Heights need to be summed together in vertical layouts to find the remaining space in the parent
            if(dir == LayoutDirection::VERTICAL)
            {
                p_height_accumulated += children[i].sizing.height.current;
                revisit_height_elements[revisit_height_element_count] = &children[i];
                revisit_height_element_count++;
            }
            else // In a horizontal layout we can immediately grow height measures
            {
                assert(dir == LayoutDirection::HORIZONTAL);
                
                // width-grow space in a vertical layout is the remaining space either side of the element
                float child_size = accumulate_child_axis(&children[i].sizing.height);
                float growth_space = (p_height - child_size) / grow_height_meaure_count;
                growth_space = MAX(growth_space, 0.0f);
                grow_height_meaure_count = 0; // Reset so other element can know how many growth measurements they have
                
                grow_child(&children[i].sizing.height, growth_space);
            }
        }
    }
    
    // Note(Leo): Since the required min size of growth elements is included in the p_width_accumulated this is the 
    //            extra leftover space. IF this is less than zero that indicates a overflow of the parent.
    float growth_space = p_width - p_width_accumulated;
    
    if(grow_width_meaure_count)
    {
        // Divying up the extra space if there is any
        growth_space = MAX(growth_space / grow_width_meaure_count, 0.0f);
        
        for(int i = 0; i < revisit_width_element_count; i++)
        {
            grow_child(&revisit_width_elements[i]->sizing.width, growth_space);
        } 
    }
    if(grow_height_meaure_count)
    {
        // Note(Leo): Since the required min size of growth elements is included in the p_height_accumulated this
        //            is the extra leftover space. IF this is less than zero that indicates a overflow of the parent.
        growth_space = p_height - p_height_accumulated;
        
        // Divying up the extra space if there is any
        growth_space = MAX(growth_space / grow_height_meaure_count, 0.0f);
        
        for(int i = 0; i < revisit_height_element_count; i++)
        {
            grow_child(&revisit_height_elements[i]->sizing.height, growth_space);
        } 
    }
    
    DeAllocScratch(revisit_width_elements);
    DeAllocScratch(revisit_height_elements);
}

void ShapingPlatformShape(Element* root_element, Arena* shape_arena, int element_count)
{
    shaping_context context = {};
    
    shape_arena->alloc_size = sizeof(LayoutElement);
    
    // Note(Leo): The unpacking behaviour depends on shape_arena being empty.
    assert(shape_arena && shape_arena->next_address == shape_arena->mapped_address);
    
    void* memory_block = Alloc(shape_arena, element_count*sizeof(LayoutElement));
    
    // Note(Leo): Give layout elements their own arena so that when we shape text inbetween unpacks the two things arent mixed
    Arena layout_element_arena = CreateArenaWith(memory_block, element_count*sizeof(LayoutElement), sizeof(sizeof(LayoutElement)));
    
    context.shape_arena = shape_arena;
    context.layout_element_arena = &layout_element_arena;
    
    LayoutElement* converted_root;
    uint16_t unpacked_count; // The amount of elements remaining that have been unpacked as children but have
    // not unpacked their children
    unpack(&context, root_element, &converted_root, &unpacked_count);
    assert(unpacked_count == 1); // Root shouldnt have any siblings
    
    LayoutElement* curr_element = (LayoutElement*)context.layout_element_arena->mapped_address; 
    
    // Note(Leo): Explanation: 
    // We first unpack root as a child element, which creates it as the first LayoutElement in the layout_element_arena.
    // Then we iterate over the shape arena. Since the unpacked root is the first (and only) element in the shape arena
    // we unpack its children. To do that we have to get its corresponding element from the dom, which is done by using the
    // elemet_id that was gotten when we unpacked it.
    // When we unpack its children they are converted to layout elements and contiguously allocated onto the layout_element_arena. 
    // We also add the number if children that root had onto the unpacked count which serves as a running tally of how many
    // children weve allocated and converted but have not called unpack on. We continue doing this until we have unpacked
    // all children.
    while(unpacked_count)
    {
        // Note(Leo): This relies on elements being indexed into the elements arena by their id
        Element* unpack_parent = root_element + curr_element->element_id;
        
        unpack(&context, unpack_parent->first_child, &(curr_element->children), &(curr_element->child_count));
        unpacked_count += curr_element->child_count;
        
        shape_first_pass(&context, curr_element);
        
        curr_element++;
        unpacked_count--;
    }
    
    // Iterating backwards which is the equivelant of going up from leaf elements.
    // A child element will always be visited before its parent since we unpacked in a breadth first schema
    curr_element = (LayoutElement*)context.layout_element_arena->next_address;
    curr_element--; // Note(Leo): -1 since we are pointing past the last element.
    while((uintptr_t)curr_element >= context.layout_element_arena->mapped_address)
    {
        if(curr_element->type != LayoutElementType::TEXT) // Skip non-combined text
        {
            shape_second_pass(&context, curr_element);
        }
        curr_element--;
    }
    
    // Iterating forward which is close enough to a breadth first walk
    // A parent element will always be visited before all its children
    curr_element = (LayoutElement*)context.layout_element_arena->mapped_address;
    while((uintptr_t)curr_element < context.layout_element_arena->next_address)
    {
        shape_final_pass(&context, curr_element);
        curr_element++;
    }
}