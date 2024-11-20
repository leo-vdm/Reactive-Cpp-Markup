#include <iostream>
#include <fstream>

#include "arena.h"

#pragma once
// Compilation Types //

struct CompilerState
{
    int next_file_id;
    int next_bound_var_id;
    int next_bound_expr_id;
    
    int next_style_id;
    int next_selector_id;
    
    int next_component_id;
    
    CompilerState()
    {
        next_file_id = 1;
        next_bound_var_id = 1;
        next_bound_expr_id = 1;
        next_style_id = 1;
        next_selector_id = 1;
        next_component_id = 1;
    }
};

// Compilation Functions //
int RegisterComponent(char* name, int name_length, CompilerState* state);

// Prepass types

#define LARGEST_FILE_TYPE_LENGTH 11 // size of the longest addition the prepass makes to a filename. eg .markup.txt

#define LONGEST_META_TAG_LENGTH 8 // size of the longest extracted tag eg </style>

struct SplitFileNames
{
    char* code_file_name;
    char* style_file_name;
    char* markup_file_name;
};

// Prepass functions


// Seperates the source file into by grabbing the script and style tags
// source.markup.txt, source.cpp, source.style.txt
SplitFileNames SeperateSource(FILE* source_file, Arena* file_name_arena, char* source_file_name, char* output_dir);

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
    
    int value_length = 0; // Length in chars, not bytes (same for all other lengths)
    void* token_value; // The contents of the token (if any)
};

// Lexer Functions
void Tokenize(FILE* src, Arena* tokens_arena, Arena* token_values_arena);

// Returns a pointer to the start of the region it lexed into, end is marked with an END token
Token* TokenizeAttribute(Arena* tokens_arena, Arena* token_values_arena, Token* attribute_token);

void TokenizeStyle(FILE* src, Arena* tokens_arena, Arena* token_values_arena);

void TokenizeCode(FILE* src, Arena* tokens_arena, Arena* token_values_arena);

Token* TokenizeDirective(Arena* tokens_arena, Arena* token_values_arena, Token* directive_token);

// For the code inside markup bindings
Token* TokenizeBindingCode(Arena* tokens_arena, Arena* token_values_arena, char* src, int src_length);


// Parser Types

#define MAX_TAG_NAME_LENGTH 4  // The length of the longest built in tag name

enum class TagType {
ROOT,
TEXT,
DIV,
CUSTOM, // User defined component, the name given is set as an attribute
GRID,
IMG,
VIDEO,
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
};

#define MAX_TAGS_PER_BINDING 20

struct RegisteredBinding{
    int binding_id;
    
    char* binding_name;
    int name_length;
    
    int registered_tag_ids[MAX_TAGS_PER_BINDING];
    int num_registered;
};

#define MAX_ATTRIBUTE_NAME_LENGTH 57// The length of the longest built in attribute name

struct Attribute {
    AttributeType type;
    char* attribute_value;
    int value_length;
    int binding_position; // Position where the binding gets inserted into the value (for attributes like text)
    int binding_id; 
};

struct Tag {
    TagType type;
    int tag_id; // Given by parser
    
    Attribute* first_attribute;
    int num_attributes;
    
    Tag* parent;
    Tag* next_sibling;
    Tag* first_child;
};

// Currently: maxHeight - 9 chars
#define MAX_STYLE_FIELD_NAME_LENGTH 9 // Length of longest field name for styles (prevents alocating arbitrarily large buffer)

enum class StyleFieldType {
NONE, 
WIDTH,
HEIGHT,
MAX_WIDTH,
MAX_HEIGHT,
};

enum class MeasurementType {
NONE,
AUTO,
PIXELS,
PERCENT,
};

struct Measurement {
    float size;
    MeasurementType type;
    
    Measurement()
    {
        size = 0.0f;
        type = MeasurementType::NONE;
    }
};

struct Style {
    int global_id;
    
    Measurement width, height;
    Measurement max_width, max_height;
};

#define MAX_STYLES_PER_SELECTOR 20

struct Selector {
    int global_id;
    char* name;
    int name_length;
    int style_ids [MAX_STYLES_PER_SELECTOR]; // IDS of all style that this selector selects
    int num_styles; // Number of styles in the style_ids array.
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
    
    int registered_bindings_count;
    int file_id;
    
    Arena* values;
    
    AST()
    {
        root_tag = NULL;
        attributes = NULL;
        registered_bindings = NULL;
        registered_bindings_count = 0;
        values = NULL;
    }
    
};

// Parser Functions
TagType GetTagFromName(char* value, int value_length);
AttributeType GetAttributeFromName(char* value, int value_length);

// Register a binding if it doesnt exist or return its id if it does
int RegisterBindingByName(Arena* bindings_arena, Arena* values_arena, char* value, int value_length, int tag_id, CompilerState* state); // Returns the id of the binding.

// Wants the target to already have initialized arenas in it.
void ProduceAST(AST* target, Arena* tokens, Arena* token_values, CompilerState* state);

// Register a style selector if it doesnt exist and return its ID.
int RegisterSelectorByName(LocalStyles* target, char* value, int value_length, int style_id, int global_prefix, CompilerState* state);

// NOTE: file_prefix_string should be NULL terminated!!
void ParseStyles(LocalStyles* target, Arena* style_tokens, Arena* style_token_values, int file_prefix, CompilerState* state);
char* ParseStyleStub(); // For inline styles. Returns the name given to the style. ** MAY CHANGE **

// Codegen Types
typedef void (*SubscribedFunction)(); // For user defined fn's, generated by #subto. also for markup bindings, generated by #if {expr}
typedef char* (*SubscribedExpression)(); // For markup bindings, generated by {expr}

// Runtime version
/*
struct BindingExpression {
    union {
        SubscribedFunction eval_fn;
        SubscribedExpression eval_expr;
    };
    
    int* subscribed_element_ids;
    int subscriber_count;
};
*/

#define MAX_VARS_PER_EXPRESSION 20

// Compiled version
struct BindingExpression {
    int id;

    char* eval_fn_name;
    int name_length;
    
    int subscribed_variable_ids[MAX_VARS_PER_EXPRESSION];
    int subscribed_var_count;
    
    int subscribed_element_ids[MAX_TAGS_PER_BINDING];
    int subscriber_count;
};

#define MAX_BOUND_VAR_SUBS 20

// Runtime version
/*
struct BoundVariable
{
    int id;
    BindingExpression* subscribers[MAX_BOUND_VAR_SUBS];
    int subscriber_count;
};
*/

// Compiled version
struct BoundVariable
{
    char* var_name;
    int name_length;

    int id;
};

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
    Arena* bound_vars;
    Arena* bound_var_names;
    Arena* bound_expressions;
    
    int file_id;
};

// Length in chars of the longest directive keyword, currently: USECOMPONENT
#define MAX_DIRECTIVE_LENGTH 12

// Codegen Functions

// Finds all the directives and registers them, then adds the registration fn's for bound functions
#define is_component() 1 << 0
void RegisterDirectives(CompileTarget* target, Arena* tokens, Arena* token_values, int flags = 0);


// Adds registration fn's for markup bindings
void RegisterMarkupBindings(CompileTarget* target, Arena* markup_bindings, Arena* tokens, Arena* token_values);



// Scans the given dir recursively looking for .cmc and .cmp files
void ScanSourceDirectory(Arena* sources, char* dir_name);

void CompileComponent(FILE* component_source);

void CompilePage(FILE* page_source);