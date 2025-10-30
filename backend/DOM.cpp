#include "DOM.h"
#include "dom_attatchment.h"
#include <cassert>

// Initialize the DOM
void InitDOM(Arena* master_arena, DOM* target)
{
    target->static_cstrings = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->cached_cstrings = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->dynamic_cstrings = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->strings = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->pointer_arrays = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->elements = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->attributes = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->frame_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->events = (Arena*)Alloc(master_arena, sizeof(Arena));
    
    *(target->static_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->cached_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->dynamic_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->strings) = CreateArena(sizeof(StringBlock)*1000000, sizeof(StringBlock));
    *(target->pointer_arrays) = CreateArena(sizeof(LinkedPointer)*10000, sizeof(LinkedPointer));
    *(target->elements) = CreateArena(sizeof(Element)*1000000, sizeof(Element));
    *(target->attributes) = CreateArena(sizeof(Attribute)*200000, sizeof(Attribute));
    *(target->frame_arena) = CreateArena(sizeof(char)*10000000, sizeof(char));
    *(target->events) = CreateArena(sizeof(Event)*1000000, sizeof(Event));
    
    target->max_events = 1000000;
}

#define bound_expr(expr_context, fn_type, expr_type, union_name)                                                  \
BoundExpression* register_bound_expr(fn_type fn, int id)                                            \
{                                                                                                   \
    BoundExpression* created = (BoundExpression*)runtime.bound_expressions->mapped_address + id;    \
    if(runtime.bound_expressions->next_address <= (uintptr_t)created)                               \
    {                                                                                               \
        uintptr_t endpoint = (uintptr_t)created + sizeof(BoundExpression);                          \
        Alloc(runtime.bound_expressions, endpoint - runtime.bound_expressions->mapped_address);     \
    }                                                                                               \
    created->expression_id = id;                                                                    \
    created->type = expr_type ;                                                                     \
    created->union_name = fn;                                                                       \
    created->context = expr_context;                                                                \
    return created;                                                                                 \
}

bound_expr(BindingContext::GLOBAL, SubscribedStubVoid, BoundExpressionType::VOID_RET, stub_void);
bound_expr(BindingContext::GLOBAL, SubscribedStubVoidBool, BoundExpressionType::VOID_BOOL_RET, stub_void_bool);
bound_expr(BindingContext::GLOBAL, SubscribedStubString, BoundExpressionType::ARENA_STRING, stub_string);
bound_expr(BindingContext::GLOBAL, SubscribedStubPointer, BoundExpressionType::VOID_PTR, stub_ptr);
bound_expr(BindingContext::GLOBAL, SubscribedStubBool, BoundExpressionType::BOOL_RET, stub_bool);
bound_expr(BindingContext::GLOBAL, SubscribedStubGetPointer, BoundExpressionType::PTR_RET, stub_get_ptr);
bound_expr(BindingContext::GLOBAL, SubscribedStubInt, BoundExpressionType::INT_RET, stub_int);
bound_expr(BindingContext::GLOBAL, SubscribedStubArgs, BoundExpressionType::ARG_RET, stub_args);

bound_expr(BindingContext::LOCAL, ArrSubscribedStubVoid, BoundExpressionType::VOID_RET, arr_stub_void);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubVoidBool, BoundExpressionType::VOID_BOOL_RET, arr_stub_void_bool);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubString, BoundExpressionType::ARENA_STRING, arr_stub_string);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubPointer, BoundExpressionType::VOID_PTR, arr_stub_ptr);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubBool, BoundExpressionType::BOOL_RET, arr_stub_bool);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubGetPointer, BoundExpressionType::PTR_RET, arr_stub_get_ptr);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubInt, BoundExpressionType::INT_RET, arr_stub_int);
bound_expr(BindingContext::LOCAL, ArrSubscribedStubArgs, BoundExpressionType::ARG_RET, arr_stub_args);

// Note(Leo): Expressions are stored in the expression arena as an array with their id as their index
BoundExpression* GetBoundExpression(int id)
{
    assert(id > 0);
    assert(runtime.bound_expressions->mapped_address);
    return (BoundExpression*)runtime.bound_expressions->mapped_address + id;
}

