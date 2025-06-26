#include "platform.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits.h>

#include "third_party/harfbuzz/harfbuzz-11.2.1/src/hb.h"
#include "third_party/harfbuzz/harfbuzz-11.2.1/src/hb-ft.h"

#include "third_party/meow_hash/meow_hash_x64_aesni.h"

#define DEFAULT_FACE_SIZE_PIXELS 150
#define CACHE_SIZE_GLYPHS 2000

struct loaded_font_handle
{
    hb_font_t* font;
    // Todo(Leo): This is probably really stupid so dont do it!
    // Note(Leo): Glyphs are stored in same sized large containers and sampled down in the shader
    std::map<uint32_t, FontPlatformGlyph*>* glyph_cache_map;
    float line_height; // Pixel height of this face
    float line_bottom_height;
    float line_top_height;
    
    FT_Face face;
};

struct cached_shaped_glyph
{
    union
    {
        struct
        {
            uint32_t next_glyph; 
            uint16_t buffer_index; // Index of this glyph run into the original buffer
            uint16_t run_length; // Length of this run (Several codepoints may be combined by harfbuzz)
        }; // For normal glyphs
        struct
        {
            uint32_t first_free; 
            uint32_t furthest_allocated;
        }; // For the master glyph
        struct
        {
            // Note(Leo): This should overlap with next_glyph for O(1) free-ing operation.
            uint32_t next_free;
        }; // For free-ed glyphs
    };

    uint32_t glyph_code; // ---> cached glyphs

    // Note(Leo): All dimensions here are scaled versions of those of fixed size glyphs to fit the requested font size
    vec2 placement_offsets;
    vec2 placement_advances;
    vec2 placement_size;
    
};

// A run of glyphs that we get back from harfbuzz. Unlike FontPlatformShapedText where we would have placed
// the glyphs in accordance with wrapping requirements.
struct cached_shaped_text_handle
{
    union
    {
        struct
        {
            uint32_t first_free;
        
            uint32_t most_ru;
            uint32_t least_ru;
            
            uint32_t furthest_allocated;
        }; // For the master handle
        struct
        {
            uint32_t next_with_same_hash; 
        
            uint32_t next_lru;
            uint32_t prev_lru;
            
            uint32_t hash;
        }; // For normal handles
        struct // For free-ed handles
        {
            uint32_t next_free;
        };
    };
    
    uint32_t first_glyph; // ---> cached glyph runs
    uint32_t last_glyph; // ---> cached glyph runs
    
    // Stuff for verifying this is our intended text and not a hash colision.
    FontHandle font;
    uint16_t buffer_length; // Length of the original buffer
    uint16_t font_size;
};

struct text_handle_table
{
    cached_shaped_glyph* cached_glyph_runs;
    cached_shaped_text_handle* cached_text_handles;
    uint32_t* hash_table;
    
    uint32_t text_handle_count;
    uint32_t glyph_run_count;
    uint32_t hash_count;
    
    uint32_t hash_mask; // Mask limiting the hash_table access range, equal to hash_table size.

};

struct rasterized_glyph_cache
{
    uint32_t most_ru;
    uint32_t least_ru;
};

struct FontPlatform
{
    FT_Library freetype;
    Arena* master_arena;
    text_handle_table* text_cache;
    
    Arena* loaded_fonts;
    Arena* font_binaries;
    Arena* cached_glyphs; // Glyphs that are currently on the GPU.
    
    std::map<std::string, loaded_font_handle*>* loaded_font_map;
    
    hb_buffer_t* shaping_buffer;
    
    int standard_glyph_size;
    int cache_slot_count;

    rasterized_glyph_cache rasterized_glyphs;
};

FontPlatform font_platform;

cached_shaped_text_handle* get_master_text_handle(text_handle_table* table)
{
    return table->cached_text_handles;
}

cached_shaped_glyph* get_master_shaped_glyph(text_handle_table* table)
{
    return table->cached_glyph_runs;
}

// hash_count should be a power of 2 for masking to work properly
uint64_t get_table_footprint(uint32_t hash_count, uint32_t text_handle_count, uint32_t glyph_run_count)
{
    uint64_t size = 0;
    size += sizeof(text_handle_table);
    // Note(Leo) +1 to leave alignment space
    size += (hash_count + 1) * sizeof(uint32_t); 
    size += (text_handle_count + 1) * sizeof(cached_shaped_text_handle);
    size += (glyph_run_count + 1) * sizeof(cached_shaped_glyph);
    return size;
}

