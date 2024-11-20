#include "compiler.h"
#include <map>
#include <cstring>

DirectiveType get_directive_from_name(char* name);
DirectiveType get_directive_from_name(char* name, int name_length);
int bind_variable_by_name(Arena* bound_vars, Arena* bound_var_names, char* name, int name_length);
void write_token_value(FILE* target, Token* source);
int get_bound_var_id(Arena* bound_vars, char* name, int name_length);
bool should_add_notify(BindingExpression* context, int depth_context, int bound_var_id, int flags = 0);
char* get_component_name(char* component_file_name, int name_length);
BindingExpression* register_subto_binding(Arena* bound_expressions, Arena* bound_vars, Arena*Bound_var_names, Token* first_variable);
static bool expect_eat(TokenType expected_type);

int next_bound_var_id;

#define eat() curr_token++

#define STATIC_INCLUDES "#include \"overloads.cpp\"\n"

#define COMP_INCLUDE_TEMPLATE "#include \"%s.cpp\"\n"

#define VAR_UPDATED_FN_TEMPLATE "bound_var_changed(%d);\n"

#define REGISTER_SUBSCRIBER_FN_TEMPLATE "\nvoid register_subscriber_functions() {\n"

// Function the dom calls to register all the individual markup binding stub functions to variables
#define REGISTER_BINDINGS_SUBSCRIBER_FN_TEMPLATE "\nvoid register_binding_subscriptions() {\n"

// Stub to contain markup binding expr. First arg is the stub fn name and the second arg is the user expression
// which gets contained by make_string so we get a string back.
#define BINDING_STUB_TEMPLATE_FN "\nstd::string %s()\n{\nreturn make_string(%s);\n}\n"

// Asks the dom to add a new binding expression object to its arena and returns a pointer to it
// Takes the name of the variable to put the pointer in and the name of the fn to register 
//and the id of the markup binding that it belongs to, zero for non-bindings
#define ADD_BINDING_EXPRESSION_TEMPLATE "%s = register_new_func_expr(&%s, %d);\n"


// Takes the name of a binding expression variable and the id of the variable to register to 
#define REGISTER_SUBTO_TEMPLATE "subscribe_to(%s, %d);\n"

static Token* curr_token;

