#include "arena.h"
#include "prim_types.h"

#if !XAR_HEADER
#define XAR_HEADER 1
u64 MSB(u64 x);

#define MAX_XAR_BLOCK_COUNT 10

struct XarPool
{
    // Note(Leo): Smallest levels are 0 and 1 with sizes getting exponentially larger as you go up!
    Arena levels[MAX_XAR_BLOCK_COUNT - 1];
    u32 element_size;
    u8 level_count; // Number of levels this pool supports
    u8 shift; // Dictates the base amout of items in a level
};

struct Xar
{
    XarPool* source_pool;
    void* blocks[MAX_XAR_BLOCK_COUNT];
    u32 element_size; // Size of element in bytes
    u32 count;
    u8 shift;
    u8 block_count; // Number of blocks this xar has atm
};

void* XarGet(Xar* xar, u32 index);
u32 XarGetBlockIndex(Xar* xar, u32 index);

u32 XarGetBlockCapacity(Xar* xar, u32 block_index);
u32 XarGetBlockCapacity(u8 shift, u32 block_index);
u32 XarCapacity(Xar* xar);

b32 XarTouch(Xar* xar, u32 index, u32 flags = 0);
void* XarPush(Xar* xar, u32 count = 1, u32 flags = 0);

void XarPop(Xar* xar);

b32 CreateXarPool(XarPool* target, ArrayView<u32> level_block_counts, u32 element_size, u8 shift);

Xar CreateXar(XarPool* pool);
Xar CreateMallocedXar(u8 shift, u32 element_size);

void FreeXar(Xar* xar);
#endif
