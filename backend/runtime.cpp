#include "arena.h"
#include "DOM.h"
#include "dom_attatchment.h"
#include "file_system.h"
#include "compiler.h"
#include "platform.h"
#include <map>

std::map<int, LoadedFileHandle*> file_id_map = {};
std::map<std::string, LoadedFileHandle*> file_name_map = {};

void InitRuntime(Arena* master_arena, Runtime* target)
{
    target->master_arena = master_arena;
    target->loaded_files = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_tags = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_attributes = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_styles = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->loaded_selectors = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->static_combined_values = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->doms = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->selectors = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->styles = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->bound_expressions = (Arena*)Alloc(master_arena, sizeof(Arena));
    target->strings = (Arena*)Alloc(master_arena, sizeof(Arena));

    *(target->loaded_files) = CreateArena(100*sizeof(LoadedFileHandle), sizeof(LoadedFileHandle));
    *(target->loaded_tags) = CreateArena(10000*sizeof(Compiler::Tag), sizeof(Compiler::Tag));
    *(target->loaded_attributes) = CreateArena(10000*sizeof(Compiler::Attribute), sizeof(Compiler::Attribute));
    *(target->loaded_styles) = CreateArena(10000*sizeof(Compiler::Style), sizeof(Compiler::Style));
    *(target->loaded_selectors) = CreateArena(10000*sizeof(Compiler::Selector), sizeof(Compiler::Selector));
    *(target->static_combined_values) = CreateArena(100000*sizeof(char), sizeof(char));
    *(target->doms) = CreateArena(100*sizeof(DOM), sizeof(DOM));
    *(target->selectors) = CreateArena(100*sizeof(Selector), sizeof(Selector));
    *(target->styles) = CreateArena(100*sizeof(Style), sizeof(Style));
    *(target->bound_expressions) = CreateArena(1000*sizeof(BoundExpression), sizeof(BoundExpression));
    *(target->strings) = CreateArena(1000*sizeof(StringBlock), sizeof(StringBlock));
}

Runtime runtime;

std::map<std::string, Selector*> selector_map = {};

// Note(Leo): Selectors are indexed as an array by their global ID
void ConvertSelectors(Compiler::Selector* selector)
{
    assert(selector);
    Selector* arena_base = (Selector*)runtime.selectors->mapped_address;
    Compiler::Selector* curr_selector = selector;

    while(curr_selector->global_id)
    {
        // Test if we need to allocate more space for this selector to have its slot
        if((arena_base + curr_selector->global_id) >= (Selector*)runtime.selectors->next_address) 
        {
            // + 1 since if we are allocated up to the required address we are still 1 Selector short
            int allocated_count = ((arena_base + curr_selector->global_id) + 1) - (Selector*)runtime.selectors->next_address;
            Alloc(runtime.selectors, sizeof(Selector) * allocated_count, zero());
        }    
        
        Selector* added_selector = arena_base + curr_selector->global_id;
        added_selector->id = curr_selector->global_id;
        
        added_selector->style_count = curr_selector->num_styles;
        memcpy(added_selector->style_ids, curr_selector->style_ids, curr_selector->num_styles*sizeof(int));
        added_selector->name_length = curr_selector->name_length;
        added_selector->name = curr_selector->name;
        
        // TODO(Leo): The compiler seems to add null terminators after selector names but it is not clear where that is happening
        // so this code doesnt rely on names coming in null terminated. Figure out why its happening. 
        
        char* terminated_name = (char*)AllocScratch((curr_selector->name_length + 1)*sizeof(char)); // +1 to fit \0
        memcpy(terminated_name, curr_selector->name, curr_selector->name_length*sizeof(char));
        terminated_name[curr_selector->name_length] = '\0';
        
        selector_map.insert({(const char*)terminated_name, added_selector});
        DeAllocScratch(terminated_name);
        curr_selector++;
    }
}

