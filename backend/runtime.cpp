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
    target->bound_expressions = (Arena*)Alloc(master_arena, sizeof(Arena));

    *(target->loaded_files) = CreateArena(100*sizeof(LoadedFileHandle), sizeof(LoadedFileHandle));
    *(target->loaded_tags) = CreateArena(10000*sizeof(Compiler::Tag), sizeof(Compiler::Tag));
    *(target->loaded_attributes) = CreateArena(10000*sizeof(Compiler::Attribute), sizeof(Compiler::Attribute));
    *(target->loaded_styles) = CreateArena(10000*sizeof(Compiler::Style), sizeof(Compiler::Style));
    *(target->loaded_selectors) = CreateArena(10000*sizeof(Compiler::Selector), sizeof(Compiler::Selector));
    *(target->static_combined_values) = CreateArena(100000*sizeof(char), sizeof(char));
    *(target->doms) = CreateArena(100*sizeof(DOM), sizeof(DOM));
    *(target->bound_expressions) = CreateArena(1000*sizeof(BoundExpression), sizeof(BoundExpression));
}

Runtime runtime;

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
                ArenaString* binding_text = binding->stub_string((void*)element->master);
                
                element->Text.temporal_text_length = binding_text->length;
                
                // Note(Leo): We do a length limited flatten since we dont want a \0
                element->Text.temporal_text = (char*)Alloc(dom->frame_arena, sizeof(char)*binding_text->length);
                Flatten(binding_text, element->Text.temporal_text, sizeof(char)*binding_text->length);
                
                FreeString(binding_text);
                
                break;
            }
            case(AttributeType::CLASS):
            {
                char* class_string = NULL;
                int class_string_length = curr_attribute->Text.value_length;
                int class_binding_length = 0;
                
                if(curr_attribute->Text.binding_id)
                {
                    BoundExpression* binding = GetBoundExpression(curr_attribute->Text.binding_id);
                    assert(binding->type == BoundExpressionType::ARENA_STRING);
                    assert(element->master);
                    ArenaString* binding_text = binding->stub_string((void*)element->master);
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