#include <cstring>
#include <cassert>
#include <map>

#include "compiler.h"
using namespace Compiler;

static Token* curr_token; 

#define eat() curr_token++
static bool expect_eat(TokenType expected_type);
Attribute* parse_attribute_expr(AST* target, Tag* parent_tag, Token* attribute_start_token, CompilerState* state);
void aggregate_selectors(LocalStyles* target, int style_id, int global_prefix, CompilerState* state);
StyleFieldType parse_field_name(StringView* name);
Measurement parse_size_field(StringView* expression);
void parse_style_expr(Style* target, Arena* values_arena);
void clear_registered_bindings();
void clear_registered_ids();
void sanity_check_style(Style* target);

struct ast_context
{
    Arena* tags;
    int curr_tag_id;
    StringView context_name;
};

void ProduceAST(AST* target, Arena* tokens, Arena* token_values, CompilerState* state)
{
    void* context_stack_memory = AllocScratch(sizeof(ast_context)*101);
    ast_context* context_stack = (ast_context*)align_mem(context_stack_memory, ast_context);
    int context_depth = 0;
    
    #define push_context(added, name) context_depth++;context_stack[context_depth - 1] = {added, 0, name}
    #define pop_context() context_depth--
    #define context_arena() (context_stack[context_depth - 1].tags)
    #define context_tag_id() (context_stack[context_depth - 1].curr_tag_id)
    #define context_name() (context_stack[context_depth - 1].context_name)
    #define push_tag() (Tag*)Alloc(context_arena(), sizeof(Tag))
    
    StringView base_context_name = {};
    push_context(target->tags, base_context_name);
    
    curr_token = (Token*)tokens->mapped_address;
    Tag* curr_tag = push_tag();
    
    // First tag is always root
    // Note(Leo): Tag IDS are local to the file, other systems use global ids. Element ids are decided at runtime.
    context_tag_id() = 1;
    curr_tag->type = TagType::ROOT;
    curr_tag->tag_id = context_tag_id()++;
    
    curr_tag->first_attribute = NULL;
    curr_tag->parent = NULL;
    curr_tag->next_sibling = NULL;
    curr_tag->num_attributes = 0;
    
    target->root_tag = curr_tag;
    
    bool has_ended = false;
    
    const char* token_names[] = {  "OPEN_TAG", "CLOSE_TAG", "OPEN_BRACKET", "CLOSE_BRACKET", "EQUALS", "QUOTE", "SLASH", "TEXT", "TAG_START", "TAG_END", "TAG_ATTRIBUTE", "ATTRIBUTE_IDENTIFIER", "ATTRIBUTE_VALUE", "COLON", "SEMI_COLON", "COMMA", "END"};
    
    
    while(curr_token->type != TokenType::END)
    {    
        switch(curr_token->type)
        {
            case (TokenType::TAG_START):
            {
                Tag* new_tag = push_tag();
                
                new_tag->context_name = context_name();
                
                // Indicates new tag is a sibling
                if(has_ended)
                {
                    // Siblings share a parent
                    new_tag->parent = curr_tag->parent;
                    
                    curr_tag->next_sibling = new_tag;
                    has_ended = false;
                    
                    curr_tag = new_tag;
                }
                else
                {   // Indicates new tag is a child
                    // This must be the first child
                    new_tag->parent = curr_tag;
                    
                    curr_tag->first_child = new_tag;
                    has_ended = false;
                    
                    curr_tag = new_tag;
                } 
                
                // Find the tag's type
                curr_tag->type = GetTagFromName(&curr_token->body);
                
                // Zero indicates this tag is not a component
                int custom_component_id = 0;
                
                if(curr_tag->type == TagType::CUSTOM)
                {
                    custom_component_id = RegisterComponent(&curr_token->body, state);
                }
                // Note(Leo): Each is special cos its only allowed the loop attribute, it cant have others cos it is 
                //            not a "real" element and also needs to push a new context to put its child tags into a
                //            template.
                else if(curr_tag->type == TagType::EACH) 
                {
                    curr_tag->tag_id = context_tag_id()++;
                    eat();
                    
                    if(curr_token->type != TokenType::TAG_ATTRIBUTE)
                    {
                        printf("Error: The Each element must have a \"loop\" attribute\n");
                        break;
                    }
                    
                    // Tokenize the attribute and then eat the token
                    Token* first_attribute_token = TokenizeAttribute(tokens, token_values, curr_token);
                    curr_tag->first_attribute = parse_attribute_expr(target, curr_tag, first_attribute_token, state);
                    assert(curr_tag->first_attribute->type == AttributeType::LOOP);
                    
                    RegisteredTemplate* each_template = RegisterTemplate(target->templates, state);
                    push_context(&each_template->tags, curr_tag->first_attribute->Loop.type_name);
                    context_tag_id() = 1;
                                        
                    curr_tag->first_attribute->Loop.template_id = each_template->template_id;
                    
                    eat();  
                    break;
                }
                
                curr_tag->tag_id = context_tag_id()++;
                
                eat();
                curr_tag->first_attribute = NULL;
                if(curr_token->type == TokenType::TAG_ATTRIBUTE)
                {
                    // Tokenize the attribute and then eat the token
                    Token* first_attribute_token = TokenizeAttribute(tokens, token_values, curr_token);
                    eat();                    
                    
                    curr_tag->first_attribute = parse_attribute_expr(target, curr_tag, first_attribute_token, state);
                
                }
                
                if(custom_component_id)
                {
                    // NOTE(Leo): This code assumes that the attribute we allocate here is contiguos to the last allocated block!
                    curr_tag->num_attributes++;
                    Attribute* added_attribute = (Attribute*)Alloc(target->attributes, sizeof(Attribute));
                    
                    added_attribute->type = AttributeType::COMP_ID;
                    
                    added_attribute->CompId.id = custom_component_id;
                    
                    
                    // Note(Leo): If the component had no other attributes we need to set this otherwise it doesnt know about its comp_id
                    if(!curr_tag->first_attribute)
                    {
                        curr_tag->first_attribute = added_attribute;
                    }
                }
                
                if(!(curr_token->type == TokenType::CLOSE_TAG)) // Tag should close after attribute)
                {
                    printf("Expected tag closure, got %s instead!\n", token_names[(int)curr_token->type]);
                }
            
                break;
            }
            case (TokenType::TAG_END):
            {
                TagType expected_type; 
                
                if(has_ended)
                {   // Indicates ending the parent element
                    //Note(Leo): Since we are ending the parent element, the type of the closing tag should match the parent
                    expected_type = curr_tag->parent->type;
                    
                    curr_tag->next_sibling = NULL;
                    curr_tag = curr_tag->parent;
                    has_ended = true;
                }
                else // Indicates ending the current element
                {
                    has_ended = true;
                    
                    // We are closing current element so the closing tag type should match its type
                    expected_type = curr_tag->type;
                }
                
                if(expected_type == TagType::EACH && curr_tag->type != TagType::TEXT)
                {
                    pop_context();
                }
                
                if(curr_tag->type != TagType::TEXT && expected_type != GetTagFromName(&curr_token->body))
                {
                    printf("Unexpected tag end!\n");
                }
                
                break;
            }
            case(TokenType::TEXT):
            {
                Tag* new_tag = push_tag();          
                
                new_tag->context_name = context_name();
                
                if(has_ended) // Indicates text is a sibling
                {
                    curr_tag->next_sibling = new_tag;
                    
                    new_tag->parent = curr_tag->parent;
                    curr_tag = new_tag;
                    has_ended = true; // Text is special and is not followed by an ending token so auto end it
                
                }
                else // Indicates text is a child
                {
                    curr_tag->first_child = new_tag;
                    new_tag->parent = curr_tag;
                    
                    curr_tag = new_tag;
                    has_ended = true;// Text is special and is not followed by an ending token so auto end it                
                }
                
                curr_tag->type = TagType::TEXT;
                curr_tag->tag_id = context_tag_id()++;
                curr_tag->num_attributes = 1;
                
                Attribute* text_content = (Attribute*)Alloc(target->attributes, sizeof(Attribute), zero());
                text_content->type = AttributeType::TEXT;
                
                StringView text_value = StripOuterWhitespace(&curr_token->body);
                
                text_content->Text.value = (char*)Alloc(target->values, text_value.len*sizeof(char));
                text_content->Text.value_length = text_value.len;
                 
                memcpy((void*)text_content->Text.value, text_value.value, text_value.len*sizeof(char));
                
                curr_tag->first_attribute = text_content;
                break;
            }
            case(TokenType::OPEN_BRACKET):
            {
                bool is_local = false;
                
                // Double brackets are local bindings
                if((curr_token + 1)->type == TokenType::OPEN_BRACKET)
                {
                    is_local = true;
                    eat();
                }    
                
                if(!expect_eat(TokenType::TEXT))
                {
                    printf("Expeced binding definition!");
                }
                
                Tag* new_tag = push_tag();    
                
                new_tag->context_name = context_name();
                if(has_ended) // Indicates text binding is a sibling
                {
                    curr_tag->next_sibling = new_tag;
                    
                    new_tag->parent = curr_tag->parent;
                    curr_tag = new_tag;
                    has_ended = true; // Text binding is special and is not followed by an ending token so auto end it
                
                }
                else // Indicates text is a child
                {
                    curr_tag->first_child = new_tag;
                    new_tag->parent = curr_tag;
                    
                    curr_tag = new_tag;
                    has_ended = true;// Text is special and is not followed by an ending token so auto end it                
                }
                
                curr_tag->type = TagType::TEXT;
                curr_tag->tag_id = context_tag_id()++;
                curr_tag->num_attributes = 1;
                
                // need an attribute to attatch the binding to
                Attribute* text_content = (Attribute*)Alloc(target->attributes, sizeof(Attribute));
                text_content->type = AttributeType::TEXT;
                text_content->Text.value = NULL;
                text_content->Text.value_length = 0;
                text_content->Text.binding_position = 0;
                
                text_content->Text.binding_id = RegisterBindingByName(target->registered_bindings, target->values, &curr_token->body, RegisteredBindingType::TEXT_RET, is_local, state, curr_tag->context_name);
                                
                curr_tag->first_attribute = text_content;
                
                if(is_local)
                {
                    if(!expect_eat(TokenType::CLOSE_BRACKET))
                    {
                        printf("Expeced } for local binding!\n");
                    }
                }
                
                if(!expect_eat(TokenType::CLOSE_BRACKET))
                {
                    printf("Expeced } for binding!\n");
                }
                
                break;
            }
        }
        
        eat();
    }
    
    // Note(Leo): Clear the registered bindings here so files dont end up using eachothers ids
    clear_registered_bindings();
    clear_registered_ids();
    Alloc(target->registered_bindings, sizeof(RegisteredBinding), zero());
    
    // Note(Leo): File system save fn expects a zeroed tag to mark the end of the page/comp
    Alloc(target->tags, sizeof(Tag), zero());
    // Note(leo): same for attributes
    Alloc(target->attributes, sizeof(Attribute), zero());
    Alloc(target->templates, sizeof(RegisteredTemplate), zero());
    Alloc(target->element_ids, sizeof(ElementId), zero());
    DeAllocScratch(context_stack_memory);
}