Attribute* GetAttribute(Element* element, AttributeType searched_type)
{
    Attribute* curr = element->first_attribute;
    while(curr)
    {
        if(curr->type == searched_type)
        {
            return curr;
        }
        curr = curr->next_attribute;
    }
    
    return NULL;
}


void* AllocPage(DOM* dom, int size, int file_id)
{
    // Note(Leo): Need to use FreeSubtreeObjects to ensure that this malloc'ed memory gets freed
    void* allocated = malloc(size);
    
    assert(allocated);
    
    memset(allocated, 0, size);
    return allocated;
}


void* AllocComponent(DOM* dom, int size, int file_id)
{
    // Note(Leo): Need to use FreeSubtreeObjects to ensure that this malloc'ed memory gets freed
    void* allocated = malloc(size);
    memset(allocated, 0, size);
    return allocated;
}


void DefaultStyle(InFlightStyle* target)
{
    #define DEFAULT_PRIORITY -1
    target->wrapping = TextWrapping::WORDS;
    target->wrapping_p = DEFAULT_PRIORITY;

    target->horizontal_clipping = ClipStyle::HIDDEN;
    target->horizontal_clipping_p = DEFAULT_PRIORITY;
    target->vertical_clipping = ClipStyle::HIDDEN;
    target->vertical_clipping_p = DEFAULT_PRIORITY;
    
    target->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    target->color_p = DEFAULT_PRIORITY;
    
    target->text_color = { 0.0f, 0.0f, 0.0f, 1.0f };
    target->text_color_p = DEFAULT_PRIORITY;
    
    target->display = DisplayType::NORMAL;
    target->display_p = DEFAULT_PRIORITY;
    
    target->width.type = MeasurementType::GROW;
    target->width_p = DEFAULT_PRIORITY;
    
    target->min_width.type = MeasurementType::PIXELS;
    target->min_width.size = 0.0f;
    target->min_width_p = DEFAULT_PRIORITY;
    
    target->max_width.type = MeasurementType::PIXELS;
    target->max_width.size = 0.0f;
    target->max_width_p = DEFAULT_PRIORITY;
    
    target->height.type = MeasurementType::GROW;
    target->height_p = DEFAULT_PRIORITY;

    target->min_height.type = MeasurementType::PIXELS;
    target->min_height.size = 0.0f;
    target->min_height_p = DEFAULT_PRIORITY;
    
    target->max_height.type = MeasurementType::PIXELS;
    target->max_height.size = 0.0f;
    target->max_height_p = DEFAULT_PRIORITY;
    
    target->margin.m[0] = {0.0f, MeasurementType::PIXELS};
    target->margin.m[1] = {0.0f, MeasurementType::PIXELS};
    target->margin.m[2] = {0.0f, MeasurementType::PIXELS};
    target->margin.m[3] = {0.0f, MeasurementType::PIXELS};
    target->margin_p = DEFAULT_PRIORITY;

    target->padding.m[0] = {0.0f, MeasurementType::PIXELS};
    target->padding.m[1] = {0.0f, MeasurementType::PIXELS};
    target->padding.m[2] = {0.0f, MeasurementType::PIXELS};
    target->padding.m[3] = {0.0f, MeasurementType::PIXELS};
    target->padding_p = DEFAULT_PRIORITY;
    
    target->corners = {0.0f, 0.0f, 0.0f, 0.0f};
    target->corners_p = DEFAULT_PRIORITY;
    
    target->font_size = 90;
    target->font_size_p = DEFAULT_PRIORITY;
    
    target->font_id = 1;
    target->font_id_p = DEFAULT_PRIORITY;
}

Attribute* convert_saved_attribute(DOM* dom, Compiler::Attribute* converted_attribute)
{
    Attribute* added = (Attribute*)Alloc(dom->attributes, sizeof(Attribute), zero());
    added->type = (AttributeType)((int)converted_attribute->type);
    
    switch(added->type)
    {
        case(AttributeType::ON_CLICK):
        {
            added->OnClick.binding_id = converted_attribute->OnClick.binding_id;
            break;
        }
        case(AttributeType::ON_FOCUS):
        {
            added->OnFocus.binding_id = converted_attribute->OnFocus.binding_id;
            break;
        }
        case(AttributeType::COMP_ID):
        {
            added->CompId.id = converted_attribute->CompId.id;
            break;
        }
        case(AttributeType::CUSTOM):
        {
            added->Args.binding_id = converted_attribute->Args.binding_id;
            break;
        }
        case(AttributeType::LOOP):
        {
            added->Loop.array_binding = converted_attribute->Loop.array_binding;
            added->Loop.length_binding = converted_attribute->Loop.length_binding;
            added->Loop.template_id = converted_attribute->Loop.template_id;
            break;
        }
        case(AttributeType::THIS_ELEMENT):
        {
            added->This.binding_id = converted_attribute->This.binding_id;
            break;
        }
        case(AttributeType::FOCUSABLE):
        {
            break;
        }
        default:
            text_like:
            added->Text.binding_id = converted_attribute->Text.binding_id;
            added->Text.static_value = converted_attribute->Text.value;
            added->Text.binding_position = converted_attribute->Text.binding_position;
            added->Text.value_length = converted_attribute->Text.value_length;
            break;
    }
    
    return added;
}

