#include <iostream>
#include <fstream>

#include "arena.h"

#pragma once
#include "string_view.h"

// Aligns the given pointer to where the type wants it to start in memory
// Note(Leo): GCC doesnt need decltype inside of alignof but msvc does and will error otherwise
#define align_mem(ptr, type) (type*)((uintptr_t)ptr + alignof(type) - ((uintptr_t)ptr % alignof(type)))

// Compilation Types //
namespace Compiler {

struct CompilerState
{
    int next_file_id;
    int next_bound_expr_id;
    
    int next_style_id;
    int next_selector_id;
    int next_template_id;
    // Note(Leo): Element ID's are global identifiers given to tags that have an ID attribute,
    //            The numeric ids are global but names can overlap between files.
    int next_element_id; 
    
    CompilerState()
    {
        next_file_id = 1;
        next_bound_expr_id = 1;
        next_style_id = 1;
        next_selector_id = 1;
        next_template_id = 1;
        next_element_id = 1;
    }
};



// Prepass types

#define LARGEST_FILE_TYPE_LENGTH 11 // size of the longest addition the prepass makes to a filename. eg .markup.txt

#define LONGEST_META_TAG_LENGTH 8 // size of the longest extracted tag eg </style>

struct SplitFileNames
{
    char* code_file_name;
    char* style_file_name;
    char* markup_file_name;
};


// Lexer Types
enum class TokenType
{
    OPEN_TAG,
    CLOSE_TAG,
    OPEN_BRACKET,
    CLOSE_BRACKET,
    EQUALS,
    QUOTE,
    SLASH,
    TEXT,
    TAG_START,
    TAG_END,
    TAG_ATTRIBUTE,
    ATTRIBUTE_IDENTIFIER,
    ATTRIBUTE_VALUE,
    COLON,
    SEMI_COLON,
    COMMA,
    OPEN_PARENTHESIS,
    CLOSE_PARENTHESIS,
    DIRECTIVE,
    NEW_LINE,
    END
};

struct Token {
    TokenType type;
    
    StringView body;
};


// Parser Types

#define MAX_TAG_NAME_LENGTH 4  // The length of the longest built in tag name

enum class TagType {
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
    ON_CLICK, // For click bindings
    THIS_ELEMENT,
    CONDITION, // Makes any element into the equivelent of an #if statement in svelte
    LOOP, // For the each element
    ON_FOCUS,
    FOCUSABLE,
    ID,
};

#define MAX_TAGS_PER_BINDING 20

enum class RegisteredBindingType
{
    NONE,
    VOID_RET,
    TEXT_RET,
    VOID_PTR,
    BOOL_RET,
    PTR_RET,
    INT_RET,
};

enum class BindingContext
{
    NONE,
    GLOBAL,
    LOCAL,
};

struct RegisteredBinding
{
    int binding_id;
    
    char* binding_name;
    int name_length;
    
    RegisteredBindingType type;
    BindingContext context;
    StringView context_name;
};

struct RegisteredTemplate
{
    int template_id;
    Arena tags;
};

struct attr_comp_id_body 
{
    int id;
};

struct attr_this_body
{
    int binding_id;
};

struct attr_on_click_body
{
    int binding_id;
};

struct attr_condition_body
{
    int binding_id;
};

struct attr_on_focus_body
{
    int binding_id;
};

struct attr_text_like_body
{
    char* value;
    int value_length;
    int binding_position; // Position where the binding gets inserted into the value (for attributes like text)
    int binding_id; 
};

struct attr_loop_body
{
    int array_binding;
    int length_binding;
    StringView type_name;
    int template_id;
};

struct attr_custom_body : attr_text_like_body
{
    char* name;
    int name_length;
};

struct Attribute
{
    AttributeType type;
    
    union 
    {
        attr_on_click_body OnClick;
        attr_comp_id_body CompId;
        attr_text_like_body Text;
        attr_custom_body Custom;
        attr_this_body This;
        attr_condition_body Condition;
        attr_loop_body Loop;
        attr_on_focus_body OnFocus;
    };
};

struct Tag
{
    TagType type;
    int tag_id; // Given by parser
    int global_id; // Set if tag has an ID attribute
    
    Attribute* first_attribute;
    int num_attributes;
    
    Tag* parent;
    Tag* next_sibling;
    Tag* first_child;

