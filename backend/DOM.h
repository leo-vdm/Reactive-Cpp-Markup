#include <cstring>
#include <map>

#include "arena.h"
#include "arena_string.h"
#include "file_system.h"


#pragma once


// flag for page_switch to save dom state
#define save_dom() 1 << 0

// Aligns the given pointer to where the type wants it to start in memory
// Note(Leo): GCC doesnt need decltype inside of alignof but msvc does and will error otherwise
#define align_mem(ptr, type) (type*)((uintptr_t)ptr + alignof(type) - ((uintptr_t)ptr % alignof(type)))

// Aligns the given pointer to a pointer aligned address (8 byte aligned)
#define align_ptr(ptr) ((void*)((uintptr_t)ptr + alignof(decltype(ptr)) - ((uintptr_t)ptr % alignof(decltype(ptr)))))

// Aligns the given memory to the given alignment requirement 
#define align_mem_to(ptr, alignoftype) (void*)((uintptr_t)ptr + alignoftype - ((uintptr_t)ptr % alignoftype))

// Aligns the given offset to the given alignment requirement assuming that the offset is ofsetting off an aligned address 
#define align_offset_to(offset, alignoftype) (int)(offset + alignoftype - (offset % alignoftype))

// Ineger offset in bytes of a pointer from a 'base' pointer
#define offset_of(ptr_offset, ptr_base) (((uintptr_t)ptr_offset) - ((uintptr_t)ptr_base))

// Index of a pointer from a base pointer calculated using the size of type
#define index_of(ptr_offset, ptr_base, type) ((((uintptr_t)ptr_offset) - ((uintptr_t)ptr_base)) / sizeof(type))
// Element Flags //



struct LinkedPointer
{
    LinkedPointer* next;
    void* data;  
};

struct ElementMaster 
{
    int file_id;
};

struct Component : ElementMaster
{

};

struct Page : ElementMaster
{

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

    Arena* bound_vars;
/*    
    Arena* styles;
    Arena* selectors;
*/    
    Arena* changed_que;
    
    Arena* frame_arena; // Composted every frame
};

struct Runtime
{
    Arena* master_arena;
    Arena* doms;
    Arena* styles;
    Arena* selectors;
    Arena* strings;  

    Arena* loaded_files;

    Arena* loaded_tags;
    Arena* loaded_attributes;
    Arena* loaded_styles;
    Arena* loaded_selectors;
    Arena* static_combined_values;
    Arena* bound_expressions;
    
};

typedef Compiler::MeasurementType MeasurementType;
typedef Compiler::Measurement Measurement;

typedef Compiler::Padding Padding;
typedef Compiler::Margin Margin;
typedef Compiler::Corners Corners;

typedef Compiler::Color StyleColor;
typedef Compiler::TextWrapping TextWrapping;
typedef Compiler::ClipStyle ClipStyle;
typedef Compiler::DisplayType DisplayType;

typedef Compiler::Style Style;


// Note(Leo): Same struct as Style but with priority numbers for each member, allowing styles to be combined and the higher
// priority style's non-null members to override the lower priority style's ones.
struct InFlightStyle
{
    TextWrapping wrapping;
    int wrapping_p;
    
    ClipStyle horizontal_clipping;
    ClipStyle vertical_clipping;
    int horizontal_clipping_p, vertical_clipping_p;

    Measurement width, min_width, max_width;
    Measurement height, min_height, max_height;
    int width_p, min_width_p, max_width_p;
    int height_p, min_height_p, max_height_p;
    StyleColor color, text_color;
    int color_p, text_color_p;
    
    DisplayType display;
    int display_p;
    
    Margin margin;
    Padding padding;
    Corners corners;
    int margin_p, padding_p, corners_p;

    uint16_t font_id;
    uint16_t font_size;
    int font_id_p, font_size_p;
};

struct Selector
{
    int id;
    int style_ids[MAX_STYLES_PER_SELECTOR]; // IDS of all style that this selector selects
    int style_count; // Number of styles in the style_ids array.
    int name_length;
    char* name;
};

enum class AttributeType
{
NONE,
CUSTOM, // For user defined args
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
THIS_ELEMENT, // For binding an element to a variable
};

struct attr_comp_id_body 
{
    int id;
};

