#include "platform.h"

struct shaping_context 
{
    uint32_t element_count;
    uint32_t glyph_count;
    uint32_t image_tile_count;
    
    // Note(Leo): These are included in elmement count so the non-manual/relative elements is elements minus these
    uint32_t manual_element_count;
    uint32_t relative_element_count;
    
    Arena* shape_arena;
    Arena* layout_element_arena;
    Arena* final_renderque;
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
    if(region)
    {
        region->x = MAX(first->x, second->x); 
        region->y = MAX(first->y, second->y); 
        region->width = MIN(first_right, second_right) - region->x;
        region->height = MIN(first_bottom, second_bottom) - region->y;   
    }
    
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
                memcpy(&target->NORMAL.color, &in->color, sizeof(StyleColor));
                target->NORMAL.clipping.horizontal = in->horizontal_clipping;
                target->NORMAL.clipping.vertical = in->vertical_clipping;
                target->NORMAL.wrapping = in->wrapping;
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
                memcpy(&target->TEXT.text_color, &in->text_color, sizeof(StyleColor));
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
        // Dont add curr or any of its children to layouts
        if(curr->flags & is_hidden())
        {
            curr = curr->next_sibling;
            continue;
        }
    
        count++;
        LayoutElement* converted = (LayoutElement*)Push(context->layout_element_arena, sizeof(LayoutElement));
        converted->element_id = curr->id;
        
        curr->last_sizing = converted;
        