void RegisterDirectives(CompileTarget* target, Arena* tokens, Arena* token_values, int flags)
{
    next_bound_var_id = 1; // ZII
    curr_token = (Token*)tokens->mapped_address;
    
    Token* insert_after_newline = NULL;
    
    BindingExpression* bound_context = NULL; // The bound fn, if any, that we are inside of
    
    int depth_context = 0; // The amount of brackets we are inside of
    
    // Write all statically included files
    fprintf(target->code, STATIC_INCLUDES);
    
    while(curr_token->type != TokenType::END)
    {
        switch(curr_token->type)
        {
            case(TokenType::TEXT):
                {
                // Check if its a declared var
                int result = get_bound_var_id(target->bound_vars, (char*)curr_token->token_value, curr_token->value_length);
                
                if(result != 0 && should_add_notify(bound_context, depth_context, result, flags)) // This is a declared bound variable, add in a update callback for it
                {
                    insert_after_newline = (Token*)Alloc(tokens, sizeof(Token));
                    int desired_size = snprintf(NULL, 0, VAR_UPDATED_FN_TEMPLATE, result);
                    desired_size++; // Need to account for the NULL terminator
                    char* bound_updated_fn = (char*)Alloc(token_values, desired_size*sizeof(char));
                    
                    sprintf(bound_updated_fn, VAR_UPDATED_FN_TEMPLATE, result);
                                            
                    insert_after_newline->type = TokenType::TEXT;
                    insert_after_newline->token_value = (void*)bound_updated_fn;
                    insert_after_newline->value_length = (desired_size - 1); // -1 to leave out the null terminator
                }
                
                write_token_value(target->code, curr_token);
                putc(' ', target->code);
                break;
                }
            case(TokenType::NEW_LINE):
                putc('\n', target->code);
                if(insert_after_newline)
                {
                    write_token_value(target->code, insert_after_newline);
                    insert_after_newline = NULL;
                }
                break;
            case(TokenType::SEMI_COLON):
                putc(';', target->code);
                break;
            case(TokenType::OPEN_PARENTHESIS):
                putc('(', target->code);
                break;
            case(TokenType::CLOSE_PARENTHESIS):
                putc(')', target->code);
                break;
            case(TokenType::QUOTE):
                putc('"', target->code);
                break;
            case(TokenType::COMMA):
                putc(',', target->code);
                break;
            case(TokenType::EQUALS):
                putc('=', target->code);
                break;
            case(TokenType::OPEN_BRACKET):
                putc('{', target->code);
                depth_context++;
                break;
            case(TokenType::CLOSE_BRACKET):
                putc('}', target->code);
                depth_context--;
                
                bound_context = NULL;
                break;
            case(TokenType::DIRECTIVE):
                {
                Token* directive_token = curr_token;
                curr_token = TokenizeDirective(tokens, token_values, curr_token);
                
                // Cant be one of our directives
                if(curr_token->type != TokenType::DIRECTIVE)
                {
                    curr_token = directive_token;
                    curr_token->type = TokenType::TEXT;
                    write_token_value(target->code, curr_token);
                    break;
                }
                
                DirectiveType type = get_directive_from_name((char*)curr_token->token_value, curr_token->value_length);
                
                // Cant be one of our directives
                if(type == DirectiveType::NONE)
                {
                    curr_token = directive_token;
                    curr_token->type = TokenType::TEXT;
                    write_token_value(target->code, curr_token);
                    break;
                }
                
                // A directive, handle it
                switch(type)
                {
                    case(DirectiveType::BIND):
                    {
                        curr_token = directive_token; // Return back to where the directive is 
                        
                        // eat #bind and expect a variable decleration directly afterwards
                        if(!expect_eat(TokenType::NEW_LINE)) // End of #bind
                        {
                            printf("Unexpected decleration after #bind directive!\n");
                            return;
                        }
                        putc('\n', target->code);
                        
                        if(!expect_eat(TokenType::TEXT)) // Variable type
                        {
                            printf("Expected a variable type identifier after #bind directive!\n");
                            return;
                        }
                        write_token_value(target->code, curr_token);
                        putc(' ', target->code);
                        
                        if(!expect_eat(TokenType::TEXT)) // Variable name
                        {
                            printf("Expected a variable decleration after #bind directive!\n");
                            return;
                        }
                        write_token_value(target->code, curr_token);
                        
                        int added_id = bind_variable_by_name(target->bound_vars, target->bound_var_names, (char*)curr_token->token_value, curr_token->value_length);
                        
                        //insert_after_newline = (Token*)Alloc(tokens, sizeof(Token));
                        //int desired_size = snprintf(NULL, 0, VAR_UPDATED_FN_TEMPLATE, added_id);
                        //desired_size++; // Need to account for the NULL terminator
                        //char* bound_updated_fn = (char*)Alloc(token_values, desired_size*sizeof(char));
                        //
                        //sprintf(bound_updated_fn, VAR_UPDATED_FN_TEMPLATE, added_id);
                        //                        
                        //insert_after_newline->type = TokenType::TEXT;
                        //insert_after_newline->token_value = (void*)bound_updated_fn;
                        //insert_after_newline->value_length = (desired_size - 1); // -1 to leave out the null terminator
                        
                        break;
                    }
                    case(DirectiveType::SUBTO):
                    {
                        
                        BindingExpression* added = register_subto_binding(target->bound_expressions, target->bound_vars, target->bound_var_names, curr_token);
                        
                        bound_context = added;
                        
                        curr_token = directive_token; // Return back to where the directive is
                        
                        if(!expect_eat(TokenType::NEW_LINE)) // End of #subto
                        {
                            printf("Unexpected decleration after #subto directive!\n");
                            return;
                        }
                        putc('\n', target->code);
                        
                        if(!expect_eat(TokenType::TEXT)) // function return type
                        {
                            printf("Expected a variable type identifier after #bind directive!\n");
                            return;
                        }
                        write_token_value(target->code, curr_token);
                        putc(' ', target->code);
                        
                        if(!expect_eat(TokenType::TEXT)) // function name
                        {
                            printf("Expected a variable decleration after #bind directive!\n");
                            return;
                        }
                        write_token_value(target->code, curr_token);
                        
                        added->eval_fn_name = (char*)curr_token->token_value;
                        added->name_length = curr_token->value_length;
                        
                        break;
                    }
                    case(DirectiveType::USECOMP):
                    {
                        if(!expect_eat(TokenType::TEXT)) // Arg for usecomp
                        {
                            printf("Expected an argument after #usecomp directive!\n");
                            return;
                        }
                        
                        char* component_name = get_component_name((char*)curr_token->token_value, curr_token->value_length);
                        fprintf(target->code, COMP_INCLUDE_TEMPLATE, component_name);
                        DeAllocScratch(component_name);
                        
                        curr_token = directive_token;
                        break;
                    }
                    default:
                        break;
                }
                break;

                }
            default:
                break;
            
        }
        eat();
    }
    
    
    // Add the subscribtion method that the dom calls for registering functions to bound vars
    BindingExpression* curr_expr = (BindingExpression*)(target->bound_expressions->mapped_address);
    fprintf(target->code, REGISTER_SUBSCRIBER_FN_TEMPLATE);
    
    char* EXPR_VAR_NAME = "added_expression";
    fprintf(target->code, "BindingExpression* %s;\n", EXPR_VAR_NAME);
    
        
    while(curr_expr->name_length != 0)
    {
        // Tell the dom to add a new expression and give us the pointer and give it the name of the user function to register to
        char* terminated_fn_name = (char*)AllocScratch((curr_expr->name_length + 1)*sizeof(char)); // + 1 to fit \0
        
        memcpy(terminated_fn_name, curr_expr->eval_fn_name, curr_expr->name_length*sizeof(char));
        terminated_fn_name[curr_expr->name_length] = '\0';
        
        fprintf(target->code, ADD_BINDING_EXPRESSION_TEMPLATE, EXPR_VAR_NAME, terminated_fn_name, 0);
        for(int i = 0; i < curr_expr->subscribed_var_count; i++)
        {   
            // Use the pointer to ask the dom to subscribe our expression to all the variables by id
            fprintf(target->code, REGISTER_SUBTO_TEMPLATE, EXPR_VAR_NAME, curr_expr->subscribed_variable_ids[i]);
        }
        
        DeAllocScratch(terminated_fn_name);
        curr_expr++;
    }
    
    fprintf(target->code, "}\n");
    
}

