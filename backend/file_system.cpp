#include "compiler.h"
using namespace Compiler;

#include "file_system.h"
#include <cstring>
#include <assert.h>

int push_val_to_combined_arena(Arena* combined_values, char* value, int value_length);

//Note(Leo): file_name MUST be null terminated!!
void SavePage(AST* saved_tree, LocalStyles* saved_styles, const char* file_name, int file_id, int flags)
{
    #define get_index(arena_ptr, pointer) (uintptr_t)pointer ? (((uintptr_t)pointer) -  arena_ptr->mapped_address)/sizeof(*pointer) : 0

    int current_index = 0; // Current index in bytes into the file, PageFileHeader is always at zero
    FILE* out_file = fopen(file_name, "wb+");
    
    if(!out_file)
    {
        return;
    }
    
    Arena combined_values_arena = CreateArena(10000*sizeof(char), sizeof(char));
    
    // Note(Leo): index zero of the combined values should be a zero for any NULL values to point too
    Alloc(&combined_values_arena, sizeof(char)); 
    
    PageFileHeader header = PageFileHeader();
    memset(&header, 0, sizeof(PageFileHeader));
        
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
        added_tag.first_attribute_index = get_index(saved_tree->attributes, curr_tag->first_attribute);
        added_tag.num_attributes = curr_tag->num_attributes;
        
        added_tag.type = curr_tag->type;
        
        added_tag.parent_index = get_index(saved_tree->tags, curr_tag->parent);
        added_tag.next_sibling_index = get_index(saved_tree->tags, curr_tag->next_sibling);
        added_tag.first_child_index = get_index(saved_tree->tags, curr_tag->first_child);
        
        fwrite(&added_tag, sizeof(SavedTag), 1, out_file);
        
        current_index += sizeof(SavedTag);
        header.tag_count++;
        curr_tag++;
    }
    
    header.first_attribute_index = current_index;
    
    Attribute* curr_attribute = (Attribute*)saved_tree->attributes->mapped_address;
    SavedAttribute added_attribute;
    while(curr_attribute->type != AttributeType::NONE)
    {
        added_attribute.type = curr_attribute->type;
        switch(curr_attribute->type)
        {
            case(AttributeType::COMP_ID):
                added_attribute.attribute_value_index = 0;
                break;
            default:
                added_attribute.attribute_value_index = push_val_to_combined_arena(&combined_values_arena, curr_attribute->attribute_value, curr_attribute->value_length);
                break;
        }
        
        added_attribute.value_length = curr_attribute->value_length;
        added_attribute.binding_position = curr_attribute->binding_position;
        added_attribute.binding_id = curr_attribute->binding_id;
        
        fwrite(&added_attribute, sizeof(SavedAttribute), 1, out_file);
        
        current_index += sizeof(SavedAttribute);
        header.attribute_count++;
        curr_attribute++;
    }
    
    header.first_style_index = current_index;
    
    Style* curr_style = (Style*)saved_styles->styles->mapped_address;
    SavedStyle added_style;
    
    while(curr_style->global_id != 0)
    {
        added_style.global_id = curr_style->global_id;
        added_style.width = curr_style->width;
        added_style.max_width = curr_style->max_width;
        added_style.height = curr_style->height;
        added_style.width = curr_style->max_height;
        
        fwrite(&added_style, sizeof(SavedStyle), 1, out_file);
            
        current_index += sizeof(SavedStyle);
        header.style_count++;
        curr_style++;
    }
    
    header.first_selector_index = current_index;
    Selector* curr_selector = (Selector*)saved_styles->selectors->mapped_address;
    SavedSelector added_selector;
    
    while(curr_selector->global_id != 0)
    {
        added_selector.global_id = curr_selector->global_id;
        added_selector.name_index = push_val_to_combined_arena((&combined_values_arena), curr_selector->name, curr_selector->name_length);
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
    
    fwrite((char*)combined_values_arena.mapped_address, sizeof(char), get_index((&combined_values_arena), (char*)combined_values_arena.next_address), out_file);
    
    header.values_length = get_index((&combined_values_arena), ((char*)combined_values_arena.next_address));
    
    // Re-write header with final values
    fseek(out_file, 0, SEEK_SET);
    fwrite(&header, sizeof(PageFileHeader), 1, out_file);
    fclose(out_file);
    
}

