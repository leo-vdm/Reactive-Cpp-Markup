#include "arena.h"
#include "prim_types.h"

#if !STRING_HEADER
#define STRING_HEADER 1

#define STRING_BLOCK_BODY_SIZE 100
#define no_copy() 1 << 0
#define null_term() 1 << 1

// Note(Leo): StringBlock MUST be larger than ArenaString otherwise it cant fit on the same arena
struct StringBlock
{
    StringBlock* next;
    u16 fill_level;
    char content[STRING_BLOCK_BODY_SIZE];  
};

struct ArenaString
{
    Arena* parent_arena;

    StringBlock* head;
    StringBlock* tail;
    
    u32 length; // Length of the string in chars
};

ArenaString* CreateString(Arena* arena);

void FreeString(ArenaString* freed_string);

// Note(Leo): When a flatten function gets the null_term flag it will null terminate the resulting string,
//            otherwise it will NOT be null terminated!
// Note(Leo): The null terminator in a string IS included in the length of the returned string view

// Flattens the string out into a single cstring on the scratch arena
StringView FlattenToScratch(ArenaString* string, u32 flags = 0);

// Same as flatten but puts the string into the given arena
StringView FlattenToArena(ArenaString* string, Arena* target_arena, u32 flags = 0);

// Same as flatten but puts the string into the given buffer
StringView FlattenToBuffer(ArenaString* string, char* target_buffer, u32 flags = 0);

// Same as flatten but puts n bytes of the string into the given buffer
// Note(Leo): length is treated the same regardless of null termination, so if you are null terminating length + 1 bytes
//            will be written to the buffer. Keep this in mind to not overflow!
StringView FlattenToBufferN(ArenaString* string, char* target_buffer, u32 length, u32 flags = 0);

void Append(ArenaString* target, const char* c_string);

void Append(ArenaString* target, const char* source_buffer, u32 length);


// The no_copy() flag causes the target to simply point to the first value in source as a continuation after its last value so that both strings are sharing the values.
// The source ArenaString block is automatically de-allocated so that source is fully consumed.
void Append(ArenaString* target, ArenaString* source, u32 flags = 0);

// Remove count chars starting at index
void Remove(ArenaString* target, u32 index, u32 count = 0);

void Insert(ArenaString* target, const char* source, u32 index);
void Insert(ArenaString* target, const char* source, u32 length, u32 index);
void Insert(ArenaString* target, ArenaString* src, u32 index);

#endif
