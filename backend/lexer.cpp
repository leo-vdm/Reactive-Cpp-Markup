#include "compiler.h"
using namespace Compiler;

#include "arena.h"

#include <iostream>
#include <fstream>

int aggregate_text(FILE* src, Arena* values_arena, Token* concerned_token, char* stop_chars, int stop_chars_length);
int aggregate_text(FILE* src, Arena* values_arena, Token* concerned_token);
int aggregate_text(char* start_char, char* max_char, Arena* values_arena, Token* concerned_token, char* stop_chars, int stop_chars_length);
int tokenize_attribute_value(Arena* tokens_arena, Arena* token_values_arena, char* starting_char, char* boundary);

void Tokenize(FILE* src, Arena* tokens_arena, Arena* token_values_arena){
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))

    char next_char;
    
    while((next_char = fgetc(src)) != EOF)
    {
        Token* new_token;
        switch(next_char)
        {
            case('<'):
                next_char = fgetc(src);
                new_token = push_token();
                       
                // If char after < is a / then the tag is an end tag
                if(next_char == '/')
                {
                    new_token->type = TokenType::TAG_END;
                }
                else
                {
                    new_token->type = TokenType::TAG_START;
                    ungetc(next_char, src); // put the non / back
                }
                
                // Collect the tag name and put it in the token
                aggregate_text(src, token_values_arena, new_token, " >", 2);
                                
                // Check for attributes
                next_char = fgetc(src);
                if(next_char == '>')  // a > after the tag name indicates no attributes
                {
                    ungetc(next_char, src);
                    break;
                }
                
                // Push a new token for the attributes
                new_token = push_token();
                new_token->type = TokenType::TAG_ATTRIBUTE;
                aggregate_text(src, token_values_arena, new_token, ">", 1); // everything between a tag name and closure is an attribute
                
                break;
            case('>'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_TAG;
                break;
            case('{'):
                new_token = push_token();
                new_token->type = TokenType::OPEN_BRACKET;
                break;
            case('}'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_BRACKET;
                break;
            case('\n'): // Ignore line ends
                break;
            case('\0'):
                break;
            case(' '): // Ignore whitespace
                break;
            case('\t'): // Ignore tab
                break;
            default: // Loose text
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token);
                break;
        }
    }

    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->token_value = NULL;
}

Token* TokenizeAttribute(Arena* tokens_arena, Arena* token_values_arena, Token* attribute_token)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))
    
    char* current_char = (char*)attribute_token->token_value;

    char* boundary = current_char + (attribute_token->value_length);
    
    Token* new_token;
    Token* first_token = (Token*)tokens_arena->next_address;
    
    while(current_char < boundary)
    {
        switch(*current_char)
        {
            case(' '):
                break;
            case('\n'):
                break;
            case('\0'):
                break;
            case('='):
                new_token = push_token();
                new_token->type = TokenType::EQUALS;
                break;
            case('"'):
                new_token = push_token();
                new_token->type = TokenType::QUOTE;
                
                current_char++; // move over so we dont hit the "
                current_char += tokenize_attribute_value(tokens_arena, token_values_arena, current_char, boundary);
                
                // If we hit an ending quote add it so it doesnt get skipped during iteration
                if(*(current_char) == '"'){
                    new_token = push_token();
                    new_token->type = TokenType::QUOTE; 
                }
                
                break;
            default: // Text outside of a "" must be an identifier
                new_token = push_token();
                new_token->type = TokenType::ATTRIBUTE_IDENTIFIER;
                
                current_char += aggregate_text(current_char, boundary, token_values_arena, new_token, "\" =", 3);
                // If we hit an = add it here so we dont skip it on the iteration
                if(*current_char == '=')
                {
                    new_token = push_token();
                    new_token->type = TokenType::EQUALS;
                }
                break;
        }
        current_char++;
    }
    
    // Mark the end of this attribute with a token
    new_token = push_token();
    new_token->type = TokenType::END;
    new_token->token_value = NULL;
    
    return first_token;
}

