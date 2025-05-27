#include "compiler.h"
using namespace Compiler;

#include "file_system.h"
#include <cstring>
#include <assert.h>

int push_val_to_combined_arena(Arena* combined_values, char* value, int value_length);

//Note(Leo): file_name MUST be null terminated!!
void SavePage(AST* saved_tree, LocalStyles* saved_styles, const char* file_name, int file_id, int flags)
{
    //Note(Leo): we +1 every index so that index 0 can represent NULL ptrs, minus 1 off the index when loading to account
    #define get_index(arena_ptr, pointer) ((uintptr_t)pointer ? ((((uintptr_t)pointer) - arena_ptr->mapped_address)/sizeof(*pointer)) + 1 : 0)

    int current_index = 0; // Current index in bytes into the file, PageFileHeader is always at zero
    FILE* out_file = fopen(file_name, "wb+");
    
    if(!out_file)
    {
        return;
    }
    
    Arena combined_values_arena = CreateArena(10000*sizeof(char), sizeof(char));
    
    // Note(Leo): index zero of the values arena should be a zero for any NULL values to point too
    Alloc(&combined_values_arena, sizeof(char)); 
    
    PageFileHeader header = {};
        
    fwrite(&header, sizeof(PageFileHeader), 1, out_file);
    current_index += sizeof(PageFileHeader);
    
    header.flags = flags;
    header.file_id = file_id;
    header.first_tag_index = current_index;
    
    Tag* curr_tag = (Tag*)saved_tree->tags->mapped_address;
    SavedTag added_tag;
        
    while(curr_tag->tag_id != 0)
    {
        added_tag.tag_id = curr_tag->tag_id;
        added_tag.global_id = curr_tag->global_id;
        added_tag.first_attribute_index = get_index(saved_tree->attributes, curr_tag->first_attribute);
        added_tag.num_attributes = curr_tag->num_attributes;
        
        added_tag.type = curr_tag->type;
        
        added_tag.parent_index = get_index(saved_tree->tags, curr_tag->parent);
        added_tag.next_sibling_index = get_index(saved_tree->tags, curr_tag->next_sibling);
        
        // Each shouldnt have children
        if(added_tag.type != TagType::EACH)
        {
            added_tag.first_child_index = get_index(saved_tree->tags, curr_tag->first_child);
        }
        
        fwrite(&added_tag, sizeof(SavedTag), 1, out_file);
        
        current_index += sizeof(SavedTag);
        header.tag_count++;
        curr_tag++;
    }
    
    RegisteredTemplate* curr_template = (RegisteredTemplate*)saved_tree->templates->mapped_address;
    
    int template_count = (RegisteredTemplate*)saved_tree->templates->next_address - (RegisteredTemplate*)saved_tree->templates->mapped_address;
    
    // Note(Leo): +1 to account for alignment
    void* saved_template_mem = AllocScratch((template_count + 1)*sizeof(SavedTemplate)); 
    Arena saved_templates = CreateArenaWith(align_mem(saved_template_mem, SavedTemplate), template_count*sizeof(SavedTemplate), sizeof(SavedTemplate));
    
    // Saving all tags inside of templates
    while(curr_template->template_id != 0)
    {
        int tag_index_offset = header.tag_count;
        
        SavedTemplate* created_template = (SavedTemplate*)Alloc(&saved_templates, sizeof(SavedTemplate), zero());
        created_template->id = curr_template->template_id;
        created_template->first_tag_index = tag_index_offset + 1; // Todo(Leo): Is this correct??
    
        curr_tag = (Tag*)curr_template->tags.mapped_address;
        SavedTag added_tag;
        while(curr_tag->tag_id != 0)
        {
            added_tag = {};
            added_tag.tag_id = curr_tag->tag_id;
            added_tag.global_id = curr_tag->global_id;
            added_tag.first_attribute_index = get_index(saved_tree->attributes, curr_tag->first_attribute);
            added_tag.num_attributes = curr_tag->num_attributes;
            
            added_tag.type = curr_tag->type;
            
            // Base tags in a template should have parent set to null
            if(curr_tag->parent->type != TagType::EACH)
            {
                // Indexe all need to be offset cos the static tags have already been added
                added_tag.parent_index = curr_tag->parent ? tag_index_offset + get_index((&curr_template->tags), curr_tag->parent) : 0;
            }
            
            added_tag.next_sibling_index = curr_tag->next_sibling ? tag_index_offset + get_index((&curr_template->tags), curr_tag->next_sibling) : 0;
            added_tag.first_child_index = curr_tag->first_child ? tag_index_offset + get_index((&curr_template->tags), curr_tag->first_child) : 0;
            
            fwrite(&added_tag, sizeof(SavedTag), 1, out_file);
        
            current_index += sizeof(SavedTag);
            header.tag_count++;
            created_template->tag_count++;
            curr_tag++;
        }
        curr_template++;
    }
    
    Alloc(&saved_templates, sizeof(SavedTemplate), zero()); // Stopper
    
    header.first_template_index = current_index;
    
    SavedTemplate* curr_saved_template = (SavedTemplate*)saved_templates.mapped_address;
    while(curr_saved_template->id != 0)
    {
        fwrite(curr_saved_template, sizeof(SavedTemplate), 1, out_file);
        header.template_count++;
        current_index += sizeof(SavedTemplate);
        curr_saved_template++;
    }
    
    DeAllocScratch(saved_template_mem);
    
    header.first_attribute_index = current_index;
    
    Attribute* curr_attribute = (Attribute*)saved_tree->attributes->mapped_address;
    SavedAttribute added_attribute;
    while(curr_attribute->type != AttributeType::NONE)
    {
        added_attribute.type = curr_attribute->type;
        switch(curr_attribute->type)
        {
            case(AttributeType::ON_CLICK):
                added_attribute.OnClick.binding_id = curr_attribute->OnClick.binding_id;
                break;
            case(AttributeType::ON_FOCUS):
                added_attribute.OnFocus.binding_id = curr_attribute->OnFocus.binding_id;
                break;
            case(AttributeType::THIS_ELEMENT):
                added_attribute.This.binding_id = curr_attribute->This.binding_id;
                break;
            case(AttributeType::COMP_ID):
                added_attribute.CompId.id = curr_attribute->CompId.id;
                break;
            case(AttributeType::CONDITION):
            {
                added_attribute.Condition.binding_id = curr_attribute->Condition.binding_id;
                break;
            }
            case(AttributeType::LOOP):
            {
                added_attribute.Loop.array_binding = curr_attribute->Loop.array_binding;
                added_attribute.Loop.length_binding = curr_attribute->Loop.length_binding;
                added_attribute.Loop.template_id = curr_attribute->Loop.template_id;
                break;
            }
            case(AttributeType::FOCUSABLE):
            {
                break;
            }
            case(AttributeType::CUSTOM):
            {
                added_attribute.Custom.name_index = push_val_to_combined_arena(&combined_values_arena, curr_attribute->Custom.name, curr_attribute->Custom.name_length);
                added_attribute.Custom.name_length = curr_attribute->Custom.name_length;
                // Fill the shared parameters
                goto text_like;
                break;
            }
            default: // Text like attributes
            {
                text_like:
                added_attribute.Text.value_index = push_val_to_combined_arena(&combined_values_arena, curr_attribute->Text.value, curr_attribute->Text.value_length);
                added_attribute.Text.value_length = curr_attribute->Text.value_length;
                added_attribute.Text.binding_position = curr_attribute->Text.binding_position;
                added_attribute.Text.binding_id = curr_attribute->Text.binding_id;
                
                break;
            }
        }
        
        fwrite(&added_attribute, sizeof(SavedAttribute), 1, out_file);
        
        current_index += sizeof(SavedAttribute);
        header.attribute_count++;
        curr_attribute++;
    }
    
    header.first_style_index = current_index;
    
    Style* curr_style = (Style*)saved_styles->styles->mapped_address;
    
    while(curr_style->global_id != 0)
    {
        StringView style_name = {};
        // Grab the string view before writing to the font name members since they are all in a union
        memcpy(&style_name, &curr_style->font_name, sizeof(StringView));
        
        curr_style->saved_font_name.index = push_val_to_combined_arena(&combined_values_arena, style_name.value, style_name.len);
        curr_style->saved_font_name.length = style_name.len;
        
        fwrite(curr_style, sizeof(Style), 1, out_file);
        
            
        current_index += sizeof(Style);
        header.style_count++;
        curr_style++;
    }
    
    header.first_selector_index = current_index;
    Selector* curr_selector = (Selector*)saved_styles->selectors->mapped_address;
    SavedSelector added_selector;
    
    while(curr_selector->global_id != 0)
    {
        added_selector.global_id = curr_selector->global_id;
        added_selector.name_index = push_val_to_combined_arena(&combined_values_arena, curr_selector->name, curr_selector->name_length);
        added_selector.name_length = curr_selector->name_length;
            
        added_selector.num_styles = curr_selector->num_styles;
        
        for(int i = 0; i < curr_selector->num_styles; i++)
        {
            added_selector.style_ids[i] = curr_selector->style_ids[i];
        }
        
        fwrite(&added_selector, sizeof(SavedSelector), 1, out_file);
        
        current_index += sizeof(SavedSelector);
        header.selector_count++;
        curr_selector++;
    }
    
    header.first_value_index = current_index;
    
    //fwrite((char*)combined_values_arena.mapped_address, sizeof(char), get_index((&combined_values_arena), (char*)combined_values_arena.next_address), out_file);
    header.values_length = (combined_values_arena.next_address - combined_values_arena.mapped_address) / sizeof(char);
    fwrite((char*)combined_values_arena.mapped_address, sizeof(char), header.values_length, out_file);
    
    
    // Re-write header with final values
    fseek(out_file, 0, SEEK_SET);
    fwrite(&header, sizeof(PageFileHeader), 1, out_file);
    fclose(out_file);
    
}