text_handle_table* create_text_cache_table(uint32_t hash_count, uint32_t text_handle_count, uint32_t glyph_run_count)
{
    assert(hash_count && text_handle_count && glyph_run_count);
    
    // Note(Leo): From https://github.com/cmuratori/refterm/blob/main/refterm.h
    #define IsPowerOfTwo(Value) (((Value) & ((Value) - 1)) == 0)
    // Note(Leo): The hash count should be a power of two for its hash mask to work 
    assert(IsPowerOfTwo(hash_count));
    
    uint64_t table_size = get_table_footprint(hash_count, text_handle_count, glyph_run_count);
    text_handle_table* table = (text_handle_table*)malloc(table_size);
    
    if(!table)
    {
        return NULL;
    }
    
    table->hash_count = hash_count;
    table->hash_mask = hash_count - 1;
    
    table->text_handle_count = text_handle_count;
    table->glyph_run_count = glyph_run_count;
    
    void* cached_text = (void*)((uintptr_t)table + sizeof(text_handle_table)); 
    table->cached_text_handles = align_mem(cached_text, cached_shaped_text_handle);
    
    void* cached_glyphs = (void*)(table->cached_text_handles + text_handle_count);
    table->cached_glyph_runs = align_mem(cached_glyphs, cached_shaped_glyph);
    
    void* hash_table = (void*)(table->cached_glyph_runs + glyph_run_count);
    table->hash_table = align_mem(hash_table, uint32_t);
    
    memset(hash_table, 0, hash_count);
    
    // Note(Leo): The freelist is maintained using the first element of the array as a master. The master maintains the
    //            LRU first/last items aswell. The master also keeps track of the furthest weve allocated into the array
    cached_shaped_text_handle* master_text_handle = get_master_text_handle(table);
    *master_text_handle = {};
    master_text_handle->furthest_allocated++;
    
    cached_shaped_glyph* master_glyph_handle = get_master_shaped_glyph(table);
    *master_glyph_handle = {};
    master_glyph_handle->furthest_allocated++;
    
    return table;
}

cached_shaped_text_handle* get_cached_text_handle(text_handle_table* table, char* buffer, uint32_t buffer_len, FontHandle font, uint16_t font_size)
{
    BEGIN_TIMED_BLOCK(MEOW);
    meow_u128 buffer_hash = MeowHash(MeowDefaultSeed, buffer_len, buffer);
    END_TIMED_BLOCK(MEOW);
    uint32_t text_hash = MeowU32From(buffer_hash, 0);
    
    uint32_t lookup_index = table->hash_table[text_hash & table->hash_mask];
    if(!lookup_index)
    {
        return NULL;
    }
    
    cached_shaped_text_handle* found = &table->cached_text_handles[lookup_index];
    // Search until we find a match or run out of candidates
    while(found->font != font || found->buffer_length != static_cast<uint16_t>(buffer_len) || found->font_size != font_size)
    {
        // No more candidates
        if(!found->next_with_same_hash)
        {
            return NULL;
        }
        
        found = &table->cached_text_handles[found->next_with_same_hash];
        
    }

    // Update the lru order to reflect this handle being touched
    if(found->prev_lru)
    {
        cached_shaped_text_handle* prev = &table->cached_text_handles[found->prev_lru];
        prev->next_lru = found->next_lru;
    }
    
    if(found->next_lru)
    {
        cached_shaped_text_handle* next = &table->cached_text_handles[found->next_lru];
        next->prev_lru = found->prev_lru;
    }
    
    uint32_t found_index = index_of(found, table->cached_text_handles, cached_shaped_text_handle);
    
    cached_shaped_text_handle* master_text_handle = get_master_text_handle(table);
    if(master_text_handle->most_ru)
    {
        cached_shaped_text_handle* prev = &table->cached_text_handles[master_text_handle->most_ru];
        prev->prev_lru = found_index;
    }
    
    if(master_text_handle->least_ru == found_index)
    {
        master_text_handle->least_ru = found->prev_lru;
    }
    
    found->prev_lru = 0;
    found->next_lru = master_text_handle->most_ru;
    master_text_handle->most_ru = found_index;

    return found;
}

