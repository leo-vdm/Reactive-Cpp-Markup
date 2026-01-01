#include "arena.h"
#include "prim_types.h"

#if !QUEUE_HEADER
#define QUEUE_HEADER 1

struct ArenaQueue
{
    Arena items;
    u32 count;
    u32 capacity;
    u32 head;
    u32 tail;
};

ArenaQueue CreateQueue(u32 item_size, u32 capacity, u32 flags=0);
// Note(Leo): Auto calculates the capacity using the arena's alloc size
ArenaQueue CreateQueue(Arena arena);

// Push onto the head
void* Enqueue(ArenaQueue* que, u32 flags = 0);

void* GetTail(ArenaQueue* que, u32 index = 0);
void* GetHead(ArenaQueue* que, u32 undex = 0);

void DeQueueTail(ArenaQueue* que);
void DeQueueHead(ArenaQueue* que);

#endif
