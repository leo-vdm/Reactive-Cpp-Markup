#include <cstring>
#include <map>

#include "file_system.h"
#include "arena.h"
#include "arena_string.h"
#pragma once

// flag for page_switch to save dom state
#define save_dom() 1 << 0

// Common flags
#define cache_valid() (uint64_t)(1 << 0)
#define is_hovered() (uint64_t)(1 << 1)
#define is_hidden() (uint64_t)(1 << 2)
#define is_clicked() (uint64_t)(1 << 3)
#define is_focusable() (uint64_t)(1 << 4)

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

typedef uint8_t VirtualKeyCode;

#define FontHandle uint16_t

struct LinkedPointer
{
    LinkedPointer* next;
    void* data;  
};

struct PageSwitchRequest
{
    int flags;
    int file_id;
};

struct Element;

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
    Arena* events;
    uint32_t event_count;
    uint32_t max_events;
    uint32_t last_event; // Used to keep track of where the last event was popped from
/*    
    Arena* styles;
    Arena* selectors;
*/    
    Arena* changed_que;
    
    Arena* frame_arena; // Composted every frame
    
    Element* focused_element;
    
    PageSwitchRequest switch_request;
};

struct ElementMaster 
{
    int file_id;
    DOM* master_dom;
};

enum class EventType
{
    NONE,
    KEY_DOWN,
    KEY_REPEAT,
    KEY_UP,
    FOCUSED,
    DE_FOCUSED,
    VIRTUAL_KEYBOARD, // Mostly used for mobile
};

struct Event
{
    EventType type;
    union
    {
        struct
        {
            uint32_t key_char;   // UTF 32 char this key represents (indicated by the os)
            VirtualKeyCode code; // The physical key on the keyboard
        } Key;
        struct
        {
            Element* target;
        } Focused;
        struct
        {
            Element* target;
        } DeFocused;
        struct
        {
            bool isShown;
        } VirtualKeyboard;
    };
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
    Arena* loaded_templates;
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
typedef Compiler::BindingContext BindingContext;

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
    InFlightStyle cached_style; //  Cached result of calling merge_selector_styles on this selector.
    uint64_t flags;
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
CONDITION,
LOOP,
ON_FOCUS,
FOCUSABLE,
};

struct attr_comp_id_body 
{
    int id;
};

struct attr_on_click_body
{
    int binding_id;
};

struct attr_on_focus_body
{
    int binding_id;
};

struct attr_condition_body
{
    int binding_id;
};

struct attr_this_body
{
    int binding_id;
    bool is_initialized;
};

struct attr_loop_body
{
    int array_binding;
    int length_binding;
    int template_id;
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
        attr_on_click_body OnClick;
        attr_on_focus_body OnFocus;
        attr_this_body This;
        attr_condition_body Condition;
        attr_loop_body Loop;
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
    EACH,
};

struct LoadedImageHandle;

struct size_axis
{
    union
    {
        struct
        {
            Measurement desired;
            Measurement min; // Must be in px or %
            Measurement max; // Must be in px or %             
            
            Measurement margin1; // Must be in px, % or grow             
            Measurement margin2; // Must be in px, % or grow             
            
            Measurement padding1; // Must be in px or %
            Measurement padding2; // Must be in px or %
        };
        
        Measurement measures[7];
    };
    
    // Calculated
    float current;
};

struct shape_axis 
{
    size_axis width;
    size_axis height;
};

struct shape_clipping
{
    ClipStyle horizontal;
    float left_scroll; // How far in pixels we have scrolled from the left of the clipped region
    ClipStyle vertical;    
    float top_scroll; // How far in pixels we have scrolled from the top of the clipped region
};

struct bounding_box 
{
    float x, y, width, height;
};

struct screen_position
{
    float x, y;
};

// Controls how an element lays out its children
enum class LayoutDirection
{
    NONE, // For elements that cant have chilren
    VERTICAL,
    HORIZONTAL,
    GRID,
};