    StringView context_name;
};

// Currently: maxHeight - 9 chars
#define MAX_STYLE_FIELD_NAME_LENGTH 9 // Length of longest field name for styles (prevents alocating arbitrarily large buffer)

enum class StyleFieldType {
    NONE, 
    WRAPPING,
    HORIZONTAL_CLIPPING,
    VERTICAL_CLIPPING,
    COLOR,
    TEXT_COLOR,
    DISPLAY,
    WIDTH,
    HEIGHT,
    MIN_WIDTH,
    MIN_HEIGHT,
    MAX_WIDTH,
    MAX_HEIGHT,
    MARGIN,
    PADDING,
    CORNERS,
    FONT_SIZE,
    FONT_NAME,
    PRIORITY,
};

// Todo(Leo): Reflect these types in the compiler
enum class MeasurementType
{
    NONE,
    GROW, // Grow to fill the parent sharing free space with other grow measurements.
    FIT, // Not allowed for margin/padding. Fit the parent around its children
    PIXELS,
    PERCENT, // Relative to parent for margin/padding aswell as width/height 
             // Should be normalized to 0-1 range
};

struct Measurement {
    float size;
    MeasurementType type;
};

struct Padding 
{
    union
    {
        struct
        {
            Measurement left;
            Measurement right;
            Measurement top;
            Measurement bottom;
        };
        Measurement m[4];
    };
};

struct Margin 
{
    union
    {
        struct
        {
            Measurement left;
            Measurement right;
            Measurement top;
            Measurement bottom;
        };
        Measurement m[4];
    };
};

// Corner radii in px
struct Corners
{
    union
    {
        struct
        {
            float top_left;
            float top_right;
            float bottom_left;
            float bottom_right;
        };
        float c[4];
    };
};

// Note(Leo): To match vulkan, alpha of 0 is fully transparent and 1 is fully opaque
// Note(Leo): All the color channels should be from 0 to 1
struct Color 
{
    union
    {
        struct
        {
            float r, g, b, a;
        };
        float c[4];
    };
};

enum class TextWrapping
{
    NONE, // Invalid/use default
    WORDS, // Text is wrapped but words are kept together 
    CHARS, // Text is wrapped but in arbitrary positions
    NO, // Text will not wrap and will overflow
};

enum class ClipStyle
{
    NONE,
    HIDDEN, // Just hide clipped region. Clipped region can still be scrolled through internal means
    SCROLL, // Hide clipped region and show a scroll bar
};


enum class DisplayType
{
    NONE,
    NORMAL, // Like css block
    HIDDEN, // Neither element nor its children are shown or taken into account at all. 
    MANUAL, // Like css relative, top: n px, left: n px for placing the element inside its parent
};

struct Style 
{
    union 
    {
        int id;
        int global_id;
    };
    int priority;

    TextWrapping wrapping;
    
    ClipStyle horizontal_clipping;
    ClipStyle vertical_clipping;
    
    Color color; // Background color
    Color text_color; // The color of child text
    
    DisplayType display;
    
    Measurement width, min_width, max_width;
    Measurement height, min_height, max_height;
    Margin margin;
    Padding padding;
    Corners corners;
    
    uint16_t font_size;     
    union
    {
        uint16_t font_id; // For the runtime
        StringView font_name; // For the compiler
        
        struct // For the filesystem
        {
            int index;
            int length;
        } saved_font_name;
    };
};

#define MAX_STYLES_PER_SELECTOR 20

struct Selector {
    int global_id;
    char* name;
    int name_length;
    int style_ids [MAX_STYLES_PER_SELECTOR]; // IDS of all style that this selector selects
    int num_styles; // Number of styles in the style_ids array.
};

struct ElementId
{
    int id;
    StringView name;
};

struct LocalStyles {
    Arena* selectors;
    Arena* selector_values;
    
    Arena* styles;
    
    LocalStyles()
    {
        selectors = NULL;
        selector_values = NULL;
        styles = NULL;
    }
};

struct AST {

    Arena* tags;
    Tag* root_tag;
    
    Arena* attributes;
    Arena* registered_bindings;
    Arena* templates;
    Arena* element_ids;
    
    int file_id;
    
    Arena* values;
    
    AST()
    {
        root_tag = NULL;
        attributes = NULL;
        registered_bindings = NULL;
        values = NULL;
        templates = NULL;
        element_ids = NULL;
    }
    
};


#define MAX_VARS_PER_EXPRESSION 20

// Compiled version
struct BindingExpression {
    int id;

    char* eval_fn_name;
    int name_length;
};

#define MAX_BOUND_VAR_SUBS 20

enum class DirectiveType
{
    NONE,
    BIND,
    SUBTO,
    USECOMP,
};

struct CompileTarget
{
    FILE* code;
    Arena* bound_expressions;
    
    char* file_name;
    