        // Converting element type to layout element type
        switch(curr->type)
        {
            case(ElementType::VDIV):
            case(ElementType::HDIV):
            case(ElementType::ROOT):
            case(ElementType::GRID):
            {
                converted->type = LayoutElementType::NORMAL;
                
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
                // Moving over the element's scroll
                converted->NORMAL.clipping.left_scroll = curr->scroll.x;
                converted->NORMAL.clipping.top_scroll = curr->scroll.y;
                break;
            }
            case(ElementType::CUSTOM):
            {
                // Note(Leo): Custom element is just a parent for the root of the component, we flatten it here by skipping it
                // and replacing it with its root
                Element* comp_root = curr->first_child;
                assert(comp_root->type == ElementType::ROOT);
                converted->element_id = comp_root->id;
                converted->type = LayoutElementType::NORMAL;
                converted->dir = LayoutDirection::VERTICAL;
                convert_element_style(&comp_root->working_style, converted);
                break;
            }
            case(ElementType::TEXT):
            {
                converted->type = LayoutElementType::TEXT;
                converted->dir = LayoutDirection::NONE;
                converted->TEXT.text_content = curr->Text.temporal_text;
                converted->TEXT.text_length = curr->Text.temporal_text_length;
                convert_element_style(&curr->working_style, converted);
                break;
            }
            case(ElementType::IMG):
            {
                converted->type = LayoutElementType::IMAGE;
                converted->dir = LayoutDirection::NONE;
                converted->IMAGE.handle = curr->Image.handle;
                convert_element_style(&curr->working_style, converted);
                break;
            }
            case(ElementType::EACH):
            {
                // Note(Leo): Each gets flat unpacked into its parent since it does not actually have sizing 
                //            (it isnt a container)
                Pop(context->layout_element_arena, sizeof(LayoutElement));
                uint16_t extra_children = 0;
                LayoutElement* first_unpacked;
                unpack(context, curr->first_child, &first_unpacked, &extra_children);
                count += extra_children;
                count--;
                break;
            }
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
    if(parent->desired.type == MeasurementType::PIXELS && parent->padding1.type == MeasurementType::PIXELS &&
        parent->padding2.type == MeasurementType::PIXELS)
    {
        float p_size = (parent->desired.size - (parent->padding1.size + parent->padding2.size));
        if(child->desired.type == MeasurementType::PERCENT)
        {
            // Note(Leo): Size for a percentage should be normalized
            child->desired.size = child->desired.size * p_size;
            child->desired.type = MeasurementType::PIXELS;
        }
        if(child->min.type == MeasurementType::PERCENT)
        {
            child->min.size = child->min.size * p_size;
            child->min.type = MeasurementType::PIXELS; 
        }
        if(child->max.type == MeasurementType::PERCENT)
        {
            child->max.size = child->max.size * p_size;
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
        if(child->padding1.type == MeasurementType::PERCENT)
        {
            child->padding1.type = MeasurementType::PIXELS;
            child->padding1.size = child->padding1.size * child->desired.size;
        }
        if(child->padding2.type == MeasurementType::PERCENT)
        {
            child->padding2.type = MeasurementType::PIXELS;
            child->padding2.size = child->padding2.size * child->desired.size;
        }
    }
}

// Note(Leo): Explanation for first pass
// First pass checks all the children of the given element for erroneous sizing and passes down known sizes (pixels or calculated 
// %)
// Also combines sibling text elements and measures + wraps them if it can.
// Also also add the number of glyphs and tiles in an image into the context to eventaully get the renderque size
void shape_first_pass(shaping_context* context, LayoutElement* parent)
{
    MeasurementType p_width_type = parent->sizing.width.desired.type;
    MeasurementType p_height_type = parent->sizing.height.desired.type;
    
    float p_width = parent->sizing.width.desired.type == MeasurementType::PIXELS ? parent->sizing.width.desired.size : 0.0f;
    float p_height = parent->sizing.height.desired.type == MeasurementType::PIXELS ? parent->sizing.height.desired.size : 0.0f;
    
    LayoutDirection dir = parent->dir;
    LayoutElement* target = parent->children + parent->child_count; 
    LayoutElement* curr_child = parent->children;
    
    while(curr_child < target)
    {
        MeasurementType width_type = curr_child->sizing.width.desired.type;
        MeasurementType height_type = curr_child->sizing.height.desired.type;
        
        // Check if this is a text child
        if(curr_child->type == LayoutElementType::TEXT)
        {
            // Note(Leo): There cant be more text children than total children in the parent so we wont underallocate here
            void* text_view_mem = AllocScratch((parent->child_count + 1)*sizeof(StringView));
            StringView* text_views = align_mem(text_view_mem, StringView);
            
            void* text_font_mem = AllocScratch((parent->child_count + 1)*sizeof(FontHandle));
            FontHandle* text_fonts = align_mem(text_font_mem, FontHandle);
            
            void* font_size_mem = AllocScratch((parent->child_count + 1)*sizeof(uint16_t));
            uint16_t* font_sizes = align_mem(font_size_mem, uint16_t);
            
            void* text_color_mem = AllocScratch((parent->child_count + 1)*sizeof(StyleColor));
            StyleColor* text_colors = align_mem(text_color_mem, StyleColor);
            
            // Aggregate text children
            int text_sibling_count = 0;
            LayoutElement* curr_text = curr_child;
        
            while(curr_text < target && curr_text->type == LayoutElementType::TEXT)
            {
                text_views[text_sibling_count] = {curr_text->TEXT.text_content, curr_text->TEXT.text_length};
                text_fonts[text_sibling_count] = curr_text->TEXT.font_id;
                font_sizes[text_sibling_count] = curr_text->TEXT.font_size;
                text_colors[text_sibling_count] = curr_text->TEXT.text_color;
                 
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
            FontPlatformShapedText result = {};
            BEGIN_TIMED_BLOCK(TEXT_SHAPE);
            FontPlatformShapeMixed(context->shape_arena, &result, text_views, text_fonts, font_sizes, text_colors, text_sibling_count, wrapping_point);
            END_TIMED_BLOCK(TEXT_SHAPE);
            
            context->glyph_count += result.glyph_count;
            
            // Convert current TEXT element to a combined text
            curr_child->type = LayoutElementType::TEXT_COMBINED;
            curr_child->TEXT_COMBINED.first_glyph = result.first_glyph; 
            curr_child->TEXT_COMBINED.glyph_count = result.glyph_count; 
            curr_child->sizing.width.current = (float)result.required_width;
            curr_child->sizing.height.current = (float)result.required_height;

            // Note(Leo): These may be more convenient for some calculations that loop over children than .current
            curr_child->sizing.width.desired.size = (float)result.required_width;
            curr_child->sizing.width.desired.type = MeasurementType::PIXELS;
            
            curr_child->sizing.height.desired.size = (float)result.required_height;
            curr_child->sizing.height.desired.type = MeasurementType::PIXELS;
            
            DeAllocScratch(text_color_mem);
            DeAllocScratch(font_size_mem);
            DeAllocScratch(text_font_mem);
            DeAllocScratch(text_view_mem);
            
            // Skip outer iteration over the other text siblings which have been combined into this one.
            curr_child = curr_child + text_sibling_count;
            continue;
        }
        else if(curr_child->type == LayoutElementType::IMAGE)
        {
            context->image_tile_count += curr_child->IMAGE.handle->tiled_width * curr_child->IMAGE.handle->tiled_height;
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
    
    // Note(Leo): We actually should always do this otherwise parent wont know how much it has clipped
    //            which it needs to be able to do scrolling 
    /*
    // Dont need any sizing calculations if parent is fixed size.
    if(parent->desired.type == MeasurementType::PIXELS)
    {
        parent->current = parent->desired.size;
        return;
    }*/
    
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
        if(additive_dir && child_element->display != DisplayType::RELATIONAL && child_element->display != DisplayType::MANUAL)
        {
            parent->current += child->current + margin_size;
        }
        else
        {
            parent->current = MAX(parent->current, child->current + margin_size);
        }
    }
    else
    {
        assert(child->desired.type == MeasurementType::PIXELS);
        if(additive_dir && child_element->display != DisplayType::RELATIONAL && child_element->display != DisplayType::MANUAL)
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
            // Note(Leo): max of 0 means no max
            if(parent->max.size > 0.0f)
            {
                parent->desired.size = parent->max.size; 
                parent->desired.type = MeasurementType::PIXELS; 
                parent->current = parent->desired.size;
            }
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
    size_parent_axis(parent, true);  // Height sizing
    
}

// Calculates the final size of the child axis (if it isnt a grow)
void size_child_axis(LayoutElement* parent_element, LayoutElement* child_element, bool is_vertical)
{
    size_axis* parent = is_vertical ? &parent_element->sizing.height : &parent_element->sizing.width;
    size_axis* child = is_vertical ? &child_element->sizing.height : &child_element->sizing.width;
    
    // Note(Leo): Account for padding taking some of the parent's useable space.
    assert(parent->padding1.type == MeasurementType::PIXELS && parent->padding2.type == MeasurementType::PIXELS);
    float p_size = parent->desired.size - (parent->padding1.size + parent->padding2.size);
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
// Adds grow_size to all the grow measures in the child
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
        child->current = child->desired.size;
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
    
    // Nothing to do
    if(!parent->child_count)
    {
        return;
    }
    
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
            // Widths add together for a horiontal layout (Accept the manual placed elements)
            if(children[i].display != DisplayType::RELATIONAL && children[i].display != DisplayType::MANUAL)
            {
                p_width_accumulated += accumulate_child_axis(&children[i].sizing.width);
            }

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
            
            // Heights add together for a vertical layout (Accept the manual placed elements)
            if(children[i].display != DisplayType::RELATIONAL && children[i].display != DisplayType::MANUAL)
            {
                p_height_accumulated += accumulate_child_axis(&children[i].sizing.height);
            }
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
            if(dir == LayoutDirection::HORIZONTAL && children[i].display != DisplayType::RELATIONAL && children[i].display != DisplayType::MANUAL)
            {
                // Only add the min-width if desired isnt defined yet 
                if(children[i].sizing.width.desired.type == MeasurementType::GROW)
                {
                    p_width_accumulated += children[i].sizing.width.current;
                }
                revisit_width_elements[revisit_width_element_count] = &children[i];
                revisit_width_element_count++;
            }
            else // In a vertical layout we can immediately grow width measures
            {
                assert(dir == LayoutDirection::VERTICAL);
                
                // width-grow space in a vertical layout is the remaining space either side of the element
                float child_size = accumulate_child_axis(&children[i].sizing.width);
                
                // Only add the min-width if desired isnt defined yet 
                if(children[i].sizing.width.desired.type == MeasurementType::GROW)
                {
                    child_size += children[i].sizing.width.current;
                }
                float growth_space = (p_width - child_size) / grow_width_meaure_count;
                growth_space = MAX(growth_space, 0.0f);
                grow_width_meaure_count = 0; // Reset so other element can know how many growth measurements they have
                
                grow_child(&children[i].sizing.width, growth_space);
            }
        }
        if(children[i].sizing.height.desired.type == MeasurementType::GROW || children[i].sizing.height.margin1.type == MeasurementType::GROW || children[i].sizing.height.margin2.type == MeasurementType::GROW)
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
            if(dir == LayoutDirection::VERTICAL && children[i].display != DisplayType::RELATIONAL && children[i].display != DisplayType::MANUAL)
            {
                if(children[i].sizing.height.desired.type == MeasurementType::GROW)
                {
                    p_height_accumulated += children[i].sizing.height.current;
                }
                revisit_height_elements[revisit_height_element_count] = &children[i];
                revisit_height_element_count++;
            }
            else // In a horizontal layout we can immediately grow height measures
            {
                assert(dir == LayoutDirection::HORIZONTAL || children[i].display == DisplayType::RELATIONAL || children[i].display == DisplayType::MANUAL);
                
                // height-grow space in a horizontal layout is the remaining space above/below the element
                float child_size = accumulate_child_axis(&children[i].sizing.height);
                if(children[i].sizing.height.desired.type == MeasurementType::GROW)
                {
                    child_size += children[i].sizing.height.current;
                }
                float growth_space = (p_height - child_size) / grow_height_meaure_count;
                growth_space = MAX(growth_space, 0.0f);
                grow_height_meaure_count = 0; // Reset so other element can know how many growth measurements they have
                
                grow_child(&children[i].sizing.height, growth_space);
            }
        }
    }
    
    
    if(grow_width_meaure_count)
    {
        // Note(Leo): Since the required min size of growth elements is included in the p_width_accumulated this is the 
        //            extra leftover space. IF this is less than zero that indicates an overflow of the parent.
        float growth_space = p_width - p_width_accumulated;
        
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
        float growth_space = p_height - p_height_accumulated;
        
        // Divying up the extra space if there is any
        growth_space = MAX(growth_space / grow_height_meaure_count, 0.0f);
                   
        for(int i = 0; i < revisit_height_element_count; i++)
        {
            grow_child(&revisit_height_elements[i]->sizing.height, growth_space);
        } 
    }
    
    // Note(Leo): To properly calculate scrollable elements we need the size of the parents contents.
    parent->sizing.width.current = MAX(MAX(p_width_accumulated, parent->sizing.width.current), parent->sizing.width.desired.size);
    parent->sizing.height.current = MAX(MAX(p_height_accumulated, parent->sizing.height.current), parent->sizing.height.desired.size);
    
    DeAllocScratch(revisit_width_elements);
    DeAllocScratch(revisit_height_elements);
    
}

void final_place_image(shaping_context* context, LayoutElement* image)
{
    LoadedImageHandle* handle = image->IMAGE.handle;
    float horizontal_scale = image->sizing.width.desired.size / (float)handle->image_width;
    float vertical_scale = image->sizing.height.desired.size / (float)handle->image_height;
    float base_x = image->position.x;
    float base_y = image->position.y;

    
    RenderPlatformImageTile* curr_tile = handle->first_tile;
    while(curr_tile)
    {
        // The instance bounds are the actual screen coordinates of each corner of the bounding box
        bounding_box tile_bounds = { ((float)curr_tile->image_offsets.x * horizontal_scale) + base_x, ((float)curr_tile->image_offsets.y * vertical_scale) + base_y, (float)curr_tile->content_width * horizontal_scale, (float)curr_tile->content_height * vertical_scale };
        
        boxes_intersect(&image->bounds, &tile_bounds, &tile_bounds);
        
        vec4 bounds = { tile_bounds.x, tile_bounds.y, tile_bounds.x + tile_bounds.width, tile_bounds.y + tile_bounds.height };
        combined_instance* created = (combined_instance*)Alloc(context->final_renderque, sizeof(combined_instance));
        memcpy(&created->bounds, &bounds, sizeof(vec4));
                
        static_assert(sizeof(Corners) == sizeof(vec4));
        memcpy(&created->corners, &image->IMAGE.corners, sizeof(Corners));
        
        /*
        created->shape_position = { ((float)curr_tile->image_offsets.x * horizontal_scale) + base_x, ((float)curr_tile->image_offsets.y * vertical_scale) + base_y };
        created->shape_size = { (float)curr_tile->content_width * horizontal_scale, (float)curr_tile->content_height * vertical_scale };
        */
        created->shape_position = { base_x, base_y };
        created->shape_size = { image->sizing.width.desired.size, image->sizing.height.desired.size };
        
        created->sample_position = { (float)curr_tile->atlas_offsets.x - (float)curr_tile->image_offsets.x, (float)curr_tile->atlas_offsets.y - (float)curr_tile->image_offsets.y, (float)curr_tile->atlas_offsets.z };
        created->sample_size = { (float)handle->image_width, (float)handle->image_height };
        
        created->type = (int)CombinedInstanceType::IMAGE_TILE;
        
        curr_tile = curr_tile->next;
    }
}

void final_place_text(shaping_context* context, LayoutElement* combined_text)
{
    float base_x = combined_text->position.x;
    float base_y = combined_text->position.y;
    
    for(uint32_t i = 0; i < combined_text->TEXT_COMBINED.glyph_count; i++)
    {
        FontPlatformShapedGlyph* curr_glyph = combined_text->TEXT_COMBINED.first_glyph + i;
        
        bounding_box adjusted_bounds = { base_x + curr_glyph->placement_offsets.x, base_y + curr_glyph->placement_offsets.y, curr_glyph->placement_size.x, curr_glyph->placement_size.y };
     
        // Skip hidden glyphs
        if(!boxes_intersect(&combined_text->bounds, &adjusted_bounds, &adjusted_bounds))
        {
            continue;
        }
        
        combined_instance* created = (combined_instance*)Alloc(context->final_renderque, sizeof(combined_instance));
        
        // The instance bounds are the actual screen coordinates of each corner of the bounding box
        vec4 bounds = { adjusted_bounds.x, adjusted_bounds.y, adjusted_bounds.x + adjusted_bounds.width, adjusted_bounds.y + adjusted_bounds.height };
        memcpy(&created->bounds, &bounds, sizeof(vec4));
        
        static_assert(sizeof(StyleColor) == sizeof(vec4));
        // Note(Leo): Color gets put into the corners variable for glyphs since they dont have corners.
        memcpy(&created->corners, &curr_glyph->color, sizeof(StyleColor));
        
        created->shape_position = { base_x + curr_glyph->placement_offsets.x, base_y + curr_glyph->placement_offsets.y };
        memcpy(&created->shape_size, &curr_glyph->placement_size, sizeof(vec2));
        
        memcpy(&created->sample_position, &curr_glyph->atlas_offsets, sizeof(vec3));
        memcpy(&created->sample_size, &curr_glyph->atlas_size, sizeof(vec2));
               
        created->type = (int)CombinedInstanceType::GLYPH;
    }
}


void final_place_element(shaping_context* context, LayoutElement* element)
{
    combined_instance* created = (combined_instance*)Alloc(context->final_renderque, sizeof(combined_instance));
    vec4 bounds = { element->bounds.x, element->bounds.y, element->bounds.x + element->bounds.width, element->bounds.y + element->bounds.height };
    memcpy(&created->bounds, &bounds, sizeof(vec4));
    
    memcpy(&created->corners, &element->NORMAL.corners, sizeof(Corners));
    
    // Note(Leo): Color gets put into the sample_position variable since shapes dont sample anything.
    created->sample_position = { element->NORMAL.color.r, element->NORMAL.color.g, element->NORMAL.color.b };
    created->sample_size = { element->NORMAL.color.a, 0.0f };
        
    memcpy(&created->shape_position, &element->position, sizeof(vec2));
    created->shape_size = { element->sizing.width.desired.size, element->sizing.height.desired.size };
    created->type = (int)CombinedInstanceType::NORMAL;
}

// Note(Leo): The root element should have the screen size as its width/height and the measures should be pixels
Arena* ShapingPlatformShape(Element* root_element, Arena* shape_arena, int element_count, int window_width, int window_height)
{
    shaping_context context = {};
    context.element_count = (uint32_t)element_count;
    
    shape_arena->alloc_size = sizeof(LayoutElement);
    
    // Note(Leo): The unpacking behaviour depends on shape_arena being empty.
    assert(shape_arena && shape_arena->next_address == shape_arena->mapped_address);
    
    void* memory_block = Alloc(shape_arena, element_count*sizeof(LayoutElement));
    
    // Note(Leo): Give layout elements their own arena so that when we shape text inbetween unpacks the two things arent mixed
    Arena layout_element_arena = CreateArenaWith(memory_block, element_count*sizeof(LayoutElement), sizeof(LayoutElement));
    
    context.shape_arena = shape_arena;
    context.layout_element_arena = &layout_element_arena;

    // Note(Leo): Root has to have its sizes set as pixels before going into the main loop
    root_element->working_style.width.type = MeasurementType::PIXELS;
    root_element->working_style.width.size = (float)window_width;
    
    root_element->working_style.height.type = MeasurementType::PIXELS;
    root_element->working_style.height.size = (float)window_height;
    
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
    // We also add the number of children that root had onto the unpacked count which serves as a running tally of how many
    // children weve allocated and converted but have not called unpack on. We continue doing this until we have unpacked
    // all children.
    while(unpacked_count)
    {
        // Note(Leo): This relies on elements being indexed into the elements arena by their id
        Element* unpack_parent = root_element + curr_element->element_id;
        
        unpack(&context, unpack_parent->first_child, &(curr_element->children), &(curr_element->child_count));
        unpacked_count += curr_element->child_count;
        
        BEGIN_TIMED_BLOCK(FIRST_PASS);
        shape_first_pass(&context, curr_element);
        END_TIMED_BLOCK(FIRST_PASS);
        
        if(curr_element->display == DisplayType::RELATIONAL)
        {
            context.relative_element_count++;
        }
        else if(curr_element->display == DisplayType::MANUAL)
        {
            context.manual_element_count++;
        }
        
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
            BEGIN_TIMED_BLOCK(SECOND_PASS);
            shape_second_pass(&context, curr_element);
            END_TIMED_BLOCK(SECOND_PASS);
        }
        curr_element--;
    }
    
    // Note(Leo): Leave space for alignment
    void* final_pass_memory = Alloc(context.shape_arena, (element_count + 1)*sizeof(LayoutElement*));
    Arena final_pass_visit = CreateArenaWith(align_mem(final_pass_memory, LayoutElement*), element_count*sizeof(LayoutElement*), sizeof(LayoutElement*));

    // Note(Leo): Due to the nature of us adding items to the renderque in a breadth first manner there is an interweave issue
    //            with elements that use relative or manual display. Elements have their subtrees draw calls intermixed
    //            with the draws of their sibling's subtrees which is fine usually since elements dont overlap, accept they can
    //            when manual/relative are used. The deffered que fixes this by allowing all the manual element's siblings to have
    //            fully drawn their subtrees before drawing its own which fixes the weaving issue. 
    void* relative_pass_memory = Alloc(context.shape_arena, (context.relative_element_count + 1)*sizeof(LayoutElement*));
    Arena relative_pass_visit = CreateArenaWith(align_mem(relative_pass_memory, LayoutElement*), context.relative_element_count*sizeof(LayoutElement*), sizeof(LayoutElement*));
    LayoutElement** relative_elements = (LayoutElement**)relative_pass_visit.mapped_address; 
    
    void* manual_pass_memory = Alloc(context.shape_arena, (context.manual_element_count + 1)*sizeof(LayoutElement*));
    Arena manual_pass_visit = CreateArenaWith(align_mem(manual_pass_memory, LayoutElement*), context.manual_element_count*sizeof(LayoutElement*), sizeof(LayoutElement*));
    LayoutElement** manual_elements = (LayoutElement**)manual_pass_visit.mapped_address; 
    
    // Iterating forward from the root element in a breadth first way.    
    // We shape all the elements of root then only push the ones inside of root's bounding box onto the stack of children.
    // Each time we visit a child we shape all of its elements and cull based on the bouding box of the child. 
    // This helps with not visiting elements that arent visible
    LayoutElement** visit_elements = (LayoutElement**)Push(&final_pass_visit, sizeof(LayoutElement*));
    *visit_elements = (LayoutElement*)context.layout_element_arena->mapped_address; // Grabbing the root element
    
    curr_element = *visit_elements;
    uint32_t visit_count = 1;
    uint32_t deferred_relative_count = 0;
    uint32_t deferred_manual_count = 0;
    
    // Setup the bounds for the root
    curr_element->bounds.x = 0;
    curr_element->bounds.y = 0;
    
    curr_element->bounds.width = curr_element->sizing.width.desired.size;
    curr_element->bounds.height = curr_element->sizing.height.desired.size;
    
    // +1 to leave space for alignment
    // Note(Leo): This doesnt take culling into account so it will over-estimate the actual # of renderque objects
    int renderque_length = (context.element_count + context.glyph_count + context.image_tile_count + 1);
    void* final_renderque_memory = Alloc(context.shape_arena, renderque_length*sizeof(combined_instance));
    
    Arena* final_renderque = (Arena*)align_mem(Alloc(context.shape_arena, 2*sizeof(Arena)), Arena); // +1 for alignment
    *final_renderque = CreateArenaWith(align_mem(final_renderque_memory, combined_instance), renderque_length*sizeof(combined_instance), sizeof(combined_instance));
    context.final_renderque = final_renderque;
    
    while(visit_count || deferred_relative_count || deferred_manual_count)
    {
        // Note(Leo): Only start grabbing from the deferred relative que once our visit que is exhausted, once deferred
        //            relative is exhausted start grabbing from the manual que.
        if(visit_count)
        {
            visit_count--;
            curr_element = *visit_elements;
            visit_elements++;
        }
        else if(deferred_relative_count)
        {
            deferred_relative_count--;
            curr_element = *relative_elements;
            relative_elements++;
        }
        else
        {
            deferred_manual_count--;
            curr_element = *manual_elements;
            manual_elements++;
        }
        
        BEGIN_TIMED_BLOCK(FINAL_PASS);
        shape_final_pass(&context, curr_element);
        END_TIMED_BLOCK(FINAL_PASS);
        
        switch(curr_element->type)
        {
            case(LayoutElementType::NORMAL):
            {
                final_place_element(&context, curr_element);
                break;
            }
            case(LayoutElementType::TEXT_COMBINED):
            {
                final_place_text(&context, curr_element);
                break;
            }
            case(LayoutElementType::IMAGE):
            {
                final_place_image(&context, curr_element);
                break;
            }
        }
        
        LayoutDirection dir = curr_element->dir;
        
        float inner_width = curr_element->sizing.width.desired.size;
        float inner_height = curr_element->sizing.height.desired.size;

        inner_width -= curr_element->sizing.width.padding1.size;
        inner_width -= curr_element->sizing.width.padding2.size;

        inner_height -= curr_element->sizing.height.padding1.size;
        inner_height -= curr_element->sizing.height.padding2.size;
        
        bounding_box inner_bounds = {};
        
        inner_bounds.x = curr_element->position.x + curr_element->sizing.width.padding1.size;
        inner_bounds.y = curr_element->position.y + curr_element->sizing.height.padding1.size;        
        
        inner_bounds.width = inner_width;
        inner_bounds.height = inner_height;
        
        float cursor_x = inner_bounds.x;
        float cursor_y = inner_bounds.y;
        
        // Note(Leo): bounds of the parent element is the section of it which was visible inside of its parent.
        //            inner bounds is the inner capsule of the parent (parent size minus padding) some of which may 
        //            not be visible. The area of overlap between the bounds and inner bounds is the actual visible area
        //            of the parent where children can be (children cant be inside padding). 
        boxes_intersect(&curr_element->bounds, &inner_bounds, &inner_bounds);
        
        // Transforming the children from parent-space to screenspace
        if(curr_element->type == LayoutElementType::NORMAL)
        {
            cursor_x -= curr_element->NORMAL.clipping.left_scroll;
            cursor_y -= curr_element->NORMAL.clipping.top_scroll;
        }
        
        for(int i = 0; i < curr_element->child_count; i++)
        {
            if(curr_element->children[i].display == DisplayType::RELATIONAL)
            {
                curr_element->children[i].position.x = curr_element->position.x + curr_element->sizing.width.padding1.size + curr_element->children[i].sizing.width.margin1.size;
                curr_element->children[i].position.y = curr_element->position.y + curr_element->sizing.height.padding1.size + curr_element->children[i].sizing.height.margin1.size;
            }
            else if(curr_element->children[i].display == DisplayType::MANUAL)
            {
                curr_element->children[i].position.x = curr_element->children[i].sizing.width.margin1.size;
                curr_element->children[i].position.y = curr_element->children[i].sizing.height.margin1.size;
            }
            else
            {
                cursor_x += curr_element->children[i].sizing.width.margin1.size;
                cursor_y += curr_element->children[i].sizing.height.margin1.size;
                
                curr_element->children[i].position.x = cursor_x;
                curr_element->children[i].position.y = cursor_y;
            }

            curr_element->children[i].bounds.x = curr_element->children[i].position.x;
            curr_element->children[i].bounds.y = curr_element->children[i].position.y;
            curr_element->children[i].bounds.width = curr_element->children[i].sizing.width.desired.size;
            curr_element->children[i].bounds.height = curr_element->children[i].sizing.height.desired.size;
 
            // Note(Leo): boxes_intersect puts the intersection region as the new bounding box of the child
            // Only add elements to be visited if they will be visisble
            if(boxes_intersect(&inner_bounds, &curr_element->children[i].bounds, &curr_element->children[i].bounds))
            {
                if(curr_element->children[i].display == DisplayType::RELATIONAL)
                {
                    LayoutElement** added_deferred = (LayoutElement**)Push(&relative_pass_visit, sizeof(LayoutElement*));
                    *added_deferred = &curr_element->children[i];
                    deferred_relative_count++;   
                }
                else if(curr_element->children[i].display == DisplayType::MANUAL)
                {                
                    LayoutElement** added_deferred = (LayoutElement**)Push(&manual_pass_visit, sizeof(LayoutElement*));
                    *added_deferred = &curr_element->children[i];
                    deferred_manual_count++;   
                }
                else
                {
                    LayoutElement** added_visit = (LayoutElement**)Push(&final_pass_visit, sizeof(LayoutElement*));
                    *added_visit = &curr_element->children[i];
                    visit_count++;
                }
            }
            
            if(curr_element->children[i].display == DisplayType::RELATIONAL || curr_element->children[i].display == DisplayType::MANUAL)
            {
            
            }
            else if(dir == LayoutDirection::HORIZONTAL)
            {
                // Need to move x over for the next element
                cursor_x += curr_element->children[i].sizing.width.desired.size;
                cursor_x += curr_element->children[i].sizing.width.margin2.size;
                
                // Need to reset the y back to what it was before
                cursor_y -= curr_element->children[i].sizing.height.margin1.size;
            }
            else
            {
                assert(dir == LayoutDirection::VERTICAL);
                
                // Need to move y over for the next element
                cursor_y += curr_element->children[i].sizing.height.desired.size;
                cursor_y += curr_element->children[i].sizing.height.margin2.size;
                
                // Need to reset the x back to what it was before
                cursor_x -= curr_element->children[i].sizing.width.margin1.size;
            }
        }
        
    }
    
    return context.final_renderque;
}

// Does a mini version of what the first pass does with sibling text elements. Shapes into the given arena and returns the
// combined text element.
void PlatformPreviewText(Arena* shape_arena, Element* first_text, Measurement width, Measurement height)
{
    assert(shape_arena && first_text);
    if(!shape_arena || !first_text)
    {
        return;
    }
    
    // Note(Leo): Align arena just incase
    shape_arena->next_address = (uintptr_t)align_mem(shape_arena->next_address, LayoutElement);
    
    shaping_context temp_context = {};
    
    temp_context.shape_arena = shape_arena;
    
    // Note(Leo): This is okay to do since we only shape one set of child elements so we dont get the issue of glyphs
    //            being allocated in between layout elements
    temp_context.layout_element_arena = shape_arena;
    
    LayoutElement* fake_parent = (LayoutElement*)Alloc(shape_arena, sizeof(LayoutElement), zero());
    fake_parent->sizing.width.desired = width;
    fake_parent->sizing.height.desired = height;
    fake_parent->dir = LayoutDirection::VERTICAL;
    
    unpack(&temp_context, first_text, &(fake_parent->children), &(fake_parent->child_count));
    
    shape_first_pass(&temp_context, fake_parent);
    
    first_text->last_sizing = fake_parent->children;
}