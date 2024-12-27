#include <iostream>
#include <cstring>
#include <map>

#include "compiler.h"
using namespace Compiler;
#include "file_system.h"
#include "arena_string.h"

void print_tokens(Arena* tokens, Arena* token_values);
void print_binding(int binding_id, Arena* bindings_arena);
void print_attribute(Attribute* attribute, Arena* bindings_arena);
void print_ast(AST* ast);
void print_styles(LocalStyles* glob_styles);
void register_to_dom_attatchment(FILE* dom_attatchment, char* added_file_name);

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
    
    created.tags = tags_arena;
    created.attributes = attributes_arena;
    created.registered_bindings = bindings_arena;
    created.values = values_arena;
    return created;
}

void reset_ast(AST* ast)
{   
    ResetArena(ast->tags);
    ResetArena(ast->attributes);
    ResetArena(ast->registered_bindings);
    ResetArena(ast->values);
    
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
    Arena* bound_vars_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *bound_vars_arena = CreateArena(sizeof(BoundVariable)*1000, sizeof(BoundVariable));
    Arena* bound_var_names_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *bound_var_names_arena = CreateArena(sizeof(char)*100000, sizeof(char));
    Arena* bound_expressions_arena = (Arena*)Alloc(master_arena, sizeof(Arena));
    *bound_expressions_arena = CreateArena(sizeof(BindingExpression)*1000, sizeof(BindingExpression));
    
    created.bound_vars = bound_vars_arena;
    created.bound_var_names = bound_var_names_arena;
    created.bound_expressions = bound_expressions_arena;
    
    created.file_name = NULL;
    created.file_id = 0;
    created.code = NULL;
    created.header = NULL;
    
    return created;
}

void reset_compile_target(CompileTarget* target)
{
    ResetArena(target->bound_vars);
    ResetArena(target->bound_var_names);
    ResetArena(target->bound_expressions);
    
    target->code = NULL;
    target->file_name = NULL;
    target->header = NULL;
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
    char* comp_main = (char*)AllocScratch(desired_size + 1);
    sprintf(comp_main, used_template, file_id, file_id);
    Append(string, comp_main);
    DeAllocScratch(comp_main);
}