std::map<std::string, TagType> tag_map = 
{
    {"text", TagType::TEXT },
    {"hdiv", TagType::HDIV },
    {"button", TagType::HDIV }, // Buttons are just a macro for hdiv
    {"vdiv", TagType::VDIV },
    {"img", TagType::IMG },
    {"each", TagType::EACH },
    //{"end", TagType::CUSTOM},
};

std::map<std::string, AttributeType> attribute_map = 
{
    {"text", AttributeType::TEXT },
    {"class", AttributeType::CLASS },
    {"style", AttributeType::STYLE },
    {"src", AttributeType::SRC },
    {"onclick", AttributeType::ON_CLICK },
    {"this", AttributeType::THIS_ELEMENT },
    {"condition", AttributeType::CONDITION },
    {"loop", AttributeType::LOOP },
    {"onfocus", AttributeType::ON_FOCUS },
    {"focusable", AttributeType::FOCUSABLE },
    {"id", AttributeType::ID },
    {"args", AttributeType::CUSTOM},
    {"ticking", AttributeType::TICKING},
};


std::map<std::string, int> registered_binding_map = {};
std::map<std::string, int> element_id_map = {};

TagType GetTagFromName(StringView* name)
{   
    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero());
    memcpy(terminated_name, name->value, name->len*sizeof(char));
    
    terminated_name[name->len] = '\0';
        
    auto search = tag_map.find((const char*)terminated_name);
    
    DeAllocScratch(terminated_name);
    
    if(search != tag_map.end()){
        return search->second;
    }
    return TagType::CUSTOM;
}