enum class LayoutElementType
{
    NORMAL, // Normal div types
    TEXT,
    TEXT_COMBINED,
    IMAGE,
    END,
};

struct FontPlatformShapedGlyph;

struct LayoutElement
{
    uint32_t element_id;
    LayoutElementType type;
    LayoutDirection dir;
    DisplayType display;
    shape_axis sizing;
    
    bounding_box bounds;
    screen_position position; // Position of the elment's top left corner. different from its bounding box since 
    //                           bounding box is the visisble parts of the element and this is the actual position.
    LayoutElement* children;
    uint16_t child_count;
    
    union 
    {
        struct
        {
            StyleColor color;
            Corners corners;
            shape_clipping clipping;
            TextWrapping wrapping; // Controls how child text should be wrapped
        } NORMAL;
        struct
        {
            StyleColor text_color;
            FontHandle font_id;
            uint16_t font_size;     
            uint16_t text_length;
            char* text_content;
        } TEXT;
        struct
        {
            FontPlatformShapedGlyph* first_glyph;
            uint32_t glyph_count;
        } TEXT_COMBINED;
        struct 
        {
            Corners corners;
            LoadedImageHandle* handle;
        } IMAGE;
    };
};

enum class ClickState
{
    NONE, // Element has no current click state
    MOUSE_DOWN, // Element has had mouse down on it and mouse has stayed over it
};

struct Element 
{
    void* master;
    void* context_master;
    uint64_t flags;
    
    int id;
    int global_id; // From the Id attribute.
    int context_index; // For keeping track of which index in an each element spawned this element.
    ElementType type;
    ClickState click_state;
    
    // Note(Leo): Attributes are not contiguous in memory
    Attribute* first_attribute;
    int num_attributes;
    
    Element* parent;
    Element* next_sibling;
    Element* first_child;
    
    LayoutElement* last_sizing;
    
    // In Flight Vars
    InFlightStyle working_style;
    
    struct
    {
        float x;  
        float y;  
    } scroll;    
    
    union
    {
        struct {
            // Note(Leo): temporal since its on the frame arena
            char* temporal_text;
            int temporal_text_length;
        } Text;
        struct 
        {
            LoadedImageHandle* handle;
        } Image;
        struct {
        
        } Video;
        struct {
            // Note(Leo): row/columns measurements can be non-pixel values until final layout pass at which point they MUST
            // all be baked down into pixel values.
            // Note(Leo): temporal since theyre on the frame arena
            Measurement* temporal_rows;
            Measurement* temporal_columns;
        
        } Grid;
        struct {
            Compiler::Tag* inner_template;
            int last_count;
            void* array_ptr;
        } Each;
    };
};


struct Component : ElementMaster
{
    Element* custom_element;
};

struct Page : ElementMaster
{

};

// Types of bound functions
typedef void (*SubscribedStubVoid)(void*); 
typedef void (*SubscribedStubVoidBool)(void*, bool); 
typedef ArenaString* (*SubscribedStubString)(void*, Arena*);
typedef bool (*SubscribedStubBool)(void*); 
typedef void (*SubscribedStubPointer)(void*, void*); // For feeding pointers to the user
typedef void* (*SubscribedStubGetPointer)(void*); // For getting pointers from the user
typedef int (*SubscribedStubInt)(void*);

typedef ArenaString* (*ArrSubscribedStubString)(void*, Arena*, int);
typedef void (*ArrSubscribedStubVoid)(void*, void*, int);
typedef void (*ArrSubscribedStubVoidBool)(void*, void*, int, bool);
typedef bool (*ArrSubscribedStubBool)(void*, void*, int);
typedef void (*ArrSubscribedStubPointer)(void*, int, void*); // For feeding pointers to the user
typedef void* (*ArrSubscribedStubGetPointer)(void*, int); // For getting pointers from the user
typedef int (*ArrSubscribedStubInt)(void*, void*, int);

