#include <cstddef>
#include <cstdint>
#pragma once

// Buddy allocator for Buffers that cant be arbitrarily read from (we cant store the allocation tree in)

struct Buddy
{
    uintptr_t base_offset;
    int size;
    
    Buddy(uintptr_t base_offset, int size)
    {
        
    }
}

Buddy CreateBuddy(int base_offset, int size);

int Alloc(Buddy* buddy, int size);

void DeAlloc(Buddy* buddy, int allocation_offset);

void ResetBuddy(Buddy* buddy);