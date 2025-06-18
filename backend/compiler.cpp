#include <iostream>
#include <cstring>
#include <map>

#define STRING_VIEW_IMPLEMENTATION 1
#include "compiler.h"
using namespace Compiler;
#include "file_system.h"
#include "arena_string.h"

void print_binding(int binding_id, Arena* bindings_arena);
void print_attribute(Attribute* attribute, Arena* bindings_arena);
void print_ast(AST* ast);
void print_styles(LocalStyles* glob_styles);
void register_to_dom_attatchment(FILE* dom_attatchment, char* added_file_name);
void register_element_ids(FILE* ids_header, Arena* ids_arena, char* file_name);

void cleanup_sources(SplitFileNames* target)
{
    remove(target->style_file_name);
    remove(target->markup_file_name);
}

AST init_ast(Arena* master_arena)
{
    AST created = AST();
    Arena* tags_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *tags_arena = CreateArena(sizeof(Tag)*10000, sizeof(Tag));
    Arena* attributes_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *attributes_arena = CreateArena(sizeof(Attribute)*10000, sizeof(Attribute));
    Arena* bindings_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *bindings_arena = CreateArena(sizeof(RegisteredBinding)*5000, sizeof(RegisteredBinding));
    Arena* values_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *values_arena = CreateArena(sizeof(char)*100000, sizeof(char)); 
    Arena* templates_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *templates_arena = CreateArena(sizeof(RegisteredTemplate)*200, sizeof(RegisteredTemplate));
    Arena* element_ids_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *element_ids_arena = CreateArena(sizeof(ElementId)*500, sizeof(ElementId));
    
    created.tags = tags_arena;
    created.attributes = attributes_arena;
    created.registered_bindings = bindings_arena;
    created.values = values_arena;
    created.templates = templates_arena;
    created.element_ids = element_ids_arena;
    return created;
}

void reset_ast(AST* ast)
{   
    ResetArena(ast->tags);
    ResetArena(ast->attributes);
    ResetArena(ast->registered_bindings);
    ResetArena(ast->values);
    ResetArena(ast->templates);
    ResetArena(ast->element_ids);
    
    ast->file_id = 0;
}


LocalStyles init_styles(Arena* master_arena)
{
    LocalStyles created = LocalStyles();
    
    Arena* selectors_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *selectors_arena = CreateArena(sizeof(Selector)*10000, sizeof(Selector));
    Arena* selector_values_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *selector_values_arena = CreateArena(sizeof(char)*10000, sizeof(char));
    Arena* styles_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *styles_arena = CreateArena(sizeof(Style)*10000, sizeof(Style));
    
    created.selectors = selectors_arena;
    created.selector_values = selector_values_arena;
    created.styles = styles_arena;
    
    return created;
}  

void reset_styles(LocalStyles* styles)
{
    ResetArena(styles->selectors);
    ResetArena(styles->selector_values);
    ResetArena(styles->styles);
}

CompileTarget init_compile_target(Arena* master_arena)
{
    CompileTarget created;
    Arena* bound_expressions_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *bound_expressions_arena = CreateArena(sizeof(BindingExpression)*1000, sizeof(BindingExpression));
    
    created.bound_expressions = bound_expressions_arena;
    
    created.file_name = NULL;
    created.file_id = 0;
    created.code = NULL;
    
    return created;
}

void reset_compile_target(CompileTarget* target)
{
    ResetArena(target->bound_expressions);
    
    target->code = NULL;
    target->file_name = NULL;
    target->file_id = 0;
}

Arena* init_strings_arena(Arena* master_arena)
{
    Arena* created;
    created = (Arena*)Alloc(master_arena, sizeof(Arena));
    *created = CreateArena(sizeof(StringBlock)*1000, sizeof(StringBlock));
    return created;
}

