#include <cstring>
#include <map>

#include "arena.h"
#include "arena_string.h"
#include "file_system.h"

// flag for page_switch to save dom state
#define save_dom() 1 << 0

#pragma once

// Aligns the fiven pointer to where the type wants it to start in memory
#define align_mem(ptr, type) (type*)((uintptr_t)ptr + alignof(type) - ((uintptr_t)ptr % alignof(type)))

// Aligns the given pointer to a pointer aligned address (8 byte aligned)
#define align_ptr(ptr) ((void*)((uintptr_t)ptr + alignof(ptr) - ((uintptr_t)ptr % alignof(ptr))))

struct LinkedPointer
{
    LinkedPointer* next;
    void* data;  
};

struct Component 
{
    int generation;
    int file_id;
};

struct DOM
{
    Arena* static_cstrings;
    Arena* cached_cstrings;
    Arena* dynamic_cstrings;
    Arena* strings;
    
    Arena* pointer_arrays;
    
    Arena* elements;
    Arena* attributes;
    Arena* bound_expressions;
    Arena* bound_vars;
    Arena* generations;
    
    Arena* styles;
    Arena* selectors;
    
    Arena* changed_que;
    
    Arena* frame_arena; // Composted every frame
};

struct Runtime
{
    Arena* loaded_files;

    Arena* loaded_tags;
    Arena* loaded_attributes;
    Arena* loaded_styles;
    Arena* loaded_selectors;
    Arena* static_combined_values;
};

struct Style 
{
    int id;
    
    
};

struct Selector
{
    int id;
    int style_count;
    
    char* name; // \0 terminated
    Style** styles; // All the styles selected by this selector
};

enum class AttributeType
{
NONE,
CUSTOM, // For user defined args, name goes in value. Arg goes in Attribute binding
TEXT,
STYLE,
CLASS,
COLUMNS, // For grid element
ROWS, // For grid element
ROW, // For any element to specify which row/column of a parent grid it wants to be in
COLUMN,
SRC, // For the VIDEO and IMG tags
COMP_ID, // For custom components
ON_CLICK,
};

struct Attribute
{
    Attribute* next_attribute;
    AttributeType type;
    char* static_value; // \0 terminated string with printf formatting to put the binding value in.
    int value_length;
    int binding_id;
    
    // Cache //
    char* cached_value;
};

enum class ElementType 
{
    NONE,
    ROOT,
    TEXT,
    DIV,
    CUSTOM,
    GRID,
    IMG,
    VIDEO,
};

struct Element 
{
    void* master;
    
    int id;
    int flags;
    ElementType type;
    
    // Note(Leo): Attributes are not continuous in memory
    Attribute* first_attribute;
    int num_attributes;
    
    Element* parent;
    Element* next_sibling;
    Element* first_child;
    
    // Cache related things //
    Style* cached_final_style;
    Style* cached_pre_attrib_style; // Style after compiling together all selected styles but before compiling Style="" 

    Style* cached_final_sizing;
};



struct Generation 
{
    int generation_id;
    Element* generation_head; // The parent element of elements from this gen
    
    std::map<int, LinkedPointer*>* expression_sub_map; // Maps expr ids to subsections of the subscribers list containing Attributes subcribed to the given expr
    
    LinkedPointer* subscribers_head;
};

// Types of bound functions
typedef void (*SubscribedStubVoid)(); 
typedef ArenaString* (*SubscribedStubString)(); 
typedef void (*SubscribedCompStubVoid)(void*); 
typedef ArenaString* (*SubscribedCompStubString)(void*); 

struct BoundExpression 
{
    int id;
    union {
        SubscribedStubVoid stub_void;
        SubscribedStubString stub_string;
        SubscribedCompStubVoid comp_stub_void;
        SubscribedCompStubString comp_stub_string;
    };
    
    
    // TODO(Leo): Change this to use the generation system instead
    //Element** subscribed_elements;
    //int subscriber_count;
};

BoundExpression* register_bound_expr(SubscribedStubVoid, int id);
BoundExpression* register_bound_expr(SubscribedStubString, int id);
BoundExpression* register_bound_expr(SubscribedCompStubVoid, int id);
BoundExpression* register_bound_expr(SubscribedCompStubString, int id);

void subscribe_to(BoundExpression* expr, int target_bound_id);


struct BoundVariable
{
    int id;
    BoundExpression** subscribers;
    int subscriber_count;
};

extern Runtime runtime;

void NotifyVariableSubscirbers(BoundVariable*);
void NotifyExpressionSubscribers(BoundExpression*);

// Returns the first attribute of a given type that appears in an element's attribtue list.
Attribute* GetAttribute(Element* element, AttributeType searched_type);

// Note(Leo): Name should be NULL terminated
Selector* GetSelectorFromName(char* name);

Element* CreateElement(DOM* dom, SavedTag* tag_template);

void InitDOM(Arena* master_arena, DOM* target);

void CalculateStyles(DOM* dom);
void BuildRenderQue(DOM* dom);
void Draw(DOM* dom);

// Tells the program master to switch the page of the specified DOM to the specified one
// Flags may tell the master wether to delete the curernt DOM or keep it and save the state
void SwitchPage(DOM* dom, int id, int flags);

// Expects a DOM with cleared element Arena, instances all the 
void InstancePage(DOM* target_dom, int id);

// Instances a component of the type specified by id, returns a pointer to the component's object
void* InstanceComponent(DOM* target_dom, Element* parent, int id);

void* AllocComponent(DOM* dom, int size, int file_id);

void bound_var_changed(int changed_var_id);
void bound_var_changed(int changed_var_id, void* d_void);

// Runtime Function Definitions //
void LoadFromBin(char* file_path, Runtime* runtime);

LoadedFileHandle* GetFileFromId(int id);
LoadedFileHandle* GetFileFromName(const char* name);