void RegisterMarkupBindings(CompileTarget* target, Arena* markup_bindings, Arena* tokens, Arena* token_values)
{
    #define BINDING_STUB_FN_NAME_TEMPLATE "stub_%d_%d"
    // TODO(Leo): Continue here
    RegisteredBinding* curr_binding = (RegisteredBinding*)markup_bindings->mapped_address;
    BindingExpression* first_expr = (BindingExpression*)target->bound_expressions->next_address;
    
    while(curr_binding->binding_id != 0)
    {
        curr_token = TokenizeBindingCode(tokens, token_values, curr_binding->binding_name, curr_binding->name_length);
        
        BindingExpression* added_expr = (BindingExpression*)Alloc(target->bound_expressions, sizeof(BindingExpression));
        added_expr->id = curr_binding->binding_id;
        
        memcpy(added_expr->subscribed_element_ids, curr_binding->registered_tag_ids, curr_binding->num_registered*sizeof(int));
        added_expr->subscriber_count = curr_binding->num_registered;
        
        int desired_size = snprintf(NULL, 0, BINDING_STUB_FN_NAME_TEMPLATE, target->file_id, added_expr->id);
        desired_size++; // +1 to fit \0
        added_expr->eval_fn_name = (char*)Alloc(token_values, desired_size*sizeof(char));
        sprintf(added_expr->eval_fn_name, BINDING_STUB_FN_NAME_TEMPLATE, target->file_id, added_expr->id);
        
        added_expr->name_length = desired_size;
        
        while(curr_token->type != TokenType::END)
        {   
            if(curr_token->type == TokenType::TEXT)
            {
                // Check if token is a known bound var
                int found_id = get_bound_var_id(target->bound_vars, (char*)curr_token->token_value, curr_token->value_length);
                if(found_id)
                {
                    added_expr->subscribed_variable_ids[added_expr->subscribed_var_count] = found_id;
                    added_expr->subscribed_var_count++;              
                }                    
            }
            
            
            curr_token++;
        }
        curr_binding++;    
    }   
    
    // Reloop and add all the bindings to the file
    curr_binding = (RegisteredBinding*)markup_bindings->mapped_address;
    BindingExpression* curr_expr = first_expr;
    char* terminated_binding_name;
    
    // Add all stub functions
    while(curr_binding->binding_id != 0)
    {   
        terminated_binding_name = (char*)AllocScratch((curr_binding->name_length + 1)*sizeof(char));
        memcpy(terminated_binding_name, curr_binding->binding_name, curr_binding->name_length);
        terminated_binding_name[curr_binding->name_length] = '\0';
        
        fprintf(target->code, BINDING_STUB_TEMPLATE_FN, curr_expr->eval_fn_name, terminated_binding_name);
        
        curr_binding++;
        curr_expr++;
        DeAllocScratch(terminated_binding_name);
    }
    
    // Add the fn called by the dom to register all the binding stubs
    fprintf(target->code, REGISTER_BINDINGS_SUBSCRIBER_FN_TEMPLATE);
    
    char* EXPR_VAR_NAME = "added_expression";
    fprintf(target->code, "BindingExpression* %s;\n", EXPR_VAR_NAME);
    
    curr_expr = first_expr;
    while(curr_expr->id != 0)
    {
        fprintf(target->code, ADD_BINDING_EXPRESSION_TEMPLATE, EXPR_VAR_NAME, curr_expr->eval_fn_name, curr_expr->id);
        for(int i = 0; i < curr_expr->subscribed_var_count; i++)
        {   
            // Use the pointer to ask the dom to subscribe our expression to all the variables by id
            fprintf(target->code, REGISTER_SUBTO_TEMPLATE, EXPR_VAR_NAME, curr_expr->subscribed_variable_ids[i]);
        }
        curr_expr++;
    }
    
    fprintf(target->code, "}\n");
}

