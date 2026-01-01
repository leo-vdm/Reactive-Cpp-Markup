#include "prim_types.h"

#if !ARENA_HEADER

#define ARENA_HEADER 1
struct FreeBlock
{
    FreeBlock* next_free;
};

struct Arena
{
    uptr mapped_address;
    uptr next_address;
    uptr furthest_committed; // The end of the furthest page we have commited
    FreeBlock first_free;
    u32 size;
    u32 alloc_size; // Size of objects in this arena
    u32 flags;
};

#define zero_mem() (u32)(0 << 0)
#define no_zero_mem() (u32)(1 << 0)

#define malloced_arena() (u32)(1 << 1)
#define custon_alloced_arena() (u32)(1 << 2)

extern Arena scratch_arena;

Arena CreateArena(u32 reserved_size, u32 alloc_size, u32 flags = 0);
Arena CreateArenaWith(void* memory_block, u32 memory_block_size, u32 alloc_size, u32 flags = 0);

void* AllocScratch(u32 alloc_size, u32 flags = 0);
void DeAllocScratch(void* address);
void InitScratch(u32 reserved_size, u32 flags = 0);

void* Alloc(Arena* arena, u32 size, u32 flags = 0); // Allocate an arbitrary size
void DeAlloc(Arena* arena, void* address);

void ResetArena(Arena* arena);
void FreeArena(Arena* arena); // Free the memory region associated with an arena.

#define ArenaPush(arena_ptr, type, ...) ((type*)Alloc((arena_ptr), sizeof(type), __VA_ARGS__))
#define ArenaPushArray(arena_ptr, count, type, ...) ((type*)Alloc((arena_ptr), sizeof(type)*(count), __VA_ARGS__))
#define ArenaPushString(arena_ptr, length) ((char*)Alloc((arena_ptr), length, zero_mem()))

#if PLATFORM_WINDOWS
extern uptr WINDOWS_PAGE_SIZE;
extern uptr WINDOWS_PAGE_MASK;
#endif

#endif