bool CheckElementValid(Element* el)
{
    if(!el)
    {
        return false;
    }
    
    if(el == el->parent || el == el->next_sibling || el == el->first_child)
    {
        return false;
    }
    
    Attribute* curr = el->first_attribute;
    
    while(curr)
    {
        if(curr == curr->next_attribute)
        {
            return false;
        }
        curr = curr->next_attribute;
    }
    
    return true;
}

Element* tag_to_element(DOM* dom, Arena* element_arena, Compiler::Tag* converted_tag, Element* target_element = NULL)
{
    Element* added = target_element;
    if(!added)
    {
        added = (Element*)Alloc(element_arena, sizeof(Element), zero());
    }
    
    added->global_id = converted_tag->global_id;
    added->num_attributes = converted_tag->num_attributes;
    added->type = (ElementType)((int)converted_tag->type);
    
    // Setting default style attributes. 
    // Note(Leo): We could probably just setup a const/global instance which gets memcpy'd instead of creating the default 
    //            style for every element but it might not be much faster anyway...
    DefaultStyle(&added->working_style);
    DefaultStyle(&added->override_style);
    
    Attribute* prev_added_attribute = NULL;
    Attribute* curr_added_attribute; 
    
    for(int i = 0; i < added->num_attributes; i++)
    {
        curr_added_attribute = convert_saved_attribute(dom, converted_tag->first_attribute + i);
        if(prev_added_attribute)
        {
            prev_added_attribute->next_attribute = curr_added_attribute;
        }
        else // This is the first attribute
        {
            added->first_attribute = curr_added_attribute;
        }
        
        prev_added_attribute = curr_added_attribute;
        
    }
    
    assert(CheckElementValid(added));
    
    return added;
}

void* InstancePage(DOM* target_dom, int id)
{
    // Note(Leo): Get the difference between the old base and target in indexes and apply that to the new base and pointer type
    #define get_pointer(old_base_ptr, new_base_ptr, target_ptr) (target_ptr ? ((new_base_ptr) + (uint64_t)((decltype(old_base_ptr))target_ptr - old_base_ptr)) : 0)
    
    LoadedFileHandle* page_bin = GetFileFromId(id);
    
    if(!page_bin)
    {   
        printf("Page binary could not be found!\n");
        assert(page_bin);
    }
    
    // Allocate and setup page object
    void* created_page;
    call_page_main(target_dom, page_bin->file_id, &created_page);
    
    // Note(Leo): All pages should inherit the Page struct so that they have the file_id member
    Page* page_obj = (Page*)created_page;
    page_obj->file_id = page_bin->file_id;
    page_obj->master_dom = target_dom;
    
    Compiler::Tag* curr = page_bin->root_tag;
    
    Element* added;
    
    // Note(Leo): Should already be aligned to Element requirements
    // Using an arena to reserve all the required contiguous slots for elements in this page.
    // Required since otherwise instance component will interfere with our pointers by allocing components
    void* page_elements_mem = Alloc(target_dom->elements, page_bin->file_info.tag_count*sizeof(Element));
    Arena page_elements = CreateArenaWith(page_elements_mem, page_bin->file_info.tag_count*sizeof(Element), sizeof(Element));
    
    Compiler::Tag* tag_base = (Compiler::Tag*)page_bin->root_tag;
    Element* element_base = (Element*)page_elements_mem;
    
    // Adding root element
    added = tag_to_element(target_dom, &page_elements, curr);
    added->parent = NULL;
    added->next_sibling = NULL;
    added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
    added->master = created_page;
    curr++;
    
    while(curr->tag_id)
    {
        added = tag_to_element(target_dom, &page_elements, curr);
        added->parent = (Element*)get_pointer(tag_base, element_base, curr->parent);;
        added->next_sibling = (Element*)get_pointer(tag_base, element_base, curr->next_sibling);
        added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
        
        added->master = created_page;
        
        added->id = index_of(added, target_dom->elements->mapped_address, Element);
                
        // If the element being added is a component, instance it
        if(curr->type == Compiler::TagType::CUSTOM)
        {
            Attribute* comp_specifier = GetAttribute(added, AttributeType::COMP_ID);
            
            // Comp element must be corrupted/uninitialized if it doesnt have a specifier for file id
            assert(comp_specifier);
            // Must specify a comp id
            assert(comp_specifier->CompId.id);
                        
            if(comp_specifier)
            {
                InstanceComponent(target_dom, added, comp_specifier->CompId.id);
            }
            
        }
        
        curr++;
    }
    
    return created_page;
}

