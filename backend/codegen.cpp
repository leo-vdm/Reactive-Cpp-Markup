#include "compiler.h"
using namespace Compiler;
#include <map>
#include <cstring>

DirectiveType get_directive_from_name(char* name);
DirectiveType get_directive_from_name(StringView* name);
void write_token_value(FILE* target, Token* source);
StringView get_component_name(StringView* name);
static bool expect_eat(TokenType expected_type);

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
#define PAGE_FRAME_STUB_FN_TEMPLATE "void page_frame_%d(DOM* dom, void* d_void){\n\t((%s*)d_void)->OnFrame(dom);\n}\n"

///////////////////////////////
//// Component Definitions ////
///////////////////////////////

// Args: File id, component name x 2
#define COMP_MAIN_STUB_FN_TEMPLATE "void comp_main_%d(DOM* dom, int file_id, void** d_void_target, CustomArgs* ARGS){\nvoid* allocated = AllocComponent(dom, sizeof(%s), file_id);\n((%s*)allocated)->CompMain(dom, ARGS);\n*(d_void_target) = allocated;\n}\n"
#define COMP_EVENT_STUB_FN_TEMPLATE "void comp_event_%d(DOM* dom, Event* event, void* d_void){\n((%s*)d_void)->OnEvent(dom, event);\n}\n"

/////////////////////////////////////
//// DOM Attatchment Definitions ////
/////////////////////////////////////

#define REGISTER_BINDINGS_SUBSCRIBER_DOM_TEMPLATE "\nvoid register_binding_subscriptions(Runtime* runtime) {\n"

#define CALL_BINDINGS_SUBSCRIBER_FN_TEMPLATE "register_binding_subscriptions_%d(runtime);\n"

// Args are comp name * 2
#define DEFINE_SELF_TEMPLATE "#define %s_MACRO 1"
#define USECOMP_INCLUDE_TEMPLATE "#ifndef %.*s_MACRO\n#include \"%.*s.cpp\"\n#endif"

// Args: stub name, comp/page class name, var/fn name
#define BINDING_TEXT_STUB_TEMPLATE "\nArenaString* %s(void* d_void, Arena* strings)\n{\nreturn make_string(((%s*)d_void)->%s, strings);\n}\n"
#define BINDING_VOID_STUB_TEMPLATE "\nvoid %s(void* d_void)\n{\nauto e = (%s*)d_void;\n%s;\n}\n"
#define BINDING_VOID_BOOL_STUB_TEMPLATE "\nvoid %s(void* d_void, bool arg0)\n{\nauto e = (%s*)d_void;\n%s;\n}\n"
#define BINDING_BOOL_STUB_TEMPLATE "\nbool %s(void* d_void)\n{\nauto e = (%s*)d_void;\n%s;\n}\n"
#define BINDING_VOID_PTR_STUB_TEMPLATE "\nvoid %s(void* d_void, void* ptr_void)\n{\n((%s*)d_void)->%s = ptr_void;\n}\n"
#define BINDING_PTR_STUB_TEMPLATE "\nvoid* %s(void* d_void)\n{\nreturn (void*)((%s*)d_void)->%s;\n}\n"
#define BINDING_INT_STUB_TEMPLATE "\nint %s(void* d_void)\n{\nauto e = (%s*)d_void;\n%s;\n}\n"
#define BINDING_ARG_STUB_TEMPLATE "\nvoid %s(void*d_void, CustomArgs* ARGS)\n{\nauto e = (%s*)d_void;\n *ARGS = {%s};\nARGS->count = %d;\n}\n"

