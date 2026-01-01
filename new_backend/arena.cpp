#include "arena.h"

#if !ARENA_IMPLEMENTATION

#define ARENA_IMPLEMENTATION 1

Arena CreateArenaWith(void* memory_block, u32 memory_block_size, u32 alloc_size, u32 flags)
{
    Arena new_arena = {(uptr)memory_block, (uptr)memory_block, 0, 0, memory_block_size, alloc_size, flags};

    new_arena.furthest_committed = new_arena.next_address + memory_block_size;
    
    // Allocate the stub item
    new_arena.next_address += alloc_size;
    
    return new_arena;
}

void DeAlloc(Arena* arena, void* address)
{
    ((FreeBlock*)address)->next_free = arena->first_free.next_free;
    arena->first_free.next_free = ((FreeBlock*)address);
}

void ResetArena(Arena* arena)
{
    // Note(Leo): Account for the stub item
    arena->next_address = arena->mapped_address + arena->alloc_size;
    arena->first_free = {};
}

#if PLATFORM_WINDOWS

uptr WINDOWS_PAGE_MASK = 0;
uptr WINDOWS_PAGE_SIZE = 1;

#include <windows.h>
Arena CreateArena(u32 reserved_size, u32 alloc_size, u32 flags)
{
    Arena new_arena = {};
    if(flags & malloced_arena())
    {
        // Note(Leo): Add alloc size to leave space for our stub item
        void* mapped_address = malloc(reserved_size + alloc_size);
        new_arena = {(uptr)mapped_address, (uptr)mapped_address, 0, 0, reserved_size + alloc_size, alloc_size, flags};
        new_arena.furthest_committed = ((uptr)mapped_address) + reserved_size + alloc_size;
    }
    else
    {
        void* mapped_address = VirtualAlloc(NULL, reserved_size + alloc_size, MEM_RESERVE, PAGE_READWRITE);
        new_arena = {(uptr)mapped_address, (uptr)mapped_address, 0, 0, reserved_size + alloc_size, alloc_size, flags};
        
        // Allocate the first n pages to fit our stub item
        u32 allocated = (alloc_size + WINDOWS_PAGE_SIZE) / WINDOWS_PAGE_SIZE;
        VirtualAlloc((void*)new_arena.next_address, allocated, MEM_COMMIT, PAGE_READWRITE);
        new_arena.furthest_committed = new_arena.next_address + allocated;
    }
    
    new_arena.next_address += alloc_size;
    
    return new_arena;
}

void* Alloc(Arena* arena, u32 size, u32 flags)
{
    // Hand out the stub by default
    void* allocated_address = (void*)arena->mapped_address;
    
    // If allocation is 1 item, try to use a freeblock if there is one
    if(size == arena->alloc_size && arena->first_free.next_free)
    {
        allocated_address = (void*)(arena->first_free.next_free);
        arena->first_free.next_free = ((FreeBlock*)allocated_address)->next_free;
    }
    else if(arena->next_address + size < arena->mapped_address + arena->size)
    {    
        allocated_address = (void*)arena->next_address;
        
        // Check if we're allocating over the boundry of memory weve commited, if we are commit the new pages.
        uptr new_next_address = arena->next_address + size;
        
        if(new_next_address > arena->furthest_committed)
        {
            LPVOID result = VirtualAlloc((void*)arena->furthest_committed, WINDOWS_PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
            arena->furthest_committed += WINDOWS_PAGE_SIZE;
        }
        arena->next_address += size;
    }
    // We cant hand out the stub and theres no remaining space
    else if(size > arena->alloc_size)
    {
        // Overflow
        debug_assert(0, "Arena ran out of space!\n");
        return NULL;
    }
    
    if(!(flags & no_zero_mem()))
    {
        memset((void*)allocated_address, 0, size);
    }
        
    return allocated_address;
}

void FreeArena(Arena* arena)
{
    if(arena->flags & malloced_arena())
    {
        free((void*)arena->mapped_address);
    }
    else
    {
        VirtualFree((void*)arena->mapped_address, arena->size, MEM_RELEASE);
    }
}

#endif // PLATFORM_WINDOWS
#if PLATFORM_LINUX || PLATFORM_ANDROID

#include <sys/mman.h>
Arena CreateArena(u32 reserved_size, u32 alloc_size, u32 flags)
{
    Arena new_arena = {};
    if(flags & malloced_arena())
    {
        void* mapped_address = malloc(reserved_size + alloc_size);
        new_arena = {(uptr)mapped_address, (uptr)mapped_address, 0, 0, reserved_size + alloc_size, alloc_size, flags};
        new_arena.furthest_committed = ((uptr)mapped_address) + reserved_size + alloc_size;
    }
    else
    {
        void* mapped_address = mmap(NULL, reserved_size + alloc_size,  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 0, 0);
        new_arena = {(uptr)mapped_address, (uptr)mapped_address, 0, 0, reserved_size + alloc_size, alloc_size, flags};
        new_arena.furthest_committed = new_arena.next_address + alloc_size;
    }
    
    // Allocate the stub item
    new_arena.next_address += alloc_size;
    
    return new_arena;
}

void* Alloc(Arena* arena, u32 size, u32 flags)
{
    void* allocated_address = arena->mapped_address;
    
    // If allocation is 1 item, try to use a freeblock if there is one
    if(size == arena->alloc_size && arena->first_free.next_free)
    {
        allocated_address = (void*)(arena->first_free.next_free);
        arena->first_free.next_free = ((FreeBlock*)allocated_address)->next_free;
    }
    else if(arena->next_address + size < arena->mapped_address + arena->size)
    {
        allocated_address = (void*)arena->next_address;
        arena->next_address += size;
        
        arena->furthest_committed = MAX(arena->next_address, arena->furthest_committed);
    }
    // We cant hand out the stub and theres no remaining space
    else if(size > arena->alloc_size)
    {
        // Overflow
        debug_assert(0, "Arena ran out of space!\n");
        return NULL;
    }
    
    if(!(flags & no_zero_mem()))
    {
        memset((void*)allocated_address, 0, size);
    }
        
    return allocated_address;
}

void FreeArena(Arena* arena)
{
    if(arena->flags & malloced_arena())
    {
        free((void*)arena->mapped_address);
    }
    else
    {
        munmap((void*)arena->mapped_address, arena->size);
    }
}
#endif // PLATFORM_LINUX || PLATFORM_ANDROID

// Get space from the scratch arena
void* AllocScratch(u32 alloc_size, u32 flags)
{
    debug_assert(alloc_size > 0, "Encountered zero sized allocation!\n");
    if(scratch_arena.mapped_address == 0)
    {
        printf("Scratch arena not initialized!\n");
        return NULL;
    }

    uintptr_t allocated_address = (uintptr_t)Alloc(&scratch_arena, alloc_size, flags); 
    
    // Use alloc size as a counter instead to know how many allocs have taken place
    scratch_arena.alloc_size++;
    
    return (void*)allocated_address;
}


// Return space to the scratch arena
void DeAllocScratch(void* address)
{
    // early return if we can just free the whole arena
    scratch_arena.alloc_size--;
    if(scratch_arena.alloc_size <= 0)
    {
        scratch_arena.alloc_size = 0;
        scratch_arena.next_address = scratch_arena.mapped_address;
        return;
    }   
    // TODO(Leo): Implement an actual de-allocation system.
}


Arena scratch_arena = Arena();

void InitScratch(u32 reserved_size, u32 flags)
{
    scratch_arena = CreateArena(reserved_size, 0, flags);
}

#endif
