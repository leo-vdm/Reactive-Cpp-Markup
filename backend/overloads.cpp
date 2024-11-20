#include <cstring>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                           Overloads used by the dom to parse bound expressions to strings                   /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define make_string_macro(type) inline std::string make_string(type arg) { return std::to_string(arg); }

// Overload to allow char* to a c-str
inline std::string make_string(char* arg)
{
    std::string temp = arg;
    return temp;
}

// Overload to allow std::string
inline std::string make_string(std::string arg)
{
    return arg;
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