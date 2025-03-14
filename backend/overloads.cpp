#include <cstring>
#include <string>
#include "arena_string.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                     Overloads used by the dom to parse bound expressions to strings                         /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define make_string_macro(type) inline ArenaString* make_string(type arg, Arena* strings) { std::string temp = std::to_string(arg); ArenaString* created = CreateString(strings); Append(created, temp.c_str()); return created; }

// Overload to allow char* to a c-str
inline ArenaString* make_string(char* arg, Arena* strings)
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

// Default supported cases from std::to_string
make_string_macro(int);
make_string_macro(long);
make_string_macro(long long);
make_string_macro(unsigned);
make_string_macro(unsigned long);
make_string_macro(unsigned long long);
make_string_macro(float);
make_string_macro(double);
make_string_macro(long double);


// Add your own here to support arbitrary types.