int tokenize_attribute_value(Arena* tokens_arena, Arena* token_values_arena, char* starting_char, char* boundary)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))

    int iterations = 0;
    
    char* current_char = starting_char;
    
    Token* new_token;
    while(current_char < boundary)
    {
        switch(*current_char)
        {
            case('"'):
                return iterations;
                break;
            case('{'):
                new_token = push_token();
                new_token->type = TokenType::OPEN_BRACKET;
                break;
            case('}'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_BRACKET; 
                break;               
            default:
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                int skipped_chars_count = aggregate_text(current_char, boundary, token_values_arena, new_token, "{}\"", 3);
                
                // -1 to account for this iteration.
                iterations += (skipped_chars_count - 1);
                current_char += (skipped_chars_count - 1);
                
                break;
        }
        current_char++;
        iterations++;
    }
    
    return iterations;
}

void TokenizeStyle(FILE* src, Arena* tokens_arena, Arena* token_values_arena)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))

    char next_char;
    
    while((next_char = fgetc(src)) != EOF)
    {
        Token* new_token;
        switch(next_char)
        {
            case('{'):
                new_token = push_token();
                new_token->type = TokenType::OPEN_BRACKET;
                break;
            case('}'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_BRACKET;
                break;
            case('\"'):
                new_token = push_token();
                new_token->type = TokenType::QUOTE;
                break;
            case(':'):
                new_token = push_token();
                new_token->type = TokenType::COLON;
                break;
            case(';'):
                new_token = push_token();
                new_token->type = TokenType::SEMI_COLON;
                break;
            case('\0'): // Skip whitespaces
                break;
            case('\n'): // Put newline tokens in
                new_token = push_token();
                new_token->type = TokenType::NEW_LINE;
                break;
            case(' '):
                break;
            case('\t'):
                break;
            default: // Loose text.
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token, "{}\":;", 5);
                break;
                
        }
    
    }

    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->token_value = NULL;
}

void TokenizeCode(FILE* src, Arena* tokens_arena, Arena* token_values_arena)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))

    char next_char;
    
    while((next_char = fgetc(src)) != EOF)
    {
        Token* new_token;
        switch(next_char)
        {
            case(';'):
                new_token = push_token();
                new_token->type = TokenType::SEMI_COLON;
                break;
            case('('):
                new_token = push_token();
                new_token->type = TokenType::OPEN_PARENTHESIS;
                break;
            case(')'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_PARENTHESIS;
                break;
            case('='):
                new_token = push_token();
                new_token->type = TokenType::EQUALS;
                break;
            case(','):
                new_token = push_token();
                new_token->type = TokenType::COMMA;
                break;
            case('"'):
                new_token = push_token();
                new_token->type = TokenType::QUOTE;
                break;
            case('#'): // Directives (C++ or ours)
            {
                new_token = push_token();
                new_token->type = TokenType::DIRECTIVE;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token, "\n", 1);
                break;                
            }
            case('{'):
                new_token = push_token();
                new_token->type = TokenType::OPEN_BRACKET;
                break;
            case('}'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_BRACKET;
                break;
            case('\0'): // Skip whitespaces/newlines
                break;
            case('\n'):
                new_token = push_token();
                new_token->type = TokenType::NEW_LINE;
                break;
            case(' '):
                break;
            case('\t'):
                break;
            default: // FN declerations, var declerations, type declerations etc.
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token, "()\";=, \n{}", 10);
                break;
        }
    }
    
    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->token_value = NULL;
}

Token* TokenizeBindingCode(Arena* tokens_arena, Arena* token_values_arena, char* src, int src_length)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))
    
    Token* first_token = (Token*)tokens_arena->next_address;
    
    char* curr_char = src;
    char* boundary = src + sizeof(char)*src_length;
    
    while(curr_char < boundary)
    {
        Token* new_token;
        switch(*curr_char)
        {
            case('('):
                new_token = push_token();
                new_token->type = TokenType::OPEN_PARENTHESIS;
                break;
            case(')'):
                new_token = push_token();
                new_token->type = TokenType::CLOSE_PARENTHESIS;
                break;
            case('='):
                new_token = push_token();
                new_token->type = TokenType::EQUALS;
                break;
            case(','):
                new_token = push_token();
                new_token->type = TokenType::COMMA;
                break;
            case(':'):
                new_token = push_token();
                new_token->type = TokenType::COLON;
            case('\0'): // Skip whitespaces/newlines
                break;
            case('\n'):
                new_token = push_token();
                new_token->type = TokenType::NEW_LINE;
                break;
            case(' '):
                break;
            case('\t'):
                break;
            default: // FN declerations, var declerations, type declerations etc.
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                curr_char += aggregate_text(curr_char, boundary, token_values_arena, new_token, "()=,:\0\n \t", 9);

                break;
        }
    }
    
    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->token_value = NULL;
    
    return first_token;
}

