#include "arena_string.h"
#include <assert.h>
#include <cstdio>
#include <cstring>

#if !STRING_IMPLEMENTATION
#define STRING_IMPLEMENTATION 1

ArenaString* CreateString(Arena* arena)
{
    ArenaString* new_string = (ArenaString*)Alloc(arena, sizeof(StringBlock));
    
    new_string->parent_arena = arena;
    new_string->head = ArenaPush(arena, StringBlock);
    new_string->head->fill_level = 0;
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
            DeAlloc(freed_string->parent_arena, freed_string);
            return;
        }
        
        Arena* parent_arena = freed_string->parent_arena;
        StringBlock* curr_block = freed_string->head;
        StringBlock* prev_block = (StringBlock*)freed_string;
        
        while(curr_block)
        {
            DeAlloc(parent_arena, prev_block);
            prev_block = curr_block;
            curr_block = curr_block->next;
        }
        
        // Note(Leo): Dealloc remaining block 
        DeAlloc(parent_arena, prev_block);
    }
}

#define is_full(StringBlock_ptr) (StringBlock_ptr->fill_level >= STRING_BLOCK_BODY_SIZE)

void Remove(ArenaString* target, u32 index, u32 count)
{
    assert(target);
    assert(target->length >= index + count);
    assert(index >= 0 && count > 0);
    if(target->length < (index + count) || !count || count <= 0 || index < 0)
    {
        return;
    }
 
    StringBlock* starting_block = NULL;
    StringBlock* ending_block = NULL;
    
    u32 curr_index = 0;
    StringBlock* curr = target->head;
    while(curr)
    {
        if((curr_index + curr->fill_level) >= index)
        {
            starting_block = curr;
            break;
        }
        curr_index += curr->fill_level;
        
        curr = curr->next;
    }
    
    assert(starting_block);
    
    u32 end_index = index + count;
    
    // Note(Leo): Removed part is contained entirely inside of startblock
    if((curr_index + starting_block->fill_level) >= end_index)
    {
        // Note(Leo): Manual copy since memcpy doesnt like overlapping copies
        u32 local_index = index - curr_index;
        char* dst = &starting_block->content[local_index];
        char* src = &starting_block->content[local_index + count];
        u32 copy_count = starting_block->fill_level - (local_index + count);
        
        for(u32 i = 0; i < copy_count; i++)
        {
            dst[i] = src[i];
        }
        
        starting_block->fill_level -= count;
        target->length -= count;
        
        return;
    }
    
    // Note(Leo): Removed part has parts contained in both startingblock and endingblock
    u32 starting_block_index = index - curr_index;
    StringBlock* prev = NULL;
    while(curr)
    {
        if((curr_index + curr->fill_level) >= end_index)
        {
            ending_block = curr;
            break;
        }
        curr_index += curr->fill_level;
        
        StringBlock* old = curr;
        
        // Note(Leo): This block is entirely removed so de-allocate it 
        if(old != starting_block)
        {
            assert(prev); // This shouldve been initialized by here
            prev->next = curr->next;
            DeAlloc(target->parent_arena, old);
        }
        prev = curr;
        curr = curr->next;
    }
    assert(ending_block);
    
    // Remove content at end of startingblock
    starting_block->fill_level -= starting_block->fill_level - starting_block_index;
    
    // Note(Leo): Manual copy since memcpy doesnt like overlapping copies
    u32 ending_block_index = end_index - curr_index;
    char* dst = &ending_block->content[0];
    char* src = &ending_block->content[ending_block_index];
    u32 copy_count = ending_block->fill_level - ending_block_index;
    
    for(u32 i = 0; i < copy_count; i++)
    {
        dst[i] = src[i];
    }
    
    ending_block->fill_level -= ending_block_index;
    
    target->length -= count;
}

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
            target->tail = working_block;
            
        }
        working_block->content[working_block->fill_level] = *curr_char;
        working_block->fill_level++;
        
        target->length++;
        curr_char++;
    }
}