void* InstanceComponent(DOM* target_dom, Element* parent, int id)
{   
    LoadedFileHandle* comp_bin =  GetFileFromId(id);
    
    if(!comp_bin)
    {   
        printf("Comp binary could not be found!\n");
        assert(comp_bin);
    }
    
    // Allocate and setup component
    void* added_comp = NULL;
    
    // Check for an args binding
    Attribute* args_attribute = GetAttribute(parent, AttributeType::CUSTOM);
    
    CustomArgs args = {};
    
    if(args_attribute)
    {
        BoundExpression* binding = GetBoundExpression(args_attribute->Args.binding_id);
        assert(binding->type == BoundExpressionType::ARG_RET);
        assert(parent->master);
        
        if(binding->context == BindingContext::GLOBAL)
        {
            binding->stub_args((void*)parent->master, &args);
        }
        else
        {
            binding->arr_stub_args((void*)parent->context_master, (void*)parent->master, parent->context_index, &args);
        }
        
    }

    call_comp_main(target_dom, comp_bin->file_id, &added_comp, &args);
    
    // Note(Leo): All components should inherit the Component struct so that they have the file_id member
    Component* comp_obj = (Component*)added_comp;
    comp_obj->file_id = comp_bin->file_id;
    comp_obj->master_dom = target_dom;
    comp_obj->custom_element = parent;
    
    // Allocate the element lookup array and align it so we can store pointers
    // Note(Leo): + 1 since we use the element's id to index into this array and element ids start at 1 and + 1 to leave alignment room
    void* element_addresses_unaligned = AllocScratch((comp_bin->file_info.tag_count + 2) * sizeof(Element*), zero());
    void** element_addresses = (void**)align_ptr(element_addresses_unaligned);
    
    // Note(Leo): We use element id to index into this array, since id's are local to the file we know that
    //  they are in the range 1 to num_tags + 1
    
    Compiler::Tag* curr = comp_bin->root_tag;
    
    // Adding root element
    Element* added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
    element_addresses[curr->tag_id] = (void*)added;
    tag_to_element(target_dom, target_dom->elements, curr, added);
    added->master = added_comp;
    added->context_master = parent->context_master;
    added->context_index = parent->context_index;
    
    added->id = index_of(added, target_dom->elements->mapped_address, Element);
    
    // Note(Leo): Parent is the Custom element type that calls this instance-ing and so it should have no existing children
    assert(!parent->first_child);
    parent->first_child = added;
    
    added->parent = parent;
    added->next_sibling = NULL;
    
    // Pre allocate an address for the first child element of root
    element_addresses[curr->first_child->tag_id] = (void*)Alloc(target_dom->elements, sizeof(Element), zero());
    if(curr->first_child)
    {
        added->first_child = (Element*)(element_addresses[curr->first_child->tag_id]); 
    }
    
    // Note(Leo): Move off of root since it has id == 0 which would break the loop 
    curr++;
    
    // Allocates an element for the given tag id and puts the pointer in element_addresses 
    #define PushElement(tag_id) element_addresses[tag_id] = Alloc(target_dom->elements, sizeof(Element), zero())
    
    while(curr->tag_id)
    {
        // An address has already been allocated for this element
        if(element_addresses[curr->tag_id])
        {
            added = (Element*)element_addresses[curr->tag_id];
        }
        // Element has not had an address allocated yet, allocate one
        else
        {
            added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
            element_addresses[curr->tag_id] = (void*)added;
        }
        
        tag_to_element(target_dom, target_dom->elements, curr, added);
        added->parent = (Element*)element_addresses[curr->parent->tag_id];
        added->id = index_of(added, target_dom->elements->mapped_address, Element);
        
        if(curr->next_sibling)
        {
            // Note(Leo): Allocate the element in advance if it doenst exist
            if(!element_addresses[curr->next_sibling->tag_id])
            {
                PushElement(curr->next_sibling->tag_id);
            }
            added->next_sibling = (Element*)element_addresses[curr->next_sibling->tag_id];
        }
        if(curr->first_child)
        {
            // Note(Leo): Allocate the element in advance if it doenst exist
            if(!element_addresses[curr->first_child->tag_id])
            {
                PushElement(curr->first_child->tag_id);
            }
            added->first_child = (Element*)element_addresses[curr->first_child->tag_id];
        }
        
        added->master = added_comp;
        added->context_master = parent->context_master;
        added->context_index = parent->context_index;
        
        // If the element being added is a component, instance it
        if(curr->type == Compiler::TagType::CUSTOM)
        {
            Attribute* comp_specifier = GetAttribute(added, AttributeType::COMP_ID);
            
            // Comp element must be corrupted/uninitialized if it doesnt have a specifier for file id
            assert(comp_specifier);
            // Must specify a comp id
            assert(comp_specifier->CompId.id);
            
            if(comp_specifier)
            {
                InstanceComponent(target_dom, added, comp_specifier->CompId.id);
            }
            
        }
        
        curr++;
    }
    
    DeAllocScratch(element_addresses_unaligned);
    
    return added_comp;
}