// Note(Leo): Styles are indexed as an array by their global ID 
void ConvertStyles(Compiler::Style* style)
{
    assert(style);
    Style* arena_base = (Style*)runtime.styles->mapped_address;
    Compiler::Style* curr_style = style;
    while(curr_style->global_id)
    {
        // Test if we need to allocate more space for this style to have its slot
        if((arena_base + curr_style->global_id) >= (Style*)runtime.styles->next_address) 
        {
            // + 1 since if we are allocated up to the required address we are still 1 Style short
            int allocated_count = ((arena_base + curr_style->global_id) + 1) - (Style*)runtime.styles->next_address;
            Alloc(runtime.styles, sizeof(Style) * allocated_count, zero());
        }    

        Style* added_style = arena_base + curr_style->global_id;
        memcpy(added_style, curr_style, sizeof(Style));

        // Indicates the style has a font to use
        if(curr_style->font_name.len > 0)
        {
            char* terminated_name = (char*)AllocScratch((curr_style->font_name.len + 1)*sizeof(char));
            memcpy(terminated_name, curr_style->font_name.value, curr_style->font_name.len*sizeof(char));
            terminated_name[curr_style->font_name.len] = '\0';
            
            added_style->font_id = FontPlatformGetFont(terminated_name);
            if(!added_style->font_id)
            {
                printf("Error while loading font '%s'\n", terminated_name);
                added_style->font_id = 1; // Use the default font instead
            }
            DeAllocScratch(terminated_name);
        }
        else // Use the default font
        {
            added_style->font_id = 1;
        }
        
        curr_style++;
    }
}


int InitializeRuntime(Arena* master_arena, FileSearchResult* first_binary)
{   
    // Initialize Runtime
    
    InitRuntime(master_arena, &runtime);

    FileSearchResult* curr = first_binary;
        
    // Load all page/comp binaries
    while(curr->file_path)
    {
        printf("Found binary \"%s\"\n", curr->file_name);
        FILE* bin_file = fopen(curr->file_path, "rb");
        
        LoadedFileHandle* loaded_bin = (LoadedFileHandle*)Alloc(runtime.loaded_files, sizeof(LoadedFileHandle));
        *loaded_bin = LoadPage(bin_file, runtime.loaded_tags, runtime.loaded_attributes, runtime.loaded_styles, runtime.loaded_selectors, runtime.static_combined_values);
        assert(loaded_bin);
        
        ConvertSelectors(loaded_bin->first_selector);
        ConvertStyles(loaded_bin->first_style);
        
        file_id_map[loaded_bin->file_id] = loaded_bin;
    
        int name_len = strlen(curr->file_name);
        name_len -= 3; // -4 to exclude .bin, + 1 to fit \0
        char* name = (char*)AllocScratch(sizeof(name_len), no_zero());
        memcpy(name, curr->file_name, name_len);
        name[name_len - 1] = '\0';
        
        std::string name_string = name;
        
        file_name_map[name_string] = loaded_bin;
        printf("Registering under name \"%s\"\n", name);
        
        DeAllocScratch(name);
        
        fclose(bin_file);
        
        curr++;
    }
    
    register_binding_subscriptions(&runtime);
    
    return 0;
}

bool RuntimeInstanceMainPage()
{
    LoadedFileHandle* main_page = GetFileFromName("MainPage");
    if(!main_page)
    {
        return false;
    }

    DOM* main_dom = (DOM*)Alloc(runtime.doms, sizeof(DOM), zero());    
    InitDOM(runtime.master_arena, main_dom);
    
    SwitchPage(main_dom, main_page->file_id);
    
    PlatformRegisterDom((void*)main_dom);
    return true;
}

bool PointInsideBounds(const bounding_box bounds, const vec2 point)
{
    return point.x >= bounds.x && point.x <= (bounds.x + bounds.width) && point.y >= bounds.y && point.y <= (bounds.y + bounds.height);
}

// Creates a null terminated global name from a selector's name
// Note(Leo): De-allocating name from scratch space is the caller's responsibility
char* mangle_selector_name(StringView name, int file_id)
{
    int global_name_length = snprintf(NULL, 0, "%d-%.*s", file_id, name.len, name.value);
    global_name_length++; // +1 to fit \0
    char* global_name = (char*)AllocScratch(sizeof(char)*global_name_length);
    sprintf(global_name, "%d-%.*s", file_id, name.len, name.value);
    return global_name;
}

LoadedFileHandle* GetFileFromId(int id)
{
    auto search = file_id_map.find(id);
    if(search != file_id_map.end())
    {
        return search->second;
    }
    
    return NULL;
}