#define debug_load 0
// Note(Leo): We cannot acces values to print for debugging until the end of the FN since thats when the values get read in.
LoadedFileHandle LoadPage(FILE* file, Arena* tags, Arena* templates, Arena* attributes, Arena* styles, Arena* selectors, Arena* values)
{
    #define get_pointer(base_ptr, index, type) (index ? (type*)(base_ptr + sizeof(type)*(index - 1)) : NULL)
    
    LoadedFileHandle handle;
    
    assert(file);
    if(!file)
    {
        handle.file_id = 0;
        handle.flags = 0;
        handle.root_tag = NULL;
    
        return handle;
    }
    
    uintptr_t base_tag = tags->next_address;
    uintptr_t base_template = templates->next_address;
    uintptr_t base_attribute = attributes->next_address;
    uintptr_t base_style = styles->next_address;
    uintptr_t base_selector = selectors->next_address;
    uintptr_t base_value = values->next_address;
    
    PageFileHeader header = PageFileHeader();
    memset(&header, 0, sizeof(PageFileHeader));
    
    fread(&header, sizeof(PageFileHeader), 1, file);
    
    handle.file_id = header.file_id;
    handle.flags = header.flags;
    handle.root_tag = (Tag*)tags->next_address;
    handle.first_template = (BodyTemplate*)templates->next_address;
    handle.first_selector = (Selector*)selectors->next_address;
    handle.first_style = (Style*)styles->next_address;
    handle.file_info = header;
    
#if debug_load
    printf("\n --== File Header ==--\n");
    printf("\t Flags: %d\n", header.flags);
    printf("\t File ID: %d\n", header.file_id);
    printf("\t First Tag: %d, # of Tags: %d\n", header.first_tag_index, header.tag_count);
    printf("\t First Template: %d, # of Templates: %d\n", header.first_template_index, header.template_count);
    printf("\t First Atr: %d, # of Atribs: %d\n", header.first_attribute_index, header.attribute_count);
    printf("\t First Style: %d, # of Styles: %d\n", header.first_style_index, header.style_count);
    printf("\t First Selector: %d, # of Selectors: %d\n", header.first_selector_index, header.selector_count);
    printf("\t First Value: %d, # of Values: %d\n", header.first_value_index, header.values_length);
    
    printf("\n --== Tags ==--\n");
#endif    

    SavedTag read_tag;
    Tag* added_tag;
    
    for(int i = 0; i < header.tag_count; i++)
    {
        fread(&read_tag, sizeof(SavedTag), 1, file);
        added_tag = (Tag*)Alloc(tags, sizeof(Tag));
        
        added_tag->type = read_tag.type;
        added_tag->tag_id = read_tag.tag_id;
        added_tag->global_id = read_tag.global_id;
        added_tag->first_attribute = get_pointer(base_attribute, read_tag.first_attribute_index, Attribute);
        added_tag->num_attributes = read_tag.num_attributes;
        
        added_tag->parent = get_pointer(base_tag, read_tag.parent_index, Tag);
        added_tag->next_sibling = get_pointer(base_tag, read_tag.next_sibling_index, Tag);
        added_tag->first_child = get_pointer(base_tag, read_tag.first_child_index, Tag);
#if debug_load
        printf(" --Tag--\n");
        printf("\tTag Id: %d Tag type: %d\n", added_tag->tag_id, (int)added_tag->type);
        printf("\tParent: %d, Sibling: %d, Child: %d\n", read_tag.parent_index + 1, read_tag.next_sibling_index + 1, read_tag.first_child_index + 1);
        printf("\t# of Attributes: %d\n", added_tag->num_attributes);
#endif
    }
    // Push empty divider tag to mark end of file
    (Tag*)Alloc(tags, sizeof(Tag), zero());
    
    
    SavedAttribute read_attribute;
    Attribute* added_attribute;

#if debug_load
    printf("\n--== Templates ==--\n");
#endif

    BodyTemplate* added_template;
    SavedTemplate read_template;

    for(int i = 0; i < header.template_count; i++)
    {
        fread(&read_template, sizeof(SavedTemplate), 1, file);
        // Note(Leo): Templates are indexed in the arena by their ID 
        
        // See if we need to allocated more for this template's slot to be valid
        BodyTemplate* added_template = get_pointer(((BodyTemplate*)templates->mapped_address), read_template.id, BodyTemplate);
        // Note(Leo): <= since if we are on the next_address then the memory for the object is over it
        if((BodyTemplate*)templates->next_address <= added_template)
        {
            int required = added_template - (BodyTemplate*)templates->next_address;
            if(required == 0)
            {
                required++;
            }
            required++; 
            
            Alloc(templates, required*sizeof(BodyTemplate), zero());
        }
                
        added_template->id = read_template.id;
        added_template->first_tag = get_pointer(base_tag, read_template.first_tag_index, Tag);
        added_template->tag_count = read_template.tag_count;
#if debug_load
        printf(" --Template--\n");
        printf("\tTemplate id: %d\n", added_template->id);
        printf("\tFirst Tag: %d\n", read_template.first_tag_index);
        printf("\tTag Count: %d\n", read_template.tag_count);
#endif
    }

#if debug_load
    printf("\n--== Attributes ==--\n");
#endif
    
    for(int i = 0; i < header.attribute_count; i++)
    {
        fread(&read_attribute, sizeof(SavedAttribute), 1, file);
        added_attribute = (Attribute*)Alloc(attributes, sizeof(Attribute));
        
        added_attribute->type = read_attribute.type;
        switch(read_attribute.type)
        {
            case(AttributeType::COMP_ID):
                added_attribute->CompId.id = read_attribute.CompId.id;            
                break;
            case(AttributeType::CUSTOM):
                added_attribute->Custom.name = get_pointer(base_value, read_attribute.Custom.name_index, char);
                added_attribute->Custom.name_length = read_attribute.Custom.name_length;
                goto text_like;
                break;
            case(AttributeType::ON_CLICK):
            {
                added_attribute->OnClick.binding_id = read_attribute.OnClick.binding_id;
                break;
            }
            case(AttributeType::ON_FOCUS):
            {
                added_attribute->OnFocus.binding_id = read_attribute.OnFocus.binding_id;
                break;
            }
            case(AttributeType::FOCUSABLE):
            {
                break;
            }
            case(AttributeType::THIS_ELEMENT):
            {
                added_attribute->This.binding_id = read_attribute.This.binding_id;
                break;
            }
            case(AttributeType::CONDITION):
            {
                added_attribute->Condition.binding_id = read_attribute.Condition.binding_id;
                break;
            }
            case(AttributeType::LOOP):
            {
                added_attribute->Loop.array_binding = read_attribute.Loop.array_binding;
                added_attribute->Loop.length_binding = read_attribute.Loop.length_binding;
                added_attribute->Loop.template_id = read_attribute.Loop.template_id;
                break;
            }
            default:
                text_like:
                added_attribute->Text.value = get_pointer(base_value, read_attribute.Text.value_index, char);
                added_attribute->Text.value_length = read_attribute.Text.value_length;
                added_attribute->Text.binding_position = read_attribute.Text.binding_position;
                added_attribute->Text.binding_id = read_attribute.Text.binding_id;
                break;
        }
        
#if debug_load
        printf(" --Attribute--\n");
        printf("\tType: %d\n", (int)added_attribute->type);
        printf("\tVal length: %d\n", (int)added_attribute->Text.value_length);
        printf("\tBinding id: %d\n", added_attribute->Text.binding_id);
#endif
    }
    
#if debug_load
    printf("\n--== Styles ==--\n");
#endif
    
    Style* added_style;
    
    for(int i = 0; i < header.style_count; i++)
    {
        added_style = (Style*)Alloc(styles, sizeof(Style));
        fread(added_style, sizeof(Style), 1, file);
        
        // Grabbing the length and pointer to the value for the font name before writing to the stringview since they
        //      are in a union
        int name_len = added_style->saved_font_name.length;
        char* name_value = get_pointer(base_value, added_style->saved_font_name.index, char);
        added_style->font_name.value = name_value;
        added_style->font_name.len = name_len;
        
#if debug_load
        printf(" --Style--\n");
        printf("\tGlobal Id: %d\n", added_style->global_id);
        printf("\tPriority: %d\n", added_style->priority);
#endif
    }
    
    SavedSelector read_selector;
    Selector* added_selector;

#if debug_load
    printf("\n--== Selectors ==--\n");
#endif

    for(int i = 0; i < header.selector_count; i++)
    {
        fread(&read_selector, sizeof(SavedSelector), 1, file);
        added_selector = (Selector*)Alloc(selectors, sizeof(Selector));
        
        added_selector->global_id = read_selector.global_id;
        added_selector->name = get_pointer(base_value, read_selector.name_index, char);
        added_selector->name_length = read_selector.name_length;
        
        for(int j = 0; j < read_selector.num_styles; j++)
        {
            added_selector->style_ids[j] = read_selector.style_ids[j];
        }
        
        added_selector->num_styles = read_selector.num_styles;
#if debug_load
        printf(" --Selector--\n");
        printf("\tGlobal Id: %d\n", added_selector->global_id);
        printf("\t# of Styles: %d\n", added_selector->num_styles);
        printf("\tName length: %d\n", added_selector->name_length);
#endif
    }
    
    char* values_blob = (char*)Alloc(values, header.values_length*sizeof(char));
    fread(values_blob, sizeof(char), header.values_length, file);

    // Push empty structs onto each arena to seperate future loaded file records
    Alloc(tags, sizeof(Tag), zero());
    Alloc(attributes, sizeof(Attribute), zero());
    Alloc(styles, sizeof(Style), zero());
    Alloc(selectors, sizeof(Selector), zero());
    Alloc(values, sizeof(char), zero());

    return handle;
}

