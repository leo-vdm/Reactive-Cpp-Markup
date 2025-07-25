#include "arena.h"
#pragma once
#define STRING_BLOCK_BODY_SIZE 100
#define no_copy() 1 << 0

// Note(Leo): StringBlock MUST be larger than ArenaString otherwise it cant fit on the same arena
struct StringBlock
{
    StringBlock* next;
    uint16_t fill_level;
    char content[STRING_BLOCK_BODY_SIZE];
    
};


struct ArenaString
{
    Arena* parent_arena;

    StringBlock* head;
    StringBlock* tail;
    
    int length; // Length of the string in chars not including null terminator
};

ArenaString* CreateString(Arena* arena);

void FreeString(ArenaString* freed_string);

// Flattens the string out into a single cstring (with \0) on the scratch arena
char* Flatten(ArenaString* string);

// Same as flatten but puts the string into the given arena
char* Flatten(ArenaString* string, Arena* target_arena);

// Same as flatten but puts the string into the given buffer
void Flatten(ArenaString* string, char* target_buffer);

// Same as flatten but puts n bytes of the string into the given buffer
void Flatten(ArenaString* string, char* target_buffer, int length);

void Append(ArenaString* target, const char* c_string);

void Append(ArenaString* target, const char* source_buffer, int length);


// The no_copy() flag causes the target to simply point to the first value in source as a continuation after its last value so that both strings are sharing the values.
// The source ArenaString block is automatically de-allocated so that source is fully consumed.
void Append(ArenaString* target, ArenaString* source, int flags = 0);

// Remove count chars starting at index
void Remove(ArenaString* target, int index, int count = 0);

void Insert(ArenaString* target, const char* source, int index);
void Insert(ArenaString* target, const char* source, int length, int index);
void Insert(ArenaString* target, ArenaString* src, int index);