// Note(Leo): Templates are stored in the templates arena with their id as their index + 1
BodyTemplate* GetTemplate(int id)
{
    assert(id > 0);
    assert(runtime.loaded_templates->mapped_address);
    return (BodyTemplate*)runtime.loaded_templates->mapped_address + (id - 1);
}

void InstanceTemplate(DOM* target_dom, Element* parent, void* array_ptr, int template_id, int index)
{
    BodyTemplate* used_template = GetTemplate(template_id);
    
    // Allocate the element lookup array and align it so we can store pointers
    // Note(Leo): + 1 since we use the element's id to index into this array and element ids start at 1 and + 1 to leave alignment room
    void* element_addresses_unaligned = AllocScratch((used_template->tag_count + 2) * sizeof(Element*), zero());
    void** element_addresses = (void**)align_ptr(element_addresses_unaligned);
    
    // Note(Leo): We use element id to index into this array, since id's are local to the file we know that
    //  they are in the range 1 - num_tags + 1
    
    Compiler::Tag* curr = used_template->first_tag;
    
    // Adding first element
    Element* added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
    element_addresses[curr->tag_id] = (void*)added;
    tag_to_element(target_dom, target_dom->elements, curr, added);
    added->master = parent->master;
    added->context_master = array_ptr;
    added->context_index = index;
    
    added->id = index_of(added, target_dom->elements->mapped_address, Element);
    
    // Note(Leo): Parent is the each element, indeces are added in reverse order so that we end up with the 
    //            highest index as the last child.
    Element* sibling = parent->first_child;
    parent->first_child = added;
    
    added->parent = parent;
    
    // Pre allocate an address for the child/sibling of the first element
    if(curr->first_child)
    {
        element_addresses[curr->first_child->tag_id] = (void*)Alloc(target_dom->elements, sizeof(Element), zero());
        added->first_child = (Element*)(element_addresses[curr->first_child->tag_id]); 
    }
    if(curr->next_sibling)
    {
        element_addresses[curr->next_sibling->tag_id] = (void*)Alloc(target_dom->elements, sizeof(Element), zero());
        added->next_sibling = (Element*)(element_addresses[curr->next_sibling->tag_id]); 
    }
    // Note(Leo): The last top level element should have the previous first child of EACH as its sibling
    else
    {      
        added->next_sibling = sibling;
    }
    
    // Note(Leo): Move off of root since it has id == 0 which would break the loop 
    curr++;
    
    // Allocates an element for the given tag id and puts the pointer in element_addresses 
    #define PushElement(tag_id) element_addresses[tag_id] = Alloc(target_dom->elements, sizeof(Element), zero())
    
    for(int i = 0; i < (used_template->tag_count - 1); i++)
    {
        // An address has already been allocated for this element
        if(element_addresses[curr->tag_id])
        {
            added = (Element*)element_addresses[curr->tag_id];
        }
        // Element has not had an address allocated yet, allocate one
        else
        {
            added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
            element_addresses[curr->tag_id] = (void*)added;
        }
        
        tag_to_element(target_dom, target_dom->elements, curr, added);
        
        // Note(Leo): The top level tags in a template have no parents, give them the each element as a parent
        if(curr->parent)
        {
            added->parent = (Element*)element_addresses[curr->parent->tag_id];
        }
        else
        {
            added->parent = parent;
            
            // Note(Leo): The last top level element should have the previous first child of EACH as its sibling
            if(!curr->next_sibling)
            {      
                added->next_sibling = sibling;
            }
        }
        added->id = index_of(added, target_dom->elements->mapped_address, Element);
        
        if(curr->next_sibling)
        {
            // Note(Leo): Allocate the element in advance if it doenst exist
            if(!element_addresses[curr->next_sibling->tag_id])
            {
                PushElement(curr->next_sibling->tag_id);
            }
            added->next_sibling = (Element*)element_addresses[curr->next_sibling->tag_id];
        }
        if(curr->first_child)
        {
            // Note(Leo): Allocate the element in advance if it doenst exist
            if(!element_addresses[curr->first_child->tag_id])
            {
                PushElement(curr->first_child->tag_id);
            }
            added->first_child = (Element*)element_addresses[curr->first_child->tag_id];
        }
        
        added->master = parent->master;
        added->context_master = array_ptr;
        added->context_index = index;
        
        // If the element being added is a component, instance it
        if(curr->type == Compiler::TagType::CUSTOM)
        {
            Attribute* comp_specifier = GetAttribute(added, AttributeType::COMP_ID);
            
            // Comp element must be corrupted/uninitialized if it doesnt have a specifier for file id
            assert(comp_specifier);
            // Must specify a comp id
            assert(comp_specifier->CompId.id);
            
            if(comp_specifier)
            {
                InstanceComponent(target_dom, added, comp_specifier->CompId.id);
            }
            
        }
        
        assert(CheckElementValid(added));
        
        curr++;
    }
    
    DeAllocScratch(element_addresses_unaligned);
}