// Returns the index of the value start
int push_val_to_combined_arena(Arena* combined_values, char* value, int value_length)
{
    char* start = (char*)Alloc(combined_values, value_length*sizeof(char));
    memcpy(start, value, value_length*sizeof(char));
    return get_index(combined_values, start);
}


#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
// Windows definitions for memory management
#include <windows.h>
void search_dir_r(Arena* results, Arena* result_values, const char* dir_ref, const char* file_extension)
{
    int parent_len = strlen(dir_ref);
    WIN32_FIND_DATAA item;
    char* curr_dir_search = (char*)AllocScratch(sizeof(char)*(parent_len + 3), no_zero()); // +3 to fit \, * and \0
    memcpy(curr_dir_search, dir_ref, parent_len*sizeof(char));
    curr_dir_search[parent_len] = '\\';
    curr_dir_search[parent_len + 1] = '*';
    curr_dir_search[parent_len + 2] = '\0';
    
    HANDLE hFind = FindFirstFileA(curr_dir_search, &item);
    DeAllocScratch(curr_dir_search);
    
    bool success = false;
    if(hFind != INVALID_HANDLE_VALUE)
    {
        success = true;
    }
    while(success)
    {
        int len = strlen(item.cFileName);
        if(len <= 2) // avoid .. and . dirs
        {
            success = FindNextFile(hFind, &item);
            continue;
        }
        if(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) // indicates a directory
        {
            char* new_dir_name = (char*)AllocScratch(sizeof(char)*(len + parent_len + 2), no_zero());
            memcpy(new_dir_name, dir_ref, parent_len*sizeof(char));
            new_dir_name[parent_len] = '\\';
            memcpy((new_dir_name + parent_len + 1), item.cFileName, len*sizeof(char));
            new_dir_name[len + parent_len + 1] = '\0';
        
            search_dir_r(results, result_values, new_dir_name, file_extension);
            
            DeAllocScratch(new_dir_name);
        }
        else // Indicates a file
        {
            
            if (!strstr(item.cFileName, file_extension))
            {
                success = FindNextFile(hFind, &item);
                continue;
            }
            FileSearchResult* added = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult));

            added->file_path = (char*)Alloc(result_values, (parent_len + len + 2) * sizeof(char)); // +2 to fit \ and \0

            memcpy(added->file_path, dir_ref, parent_len * sizeof(char));
            added->file_path[parent_len] = '\\';
            memcpy((added->file_path + parent_len + 1), item.cFileName, len * sizeof(char));
            added->file_path[len + parent_len + 1] = '\0';

            added->file_name = added->file_path + (parent_len + 1);

        }
        success = FindNextFile(hFind, &item);
    }
}