Token* TokenizeDirective(Arena* tokens_arena, Arena* token_values_arena, Token* directive_token)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))
    
    char* current_char = (char*)directive_token->token_value;

    char* boundary = current_char + (directive_token->value_length);
    
    Token* new_token;
    Token* first_token = (Token*)tokens_arena->next_address;
    
    bool identifier_hit = false;
    while(current_char < boundary)
    {
        switch(*current_char)
        {
            case(','):
                new_token = push_token();
                new_token->type = TokenType::COMMA;
                break;
            case(' '):
                break;
            case('#'):
                break;
            default: // Text is either a directive identifier or an argument to the directive
                if(!identifier_hit)
                {
                    new_token = push_token();
                    new_token->type = TokenType::DIRECTIVE;
                    
                    current_char += aggregate_text(current_char, boundary, token_values_arena, new_token, " ,\n", 3);
                    identifier_hit = true;
                }
                else{
                    new_token = push_token();
                    new_token->type = TokenType::TEXT;
                    
                    current_char += aggregate_text(current_char, boundary, token_values_arena, new_token, " ,\n", 3);
                }
                
                if(*current_char == ',') // If we ended on a comma add it here so it doesnt get skipped during iteration
                {
                    new_token = push_token();
                    new_token->type = TokenType::COMMA;
                }
                
                break;
        }
        current_char++;
    }
    
    // Mark the end of this directive with a token
    new_token = push_token();
    new_token->type = TokenType::END;
    new_token->token_value = NULL;
    
    return first_token;
}



// Suited for tokenizing attributes/bindings
int aggregate_text(char* start_char, char* max_char, Arena* values_arena, Token* concerned_token, char* stop_chars, int stop_chars_length)
{
    int value_length = 0;
    void* value_start = (void*)values_arena->next_address; // Assume we will get the next address
    
    char* current_char = start_char;
    while(current_char < max_char)
    {
        // Check char against all stopping chars.
        // TODO: SIMD the stop chars, next char to do multiple checks at once
        for(int i = 0; i < stop_chars_length; i++)
        {
            if(*current_char == stop_chars[i])
            {   
                concerned_token->value_length = value_length;
                concerned_token->token_value = value_start;
                
                return value_length;
            }        
        }
        
        *(char*)Alloc(values_arena, sizeof(char)) = *current_char;
        value_length++;
        current_char++;
    }
    
    // Got to end without stopping
    concerned_token->value_length = value_length;
    concerned_token->token_value = value_start;
    return value_length;
}

// Aggregates the text from the current char to the next significant char, returns the length of text grabbed.
int aggregate_text(FILE* src, Arena* values_arena, Token* concerned_token, char* stop_chars, int stop_chars_length)
{
    int value_length = 0;
    void* value_start = (void*)values_arena->next_address; // Cheat and assume the address we will get is the next
    
    int next_char;
    while((next_char = fgetc(src)) != EOF)
    {        
        // Check char against all stopping chars.
        // TODO: SIMD the stop chars, next char to do multiple checks at once
        for(int i = 0; i < stop_chars_length; i++)
        {
            if(next_char == stop_chars[i])
            {   
                concerned_token->value_length = value_length;
                concerned_token->token_value = value_start;
                ungetc(next_char, src); // Put the char back
                
                return value_length;
            }        
        }
        
        *(char*)Alloc(values_arena, sizeof(char)) = next_char;
        value_length++;
    }
    
    // Got to end without stopping
    concerned_token->value_length = value_length;
    concerned_token->token_value = value_start;
    return value_length;
}

inline int aggregate_text(FILE* src, Arena* values_arena, Token* concerned_token)
{
    return aggregate_text(src, values_arena, concerned_token, "<>{}", 4);
}