AttributeType GetAttributeFromName(StringView* name)
{   
    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero());
    memcpy(terminated_name, name->value, name->len*sizeof(char));
    
    terminated_name[name->len] = '\0';
        
    auto search = attribute_map.find((const char*)terminated_name);
    
    DeAllocScratch(terminated_name);
    
    if(search != attribute_map.end()){
        return search->second;
    }
    return AttributeType::NONE;
}

RegisteredTemplate* RegisterTemplate(Arena* templates_arena, Compiler::CompilerState* state)
{
    RegisteredTemplate* new_template = (RegisteredTemplate*)Alloc(templates_arena, sizeof(RegisteredTemplate), zero());
    new_template->tags = CreateArena(2000*sizeof(Tag), sizeof(Tag));
    new_template->template_id = state->next_template_id++;
    
    return new_template;
}

int RegisterElementIdByName(Arena* element_ids_arena, Arena* values_arena, Compiler::CompilerState* state, StringView* name)
{
    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, name->value, name->len*sizeof(char)); 
    
    terminated_name[name->len] = '\0';
    
    auto search = element_id_map.find((const char*)terminated_name);
    
    // If the binding is already registered, return its ID
    if(search != element_id_map.end()){
        // Register the tag with the binding
        int element_id = search->second;
        
        DeAllocScratch(name);
        return search->second;
    }
    
    ElementId* new_id = (ElementId*)Alloc(element_ids_arena, sizeof(ElementId));
    new_id->id = state->next_element_id++;
    
    char* saved_name = (char*)Alloc(values_arena, name->len*sizeof(char));
    memcpy(saved_name, name->value, name->len*sizeof(char));
    new_id->name = {saved_name, name->len};
    
    element_id_map.insert({(const char*)terminated_name, new_id->id});
    DeAllocScratch(terminated_name);
    return new_id->id;
}

int RegisterBindingByName(Arena* bindings_arena, Arena* values_arena, StringView* name, Compiler::RegisteredBindingType type, bool is_local, Compiler::CompilerState* state, StringView context_name)
{
    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, name->value, name->len*sizeof(char)); 
    
    terminated_name[name->len] = '\0';
     
    auto search = registered_binding_map.find((const char*)terminated_name);
    
    // If the binding is already registered, return its ID
    if(search != registered_binding_map.end())
    {
        // Register the tag with the binding
        int binding_id = search->second;
        
        DeAllocScratch(name);
        return search->second;
    }
    
    // Register the binding since it doesnt exist
    RegisteredBinding* new_binding = (RegisteredBinding*)Alloc(bindings_arena, sizeof(RegisteredBinding));
    new_binding->binding_id = state->next_bound_expr_id;
    state->next_bound_expr_id++;
    
    char* saved_name = (char*)Alloc(values_arena, name->len*sizeof(char));
    memcpy(saved_name, name->value, name->len*sizeof(char));

    new_binding->binding_name = saved_name;
    new_binding->name_length = name->len;
    
    new_binding->type = type;
    
    if(is_local)
    {
        new_binding->context = BindingContext::LOCAL;
        new_binding->context_name = context_name;   
    }
    else
    {
        new_binding->context = BindingContext::GLOBAL;
    }
    
    registered_binding_map.insert({(const char*)terminated_name, new_binding->binding_id});
    DeAllocScratch(terminated_name);
    return new_binding->binding_id;
}

void clear_registered_bindings()
{
    registered_binding_map.clear();
}

void clear_registered_ids()
{
    element_id_map.clear();
}

std::map<std::string, Selector*> registered_selector_map = {};

void ClearRegisteredSelectors()
{
    registered_selector_map.clear();
}

void ParseStyles(LocalStyles* target, Arena* style_tokens, Arena* values_arena, int file_prefix, CompilerState* state)
{
    #define push_style() (Style*)Alloc(target->styles, sizeof(Style), zero())
    
    curr_token = (Token*)style_tokens->mapped_address;
    Style* curr_style;
    bool style_opened = false;
    while(curr_token->type != TokenType::END)
    {
        switch(curr_token->type)
        {
            case(TokenType::TEXT):
            {
                // Indicates a selector
                if(!style_opened)
                {
                    curr_style = push_style();
                    curr_style->global_id = state->next_style_id;
                    state->next_style_id++;
                    // Aggregate selectors
                    aggregate_selectors(target, curr_style->global_id, file_prefix, state);

                    style_opened = true;
                }
                else // Style attribute decleration.
                {
                    parse_style_expr(curr_style, values_arena);
                }
                break;
            }
            case(TokenType::CLOSE_BRACKET):
            {
                style_opened = false;
                if(!curr_style)
                {
                    printf("Unexpected closing bracket while parsing style!\n");
                    break;
                }
                
                sanity_check_style(curr_style);
                break;
            }
        }
        eat();
    }
    
    // Stoppers to mark the end of styles/selectors
    push_style();
    Alloc(target->selectors, sizeof(Selector), zero());
}

char* ParseStyleStub()
{
    return NULL;
}