// Args: stub name, array type name, var/fn name
#define BINDING_ARR_TEXT_STUB_TEMPLATE "\nArenaString* %s(void* a_void, Arena* strings, int index)\n{\nreturn make_string(((%.*s*)a_void + index)->%s, strings);\n}\n"
#define BINDING_ARR_VOID_STUB_TEMPLATE "\nvoid %s(void* a_void, void* d_void, int index)\n{\nauto a = (%.*s*)a_void; auto e = (%s*)d_void; %s;\n}\n"
#define BINDING_ARR_VOID_BOOL_STUB_TEMPLATE "\nvoid %s(void* a_void, void* d_void, int index, bool arg0)\n{\nauto a = (%.*s*)a_void; auto e = (%s*)d_void; %s;\n}\n"
#define BINDING_ARR_BOOL_STUB_TEMPLATE "\nbool %s(void* a_void, void* d_void, int index)\n{\nauto a = (%.*s*)a_void; auto e = (%s*)d_void; %s;\n}\n"
#define BINDING_ARR_VOID_PTR_STUB_TEMPLATE "\nvoid %s(void* a_void, int index, void* ptr_void)\n{\n((%.*s*)a_void + index)->%s = ptr_void;\n}\n"
#define BINDING_ARR_PTR_STUB_TEMPLATE "\nvoid* %s(void* a_void, int index)\n{\nreturn (void*)((%.*s*)a_void + index)->%s;\n}\n"
#define BINDING_ARR_INT_STUB_TEMPLATE "\nint %s(void* a_void, void* d_void, int index)\n{\nauto a = (%.*s*)a_void;\nauto e = (%s)d_void;\n %s;\n}\n"
#define BINDING_ARR_ARG_STUB_TEMPLATE "\nvoid %s(void* a_void, void* d_void, int index, CustomArgs* ARGS)\n{\nauto a = (%.*s*)a_void;\nauto e = (%s*)d_void;\n *ARGS = {%s};\nARGS->count = %d;\n}\n"

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
                
                DirectiveType type = get_directive_from_name(&curr_token->body);
                
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
                        
                        StringView component_name = get_component_name(&curr_token->body);
                        fprintf(target->code, USECOMP_INCLUDE_TEMPLATE, component_name.len, component_name.value, component_name.len, component_name.value);
                             
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
    // Add zero-ed expr as stopping point
    Alloc(target->bound_expressions, sizeof(BindingExpression), zero());
    
    // Add the method the dom calls when instancing this component/page
    if(flags & is_component())
    {
        fprintf(target->code, COMP_MAIN_STUB_FN_TEMPLATE, target->file_id, target->file_name, target->file_name);
        fprintf(target->code, COMP_EVENT_STUB_FN_TEMPLATE, target->file_id, target->file_name);
    }
    else
    {
        fprintf(target->code, PAGE_MAIN_STUB_FN_TEMPLATE, target->file_id, target->file_name, target->file_name);
        fprintf(target->code, PAGE_FRAME_STUB_FN_TEMPLATE, target->file_id, target->file_name);
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

        if(curr_binding->context == BindingContext::GLOBAL)
        {
            switch(curr_binding->type)
            {
            case(RegisteredBindingType::TEXT_RET):
                fprintf(target->code, BINDING_TEXT_STUB_TEMPLATE, curr_expr->eval_fn_name, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::VOID_RET):
                fprintf(target->code, BINDING_VOID_STUB_TEMPLATE, curr_expr->eval_fn_name,  target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::VOID_PTR):
                fprintf(target->code, BINDING_VOID_PTR_STUB_TEMPLATE, curr_expr->eval_fn_name,  target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::BOOL_RET):
                fprintf(target->code, BINDING_BOOL_STUB_TEMPLATE, curr_expr->eval_fn_name,  target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::PTR_RET):
                fprintf(target->code, BINDING_PTR_STUB_TEMPLATE, curr_expr->eval_fn_name, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::INT_RET):
                fprintf(target->code, BINDING_INT_STUB_TEMPLATE, curr_expr->eval_fn_name, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::VOID_BOOL_RET):
                fprintf(target->code, BINDING_VOID_BOOL_STUB_TEMPLATE, curr_expr->eval_fn_name,  target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::ARG_RET):
             {
                int arg_count = 1; // Should have atleast 1
                char* curr_char = terminated_binding_name;
                while(*curr_char != '\0')
                {
                    // Count the commas to find the number of args
                    if(*curr_char == ',')
                    {
                        arg_count++;
                    }
                    curr_char++;
                }
            
                fprintf(target->code, BINDING_ARG_STUB_TEMPLATE, curr_expr->eval_fn_name,  target->file_name, terminated_binding_name, arg_count);
                break;
             }
            }
        }
        else if(curr_binding->context == BindingContext::LOCAL)
        {
            switch(curr_binding->type)
            {
            case(RegisteredBindingType::TEXT_RET):
                fprintf(target->code, BINDING_ARR_TEXT_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, terminated_binding_name);
                break;
            case(RegisteredBindingType::VOID_RET):
                fprintf(target->code, BINDING_ARR_VOID_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::VOID_PTR):
                fprintf(target->code, BINDING_ARR_VOID_PTR_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, terminated_binding_name);
                break;
            case(RegisteredBindingType::BOOL_RET):
                fprintf(target->code, BINDING_ARR_BOOL_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::PTR_RET):
                fprintf(target->code, BINDING_ARR_PTR_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, terminated_binding_name);
                break;
            case(RegisteredBindingType::INT_RET):
                fprintf(target->code, BINDING_ARR_INT_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::VOID_BOOL_RET):
                fprintf(target->code, BINDING_ARR_VOID_BOOL_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, target->file_name, terminated_binding_name);
                break;
            case(RegisteredBindingType::ARG_RET):
             {
                int arg_count = 1; // Should have atleast 1
                char* curr_char = terminated_binding_name;
                while(curr_char)
                {
                    // Count the commas to find the number of args
                    if(*curr_char == ',')
                    {
                        arg_count++;
                    }
                }
             
                fprintf(target->code, BINDING_ARR_ARG_STUB_TEMPLATE, curr_expr->eval_fn_name, curr_binding->context_name.len, curr_binding->context_name.value, target->file_name, terminated_binding_name, arg_count);
                break;
             }
            }
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


DirectiveType get_directive_from_name(StringView* name)
{
    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero()); // + 1 to fit null terminator
    memcpy(terminated_name, name->value, name->len*sizeof(char));
    
    terminated_name[name->len] = '\0';
    DirectiveType result = get_directive_from_name(terminated_name);
    DeAllocScratch(terminated_name);
    return result;
}

// Note(Leo): Null terminated names only!!!
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
    fwrite(source->body.value, source->body.len, 1, target);
/*
    for(int i = 0; i < source->value_length; i++)
    {
        putc(((char*)source->token_value)[i], target);
    }
*/
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

StringView get_component_name(StringView* name)
{
    StringView comp_name = {};
    comp_name.value = name->value;
    // Go until finding the . in the file name which indicates the end of the component name
    // skip qoutes 
    for(int i = 0; i < name->len; i++)
    {   
        if(name->value[i] == '"')
        {
            comp_name.value++;
            continue;
        }
        if(name->value[i] == '.')
        {
            return comp_name;
            break;
        }
        comp_name.len++;
    }

    return comp_name;

}