std::map<std::string, DirectiveType> directive_map = 
{
    {"bind", DirectiveType::BIND },
    {"subscribeto", DirectiveType::SUBTO },
    {"subto", DirectiveType::SUBTO },
    {"usecomponent", DirectiveType::USECOMP },
    {"usecomp", DirectiveType::USECOMP },
};


DirectiveType get_directive_from_name(char* name, int name_length)
{
    char* terminated_name = (char*)AllocScratch((name_length + 1)*sizeof(char)); // + 1 to fit null terminator
    memcpy(terminated_name, name, name_length*sizeof(char));
    
    terminated_name[name_length] = '\0';
    DirectiveType result = get_directive_from_name(terminated_name);
    DeAllocScratch(terminated_name);
    return result;
}

// Note(LEO): Null terminated names only!!!
DirectiveType get_directive_from_name(char* name)
{
    std::string name_string;
    name_string = name;
    
    auto search = directive_map.find(name_string);
    if(search != directive_map.end())
    {
        return search->second;
    }
    return DirectiveType::NONE;
}

void write_token_value(FILE* target, Token* source)
{
    for(int i = 0; i < source->value_length; i++)
    {
        putc(((char*)source->token_value)[i], target);
    }
}


// Returns true if the next token is the expected type.
bool expect_eat(TokenType expected_type)
{
    eat();
    if(curr_token->type == expected_type)
    {
        return true;
    }
    return false;
}

std::map<std::string, int> bound_variables = {};

// Registers a bound variable if it doesnt exist already and returns the ID
int bind_variable_by_name(Arena* bound_vars, Arena* bound_var_names, char* name, int name_length)
{
    char* terminated_name = (char*)AllocScratch((name_length + 1)*sizeof(char)); // + 1 to fit null terminator
    memcpy(terminated_name, name, name_length*sizeof(char));
    terminated_name[name_length] = '\0';
    
    std::string name_string;
    name_string = terminated_name;
    
    auto search = bound_variables.find(name_string);
    if(search != bound_variables.end())
    {
        DeAllocScratch(terminated_name);
        return search->second;
    }
    
    BoundVariable* new_bound = (BoundVariable*)Alloc(bound_vars, sizeof(BoundVariable));
    
    char* saved_name = (char*)Alloc(bound_var_names, name_length*sizeof(char));
    memcpy(saved_name, name, name_length);
    
    new_bound->id = next_bound_var_id++;
    new_bound->var_name = saved_name;
    new_bound->name_length = name_length;
    
    
    bound_variables[name_string] = new_bound->id;
    DeAllocScratch(terminated_name);
    return new_bound->id;
}