// Merge the members of the secondary in-flight style into the main style
void MergeStyles(InFlightStyle* main, InFlightStyle* secondary)
{
    if(secondary->wrapping_p > main->wrapping_p) { main->wrapping = secondary->wrapping; main->wrapping_p = secondary->wrapping_p; }   
    
    if(secondary->horizontal_clipping_p > main->horizontal_clipping_p) { main->horizontal_clipping = secondary->horizontal_clipping; main->horizontal_clipping_p = secondary->horizontal_clipping_p; }   
    if(secondary->vertical_clipping_p > main->vertical_clipping_p) { main->vertical_clipping = secondary->vertical_clipping; main->vertical_clipping_p = secondary->vertical_clipping_p; } 

    if(secondary->width_p > main->width_p) { main->width = secondary->width; main->width_p = secondary->width_p; }   
    if(secondary->min_width_p > main->min_width_p) { main->min_width = secondary->min_width; main->min_width_p = secondary->min_width_p; }   
    if(secondary->max_width_p > main->max_width_p) { main->max_width = secondary->max_width; main->max_width_p = secondary->max_width_p; }   

    if(secondary->height_p > main->height_p) { main->height = secondary->height; main->height_p = secondary->height_p; }
    if(secondary->min_height_p > main->min_height_p) { main->min_height = secondary->min_height; main->min_height_p = secondary->min_height_p; }
    if(secondary->max_height_p > main->max_height_p) { main->max_height = secondary->max_height; main->max_height_p = secondary->max_height_p; }

    if(secondary->color_p > main->color_p) { main->color = secondary->color; main->color_p = secondary->color_p; }
    if(secondary->text_color_p > main->text_color_p) { main->text_color = secondary->text_color; main->text_color_p = secondary->text_color_p; }
    
    if(secondary->display_p > main->display_p) { main->display = secondary->display; main->display_p = secondary->display_p; }
    
    if(secondary->margin_p > main->margin_p) { main->margin = secondary->margin; main->margin_p = secondary->margin_p; }
    if(secondary->padding_p > main->padding_p) { main->padding = secondary->padding; main->padding_p = secondary->padding_p; }
    if(secondary->corners_p > main->corners_p) { main->corners = secondary->corners; main->corners_p = secondary->corners_p; }
    
    if(secondary->font_id_p > main->font_id_p) { main->font_id = secondary->font_id; main->font_id_p = secondary->font_id_p; }
    if(secondary->font_size_p > main->font_size_p) { main->font_size = secondary->font_size; main->font_size_p = secondary->font_size_p; }

}


