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
    target->bound_expressions = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->bound_vars = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->styles = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->selectors = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->changed_que = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->frame_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->generations = (Arena*)Alloc(master_arena, sizeof(Arena));
    
    *(target->static_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->cached_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->dynamic_cstrings) = CreateArena(sizeof(char)*100000, sizeof(char));
    *(target->strings) = CreateArena(sizeof(StringBlock)*10000, sizeof(StringBlock));
    *(target->pointer_arrays) = CreateArena(sizeof(LinkedPointer)*10000, sizeof(LinkedPointer));
    *(target->elements) = CreateArena(sizeof(Element)*5000, sizeof(Element));
    *(target->attributes) = CreateArena(sizeof(Element)*20000, sizeof(Attribute));
    *(target->bound_expressions) = CreateArena(sizeof(BoundExpression)*1000, sizeof(BoundExpression));
    *(target->bound_vars) = CreateArena(sizeof(BoundVariable)*1000, sizeof(BoundVariable));
    *(target->styles) = CreateArena(sizeof(Style)*10000, sizeof(Style));
    *(target->selectors) = CreateArena(sizeof(Selector)*1000, sizeof(Selector));
    *(target->changed_que) = CreateArena(sizeof(int*)*1000, sizeof(int*));
    *(target->frame_arena) = CreateArena(sizeof(char)*10000, sizeof(char));
    *(target->generations) = CreateArena(sizeof(Generation)*1000, sizeof(Generation));
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

BoundExpression* register_bound_expr(SubscribedStubVoid, int id)
{
    return NULL;
}
BoundExpression* register_bound_expr(SubscribedStubString, int id)
{
    return NULL;
}
BoundExpression* register_bound_expr(SubscribedCompStubVoid, int id)
{
    return NULL;
}
BoundExpression* register_bound_expr(SubscribedCompStubString, int id)
{
    return NULL;
}

void subscribe_to(BoundExpression* expr, int target_bound_id)
{

}

void bound_var_changed(int changed_var_id)
{

}

void bound_var_changed(int changed_var_id, void* d_void)
{
    
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


void* AllocComponent(DOM* dom, int size, int file_id)
{
    // TODO(Leo): DONT USE MALLOC HERE THIS IS TEMPORARY, put this onto an arena instead
    void* allocated = malloc(size);
    memset(allocated, 0, size);
    return allocated;
}

LinkedPointer* get_attribute_by_expression(Generation* gen, int id)
{
    auto search = gen->expression_sub_map->find(id);
    // Expression already has subscribers
    if(search != gen->expression_sub_map->end())
    {
        return search->second;
    }
    // Expression has no subscribers
    return NULL;
}

Attribute* convert_saved_attribute(DOM* dom, Compiler::Attribute* converted_attribute, Generation* gen)
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
            added->Text.value_length = converted_attribute->Text.value_length;
            break;
    }
    
    // Note(Leo): Only text like should get to this point, others should early return!
    if(!added->Text.binding_id)
    {
        return added;
    }
    
    LinkedPointer* subscription_target = get_attribute_by_expression(gen, added->Text.binding_id);
    if(subscription_target) // Found a pre-existing subscriber to this expression
    {
        LinkedPointer* added_subscription = (LinkedPointer*)Alloc(dom->pointer_arrays, sizeof(LinkedPointer));
        added_subscription->next = subscription_target->next;
        subscription_target->next = added_subscription;
        added_subscription->data = (void*)added;
    }
    else // First attribute to subscribe, add it
    {
        LinkedPointer* added_subscription = (LinkedPointer*)Alloc(dom->pointer_arrays, sizeof(LinkedPointer));
        subscription_target = gen->subscribers_head;
        LinkedPointer* added_divider = (LinkedPointer*)Alloc(dom->pointer_arrays, sizeof(LinkedPointer), zero()); // Create a dividing entry with NULL data field
        added_divider->next = subscription_target;
        added_subscription->next = added_divider;
        gen->subscribers_head = added_subscription;
        
        // Register the new expression subscription
        // Note(Leo): [] style access was creating a bug, ->insert() works fine
        gen->expression_sub_map->insert({added->Text.binding_id, added_subscription});
    }
    
    return added;
}

Element* tag_to_element(DOM* dom, Compiler::Tag* converted_tag, Generation* gen, Element* target_element = NULL)
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
        curr_added_attribute = convert_saved_attribute(dom, converted_tag->first_attribute + i, gen);
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

Generation* create_gen(DOM* dom)
{
    Generation* created = (Generation*)Alloc(dom->generations, sizeof(Generation), zero());
    // Get the id of the generation from the slot it gets
    created->generation_id = created - (Generation*)(dom->generations->mapped_address);
    
    created->expression_sub_map = new std::map<int, LinkedPointer*>();
    
    created->subscribers_head = (LinkedPointer*)Alloc(dom->pointer_arrays, sizeof(LinkedPointer), zero());
    return created;
}

void InstancePage(DOM* target_dom, int id)
{
    #define get_pointer(old_base_ptr, new_base_ptr, target_ptr) (target_ptr ? (((uintptr_t)new_base_ptr) + ((uintptr_t)target_ptr - (uintptr_t)old_base_ptr)) : 0)
    
    LoadedFileHandle* page_bin =  GetFileFromId(id);
    
    if(!page_bin)
    {   
        printf("Page binary could not be found!\n");
        assert(page_bin);
    }
    
    Compiler::Tag* curr = page_bin->root_tag;
    
    Element* added;
    
    Generation* new_gen = create_gen(target_dom);
    
    void* tag_base = (void*)page_bin->root_tag;
    void* element_base = (void*)target_dom->elements->mapped_address;
    
    added = tag_to_element(target_dom, curr, new_gen);
    curr++;
    new_gen->generation_head = added; // Set the root pointer as the head of the page generation
    
    added->parent = NULL;
    added->next_sibling = NULL;
    added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
    
    
    while(curr->tag_id)
    {
        added = tag_to_element(target_dom, curr, new_gen);
        added->parent = (Element*)get_pointer(tag_base, element_base, curr->parent);;
        added->next_sibling = (Element*)get_pointer(tag_base, element_base, curr->next_sibling);
        added->first_child = (Element*)get_pointer(tag_base, element_base, curr->first_child);
        
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
}

void* InstanceComponent(DOM* target_dom, Element* parent, int id)
{

    LoadedFileHandle* comp_bin =  GetFileFromId(id);
    
    if(!comp_bin)
    {   
        printf("Page binary could not be found!\n");
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
    
    Generation* new_gen = create_gen(target_dom);
    ((Component*)added_comp)->generation = new_gen->generation_id;
    
    Element* added = (Element*)Alloc(target_dom->elements, sizeof(Element), zero());
    tag_to_element(target_dom, curr, new_gen, added);
    added->master = added_comp;
    
    // Pre allocate an address for the first child element
    
    element_addresses[curr->first_child->tag_id] = (void*)Alloc(target_dom->elements, sizeof(Element), zero());
    
    element_addresses[curr->tag_id] = (void*)added;
    
    new_gen->generation_head = added;
    
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
        
        tag_to_element(target_dom, curr, new_gen, added);
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