cached_shaped_text_handle* get_or_evict_text_handle(text_handle_table* table, bool alwaysEvict = false)
{
    cached_shaped_text_handle* master_text_handle = get_master_text_handle(table);
    cached_shaped_text_handle* used = NULL;
    
    // There is already a free handle to use
    if(master_text_handle->first_free && !alwaysEvict)
    {
        used = &table->cached_text_handles[master_text_handle->first_free];
        master_text_handle->first_free = used->next_free;
    }
    // There are unallocated handles left to use
    else if(master_text_handle->furthest_allocated < table->text_handle_count && !alwaysEvict)
    {
        used = &table->cached_text_handles[master_text_handle->furthest_allocated];
        master_text_handle->furthest_allocated++;
    }
    // Need to evict a handle
    else
    {
    
        used = &table->cached_text_handles[master_text_handle->least_ru];
        master_text_handle->least_ru = used->prev_lru;
        
        if(used->prev_lru)
        {
            cached_shaped_text_handle* prev = &table->cached_text_handles[used->prev_lru];
            prev->next_lru = 0;
        }
        
        // When evicting a handle we also need to free its glyphs
        cached_shaped_glyph* master_glyph = get_master_shaped_glyph(table);
        
        cached_shaped_glyph* free_end = &table->cached_glyph_runs[used->last_glyph];
        
        free_end->next_free = master_glyph->first_free;
        master_glyph->first_free = used->first_glyph;
        
        // Evict from the hash table aswell
        cached_shaped_text_handle* hash_sibling = &table->cached_text_handles[table->hash_table[used->hash & table->hash_mask]];
        
        // Evicted is somewhere in the linked list
        if(hash_sibling != used)
        {
            while(hash_sibling != used)
            {
                cached_shaped_text_handle* next = &table->cached_text_handles[hash_sibling->next_with_same_hash];
                if(next == used)
                {
                    hash_sibling->next_with_same_hash = used->next_with_same_hash;
                    break;
                }
                hash_sibling = next;
            }
        }
        else // Evicted is the first in the linked list
        {
            table->hash_table[used->hash & table->hash_mask] = used->next_with_same_hash;
        }
    }

    memset(used, 0, sizeof(cached_shaped_text_handle));
    return used;
}

cached_shaped_text_handle* insert_cached_text_handle(text_handle_table* table, char* buffer, uint32_t buffer_len, FontHandle font, uint16_t font_size)
{
    assert(table && buffer && buffer_len && font && font_size);
    
    BEGIN_TIMED_BLOCK(MEOW);
    meow_u128 buffer_hash = MeowHash(MeowDefaultSeed, buffer_len, buffer);
    END_TIMED_BLOCK(MEOW);
    uint32_t text_hash = MeowU32From(buffer_hash, 0);
    
    cached_shaped_text_handle* created = NULL;
    
    uint32_t lookup_index = table->hash_table[text_hash & table->hash_mask];
    if(!lookup_index)
    {
        created = get_or_evict_text_handle(table);
        table->hash_table[text_hash & table->hash_mask] = index_of(created, table->cached_text_handles, cached_shaped_text_handle);
    }
    else
    {
        created = get_or_evict_text_handle(table);
        cached_shaped_text_handle* hash_sibling = &table->cached_text_handles[lookup_index];
        created->next_with_same_hash = hash_sibling->next_with_same_hash;
        hash_sibling->next_with_same_hash = index_of(created, table->cached_text_handles, cached_shaped_text_handle);
    }
    
    assert(created);
    cached_shaped_text_handle* master_text_handle = get_master_text_handle(table);
    created->next_lru = master_text_handle->most_ru;
    if(created->next_lru)
    {
        cached_shaped_text_handle* old_most_ru = &table->cached_text_handles[created->next_lru];
        old_most_ru->prev_lru = index_of(created, table->cached_text_handles, cached_shaped_text_handle);
    }
    else
    {
        // If there was no recently used then this must be the first handle.
        assert(!master_text_handle->least_ru);
        master_text_handle->least_ru = index_of(created, table->cached_text_handles, cached_shaped_text_handle);
    }
    
    master_text_handle->most_ru = index_of(created, table->cached_text_handles, cached_shaped_text_handle);

    created->hash = text_hash;
    created->font = font;
    created->buffer_length = static_cast<uint16_t>(buffer_len);
    created->font_size = font_size;
    
    return created;
}