LoadedFileHandle* GetFileFromName(const char* name)
{
    std::string name_string;
    name_string = name;
    auto search = file_name_map.find(name_string);
    if(search != file_name_map.end())
    {
        return search->second;
    }
    
    return NULL;
}

void SwitchPage(DOM* dom, int id, int flags)
{
    if(flags & save_dom())
    {
        printf("DOM state saving not implemented yet!\n");    
    }
    
    // Clear elements/data from dom
    ResetArena(dom->cached_cstrings);
    ResetArena(dom->dynamic_cstrings);
    ResetArena(dom->strings);
    ResetArena(dom->pointer_arrays);
    ResetArena(dom->elements);
    ResetArena(dom->attributes);
    
    InstancePage(dom, id);    
}

Selector* GetSelectorFromName(const char* name)
{
    auto search = selector_map.find(name);
    if(search != selector_map.end())
    {
        return search->second;
    }
    
    return NULL;
}

inline Style* GetStyleFromID(int style_id)
{
    assert(style_id >= 0);
    return ((Style*)runtime.styles->mapped_address) + style_id;
}

InFlightStyle* merge_selector_styles(Selector* selector)
{   
    // Note(Leo): We save the merged style into the selector as a cache, if we see that the selector has a valid
    //            cached value we early return.
    if(selector->flags & cache_valid())
    {
        return &selector->cached_style;
    }
    
    selector->flags = selector->flags | cache_valid();
    
    InFlightStyle* merged_style = &selector->cached_style;
    
    // Note(Leo): Since styles can currently have priority 0, we must ensure that default style has -1 priority otherswise
    // priority 0 styles will be ignored
    DefaultStyle(merged_style);
    
    for(int i = 0; i < selector->style_count; i++)
    {
        MergeStyles(merged_style, GetStyleFromID(selector->style_ids[i]));
    }
    
    return merged_style;
}

// Gets a selector from the global pool by mangling its name into a global name
Selector* GetGlobalSelector(StringView local_name, int file_id)
{
    int global_name_length = snprintf(NULL, 0, "%d-%.*s", file_id, local_name.len, local_name.value);
    global_name_length++; // +1 to fit \0
    char* global_name = (char*)AllocScratch(sizeof(char)*global_name_length);
    sprintf(global_name, "%d-%.*s", file_id, local_name.len, local_name.value);
    
    Selector* found = GetSelectorFromName(global_name);
    
    DeAllocScratch(global_name);
    return found;
}

// Merge the style of an element based on its type selector into the given style
void merge_element_type_style(ElementType type, bool is_hovered, int file_id, InFlightStyle* target)
{
    ArenaString* selector = CreateString(runtime.strings);
    switch(type)
    {
        case(ElementType::ROOT):
        {
            Append(selector, "root");
            break;
        }
        case(ElementType::HDIV):
        {
            Append(selector, "hdiv");
            break;
        }
        case(ElementType::VDIV):
        {
            Append(selector, "hdiv");
            break;
        }
        case(ElementType::IMG):
        {
            Append(selector, "img");
            break;
        }
        case(ElementType::GRID):
        {
            Append(selector, "grid");
            break;
        }
        default:
        {
            break;
        }
    }
    
    char* base_name = Flatten(selector);
    Selector* base_selector = GetGlobalSelector({base_name, (uint32_t)selector->length}, file_id);
    
    if(base_selector)
    {
        InFlightStyle* base_style = merge_selector_styles(base_selector);
        MergeStyles(target, base_style);
        DeAllocScratch(base_name); 
    }
    if(is_hovered)
    {
        Append(selector, "!hover");
        char* hovered_name = Flatten(selector);
        Selector* hovered_selector = GetGlobalSelector({hovered_name, (uint32_t)selector->length}, file_id);
        if(hovered_selector)
        {
            InFlightStyle* hovered_style = merge_selector_styles(hovered_selector);
            MergeStyles(target, hovered_style);
            DeAllocScratch(hovered_name);
        }
    }
    
    FreeString(selector);
}

