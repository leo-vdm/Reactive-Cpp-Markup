#include <string.h>
#include <cassert>

#include <stdio.h>
#include <iostream>
#include "arena.h"

#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
uintptr_t WINDOWS_PAGE_MASK;
uintptr_t WINDOWS_PAGE_SIZE;
// Windows definitions for memory management
#include <windows.h>
Arena CreateArena(int reserved_size, int alloc_size, uint64_t flags)
{
    Arena new_arena = Arena(VirtualAlloc(NULL, reserved_size, MEM_RESERVE, PAGE_READWRITE), reserved_size, alloc_size, flags);
    
    // Allocate the first page
    VirtualAlloc((void*)(new_arena.next_address & ~(WINDOWS_PAGE_MASK)), WINDOWS_PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
    new_arena.furthest_committed = (new_arena.next_address & ~(WINDOWS_PAGE_MASK)) + WINDOWS_PAGE_SIZE;
    
    return new_arena;
}

Arena CreateArenaWith(void* memory_block, int memory_block_size, int alloc_size, uint64_t flags)
{
    Arena new_arena = Arena(memory_block, memory_block_size, alloc_size, flags);

    new_arena.furthest_committed = new_arena.next_address + memory_block_size;
    return new_arena;
}

void* Alloc(Arena* arena, int size, uint64_t flags)
{
    #ifndef NDEBUG
    if(size == 0)
    {
        printf("Debug: Warning, allocation with size of 0 bytes occured, ensure that is intentional!\n");
    }    
    #endif
    assert(size >= 0);
    assert(arena);
    void* allocatedAddress = (void*)arena->next_address;
    
    // If allocation is 1 item, try to use a freeblock if there is one
    if(size == arena->alloc_size && arena->first_free.next_free)
    {
        allocatedAddress = (void*)(arena->first_free.next_free);
        arena->first_free.next_free = ((FreeBlock*)allocatedAddress)->next_free;
    }
    else
    {    
        // Check if we are allocating over the page boundry of memory weve commited, if we are commit the new pages.
        //
        // Note(Leo): + Page size to always round up
        uintptr_t new_next_address = arena->next_address + size;
        
        if(new_next_address > arena->furthest_committed)
        {
            uintptr_t aligned_new_next_address = (new_next_address + WINDOWS_PAGE_SIZE) & ~(WINDOWS_PAGE_MASK);
            LPVOID result = VirtualAlloc((void*)arena->furthest_committed, aligned_new_next_address - arena->furthest_committed, MEM_COMMIT, PAGE_READWRITE);
            arena->furthest_committed = aligned_new_next_address;
        }
        arena->next_address += size;
    }
    
    // Overflow
    assert(arena->next_address <= arena->mapped_address + arena->size);
    
    if(flags & no_zero())
    {
       return allocatedAddress;
    }
    
    if(flags & zero() || !arena->flags)
    {
        memset((void*)allocatedAddress, 0, size);
        return allocatedAddress;
    }
    
    return allocatedAddress;
}

void DeAlloc(Arena* arena, void* address)
{
    ((FreeBlock*)address)->next_free = arena->first_free.next_free;
    arena->first_free.next_free = ((FreeBlock*)address);
    
}

void ResetArena(Arena* arena)
{
    
    arena->next_address = arena->mapped_address;
    arena->first_free = {};
    
}

void FreeArena(Arena* arena)
{
    VirtualFree((void*)arena->mapped_address, arena->size, MEM_RELEASE);

}

#elif defined(__linux__) && !defined(_WIN32)
// Unix definitions for memory management
#include <sys/mman.h>
Arena CreateArena(int reserved_size, int alloc_size, uint64_t flags)
{
    Arena newArena = Arena(mmap(NULL, reserved_size,  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, 0, 0), reserved_size, alloc_size, flags);
    return newArena;
}

Arena CreateArenaWith(void* memory_block, int memory_block_size, int alloc_size, uint64_t flags)
{
    Arena new_arena = Arena(memory_block, memory_block_size, alloc_size, flags);
    return new_arena;
}

void* Alloc(Arena* arena, int size, uint64_t flags)
{
    #ifdef DEBUG
    if(size == 0)
    {
        printf("Debug: Warning, allocation with size of 0 bytes occured, ensure that is intentional!\n");
    }
    #endif
    assert(size >= 0);
    assert(arena);
    void* allocatedAddress = (void*)arena->next_address;
    
    // If allocation is 1 item, try to use a freeblock if there is one
    if(size == arena->alloc_size && arena->first_free.next_free)
    {
        allocatedAddress = (void*)(arena->first_free.next_free);
        arena->first_free.next_free = ((FreeBlock*)allocatedAddress)->next_free;
    }
    else
    {
        arena->next_address += size;
    }
    
    // Overflow
    assert(arena->next_address <= arena->mapped_address + arena->size);
    
    if(flags & no_zero())
    {
        return allocatedAddress;
    }
    if(flags & zero() || !arena->flags)
    {
        memset((void*)allocatedAddress, 0, size);
        return allocatedAddress;
    }
    
    return allocatedAddress;
}

void DeAlloc(Arena* arena, void* address)
{
    ((FreeBlock*)address)->next_free = arena->first_free.next_free;
    arena->first_free.next_free = ((FreeBlock*)address);    
}

// Release and re-obtain an address space to ensure resources go back to kernel
void ResetArena(Arena* arena)
{
    arena->next_address = arena->mapped_address;
    arena->first_free = {};
}

void FreeArena(Arena* arena)
{
    munmap((void*)arena->mapped_address, arena->size);
}


#endif

void Pop(Arena* arena, int size)
{
    assert(size > 0);
    assert(arena->next_address - size > arena->mapped_address);
    
    arena->next_address -= size;
}

void* Push(Arena* arena, int size, uint64_t flags)
{
    return Alloc(arena, size, flags);
}

// Get space from the scratch arena
void* AllocScratch(int alloc_size, uint64_t flags)
{
    assert(alloc_size > 0);
    if(scratch_arena.mapped_address == 0)
    {
        printf("Scratch arena not initialized!");
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
    if(scratch_arena.alloc_size <= 0){
        scratch_arena.alloc_size = 0;
        scratch_arena.next_address = scratch_arena.mapped_address;
        return;
    }   
    // TODO(Leo): Implement an actual de-allocation system.
}


Arena scratch_arena = Arena();

void InitScratch(int reserved_size, uint64_t flags)
{
    scratch_arena = CreateArena(reserved_size, 0, flags);
}