cached_shaped_glyph* insert_cached_shaped_glyph(text_handle_table* table, cached_shaped_text_handle* target)
{
    assert(table && target);
    cached_shaped_glyph* master_glyph = get_master_shaped_glyph(table);
    cached_shaped_glyph* created = NULL;
    
    // There is already a free slot to use
    if(master_glyph->first_free)
    {
        created = &table->cached_glyph_runs[master_glyph->first_free];
        master_glyph->first_free = created->next_free;
    }
    // There is unused space left to use
    else if(master_glyph->furthest_allocated < table->glyph_run_count)
    {
        created = &table->cached_glyph_runs[master_glyph->furthest_allocated];
        master_glyph->furthest_allocated++;
    }
    // Need to evict a text_handle to create space
    else
    {
        // Evict a handle
        cached_shaped_text_handle* freed = get_or_evict_text_handle(table, true);
        cached_shaped_text_handle* master_text_handle = get_master_text_handle(table);
        freed->next_free = master_text_handle->first_free;
        master_text_handle->first_free = index_of(freed, table->cached_text_handles, cached_shaped_text_handle);
        
        // There should now be a free glyph
        assert(freed && master_glyph->first_free);
        created = &table->cached_glyph_runs[master_glyph->first_free];
        master_glyph->first_free = created->next_free;
    }
    memset(created, 0, sizeof(cached_shaped_glyph));
    
    if(!target->first_glyph)
    {
        target->first_glyph = index_of(created, table->cached_glyph_runs, cached_shaped_glyph);
        target->last_glyph = target->first_glyph;
    }
    else
    {
        cached_shaped_glyph* last_glyph = &table->cached_glyph_runs[target->last_glyph];
        last_glyph->next_glyph = index_of(created, table->cached_glyph_runs, cached_shaped_glyph);
        target->last_glyph = last_glyph->next_glyph;
    }
    
    return created;
}

#define GlyphSlot(glyph_ptr) (((uintptr_t)glyph_ptr - font_platform.cached_glyphs->mapped_address) / sizeof(FontPlatformGlyph)) 

int InitializeFontPlatform(Arena* master_arena, int standard_glyph_size)
{
    font_platform = {};
    font_platform.master_arena = (Arena*)Alloc(master_arena, sizeof(Arena), zero());
    *(font_platform.master_arena) = CreateArena(100*sizeof(Arena), sizeof(Arena));
    
    font_platform.loaded_fonts = (Arena*)Alloc(font_platform.master_arena, sizeof(Arena), zero());
    *(font_platform.loaded_fonts) = CreateArena(200*sizeof(loaded_font_handle), sizeof(loaded_font_handle));
    
    font_platform.font_binaries = (Arena*)Alloc(font_platform.master_arena, sizeof(Arena), zero());
    *(font_platform.font_binaries) = CreateArena(Megabytes(20), sizeof(char));
    
    font_platform.standard_glyph_size = DEFAULT_FACE_SIZE_PIXELS;
    if(standard_glyph_size)
    {
        font_platform.standard_glyph_size = standard_glyph_size;
    }
    
    font_platform.cached_glyphs = (Arena*)Alloc(font_platform.master_arena, sizeof(Arena), zero());
    *(font_platform.cached_glyphs) = CreateArena(sizeof(FontPlatformGlyph) * CACHE_SIZE_GLYPHS, sizeof(FontPlatformGlyph));
    font_platform.cache_slot_count = CACHE_SIZE_GLYPHS;
    
    int error = FT_Init_FreeType(&(font_platform.freetype));
    if(error)
    {
        printf("Failed to initialize freetype!\n");
        return 1;
    }
    
    font_platform.loaded_font_map = new std::map<std::string, loaded_font_handle*>;
    
    font_platform.shaping_buffer = hb_buffer_create();
    //hb_buffer_pre_allocate(font_platform.shaping_buffer, Megabytes(1));

    // Todo(Leo): Tune these values
    font_platform.text_cache = create_text_cache_table(0x1000, 400, 5000);
    
    return 0;
}

// Note(Leo): This is here since the GPU is very likely going to over allocate its glyph cache so we should use that space
void FontPlatformUpdateCache(int new_size_glyphs)
{
    if(font_platform.cache_slot_count == new_size_glyphs)
    {
        return;
    }
    
    // Inform all fonts that cached glyphs have been destroyed
    for(auto font : *(font_platform.loaded_font_map))
    {
        font.second->glyph_cache_map->clear();
    }
    
    FreeArena(font_platform.cached_glyphs);
    *(font_platform.cached_glyphs) = CreateArena(sizeof(FontPlatformGlyph) * new_size_glyphs, sizeof(FontPlatformGlyph));
    font_platform.cache_slot_count = new_size_glyphs;
}