int RegisterSelectorByName(Compiler::LocalStyles* target, StringView* name, int style_id, int global_prefix, Compiler::CompilerState* state)
{
    char* terminated_name = (char*)AllocScratch((name->len + 1) * sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, name->value, name->len*sizeof(char)); 
    terminated_name[name->len] = '\0';
    
    auto search = registered_selector_map.find((const char*)terminated_name);
    
    // If the binding is already registered, return its ID
    if(search != registered_selector_map.end())
    {
        // Register the tag with the binding
        Selector* found_selector = (Selector*)search->second;
        
        found_selector->style_ids[found_selector->num_styles] = style_id;
        found_selector->num_styles = (found_selector->num_styles + 1) % MAX_STYLES_PER_SELECTOR;
        if(found_selector->num_styles == 0) // Indicates that the index passed over modulo
        {
            printf("Be careful, selector with name %s has gone over its registered style limit!\n", terminated_name);
        }
        DeAllocScratch(terminated_name);
        
        return found_selector->global_id;
    }
    
    // Register the selector globally and locally since it doesnt exist
    Selector* new_selector = (Selector*)Alloc(target->selectors, sizeof(Selector));
    new_selector->global_id = state->next_selector_id;
    state->next_selector_id++;
    
    // Create the global name for the selector
    // get length the name wants
    int global_name_length = snprintf(NULL, 0, "%d-%s", global_prefix, terminated_name);
    
    char* global_name = (char*)Alloc(target->selector_values, (global_name_length + 1)*sizeof(char)); // +1 to fit \0

    sprintf(global_name, "%d-%s", global_prefix, terminated_name);

    new_selector->name = global_name;
    new_selector->name_length = global_name_length;
    
    new_selector->num_styles = 1;
    new_selector->style_ids[0] = style_id;
    
    // Set the local selector
    registered_selector_map.insert({(const char*)terminated_name, new_selector});
    DeAllocScratch(terminated_name);
    return new_selector->global_id;
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



// Parses from the given start token till hitting an END token.
Attribute* parse_attribute_expr(AST* target, Tag* parent_tag, Token* attribute_start_token, CompilerState* state) // The first token returned by tokenizeAttribute
{
    #define push_attribute() (Attribute*)Alloc(attribute_arena, sizeof(Attribute))
    Arena* attribute_arena = target->attributes;
    Arena* registered_bindings_arena = target->registered_bindings;
    Arena* values_arena = target->values;
    Arena* element_ids_arena = target->element_ids;
    
    Token* initial_token = curr_token; // Save the state of curr token so we can return it back after were done
    
    curr_token = attribute_start_token;
    Attribute* first_added = (Attribute*)attribute_arena->next_address; // Peek to see where the first attribute will be placed
    
    while(curr_token->type != TokenType::END)
    {
        if(curr_token->type != TokenType::ATTRIBUTE_IDENTIFIER)
        {
            printf("Unexpected token while parsing attribute!");
            return NULL;
        }
        Attribute* new_attribute = push_attribute();
        // Determine the attribute type
        new_attribute->type = GetAttributeFromName(&curr_token->body);
    
        switch(new_attribute->type)
        {
        case(AttributeType::NONE):
        {
            printf("Unknown attribute \"%.*s\".", curr_token->body.len, curr_token->body.value);
            eat();
            continue;
        }
        case(AttributeType::FOCUSABLE): // No value 
        {
            eat();
            if(curr_token->type != TokenType::ATTRIBUTE_IDENTIFIER && curr_token->type != TokenType::END)
            {
                printf("Unexpected token while parsing attribute!\n");
                return NULL;
            }
            else if(curr_token->type == TokenType::END)
            {
                parent_tag->num_attributes++;
                continue;
            }
            parent_tag->num_attributes++;
            continue;
        }
        case(AttributeType::TICKING):
        {
            eat();
            if(curr_token->type != TokenType::ATTRIBUTE_IDENTIFIER && curr_token->type != TokenType::END)
            {
                printf("Unexpected token while parsing attribute!\n");
                return NULL;
            }
            else if(curr_token->type == TokenType::END)
            {
                parent_tag->num_attributes++;
                continue;
            }
            
            parent_tag->num_attributes++;
            continue;
        }
        }
        
        if(!expect_eat(TokenType::EQUALS))
        {
            printf("Expected = while parsing attribute!");
            return NULL;
        }
        
        if(!expect_eat(TokenType::QUOTE))
        {
            printf("Expected \" for attribute value decleration!");
            return NULL;
        }
        
        eat();
        if(curr_token->type != TokenType::OPEN_BRACKET && curr_token->type != TokenType::TEXT)
        {
            printf("Expected attribute value decleration!");
            return NULL;
        }
        
        //new_attribute->attribute_value = (char*)values_arena->next_address; // Peek at where the value will start
        int front_length = 0;
        char* front_value = NULL;
        int back_length = 0;
        char* back_value = NULL;
        
        // If text is first, handle that. Either text or a binding can be first.
        if(curr_token->type == TokenType::TEXT)
        {
            front_length = curr_token->body.len;
            front_value = curr_token->body.value;
            eat();
        }
        // If binding was first or after text handle it
        if(curr_token->type == TokenType::OPEN_BRACKET)
        {
            bool is_local = false;
            // Double brackets is for locals
            if((curr_token + 1)->type == TokenType::OPEN_BRACKET)
            {
                is_local = true;
                eat();
            }
            
            if(!expect_eat(TokenType::TEXT))
            {
                printf("No variable name provided for binding!");
                return NULL;
            }
            
            // Attribute type can effect how the binding is added.
            switch(new_attribute->type)
            {
            case(AttributeType::ON_CLICK):
                new_attribute->OnClick.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, &curr_token->body, RegisteredBindingType::VOID_RET, is_local, state, parent_tag->context_name);
                break;
            case(AttributeType::THIS_ELEMENT):
            {
                new_attribute->This.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, &curr_token->body, RegisteredBindingType::VOID_PTR, is_local, state, parent_tag->context_name);
                break;
            }
            case(AttributeType::CONDITION):
            {
                new_attribute->Condition.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, &curr_token->body, RegisteredBindingType::BOOL_RET, is_local, state, parent_tag->context_name);
                break;
            }
            case(AttributeType::ON_FOCUS):
            {
                new_attribute->Condition.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, &curr_token->body, RegisteredBindingType::VOID_BOOL_RET, is_local, state, parent_tag->context_name);
                break;
            }
            case(AttributeType::ID):
            {
                printf("Found a binding while parsing id attribute. Bindings are not allowed for id attributes since they baked at compile time.\n");
                break;
            }
            case(AttributeType::CUSTOM):
            {
                if(parent_tag->type != TagType::CUSTOM)
                {
                    printf("Warning: Args attribute on non-component tag. Please ensure this was intended and not a typo.\n");
                }
                
                new_attribute->Args.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, &curr_token->body, RegisteredBindingType::ARG_RET, is_local, state, parent_tag->context_name);
                
                break;
            }
            case(AttributeType::LOOP):
            {
                assert(parent_tag->type == TagType::EACH);
                StringView array_name = {};
                StringView count_name = {};
                
                array_name.value = curr_token->body.value;
                
                for(int i = 0; i < curr_token->body.len; i++)
                {
                    if(array_name.value[i] == ';')
                    {
                        break;
                    }
                    array_name.len++;
                }
                
                if(array_name.len == 0 || array_name.len == curr_token->body.len)
                {
                    printf("Error: Needed an array name in loop attribute binding!\n");
                    break;
                }
                
                new_attribute->Loop.array_binding = RegisterBindingByName(registered_bindings_arena, values_arena, &array_name, RegisteredBindingType::PTR_RET, is_local, state, parent_tag->context_name);
                
                // +1 to step over ;
                count_name.value = curr_token->body.value + (array_name.len + 1);
                
                for(int i = 0; i < (curr_token->body.len - array_name.len); i++)
                {
                    if(count_name.value[i] == ';')
                    {
                        break;
                    }
                    count_name.len++;
                }
            
                if(count_name.len == 0 || (count_name.len + array_name.len + 1) == curr_token->body.len)
                {
                    printf("Error: Needed an array count variable in loop attribute binding!\n");
                    break;
                }
                
                new_attribute->Loop.length_binding = RegisterBindingByName(registered_bindings_arena, values_arena, &count_name, RegisteredBindingType::INT_RET, is_local, state, parent_tag->context_name);
                
                // +1 to step over ;
                char* type_name = count_name.value + (count_name.len + 1);
                if(type_name > (curr_token->body.value + curr_token->body.len))
                {
                    printf("Error: Needed a type name in loop attribute binding!\n");
                    break;
                }
                new_attribute->Loop.type_name.len = curr_token->body.len - (array_name.len + count_name.len + 2);
                
                assert(type_name + new_attribute->Loop.type_name.len == curr_token->body.value + curr_token->body.len);
                
                // +2 to account for 2 skipped ; chars
                new_attribute->Loop.type_name.value = (char*)Alloc(values_arena, new_attribute->Loop.type_name.len*sizeof(char));
                memcpy(new_attribute->Loop.type_name.value, type_name, new_attribute->Loop.type_name.len);
            
                break;
            }
            default: // All the attribtues that just use a text like body
                //new_attribute->binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, (char*)curr_token->token_value, curr_token->value_length, parent_tag->tag_id, RegisteredBindingType::TEXT_RET, state);
                new_attribute->Text.binding_position = front_length;
                new_attribute->Text.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, &curr_token->body, RegisteredBindingType::TEXT_RET, is_local, state, parent_tag->context_name);
                break;
            }
            
            if(!expect_eat(TokenType::CLOSE_BRACKET))
            {
                printf("Expected a closing bracket after binding");
                return NULL;
            }
            
            if(is_local)
            {
                if(!expect_eat(TokenType::CLOSE_BRACKET))
                {
                    printf("Expected another closing bracket after local binding");
                    return NULL;
                }
            }
            
            eat(); // Eat the closing bracket
        }
        //if there is text after the binding handle it
        if(curr_token->type == TokenType::TEXT)
        {
            back_length = curr_token->body.len;
            back_value = curr_token->body.value;
            eat();
        }
        
        // Attribute type affects how the value gets added
        switch(new_attribute->type)
        {
            // Attributes with no value
            case(AttributeType::ON_CLICK):
            case(AttributeType::ON_FOCUS):
            case(AttributeType::THIS_ELEMENT):
            case(AttributeType::CONDITION):
            case(AttributeType::LOOP):
            case(AttributeType::CUSTOM):
            {
                break;
            }
            case(AttributeType::ID):
            {
                // Id doesnt produce an actual attribute
                DeAlloc(attribute_arena, new_attribute);
                // Should only have a single continuos value
                assert(back_length == 0 && front_length);
                
                StringView id_name = {front_value, (uint32_t)front_length};
                parent_tag->global_id = RegisterElementIdByName(element_ids_arena, values_arena, state, &id_name);
                parent_tag->num_attributes--; // Prevent this attribute from being counted
                
                break;
            }
            // Text like attributes
            default:
            {
                new_attribute->Text.value_length = front_length + back_length;
                new_attribute->Text.value = (char*)Alloc(values_arena, new_attribute->Text.value_length*sizeof(char));
                // Copy over the attribute value
                if(front_value)
                {
                    memcpy(new_attribute->Text.value, front_value, front_length*sizeof(char));
                }
                if(back_value)
                {
                    memcpy((new_attribute->Text.value + front_length), back_value, back_length*sizeof(char));
                }
                break;
            }
        }
        
        if(curr_token->type != TokenType::QUOTE)
        {
            printf("Expected decleration of attribute value!\n");
            return NULL;
        }
        
        eat();
        
        parent_tag->num_attributes++;
    }

    
    curr_token = initial_token;
    return first_added;
}

