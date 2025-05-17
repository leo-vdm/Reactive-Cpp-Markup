#include <cstring>
#include <string>
#include <cassert>
#include "arena_string.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                     Overloads used by the dom to parse bound expressions to strings                         /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Overload to allow char* to a c-str
inline ArenaString* make_string(const char* arg, Arena* strings)
{
    ArenaString* created = CreateString(strings);
    Append(created, arg);
    return created;
}

// Overload to allow std::string
inline ArenaString* make_string(std::string arg, Arena* strings)
{
    ArenaString* created = CreateString(strings);
    Append(created, arg.c_str());
    return created;
}

// Overload to allow ArenaString
inline ArenaString* make_string(ArenaString* arg, Arena* strings)
{
    ArenaString* created = CreateString(strings);
    Append(created, arg);
    return created;
}

// Default supported cases from std::to_string
inline ArenaString* make_string(int arg, Arena* strings)
{
    ArenaString* created = CreateString(strings);
    int len = snprintf(NULL, 0, "%d", arg);
    assert(len >= 0);
    len++; // +1 since sprintf adds a \0
    char* buffer = (char*)AllocScratch(len*sizeof(char));
    sprintf(buffer, "%d", arg);
    
    Append(created, buffer, len - 1);
    DeAllocScratch(buffer);
    return created;
}

/*
make_string_macro(long);
make_string_macro(long long);
make_string_macro(unsigned);
make_string_macro(unsigned long);
make_string_macro(unsigned long long);
make_string_macro(float);
make_string_macro(double);
make_string_macro(long double);
*/

// Add your own here to support arbitrary types.