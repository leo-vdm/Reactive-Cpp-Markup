#include <iostream>
#include <cstring>
#include <map>

#include "compiler.h"
#include "file_system.h"

void print_tokens(Arena* tokens, Arena* token_values);
void print_binding(int binding_id, Arena* bindings_arena);
void print_attribute(Attribute* attribute, Arena* bindings_arena);
void print_ast(AST* ast);
void print_styles(LocalStyles* glob_styles);

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
    
    created.file_id = 0;
    created.code = NULL;
    
    return created;
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
    
    Arena* tokens_arena = (Arena*)Alloc(&master_arena, sizeof(Arena));
    Arena* token_values_arena = (Arena*)Alloc(&master_arena, sizeof(Arena));
    
    *tokens_arena = CreateArena(sizeof(Token)*10000, sizeof(Token));
    *token_values_arena = CreateArena(sizeof(char)*100000, sizeof(char));
    
    while(curr->file_name)
    {
        // -4 to leave out .cmc, + 1 to make space for \0
        int name_len = strlen(curr->file_name);
        char* comp_name = (char*)AllocScratch((name_len - 3) * sizeof(char));
        memcpy(comp_name, curr->file_name, (name_len - 3)*sizeof(char));
        comp_name[name_len - 4] = '\0';
        
        RegisterComponent(comp_name, name_len, &state);
        
        DeAllocScratch(comp_name);
        
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
        int component_file_id = state.next_file_id;
        generated_code.file_id = component_file_id;
        state.next_file_id++;
        
        TokenizeStyle(style_source, tokens_arena, token_values_arena);
        
        ParseStyles(&styles, tokens_arena, token_values_arena, component_file_id, &state);
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
        
        RegisterDirectives(&generated_code, tokens_arena, token_values_arena, is_component());
        RegisterMarkupBindings(&generated_code, target.registered_bindings, tokens_arena, token_values_arena);
        
        fclose(component_code);
        
        print_ast(&target);
        
        fclose(style_source);
        fclose(markup_source);
        
        fclose(component_file);
        curr++;
    }
    
    return 0;
}



