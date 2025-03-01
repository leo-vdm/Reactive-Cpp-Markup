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

    *(target->loaded_files) = CreateArena(100*sizeof(LoadedFileHandle), sizeof(LoadedFileHandle));
    *(target->loaded_tags) = CreateArena(10000*sizeof(Compiler::Tag), sizeof(Compiler::Tag));
    *(target->loaded_attributes) = CreateArena(10000*sizeof(Compiler::Attribute), sizeof(Compiler::Attribute));
    *(target->loaded_styles) = CreateArena(10000*sizeof(Compiler::Style), sizeof(Compiler::Style));
    *(target->loaded_selectors) = CreateArena(10000*sizeof(Compiler::Selector), sizeof(Compiler::Selector));
    *(target->static_combined_values) = CreateArena(100000*sizeof(char), sizeof(char));
    *(target->doms) = CreateArena(100*sizeof(DOM), sizeof(DOM));
}

Runtime runtime;

// InitializeRuntime(FileSearchResult* first_binary)



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
    
    register_subscriber_functions(main_dom);
    register_binding_subscriptions(main_dom);
    
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
    ResetArena(dom->generations);
    
    // Call page main and instance the page.
    call_page_main(dom, id);
    InstancePage(dom, id);
    
}