void Append(ArenaString* target, const char* source_buffer, u32 length)
{
    StringBlock* working_block = target->tail;
    for(u32 i = 0; i < length; i++)
    {
        if(is_full(working_block))
        {
            working_block->next = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
            working_block = working_block->next;
            target->tail = working_block;
        }
        working_block->content[working_block->fill_level] = *(source_buffer + i);
        working_block->fill_level++;
        
        target->length++;
    }
    
}

void Append(ArenaString* target, ArenaString* source, u32 flags)
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
        curr_copied = curr_copied->next;
    }

    target->length += source->length;
    curr_target->next = NULL;
    target->tail = curr_target;
    
    return;
}

StringView FlattenToScratch(ArenaString* string, u32 flags)
{
    StringBlock* curr_block = string->head;
    StringView result = {};
    if(flags & null_term())
    {
        result.value = (char*)AllocScratch((string->length + 1)*sizeof(char), no_zero_mem());
        result.value[string->length] = '\0';
        result.length = string->length + 1;
    }
    else
    {
        result.value = (char*)AllocScratch((string->length)*sizeof(char), no_zero_mem());
        result.length = string->length;
    }
    
    u32 buffer_index = 0;
    
    while(curr_block)
    {
        memcpy(result.value + buffer_index, curr_block->content, curr_block->fill_level*sizeof(char));
        buffer_index += curr_block->fill_level;
        curr_block = curr_block->next;
    }
    
    return result;
}

StringView FlattenToArena(ArenaString* string, Arena* target_arena, u32 flags)
{   
    StringBlock* curr_block = string->head;
    
    StringView result = {};
    if(flags & null_term())
    {
        result.value = (char*)Alloc(target_arena, (string->length + 1)*sizeof(char));
        result.value[string->length] = '\0';
        result.length = string->length + 1;
    }
    else
    {
        result.value = (char*)Alloc(target_arena, string->length*sizeof(char));
        result.length = string->length;
    }
    
    u32 buffer_index = 0;
    
    while(curr_block)
    {
        memcpy(result.value + buffer_index, curr_block->content, curr_block->fill_level*sizeof(char));
        buffer_index += curr_block->fill_level;
        curr_block = curr_block->next;
    }

    return result;
}

StringView FlattenToBuffer(ArenaString* string, char* target_buffer, u32 flags)
{
    StringBlock* curr_block = string->head;
    StringView result = {target_buffer, string->length};
    
    u32 buffer_index = 0;
    
    while(curr_block)
    {
        memcpy((target_buffer + buffer_index), curr_block->content, curr_block->fill_level*sizeof(char));
        buffer_index += curr_block->fill_level;
        curr_block = curr_block->next;
    }
    
    if(flags & null_term())
    {
        target_buffer[string->length] = '\0';
        result.length = string->length + 1;
    }
    
    return result;
}

