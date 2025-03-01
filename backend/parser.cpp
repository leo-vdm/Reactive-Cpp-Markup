#include <cstring>
#include <map>

#include "compiler.h"
using namespace Compiler;

static Token* curr_token; 

int curr_tag_id;

#define eat() curr_token++
static bool expect_eat(TokenType expected_type);
Attribute* parse_attribute_expr(Arena* attribute_arena, Arena* registered_bindings_arena, Arena* values_arena, Tag* parent_tag, Token* attribute_start_token, CompilerState* state);
void aggregate_selectors(LocalStyles* target, int style_id, int global_prefix, CompilerState* state);
StyleFieldType parse_field_name(char* name, int name_length);
Measurement parse_size_field(char* field_value, int field_value_length);
void parse_style_expr(Style* target);
void clear_registered_bindings();

const char* tag_type_names[] = { "ROOT", "TEXT", "DIV", "CUSTOM", "GRID", "IMG", "VIDEO" };


void ProduceAST(AST* target, Arena* tokens, Arena* token_values, CompilerState* state)
{
    #define push_tag() (Tag*)Alloc(target->tags, sizeof(Tag))
    
    
    curr_token = (Token*)tokens->mapped_address;
    Tag* curr_tag = push_tag();
    
    // First tag is always root
    // NOTE(Leo): Tag IDS are local to the file, other systems use global ids. Element ids are decided at runtime.
    curr_tag_id = 1;
    curr_tag->type = TagType::ROOT;
    curr_tag->tag_id = curr_tag_id++;
    
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
                
                // Indicates new tag is a sibling
                if(has_ended)
                {
                    // Siblings share a parent
                    new_tag->parent = curr_tag->parent;
                    
                    curr_tag->next_sibling = new_tag;
                    has_ended = false;
                    
                    curr_tag = new_tag;
                }
                else{ // Indicates new tag is a child
                    // This must be the first child
                    new_tag->parent = curr_tag;
                    curr_tag->first_child = new_tag;
                    has_ended = false;
                    
                    curr_tag = new_tag;
                } 
                
                // Find the tag's type
                curr_tag->type = GetTagFromName((char*)curr_token->token_value, curr_token->value_length);
                
                // Zero indicates this tag is not a component
                int custom_component_id = 0;
                
                if(curr_tag->type == TagType::CUSTOM)
                {
                    custom_component_id = RegisterComponent((char*)curr_token->token_value, curr_token->value_length, state);
                }
                
                curr_tag->tag_id = curr_tag_id++;
                
                eat();
                curr_tag->first_attribute = NULL;
                if(curr_token->type == TokenType::TAG_ATTRIBUTE)
                {
                    // Tokenize the attribute and then eat the token
                    Token* first_attribute_token = TokenizeAttribute(tokens, token_values, curr_token);
                    eat();                    
                    
                    curr_tag->first_attribute = parse_attribute_expr(target->attributes, target->registered_bindings, target->values, curr_tag, first_attribute_token, state);
                
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
                
                if(has_ended){ // Indicates ending the parent element
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
                
                if(curr_tag->type != TagType::TEXT && expected_type != GetTagFromName((char*)curr_token->token_value, curr_token->value_length))
                {
                    char* temp = (char*)AllocScratch(curr_token->value_length + 1, no_zero());
                    memcpy(temp, curr_token->token_value, curr_token->value_length);
                    temp[curr_token->value_length] = '\0';
                    printf("Unexpected tag end!\n");
                    printf("Expected \"%s\" but got \"%s\"\n", tag_type_names[(int)curr_tag->type], temp);
                    DeAllocScratch(temp);
                }
                
                break;
                }
            case(TokenType::TEXT):
            {
                Tag* new_tag = push_tag();          
                
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
                curr_tag->tag_id = curr_tag_id++;
                curr_tag->num_attributes = 1;
                
                Attribute* text_content = (Attribute*)Alloc(target->attributes, sizeof(Attribute), zero());
                text_content->type = AttributeType::TEXT;
                text_content->Text.value = (char*)Alloc(target->values, curr_token->value_length*sizeof(char));
                text_content->Text.value_length = curr_token->value_length;
                 
                memcpy((void*)text_content->Text.value, curr_token->token_value, curr_token->value_length*sizeof(char));
                
                curr_tag->first_attribute = text_content;
                break;
            }
            case(TokenType::OPEN_BRACKET):
                {
                if(!expect_eat(TokenType::TEXT))
                {
                    printf("Expeced binding definition!");
                }
                
                Tag* new_tag = push_tag();          
                
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
                curr_tag->tag_id = curr_tag_id++;
                curr_tag->num_attributes = 1;
                
                // need an attribute to attatch the binding to
                Attribute* text_content = (Attribute*)Alloc(target->attributes, sizeof(Attribute));
                text_content->type = AttributeType::TEXT;
                text_content->Text.value = NULL;
                text_content->Text.value_length = 0;
                text_content->Text.binding_position = 0;
                
                text_content->Text.binding_id = RegisterBindingByName(target->registered_bindings, target->values, (char*)curr_token->token_value, curr_token->value_length, curr_tag->tag_id, RegisteredBindingType::TEXT_RET, state);
                                
                curr_tag->first_attribute = text_content;
                
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
    
    // Note(Leo): File system save fn expects a zeroed tag to mark the end of the page/comp
    Alloc(target->tags, sizeof(Tag), zero());
    // Note(leo): same for attributes
    Alloc(target->attributes, sizeof(Attribute), zero());
}

std::map<std::string, TagType> tag_map = 
{
    {"text", TagType::TEXT },
    {"div", TagType::DIV },
    //{"end", TagType::CUSTOM},
};

std::map<std::string, AttributeType> attribute_map = 
{
    {"text", AttributeType::TEXT },
    {"class", AttributeType::CLASS },
    {"style", AttributeType::STYLE },
};


std::map<std::string, int> registered_binding_map = {};

TagType GetTagFromName(const char* value, int value_length)
{
    if(value_length > MAX_TAG_NAME_LENGTH){
        return TagType::CUSTOM; 
    }
    
    char* name = (char*)AllocScratch((MAX_TAG_NAME_LENGTH + 1)*sizeof(char), no_zero());
    memcpy(name, value, value_length*sizeof(char));
    
    name[value_length] = '\0';
    std::string name_string;
    name_string = name; // Convert name into a string
    
    //printf("Searching for tag: %s\nLength:%d\n", name, value_length);
    //std::cout << name_string << std::endl;
    
    auto search = tag_map.find(name_string);
    
    DeAllocScratch(name);
    
    if(search != tag_map.end()){
        return search->second;
    }
    return TagType::CUSTOM;
}

AttributeType GetAttributeFromName(char* value, int value_length)
{
    if(value_length > MAX_ATTRIBUTE_NAME_LENGTH){
        return AttributeType::CUSTOM; 
    }
    
    //char name[MAX_ATTRIBUTE_NAME_LENGTH + 1]; // extra charachter to fit the NULL terminator
    char* name = (char*)AllocScratch((MAX_ATTRIBUTE_NAME_LENGTH + 1)*sizeof(char), no_zero());
    memcpy(name, value, value_length*sizeof(char));
    
    name[value_length] = '\0';
    std::string name_string;
    name_string = name; // Convert name into a string

//    printf("Searching for attribute: %s\nLength:%d\nSTD representation: ", name, value_length);
//    std::cout << name_string << std::endl;
    
    auto search = attribute_map.find(name_string);
    
    DeAllocScratch(name);
    
    if(search != attribute_map.end()){
        return search->second;
    }
    return AttributeType::CUSTOM;
}

int RegisterBindingByName(Arena* bindings_arena, Arena* values_arena, char* value, int value_length, int tag_id, RegisteredBindingType type, CompilerState* state)
{
    char* name = (char*)AllocScratch((value_length + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(name, value, value_length*sizeof(char)); 
    
    name[value_length] = '\0';
    std::string name_string;
    name_string = name; // Convert name into a string
        
    auto search = registered_binding_map.find(name_string);
    
    // If the binding is already registered, return its ID
    if(search != registered_binding_map.end()){
        // Register the tag with the binding
        int binding_id = search->second;
        RegisteredBinding* bindings = (RegisteredBinding*)bindings_arena->mapped_address;
        
        RegisteredBinding* existing_binding = bindings + binding_id;
        existing_binding->num_registered = (existing_binding->num_registered + 1) % MAX_TAGS_PER_BINDING;
        if(existing_binding->num_registered == 0) // Indicates that the index passed over modulo
        {
            printf("Be careful, binding with name %s has gone over its registered tag limit!", name);
        }
        existing_binding->registered_tag_ids[existing_binding->num_registered] = tag_id;
        
        DeAllocScratch(name);
        return search->second;
    }
    // Register the binding since it doesnt exist
    RegisteredBinding* new_binding = (RegisteredBinding*)Alloc(bindings_arena, sizeof(RegisteredBinding));
    new_binding->binding_id = state->next_bound_expr_id;
    state->next_bound_expr_id++;
    
    char* saved_name = (char*)Alloc(values_arena, value_length*sizeof(char));
    memcpy(saved_name, value, value_length*sizeof(char));
    
    
    new_binding->binding_name = saved_name;
    new_binding->name_length = value_length;
    
    new_binding->num_registered = 1;
    new_binding->registered_tag_ids[0] = tag_id;
    
    new_binding->type = type;
    
    registered_binding_map[name_string] = new_binding->binding_id;
    DeAllocScratch(name);
    return new_binding->binding_id;
}

void clear_registered_bindings()
{
    registered_binding_map.clear();
}

std::map<std::string, int> registered_selector_map = {};

void clear_registered_selectors()
{
    registered_selector_map.clear();
}

void ClearRegisteredSelectors()
{
    clear_registered_selectors();
}

void ParseStyles(LocalStyles* target, Arena* style_tokens, Arena* style_token_values, int file_prefix, CompilerState* state)
{
    #define push_style() (Style*)Alloc(target->styles, sizeof(Style))
    
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
                    parse_style_expr(curr_style);
                }
                break;
            }
            case(TokenType::CLOSE_BRACKET):
            {
                style_opened = false;
            }
        }
        eat();
    }
}