void add_main(ArenaString* string, int file_id, const char* used_template)
{
    int desired_size = snprintf(NULL, 0, used_template, file_id, file_id);
    char* comp_main = (char*)AllocScratch(desired_size + 1, no_zero());
    sprintf(comp_main, used_template, file_id, file_id);
    Append(string, comp_main);
    DeAllocScratch(comp_main);
}
#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    #if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);
    
    WINDOWS_PAGE_SIZE = static_cast<uintptr_t>(sys_info.dwPageSize);
    WINDOWS_PAGE_MASK = WINDOWS_PAGE_SIZE - 1;
    #endif

    // Initialize scratch arena
    InitScratch(sizeof(char)*100000);
    
    // Get source and build dir     
    if(argc < 3)
    {
        printf("Usage: compiler <source dir> <build dir>\n");
        return 0;
    }
    
    char* source_dir = argv[1];
    
    char* build_dir = argv[2];
    
    // Initialize compiler state;
    CompilerState state = CompilerState();
        
    Arena source_search_result = CreateArena(sizeof(FileSearchResult)*1000, sizeof(FileSearchResult));
    Arena source_search_values = CreateArena(sizeof(char)*100000, sizeof(char));
    
    
    // Find all component files in src dir
    SearchDir(&source_search_result, &source_search_values, source_dir, ".cmc");
    FileSearchResult* curr = (FileSearchResult*)source_search_result.mapped_address;
    
    // Compile all components
    Arena master_arena = CreateArena(100*sizeof(Arena), sizeof(Arena));
    
    AST target = init_ast(&master_arena);
    
    LocalStyles styles = init_styles(&master_arena);
    
    CompileTarget generated_code = init_compile_target(&master_arena);
    
    Arena* strings = init_strings_arena(&master_arena);
    
    Arena* tokens_arena = (Arena*)Alloc(&master_arena, sizeof(Arena));
    Arena* token_values_arena = (Arena*)Alloc(&master_arena, sizeof(Arena));
    
    *tokens_arena = CreateArena(sizeof(Token)*10000, sizeof(Token));
    *token_values_arena = CreateArena(sizeof(char)*100000, sizeof(char));
    
    // Re-make DOM attatchment.
    ArenaString* dom_attatchment_name = CreateString(strings);
    Append(dom_attatchment_name, build_dir);
    Append(dom_attatchment_name, "/");
    Append(dom_attatchment_name, DOM_ATTATCHMENT_NAME);
    
    char* dom_attatchment_name_temp = Flatten(dom_attatchment_name);
    remove(dom_attatchment_name_temp);
    FILE* dom_attatchment = fopen(dom_attatchment_name_temp, "w");
    DeAllocScratch(dom_attatchment_name_temp);
    fprintf(dom_attatchment, DOM_ATTATCHMENT_INCLUDES);
    
    // Re-make Element ID header.
    ArenaString* ids_header_name = CreateString(strings);
    Append(ids_header_name, build_dir);
    Append(ids_header_name, "/");
    Append(ids_header_name, ELEMENT_ID_HEADER_NAME);
    
    char* ids_header_name_temp = Flatten(ids_header_name);
    remove(ids_header_name_temp);
    FILE* ids_header = fopen(ids_header_name_temp, "w");
    DeAllocScratch(ids_header_name_temp);
    
    ArenaString* main_calls = CreateString(strings);
    Append(main_calls, COMP_MAIN_FN_TEMPLATE);
    
    ArenaString* event_calls = CreateString(strings);
    Append(event_calls, COMP_EVENT_FN_TEMPLATE);
    
    while(curr->file_name)
    {
        // -4 to leave out .cmc, + 1 to make space for \0
        int name_len = strlen(curr->file_name);
        char* comp_name = (char*)AllocScratch((name_len - 3) * sizeof(char), no_zero());
        memcpy(comp_name, curr->file_name, (name_len - 3)*sizeof(char));
        comp_name[name_len - 4] = '\0';
        
        printf("Compiling component \"%s\"\n", comp_name);
        
        // Register the generated code-file to the DOM attatchment
        register_to_dom_attatchment(dom_attatchment, comp_name);
        
        StringView comp_name_view = {comp_name, (uint32_t)name_len};
        int comp_id = RegisterComponent(&comp_name_view, &state);
        
        FILE* component_file = fopen(curr->file_path, "r");
        SplitFileNames component_sources = SeperateSource(component_file, &source_search_values, curr->file_name, build_dir);
        
        FILE* style_source = fopen(component_sources.style_file_name, "r");
        FILE* markup_source = fopen(component_sources.markup_file_name, "r");
        FILE* code_source = fopen(component_sources.code_file_name, "r");
        
        if(!style_source || !markup_source || !code_source)
        {
            printf("Error while trying to open split source files!");
            return 1;
        }

        generated_code.file_id = comp_id;
        generated_code.file_name = comp_name;
        
        TokenizeStyle(style_source, tokens_arena, token_values_arena);
        
        ParseStyles(&styles, tokens_arena, target.values, comp_id, &state);
        //print_styles(&styles);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        Tokenize(markup_source, tokens_arena, token_values_arena);
        ProduceAST(&target, tokens_arena, token_values_arena, &state);
        
        // register the element ids from this file to the IDS header
        register_element_ids(ids_header, target.element_ids, comp_name);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        TokenizeCode(code_source, tokens_arena, token_values_arena);
        
        // Close the code source file and over-write it with the generated code
        fclose(code_source);
        FILE* component_code = fopen(component_sources.code_file_name, "w");
        generated_code.code = component_code;
        
        RegisterDirectives(&generated_code, tokens_arena, token_values_arena, &state, is_component());
        RegisterMarkupBindings(&generated_code, target.registered_bindings, tokens_arena, token_values_arena, is_component());
        

        fclose(component_code);
        
        // Create the name for the output binary
        ArenaString* output_binary_name = CreateString(strings);
        
        Append(output_binary_name, build_dir);
        Append(output_binary_name, "/");
        Append(output_binary_name, comp_name);
        Append(output_binary_name, ".bin");
        
        char* output_binary_name_temp = Flatten(output_binary_name);
        
        SavePage(&target, &styles, output_binary_name_temp, comp_id, is_component());
        
        DeAllocScratch(output_binary_name_temp);
        FreeString(output_binary_name);
        //print_ast(&target);
        
        // Add the method the DOM calls to instance the component
        add_main(main_calls, comp_id, CALL_COMP_MAIN_FN_TEMPLATE);
        // Add the method the DOM calls when an event is routed to this comp
        add_main(event_calls, comp_id, CALL_COMP_EVENT_FN_TEMPLATE);
        
        
        fclose(style_source);
        fclose(markup_source);
        
        fclose(component_file);
        
        cleanup_sources(&component_sources);
        
        printf("Finished compiling \"%s\"\n", comp_name);
        
        DeAllocScratch(comp_name);
        
        ClearRegisteredSelectors();
        reset_ast(&target);
        reset_styles(&styles);
        reset_compile_target(&generated_code);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        curr++;
    }
    // Close off the component mains
    Append(main_calls, CLOSE_MAIN_CALL_TEMLATE);
    
    Append(event_calls, CLOSE_MAIN_CALL_TEMLATE);
    
    
    // Find All .cmp files
    ResetArena(&source_search_result);
    ResetArena(&source_search_values);
    
    SearchDir(&source_search_result, &source_search_values, source_dir, ".cmp");
    curr = (FileSearchResult*)source_search_result.mapped_address;
    
    // Open page mains
    Append(main_calls, PAGE_MAIN_FN_TEMPLATE);
    
    ArenaString* frame_calls = CreateString(strings);
    Append(frame_calls, PAGE_FRAME_FN_TEMPLATE);
    
    while(curr->file_name)
    {
        // -4 to leave out .cmp, + 1 to make space for \0
        int name_len = strlen(curr->file_name);
        char* page_name = (char*)AllocScratch((name_len - 3)*sizeof(char), no_zero());
        memcpy(page_name, curr->file_name, (name_len - 3)*sizeof(char));
        page_name[name_len - 4] = '\0';
        
        printf("Compiling page \"%s\"\n", page_name);
        
        
        // Register the generated code-file to the DOM attatchment
        register_to_dom_attatchment(dom_attatchment, page_name);
        
        int page_id = state.next_file_id;
        state.next_file_id++;
        
        FILE* page_file = fopen(curr->file_path, "r");
        SplitFileNames page_sources = SeperateSource(page_file, &source_search_values, curr->file_name, build_dir);
        
        FILE* style_source = fopen(page_sources.style_file_name, "r");
        FILE* markup_source = fopen(page_sources.markup_file_name, "r");
        FILE* code_source = fopen(page_sources.code_file_name, "r");
        
        if(!style_source || !markup_source || !code_source)
        {
            printf("Error while trying to open split source files!");
            return 1;
        }

        generated_code.file_id = page_id;
        generated_code.file_name = page_name;
        
        TokenizeStyle(style_source, tokens_arena, token_values_arena);
        
        ParseStyles(&styles, tokens_arena, target.values, page_id, &state);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        Tokenize(markup_source, tokens_arena, token_values_arena);
        ProduceAST(&target, tokens_arena, token_values_arena, &state);
        
        // register the element ids from this file to the IDS header
        register_element_ids(ids_header, target.element_ids, page_name);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        TokenizeCode(code_source, tokens_arena, token_values_arena);
        
        // Close the code source file and over-write it with the generated code
        fclose(code_source);
        FILE* page_code = fopen(page_sources.code_file_name, "w");
        generated_code.code = page_code;

        RegisterDirectives(&generated_code, tokens_arena, token_values_arena, &state);
        RegisterMarkupBindings(&generated_code, target.registered_bindings, tokens_arena, token_values_arena);
        
        fclose(page_code);
        
        // Create the name for the output binary
        ArenaString* output_binary_name = CreateString(strings);
        
        Append(output_binary_name, build_dir);
        Append(output_binary_name, "/");
        Append(output_binary_name, page_name);
        Append(output_binary_name, ".bin");
        
        char* output_binary_name_temp = Flatten(output_binary_name);
        
        //print_ast(&target);

        SavePage(&target, &styles, output_binary_name_temp, page_id);
        
        DeAllocScratch(output_binary_name_temp);
        FreeString(output_binary_name);
        
        // Add the method the DOM calls when switching to this page
        add_main(main_calls, page_id, CALL_PAGE_MAIN_FN_TEMPLATE);
        
        // Add the method the runtime calls each frame for this page
        add_main(frame_calls, page_id, CALL_PAGE_FRAME_FN_TEMPLATE);
        
        fclose(style_source);
        fclose(markup_source);
        
        fclose(page_file);
        
        cleanup_sources(&page_sources);
        printf("Finished compiling page \"%s\"\n", page_name);
        
        DeAllocScratch(page_name);
        
        ClearRegisteredSelectors();
        reset_ast(&target);
        reset_styles(&styles);
        reset_compile_target(&generated_code);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        curr++;
    }
    
    // Close off the page main
    Append(main_calls, CLOSE_MAIN_CALL_TEMLATE);
    
    Append(frame_calls, CLOSE_MAIN_CALL_TEMLATE);
    
    // Generate DOM attatchment
    GenerateDOMAttatchment(dom_attatchment, &state);
    
    // Add main calls to dom attatchment
    char* flattened_main_calls = Flatten(main_calls);
    fprintf(dom_attatchment, flattened_main_calls);
    
    char* flattened_frame_calls = Flatten(frame_calls);
    fprintf(dom_attatchment, flattened_frame_calls);
    
    char* flattened_event_calls = Flatten(event_calls);
    fprintf(dom_attatchment, flattened_event_calls);
    
    fclose(dom_attatchment);
    fclose(ids_header);
    
    return 0;
}