// Converts a style to an inflight style and only initializes the priorities of non-null members
void convert_style(Style* input, InFlightStyle* target)
{
    int s_p = input->priority;
    
    if(input->wrapping != TextWrapping::NONE){ target->wrapping = input->wrapping; target->wrapping_p = s_p; }
   
    if(input->horizontal_clipping != ClipStyle::NONE) { target->horizontal_clipping = input->horizontal_clipping; target->horizontal_clipping_p = s_p; }   
    if(input->vertical_clipping != ClipStyle::NONE) { target->vertical_clipping = input->vertical_clipping; target->vertical_clipping_p = s_p; }   
   
    if(input->width.type != MeasurementType::NONE) { target->width = input->width; target->width_p = s_p; }    
    if(input->max_width.type != MeasurementType::NONE) { target->max_width = input->max_width; target->max_width_p = s_p; }    
    if(input->min_width.type != MeasurementType::NONE) { target->min_width = input->min_width; target->min_width_p = s_p; }    

    if(input->height.type != MeasurementType::NONE) { target->height = input->height; target->height_p = s_p; }    
    if(input->max_height.type != MeasurementType::NONE) { target->max_height = input->max_height; target->max_height_p = s_p; }    
    if(input->min_height.type != MeasurementType::NONE) { target->min_height = input->min_height; target->min_height_p = s_p; }    

    // Note(Leo): We can really tell if colors are uninitialized or not.
    target->color = input->color;
    target->color_p = s_p;
    target->text_color = input->text_color;
    target->text_color_p = s_p;
    
    if(input->display != DisplayType::NONE) { target->display = input->display; target->display_p = s_p; }
    
    for(int i = 0; i < 4; i++)
    {
        if(input->margin.m[i].type == MeasurementType::NONE)
        {
            input->margin.m[i].type = MeasurementType::PIXELS;
        }
    }
    target->margin = input->margin;
    target->margin_p = s_p;
    
    for(int i = 0; i < 4; i++)
    {
        if(input->padding.m[i].type == MeasurementType::NONE)
        {
            input->padding.m[i].type = MeasurementType::PIXELS;
        }
    }
    target->padding = input->padding;
    target->padding_p = s_p;
    
    target->corners = input->corners;
    target->corners_p = s_p;
    
    if(input->font_id != 0) { target->font_id = input->font_id; target->font_id_p = s_p; }
    if(input->font_size != 0) { target->font_size = input->font_size; target->font_size_p = s_p; }   
}

// Merge the members of style into the in-flight main style 
void MergeStyles(InFlightStyle* main, Style* style)
{
    InFlightStyle temp;
    
    DefaultStyle(&temp);
    convert_style(style, &temp);
        
    MergeStyles(main, &temp);
}

void FreeSubtreeObjects(Element* start, DOM* dom)
{
    // Todo(Leo): This is a recursive approach which isnt ideal, replace it!
    Element* curr = start->first_child;
    while(curr)
    {
        FreeSubtreeObjects(curr, dom);
        curr = curr->next_sibling;
    }
    
    if(start->type == ElementType::ROOT && start->master)
    {
        free(start->master);
    }
    
    if(dom)
    {
        Attribute* curr_attribute = start->first_attribute;
        Attribute* last_attribute = NULL;
        while(curr_attribute)
        {
            assert(curr_attribute != curr_attribute->next_attribute);
            last_attribute = curr_attribute; 
            curr_attribute = curr_attribute->next_attribute;
            DeAlloc(dom->attributes, last_attribute);
        }
        
        DeAlloc(dom->elements, start);
    }
}

Element* GetFocused(DOM* dom)
{
    return dom->focused_element;
}

