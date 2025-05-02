#include "platform.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits.h>

#include "harfbuzz/harfbuzz-10.1.0/src/hb.h"
#include "harfbuzz/harfbuzz-10.1.0/src/hb-ft.h"

#define DEFAULT_FACE_SIZE_PIXELS 250
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

struct FontPlatform
{
    FT_Library freetype;
    Arena* master_arena;
    
    Arena* loaded_fonts;
    Arena* font_binaries;
    Arena* cached_glyphs; // Glyphs that are currently on the GPU.
    
    std::map<std::string, loaded_font_handle*>* loaded_font_map;
    
    hb_buffer_t* shaping_buffer;
    
    int standard_glyph_size;
    int cache_slot_count;
};

FontPlatform font_platform;

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
    hb_buffer_pre_allocate(font_platform.shaping_buffer, Megabytes(1));
    
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

    #if FONT_PLATFORM_USE_SDF
    FT_Render_Glyph(slot, FT_RENDER_MODE_SDF);
    #else
    FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
    #endif
    
    FT_Bitmap glyph_bitmap = slot->bitmap;
    
    FontPlatformGlyph* added_glyph = (FontPlatformGlyph*)Alloc(font_platform.cached_glyphs, sizeof(FontPlatformGlyph));
    
    // Todo(Leo): Actually implement the least recently used cache eviction rather than just running out of space!
    // Check if weve run out of space
    assert(font_platform.cached_glyphs->next_address + sizeof(FontPlatformGlyph) < font_platform.cached_glyphs->next_address + font_platform.cached_glyphs->size);
    
    added_glyph->width = glyph_bitmap.width;
    added_glyph->height = glyph_bitmap.rows;
    
    added_glyph->bearing_x = slot->bitmap_left;
    added_glyph->bearing_y = slot->bitmap_top;
    
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
    if(search == font->glyph_cache_map->end())
    {
        return FontPlatformRasterizeGlyph(font_handle, glyph_index);
    }
    return search->second;
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
    
    int cursor_x = 0;
    int cursor_y = 0;    
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
        
        hb_buffer_reset(font_platform.shaping_buffer);
        hb_buffer_add_utf8(font_platform.shaping_buffer, utf8_buffer, buffer_length, 0, -1);
        hb_buffer_guess_segment_properties(font_platform.shaping_buffer);
            
        loaded_font_handle* used_font = platform_get_font(font_handle);
        if(!used_font)
        {
            assert(used_font);
            mark_end();
            return;
        }
        
        float font_scale = (float)font_size / (float)font_platform.standard_glyph_size;
        top_line_height = MAX(top_line_height, used_font->line_top_height * font_scale); // See if this font's height should be the current lines height
        lower_line_height = MAX(lower_line_height, used_font->line_bottom_height * font_scale);
        
        // Change font size to the desired size so that shaping will have the offsets already in the correct size
        FT_Set_Pixel_Sizes(used_font->face, font_size, font_size);
        hb_ft_font_changed(used_font->font);
        
        BEGIN_TIMED_BLOCK(HARFBUZZ);
        hb_shape(used_font->font, font_platform.shaping_buffer, shaping_features, sizeof(shaping_features) / sizeof(hb_feature_t));
        END_TIMED_BLOCK(HARFBUZZ);
        
        unsigned int glyph_count;
        hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(font_platform.shaping_buffer, &glyph_count);
        hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(font_platform.shaping_buffer, &glyph_count);
        
        result->glyph_count += glyph_count;
        
        FT_Set_Pixel_Sizes(used_font->face, font_platform.standard_glyph_size, font_platform.standard_glyph_size);
        hb_ft_font_changed(used_font->font);
        
        if(!glyph_info || !glyph_pos)
        {
            mark_end();
            return;
        }
        
        FontPlatformShapedGlyph* added_glyph = NULL;
    
        for(int i = 0; i < glyph_count; i++)
        {
            // Check linewrap
            // Todo(Leo): Implement word wrapping as an option
            if(wrapping_point && cursor_x + (glyph_pos[i].x_advance / 64) >= wrapping_point)
            {
                result->required_width = wrapping_point;
                
                // Go back and add line heights to all the glyphs
                FontPlatformShapedGlyph* curr_glyph = line_first;
                for(uint32_t j = 0; j < line_count; j++ )
                {
                    assert(curr_glyph);
                    curr_glyph->placement_offsets.y += top_line_height; 
                    
                    curr_glyph++;
                }
                
                result->required_height += top_line_height;
                result->required_height += lower_line_height;
                
                line_first = NULL;
                line_count = 0;
                
                cursor_x = 0;
                cursor_y += top_line_height;
                cursor_y += lower_line_height;
                top_line_height = 0.0f;
                lower_line_height = 0.0f;
            }
            
            added_glyph = (FontPlatformShapedGlyph*)Alloc(glyph_arena, sizeof(FontPlatformShapedGlyph), no_zero());
            
            if(!line_first)
            {
                line_first = added_glyph; 
            }
            
            line_count++;
            
            FontPlatformGlyph* added_glyph_raster_info = plaform_get_glyph_or_raster(font_handle, glyph_info[i].codepoint);
                     
    //        printf("Shaping char \"%d\"\n", glyph_info[i].codepoint);
            
            added_glyph->color = color;
            
            // Bearings are relative to the glyph's raster size which is different from the size of the font so scale it
            float scaled_bearing_x = (float)added_glyph_raster_info->bearing_x * font_scale;
            float scaled_bearing_y = (float)added_glyph_raster_info->bearing_y * font_scale;
            
            added_glyph->atlas_offsets = RenderPlatformGetGlyphPosition(GlyphSlot(added_glyph_raster_info));
            
            added_glyph->atlas_size = { (float)added_glyph_raster_info->width, (float)added_glyph_raster_info->height };
            
            // Note(Leo): The calculated coordinate is the top-left corner of the quad enclosing the char
            // Note(Leo): Vulkan has the y-axis upside down compared to cartesian coordinates which freetype uses, so Y gets more positive as we go down
            // Note(Leo): Divide by 64 to convert back to pixel measurements from harfbuzz
            added_glyph->placement_offsets.x = cursor_x + (glyph_pos[i].x_offset / 64) + scaled_bearing_x;
            added_glyph->placement_offsets.y = cursor_y - (glyph_pos[i].y_offset / 64 + scaled_bearing_y);
    
            added_glyph->placement_size.x = (float)added_glyph_raster_info->width * font_scale;
            added_glyph->placement_size.y = (float)added_glyph_raster_info->height * font_scale;
            
            cursor_x += glyph_pos[i].x_advance / 64;
            cursor_y += glyph_pos[i].y_advance / 64;
            
            result->required_width = MAX(result->required_width, cursor_x);
    //        printf("Cursor at (%d, %d)\n", cursor_x, cursor_y);
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