char* ParseStyleStub()
{
    return NULL;
}

int RegisterSelectorByName(LocalStyles* target, char* value, int value_length, int style_id, int global_prefix, CompilerState* state)
{
    char* name = (char*)AllocScratch((value_length + 1) * sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(name, value, value_length*sizeof(char)); 

    
    name[value_length] = '\0';
    std::string name_string;
    name_string = name; // Convert name into a string
    
    auto search = registered_selector_map.find(name_string);
    
    // If the binding is already registered, return its ID
    if(search != registered_selector_map.end()){
        // Register the tag with the binding
        int selector_id = search->second;
        Selector* found_selector = (Selector*)target->selectors->mapped_address;
        found_selector += selector_id; // ID is always the offset into the global selectors arena for a given selector.
        
        found_selector->num_styles = (found_selector->num_styles + 1) % MAX_STYLES_PER_SELECTOR;
        if(found_selector->num_styles == 0) // Indicates that the index passed over modulo
        {
            printf("Be careful, selector with name %s has gone over its registered style limit!", name);
        }
        found_selector->style_ids[found_selector->num_styles] = style_id;
        DeAllocScratch(name);
        
        return search->second;
    }
    
    // Register the selector globally and locally since it doesnt exist
    Selector* new_selector = (Selector*)Alloc(target->selectors, sizeof(Selector));
    new_selector->global_id = state->next_selector_id;
    state->next_selector_id++;
    
    // Create the global name for the selector
    // get length the name wants
    int global_name_length = snprintf(NULL, 0, "%d-%s", global_prefix, name);
    global_name_length++; // Add null terminator space
    
    char* global_name = (char*)Alloc(target->selector_values, global_name_length*sizeof(char));
    sprintf(global_name, "%d-%s", global_prefix, name);

    new_selector->name = global_name;
    new_selector->name_length = global_name_length;
    
    new_selector->num_styles = 1;
    new_selector->style_ids[0] = style_id;
    
    // Set the local selector
    registered_selector_map[name_string] = new_selector->global_id;
    DeAllocScratch(name);
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
Attribute* parse_attribute_expr(Arena* attribute_arena, Arena* registered_bindings_arena, Arena* values_arena, Tag* parent_tag, Token* attribute_start_token, CompilerState* state) // The first token returned by tokenizeAttribute
{
    #define push_attribute() (Attribute*)Alloc(attribute_arena, sizeof(Attribute))
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
        new_attribute->type = GetAttributeFromName((char*)curr_token->token_value, curr_token->value_length);
        
        if(new_attribute->type == AttributeType::CUSTOM)
        {
            new_attribute->Custom.name_length = curr_token->value_length;
            new_attribute->Custom.name = (char*)Alloc(values_arena, new_attribute->Custom.name_length*sizeof(char));
            memcpy(new_attribute->Custom.name, curr_token->token_value, new_attribute->Custom.name_length);
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
            front_length = curr_token->value_length;
            front_value = (char*)curr_token->token_value;
            eat();
        }
        // If binding was first or after text handle it
        if(curr_token->type == TokenType::OPEN_BRACKET)
        {
            if(!expect_eat(TokenType::TEXT)){
                printf("No variable name provided for binding!");
                return NULL;
            }
            
            // Attribute type can effect how the binding is added.
            switch(new_attribute->type)
            {
            case(AttributeType::ON_CLICK):
                new_attribute->OnClick.binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, (char*)curr_token->token_value, curr_token->value_length, parent_tag->tag_id, RegisteredBindingType::VOID_RET, state);
                break;
            default: // All the attribtues that just use a text like body
                //new_attribute->binding_id = RegisterBindingByName(registered_bindings_arena, values_arena, (char*)curr_token->token_value, curr_token->value_length, parent_tag->tag_id, RegisteredBindingType::TEXT_RET, state);
                new_attribute->Text.binding_position = front_length;
                new_attribute->Text.binding_id =  RegisterBindingByName(registered_bindings_arena, values_arena, (char*)curr_token->token_value, curr_token->value_length, parent_tag->tag_id, RegisteredBindingType::TEXT_RET, state);
                break;
            }
            
            if(!expect_eat(TokenType::CLOSE_BRACKET)){
                printf("Expected a closing bracket after binding");
                return NULL;
            }
            eat(); // Eat the closing bracket
        }
        //if there is text after the binding handle it
        if(curr_token->type == TokenType::TEXT)
        {
            back_length = curr_token->value_length;
            back_value = (char*)curr_token->token_value;
            eat();
        }
        
        // Attribute type affects how the value gets added
        switch(new_attribute->type)
        {
            // OnClick has no value
            case(AttributeType::ON_CLICK):
                break;
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
            printf("Expected decleration of attribute value!");
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
                RegisterSelectorByName(target, (char*)curr_token->token_value, curr_token->value_length, style_id, global_prefix, state);
                eat();
                if(curr_token->type == TokenType::COMMA) // Inidcates that there is another 
                {
                    eat();
                }
                break;
            case(TokenType::OPEN_BRACKET):
                return;
                break;
            default:
                printf("Unexpected token while parsing style selector!");
                return;
        }
    }
}