int main(int argc, char* argv[])
{
    // Initialize scratch arena
    InitScratch(sizeof(char)*10000);
    
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
    
    ArenaString* main_calls = CreateString(strings);
    Append(main_calls, COMP_MAIN_FN_TEMPLATE);
    
    while(curr->file_name)
    {
        // -4 to leave out .cmc, + 1 to make space for \0
        int name_len = strlen(curr->file_name);
        char* comp_name = (char*)AllocScratch((name_len - 3) * sizeof(char));
        memcpy(comp_name, curr->file_name, (name_len - 3)*sizeof(char));
        comp_name[name_len - 4] = '\0';
        
        printf("Compiling component \"%s\"\n", comp_name);
        
        // Register the generated code-file to the DOM attatchment
        register_to_dom_attatchment(dom_attatchment, comp_name);
        
        int comp_id = RegisterComponent(comp_name, name_len, &state);
        
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
        
        ParseStyles(&styles, tokens_arena, token_values_arena, comp_id, &state);
        //print_styles(&styles);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        Tokenize(markup_source, tokens_arena, token_values_arena);
        ProduceAST(&target, tokens_arena, token_values_arena, &state);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        TokenizeCode(code_source, tokens_arena, token_values_arena);
        
        // Close the code source file and over-write it with the generated code
        fclose(code_source);
        FILE* component_code = fopen(component_sources.code_file_name, "w");
        generated_code.code = component_code;
        // Open the header file
        FILE* component_header = fopen(component_sources.header_file_name, "w");
        generated_code.header = component_header;
        
        RegisterDirectives(&generated_code, tokens_arena, token_values_arena, &state, is_component());
        RegisterMarkupBindings(&generated_code, target.registered_bindings, tokens_arena, token_values_arena, is_component());
        
        fclose(component_header);
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
    
    
    // Find All .cmp files
    ResetArena(&source_search_result);
    ResetArena(&source_search_values);
    
    SearchDir(&source_search_result, &source_search_values, source_dir, ".cmp");
    curr = (FileSearchResult*)source_search_result.mapped_address;
    
    // Open page mains
    Append(main_calls, PAGE_MAIN_FN_TEMPLATE);
    
    while(curr->file_name)
    {
        // -4 to leave out .cmp, + 1 to make space for \0
        int name_len = strlen(curr->file_name);
        char* page_name = (char*)AllocScratch((name_len - 3)*sizeof(char));
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
        
        ParseStyles(&styles, tokens_arena, token_values_arena, page_id, &state);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        Tokenize(markup_source, tokens_arena, token_values_arena);
        ProduceAST(&target, tokens_arena, token_values_arena, &state);
        
        ResetArena(tokens_arena);
        ResetArena(token_values_arena);
        
        TokenizeCode(code_source, tokens_arena, token_values_arena);
        
        // Close the code source file and over-write it with the generated code
        fclose(code_source);
        FILE* page_code = fopen(page_sources.code_file_name, "w");
        generated_code.code = page_code;
        FILE* page_header = fopen(page_sources.header_file_name, "w");
        generated_code.header = page_header;
        
        RegisterDirectives(&generated_code, tokens_arena, token_values_arena, &state);
        RegisterMarkupBindings(&generated_code, target.registered_bindings, tokens_arena, token_values_arena);
        
        fclose(page_code);
        fclose(page_header);
        
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
    
    // Close off the page mains
    Append(main_calls, CLOSE_MAIN_CALL_TEMLATE);
    
    // Generate DOM attatchment
    GenerateDOMAttatchment(dom_attatchment, &state);
    
    // Add main calls to dom attatchment
    
    char* flattened_main_calls = Flatten(main_calls);
    
    fprintf(dom_attatchment, flattened_main_calls);
    
    fclose(dom_attatchment);
    
    return 0;
}


std::map<std::string, int> registered_component_map = {};
std::map<std::string, int> registered_page_map = {};

int RegisterComponent(char* name, int name_length, CompilerState* state)
{
    std::string name_string;
    char* terminated_name = (char*)AllocScratch((name_length + 1)*sizeof(char)); // +1 to fit \0
    memcpy(terminated_name, name, name_length*sizeof(char));
    terminated_name[name_length] = '\0';
    
    name_string = terminated_name;
    
    auto search = registered_component_map.find(name_string);
    
    // Component already registered, return its ID
    if(search != registered_component_map.end())
    {
        DeAllocScratch(terminated_name);
        return search->second;
    }
    
    // Register the new component
    registered_component_map[name_string] = state->next_file_id;
    state->next_file_id++;
    DeAllocScratch(terminated_name);
    
    return state->next_file_id - 1;
}

// Expected added_file_name to be \0 terminated and have no exptension!
void register_to_dom_attatchment(FILE* dom_attatchment, char* added_file_name)
{
    // For including the generated code files into the dom attatchment so it can access their methods.
    #define DOM_ATTATCHMENT_CODE_INCLUDE "#include \"%s.h\"\n"
    fprintf(dom_attatchment, DOM_ATTATCHMENT_CODE_INCLUDE, added_file_name);
    
}

void print_tokens(Arena* tokens, Arena* token_values)
{   
    const char* token_names[] = {  "OPEN_TAG", "CLOSE_TAG", "OPEN_BRACKET", "CLOSE_BRACKET", "EQUALS", "QUOTE", "SLASH", "TEXT", "TAG_START", "TAG_END", "TAG_ATTRIBUTE", "ATTRIBUTE_IDENTIFIER", "ATTRIBUTE_VALUE", "COLON", "SEMI_COLON", "COMMA", "OPEN PAREN", "CLOSE PAREN", "DIRECTIVE", "NEWLINE", "END"};
    Token* curr_token = (Token*)(tokens->mapped_address);
    while(curr_token->type != TokenType::END)
    {
        printf("Current token: %s\n", token_names[(int)curr_token->type]);
        printf("Current token value:\n");
        
        if(curr_token->type == TokenType::TAG_ATTRIBUTE && token_values != NULL)
        {
            printf("Hit Attribute");
            Token* base_attribute = TokenizeAttribute(tokens, token_values, curr_token);
            while(base_attribute->type != TokenType::END)
            {
                printf("\nCurrent attribute token: %s\n", token_names[(int)base_attribute->type]);
                printf("Current attribute token value:\n");
                
                char* value_base = (char*)base_attribute->token_value;
                for(int i = 0; i < base_attribute->value_length; i++){
                    putchar(*value_base);
                    value_base++;
                }
                ;
                base_attribute++;
            }
            printf("\nFinished attribute\n");
        }
        
        char* value_base = (char*)curr_token->token_value;
        for(int i = 0; i < curr_token->value_length; i++){
            putchar(*value_base);
            value_base++;
        }
        
        printf("\n");
        
        curr_token++;
    }

}

#if 0
void print_attribute(Attribute* attribute, Arena* bindings_arena)
{
    const char* attribute_names[] = {"NONE", "CUSTOM", "TEXT", "STYLE", "CLASS"};
    char* value_container = (char*)AllocScratch((attribute->value_length) + 1);
    memcpy(value_container, attribute->attribute_value, attribute->value_length);
    value_container[attribute->Text.value_length] = '\0';
    
    printf("Type: %s, Value: \"%s\"\n", attribute_names[(int)attribute->type], value_container);
    
    if(attribute->binding_id != 0)
    {
        printf("Binding ID: %d Binding position: %d\n", attribute->binding_id, attribute->binding_position);
        //print_binding(attribute->binding_id, bindings_arena);
    }
    
    DeAllocScratch(value_container);
}

void print_ast(AST* ast)
{   
    const char* tag_names[] = { "ROOT", "TEXT", "DIV", "CUSTOM"};
    Tag* curr_tag = ast->root_tag;
    while(curr_tag != NULL)
    {
        
        printf("Current tag: %s\n", tag_names[(int)curr_tag->type]);
        printf("ID: %d\n", curr_tag->tag_id);
        
        if(curr_tag->num_attributes != 0)
        {
            for(int i = 0; i < curr_tag->num_attributes; i++)
            print_attribute(curr_tag->first_attribute + i, ast->registered_bindings);
        }
        
        
        if(curr_tag->first_child)
        {
            printf("First child: %d\n", curr_tag->first_child->tag_id);
            curr_tag = curr_tag->first_child;
            continue;
        }
        if(!curr_tag->next_sibling){
            while(!curr_tag->next_sibling)
            {
                // Got back to root
                if(!curr_tag->parent)
                {
                    return;
                }
                curr_tag = curr_tag->parent;            
            }
            curr_tag = curr_tag->next_sibling;
            continue;
        }
        curr_tag = curr_tag->next_sibling;
        
    }

}

void print_styles(LocalStyles* glob_styles)
{
    printf("\n\t--== Selectors ==--");
    Selector* curr_selector = ((Selector*)glob_styles->selectors->mapped_address);
    while(curr_selector->global_id != 0)
    {
        char* name = (char*)AllocScratch((curr_selector->name_length + 1)*sizeof(char));
        memcpy(name, curr_selector->name, curr_selector->name_length);
        name[curr_selector->name_length] = '\0';
        
        printf("\n\t-INFO-\n");
        printf("\tName: %s\n", name);
        printf("\tID: %d\n", curr_selector->global_id);
        printf("\t# of styles: %d\n", curr_selector->num_styles);
        DeAllocScratch(name);
        curr_selector++;
    }
    
    printf("\n\t--== Styles ==--");
    Style* curr_style = ((Style*)glob_styles->styles->mapped_address);
    while(curr_style->global_id != 0)
    {   
        printf("\n\t-INFO-\n");
        printf("\tID: %d\n", curr_style->global_id);
        printf("\tWidth=%f/Height=%f\n", curr_style->width.size, curr_style->height.size);
        printf("\t(Max)Width=%f/Height=%f\n", curr_style->max_width.size, curr_style->max_height.size);
        
        curr_style++;
    }
}

#endif
