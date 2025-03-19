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
    target->changed_que = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->frame_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    
    *(target->static_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->cached_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->dynamic_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->strings) = CreateArena(sizeof(StringBlock)*10000, sizeof(StringBlock));
    *(target->pointer_arrays) = CreateArena(sizeof(LinkedPointer)*10000, sizeof(LinkedPointer));
    *(target->elements) = CreateArena(sizeof(Element)*5000, sizeof(Element));
    *(target->attributes) = CreateArena(sizeof(Element)*20000, sizeof(Attribute));
    *(target->changed_que) = CreateArena(sizeof(int*)*1000, sizeof(int*));
    *(target->frame_arena) = CreateArena(sizeof(char)*10000, sizeof(char));
}

void CalculateStyles()
{
    
}

void BuildRenderque()
{
    
}

void Draw()
{
    
}

// Todo(Leo): Move all bound expression related stuff into runtime since the objects are now shared universally and stored on a runtime arena
BoundExpression* register_bound_expr(SubscribedStubVoid fn, int id)
{    
    BoundExpression* created = (BoundExpression*)runtime.bound_expressions->mapped_address + id; 
    
    // Check if we have enough space allocated for the required slot to be available, if not allocated up until the needed slot
    if(runtime.bound_expressions->next_address <= (uintptr_t)created)
    {   
        uintptr_t endpoint = (uintptr_t)created + sizeof(BoundExpression);
        Alloc(runtime.bound_expressions, endpoint - runtime.bound_expressions->mapped_address);
    }
    created->expression_id = id;
    created->type = BoundExpressionType::VOID_RET;
    created->stub_void = fn;
    
    return created;
}

BoundExpression* register_bound_expr(SubscribedStubString fn, int id)
{
    BoundExpression* created = (BoundExpression*)runtime.bound_expressions->mapped_address + id; 
    
    // Check if we have enough space allocated for the required slot to be available, if not allocated up until the needed slot
    if(runtime.bound_expressions->next_address <= (uintptr_t)created)
    {   
        uintptr_t endpoint = (uintptr_t)created + sizeof(BoundExpression);
        Alloc(runtime.bound_expressions, endpoint - runtime.bound_expressions->mapped_address);
    }
    created->expression_id = id;
    created->type = BoundExpressionType::ARENA_STRING;
    created->stub_string = fn;
    
    return created;
}

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
    // TODO(Leo): DONT USE MALLOC HERE THIS IS TEMPORARY, put this onto an arena instead
    void* allocated = malloc(size);
    memset(allocated, 0, size);
    return allocated;
}


void* AllocComponent(DOM* dom, int size, int file_id)
{
    // TODO(Leo): DONT USE MALLOC HERE THIS IS TEMPORARY, put this onto an arena instead
    void* allocated = malloc(size);
    memset(allocated, 0, size);
    return allocated;
}

Attribute* convert_saved_attribute(DOM* dom, Compiler::Attribute* converted_attribute)
{
    Attribute* added = (Attribute*)Alloc(dom->attributes, sizeof(Attribute), zero());
    added->type = (AttributeType)((int)converted_attribute->type);
    
    switch(added->type)
    {
        case(AttributeType::COMP_ID):
            added->CompId.id = converted_attribute->CompId.id;
            return added;
            break;
        case(AttributeType::CUSTOM):
            added->Custom.name_value = converted_attribute->Custom.name;
            added->Custom.name_length = converted_attribute->Custom.name_length;
            goto text_like;
            break;
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

Element* tag_to_element(DOM* dom, Compiler::Tag* converted_tag, Element* target_element = NULL)
{
    Element* added = target_element;
    if(!added)
    {
        added = (Element*)Alloc(dom->elements, sizeof(Element), zero());
    }
    
    added->num_attributes = converted_tag->num_attributes;
    added->type = (ElementType)((int)converted_tag->type);
    
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
    
    return added;
}

void* InstancePage(DOM* target_dom, int id)
{
    // Note(Leo): Get the difference between the old base and target in indexes and apply that to the new base and pointer type
    #define get_pointer(old_base_ptr, new_base_ptr, target_ptr) (target_ptr ? ((new_base_ptr) + (uint64_t)((decltype(old_base_ptr))target_ptr - old_base_ptr)) : 0)
    
    LoadedFileHandle* page_bin =  GetFileFromId(id);
    
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
    
    Compiler::Tag* curr = page_bin->root_tag;
    
    Element* added;
    
    Compiler::Tag* tag_base = (Compiler::Tag*)page_bin->root_tag;
    Element* element_base = (Element*)target_dom->elements->mapped_address;
    
    // Adding root element
    added = tag_to_element(target_dom, curr);
    added->parent = NULL;
    added->next_sibling = NULL;
    added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
    added->master = created_page;
    curr++;
    
    while(curr->tag_id)
    {
        added = tag_to_element(target_dom, curr);
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
    void* added_comp;
    call_comp_main(target_dom, comp_bin->file_id, &added_comp);
    
    // Note(Leo): All components should inherit the Component struct so that they have the file_id member
    Component* comp_obj = (Component*)added_comp;
    comp_obj->file_id = comp_bin->file_id;
    
    // Allocate the element lookup array and align it so we can store pointers
    // Note(Leo): + 1 since we use the element's id to index into this array and element ids start at 1 and + 1 to leave alignment room
    void* element_addresses_unaligned = AllocScratch((comp_bin->file_info.tag_count + 2) * sizeof(Element*), zero());
    void** element_addresses = (void**)align_ptr(element_addresses_unaligned);
    
    // Note(Leo): We use element id to index into this array, since id's are local to the file we know that
    //  they are in the range 1 - num_tags + 1
    
    Compiler::Tag* curr = comp_bin->root_tag;
    
    // Adding root element
    Element* added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
    element_addresses[curr->tag_id] = (void*)added;
    tag_to_element(target_dom, curr, added);
    added->master = added_comp;
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
        
        tag_to_element(target_dom, curr, added);
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

// Merge the members of the secondary in-flight style into the main style
void MergeStyles(InFlightStyle* main, InFlightStyle* secondary)
{
    if(secondary->width_p > main->width_p) { main->width = secondary->width; main->width_p = secondary->width_p; }   
    if(secondary->height_p > main->height_p) { main->height = secondary->height; main->height_p = secondary->height_p; }
}

// Merge the members of style into the in-flight main style 
void MergeStyles(InFlightStyle* main, Style* style)
{
    int s_p = style->priority;
    if(s_p > main->width_p) { main->width = style->width; main->width_p = s_p; }   
    if(s_p > main->height_p) { main->height = style->height; main->height_p = s_p; }   
}