// Note(Leo): This needs to change if the IDS system for variables does nto give the index into the bound vars arena
//BoundVariable* get_bound_var_by_name(Arena* bound_vars, char* name, int name_length)
//{
//    int id = bind_variable_by_name(bound_vars, name, name_length);
//    BoundVariable* found_var = ((BoundVariable*)bound_vars->mapped_address) + id; // Use the id as an index
//    return found_var;
//}

// Returns 0 if var is not found
int get_bound_var_id(Arena* bound_vars, char* name, int name_length)
{
    char* terminated_name = (char*)AllocScratch((name_length + 1)*sizeof(char)); // + 1 to fit null terminator
    memcpy(terminated_name, name, name_length*sizeof(char));
    terminated_name[name_length] = '\0';
        
    std::string name_string;
    name_string = terminated_name;
    
    auto search = bound_variables.find(name_string);
    if(search != bound_variables.end())
    {   
        DeAllocScratch(terminated_name);
        return search->second;
    }
    
    DeAllocScratch(terminated_name);
    return 0;
}

// Binds a fn to a bound variable
BindingExpression* register_subto_binding(Arena* bound_expressions, Arena* bound_vars, Arena* bound_var_names, Token* first_variable)
{
    BindingExpression* added_fn = (BindingExpression*)Alloc(bound_expressions, sizeof(BindingExpression));
    
    added_fn->id = 0;
    
    Token* saved_token = curr_token;
    
    curr_token = first_variable;
    while(curr_token->type != TokenType::END)
    {
        // Indicates a variable identifier
        if(curr_token->type == TokenType::TEXT)
        {
            int bound_id = bind_variable_by_name(bound_vars, bound_var_names, (char*)curr_token->token_value, curr_token->value_length);
            added_fn->subscribed_variable_ids[added_fn->subscribed_var_count] = bound_id;
            
            // Limit the subscribers from overflowing
            if(added_fn->subscribed_var_count + 1 == MAX_VARS_PER_EXPRESSION)
            {            
                printf("Warning: A bound function has gone over its subscriber limit!");
            }
            added_fn->subscribed_var_count = (added_fn->subscribed_var_count + 1) % MAX_VARS_PER_EXPRESSION;
            
        }
        eat();
    }
    
    return added_fn;
    
}

// Determines if a bound variable should be notified that its value was changed based on context.
// When a variable is changed inside of a fn that is subbed to it it will not be notified, otherwise it is always notified
bool should_add_notify(BindingExpression* context, int depth_context, int bound_var_id, int flags)
{
    int used_depth = depth_context;
    if(flags & is_component())
    {
        used_depth--; // Components are object declerations so they have a depth of 1 built in
    }
    
    printf("Le depth: %d\n", used_depth);
     
    // We are outside any function so we should not insert calls to update!
    if(used_depth < 1)
    {
        return false;
    }
    
    if(!context)
    {
        return true;
    }
    
    for(int i = 0; i < context->subscribed_var_count; i++)
    {
        if(context->subscribed_variable_ids[i] == bound_var_id)
        {
            return false;
        }
    } 
    
    return true;
}


char* get_component_name(char* component_file_name, int name_length)
{
    char* new_name = (char*)AllocScratch((name_length + 1)*sizeof(char));
    char* curr_char = component_file_name;
    for(int i = 0; i < name_length; i++)
    {   
        if(*curr_char == '"')
        {
            curr_char++;
        }
        if(*curr_char == '.')
        {
            new_name[i] = '\0';
            return new_name;
            break;
        }
        new_name[i] = *curr_char;
        curr_char++;
    }
    
    new_name[name_length] = '\0';
    return new_name;

}