std::map<std::string, StyleFieldType> style_field_map = 
{
    {"width", StyleFieldType::WIDTH },
    {"height", StyleFieldType::HEIGHT },
    {"maxWidth", StyleFieldType::MAX_WIDTH },
    {"maxWidth", StyleFieldType::MAX_WIDTH },
    {"maxHeight", StyleFieldType::MAX_HEIGHT }
};


// Parses the style entry at the current token into the given style
void parse_style_expr(Style* target)
{
    StyleFieldType expr_type = parse_field_name((char*)curr_token->token_value, curr_token->value_length);
    // After the field name should be a colon the the value
    if(!expect_eat(TokenType::COLON))
    {
        printf("Expected a : while parsing style!");
    }
    // Expect the value decleration.
    if(!expect_eat(TokenType::TEXT)){
        printf("Expected a value decleration while parsing style!");
    }
    switch(expr_type)
    {
        case(StyleFieldType::WIDTH):
        {
            target->width = parse_size_field((char*)curr_token->token_value, curr_token->value_length);
            break;
        }
        case(StyleFieldType::HEIGHT):
        {
            target->height = parse_size_field((char*)curr_token->token_value, curr_token->value_length);
            break;
        }
        case(StyleFieldType::MAX_WIDTH):
        {
            target->max_width = parse_size_field((char*)curr_token->token_value, curr_token->value_length);
            break;
        }
        case(StyleFieldType::MAX_HEIGHT):
        {
            target->max_height = parse_size_field((char*)curr_token->token_value, curr_token->value_length);
            break;
        }
        default:
            printf("Unknown identifier encountered while parsing style expression!\n");
            break;
    }
    if(!expect_eat(TokenType::SEMI_COLON))
    {
        printf("Expected a ; after style decleration!");
    }
    
}