enum class BoundExpressionType
{
    NONE,
    VOID_RET,
    ARENA_STRING,
    VOID_PTR,
    BOOL_RET,
    PTR_RET,
    INT_RET,
    VOID_BOOL_RET,
};

struct BoundExpression 
{
    int expression_id;
    BoundExpressionType type;
    BindingContext context;
    
    union
    {
        // Globals //
        SubscribedStubVoid stub_void;
        SubscribedStubVoidBool stub_void_bool;
        SubscribedStubString stub_string;
        SubscribedStubPointer stub_ptr;
        SubscribedStubBool stub_bool;
        SubscribedStubGetPointer stub_get_ptr;
        SubscribedStubInt stub_int;
        
        // Locals //
        ArrSubscribedStubVoid arr_stub_void;
        ArrSubscribedStubVoidBool arr_stub_void_bool;
        ArrSubscribedStubString arr_stub_string;
        ArrSubscribedStubPointer arr_stub_ptr;
        ArrSubscribedStubBool arr_stub_bool;
        ArrSubscribedStubGetPointer arr_stub_get_ptr;
        ArrSubscribedStubInt arr_stub_int;
    };
    
};

BoundExpression* register_bound_expr(SubscribedStubVoid fn, int id);
BoundExpression* register_bound_expr(SubscribedStubVoidBool fn, int id);
BoundExpression* register_bound_expr(SubscribedStubString fn, int id);
BoundExpression* register_bound_expr(SubscribedStubPointer fn, int id);
BoundExpression* register_bound_expr(SubscribedStubBool fn, int id);
BoundExpression* register_bound_expr(SubscribedStubGetPointer fn, int id);
BoundExpression* register_bound_expr(SubscribedStubInt fn, int id);

BoundExpression* register_bound_expr(ArrSubscribedStubVoid fn, int id);
BoundExpression* register_bound_expr(ArrSubscribedStubVoidBool fn, int id);
BoundExpression* register_bound_expr(ArrSubscribedStubString fn, int id);
BoundExpression* register_bound_expr(ArrSubscribedStubPointer fn, int id);
BoundExpression* register_bound_expr(ArrSubscribedStubBool fn, int id);
BoundExpression* register_bound_expr(ArrSubscribedStubGetPointer fn, int id);
BoundExpression* register_bound_expr(ArrSubscribedStubInt fn, int id);

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

// Tells the program master to switch the page of the specified DOM to the specified one
// Flags may tell the master whether to delete the curernt DOM or keep it and save the state
void SwitchPage(DOM* dom, int id, int flags = 0);

// Same as SwitchPage but takes the name of the page to switch to 
void SwitchPage(DOM* dom, const char* name, int flags = 0);

// Expects a DOM with cleared element Arena, instances all the 
void* InstancePage(DOM* target_dom, int id);

// Instances a component of the type specified by id, returns a pointer to the component's object
void* InstanceComponent(DOM* target_dom, Element* parent, int id);

void InstanceTemplate(DOM* target_dom, Element* parent, void* array_ptr, int template_id, int index);

void* AllocPage(DOM* dom, int size, int file_id);

void* AllocComponent(DOM* dom, int size, int file_id);

Event* PushEvent(DOM* dom);
Event* PopEvent(DOM* dom);

// Route an event to a component object to be handled
void RouteEvent(void* master, Event* event);

Element* GetFocused(DOM* dom);

// Runtime Function Definitions //

LoadedFileHandle* GetFileFromId(int id);
LoadedFileHandle* GetFileFromName(const char* name);

BoundExpression* GetBoundExpression(int id);
BodyTemplate* GetTemplate(int id);
void RuntimeClearTemporal(DOM* target);

// Frees all the element masters that have been malloc'ed in all the child elements of the given start element
// Optionally DeAlloc's all the elements/attributes that are encountered aswell
void FreeSubtreeObjects(Element* start, DOM* dom = NULL); // If DOM is given then elements are DeAlloc'ed