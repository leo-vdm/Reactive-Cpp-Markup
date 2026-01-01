#include "arena_xar.h"

#if !XAR_IMPLEMENTATION
#define XAR_IMPLEMENTATION 1

#if PLATFORM_WINDOWS
#pragma intrinsic(_BitScanReverse64)

u64 MSB(u64 x)
{
    u64 msb = 0;
    _BitScanReverse64((unsigned long*)&msb, x);
    return msb;
}
#endif

void* XarGet(Xar* xar, u32 index)
{
    u32 local_index = index;
    u32 block_index = 0;
    u32 block_size = 1 << xar->shift;

    u32 shift_index = index >> xar->shift;
    
    if(shift_index > 0)
    {
        block_index = MSB(shift_index);
        u32 block_capacity = 1 << (block_index + xar->shift);
        // Note(Leo): This works since the capacity of a block is equal to the capacity of all blocks before it + 1
        //            which is really neat since that means that the offset into this block is the absolute index minus
        //            the capacity of this block.
        local_index -= block_capacity;
        
        ++block_index;
    }
    
    return (void*)((uptr)xar->blocks[block_index] + local_index * xar->element_size);
}

u32 XarGetBlockIndex(Xar* xar, u32 index)
{
    u32 shift_index = index >> xar->shift;
    
    if(shift_index > 0)
    {
        return MSB(shift_index) + 1;
    }
    
    return 0;
}

void* XarPush(Xar* xar, u32 count, u32 flags)
{
    u32 current_block = XarGetBlockIndex(xar, xar->count - 1);
    u32 next_block = XarGetBlockIndex(xar, xar->count);

    if(current_block != next_block && next_block >= xar->block_count)
    {
        u32 capacity = 1 << (next_block + xar->shift);
        if(!xar->source_pool)
        {
            xar->blocks[next_block] = malloc(capacity*xar->element_size);
                
            if(!(flags & no_zero_mem()))
            {                
                memset(xar->blocks[next_block], 0, capacity*xar->element_size);
            }
        }
        else
        {
            // Note(Leo): -1 since block 0 and 1 share an arena
            xar->blocks[next_block] = Alloc(&(xar->source_pool->levels[next_block - 1]), capacity*xar->element_size, flags);
        }
    }
    
    if(!xar->blocks[next_block])
    {
        return NULL;
    }
    
    xar->count++;
    
    return XarGet(xar, xar->count);
}

void XarPop(Xar* xar)
{
    assert(xar);
    if(!xar->count)
    {
        return;
    }

    u32 current_block = XarGetBlockIndex(xar, xar->count - 1);
    u32 post_block = current_block;
    if(xar->count - 1)
    {
        post_block = XarGetBlockIndex(xar, xar->count - 2);
    }
    
    // We can de-allocate this block now that its empty
    if(current_block != post_block)
    {
        if(!xar->source_pool)
        {
            free(xar->blocks[current_block]);
        }
        else
        {
            DeAlloc(&(xar->source_pool->levels[current_block - 1]), xar->blocks[current_block]);
        }
        
        xar->blocks[current_block] = NULL;
        xar->block_count--;
    }
    
    xar->count--;
    
}

// The number of elements that can fit in a block
u32 XarGetBlockCapacity(Xar* xar, u32 block_index)
{
    assert(xar);
    if(!block_index)
    {
        block_index = 1;
    }
    
    return 1 << (block_index - 1 + xar->shift);    
}

// The number of elements that can fit in a block
u32 XarGetBlockCapacity(u8 shift, u32 block_index)
{
    if(!block_index)
    {
        block_index = 1;
    }
    
    return 1 << (block_index - 1 + shift);    
}

u32 XarCapacity(Xar* xar)
{
    b32 is_malloced = xar->source_pool == NULL;
    if(is_malloced)
    {
        u32 capacity = XarGetBlockCapacity(xar, MAX_XAR_BLOCK_COUNT);
        return capacity;
    }
    else
    {
        u32 capacity = XarGetBlockCapacity(xar, xar->source_pool->level_count + 1);
        return capacity;
    }
}

// Allocate all the blocks required to make the item at index valid (except for block 0)
b32 XarTouch(Xar* xar, u32 index, u32 flags)
{
    u32 required_block = XarGetBlockIndex(xar, index);

    b32 is_malloced = xar->source_pool == NULL;
    
    // Note(Leo): +1 since the first two levels are compacted into 1;
    if(!is_malloced && required_block > (xar->source_pool->level_count + 1))
    {
        return 0;
    }
    
    // Note(Leo): We never allocate block 0 since it must be present when the XAR is created
    while(!xar->blocks[required_block] && required_block)
    {
        u32 capacity = XarGetBlockCapacity(xar, required_block);
        
        if(is_malloced)
        {
            xar->blocks[required_block] = malloc(capacity*xar->element_size);
            
            if(!(flags & no_zero_mem()))
            {                
                memset(xar->blocks[required_block], 0, capacity*xar->element_size);
            }
        }
        else
        {
            // Note(Leo): -1 since block 0 and 1 share an arena
            xar->blocks[required_block] = Alloc(&(xar->source_pool->levels[required_block - 1]), capacity*xar->element_size, flags);
        }
        
        if(!xar->blocks[required_block])
        {
            return 0;
        }
        
        --required_block;
    }

    xar->block_count = MAX(XarGetBlockIndex(xar, index) + 1, xar->block_count);

    return 1;
}


Xar CreateXar(XarPool* pool)
{
    assert(pool);
    Xar created = {};
    created.source_pool = pool;
    created.element_size = pool->element_size;
    created.shift = pool->shift;
    created.block_count = 1;
    
    // Note(Leo): First block needs to be created on initialization.
    u32 capacity = XarGetBlockCapacity(&created, 0);
    created.blocks[0] = Alloc(&(created.source_pool->levels[0]), capacity*created.element_size);
    
    return created;
}

Xar CreateMallocedXar(u8 shift, u32 element_size)
{
    Xar created = {};
    created.element_size = element_size;
    created.shift = shift;
    created.block_count = 1;
    
    // Note(Leo): First block needs to be created on initialization.
    u32 capacity = XarGetBlockCapacity(&created, 0);
    created.blocks[0] = malloc(capacity*created.element_size);
    
    if(created.blocks[0])
    {
        memset(created.blocks[0], 0, capacity*created.element_size);
    }
    
    return created;
}

b32 CreateXarPool(XarPool* target, ArrayView<u32> level_block_counts, u32 element_size, u8 shift)
{
    target->element_size = element_size;
    target->shift = shift;
    target->level_count = level_block_counts.length;
    
    for(u32 i = 0; i < level_block_counts.length; i++)
    {
        // Note(Leo): +1 since blocks 0 and 1 share a level
        u32 capacity = XarGetBlockCapacity(shift, i + 1);
        target->levels[i] = CreateArena(element_size*capacity*(level_block_counts.value[i]), element_size*capacity);
        if(!target->levels[i].mapped_address)
        {
            return 0;
        }
    }
    
    return 1;
}

void FreeXar(Xar* xar)
{
    for(int i = 0; xar->blocks[i]; i++)
    {
        if(xar->source_pool)
        {
            DeAlloc(&xar->source_pool->levels[i], xar->blocks[i]);
        }
        else
        {
            free(xar->blocks[i]);
        }
    }
}

#endif
