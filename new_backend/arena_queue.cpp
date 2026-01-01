#include "arena_queue.h"

#if !QUEUE_IMPLEMENTATION

#define QUEUE_IMPLEMENTATION 1

ArenaQueue CreateQueue(u32 item_size, u32 capacity, u32 flags)
{
    ArenaQueue created = {};
    created.items = CreateArena(capacity*item_size, item_size, flags & malloced_arena());
    created.capacity = capacity;
    
    return created;
}

ArenaQueue CreateQueue(Arena arena)
{
    ArenaQueue created = {};
    created.items = arena;
    
    debug_assert(created.items.alloc_size, "Arena must have an alloc size to be used as a queue!\n");
    if(!created.items.alloc_size)
    {
        created.capacity = 0;
    }
    
    created.capacity = created.items.size/created.items.alloc_size;

    return created;
}

void* Enqueue(ArenaQueue* que, u32 flags)
{
    que->head = (que->head + 1) % que->capacity;
 
    if(que->items.next_address - que->items.mapped_address < que->head * que->items.alloc_size)
    {
        Alloc(&que->items, que->items.alloc_size, no_zero_mem());
    }
    
    // Evict an item from the tail to make space
    if(que->head == que->tail)
    {
        que->tail = (que->tail + 1) % que->capacity;
    }
    else
    {
        ++que->count;
    }
    
    void* item = (void*)(que->items.mapped_address + (que->head * que->items.alloc_size));
    // Zero our item if the user wanted us to
    if(!(flags & no_zero_mem()))
    {
        memset(item, 0, que->items.alloc_size);
    }
    
    return item;
}

void* GetTail(ArenaQueue* que, u32 index)
{
    return (void*)(que->items.mapped_address + (((que->tail + index) % que->capacity) * que->items.alloc_size));
}

void* GetHead(ArenaQueue* que, u32 index)
{
    u32 final_index = index % que->capacity;
    if(final_index > que->head)
    {
        final_index = que->head + que->capacity - final_index;
    }
    else
    {
        final_index = que->head - final_index;
    }
    return (void*)(que->items.mapped_address + (final_index * que->items.alloc_size));
}

void DeQueueTail(ArenaQueue* que)
{
    --que->count;
    que->tail = (que->tail + 1) % que->capacity;
}

void DeQueueHead(ArenaQueue* que)
{
    --que->count;
    
    if(!que->head)
    {
        que->head = que->capacity - 1;
    }
    else
    {
        --que->head;
    }
}

#endif