// Registers all the selectors to the given style id until it hits an open bracket.
void aggregate_selectors(LocalStyles* target, int style_id, int global_prefix, CompilerState* state)
{
    while(curr_token->type != TokenType::END)
    {
        switch(curr_token->type)
        {
            case(TokenType::TEXT):
            {
                StringView stripped_name = StripOuterWhitespace(&curr_token->body);
                
                RegisterSelectorByName(target, &stripped_name, style_id, global_prefix, state);
                eat();
                if(curr_token->type == TokenType::COMMA) // Inidcates that there is another 
                {
                    eat();
                }
                break;
            }
            case(TokenType::OPEN_BRACKET):
                return;
                break;
            default:
                printf("Unexpected token while parsing style selector!");
                return;
        }
    }
}

std::map<std::string, TextWrapping> text_wrapping_map = 
{
    {"words", TextWrapping::WORDS},
    {"break_words", TextWrapping::CHARS},
    {"none", TextWrapping::NO},
};

TextWrapping parse_wrapping_expr(StringView* expression)
{
    char* terminated_name = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, expression->value, expression->len*sizeof(char));
    
    terminated_name[expression->len] = '\0';
    auto search = text_wrapping_map.find((const char*)terminated_name);
    
    DeAllocScratch(terminated_name);
    
    if(search != text_wrapping_map.end())
    {
        return search->second;
    }
    return TextWrapping::NONE;
}