#if 0
int _main_temp(int argc, char* argv[]){
    // Initialize scratch arena
    InitScratch(sizeof(char)*10000);

    Arena file_name_arena = CreateArena(sizeof(char)*1000, sizeof(char));

    FILE* source = fopen(argv[1], "r"); // Arg to compiler should be a source file
    
    SplitFileNames sources = SeperateSource(source, argv[1], &file_name_arena);
    
    fclose(source); // Close the non split source file
    
    FILE* markup_source = fopen(sources.markup_file_name, "r"); // Open the new split markup file
    
    FILE* style_source = fopen(sources.style_file_name, "r"); // Open new split style file
    
    FILE* code_source = fopen(sources.code_file_name, "r");
    
    if(markup_source == NULL)
    {
        return 0;
    }
        
    Arena tokens = CreateArena(sizeof(Token)*10000, sizeof(Token));
    Arena token_values = CreateArena(sizeof(char)*100000, sizeof(char)); 
    
    TokenizeStyle(style_source, &tokens, &token_values);
    
    fclose(style_source);
    
    LocalStyles test_styles = LocalStyles();
    
    test_styles.next_style_id = 1;
    test_styles.next_selector_id = 1;
    
    
    test_styles.selectors = (Arena*)malloc(sizeof(Arena));
    *test_styles.selectors = CreateArena(sizeof(Selector)*10000, sizeof(Selector));
    
    test_styles.selector_values = (Arena*)malloc(sizeof(Arena));
    *test_styles.selector_values = CreateArena(sizeof(char)*10000, sizeof(char));
    
    test_styles.styles = (Arena*)malloc(sizeof(Arena));
    *test_styles.styles = CreateArena(sizeof(Style)*10000, sizeof(Style));
    
    ParseStyles(&test_styles, &tokens, &token_values, 1234);
    
    print_styles(&test_styles);
    
    ResetArena(&tokens);
    ResetArena(&token_values);
    
    //print_tokens(&tokens, &token_values);
    Tokenize(markup_source, &tokens, &token_values);
    
    fclose(markup_source);
    
    AST test_tree = AST();
    
    test_tree.tags = (Arena*)malloc(sizeof(Arena));
    *test_tree.tags = CreateArena(sizeof(Tag)*10000, sizeof(Tag));
    
    test_tree.attributes = (Arena*)malloc(sizeof(Arena));
    *test_tree.attributes = CreateArena(sizeof(Attribute)*10000, sizeof(Attribute));
    
    test_tree.registered_bindings = (Arena*)malloc(sizeof(Arena));
    *test_tree.registered_bindings = CreateArena(sizeof(RegisteredBinding)*1000, sizeof(RegisteredBinding));
    
    test_tree.values = (Arena*)malloc(sizeof(Arena));
    *test_tree.values = CreateArena(sizeof(char)*100000, sizeof(char));

    test_tree.registered_bindings_count = 0;

    ProduceAST(&test_tree, &tokens, &token_values);
    
    ResetArena(&tokens);
    ResetArena(&token_values);
    
    TokenizeCode(code_source, &tokens, &token_values);
    
    remove("test_out.cpp");
    FILE* output_test = fopen("test_out.cpp", "w");
    
    Arena bound_vars_arena = CreateArena(sizeof(BoundVariable)*1000, sizeof(BoundVariable));    
    Arena bound_expressions_arena = CreateArena(sizeof(BindingExpression)*1000, sizeof(BindingExpression));    
    
    Arena bound_var_names_arena = CreateArena(sizeof(char)*100000, sizeof(char));
    
    RegisterDirectives(output_test, &tokens, &token_values, &bound_vars_arena, &bound_var_names_arena, &bound_expressions_arena);
    RegisterMarkupBindings(output_test, test_tree.registered_bindings, &bound_vars_arena, &bound_expressions_arena, &tokens, &token_values, 1234);
    
    fclose(output_test);

    
    ResetArena(&tokens);
    ResetArena(&token_values);
    
    print_ast(&test_tree);
    
    // Save compiled AST/Styles
    
    SavePage(&test_tree, &test_styles, "test_parsed_output.bin");
    
    ResetArena(test_tree.tags);
    ResetArena(test_tree.attributes);
    ResetArena(test_tree.registered_bindings);
    ResetArena(test_tree.values);
    ResetArena(test_styles.selectors);
    ResetArena(test_styles.selector_values);
    ResetArena(test_styles.styles);
    
    // Test load    
    LoadPage(&test_tree, &test_styles, test_tree.values, "test_parsed_output.bin");
    
    SearchDir(&tokens, &token_values, ".", ".cpp");
        
    return 0;
}
#endif

std::map<std::string, int> registered_component_map = {};
std::map<std::string, int> registered_page_map = {};

int RegisterComponent(char* name, int name_length, CompilerState* state)
{
    std::string name_string;
    auto search = registered_component_map.find(name_string);
    
    // Component already registered, return its ID
    if(search != registered_component_map.end())
    {
        return search->second;
    }
    
    // Register the new component
    registered_component_map[name_string] = state->next_component_id;
    state->next_component_id++;
    
    return state->next_component_id - 1;
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

// Note(Leo): This relies on bindings existing in their arena in indexed order.
void print_binding(int binding_id, Arena* bindings_arena)
{
    RegisteredBinding* binding = ((RegisteredBinding*)bindings_arena->mapped_address) + binding_id - 1;
    
    char* terminated_name = (char*)AllocScratch((binding->name_length + 1)*sizeof(char));
    memcpy(terminated_name, binding->binding_name, binding->name_length*sizeof(char));
    terminated_name[binding->name_length] = '\0';
    
    printf("Binding Name: \"%s\", ID: %d\n", terminated_name, binding->binding_id);
    DeAllocScratch(terminated_name);

}

void print_attribute(Attribute* attribute, Arena* bindings_arena)
{
    const char* attribute_names[] = {"NONE", "CUSTOM", "TEXT", "STYLE", "CLASS"};
    char* value_container = (char*)AllocScratch((attribute->value_length) + 1);
    memcpy(value_container, attribute->attribute_value, attribute->value_length);
    value_container[attribute->value_length] = '\0';
    
    printf("Type: %s, Value: \"%s\"\n", attribute_names[(int)attribute->type], value_container);
    
    if(attribute->binding_id != 0)
    {
        print_binding(attribute->binding_id, bindings_arena);
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