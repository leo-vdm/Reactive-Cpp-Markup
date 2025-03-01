#include <cstddef>
#include <cstdint>
#pragma once
struct FreeBlock {
    FreeBlock* next_free;
};

struct Arena
{
    uintptr_t mapped_address;
    uintptr_t next_address;
    FreeBlock first_free;
    int size;
    int alloc_size; // Size of objects in this arena
    int flags;
        
    // alloc_size must be larger than the size of freeblock! (>=8 bytes)    
    Arena(void* position, int size, int alloc_size, uint64_t flags){
        this->mapped_address = (uintptr_t)position;
        this->next_address = (uintptr_t)position;
        this->size = size;
        this->alloc_size = alloc_size;
        this->first_free = {};
        this->flags = flags;
    }
    
    Arena()
    {
        mapped_address = 0;
        next_address = 0;
        size = 0;
        alloc_size = 0;
        flags = 0;
    }

};


#define zero() (uint64_t)(1 << 0)

#define no_zero() (uint64_t)(1 << 1)

extern Arena scratch_arena;

Arena CreateArena(int reserved_size, int alloc_size, uint64_t flags = 0);

// Get space from the scratch arena

void* AllocScratch(int alloc_size, uint64_t flags = 0);

// Return space to the scratch arena
void DeAllocScratch(void* address);

//void* Alloc(Arena* arena); // Allocate based on the alloc_size
void* Alloc(Arena* arena, int size, uint64_t flags = 0); // Allocate an arbitrary size
void DeAlloc(Arena* arena, void* address);

void ResetArena(Arena* arena);

void FreeArena(Arena* arena); // Free the memory region associated with an arena.

void InitScratch(int reserved_size, uint64_t flags = 0);