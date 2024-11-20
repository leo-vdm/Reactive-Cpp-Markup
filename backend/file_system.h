#include "compiler.h"

#pragma once
// Filsystem types
struct PageFileHeader
{
    int file_id;
    int first_tag_index;
    int tag_count;
    int first_attribute_index;
    int attribute_count;
    int first_style_index;
    int style_count;
    int first_selector_index;
    int selector_count;
    int first_value_index;
    int values_length;
};

struct SavedSelector
{
    int global_id;
    int name_index;
    int name_length;
    int style_ids[MAX_STYLES_PER_SELECTOR];
    int num_styles;
};


struct SavedStyle
{
    int global_id;
    
    Measurement width, height;
    Measurement max_width, max_height;
};


struct SavedTag
{
    TagType type;
    int tag_id; // Given by parser
    
    int first_attribute_index;
    int num_attributes;
    
    int parent_index;
    int next_sibling_index;
    int first_child_index;
};


struct SavedAttribute 
{
    AttributeType type;
    int attribute_value_index;
    int value_length;
    int binding_position; // Position where the binding gets inserted into the value (for attributes like text)
    int binding_id;
};


struct FileSearchResult
{
    char* file_path; // Full path built by the search
    char* file_name; // Substring of the full path containing only the file name
};

// Filesystem Functions

void SavePage(AST* saved_tree, LocalStyles* saved_styles, char* file_name, int file_id);

void LoadPage(AST* target_tree, LocalStyles* target_styles, Arena* combined_values, char* file_name);

void SearchDir(Arena* results, Arena* result_values, char* dir_name, char* file_extension);