void SearchDir(Arena* results, Arena* result_values, const char* dir_name, const char* file_extension)
{
    search_dir_r(results, result_values, dir_name, file_extension);
    FileSearchResult* end = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult), zero());
}

#elif defined(__linux__) && !defined(_WIN32) && !defined(__ANDROID__)
#include <dirent.h>
void search_dir_r(Arena* results, Arena* result_values, const char* dir_ref, const char* file_extension)
{
    DIR* start = opendir(dir_ref);
    dirent* item = readdir(start);
    int parent_len = strlen(dir_ref);
    while(item)
    {
        switch(item->d_type)
        {
            case(DT_REG):
            {
                if(!strstr(item->d_name, file_extension))
                {
                    break;
                }
                int len = strlen(item->d_name);
                
                FileSearchResult* added = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult));
                
                added->file_path = (char*)Alloc(result_values, (parent_len + len + 2)*sizeof(char)); // +2 to fit / and \0
                
                memcpy(added->file_path, dir_ref, parent_len*sizeof(char));
                added->file_path[parent_len] = '/';
                memcpy((added->file_path + parent_len + 1), item->d_name, len*sizeof(char));
                added->file_path[len + parent_len + 1] = '\0';
                
                added->file_name = added->file_path + (parent_len + 1);
                break;
            }
            case(DT_DIR):
            {
                int len = strlen(item->d_name);
                if(len <= 2)  // Avoid .. and . dirs
                {
                break;
                }
                char* new_dir_name = (char*)AllocScratch(sizeof(char)*(len + parent_len + 2), no_zero());
                memcpy(new_dir_name, dir_ref, parent_len*sizeof(char));
                new_dir_name[parent_len] = '/';
                memcpy((new_dir_name + parent_len + 1), item->d_name, len*sizeof(char));
                new_dir_name[len + parent_len + 1] = '\0';
            
                search_dir_r(results, result_values, new_dir_name, file_extension);
                
                DeAllocScratch(new_dir_name);
                break;
            }
            default:
                break;
        }
        item = readdir(start);
    }
}

void SearchDir(Arena* results, Arena* result_values, const char* dir_name, const char* file_extension)
{
    search_dir_r(results, result_values, dir_name, file_extension);
    FileSearchResult* end = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult), zero());
}


#elif defined(__ANDROID__)
#endif
