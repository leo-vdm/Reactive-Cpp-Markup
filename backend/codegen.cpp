#include "compiler.h"
using namespace Compiler;
#include <map>
#include <cstring>

DirectiveType get_directive_from_name(char* name);
DirectiveType get_directive_from_name(char* name, int name_length);
void write_token_value(FILE* target, Token* source);
char* get_component_name(char* component_file_name, int name_length);
static bool expect_eat(TokenType expected_type);
void reset_bound_vars();

#define eat() curr_token++

////////////////////////////
//// Shared Definitions ////
////////////////////////////
#define BINDING_STUB_FN_NAME_TEMPLATE "stub_%d_%d"

// Function the dom calls to register all the individual markup binding stub functions to variables
#define REGISTER_BINDINGS_SUBSCRIBER_FN_TEMPLATE "\nvoid register_binding_subscriptions_%d(Runtime* runtime) {\n"


// Asks the dom to add a new binding expression object to its arena and returns a pointer to it
// Takes the name of the variable to put the pointer in and the name of the fn to register 
// and the id of the markup binding that it belongs to, zero for non-bindings
#define ADD_BINDING_EXPRESSION_TEMPLATE "register_bound_expr(&%s, %d);\n"

//////////////////////////
//// Page Definitions ////
//////////////////////////
#define VAR_UPDATED_FN_TEMPLATE "bound_var_changed(%d);\n"
 
#define PAGE_MAIN_STUB_FN_TEMPLATE "void page_main_%d(DOM* dom, int file_id, void** d_void_target){\nvoid* allocated = AllocPage(dom, sizeof(%s), file_id);\n((%s*)allocated)->PageMain(dom);\n*(d_void_target) = allocated;\n}\n"
#define PAGE_MAIN_STUB_FN_HEADER_TEMPLATE "void page_main_%d(DOM* dom, int file_id);\n"

///////////////////////////////
//// Component Definitions ////
///////////////////////////////

// Args: File id, component name x 2
#define COMP_MAIN_STUB_FN_TEMPLATE "void comp_main_%d(DOM* dom, int file_id, void** d_void_target){\nvoid* allocated = AllocComponent(dom, sizeof(%s), file_id);\n((%s*)allocated)->CompMain(dom);\n*(d_void_target) = allocated;\n}\n"

/////////////////////////////////////
//// DOM Attatchment Definitions ////
/////////////////////////////////////

#define REGISTER_BINDINGS_SUBSCRIBER_DOM_TEMPLATE "\nvoid register_binding_subscriptions(Runtime* runtime) {\n"

#define CALL_BINDINGS_SUBSCRIBER_FN_TEMPLATE "register_binding_subscriptions_%d(runtime);\n"


// New Stuff

// Args are comp name * 2
#define DEFINE_SELF_TEMPLATE "#define %s_MACRO 1"
#define USECOMP_INCLUDE_TEMPLATE "#ifndef %s_MACRO\n#include \"%s.cpp\"\n#endif"

// Args: stub name, comp/page class name, var/fn name
#define BINDING_TEXT_STUB_TEMPLATE "\nArenaString* %s(void* d_void, Arena* strings)\n{\nreturn make_string(((%s*)d_void)->%s, strings);\n}\n"
#define BINDING_VOID_STUB_TEMPLATE "\nvoid %s(void* d_void)\n{\n((%s*)d_void)->%s;\n}\n"


static Token* curr_token;