std::map<std::string, int> registered_component_map = {};
std::map<std::string, int> registered_page_map = {};

int RegisterComponent(StringView* name, CompilerState* state)
{
    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero()); // +1 to fit \0
    memcpy(terminated_name, name->value, name->len*sizeof(char));
    terminated_name[name->len] = '\0';
    
    auto search = registered_component_map.find((const char*)terminated_name);
    
    // Component already registered, return its ID
    if(search != registered_component_map.end())
    {
        DeAllocScratch(terminated_name);
        return search->second;
    }
    
    // Register the new component
    //registered_component_map[name_string] = state->next_file_id;
    registered_component_map.insert({(const char*)terminated_name, state->next_file_id});
    state->next_file_id++;
    DeAllocScratch(terminated_name);
    
    return state->next_file_id - 1;
}

// Expected added_file_name to be \0 terminated and have no exptension!
void register_to_dom_attatchment(FILE* dom_attatchment, char* added_file_name)
{
    // For including the generated code files into the dom attatchment so it can access their methods.
    #define DOM_ATTATCHMENT_CODE_INCLUDE "#ifndef %s_MACRO\n#include \"%s.cpp\"\n#endif\n"
    fprintf(dom_attatchment, DOM_ATTATCHMENT_CODE_INCLUDE, added_file_name, added_file_name);
    
}

void register_element_ids(FILE* ids_header, Arena* ids_arena, char* file_name)
{
    ElementId* curr_id = (ElementId*)ids_arena->mapped_address;
    while(curr_id->id)
    {
        #define ELEMENT_ID_DEFINITION "#define %.*s_%s_GLOBAL_ID %d\n"
        fprintf(ids_header, ELEMENT_ID_DEFINITION, curr_id->name.len, curr_id->name.value, file_name, curr_id->id);
        
        curr_id++;
    }
}