// Note(Leo): Called for every element every frame!!!!!!
void runtime_evaluate_attributes(DOM* dom, PlatformControlState* controls, Element* element)
{
    DefaultStyle(&element->working_style);
    if(element->last_sizing)
    {
        // Element is hovered
        if(PointInsideBounds(element->last_sizing->bounds, controls->cursor_pos))
        {
            element->flags = element->flags | is_hovered(); 
        }
        else // Not hovered
        {
            element->flags &= ~is_hovered(); 
        }
    }
    
    merge_element_type_style(element->type, element->flags & is_hovered(), ((ElementMaster*)element->master)->file_id, &element->working_style);

    Attribute* curr_attribute = element->first_attribute;
    while(curr_attribute)
    {
        switch(curr_attribute->type)
        {
            case(AttributeType::TEXT):
            {
                assert(element->type == ElementType::TEXT);
                
                // Text font and size comes from parent
                element->working_style.font_id = element->parent->working_style.font_id;
                element->working_style.font_size = element->parent->working_style.font_size;
                element->working_style.text_color = element->parent->working_style.text_color;
                
                if(!curr_attribute->Text.binding_id) // Text is known
                {
                    element->Text.temporal_text = (char*)Alloc(dom->frame_arena, sizeof(char)*curr_attribute->Text.value_length);
                    memcpy(element->Text.temporal_text, curr_attribute->Text.static_value, sizeof(char)*curr_attribute->Text.value_length);
                    element->Text.temporal_text_length = curr_attribute->Text.value_length;
                    break;
                }
                // Text has to come from a binding
                BoundExpression* binding = GetBoundExpression(curr_attribute->Text.binding_id);
                assert(binding->type == BoundExpressionType::ARENA_STRING);
                assert(element->master);
                ArenaString* binding_text = binding->stub_string((void*)element->master, runtime.strings);
                
                element->Text.temporal_text_length = binding_text->length;
                
                // Note(Leo): +1 to fit \0 which is automatically apended
                element->Text.temporal_text = (char*)Alloc(dom->frame_arena, sizeof(char)*(binding_text->length + 1));
                Flatten(binding_text, element->Text.temporal_text);
            
                FreeString(binding_text);
                
                break;
            }
            case(AttributeType::SRC):
            {
                // SRC is only for images and video elements
                assert(element->type == ElementType::IMG || element->type == ElementType::VIDEO);
                
                if(element->type == ElementType::IMG)
                {
                    // Handle has not been cached yet, get it.
                    if(!element->Image.handle)
                    {
                        char* terminated_name = (char*)AllocScratch((curr_attribute->Text.value_length + 1)*sizeof(char)); // +1 for \0
                        memcpy(terminated_name, curr_attribute->Text.static_value, curr_attribute->Text.value_length*sizeof(char));
                        terminated_name[curr_attribute->Text.value_length] = '\0';

                        element->Image.handle = RenderplatformGetImage(terminated_name);
                        DeAllocScratch(terminated_name);
                    }
                }
                break;
            }
            case(AttributeType::CLASS):
            {
                // Todo(Leo): If an attribute has no binding then selector name(s) cant change, this means we could cache the 
                // selectors that this attribute names in order to skip doing this string manipulation every frame! We can also
                // have a cache_valid member or similiar which we can invalidate elsewhere if the selector names are manually changed
                ArenaString* class_string = CreateString(runtime.strings);
                
                if(curr_attribute->Text.binding_position) // Indicates theres text to copy before the binding
                {
                    Append(class_string, curr_attribute->Text.static_value, curr_attribute->Text.binding_position*sizeof(char));
                }
                
                if(curr_attribute->Text.binding_id)
                {
                    BoundExpression* binding = GetBoundExpression(curr_attribute->Text.binding_id);
                    assert(binding->type == BoundExpressionType::ARENA_STRING);
                    assert(element->master);
                    ArenaString* binding_text = binding->stub_string((void*)element->master, runtime.strings);
                    
                    // Note(Leo): binding_text gets freed as a part of class_string (dont need to call freestring)
                    Append(class_string, binding_text, no_copy());
                }
                
                
                if(curr_attribute->Text.value_length > curr_attribute->Text.binding_position) // Indicates theres text after the binding
                {
                    Append(class_string, curr_attribute->Text.static_value + curr_attribute->Text.binding_position, (curr_attribute->Text.value_length - curr_attribute->Text.binding_position)*sizeof(char));
                }
                
                if(!class_string->length)
                {
                    break;
                }
                
                // Split selectors and mangle names to create global names and combine the styles from all the selectors
                char* flat_class_string = Flatten(class_string); 
                char* start_address = flat_class_string;
                char* end_address = start_address; 
                for(int i = 0; i < class_string->length; i++)
                {
                    // Go until hitting a space or getting to the end of the string
                    if(flat_class_string[i] != ' ' && i != class_string->length - 1)
                    {
                        end_address++;
                        continue;
                    }
                    
                    // Weve hit another space after just hitting one
                    if(start_address == end_address)
                    {
                        start_address++;
                        end_address++;
                        continue;
                    }
                    
                    // We are on the last iteration so include the remaining charachter
                    if(i == class_string->length - 1)
                    {
                        end_address++;
                    }
                    
                    // Weve succesfully found a selector
                    int name_length = end_address - start_address;
                    //printf("Found selector: %.*s\n", unmangled_name_length, start_address);
                    
                    Selector* found_selector = GetGlobalSelector({start_address, (uint32_t)name_length}, ((ElementMaster*)element->master)->file_id);

                    if(found_selector)
                    {
                        InFlightStyle* selector_style = merge_selector_styles(found_selector);
                        MergeStyles(&element->working_style, selector_style);
                    }
                    else
                    {
                        printf("Unknown selector name: %.*s\n", name_length, start_address);
                    }
                    
                    if(element->flags & is_hovered())
                    {
                        // Check for a hovered version of the selector
                        ArenaString* hovered_selector = CreateString(runtime.strings);
                        Append(hovered_selector, start_address, name_length);
                        Append(hovered_selector, "!hover");
                        char* flat_selector_string = Flatten(hovered_selector);
                        found_selector = GetGlobalSelector({flat_selector_string, (uint32_t)hovered_selector->length}, ((ElementMaster*)element->master)->file_id);

                        if(found_selector)
                        {
                            InFlightStyle* selector_style = merge_selector_styles(found_selector);
                            MergeStyles(&element->working_style, selector_style);
                        }
                        DeAllocScratch(flat_selector_string);
                        FreeString(hovered_selector);
                    }                
                    
                    // Move over the ' '
                    end_address++;
                    start_address = end_address;
                    
                }
                
                DeAllocScratch(flat_class_string);
                FreeString(class_string);
                
                break;
            }
            default:
            {
                break;
            }
        
        }
        
        curr_attribute = curr_attribute->next_attribute; 
    }
}