Event* PushEvent(DOM* dom)
{
    assert(dom);
    if(dom->event_count + 1 > dom->max_events)
    {
        if(dom->last_event >= dom->max_events)
        {
            dom->last_event = 0; // Circle back to evicting from the front
        }
        
        // Evict the oldest event to make space for the new one
        DeAlloc(dom->events, (Event*)dom->events->mapped_address + dom->last_event);
        dom->last_event++;
        dom->event_count--;
    }
    
    // Note(Leo): This always works cos of the Arena's freelist
    Event* created = (Event*)Alloc(dom->events, sizeof(Event), zero());
    dom->event_count++;
    return created;
}

// Note(Leo): Returns NULL if there are no more events
Event* PopEvent(DOM* dom)
{
    if(dom->event_count == 0)
    {
        ResetArena(dom->events);
        return NULL;
    }

    // Todo(Leo): Fix this by changing it to a circular buffer instead of an arena thing.
    // Note(Leo): This is a really silly way of popping of the start of the arena to get events in-order
    Event* popped = (Event*)dom->events->next_address - dom->event_count;
    
    void* allocated = Alloc(dom->frame_arena, sizeof(Event)*2);
    Event* copied = align_mem(allocated, Event);
    memcpy(copied, popped, sizeof(Event));
    
    DeAlloc(dom->events, popped);
    dom->event_count--;
    dom->last_event = dom->event_count;
    
    return copied;
}

void RouteEvent(void* master, Event* event)
{
    assert(master);
    if(!master)
    {
        return;
    }
    
    call_comp_event(((ElementMaster*)master)->master_dom, event, ((ElementMaster*)master)->file_id, master);
}

// Convenience methods for setting style overrides
void SetColor(Element* element, StyleColor color)
{
    element->override_style.color = color;
    element->override_style.color_p = 100;
    element->do_override_style = true;
}

void SetTextColor(Element* element, StyleColor color)
{
    element->override_style.text_color = color;
    element->override_style.text_color_p = 100;
    element->do_override_style = true;
}

void SetMarginL(Element* element, Measurement sizing)
{
    element->override_style.margin.left = sizing;
    element->override_style.margin_p = 100;
    element->do_override_style = true;
}

void SetMarginR(Element* element, Measurement sizing)
{
    element->override_style.margin.right = sizing;
    element->override_style.margin_p = 100;
    element->do_override_style = true;
}
void SetMarginT(Element* element, Measurement sizing)
{
    element->override_style.margin.top = sizing;
    element->override_style.margin_p = 100;
    element->do_override_style = true;
}

void SetMarginB(Element* element, Measurement sizing)
{
    element->override_style.margin.bottom = sizing;
    element->override_style.margin_p = 100;
    element->do_override_style = true;
}

void SetMargin(Element* element, Margin margin)
{
    element->override_style.margin = margin;
    element->override_style.margin_p = 100;
    element->do_override_style = true;
}

void SetHeight(Element* element, Measurement sizing)
{
    element->override_style.height = sizing;
    element->override_style.height_p = 100;
    element->do_override_style = true;
}

void SetWidth(Element* element, Measurement sizing)
{
    element->override_style.width = sizing;
    element->override_style.width_p = 100;
    element->do_override_style = true;
}

void SetFont(Element* element, FontHandle font)
{
    element->override_style.font_id = font;
    element->override_style.font_id_p = 100;
    element->do_override_style = true;
}

void SetFontSize(Element* element, uint16_t sizing)
{
    element->override_style.font_size = sizing;
    element->override_style.font_size_p = 100;
    element->do_override_style = true;
}

void ClearOverrideStyle(Element* element)
{
    DefaultStyle(&element->override_style);
    element->do_override_style = false;
}

void InvalidateEach(Element* element, DOM* dom)
{
    if(element->type != ElementType::EACH)
    {
        return;
    }
    
    element->Each.last_count = 0;
    
    // Remove old array elements
    if(element->first_child)
    {
        Element* prev_freed = element->first_child;
        Element* next_freed = element->first_child->next_sibling;
        
        while(prev_freed)
        {
            FreeSubtreeObjects(prev_freed, dom);
            prev_freed = next_freed;
            if(next_freed)
            {
                next_freed = next_freed->next_sibling;
            }
        }
        
        element->first_child = NULL;
    }
}