struct attr_text_like_body
{
    char* static_value; // Note(Leo): Text attributes (on text elements) can have EITHER a static value OR a binding but not both!
    int value_length;
    int binding_position;
    int binding_id;
};

struct attr_custom_body : attr_text_like_body
{
    char* name_value;
    int name_length;
};

// Marks an attribute as having been initialized (for attributes that need initialization in the runtime evaluator)
#define AttributeInitilized (uint32_t)(1 << 0)

struct class_attribute_cache 
{
    // Result of merging all the styles pointed too by each selector 
    InFlightStyle* merged_style;
};

struct Attribute
{
    Attribute* next_attribute;
    AttributeType type;
    uint32_t flags;
    
    union 
    {
        attr_comp_id_body CompId;
        attr_text_like_body Text;
        attr_custom_body Custom;
    };
};

enum class ElementType 
{
    NONE,
    ROOT,
    TEXT,
    HDIV,
    VDIV,
    CUSTOM,
    GRID,
    IMG,
    VIDEO,
};

struct Element 
{
    void* master;
    
    int id;
    uint32_t flags;
    ElementType type;
    
    // Note(Leo): Attributes are not contiguous in memory
    Attribute* first_attribute;
    int num_attributes;
    
    Element* parent;
    Element* next_sibling;
    Element* first_child;
    
    // In Flight Vars
    InFlightStyle working_style;
    
    union
    {
        struct {
            // Note(Leo): temporal since its on the frame arena
            char* temporal_text;
            int temporal_text_length;
        } Text;
        struct {
        
        } Video;
        struct {
            // Note(Leo): row/columns measurements can be non-pixel values until final layout pass at which point they MUST
            // all be baked down into pixel values.
            // Note(Leo): temporal since theyre on the frame arena
            Measurement* temporal_rows;
            Measurement* temporal_columns;
        
        } Grid;
    };
};


// Types of bound functions
typedef void (*SubscribedStubVoid)(void*); 
typedef ArenaString* (*SubscribedStubString)(void*, Arena*); 

enum class BoundExpressionType
{
    NONE,
    VOID_RET,
    ARENA_STRING,
    VOID_PTR,
};

struct BoundExpression 
{
    int expression_id;
    BoundExpressionType type;
    union {
        SubscribedStubVoid stub_void;
        SubscribedStubString stub_string;
    };
    
};

BoundExpression* register_bound_expr(SubscribedStubVoid fn, int id);
BoundExpression* register_bound_expr(SubscribedStubString fn, int id);

extern Runtime runtime;


// Returns the first attribute of a given type that appears in an element's attribtue list.
Attribute* GetAttribute(Element* element, AttributeType searched_type);

// Note(Leo): Name should be NULL terminated
Selector* GetSelectorFromName(const char* name);

Style* GetStyleFromID(int style_id);

Element* CreateElement(DOM* dom, SavedTag* tag_template);

void InitDOM(Arena* master_arena, DOM* target);

void ConvertSelectors(Compiler::Selector* selector);
void ConvertStyles(Compiler::Style* style);

// Merge the members of the secondary in-flight style into the main style
void MergeStyles(InFlightStyle* main, InFlightStyle* secondary);

// Merge the members of style into the in-flight main style 
void MergeStyles(InFlightStyle* main, Style* style);

// Set the members of this style to the defaults.
void DefaultStyle(InFlightStyle* target);

void CalculateStyles(DOM* dom);
void BuildRenderQue(DOM* dom);
void Draw(DOM* dom);

// Tells the program master to switch the page of the specified DOM to the specified one
// Flags may tell the master wether to delete the curernt DOM or keep it and save the state
void SwitchPage(DOM* dom, int id, int flags = 0);

// Expects a DOM with cleared element Arena, instances all the 
void* InstancePage(DOM* target_dom, int id);

// Instances a component of the type specified by id, returns a pointer to the component's object
void* InstanceComponent(DOM* target_dom, Element* parent, int id);

void* AllocPage(DOM* dom, int size, int file_id);

void* AllocComponent(DOM* dom, int size, int file_id);

// Runtime Function Definitions //
void LoadFromBin(char* file_path, Runtime* runtime);

LoadedFileHandle* GetFileFromId(int id);
LoadedFileHandle* GetFileFromName(const char* name);

BoundExpression* GetBoundExpression(int id);
void RuntimeClearTemporal(DOM* target);