#define debug_load 1
LoadedFileHandle LoadPage(FILE* file, Arena* tags, Arena* attributes, Arena* styles, Arena* selectors, Arena* values)
{
    #define get_pointer(base_ptr, index, type) (type*)(base_ptr + sizeof(type)*index)
    
    LoadedFileHandle handle;
    
    assert(file);
    if(!file)
    {
        handle.file_id = 0;
        handle.flags = 0;
        handle.root_tag = NULL;
    
        return handle;
    }

    // Push empty structs onto each arena to seperate previously loaded file records
    Alloc(tags, sizeof(Tag), zero());
    uintptr_t base_tag = tags->next_address;
    Alloc(attributes, sizeof(Attribute), zero());
    uintptr_t base_attribute = attributes->next_address;
    Alloc(styles, sizeof(Style), zero());
    uintptr_t base_style = styles->next_address;
    Alloc(selectors, sizeof(Selector), zero());
    uintptr_t base_selector = selectors->next_address;
    Alloc(values, sizeof(char), zero());
    uintptr_t base_value = values->next_address;
    
    PageFileHeader header = PageFileHeader();
    memset(&header, 0, sizeof(PageFileHeader));
    
    fread(&header, sizeof(PageFileHeader), 1, file);
    
    handle.file_id = header.file_id;
    handle.flags = header.flags;
    handle.root_tag = (Tag*)tags->next_address;
    handle.file_info = header;
    
#if debug_load
    printf("\n --== File Header ==--\n");
    printf("\t Flags: %d\n", header.flags);
    printf("\t File ID: %d\n", header.file_id);
    printf("\t First Tag: %d, # of Tags: %d\n", header.first_tag_index, header.tag_count);
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
                added_attribute->attribute_value = NULL;                
                break;
            default:
                added_attribute->attribute_value = get_pointer(base_value, read_attribute.attribute_value_index, char);
                break;
        }
        added_attribute->value_length = read_attribute.value_length;
        added_attribute->binding_position = read_attribute.binding_position;
        added_attribute->binding_id = read_attribute.binding_id;
#if debug_load
        printf(" --Attribute--\n");
        printf("\tType: %d\n", (int)added_attribute->type);
        printf("\tBinding id: %d\n", added_attribute->binding_id);
#endif
    }
    
#if debug_load
    printf("\n--== Styles ==--\n");
#endif
    
    SavedStyle read_style;
    Style* added_style;
    
    for(int i = 0; i < header.style_count; i++)
    {
        fread(&read_style, sizeof(SavedStyle), 1, file);
        added_style = (Style*)Alloc(styles, sizeof(Style));
        
        added_style->global_id = read_style.global_id;
        added_style->width = read_style.width;
        added_style->max_width = read_style.max_width;
        added_style->height = read_style.height;
        added_style->max_height = read_style.max_height;
#if debug_load
        printf(" --Style--\n");
        printf("\tGlobal Id: %d\n", added_style->global_id);
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
#endif
    }
    
    char* values_blob = (char*)Alloc(values, header.values_length*sizeof(char));
    fread(values_blob, sizeof(char), header.values_length, file);

// unrealiable due to null terminators being mixed into selector names
//#if debug_load
//    printf("\n--== Values Blob ==--\n");
//    printf(" Size: %lu\n", combined_values->next_address - combined_values->mapped_address);
//    char* end_of_blob = (char*)Alloc(combined_values, sizeof(char));
//    *end_of_blob = '\0';
//    printf("%s", (char*)combined_values->mapped_address);
//#endif
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
void search_dir_r(Arena* results, Arena* result_values, const dir_ref, const char* file_extension)
{
    int parent_len = strlen(dir_ref);
    WIN32_FIND_DATAA item;
    char* curr_dir_search = (char*)AllocScratch(sizeof(char)*(parent_len + 3)); // +3 to fit \, * and \0
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
            char* new_dir_name = (char*)AllocScratch(sizeof(char)*(len + parent_len + 2));
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
    FileSearchResult* end = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult));
    
    // Note(Leo): this step only needs to happen because arenas dont clear allocated memory, otherwise this could be removed
    memset(end, 0, sizeof(FileSearchResult));
}

#elif defined(__linux__) && !defined(_WIN32)
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
                char* new_dir_name = (char*)AllocScratch(sizeof(char)*(len + parent_len + 2));
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
    FileSearchResult* end = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult));
    
    // Note(Leo): this step only needs to happen because arenas dont clear allocated memory, otherwise this could be removed
    memset(end, 0, sizeof(FileSearchResult));
}


#endif
