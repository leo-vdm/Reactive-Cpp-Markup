#if !PRIM_HEADER

#define PRIM_HEADER 1

typedef signed char i8;
typedef unsigned char u8;

typedef short i16;
typedef unsigned short u16;

typedef int i32;
typedef unsigned int u32;

typedef long long int i64;
typedef unsigned long long int u64;

typedef unsigned long long int uptr;

typedef float f32;

typedef u32 b32;

#define assert_size(name, size) static_assert(sizeof(name) == size)

assert_size(i8, 1);
assert_size(u8, 1);
assert_size(i16, 2);
assert_size(u16, 2);
assert_size(i32, 4);
assert_size(u32, 4);
assert_size(f32, 4);
assert_size(i64, 8);
assert_size(u64, 8);
assert_size(uptr, 8);

struct vec2
{
    union
    {
        struct
        {
            f32 x;
            f32 y;
        };
        struct
        {
            f32 u;
            f32 v;
        };
    };
};

struct ivec2
{
    union
    {
        struct
        {
            i32 x;
            i32 y;
        };
        struct
        {
            i32 u;
            i32 v;
        };
    };
};

struct uvec2
{
    union
    {
        struct
        {
            u32 x;
            u32 y;
        };
        struct
        {
            u32 u;
            u32 v;
        };
    };
};

struct vec3
{
    union
    {
        struct
        {
            f32 x;
            f32 y;
            f32 z;
        };
        struct
        {
            f32 r;
            f32 g;
            f32 b;
        };
    };
};

struct ivec3
{
    union
    {
        struct
        {
            i32 x;
            i32 y;
            i32 z;
        };
        struct
        {
            i32 r;
            i32 g;
            i32 b;
        };
    };
};

struct StringView
{
    char* value;
    u32 length;
};

template <typename T> struct ArrayView
{
    T* value;
    u32 length;
};

#define IsPowerOfTwo(Value) (((Value) & ((Value) - 1)) == 0)
#define IsEven(Value) ((Value & 1) == 0)

#define MIN(LEFT, RIGHT) ((LEFT) < (RIGHT) ? (LEFT) : (RIGHT))
#define MAX(LEFT, RIGHT) ((LEFT) > (RIGHT) ? (LEFT) : (RIGHT))

#define Megabytes(x) (x*1000000)
#define Kilobytes(x) (x*1000)

// Aligns the given pointer to where the type wants it to start in memory
// Note(Leo): GCC doesnt need decltype inside of alignof but msvc does and will error otherwise
#define AlignMem(ptr, type) (type*)((uintptr_t)ptr + alignof(type) - ((uintptr_t)ptr % alignof(type)))

// Aligns the given pointer to a pointer aligned address (8 byte aligned)
#define AlignPtr(ptr) ((void*)((uintptr_t)ptr + alignof(decltype(ptr)) - ((uintptr_t)ptr % alignof(decltype(ptr)))))

// Aligns the given memory to the given alignment requirement 
#define AlignMemTo(ptr, alignoftype) (void*)((uintptr_t)ptr + alignoftype - ((uintptr_t)ptr % alignoftype))

// Aligns the given offset to the given alignment requirement assuming that the offset is ofsetting off an aligned address 
#define AlignOffsetTo(offset, alignoftype) (int)(offset + alignoftype - (offset % alignoftype))

// Ineger offset in bytes of a pointer from a 'base' pointer
#define OffsetOf(ptr_offset, ptr_base) (((uintptr_t)ptr_offset) - ((uintptr_t)ptr_base))

// Index of a pointer from a base pointer calculated using the size of type
#define IndexOf(ptr_offset, ptr_base, type) ((((uintptr_t)ptr_offset) - ((uintptr_t)ptr_base)) / sizeof(type))

// Custom assert with space for a formatted message
#if NDEBUG
    #define debug_assert(cond, ...) (void)0
#else
    #define debug_assert(cond, ...) if(!(cond))\
        {\
        printf("Assertion failed! In %s on line %d\n", __FILE__, __LINE__);\
        printf(__VA_ARGS__);\
        fflush(stdout);\
        (*((char*)0) = 0);\
        }
#endif

#define internal static

#endif // !PRIM_HEADER