void RegisterDirectives(CompileTarget* target, Arena* tokens, Arena* token_values, CompilerState* state, int flags)
{
    curr_token = (Token*)tokens->mapped_address;
    
    // Define own file
    fprintf(target->code, DEFINE_SELF_TEMPLATE, target->file_name);
    
    while(curr_token->type != TokenType::END)
    {
        switch(curr_token->type)
        {
            case(TokenType::TEXT):
                {               
                write_token_value(target->code, curr_token);
                putc(' ', target->code);
                break;
                }
            case(TokenType::NEW_LINE):
                putc('\n', target->code);
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
                break;
            case(TokenType::CLOSE_BRACKET):
                putc('}', target->code);
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
                    case(DirectiveType::USECOMP):
                    {
                        if(!expect_eat(TokenType::TEXT)) // Arg for usecomp
                        {
                            printf("Expected an argument after #usecomp directive!\n");
                            return;
                        }
                        
                        char* component_name = get_component_name((char*)curr_token->token_value, curr_token->value_length);
                        fprintf(target->code, USECOMP_INCLUDE_TEMPLATE, component_name, component_name);
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
    // Add zerod expr as stopping point
    Alloc(target->bound_expressions, sizeof(BindingExpression), zero());
    
    // Add the method the dom calls when instancing this component/page
    if(flags & is_component())
    {
        fprintf(target->code, COMP_MAIN_STUB_FN_TEMPLATE, target->file_id, target->file_name, target->file_name);
    }
    else
    {
        fprintf(target->code, PAGE_MAIN_STUB_FN_TEMPLATE, target->file_id, target->file_name, target->file_name);
    }
    
}

void RegisterMarkupBindings(CompileTarget* target, Arena* markup_bindings, Arena* tokens, Arena* token_values, int flags)
{
    RegisteredBinding* curr_binding = (RegisteredBinding*)markup_bindings->mapped_address;
    BindingExpression* first_expr = (BindingExpression*)target->bound_expressions->next_address;
    
    while(curr_binding->binding_id != 0)
    {
        //curr_token = TokenizeBindingCode(tokens, token_values, curr_binding->binding_name, curr_binding->name_length);
        
        BindingExpression* added_expr = (BindingExpression*)Alloc(target->bound_expressions, sizeof(BindingExpression));
        added_expr->id = curr_binding->binding_id;
        
        memcpy(added_expr->subscribed_element_ids, curr_binding->registered_tag_ids, curr_binding->num_registered*sizeof(int));
        added_expr->subscriber_count = curr_binding->num_registered;
        
        int desired_size = snprintf(NULL, 0, BINDING_STUB_FN_NAME_TEMPLATE, target->file_id, added_expr->id);
        desired_size++; // +1 to fit \0
        added_expr->eval_fn_name = (char*)Alloc(token_values, desired_size*sizeof(char));
        sprintf(added_expr->eval_fn_name, BINDING_STUB_FN_NAME_TEMPLATE, target->file_id, added_expr->id);
        
        added_expr->name_length = desired_size;
        curr_binding++;    
    }   
    
    // Add zerod binding as stopping point
    Alloc(markup_bindings, sizeof(RegisteredBinding), zero());
    // Add zeroed expression as stopping point.
    Alloc(target->bound_expressions, sizeof(BindingExpression), zero());
    
    // Reloop and add all the bindings to the file
    curr_binding = (RegisteredBinding*)markup_bindings->mapped_address;
    BindingExpression* curr_expr = first_expr;
    char* terminated_binding_name;
    
    // Add all stub functions
    while(curr_binding->binding_id != 0)
    {   
        terminated_binding_name = (char*)AllocScratch((curr_binding->name_length + 1)*sizeof(char), no_zero());
        memcpy(terminated_binding_name, curr_binding->binding_name, curr_binding->name_length);
        terminated_binding_name[curr_binding->name_length] = '\0';

        switch(curr_binding->type)
        {
        case(RegisteredBindingType::TEXT_RET):
            fprintf(target->code, BINDING_TEXT_STUB_TEMPLATE, curr_expr->eval_fn_name, target->file_name, terminated_binding_name);
            break;
        case(RegisteredBindingType::VOID_RET):
            fprintf(target->code, BINDING_VOID_STUB_TEMPLATE, curr_expr->eval_fn_name,  target->file_name, terminated_binding_name);
            break;
        }

        curr_binding++;
        curr_expr++;
        DeAllocScratch(terminated_binding_name);
    }
    
    // Add the fn called by the dom to register all the binding stubs
    fprintf(target->code, REGISTER_BINDINGS_SUBSCRIBER_FN_TEMPLATE, target->file_id);

    curr_expr = first_expr;
    while(curr_expr->id != 0)
    {
        fprintf(target->code, ADD_BINDING_EXPRESSION_TEMPLATE, curr_expr->eval_fn_name, curr_expr->id);
        curr_expr++;
    }
    
    fprintf(target->code, "}\n");
    
    // Note(Leo): Clear bound variables after we are done with this file so that other files dont end up using the ids from this one
    reset_bound_vars();
}

void GenerateDOMAttatchment(FILE* dom_attatchment, CompilerState* state, int flags)
{
    
    
    fprintf(dom_attatchment, REGISTER_BINDINGS_SUBSCRIBER_DOM_TEMPLATE);
    for(int i = 1; i < state->next_file_id; i++)
    {
        fprintf(dom_attatchment, CALL_BINDINGS_SUBSCRIBER_FN_TEMPLATE, i);
    }
    
    fprintf(dom_attatchment,"}\n");
}

std::map<std::string, DirectiveType> directive_map = 
{
/*
    {"bind", DirectiveType::BIND },
    {"subscribeto", DirectiveType::SUBTO },
    {"subto", DirectiveType::SUBTO },
*/
    {"usecomponent", DirectiveType::USECOMP },
    {"usecomp", DirectiveType::USECOMP },
};


DirectiveType get_directive_from_name(char* name, int name_length)
{
    char* terminated_name = (char*)AllocScratch((name_length + 1)*sizeof(char), no_zero()); // + 1 to fit null terminator
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

void reset_bound_vars()
{
    bound_variables.clear();
}

char* get_component_name(char* component_file_name, int name_length)
{
    char* new_name = (char*)AllocScratch((name_length + 1)*sizeof(char), no_zero());
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