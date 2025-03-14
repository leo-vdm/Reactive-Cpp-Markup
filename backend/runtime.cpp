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
        curr_selector++;
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
    terminated_name[curr_selector->name_length] = '\0';
    
    std::string name_string = terminated_name;
    
    selector_map.insert({name_string, added_selector});
    DeAllocScratch(terminated_name);
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
        curr_style++;
    }
    
    Style* added_style = arena_base + curr_style->global_id;
    added_style->id = curr_style->global_id;
    
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
        FILE* bin_file = fopen(curr->file_path, "r");
        
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

// Note(Leo): Called for every element every frame!!!!!!
void runtime_evaluate_attributes(DOM* dom, Element* element)
{
    Attribute* curr_attribute = element->first_attribute;
    while(curr_attribute)
    {
        switch(curr_attribute->type)
        {
            case(AttributeType::TEXT):
            {
                assert(element->type == ElementType::TEXT);
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
                
                // Note(Leo): We do a length limited flatten since we dont want a \0
                element->Text.temporal_text = (char*)Alloc(dom->frame_arena, sizeof(char)*binding_text->length);
                Flatten(binding_text, element->Text.temporal_text, sizeof(char)*binding_text->length);
                
                FreeString(binding_text);
                
                break;
            }
            case(AttributeType::CLASS):
            {
                // Todo(Leo): If an attribute has no binding then selector name(s) cant change, this means we could cache the 
                // selectors that this attribute names in order to skip doing this string manipulation every frame! We can also
                // have a cache_valid member or similiar which we can invalidate elsewhere if the selector names are manually changed
                char* class_string = NULL;
                int class_string_length = curr_attribute->Text.value_length;
                int class_binding_length = 0;
                
                if(curr_attribute->Text.binding_id)
                {
                    BoundExpression* binding = GetBoundExpression(curr_attribute->Text.binding_id);
                    assert(binding->type == BoundExpressionType::ARENA_STRING);
                    assert(element->master);
                    ArenaString* binding_text = binding->stub_string((void*)element->master, runtime.strings);
                    class_binding_length = binding_text->length;
                    class_string_length += class_binding_length;
                    class_string = (char*)Alloc(dom->frame_arena, class_string_length*sizeof(char));
                    
                    // Note(Leo): We do a length limited flatten since we dont want a \0
                    Flatten(binding_text, (class_string + curr_attribute->Text.binding_position), binding_text->length);
                }
                
                if(!class_string)
                {
                    class_string = (char*)Alloc(dom->frame_arena, class_string_length*sizeof(char));
                }
                
                if(curr_attribute->Text.binding_position) // Indicates theres text to copy before the binding
                {
                    memcpy(class_string, curr_attribute->Text.static_value, curr_attribute->Text.binding_position*sizeof(char));
                }
                
                if(curr_attribute->Text.value_length > curr_attribute->Text.binding_position) // Indicates theres text after the binding
                {
                    memcpy(class_string + (curr_attribute->Text.binding_position + class_binding_length), curr_attribute->Text.static_value + curr_attribute->Text.binding_position, (curr_attribute->Text.value_length - curr_attribute->Text.binding_position)*sizeof(char));
                }
                
                if(!class_string_length)
                {
                    break;
                }
                
                // Split selectors and mangle names to create global names
                char* base_address = class_string; 
                for(int i = 0; i < class_string_length; i++)
                {
                    if(class_string[i] == ' ')
                    {
                        int unmangled_name_length = base_address - (class_string + i);
                        if(unmangled_name_length <= 0)
                        {
                            base_address = class_string + i;
                            continue;
                        }
                        unmangled_name_length++; // +1 to fit \0
                        char* unmangled_name = (char*)AllocScratch(sizeof(char)*unmangled_name_length);
                        memcpy(unmangled_name, base_address, unmangled_name_length);
                        unmangled_name[unmangled_name_length] = '\0';
                        
                        int name_length = snprintf(NULL, 0, "%d-%s", ((ElementMaster*)element->master)->file_id, unmangled_name); 
                        name_length++; // +1 to fit \0
                        char* name = (char*)AllocScratch(sizeof(char)*name_length);
                        sprintf(name, "%d-%s", ((ElementMaster*)element->master)->file_id, unmangled_name);
                        
                        printf("Found selector name: %s", name);
                        
                        base_address = class_string + i;
                    }
                    else if(i == class_string_length - 1) // Last iteration
                    {
                        
                    }
                    
                }
                
                
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

void RuntimeTickAndBuildRenderque(Arena* renderque, DOM* dom)
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
            runtime_evaluate_attributes(dom, curr_element);
            continue;
        }
        if(curr_element->next_sibling)
        {
            curr_element = curr_element->next_sibling;
            runtime_evaluate_attributes(dom, curr_element);
            continue;
        }
        
        // We need to traverse back up until we find a next sibling or get to the top
        curr_element = curr_element->parent;
        while(curr_element)
        {
            if(curr_element->next_sibling)
            {
                curr_element = curr_element->next_sibling;
                runtime_evaluate_attributes(dom, curr_element);
                break;
            }
            curr_element = curr_element->parent;
        }
    }
}