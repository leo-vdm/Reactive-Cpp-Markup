#include "platform.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits.h>

#include "harfbuzz/harfbuzz-10.1.0/src/hb.h"
#include "harfbuzz/harfbuzz-10.1.0/src/hb-ft.h"

#define DEFAULT_FACE_SIZE_PIXELS 250
#define FACE_CACHE_SIZE_GLYPHS 100

struct loaded_font_handle
{
    FT_Face face;
    hb_font_t* font;
    Arena* glyph_cache;
    // Todo(Leo): This is probably really stupid so dont do it!
    // Note(Leo): Glyphs are stored in same sized large containers and sampled down in the shader
    std::map<uint32_t, FontPlatformGlyph*>* glyph_cache_map;
};

struct FontPlatform
{
    FT_Library freetype;
    Arena* master_arena;
    
    Arena* loaded_fonts;
    Arena* font_binaries;
    
    std::map<std::string, loaded_font_handle*>* loaded_font_map;
    
    hb_buffer_t* shaping_buffer;
    
    int standard_glyph_size;
};

FontPlatform font_platform;

// Note(Leo): Since a rasterised glyph consists of the struct with a defined size and the blob of glyph data afterwards
// this is required to get the true size
// Note(Leo): +1 char in size to account for alignment requirements of the byte blob
#define GlyphSizeBytes() ((font_platform.standard_glyph_size * font_platform.standard_glyph_size * sizeof(char)) + sizeof(char) + sizeof(FontPlatformGlyph))

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
    
    created_font->font = hb_ft_font_create_referenced(created_font->face);
    
    created_font->glyph_cache_map = new std::map<uint32_t, FontPlatformGlyph*>;
    
    created_font->glyph_cache = (Arena*)Alloc(font_platform.master_arena, sizeof(Arena), zero());
    
    *(created_font->glyph_cache) = CreateArena(FACE_CACHE_SIZE_GLYPHS * GlyphSizeBytes(), GlyphSizeBytes());
    
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
    FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
    
    FT_Bitmap glyph_bitmap = slot->bitmap;
    
    FontPlatformGlyph* added_glyph = (FontPlatformGlyph*)Alloc(font->glyph_cache, GlyphSizeBytes());
    
    // Todo(Leo): Actually implement the least recently used cache eviction rather than just running out of space!
    // Check if weve run out of space
    assert(font->glyph_cache->next_address + GlyphSizeBytes() < font->glyph_cache->next_address + font->glyph_cache->size);
    
    char* glyph_data = align_mem((added_glyph + 1), char);
    
    added_glyph->width = glyph_bitmap.width;
    added_glyph->height = glyph_bitmap.rows;
    
    added_glyph->bearing_x = slot->bitmap_left;
    added_glyph->bearing_y = slot->bitmap_top;
    
    added_glyph->glyph_code = glyph_index;
    
    memcpy(glyph_data, glyph_bitmap.buffer, added_glyph->width * added_glyph->height);
    
    font->glyph_cache_map->insert({glyph_index, added_glyph});
    
    #if 1
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
void FontPlatformShape(Arena* glyph_arena, const char* utf8_buffer, FontHandle font_handle, int font_size, int area_width, int area_height)
{
    // Used to mark the end of our sequence of glyphs
    #define mark_end() Alloc(glyph_arena, sizeof(FontPlatformShapedGlyph), zero())
    assert(glyph_arena);
    // Note(Leo): glyph_arena MUST be empty
    assert(glyph_arena->mapped_address == glyph_arena->next_address);
    
    hb_buffer_reset(font_platform.shaping_buffer);
    hb_buffer_add_utf8(font_platform.shaping_buffer, utf8_buffer, -1, 0, -1);
    hb_buffer_guess_segment_properties(font_platform.shaping_buffer);
        
    loaded_font_handle* used_font = platform_get_font(font_handle);
    if(!used_font)
    {
        assert(used_font);
        mark_end();
        return;
    }
    
    // Change font size to the desired size so that shaping will have the offsets already in the correct size
    FT_Set_Pixel_Sizes(used_font->face, font_size, font_size);
    hb_ft_font_changed(used_font->font);

    
    
    hb_shape(used_font->font, font_platform.shaping_buffer, shaping_features, sizeof(shaping_features) / sizeof(hb_feature_t));
    
    unsigned int glyph_count;
    hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(font_platform.shaping_buffer, &glyph_count);
    hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(font_platform.shaping_buffer, &glyph_count);
    if(!glyph_info || !glyph_pos)
    {
        FT_Set_Pixel_Sizes(used_font->face, font_platform.standard_glyph_size, font_platform.standard_glyph_size);
        hb_ft_font_changed(used_font->font);
        mark_end();
        return;
    }
    
    FontPlatformShapedGlyph* added_glyph;
    
    int cursor_x = 0;
    int cursor_y = 0;
    
    for(int i = 0; i < glyph_count; i++)
    {
        // Check linewrap
        if(cursor_x + (glyph_pos[i].x_advance / 64) >= area_width)
        {
            cursor_x = 0;
            cursor_y += font_size;
        }
    
        added_glyph = (FontPlatformShapedGlyph*)Alloc(glyph_arena, sizeof(FontPlatformShapedGlyph), no_zero());
        
        // Note(Leo): This might be bad to do since if a glyph gets evicted between here and the GPU upload we will raster it twice
        // but its convienient to have the info here and that should be rare in most cases
        FontPlatformGlyph* added_glyph_raster_info = plaform_get_glyph_or_raster(font_handle, glyph_info[i].codepoint);
        
        added_glyph->glyph_code = glyph_info[i].codepoint;
        
        printf("Shaping char \"%d\"\n", glyph_info[i].codepoint);
        
        // Bearings are relative to the glyph's raster size which is different from the size of the font so scale it
        float font_scale = (float)font_size / (float)font_platform.standard_glyph_size;
        int scaled_bearing_x = (int)((float)added_glyph_raster_info->bearing_x * font_scale);
        int scaled_bearing_y = (int)((float)added_glyph_raster_info->bearing_y * font_scale);
        
        // Note(Leo): The calculated coordinate is the top-left corner of the quad enclosing the char
        // Note(Leo): Vulkan has the y-axis upside down compared to cartesian coordinates which freetype uses, so Y gets more positive as we go down
        // Note(Leo): Divide by 64 to convert back to pixel measurements from harfbuzz
        added_glyph->horizontal_offset = cursor_x + ((glyph_pos[i].x_offset / 64) + scaled_bearing_x);
        added_glyph->vertical_offset = cursor_y - ((glyph_pos[i].y_offset / 64) + scaled_bearing_y);

        added_glyph->width = (int)((float)added_glyph_raster_info->width * font_scale);
        added_glyph->height = (int)((float)added_glyph_raster_info->height * font_scale);
        
        cursor_x += glyph_pos[i].x_advance / 64;
        cursor_y += glyph_pos[i].y_advance / 64;
        
        printf("Cursor at (%d, %d)\n", cursor_x, cursor_y);
        
        // Note(Leo): This assumes text is from side to side rather than top to bottom (like in some languages)
        // Clip text once area is filled
        if(cursor_y >= area_height)
        {
            break;
        }
    }
    
    FT_Set_Pixel_Sizes(used_font->face, font_platform.standard_glyph_size, font_platform.standard_glyph_size);
    hb_ft_font_changed(used_font->font);    
    mark_end();
}