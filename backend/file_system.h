
#pragma once
// Filsystem types
#include "compiler.h"

struct PageFileHeader
{
    int flags;
    int file_id;
    int first_tag_index;
    int tag_count;
    int first_template_index;
    int template_count;
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

struct SavedTag
{
    Compiler::TagType type;
    int tag_id; // Given by parser
    int global_id; 
    
    int first_attribute_index;
    int num_attributes;
    
    int parent_index;
    int next_sibling_index;
    int first_child_index;
};

struct SavedTemplate
{
    int first_tag_index;
    int tag_count;
    int id;
};

struct saved_attr_on_click_body
{
    int binding_id;
};

struct saved_attr_on_focus_body
{
    int binding_id;
};

struct saved_attr_this_body
{
    int binding_id;
};

struct saved_attr_condition_body
{
    int binding_id;
};

struct saved_attr_comp_id_body 
{
    int id;
};

struct saved_attr_loop_body
{
    int array_binding;
    int length_binding;
    int template_id;
};

struct saved_attr_text_like_body
{
    int value_index;
    int value_length;
    int binding_position; // Position where the binding gets inserted into the value (for attributes like text)
    int binding_id; 
};

struct saved_attr_custom_body : saved_attr_text_like_body
{
    int name_index;
    int name_length;
};

struct SavedAttribute 
{
    Compiler::AttributeType type;
    
    union
    {
        saved_attr_comp_id_body CompId;
        saved_attr_text_like_body Text;
        saved_attr_custom_body Custom;
        saved_attr_on_click_body OnClick;
        saved_attr_on_focus_body OnFocus;
        saved_attr_this_body This;
        saved_attr_condition_body Condition;
        saved_attr_loop_body Loop;
    };
};

//struct SavedAttribute 
//{
//    Compiler::AttributeType type;
//    int attribute_value_index;
//    int value_length;
//    int binding_position; // Position where the binding gets inserted into the value (for attributes like text)
//    int binding_id;
//};

struct FileSearchResult
{
    char* file_path; // Full path built by the search
    char* file_name; // Substring of the full path containing only the file name
};

struct BodyTemplate
{
    int id;
    Compiler::Tag* first_tag;
    int tag_count;
};

struct LoadedFileHandle
{
    int file_id;
    int flags;
    PageFileHeader file_info;
    Compiler::Tag* root_tag;
    BodyTemplate* first_template;
    Compiler::Selector* first_selector;
    Compiler::Style* first_style;
};

// Filesystem Functions

void SavePage(Compiler::AST* saved_tree, Compiler::LocalStyles* saved_styles, const char* file_name, int file_id, int flags = 0);

LoadedFileHandle LoadPage(FILE* file, Arena* tags, Arena* templates, Arena* attributes, Arena* styles, Arena* selectors, Arena* values);

// Searches a directory recursively for files ending in a specified extension. Appends a zeroed FileSearchResult after the last entry to indicate the end
void SearchDir(Arena* results, Arena* result_values, const char* dir_name, const char* file_extension);