StringView FlattenToBufferN(ArenaString* string, char* target_buffer, u32 length, u32 flags)
{
    StringBlock* curr_block = string->head;
    StringView result = {target_buffer, length};
    
    u32 buffer_index = 0;
    
    u32 curr_written_count = 0;
    u32 curr_block_index = 0;
    while(curr_block && (curr_written_count < length))
    {
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

    if(flags & null_term())
    {
        target_buffer[length] = '\0';    
        result.length = length + 1;
    }
    
    return result;
}

void Insert(ArenaString* target, const char* source, u32 index)
{
    u32 source_len = static_cast<u32>(strlen(source));
    Insert(target, source, source_len, index);
}

void Insert(ArenaString* target, const char* source, u32 length, u32 index)
{
    assert(target && source);
    if(index > target->length)
    {
        assert(0);
        return;
    }

    StringBlock* starting_block = target->head;
    u32 curr_index = 0;
    while(curr_index < index)
    {
        assert(starting_block);
        if((curr_index + starting_block->fill_level) >= index)
        {
            break;
        }

        curr_index += starting_block->fill_level;
        starting_block = starting_block->next;
    }
    
    u32 local_index = index - curr_index;
    // We can copy directly into this block
    if((length + starting_block->fill_level) < STRING_BLOCK_BODY_SIZE)
    {
        // There is text we need to move out of the way first
        if(starting_block->fill_level > local_index && starting_block->fill_level)
        {
            u32 copy_count = starting_block->fill_level - local_index;
            char* dst = &starting_block->content[local_index + length];
            char* src = &starting_block->content[local_index];
            
            // Note(Leo): Use a temp buffer otherwise we might write over sections we want to read
            char* temp = (char*)AllocScratch(copy_count*sizeof(char));
            memcpy(temp, src, copy_count);
            memcpy(dst, temp, copy_count);
            DeAllocScratch(temp);
        }
        
        starting_block->fill_level += length;
        memcpy(&starting_block->content[local_index], source, length);
        target->length += length;
        return;
    }
    // Need to allocate new blocks
    
    u32 remaining_buffer = length;
    
    char* curr_src = (char*)source;
    // There is text we need to move out of the way first
    if(starting_block->fill_level > local_index && starting_block->fill_level)
    {
        u32 copy_count = starting_block->fill_level - local_index;
        StringBlock* new_block = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
        char* dst = &new_block->content[0];
        char* src = &starting_block->content[local_index];
            
        memcpy(dst, src, copy_count);
        new_block->fill_level = copy_count;
        starting_block->fill_level -= copy_count;
        new_block->next = starting_block->next;
        starting_block->next = new_block;
        
        copy_count = STRING_BLOCK_BODY_SIZE - starting_block->fill_level;
        copy_count = copy_count < remaining_buffer ? copy_count : remaining_buffer;
        remaining_buffer -= copy_count;
        
        memcpy(&starting_block->content[local_index], curr_src, copy_count);
        curr_src += copy_count;
        starting_block->fill_level += copy_count;
    }
    
    // Start allocationg new blocks
    StringBlock* curr = starting_block;
    while(remaining_buffer)
    {
        StringBlock* new_block = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
        new_block->next = curr->next;
        curr->next = new_block;
        curr = new_block;
        
        u32 copy_count = remaining_buffer > STRING_BLOCK_BODY_SIZE ? STRING_BLOCK_BODY_SIZE : remaining_buffer;
        
        memcpy(&new_block->content[0], curr_src, copy_count);
        curr_src += copy_count;
        remaining_buffer -= copy_count;
        curr->fill_level = copy_count;
    }
    
    target->length += length;
}

void Insert(ArenaString* target, ArenaString* source, u32 index)
{
    assert(target && source);
    if(index > target->length)
    {
        assert(0);
        return;
    }

    StringBlock* starting_block = target->head;
    u32 curr_index = 0;
    while(curr_index < index)
    {
        assert(starting_block);
        if((curr_index + starting_block->fill_level) >= index)
        {
            break;
        }

        curr_index += starting_block->fill_level;
        starting_block = starting_block->next;
    }
    
    u32 local_index = index - curr_index;

    // Split starting block into two
    StringBlock* ending_block = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
    ending_block->fill_level = starting_block->fill_level - local_index;
    starting_block->fill_level = local_index;
    
    if(starting_block == target->tail)
    {
        target->tail = ending_block;
    }
    
    memcpy(&ending_block->content[0], &starting_block->content[local_index], ending_block->fill_level);

    ending_block->next = starting_block->next;
    starting_block->next = ending_block;

    StringBlock* curr_copied = source->head;
    StringBlock* curr_target = starting_block;
    
    while(curr_copied)
    {
        curr_target->next = (StringBlock*)Alloc(target->parent_arena, sizeof(StringBlock));
        memcpy(curr_target->next, curr_copied, sizeof(StringBlock));
        curr_target = curr_target->next;
        curr_copied = curr_copied->next;
    }
    
    target->length += source->length;
    curr_target->next = ending_block;
    return;
}

#endif