std::map<std::string, ClipStyle> clipping_name_map = 
{
    {"hidden", ClipStyle::HIDDEN},
    {"scroll", ClipStyle::SCROLL},
};

ClipStyle parse_clipping_expr(StringView* expression)
{
    char* terminated_name = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, expression->value, expression->len*sizeof(char));
    
    terminated_name[expression->len] = '\0';
    auto search = clipping_name_map.find((const char*)terminated_name);
    
    DeAllocScratch(terminated_name);
    
    if(search != clipping_name_map.end())
    {
        return search->second;
    }
    return ClipStyle::NONE;
}

float parse_color_channel(StringView* expression)
{
    char* terminated_expression = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero());
    memcpy(terminated_expression, expression->value, expression->len);
    terminated_expression[expression->len] = '\0';

    float size = 0.0f;
    
    int result = sscanf(terminated_expression, "%f", &size);
    if(result != 1)
    {
        return 0.0f;
    }
    
    // Note(Leo): Color value should be given in the standard 0-255 rgb range which we convert here
    return size / 255;
}

std::map<std::string, DisplayType> display_type_map = 
{
    {"normal", DisplayType::NORMAL},
    {"hidden", DisplayType::HIDDEN},
    {"relative", DisplayType::RELATIONAL},
    {"manual", DisplayType::MANUAL},
};

DisplayType parse_display_type(StringView* expression)
{
    char* terminated_name = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, expression->value, expression->len*sizeof(char));
    
    terminated_name[expression->len] = '\0';
    auto search = display_type_map.find((const char*)terminated_name);
    
    DeAllocScratch(terminated_name);
    
    if(search != display_type_map.end())
    {
        return search->second;
    }
    return DisplayType::NONE;
}

int parse_int_field(StringView* expression)
{
    char* terminated_field = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero());
    memcpy(terminated_field, expression->value, expression->len);
    terminated_field[expression->len] = '\0'; // Adding null terminator

    int value = 0;
    int result = sscanf(terminated_field, "%d", &value);
    
    if(result != 1)
    {
        DeAllocScratch(terminated_field);
        printf("Unable to parse integer style field!\n");
        return 0;
    }
    
    DeAllocScratch(terminated_field);
    return value;
}

std::map<std::string, StyleFieldType> style_field_map = 
{
    {"text_wrapping", StyleFieldType::WRAPPING },
    {"horizontal_clipping", StyleFieldType:: HORIZONTAL_CLIPPING},
    {"vertical_clipping", StyleFieldType::VERTICAL_CLIPPING },
    {"color", StyleFieldType::COLOR },
    {"text_color", StyleFieldType::TEXT_COLOR },
    {"display", StyleFieldType::DISPLAY },
    {"width", StyleFieldType::WIDTH },
    {"height", StyleFieldType::HEIGHT },
    {"min_width", StyleFieldType::MIN_WIDTH },
    {"min_height", StyleFieldType::MIN_HEIGHT },
    {"max_width", StyleFieldType::MAX_WIDTH },
    {"max_height", StyleFieldType::MAX_HEIGHT },
    {"margin", StyleFieldType::MARGIN },
    {"padding", StyleFieldType::PADDING },
    {"corners", StyleFieldType::CORNERS },
    {"font_size", StyleFieldType::FONT_SIZE },
    {"font", StyleFieldType::FONT_NAME },
    {"priority", StyleFieldType::PRIORITY },
};

// Note(Leo): For printing error messages relating to style rules
void sanity_check_style(Style* target)
{
    if(target->display == DisplayType::MANUAL || target->display == DisplayType::RELATIONAL)
    {
        if(target->width.type == MeasurementType::GROW)
        {
            printf("A manual or relative element should not use width: grow; since it has no real siblings, you should use width: 100%; instead.\n");            
            target->width.type = MeasurementType::PERCENT;
            target->width.size = 1.0f;
        }
        if(target->height.type == MeasurementType::GROW)
        {
            printf("A manual or relative element should not use height: grow; since it has no real siblings, you should use height: 100%; instead.\n");            
            target->width.type = MeasurementType::PERCENT;
            target->width.size = 1.0f;
        }
    }
}

