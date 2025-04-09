#if !STRING_VIEW_HEADER
#define STRING_VIEW_HEADER 1

struct StringView
{
    char* value;
    uint32_t len;
}; 

StringView StripOuterWhitespace(StringView* string);

#endif
#if STRING_VIEW_IMPLEMENTATION && !STRING_VIEW_DEFINED
#define STRING_VIEW_DEFINED 1

// Strip whitespace (tabs & spaces) before and after a string
// eg: "  hello this is a string   " --> "hello this is a string"
StringView StripOuterWhitespace(StringView* string)
{
    char* curr = string->value;
    int len = string->len;
    
    // Stripping leading whitespaces
    while(len > 0)
    {
        if(*curr != '\t' && *curr != ' ')
        {
            break;
        }
        
        curr++;
        len--;
    }
    
    StringView stripped = {};
    stripped.value = curr;
    stripped.len = len;
    
    // Stripping following whitespaces
    while(stripped.len > 0)
    {
        if(stripped.value[stripped.len - 1] != '\t' && stripped.value[stripped.len - 1] != ' ')
        {
            break;
        }
        
        stripped.len--;
    }
    
    return stripped;
}

#endif
