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
    target->styles = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->selectors = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->changed_que = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->frame_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    
    *(target->static_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->cached_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->dynamic_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->strings) = CreateArena(sizeof(StringBlock)*10000, sizeof(StringBlock));
    *(target->pointer_arrays) = CreateArena(sizeof(LinkedPointer)*10000, sizeof(LinkedPointer));
    *(target->elements) = CreateArena(sizeof(Element)*5000, sizeof(Element));
    *(target->attributes) = CreateArena(sizeof(Element)*20000, sizeof(Attribute));
    *(target->styles) = CreateArena(sizeof(Style)*10000, sizeof(Style));
    *(target->selectors) = CreateArena(sizeof(Selector)*1000, sizeof(Selector));
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

void ConverSelectors(Compiler::Selector* selector)
{
    
}

void ConvertStyles(Compiler::Style* style)
{
    
}

void* InstancePage(DOM* target_dom, int id)
{
    #define get_pointer(old_base_ptr, new_base_ptr, target_ptr) (target_ptr ? (((uintptr_t)new_base_ptr) + ((uintptr_t)target_ptr - (uintptr_t)old_base_ptr)) : 0)
    
    LoadedFileHandle* page_bin =  GetFileFromId(id);
    
    if(!page_bin)
    {   
        printf("Page binary could not be found!\n");
        assert(page_bin);
    }
    
    // Allocate and setup page object
    void* created_page;
    call_page_main(target_dom, page_bin->file_id, &created_page);
    
    Compiler::Tag* curr = page_bin->root_tag;
    
    Element* added;
    
    void* tag_base = (void*)page_bin->root_tag;
    void* element_base = (void*)target_dom->elements->mapped_address;
    
    added = tag_to_element(target_dom, curr);
    curr++;
    
    added->parent = NULL;
    added->next_sibling = NULL;
    added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
    added->master = created_page;
    
    while(curr->tag_id)
    {
        added = tag_to_element(target_dom, curr);
        added->parent = (Element*)get_pointer(tag_base, element_base, curr->parent);;
        added->next_sibling = (Element*)get_pointer(tag_base, element_base, curr->next_sibling);
        added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
        
        added->master = created_page;
        
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
    
    ((Component*)added_comp)->file_id = comp_bin->file_id;
    
    
    // Allocate the element lookup array and align it so we can store pointers
    // Note(Leo): + 1 since we use the element's id to index into this array and element ids start at 1 and + 1 to leave alignment room
    void* element_addresses_unaligned = AllocScratch((comp_bin->file_info.tag_count + 2) * sizeof(Element*), zero());
    void** element_addresses = (void**)align_ptr(element_addresses_unaligned);
    
    // Note(Leo): We use element id to index into this array, since id's are local to the file we know that they are in the range 1 - num_tags + 1
    
    Compiler::Tag* curr = comp_bin->root_tag;
    
    Element* added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
    tag_to_element(target_dom, curr, added);
    added->master = added_comp;
    
    // Pre allocate an address for the first child element
    
    element_addresses[curr->first_child->tag_id] = (void*)Alloc(target_dom->elements, sizeof(Element), zero());
    
    element_addresses[curr->tag_id] = (void*)added;
    
    // Note(Leo): Parent is the Custom element type that calls this instance-ing and so it should have no existing children
    assert(!parent->first_child);
    parent->first_child = added;
    
    added->parent = parent;
    added->next_sibling = NULL;
    
    if(curr->first_child)
    {
    added->first_child = (Element*)(element_addresses[curr->first_child->tag_id]); 
    }
    curr++;
    
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
        
        if(curr->next_sibling)
        {
            added->next_sibling = (Element*)element_addresses[curr->next_sibling->tag_id];
        }
        if(curr->first_child)
        {
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
    
    return added_comp;
}