int FontPlatformGetGlyphSize()
{
    return font_platform.standard_glyph_size;
}

inline loaded_font_handle* platform_get_font(FontHandle handle)
{
    assert(handle > 0);
    return ((loaded_font_handle*)(font_platform.loaded_fonts->mapped_address)) + (handle - 1);
}

void load_face_from_mem(const char* font_name, void* font_binary, uint64_t binary_length)
{
    loaded_font_handle* created_font = (loaded_font_handle*)Alloc(font_platform.loaded_fonts, sizeof(loaded_font_handle));
    
    int error;
    error = FT_New_Memory_Face(font_platform.freetype, (FT_Byte*)font_binary, binary_length, 0, &(created_font->face));
    
    if(error)
    {
        printf("Failed to create font face!\n");
    }
    
    error = FT_Set_Pixel_Sizes(created_font->face, font_platform.standard_glyph_size, font_platform.standard_glyph_size);

    if(error)
    {
        printf("Failed to set size of font face!\n");
    }
    
    created_font->line_height = (float)created_font->face->size->metrics.height / 64.0f;
    created_font->line_top_height = (float)created_font->face->size->metrics.ascender / 64.0f;
    created_font->line_bottom_height = ((float)created_font->face->size->metrics.descender / 64.0f) * -1.0f; // Descender is negative
    
    created_font->font = hb_ft_font_create_referenced(created_font->face);
    
    created_font->glyph_cache_map = new std::map<uint32_t, FontPlatformGlyph*>;

    font_platform.loaded_font_map->insert({ font_name, created_font });
}

void FontPlatformLoadFace(const char* font_name, FILE* font_file)
{
    assert(font_name && font_file);
    if(!font_name || !font_file)
    {
        return;
    }
    
    fseek(font_file, 0, SEEK_END);    
    int binary_length = ftell(font_file);
    rewind(font_file);
    void* font_binary = Alloc(font_platform.font_binaries, binary_length * sizeof(char));
    fread(font_binary, binary_length, 1, font_file);
    load_face_from_mem(font_name, font_binary, static_cast<uint64_t>(binary_length));
}

void FontPlatformLoadFace(const char* font_name, PlatformFile* font_file)
{
    void* font_binary = Alloc(font_platform.font_binaries, font_file->len);
    memcpy(font_binary, font_file->data, static_cast<size_t>(font_file->len));
    
    load_face_from_mem(font_name, font_binary, font_file->len);
}


// Note(Leo): Handle relies on fonts being allocated in an arena since the handle is their index
// Note(Leo): Handle is index + 1 so that 0 can be "not found" and 1 is the first index.
FontHandle FontPlatformGetFont(const char* font_name)
{
    auto search = font_platform.loaded_font_map->find(font_name);
    
    if(search == font_platform.loaded_font_map->end())
    {
        return 0;
    }
    
    int found_index = offset_of(search->second, font_platform.loaded_fonts->mapped_address) / sizeof(loaded_font_handle);
    return found_index + 1;
}