StyleFieldType parse_field_name(char* name, int name_length)
{

    if(name_length > MAX_STYLE_FIELD_NAME_LENGTH)
    {
        return StyleFieldType::NONE; // Cannot be a correct one 
    }

    char* terminated_name = (char*)AllocScratch((name_length + 1)*sizeof(char), no_zero()); // extra charachter to fit the NULL terminator
    memcpy(terminated_name, name, name_length*sizeof(char));
    
    terminated_name[name_length] = '\0';
    std::string name_string;
    name_string = terminated_name; // Convert name into a string
        
    auto search = style_field_map.find(name_string);
    
    DeAllocScratch(terminated_name);
    
    if(search != style_field_map.end()){
        return search->second;
    }
    return StyleFieldType::NONE;
}

std::map<std::string, MeasurementType> measurement_unit_map = 
{
    { "auto", MeasurementType::AUTO },
    { "px", MeasurementType::PIXELS },
    { "%", MeasurementType::PERCENT },
};

Measurement parse_size_field(char* field_value, int field_value_length)
{
    #define free_scratches() DeAllocScratch(terminated_field); DeAllocScratch(field_unit)

    char* terminated_field = (char*)AllocScratch((field_value_length + 1)*sizeof(char), no_zero());
    memcpy(terminated_field, field_value, field_value_length);
    terminated_field[field_value_length] = '\0'; // Adding null terminator
    char* field_unit = (char*)AllocScratch((field_value_length + 1)*sizeof(char), no_zero()); // At max sscanf will put the whole thing in the unit
    float size = 0.0f;
    // Get unit in form (size)(unit) - eg 10px
    int result = sscanf(terminated_field, "%f%s", &size, field_unit);
    
    Measurement parsed_measurement = Measurement();
    
    if(result == 2) // Indicates sscanf was a success (there is a size and a unit)
    {
        std::string unit_string = field_unit;
        auto search = measurement_unit_map.find(unit_string);
    
        if(search != measurement_unit_map.end()){
            parsed_measurement.type = search->second;
            parsed_measurement.size = size;
        }
        else
        {
            printf("Unrecognized unit '%s' encountered while parsing measurement!", field_unit);
        }
        free_scratches();
        return parsed_measurement;
    }
    
    // Auto has only a unit and no size so check for that
    std::string unit_string = terminated_field;
    auto search = measurement_unit_map.find(unit_string);
    
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