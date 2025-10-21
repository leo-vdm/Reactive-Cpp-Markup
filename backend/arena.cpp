#include <string.h>
#include <cassert>

#include <stdio.h>
#include <iostream>
#include "arena.h"
#include "simd.h"

#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
uintptr_t WINDOWS_PAGE_MASK;
uintptr_t WINDOWS_PAGE_SIZE;
// Windows definitions for memory management
#include <windows.h>
Arena CreateArena(int reserved_size, int alloc_size, uint64_t flags)
{
    Arena new_arena = Arena(VirtualAlloc(NULL, reserved_size, MEM_RESERVE, PAGE_READWRITE), reserved_size, alloc_size, flags);
    
    // Allocate the first page
    VirtualAlloc((void*)new_arena.next_address, WINDOWS_PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
    new_arena.furthest_committed = new_arena.next_address + WINDOWS_PAGE_SIZE;
    
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
        // Check if we're allocating over the boundry of memory weve commited, if we are commit the new pages.
        uintptr_t new_next_address = arena->next_address + size;
        
        if(new_next_address > arena->furthest_committed)
        {
            LPVOID result = VirtualAlloc((void*)arena->furthest_committed, size, MEM_COMMIT, PAGE_READWRITE);
            arena->furthest_committed += size;
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


// Returns false if they are not equal and true if they are
bool CompareArenaContents(Arena* first, Arena* second)
{
    uint32_t first_size = first->next_address - first->mapped_address;
    uint32_t second_size = second->next_address - second->mapped_address;
    if(first_size != second_size)
    {
        return false;
    }
    
    // Need to get the arenas aligned to our simd register size
    uint32_t required_padding = (SIMD_WIDTH*4) - (second_size % (SIMD_WIDTH*4));
    Alloc(first, required_padding, zero());
    Alloc(second, required_padding, zero());

    uintptr_t first_curr = first->mapped_address;
    uintptr_t second_curr = second->mapped_address;

    uint32_t iterations = (first_size + required_padding)/(SIMD_WIDTH*4);

    switch(SUPPORTED_SIMD)
    {
        #if ARCH_X64
        case(SimdLevel::AVX512):
        case(SimdLevel::AVX2):
        {
            for(int i = 0; i < iterations; i++)
            {
                i256 first_reg = load_i256((void*)first_curr);
                i256 second_reg = load_i256((void*)second_curr);
             
                if(!test_equal_i256(first_reg, second_reg))
                {
                    first->next_address -= required_padding;
                    second->next_address -= required_padding;
                    return false;
                }
                
                first_curr += SIMD_WIDTH*4;
                second_curr += SIMD_WIDTH*4;
            }
            
            break;
        }
        #endif
        #if ARCH_X64 || ARCH_NEON || ARCH_X64_SSE
        case(SimdLevel::SSE2):
        {
            for(int i = 0; i < iterations; i++)
            {
                i128 first_reg = load_i128((void*)first_curr);
                i128 second_reg = load_i128((void*)second_curr);
                i128 output_reg = cmp_i8_128(first_reg, second_reg);
                
                if(!test_all_ones_i128(output_reg))
                {
                    first->next_address -= required_padding;
                    second->next_address -= required_padding;
                    return false;
                }
                
                first_curr += SIMD_WIDTH*4;
                second_curr += SIMD_WIDTH*4;
            }
            
            break;
        }
        #endif
        default:
        {
            uint32_t* first_val = (uint32_t*)first_curr;
            uint32_t* second_val = (uint32_t*)second_curr;
            for(int i = 0; i < iterations; i++)
            {
                if(first_val[i] != second_val[i])
                {
                    return false;
                }
            }
        
            break;
        }
    }
    
    first->next_address -= required_padding;
    second->next_address -= required_padding;
    
    return true;
}
