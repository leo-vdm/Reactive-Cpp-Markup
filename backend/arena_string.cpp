#include "arena.h"
#include "arena_string.h"
#include <assert.h>
#include <cstdio>
#include <cstring>

ArenaString* CreateString(Arena* arena)
{
    ArenaString* new_string = (ArenaString*)Alloc(arena, sizeof(StringBlock));
    
    new_string->parent_arena = arena;
    new_string->head = (StringBlock*)Alloc(arena, sizeof(StringBlock));
    new_string->head->fill_level = 0;
    new_string->head->next = NULL;
    new_string->tail = new_string->head;
    new_string->length = 0;
    
    // Must be set otherwise DeAlloc wont work properly, dev should set it when creating the arena
    assert(arena->alloc_size == sizeof(StringBlock));
    
    arena->alloc_size = sizeof(StringBlock);
    
    return new_string;
}

void FreeString(ArenaString* freed_string)
{

    if(freed_string)
    {   
        if(!freed_string->head)
        {
            DeAlloc(freed_string->parent_arena, (void*)freed_string);
            return;
        }
        
        Arena* parent_arena = freed_string->parent_arena;
        StringBlock* curr_block = freed_string->head;
        StringBlock* prev_block = (StringBlock*)freed_string;
        
        while(curr_block)
        {
            DeAlloc(parent_arena, (void*)prev_block);
            prev_block = curr_block;
            curr_block = curr_block->next;
        }
        
        // Note(Leo): Dealloc remaining block 
        DeAlloc(parent_arena, (void*)prev_block);
    }
}

#define is_full(StringBlock_ptr) StringBlock_ptr->fill_level >= STRING_BLOCK_BODY_SIZE
#define block_size(StrinBlock_ptr) STRING_BLOCK_BODY_SIZE

void Append(ArenaString* target, const char* c_string)
{
    char* curr_char = (char*)c_string;
    
    StringBlock* working_block = target->tail;
    
    while(*curr_char != '\0')
    {
        if(is_full(working_block))
        {   
            working_block->next = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
            working_block = working_block->next;
            working_block->next = NULL; // TODO(Leo): This is stupid, Arena should zero allocated memory by default and have optional flags
            working_block->fill_level = 0;
            target->tail = working_block;
            
        }
        working_block->content[working_block->fill_level] = *curr_char;
        working_block->fill_level++;
        
        target->length++;
        curr_char++;
    }
}

void Append(ArenaString* target, const char* source_buffer, int length)
{
    StringBlock* working_block = target->tail;
    for(int i = 0; i < length; i++)
    {
        if(is_full(working_block))
        {
            working_block->next = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
            working_block = working_block->next;
            working_block->next = NULL; // TODO(Leo): This is stupid, Arena should zero allocated memory by default and have optional flags
            working_block->fill_level = 0;
            target->tail = working_block;
        }
        working_block->content[working_block->fill_level] = *(source_buffer + i);
        working_block->fill_level++;
        
        target->length++;
    }
    
}

void Append(ArenaString* target, ArenaString* source, int flags)
{
    if(flags & no_copy())
    {
        StringBlock* last_in_target = target->tail;
        last_in_target->next = source->head;
        target->tail = source->tail;
        target->length += source->length;
        // Destroy the appended string's handle 
        DeAlloc(source->parent_arena, source);
        return;
    }
    StringBlock* curr_copied = source->head;
    StringBlock* curr_target = target->tail;
    while(curr_copied)
    {
        curr_target->next = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
        memcpy(curr_target->next, curr_copied, sizeof(StringBlock));
        curr_target = curr_target->next;
        target->tail = curr_target;
        curr_copied = curr_copied->next;
    }
    target->length += source->length;
    curr_target->next = NULL;
    return;
}

// Flattens the string out into a single cstring on the scratch arena
char* Flatten(ArenaString* string)
{
    StringBlock* curr_block = string->head;
    char* out_buffer = (char*)AllocScratch((string->length + 1)*sizeof(char), no_zero()); // +1 to fit \0
    
    int buffer_index = 0;
    
    while(curr_block)
    {
        memcpy((out_buffer + buffer_index), curr_block->content, curr_block->fill_level*sizeof(char));
        buffer_index += curr_block->fill_level;
        curr_block = curr_block->next;
    }
    
    out_buffer[string->length] = '\0';
    return out_buffer;
}

char* Flatten(ArenaString* string, Arena* target_arena)
{   
    StringBlock* curr_block = string->head;
    char* out_buffer = (char*)Alloc(target_arena, (string->length + 1)*sizeof(char)); // +1 to fit \0
    
    int buffer_index = 0;
    
    while(curr_block)
    {
        memcpy((out_buffer + buffer_index), curr_block->content, curr_block->fill_level*sizeof(char));
        buffer_index += curr_block->fill_level;
        curr_block = curr_block->next;
    }
    
    out_buffer[string->length] = '\0';
    return out_buffer;
}

void Flatten(ArenaString* string, char* target_buffer)
{
    StringBlock* curr_block = string->head;
    
    int buffer_index = 0;
    
    while(curr_block)
    {
        memcpy((target_buffer + buffer_index), curr_block->content, curr_block->fill_level*sizeof(char));
        buffer_index += curr_block->fill_level;
        curr_block = curr_block->next;
    }
    
    target_buffer[string->length] = '\0';
    return;
}

void Flatten(ArenaString* string, char* target_buffer, int length)
{
    StringBlock* curr_block = string->head;
    
    int buffer_index = 0;
    
    int curr_written_count = 0;
    int curr_block_index = 0;
    while(curr_block && (curr_written_count < length)){
        if(curr_block_index >= curr_block->fill_level)
        {
            curr_block = curr_block->next;
            curr_block_index = 0;
            continue;
        }
        target_buffer[buffer_index] = curr_block->content[curr_block_index];
        
        curr_block_index++;
        buffer_index++;
        curr_written_count += sizeof(char);
    }
    
    // Indicates that the loop stopped due to hitting length limit
    if(curr_written_count == length)
    {
        target_buffer[curr_written_count - 1] = '\0';    
    }
    else
    { // Indicates there is still more space in the buffer
        target_buffer[curr_written_count] = '\0';
    }
    
    return;
}