// Parses the style entry at the current token into the given style
void parse_style_expr(Style* target, Arena* values_arena)
{
    StringView stripped_field = StripOuterWhitespace(&curr_token->body);
    StyleFieldType expr_type = parse_field_name(&stripped_field);
    // After the field name should be a colon the the value
    if(!expect_eat(TokenType::COLON))
    {
        printf("Expected a : while parsing style!");
    }

    switch(expr_type)
    {
        case(StyleFieldType::PRIORITY):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->priority = parse_int_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::WIDTH):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->width = parse_size_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::HEIGHT):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->height = parse_size_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::MIN_WIDTH):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->min_width = parse_size_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::MIN_HEIGHT):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->min_height = parse_size_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::MAX_WIDTH):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->max_width = parse_size_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::MAX_HEIGHT):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->max_height = parse_size_field(&curr_token->body);
            break;
        }
        case(StyleFieldType::WRAPPING):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            StringView stripped = StripOuterWhitespace(&curr_token->body);
            target->wrapping = parse_wrapping_expr(&stripped);
            break;
        }
        case(StyleFieldType::HORIZONTAL_CLIPPING):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            StringView stripped = StripOuterWhitespace(&curr_token->body);
            target->horizontal_clipping = parse_clipping_expr(&stripped);
            break;
        }
        case(StyleFieldType::VERTICAL_CLIPPING):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            StringView stripped = StripOuterWhitespace(&curr_token->body);
            target->vertical_clipping = parse_clipping_expr(&stripped);
            break;
        }
        case(StyleFieldType::FONT_SIZE):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            StringView stripped = StripOuterWhitespace(&curr_token->body);
            
            Measurement parsed = parse_size_field(&stripped);
            // Note(Leo): Font size should always be in pixels.
            if(parsed.type != MeasurementType::PIXELS)
            {
                printf("Non-pixel measurement type given for font size!\n");
                break;
            }
            
            target->font_size = (uint16_t)parsed.size;
            
            //printf("Parsed a font-size: %dpx\n", target->font_size);
            break;
        }
        case(StyleFieldType::FONT_NAME):
        {
            if(!expect_eat(TokenType::QUOTE)){ printf("Expected an opening quote while parsing style!\n"); }
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value while parsing font name!\n"); }
            
            target->font_name.value = (char*)Alloc(values_arena, sizeof(char)*curr_token->body.len);
            target->font_name.len = curr_token->body.len;
            
            memcpy(target->font_name.value, curr_token->body.value, sizeof(char)*curr_token->body.len);
            if(!expect_eat(TokenType::QUOTE)){ printf("Expected a closing quote while parsing style!\n"); }
            
            //printf("Parsed a font name: '%.*s'\n", target->font_name.len, target->font_name.value);
            break;
        }
        case(StyleFieldType::COLOR):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->color.r = 0.0f;
            target->color.g = 0.0f;
            target->color.b = 0.0f;
            target->color.a = 1.0f;
            
            for(int i = 0; i < 4; i++)
            {
                StringView stripped = StripOuterWhitespace(&curr_token->body);
                target->color.c[i] = parse_color_channel(&stripped);
                if(!expect_eat(TokenType::COMMA))
                {
                    // We mustve come to the end of the color decleration and hit a semicolon.
                    curr_token--;
                    break;
                }
                // If we there is another comma on the last iteration that means that the user has entered a fith value (which is wrong) 
                if(i == 3)
                {
                    printf("Too many arguments while parsing color!\n");
                    break;
                }
                if(!expect_eat(TokenType::TEXT))
                {
                    // Error since there was a comma but no further decleration.
                    printf("Expected a value decleration after a , while parsing color\n");
                    break;
                }
            }
            
            //printf("Parsed a color: rgba(%f, %f, %f, %f)\n", target->color.r, target->color.g, target->color.b, target->color.a);

            break;
        }
        case(StyleFieldType::TEXT_COLOR):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            target->text_color.r = 0.0f;
            target->text_color.g = 0.0f;
            target->text_color.b = 0.0f;
            target->text_color.a = 1.0f;
            
            // Note(Leo): Text cant have transparency so we only read 3 channels
            for(int i = 0; i < 3; i++)
            {
                StringView stripped = StripOuterWhitespace(&curr_token->body);
                target->text_color.c[i] = parse_color_channel(&stripped);
                if(!expect_eat(TokenType::COMMA))
                {
                    // We mustve come to the end of the color decleration and hit a semicolon.
                    curr_token--;
                    break;
                }
                // If we there is another comma on the last iteration that means that the user has entered a fourth value (which is wrong) 
                if(i == 2)
                {
                    printf("Too many arguments while parsing text color!\n");
                    break;
                }
                if(!expect_eat(TokenType::TEXT))
                {
                    // Error since there was a comma but no further decleration.
                    printf("Expected a value decleration after a , while parsing color\n");
                    break;
                }
            }

            //printf("Parsed a text color: rgb(%f, %f, %f)\n", target->color.r, target->color.g, target->color.b);
            break;
        }
        case(StyleFieldType::DISPLAY):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            StringView stripped = StripOuterWhitespace(&curr_token->body);
            target->display = parse_display_type(&stripped);
            
            if(target->display == DisplayType::NONE)
            {
                target->display = DisplayType::NORMAL;
                printf("Unknown display type \"%.*s\"\n", stripped.len, stripped.value);
            }
            
            break;
        }
        case(StyleFieldType::MARGIN):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
        
            target->margin.m[0].type = MeasurementType::PIXELS;
            target->margin.m[1].type = MeasurementType::PIXELS;
            target->margin.m[2].type = MeasurementType::PIXELS;
            target->margin.m[3].type = MeasurementType::PIXELS;
        
            for(int i = 0; i < 4; i++)
            {
                StringView stripped = StripOuterWhitespace(&curr_token->body);
                target->margin.m[i] = parse_size_field(&stripped);
                if(target->margin.m[i].type == MeasurementType::NONE)
                {
                    printf("Invalid measurement while parsing margin!\n");
                }
                if(!expect_eat(TokenType::COMMA))
                {
                    // We mustve come to the end of the padding decleration and hit a semicolon.
                    curr_token--;
                    break;
                }
                // If we there is another comma on the last iteration that means that the user has entered a fith value (which is wrong) 
                if(i == 3)
                {
                    printf("Too many arguments while parsing margin!\n");
                    break;
                }
                if(!expect_eat(TokenType::TEXT))
                {
                    // Error since there was a comma but no further decleration.
                    printf("Expected a value decleration after a , while parsing margin\n");
                    break;
                }
            }

            //printf("Parsed a margin: sizes(%f, %f, %f, %f)\n", target->margin.left.size, target->margin.right.size, target->margin.top.size, target->margin.bottom.size);
            break;
        }
        case(StyleFieldType::PADDING):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }
            
            target->padding.m[0].type = MeasurementType::PIXELS;
            target->padding.m[1].type = MeasurementType::PIXELS;
            target->padding.m[2].type = MeasurementType::PIXELS;
            target->padding.m[3].type = MeasurementType::PIXELS;

            for(int i = 0; i < 4; i++)
            {
                StringView stripped = StripOuterWhitespace(&curr_token->body);
                target->padding.m[i] = parse_size_field(&stripped);
                if(target->padding.m[i].type == MeasurementType::NONE)
                {
                    printf("Invalid measurement while parsing padding!\n");
                }
                if(!expect_eat(TokenType::COMMA))
                {
                    // We mustve come to the end of the padding decleration and hit a semicolon.
                    curr_token--;
                    break;
                }
                // If we there is another comma on the last iteration that means that the user has entered a fith value (which is wrong) 
                if(i == 3)
                {
                    printf("Too many arguments while parsing padding!\n");
                    break;
                }
                if(!expect_eat(TokenType::TEXT))
                {
                    // Error since there was a comma but no further decleration.
                    printf("Expected a value decleration after a , while parsing padding\n");
                    break;
                }
            }

            //printf("Parsed a padding: sizes(%f, %f, %f, %f)\n", target->padding.left.size, target->padding.right.size, target->padding.top.size, target->padding.bottom.size);
            
            break;
        }
        case(StyleFieldType::CORNERS):
        {
            if(!expect_eat(TokenType::TEXT)){ printf("Expected a value decleration while parsing style!"); }

            for(int i = 0; i < 4; i++)
            {
                StringView stripped = StripOuterWhitespace(&curr_token->body);
                
                // Note(Leo): Corner radii are always in pixels
                Measurement parsed = parse_size_field(&stripped);
                if(parsed.type != MeasurementType::PIXELS)
                {
                    printf("Non-pixel unit found while parsing style corners!");
                    break;
                }
                
                target->corners.c[i] = parsed.size;
                
                if(!expect_eat(TokenType::COMMA))
                {
                    // We mustve come to the end of the padding decleration and hit a semicolon.
                    curr_token--;
                    break;
                }
                // If we there is another comma on the last iteration that means that the user has entered a fith value (which is wrong) 
                if(i == 3)
                {
                    printf("Too many arguments while parsing corners!\n");
                    break;
                }
                if(!expect_eat(TokenType::TEXT))
                {
                    // Error since there was a comma but no further decleration.
                    printf("Expected a value decleration after a , while parsing padding\n");
                    break;
                }
            }

            //printf("Parsed a corners: sizes(%f, %f, %f, %f)\n", target->corners.c[0], target->corners.c[1], target->corners.c[2], target->corners.c[3]);
            
            break;
        }
        default:
            printf("Unknown identifier encountered while parsing style expression!\n");
            break;
    }
    if(!expect_eat(TokenType::SEMI_COLON))
    {
        printf("Expected a ; after style decleration!\n");
        printf("Near field %.*s\n", stripped_field.len, stripped_field.value);
    }
    
}