FontPlatformGlyph* FontPlatformRasterizeGlyph(FontHandle font_handle, uint32_t glyph_index)
{
    loaded_font_handle* font = platform_get_font(font_handle);
    
    FT_Int32 flags = FT_LOAD_DEFAULT;
    
    FT_Load_Glyph(font->face, glyph_index, flags);
    
    FT_GlyphSlot slot = font->face->glyph;

    FT_Render_Glyph(slot, FT_RENDER_MODE_SDF);
    
    FT_Bitmap glyph_bitmap = slot->bitmap;
    
    
    // Todo(Leo): Actually implement the least recently used cache eviction rather than just running out of space!
    // Check if weve run out of space
    //assert(font_platform.cached_glyphs->next_address + sizeof(FontPlatformGlyph) < font_platform.cached_glyphs->mapped_address + font_platform.cached_glyphs->size);
    FontPlatformGlyph* added_glyph = NULL;
    
    // Use free slot
    if(font_platform.cached_glyphs->first_free.next_free || (font_platform.cached_glyphs->next_address <
         (font_platform.cached_glyphs->mapped_address + font_platform.cached_glyphs->size)))
    {
        added_glyph = (FontPlatformGlyph*)Alloc(font_platform.cached_glyphs, sizeof(FontPlatformGlyph), zero());
    }
    // Evict a glyph
    else
    {
        FontPlatformGlyph* base = (FontPlatformGlyph*)font_platform.cached_glyphs->mapped_address;
        added_glyph = &base[font_platform.rasterized_glyphs.least_ru];
        
        if(added_glyph->prev_lru)
        {
            FontPlatformGlyph* prev = &base[added_glyph->prev_lru]; 
            prev->next_lru = 0;
            font_platform.rasterized_glyphs.least_ru = added_glyph->prev_lru;
        }
        
        // Remove glyph from its font map
        loaded_font_handle* notified_font = platform_get_font(added_glyph->font);
        if(notified_font)
        {
            notified_font->glyph_cache_map->erase(added_glyph->codepoint);
        }
        
        memset(added_glyph, 0, sizeof(FontPlatformGlyph));
    }
    
    added_glyph->width = glyph_bitmap.width;
    added_glyph->height = glyph_bitmap.rows;
    
    added_glyph->bearing_x = slot->bitmap_left;
    added_glyph->bearing_y = slot->bitmap_top;
    
    // Font so we know who to notify if this glyph is evicted
    added_glyph->font = font_handle;
    added_glyph->codepoint = glyph_index;
    
    font->glyph_cache_map->insert({glyph_index, added_glyph});
    
    // Dont upload glyphs with no size
    if(added_glyph->width && added_glyph->height)
    {
        RenderplatformUploadGlyph(glyph_bitmap.buffer, glyph_bitmap.width, glyph_bitmap.rows, GlyphSlot(added_glyph));    
    }
    
    #if 0
    for(int iy = 0; iy < glyph_bitmap.rows; iy++)
    {
        for(int ix = 0; ix < glyph_bitmap.width; ix++)
        {
            int c = (int) glyph_bitmap.buffer[iy * glyph_bitmap.width + ix];
            std::cout << (c == 255 ? '#' : '`');
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
    #endif
    
    return added_glyph;
}

// Get the given glyph from the given font out of the glyph cache or rasterize it if its not found
inline FontPlatformGlyph* plaform_get_glyph_or_raster(FontHandle font_handle, uint32_t glyph_index)
{
    loaded_font_handle* font = platform_get_font(font_handle);
    auto search = font->glyph_cache_map->find(glyph_index);
    
    FontPlatformGlyph* found = NULL;
    if(search == font->glyph_cache_map->end())
    {
        found = FontPlatformRasterizeGlyph(font_handle, glyph_index);
    }
    else
    {
        found = search->second;
    }
    
    if(!found)
    {
        return NULL;
    }
    
    FontPlatformGlyph* base = (FontPlatformGlyph*)font_platform.cached_glyphs->mapped_address;
    
    if(found->prev_lru)
    {
        FontPlatformGlyph* prev = &base[found->prev_lru];
        prev->next_lru = found->next_lru;
    }
    
    if(found->next_lru)
    {
        FontPlatformGlyph* next = &base[found->next_lru];
        next->prev_lru = found->prev_lru;
    }
    
    uint32_t found_index = index_of(found, base, FontPlatformGlyph);
    
    if(font_platform.rasterized_glyphs.most_ru)
    {
        FontPlatformGlyph* prev = &base[font_platform.rasterized_glyphs.most_ru];
        prev->prev_lru = found_index;
    }
    
    if(font_platform.rasterized_glyphs.least_ru == found_index)
    {
        font_platform.rasterized_glyphs.least_ru = found->prev_lru;
    }
    
    found->prev_lru = 0;
    found->next_lru = font_platform.rasterized_glyphs.most_ru;
    font_platform.rasterized_glyphs.most_ru = found_index;
    
    return found;
}

const hb_feature_t shaping_features[] = { { HB_TAG('k', 'e', 'r', 'n'), 1, 0, UINT_MAX }, { HB_TAG('l', 'i', 'g', 'a'), 1, 0, UINT_MAX }, { HB_TAG('c', 'l', 'i', 'g'), 1, 0, UINT_MAX } };


// Note(Leo): Area height and width are expected in pixels
void FontPlatformShapeMixed(Arena* glyph_arena, FontPlatformShapedText* result, StringView* utf8_strings, FontHandle* font_handles, uint16_t* font_sizes, StyleColor* colors, int text_block_count, uint32_t wrapping_point)
{
    // Used to mark the end of our sequence of glyphs
    #define mark_end() Alloc(glyph_arena, sizeof(FontPlatformShapedGlyph), zero())
    assert(result);
    assert(glyph_arena);
    
    // Note(Leo): glyph_arena may be unaligned (if other types have been getting allocated) 
    //            so ensure its aligned to what we want.
    glyph_arena->next_address = (uintptr_t)align_mem(glyph_arena->next_address, FontPlatformShapedGlyph);

    result->first_glyph = (FontPlatformShapedGlyph*)glyph_arena->next_address;
    
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    
    result->required_width = 0;
    result->required_height = 0;
    result->glyph_count = 0;
    
    float top_line_height = 0.0f;
    float lower_line_height = 0.0f;
    
    FontPlatformShapedGlyph* line_first = NULL;
    uint32_t line_count = 0;
    
    for(int i = 0; i < text_block_count; i++)
    {
        char* utf8_buffer = utf8_strings[i].value;
        uint32_t buffer_length = utf8_strings[i].len;
        FontHandle font_handle = font_handles[i];
        uint16_t font_size = font_sizes[i];
        StyleColor color = colors[i];
        loaded_font_handle* used_font = platform_get_font(font_handle);

        assert(used_font);
        if(!used_font)
        {
            mark_end();
            return;
        }
        
        cached_shaped_text_handle* cached_glyphs = get_cached_text_handle(font_platform.text_cache, utf8_buffer, buffer_length, font_handle, font_size);
        
        if(!buffer_length)
        {
            continue;
        }
        
        float font_scale = (float)font_size / (float)font_platform.standard_glyph_size;
        top_line_height = MAX(top_line_height, used_font->line_top_height * font_scale); // See if this font's height should be the current lines height
        lower_line_height = MAX(lower_line_height, used_font->line_bottom_height * font_scale);
        
        if(cached_glyphs)
        {
            goto shape_glyphs;
        }
        {
        
        cached_glyphs = insert_cached_text_handle(font_platform.text_cache, utf8_buffer, buffer_length, font_handle, font_size);
        hb_buffer_reset(font_platform.shaping_buffer);
        hb_buffer_add_utf8(font_platform.shaping_buffer, utf8_buffer, buffer_length, 0, -1);
        hb_buffer_guess_segment_properties(font_platform.shaping_buffer);
        
        // Change font size to the desired size so that shaping will have the offsets already in the correct size
        FT_Set_Pixel_Sizes(used_font->face, font_size, font_size);
        hb_ft_font_changed(used_font->font);
        
        BEGIN_TIMED_BLOCK(HARFBUZZ);
        hb_shape(used_font->font, font_platform.shaping_buffer, shaping_features, sizeof(shaping_features) / sizeof(hb_feature_t));
        END_TIMED_BLOCK(HARFBUZZ);
        
        unsigned int glyph_count;
        hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(font_platform.shaping_buffer, &glyph_count);
        hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(font_platform.shaping_buffer, &glyph_count);
        
        //result->glyph_count += glyph_count;
        
        FT_Set_Pixel_Sizes(used_font->face, font_platform.standard_glyph_size, font_platform.standard_glyph_size);
        hb_ft_font_changed(used_font->font);
        
        if(!glyph_info || !glyph_pos)
        {
            mark_end();
            return;
        }
        
        for(int j = 0; j < glyph_count; j++)
        {
            cached_shaped_glyph* added_glyph = insert_cached_shaped_glyph(font_platform.text_cache, cached_glyphs);
            added_glyph->buffer_index = glyph_info[j].cluster;
            
            if((uint32_t)j + 1 < glyph_count)
            {
                added_glyph->run_length = glyph_info[j + 1].cluster - glyph_info[j].cluster;
            }
            else
            {
                added_glyph->run_length = buffer_length - glyph_info[j].cluster;
            }
            
            added_glyph->glyph_code = glyph_info[j].codepoint;
            
            // Dont add any sizing for newlines.
            if(utf8_buffer[added_glyph->buffer_index] == '\n')
            {
                continue;
            }
            
            FontPlatformGlyph* added_glyph_raster_info = plaform_get_glyph_or_raster(font_handle, glyph_info[j].codepoint);
            // Bearings are relative to the glyph's raster size which is different from the size of the font so scale it
            float scaled_bearing_x = (float)added_glyph_raster_info->bearing_x * font_scale;
            float scaled_bearing_y = (float)added_glyph_raster_info->bearing_y * font_scale;
            
            // Note(Leo): The calculated coordinate is the top-left corner of the quad enclosing the char
            // Note(Leo): Vulkan has the y-axis upside down compared to cartesian coordinates which freetype uses, so Y gets more positive as we go down
            // Note(Leo): Divide by 64 to convert back to pixel measurements from harfbuzz
            added_glyph->placement_offsets.x = (glyph_pos[j].x_offset / 64) + scaled_bearing_x;
            added_glyph->placement_offsets.y = -(glyph_pos[j].y_offset / 64 + scaled_bearing_y);
    
            added_glyph->placement_size.x = (float)added_glyph_raster_info->width * font_scale;
            added_glyph->placement_size.y = (float)added_glyph_raster_info->height * font_scale;
            
            added_glyph->placement_advances.x = glyph_pos[j].x_advance / 64;
            added_glyph->placement_advances.y = glyph_pos[j].y_advance / 64;
        
        }
        
        }
        shape_glyphs:
        
        cached_shaped_glyph* curr_cached_glyph = &font_platform.text_cache->cached_glyph_runs[cached_glyphs->first_glyph];
        
        FontPlatformShapedGlyph* added_glyph = NULL;
    
        while(curr_cached_glyph)
        {
            result->glyph_count++;
        
            // Check linewrap
            // Todo(Leo): Implement word wrapping as an option
            bool auto_wrap = wrapping_point && (curr_cached_glyph->placement_offsets.x + curr_cached_glyph->placement_size.x + cursor_x) >= wrapping_point; 
            bool manual_wrap = utf8_buffer[curr_cached_glyph->buffer_index] == '\n';
            if(auto_wrap || manual_wrap)
            {
                if(auto_wrap)
                {
                    result->required_width = wrapping_point;
                }
                
                // Go back and add line heights to all the glyphs
                FontPlatformShapedGlyph* curr_glyph = line_first;
                for(uint32_t j = 0; j < line_count; j++)
                {
                    assert(curr_glyph);
                    curr_glyph->placement_offsets.y += top_line_height; 
                    
                    curr_glyph++;
                }
                
                result->required_height += top_line_height;
                result->required_height += lower_line_height;
                
                line_first = NULL;
                line_count = 0;
                
                cursor_y += top_line_height;
                //cursor_y += lower_line_height;
                cursor_x = 0.0f;
            }
            
            added_glyph = (FontPlatformShapedGlyph*)Alloc(glyph_arena, sizeof(FontPlatformShapedGlyph), no_zero());
            
            added_glyph->buffer_index = curr_cached_glyph->buffer_index;
            
            added_glyph->run_length = curr_cached_glyph->run_length;
            
            if(!line_first)
            {
                line_first = added_glyph; 
            }
            
            line_count++;
            
            FontPlatformGlyph* added_glyph_raster_info = plaform_get_glyph_or_raster(font_handle, curr_cached_glyph->glyph_code);
            
            added_glyph->color = color;
            
            added_glyph->atlas_offsets = RenderPlatformGetGlyphPosition(GlyphSlot(added_glyph_raster_info));
            
            added_glyph->atlas_size = { (float)added_glyph_raster_info->width, (float)added_glyph_raster_info->height };
            
            added_glyph->placement_offsets.x = curr_cached_glyph->placement_offsets.x + cursor_x;
            
            added_glyph->placement_offsets.y = curr_cached_glyph->placement_offsets.y + cursor_y;
            added_glyph->base_line = cursor_y + top_line_height;
    
            added_glyph->placement_size.x = curr_cached_glyph->placement_size.x;
            added_glyph->placement_size.y = curr_cached_glyph->placement_size.y;
            
            result->required_width = MAX(result->required_width, added_glyph->placement_offsets.x + added_glyph->placement_size.x);
            
            cursor_x += curr_cached_glyph->placement_advances.x;
            cursor_y += curr_cached_glyph->placement_advances.y;
            
            if(!curr_cached_glyph->next_glyph)
            {
                curr_cached_glyph = NULL;    
            }
            else
            {
                curr_cached_glyph = &font_platform.text_cache->cached_glyph_runs[curr_cached_glyph->next_glyph];
            }
        }
        
         
    }
    
    // Add line height to last line
    FontPlatformShapedGlyph* curr_glyph = line_first;
    for(uint32_t j = 0; j < line_count; j++ )
    {
        assert(curr_glyph);
        curr_glyph->placement_offsets.y += top_line_height; 
        
        curr_glyph++;
    }
    
    result->required_height += top_line_height;
    result->required_height += lower_line_height;
    
    mark_end();
    return;
}