    int file_id;
};

// Length in chars of the longest directive keyword, currently: USECOMPONENT
#define MAX_DIRECTIVE_LENGTH 12

#define CALL_PAGE_MAIN_FN_TEMPLATE "case(%d):\n\tpage_main_%d(dom, file_id, d_void_target);\n\tbreak;\n"
#define CALL_COMP_MAIN_FN_TEMPLATE "case(%d):\n\tcomp_main_%d(dom, file_id, d_void_target);\n\tbreak;\n"
#define CALL_PAGE_FRAME_FN_TEMPLATE "case(%d):\n\tpage_frame_%d(dom, d_void);\n\tbreak;\n"
#define CALL_COMP_EVENT_FN_TEMPLATE "case(%d):\n\tcomp_event_%d(dom, event, d_void);\n\tbreak;\n"
#define COMP_MAIN_FN_TEMPLATE "\nvoid call_comp_main(DOM* dom, int file_id, void** d_void_target){\nswitch(file_id){\n"
#define PAGE_MAIN_FN_TEMPLATE "\nvoid call_page_main(DOM* dom, int file_id, void** d_void_target){\nswitch(file_id){\n"
#define CLOSE_MAIN_CALL_TEMLATE "default:\n\tprintf(\"Component/Page does not exist!\\n\");\n\tbreak;\n}\n}\n"
#define PAGE_FRAME_FN_TEMPLATE "\nvoid call_page_frame(DOM* dom, int file_id, void* d_void){\nswitch(file_id){\n"
#define COMP_EVENT_FN_TEMPLATE "\nvoid call_comp_event(DOM* dom, Event* event, int file_id, void* d_void){\nswitch(file_id){\n"
#define DOM_ATTATCHMENT_INCLUDES "#include \"element_ids.h\"\n#include \"DOM.h\"\n#include \"overloads.cpp\"\n#include \"dom_attatchment.h\"\n"

};


// Compilation Functions //
int RegisterComponent(StringView* name, Compiler::CompilerState* state);

// Prepass functions


// Seperates the source file into by grabbing the script and style tags
// source.markup.txt, source.cpp, source.style.txt
Compiler::SplitFileNames SeperateSource(FILE* source_file, Arena* file_name_arena, char* source_file_name, char* output_dir);


// Lexer Functions
void Tokenize(FILE* src, Arena* tokens_arena, Arena* token_values_arena);

// Returns a pointer to the start of the region it lexed into, end is marked with an END token
Compiler::Token* TokenizeAttribute(Arena* tokens_arena, Arena* token_values_arena, Compiler::Token* attribute_token);

void TokenizeStyle(FILE* src, Arena* tokens_arena, Arena* token_values_arena);

void TokenizeCode(FILE* src, Arena* tokens_arena, Arena* token_values_arena);

Compiler::Token* TokenizeDirective(Arena* tokens_arena, Arena* token_values_arena, Compiler::Token* directive_token);


// Parser Functions
Compiler::TagType GetTagFromName(StringView* name);
Compiler::AttributeType GetAttributeFromName(StringView* name);

// Register a binding if it doesnt exist or return its id if it does
int RegisterBindingByName(Arena* bindings_arena, Arena* values_arena, StringView* name, Compiler::RegisteredBindingType type, bool is_local, Compiler::CompilerState* state, StringView context_name); // Returns the id of the binding.

// Wants the target to already have initialized arenas in it.
void ProduceAST(Compiler::AST* target, Arena* tokens, Arena* token_values, Compiler::CompilerState* state);

// Register a style selector if it doesnt exist and return its ID.
int RegisterSelectorByName(Compiler::LocalStyles* target, StringView* name, int style_id, int global_prefix, Compiler::CompilerState* state);
void ClearRegisteredSelectors();

// NOTE: file_prefix_string should be NULL terminated!!
void ParseStyles(Compiler::LocalStyles* target, Arena* style_tokens, Arena* style_token_values, int file_prefix, Compiler::CompilerState* state);
char* ParseStyleStub(); // For inline styles. Returns the name given to the style. ** MAY CHANGE **

// Codegen Functions

// Finds all the directives and registers them, then adds the registration fn's for bound functions
#define is_component() 1 << 0
void RegisterDirectives(Compiler::CompileTarget* target, Arena* tokens, Arena* token_values, Compiler::CompilerState* state, int flags = 0);

// Adds registration fn's for markup bindings
void RegisterMarkupBindings(Compiler::CompileTarget* target, Arena* markup_bindings, Arena* tokens, Arena* token_values, int flags = 0);

#define DOM_ATTATCHMENT_NAME "dom_attatchment.cpp"
#define ELEMENT_ID_HEADER_NAME "element_ids.h"
void GenerateDOMAttatchment(FILE* dom_attatchment, Compiler::CompilerState* state, int flags = 0);

// Scans the given dir recursively looking for .cmc and .cmp files
void ScanSourceDirectory(Arena* sources, char* dir_name);

Compiler::RegisteredTemplate* RegisterTemplate(Arena* templates_arena, Compiler::CompilerState* state);