StyleFieldType parse_field_name(StringView* name)
{

    char* terminated_name = (char*)AllocScratch((name->len + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, name->value, name->len*sizeof(char));
    
    terminated_name[name->len] = '\0';
        
    auto search = style_field_map.find((const char*)terminated_name);
    
    DeAllocScratch(terminated_name);
    
    if(search != style_field_map.end()){
        return search->second;
    }
    return StyleFieldType::NONE;
}

std::map<std::string, MeasurementType> measurement_unit_map = 
{
    { "fit", MeasurementType::FIT },
    { "grow", MeasurementType::GROW },
    { "px", MeasurementType::PIXELS },
    { "%", MeasurementType::PERCENT },
};

Measurement parse_size_field(StringView* expression)
{
    #define free_scratches() DeAllocScratch(terminated_field); DeAllocScratch(field_unit)

    char* terminated_field = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero());
    memcpy(terminated_field, expression->value, expression->len);
    terminated_field[expression->len] = '\0'; // Adding null terminator
    
    char* field_unit = (char*)AllocScratch((expression->len + 1)*sizeof(char), no_zero()); // At max sscanf will put the whole thing in the unit
    float size = 0.0f;
    // Get unit in form (size)(unit) - eg 10px
    int result = sscanf(terminated_field, "%f%s", &size, field_unit);
    
    Measurement parsed_measurement = {};
    
    if(result == 2) // Indicates sscanf was a success (there is a size and a unit)
    {
        auto search = measurement_unit_map.find((const char*)field_unit);
    
        if(search != measurement_unit_map.end()){
            parsed_measurement.type = search->second;
            parsed_measurement.size = size;
            
            // Note(Leo): Shaping platform expects % sizes in a normalized range
            if(parsed_measurement.type == MeasurementType::PERCENT)
            {
                parsed_measurement.size /= 100.0f;
            }
        }
        else
        {
            printf("Unrecognized unit '%s' encountered while parsing measurement!", field_unit);
        }
        free_scratches();
        return parsed_measurement;
    }
    
    // Fit and grow only have a unit and no quantity so check for that
    auto search = measurement_unit_map.find((const char*)terminated_field);
    
    if(search != measurement_unit_map.end()){
        parsed_measurement.type = search->second;
        parsed_measurement.size = 0;
        
        free_scratches();
        return parsed_measurement;
    }
    
    // invalid measurement entirely
    printf("Couldnt parse style measurement: %s", terminated_field);
    free_scratches();
    return parsed_measurement;
}