void RuntimeClearTemporal(DOM* target)
{
    ResetArena(target->frame_arena);
}

Arena* RuntimeTickAndBuildRenderque(Arena* renderque, DOM* dom, PlatformControlState* controls, int window_width, int window_height)
{
    // Note(Leo): Page root element is always at the first address of the dom
    Element* root_element = (Element*)dom->elements->mapped_address;
    assert(!root_element->parent && !root_element->next_sibling); // Root cant have a parent or siblings
    
    Element* curr_element = root_element;
    // Depth first walk
    while(curr_element)
    {
    
        if(curr_element->first_child)
        {
            curr_element = curr_element->first_child;
            runtime_evaluate_attributes(dom, controls, curr_element);
            continue;
        }
        if(curr_element->next_sibling)
        {
            curr_element = curr_element->next_sibling;
            runtime_evaluate_attributes(dom, controls, curr_element);
            continue;
        }
        
        // We need to traverse back up until we find a next sibling or get to the top
        curr_element = curr_element->parent;
        while(curr_element)
        {
            if(curr_element->next_sibling)
            {
                curr_element = curr_element->next_sibling;
                runtime_evaluate_attributes(dom, controls, curr_element);
                break;
            }
            curr_element = curr_element->parent;
        }
    }
    
    // Note(Leo): This is an approximation of the element count since there could be empty space inside the arena but we
    //            will never underestimate so its ok.
    int element_count = (dom->elements->next_address - dom->elements->mapped_address)/sizeof(Element);
    return ShapingPlatformShape(root_element, renderque, element_count, window_width, window_height);
}