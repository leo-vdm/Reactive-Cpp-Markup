#include "compiler.h"
using namespace Compiler;

#include "arena.h"

#include <iostream>
#include <fstream>
#include <cstring>

int aggregate_text(FILE* src, Arena* values_arena, Token* concerned_token, const char* stop_chars, const char* ignored_chars = NULL);
int aggregate_text(char* start_char, char* max_char, Arena* values_arena, Token* concerned_token, const char* stop_chars);
int tokenize_attribute_value(Arena* tokens_arena, Arena* token_values_arena, char* starting_char, char* boundary);

void Tokenize(FILE* src, Arena* tokens_arena, Arena* token_values_arena){
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))

    char next_char;
    bool inside_quote = false;
    
    while((next_char = fgetc(src)) != EOF)
    {
        Token* new_token;
        switch(next_char)
        {
            case('<'):
            {
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
                aggregate_text(src, token_values_arena, new_token, " >");
                                
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
                
                aggregate_text(src, token_values_arena, new_token, ">\""); // everything between a tag name and closure is an attribute
                next_char = fgetc(src);
                
                if(next_char == '>')
                {
                    ungetc(next_char, src);
                    break;
                }
                
                // hit a quote
                ungetc(next_char, src);

                char* value_start = new_token->body.value;
                int value_len = (int)new_token->body.len;
                
                while(true)
                {
                    value_len += aggregate_text(src, token_values_arena, new_token, ">\""); // everything between a tag name and closure is an attribute
                    next_char = fgetc(src);
                    if(next_char == '\"') // Hit a quote
                    {
                        *(char*)Alloc(token_values_arena, sizeof(char)) = next_char;
                        inside_quote = !inside_quote;
                        value_len++;
                    }
                    else if(!inside_quote && next_char == '>')
                    {
                        new_token->body.value = value_start;
                        new_token->body.len = (uint32_t)value_len;
                        break;
                    }
                    else
                    {
                        *(char*)Alloc(token_values_arena, sizeof(char)) = next_char;
                        value_len++;
                    }
                }
                ungetc(next_char, src);
                
                break;
            }
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
                aggregate_text(src, token_values_arena, new_token, "<>{}", "\n\t\r");
                
                break;
        }
    }

    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->body.value = NULL;
}

Token* TokenizeAttribute(Arena* tokens_arena, Arena* token_values_arena, Token* attribute_token)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))
    
    char* current_char = (char*)attribute_token->body.value;

    char* boundary = current_char + (attribute_token->body.len);
    
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
                
                current_char += aggregate_text(current_char, boundary, token_values_arena, new_token, "\" =");
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
    new_token->body.value = NULL;
    
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
                int skipped_chars_count = aggregate_text(current_char, boundary, token_values_arena, new_token, "{}\"");
                
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
            case('\0'): 
                break;
            case('\n'): 
                break;
            case(' '): // Skip whitespaces
                break;
            case('\r'):
                break;
            case('\t'):
                break;
            case(','):
                new_token = push_token();
                new_token->type = TokenType::COMMA;
                break;
            default: // Loose text.
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token, "{}\":;,");
                break;
                
        }
    
    }

    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->body.value = NULL;
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
            /*
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
            */
            case('#'): // Directives (C++ or ours)
            {
                new_token = push_token();
                new_token->type = TokenType::DIRECTIVE;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token, "\n");
                break;                
            }
            /*
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
            */
            default: // Other c++ code
            {
                new_token = push_token();
                new_token->type = TokenType::TEXT;
                
                ungetc(next_char, src); // Put the first char back.
                aggregate_text(src, token_values_arena, new_token, "#");
                break;
            }
        }
    }
    
    Token* last_token = push_token();
    last_token->type = TokenType::END; // Mark the EOF with a token.
    last_token->body.value = NULL;
}

Token* TokenizeDirective(Arena* tokens_arena, Arena* token_values_arena, Token* directive_token)
{
    #define push_token() (Token*)Alloc(tokens_arena, sizeof(Token))
    
    char* current_char = (char*)directive_token->body.value;

    char* boundary = current_char + (directive_token->body.len);
    
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
                    
                    current_char += aggregate_text(current_char, boundary, token_values_arena, new_token, " ,\n");
                    identifier_hit = true;
                }
                else
                {
                    new_token = push_token();
                    new_token->type = TokenType::TEXT;
                    
                    current_char += aggregate_text(current_char, boundary, token_values_arena, new_token, " ,\n");
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
    new_token->body.value = NULL;
    
    return first_token;
}

// Suited for tokenizing attributes/bindings
int aggregate_text(char* start_char, char* max_char, Arena* values_arena, Token* concerned_token, const char* stop_chars)
{
    int stop_chars_length = strlen(stop_chars);
    int value_length = 0;
    char* value_start = (char*)values_arena->next_address; // Assume we will get the next address
    
    char* current_char = start_char;
    while(current_char < max_char)
    {
        // Check char against all stopping chars.
        // TODO: SIMD the stop chars, next char to do multiple checks at once
        for(int i = 0; i < stop_chars_length; i++)
        {
            if(*current_char == stop_chars[i])
            {   
                concerned_token->body.len = value_length;
                concerned_token->body.value = value_start;
                
                return value_length;
            }        
        }
        
        *(char*)Alloc(values_arena, sizeof(char)) = *current_char;
        value_length++;
        current_char++;
    }
    
    // Got to end without stopping
    concerned_token->body.len = value_length;
    concerned_token->body.value = value_start;
    return value_length;
}

// Aggregates the text from the current char to the next significant char, returns the length of text grabbed.
int aggregate_text(FILE* src, Arena* values_arena, Token* concerned_token, const char* stop_chars, const char* ignored_chars)
{
    int value_length = 0;
    int ignored_chars_length = 0;
    if(ignored_chars)
    {
        ignored_chars_length = strlen(ignored_chars);
    }
    
    int stop_chars_length = strlen(stop_chars);
    char* value_start = (char*)values_arena->next_address; // Cheat and assume the address we will get is the next
    
    char next_char;
    skip_char:
    while((next_char = fgetc(src)) != EOF)
    {    
        // Check char against all the chars we wanna ignore
        for(int i = 0; i < ignored_chars_length; i++)
        {
            if(next_char == ignored_chars[i])
            {
                goto skip_char;
            }
        }
        // Check char against all stopping chars.
        // TODO: SIMD the stop chars, next char to do multiple checks at once
        for(int i = 0; i < stop_chars_length; i++)
        {
            if(next_char == stop_chars[i])
            {   
                concerned_token->body.len = value_length;
                concerned_token->body.value = value_start;
                ungetc(next_char, src); // Put the char back
                
                return value_length;
            }        
        }
        
        *(char*)Alloc(values_arena, sizeof(char)) = next_char;
        value_length++;
    }
    
    // Got to end without stopping
    concerned_token->body.len = value_length;
    concerned_token->body.value = value_start;
    return value_length;
}