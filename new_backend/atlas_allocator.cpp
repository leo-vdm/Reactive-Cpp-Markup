#if !ATLAS_ALLOCATOR_IMPLEMENTATION
#define ATLAS_ALLOCATOR_IMPLEMENTATION 1

struct AtlasMasterBlock
{
    Xar children;
    u16 first_free;
    u16 free_count;
    u8 children_across; // How many chilren are along each axis
};

struct AtlasValueNode
{
    union
    {
        u32 glyph_id;
        
        u32 next_free;  
    };
};

struct AtlasAllocator
{
    Arena master_blocks;
    XarPool value_nodes;
    u32 block_size; // Size of block in px.
    u32 atlas_size; // All units are square
    u8 atlas_depth; // 3d atlasses are allowed
    u8 max_children_across;
};

typedef u64 AtlasNodeHandle;

struct AtlasInsertResult
{
    uvec2 offsets;
    u8 depth;
    AtlasNodeHandle handle; // Two packed u32s giving the index into the master nodes then into the children to
};                          // get the value. Both have 1 added to distinguish a null handle from a valid one

AtlasNodeHandle PackAtlasHandle(u32 master_node_index, u32 value_node_index)
{
    return (((u64)master_node_index + 1) << 32) | (value_node_index + 1);
}

AtlasAllocator CreateAtlasAllocator(u32 atlas_size, u32 block_size, u8 atlas_depth, u8 max_children_across)
{
    AtlasAllocator created = {};
    
    created.atlas_size = atlas_size;
    created.block_size = block_size;
    created.atlas_depth = atlas_depth;
    created.max_children_across = max_children_across;
    
    u32 max_master_blocks = (atlas_size/block_size)*(atlas_size/block_size)*atlas_depth;
    created.master_blocks = CreateArena(sizeof(AtlasMasterBlock)*max_master_blocks, sizeof(AtlasMasterBlock));
    
    u32 level_counts[10] = {max_master_blocks, max_master_blocks, max_master_blocks >> 1, max_master_blocks >> 4, 5, 5};
    if(!CreateXarPool(&created.value_nodes, {level_counts, 6}, sizeof(AtlasValueNode), 8))
    {
        return {};
    }
    
    return created;
}

b32 IsValid(AtlasMasterBlock* block)
{
    if(!block || !block->children_across)
    {
        return 0;
    }

    return 1;
}

AtlasInsertResult InsertValue(AtlasAllocator* allocator, u32 largest_dimension, u32 glyph_id)
{
    AtlasInsertResult result = {};
    
    u32 required_divisions = MIN(allocator->block_size / largest_dimension, allocator->max_children_across);
    
    // Note(Leo): +size to step over stub item
    AtlasMasterBlock* blocks = (AtlasMasterBlock*)(allocator->master_blocks.mapped_address + sizeof(AtlasMasterBlock));
    // Note(Leo): Next address is already 1 over the highest index so we dont need to do +1 here to convert from index -> count
    u32 block_count = IndexOf(allocator->master_blocks.next_address, allocator->master_blocks.mapped_address, AtlasMasterBlock);
    
    AtlasMasterBlock* chosen_block = NULL;
    
    for(u32 block_i = 0; block_i < block_count; block_i++)
    {
        AtlasMasterBlock* curr = blocks + block_i;
        if(!IsValid(curr))
        {
            continue;
        }
        
        // Found a correctly sized master node with space
        if(curr->children_across == required_divisions &&
            (curr->children.count < (curr->children_across*curr->children_across) || curr->first_free)
        )
        {
            chosen_block = curr;
            break;
        }
    }
    
    if(!chosen_block)
    {
        chosen_block = ArenaPush(&allocator->master_blocks, AtlasMasterBlock);
        // Note(Leo): We got NULL or the stub item, return a zeroed handle so the user knows
        //            to free in order to make space
        if(!chosen_block || allocator->master_blocks.mapped_address == (uptr)chosen_block)
        {
            goto ret;
        }
        
        chosen_block->children = CreateXar(&allocator->value_nodes);
        chosen_block->children_across = required_divisions;
    }
    
    u32 local_index = 0;
    if(chosen_block->first_free)
    {
        AtlasValueNode* used = (AtlasValueNode*)XarGet(&chosen_block->children, chosen_block->first_free);
        local_index = chosen_block->first_free;
        chosen_block->first_free = used->next_free;
        --chosen_block->free_count;
        
        used->glyph_id = glyph_id;
    }
    else
    {
        AtlasValueNode* added = (AtlasValueNode*)XarPush(&chosen_block->children);
        local_index = chosen_block->children.count - 1;
        added->glyph_id = glyph_id;
    }

    u32 block_index = IndexOf(chosen_block, blocks, AtlasMasterBlock);
    result.handle = PackAtlasHandle(block_index, local_index);
    
    // Calculate offsets to the master block
    u32 blocks_per_row = allocator->atlas_size / allocator->block_size;
    u32 blocks_per_level = blocks_per_row * blocks_per_row; // Note(Leo): Atlas is Square 

    result.depth = (u8)(block_index / blocks_per_level);
    
    block_index = block_index % blocks_per_level;
    result.offsets.y = (block_index / blocks_per_row)*allocator->block_size;
    result.offsets.x = (block_index % blocks_per_row)*allocator->block_size;
    
    u32 locals_per_row = chosen_block->children_across;
    u32 local_size = allocator->block_size / locals_per_row;
    result.offsets.y += (local_index / locals_per_row) * local_size;
    result.offsets.x += (local_index % locals_per_row) * local_size;
    
    ret:
    return result;
}

// Returns true if a master block gets freed, false otherwise 
b32 FreeValue(AtlasAllocator* allocator, AtlasNodeHandle target)
{
    u32 master_node_index = target >> 32;
    u32 value_node_index = (u32)target;

    // Invalid handle
    if(!master_node_index || !value_node_index)
    {
        return 0;
    }

    master_node_index -= 1;
    value_node_index -= 1;

    AtlasMasterBlock* blocks = (AtlasMasterBlock*)(allocator->master_blocks.mapped_address + sizeof(AtlasMasterBlock));
    AtlasMasterBlock* target_block = &blocks[master_node_index];
    
    AtlasValueNode* target_node = (AtlasValueNode*)XarGet(&target_block->children, value_node_index);
    target_node->next_free = target_block->first_free;
    target_block->first_free = value_node_index;
    ++target_block->free_count;
    
    // See if we can free this master block
    // Note(Leo): This is a pretty aggressive strategy to try and help with fragmentation
    if(target_block->free_count == target_block->children.count)
    {
        FreeXar(&target_block->children);
        DeAlloc(&allocator->master_blocks, target_block);
        return 1;
    }
    
    return 0;
}
#endif
