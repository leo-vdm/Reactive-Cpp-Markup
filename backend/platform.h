#include <vulkan/vulkan.h>
#include "DOM.h"
#include "graphics_types.h"
#include <cassert>
#pragma once
#define MAX_WINDOW_COUNT 100
#define MAX_TEXTURE_COUNT 30
#define GLYPH_ATLAS_COUNT 200
#define FontHandle uint16_t


#define Kilobytes(num_bytes) (num_bytes * 1000 * sizeof(char)) 
#define Megabytes(num_bytes) (num_bytes * 1000000 * sizeof(char))
#define Gigabytes(num_bytes) (num_bytes * 1000000000 * sizeof(char))

#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

struct vk_swapchain_image
{
    vk_swapchain_image* next;
    VkImageView image_view;
    VkImage image;
};

enum class MouseState 
{
    UP,
    DOWN,
    UP_THIS_FRAME, // The mouse has switched from being down last frame to now being up
    DOWN_THIS_FRAME, // The mouse has switched from being up last frame to now being down
};

enum class KeyState
{
    UP,
    DOWN,
};

enum class CursorSource
{
    MOUSE,
    TOUCH
};

// The current shown cursor image
enum class CursorImage
{
    POINTER,
    HAND_POINT,
};

#define VIRTUAL_KEY_COUNT 255
struct VirtualKeyboard
{
    uint8_t keys[VIRTUAL_KEY_COUNT];
};

struct PlatformControlState
{
    MouseState mouse_left_state;
    MouseState mouse_middle_state;
    MouseState mouse_right_state;
    CursorSource cursor_source;
    vec2 scroll_dir;
    // Note(Leo): On touch screen we want multiple cursors, one for each touch point/finger but for mice we only have 1
    union
    {
        vec2 cursor_pos;
        vec2 cursor_positions[5]; 
    };
    
    // Note(Leo): A delta for each cursor
    union
    {
        vec2 cursor_delta;
        vec2 cursor_deltas[5];
    };
    
    uint8_t cursor_count;
    
    // Note(Leo): Keyboard state is shared by all windows but only the active window gets                                 
    //            key press events
    VirtualKeyboard* keyboard_state; 
};

struct shared_window 
{
    void* window_dom;

    VkCommandBuffer vk_command_buffer;
    VkSurfaceKHR vk_window_surface;
    VkSwapchainKHR vk_window_swapchain;
    vk_swapchain_image* vk_first_image;
    
    VkSemaphore vk_image_available_semaphore;
    VkSemaphore vk_render_finished_semaphore;
    VkFence vk_in_flight_fence;
    
    void* vk_staging_mapped_address;
    int vk_staging_buffer_size;
    VkBuffer vk_staging_buffer;
    VkDeviceMemory vk_staging_memory;
    
    int vk_input_buffer_size;
    VkBuffer vk_input_buffer;
    VkDeviceMemory vk_input_memory;

    VkDescriptorSet vk_combined_descriptor;
    
    PlatformControlState controls; 

    int width;
    int height;
    int flags;
};

KeyState GetKeyState(uint8_t key_code);

void PlatformShowVirtualKeyboard(bool should_show);

// Returns the number of bytes that were consumed
uint32_t PlatformConsumeUTF8ToUTF32(const char* utf8_buffer, uint32_t* codepoint, uint32_t buffer_length);
uint32_t PlatformConsumeUTF16ToUTF32(const uint16_t* utf16_buffer, uint32_t* codepoint, uint32_t buffer_length);
// Returns the number of UTF8 bytes generated
uint32_t PlatformUTF32ToUTF8(uint32_t codepoint, char* utf8_buffer);

struct PlatformFile
{
    Arena* data_arena; // The Arena that this file was loaded into, NULL for a malloced file
    void* data;
    uint64_t len;
};

// Note(Leo): Path is relative to the executable.
PlatformFile PlatformOpenFile(const char* file_path, Arena* bin_arena = NULL);
void PlatformCloseFile(PlatformFile* file);

PlatformControlState* PlatformGetControlState(DOM* dom);

// Searches the shaped glyphs of the given text element (only if its been shaped) and returns the glyph whats bounding
// box intersects the given point (in screenspace coords) or NULL if there are none.
FontPlatformShapedGlyph* PlatformGetGlyphAt(Element* text, vec2 pos);

// Gets the glyph containing the byte at the given index in the source buffer. (glyphs can have multiple bytes each)
FontPlatformShapedGlyph* PlatformGetGlyphForBufferIndex(Element* text, uint32_t index);

// Gets the glyph corresponding to index in the text element
FontPlatformShapedGlyph* PlatformGetGlyphAt(Element* text, uint32_t index);

// A wrapper for the pointer arithmetic finding what the index of this glyph is
uint32_t PlatformGetGlyphIndex(Element* text, FontPlatformShapedGlyph* glyph);

// Wrapper for getting the number of glyphs a text element's buffer produced
uint32_t PlatformGetGlyphCount(Element* text);

// Immediately re-merge the given element's style (including overrides)
void PlatformUpdateStyle(Element* target);

// Immediately re-evaluate the attributes of the given element.
void PlatformEvaluateAttributes(DOM* dom, Element* target);

// Does a mini version of what the first pass does with sibling text elements. Shapes into the given arena and returns the
// combined text element.
void PlatformPreviewText(Arena* shape_arena, Element* first_text, Measurement width, Measurement height);

void PlatformSetTextClipboard(const char* utf8_buffer, uint32_t buffer_len);
char* PlatformGetTextClipboard(uint32_t* buffer_len); // UTF8 on the scratch arena

extern float SCROLL_MULTIPLIER;
#define PREFETCH_INTRINSIC(...)

#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#include <windows.h>

#define PLATFORM_WINDOWS 1
#include "key_codes.h"

#define TIMER_INTRINSIC() __rdtsc()

#undef PREFETCH_INTRINSIC
#define PREFETCH_INTRINSIC(A) _mm_prefetch((char *)(A), _MM_HINT_T0)

#define WINDOWS_WINDOW_CLASS_NAME L"TemporaryMarkupWindowClass"

extern HMODULE windows_module_handle;

extern ATOM window_class_atom;

struct PlatformWindow : shared_window
{
    PlatformWindow* next_window;
    HWND window_handle;
};

#define VkLibrary HMODULE

#define PlatformGetProcAddress(module, name) GetProcAddress(module, name)

void win32_vk_create_window_surface(PlatformWindow* window, HMODULE windows_module_handle);

#elif defined(__linux__) && !defined(_WIN32) && !defined(__ANDROID__)

#define PLATFORM_LINUX 1

#include "x86intrin.h"
#define TIMER_INTRINSIC() __rdtsc()

#include <X11/Xlib.h>
#include <dlfcn.h>

#include "key_codes.h"

#define VkLibrary void*
#define PlatformGetProcAddress(module, name) dlsym( module, name)

extern Display* x_display;
extern Visual* x_visual;

struct XDefaultValues
{
    int default_screen;
    int default_depth;
    Window default_root_window;
};

extern XDefaultValues x_defaults;

struct PlatformWindow : shared_window
{
    PlatformWindow* next_window;
    Window window_handle;
    GC window_gc;
    XIC window_input_context;
};

void linux_vk_create_window_surface(PlatformWindow* window, Display* x_display);
#elif defined(__ANDROID__)
#define PLATFORM_ANDROID 1

#include <dlfcn.h>
//#include <android_native_app_glue.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include "key_codes.h"

#define VkLibrary void*
#define PlatformGetProcAddress(module, name) dlsym( module, name)

#undef printf()
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "RCM", __VA_ARGS__);

struct PlatformWindow : shared_window
{
    ANativeWindow* window_handle;
};
void android_vk_create_window_surface(PlatformWindow* window);

#endif

void vk_destroy_window_surface(PlatformWindow* window);
void vk_window_resized(PlatformWindow* window);

// Vulkan extension name macros
#define VK_E_KHR_SURFACE_NAME "VK_KHR_surface"
#define VK_E_KHR_WIN32_SURFACE_NAME "VK_KHR_win32_surface"
#define VK_E_KHR_MACOS_SURFACE_NAME "VK_MVK_macos_surface"
#define VK_E_KHR_METAL_SURFACE_NAME "VK_EXT_metal_surface"
#define VK_E_KHR_XLIB_SURFACE_NAME  "VK_KHR_xlib_surface"
#define VK_E_KHR_WAYLAND_SURFACE_NAME "VK_KHR_wayland_surface"
#define VK_E_KHR_ANDROID_SURFACE_NAME "VK_KHR_android_surface"

struct VulkanSupportedExtensions
{
    bool VK_E_KHR_SURFACE;
    bool VK_E_KHR_WIN32_SURFACE;
    bool VK_E_KHR_MACOS_SURFACE;
    bool VK_E_KHR_METAL_SURFACE;
    bool VK_E_KHR_XLIB_SURFACE;
    bool VK_E_KHR_WAYLAND_SURFACE;
    bool VK_E_KHR_ANDROID_SURFACE;
};

#define VULKAN_APPLICATION_NAME "TemporaryApplicationName"

// Window flags

// Window manager wants the window to exit
#define QUIT_WINDOW 1 << 0

// Flag indicating window is waiting for all its resources to be freed so it can be destroyed
#define DEAD_WINDOW 1 << 1 

// Flag indicating window is waiting for its resources to be unlocked so it can re-create its swapchains
#define RESIZED_WINDOW 1 << 2

PlatformWindow* PlatformCreateWindow(Arena* windows_arena, const char* window_name);

void RenderplatformDrawWindow(PlatformWindow* window, Arena* renderque);

bool RenderplatformSafeToDelete(PlatformWindow* window);

void RenderplatformLoadImage(FILE* image_file, const char* name);

void RenderplatformUploadGlyph(void* glyph_data, int glyph_width, int glyph_height, int glyph_slot);

vec3 RenderPlatformGetGlyphPosition(int glyph_slot);

LoadedImageHandle* RenderplatformGetImage(const char* name);

//void InitializePlatform(Arena* master_arena);

int InitializeRuntime(Arena* master_arena, FileSearchResult* first_binary);

bool RuntimeInstanceMainPage();

int InitializeVulkan(Arena* master_arena, const char** required_extension_names, int required_extension_count, FILE* combined_shader);
void PlatformRegisterDom(void* dom);

struct RenderPlatformImageTile
{
    RenderPlatformImageTile* next;
    uint32_t content_width;
    uint32_t content_height;
    uvec2 image_offsets; // Offset of this tile in the original image
    uvec3 atlas_offsets; // Offset of this tile's content inside the atlas
};

struct LoadedImageHandle
{
    uint32_t image_width;
    uint32_t image_height;
    
    uint32_t tiled_width; // The # of tiles that this image is wide
    uint32_t tiled_height;// # of tiles high this image is.
    
    RenderPlatformImageTile* first_tile;
};

struct FontPlatformShapedGlyph
{
    // Note(Leo): All dimensions here are scaled versions of those of fixed size glyphs to fit the requested font size
    vec2 placement_offsets;
    vec2 placement_size;
    
    vec3 atlas_offsets;
    vec2 atlas_size;
    
    uint32_t buffer_index; // Index of this glyph run into the original buffer
    uint32_t run_length; // Length of this run (Several codepoints may be combined by harfbuzz)
    
    float base_line;
    
    StyleColor color;
};

struct FontPlatformGlyph
{
    // Caching related
    uint32_t prev_lru;
    uint32_t next_lru;
    uint32_t codepoint;

    // Real values
    int bearing_x;
    int bearing_y;
    int width;
    int height;
    FontHandle font;
};

struct FontPlatformShapedText
{
    FontPlatformShapedGlyph* first_glyph;
    uint32_t glyph_count;
    
    uint32_t required_width;
    uint32_t required_height;
};


int InitializeFontPlatform(Arena* master_arena, int standard_glyph_size);

void FontPlatformLoadFace(const char* font_name, FILE* font_file);
void FontPlatformLoadFace(const char* font_name, PlatformFile* font_file);
void FontPlatformShapeMixed(Arena* glyph_arena, FontPlatformShapedText* result, StringView* utf8_strings, FontHandle* font_handles, uint16_t* font_sizes, StyleColor* colors, int text_block_count, uint32_t wrapping_point);
FontHandle FontPlatformGetFont(const char* font_name);

int FontPlatformGetGlyphSize();
void FontPlatformUpdateCache(int new_size_glyphs);

Arena* RuntimeTickAndBuildRenderque(Arena* renderque, DOM* dom, PlatformControlState* controls, int window_width, int window_height);
Arena* ShapingPlatformShape(Element* root_element, Arena* shape_arena, int element_count, int window_width, int window_height);

bool PointInsideBounds(const bounding_box bounds, const vec2 point);

#define USING_INSTREMENTATION 1
// Instrumentation related stuff
#if !INDCLUDED_INSTREMENTATION && USING_INSTREMENTATION  
#define INDCLUDED_INSTREMENTATION 1
    enum 
    {
        TIMED_BLOCKS_PLATFORM_LOOP,
        TIMED_BLOCKS_DRAW_WINDOW,
        TIMED_BLOCKS_WAIT_FENCE,
        TIMED_BLOCKS_RENDER_SUBMIT,
        TIMED_BLOCKS_RENDER_PRESENT,
        TIMED_BLOCKS_TICK_AND_BUILD,
        TIMED_BLOCKS_HARFBUZZ,
        TIMED_BLOCKS_MEOW,
        TIMED_BLOCKS_EVALUATE_ATTRIBUTES,
        TIMED_BLOCKS_PLATFORM_SHAPE,
        TIMED_BLOCKS_FIRST_PASS,
        TIMED_BLOCKS_SECOND_PASS,
        TIMED_BLOCKS_FINAL_PASS,
        TIMED_BLOCKS_TEXT_SHAPE,
        TIMED_BLOCKS_SECTION_A,
        TIMED_BLOCKS_BLOCKS_MAX, // Note(Leo): Should be at the end of the enum
    };
    

    struct timing_info 
    {
        uint64_t cycle_count;
        uint64_t hits;
        uint64_t average_cycle_count;
    };
    
    extern timing_info INSTRUMENT_TIMINGS[TIMED_BLOCKS_BLOCKS_MAX];
    
    #define SetupInstrumentation() 
    
    #if PLATFORM_LINUX || PLATFORM_ANDROID
    #define BEGIN_TIMED_BLOCK(name) uint64_t START_CYCLE_##name = TIMER_INTRINSIC(); INSTRUMENT_TIMINGS[TIMED_BLOCKS_##name].hits++;
    #define END_TIMED_BLOCK(name) INSTRUMENT_TIMINGS[TIMED_BLOCKS_##name].cycle_count += TIMER_INTRINSIC() - START_CYCLE_##name;
    #endif
    
    #if PLATFORM_WINDOWS
    #define BEGIN_TIMED_BLOCK(name) uint64_t START_CYCLE_##name = 0;\
            QueryPerformanceCounter((LARGE_INTEGER*)&START_CYCLE_##name);\
            INSTRUMENT_TIMINGS[TIMED_BLOCKS_##name].hits++;
    
    #define END_TIMED_BLOCK(name) uint64_t END_FREQ_##name = 0;\
            uint64_t END_CYCLE_##name = 0;\
            QueryPerformanceFrequency((LARGE_INTEGER*)&END_FREQ_##name);\
            QueryPerformanceCounter((LARGE_INTEGER*)&END_CYCLE_##name);\
            INSTRUMENT_TIMINGS[TIMED_BLOCKS_##name].cycle_count += ((END_CYCLE_##name - START_CYCLE_##name) * 1000000) / END_FREQ_##name;
    #endif

    // Define once at the platform implementation like an STB style lib
    #if INSTRUMENT_IMPLEMENTATION
        timing_info INSTRUMENT_TIMINGS[TIMED_BLOCKS_BLOCKS_MAX] = {};
        
        const char* BLOCK_NAMES[] = 
        {
            "PLATFORM_LOOP",
            "DRAW_WINDOW",
            "WAIT_FENCE",
            "RENDER_SUBMIT",
            "RENDER_PRESENT",
            "TICK_AND_BUILD",
            "HARFBUZZ",
            "MEOW_HASH",
            "EVALUATE_ATTRIBUTES",
            "PLATFORM_SHAPE",
            "FIRST_PASS",
            "SECOND_PASS",
            "FINAL_PASS",
            "TEXT_SHAPE",
            "SECTION_A",
            "BLOCKS_MAX",
        };

        
        void DUMP_TIMINGS()
        {
            printf("Timings:\n");
            for(int i = 0; i < TIMED_BLOCKS_BLOCKS_MAX; i++)
            {
                if(INSTRUMENT_TIMINGS[i].hits)
                {
                    #if PLATFORM_LINUX || PLATFORM_ANDROID
                    printf("\t%s: %ldcy, %ldhits, %ldcy/hit\n", BLOCK_NAMES[i], INSTRUMENT_TIMINGS[i].cycle_count, INSTRUMENT_TIMINGS[i].hits, INSTRUMENT_TIMINGS[i].cycle_count / INSTRUMENT_TIMINGS[i].hits);
                    #endif
                    #if PLATFORM_WINDOWS
                    uint64_t added_cycles = INSTRUMENT_TIMINGS[i].average_cycle_count ? INSTRUMENT_TIMINGS[i].cycle_count : INSTRUMENT_TIMINGS[i].cycle_count * 2;
                    INSTRUMENT_TIMINGS[i].average_cycle_count = (INSTRUMENT_TIMINGS[i].average_cycle_count * 29) + added_cycles;
                    
                    if(INSTRUMENT_TIMINGS[i].average_cycle_count)
                    {
                        INSTRUMENT_TIMINGS[i].average_cycle_count = INSTRUMENT_TIMINGS[i].average_cycle_count / 30;
                    }
                    
                    printf("\t%s: %ld microseconds, %ldhits, %ld microseconds/hit, %ld avg microseconds\n", BLOCK_NAMES[i], INSTRUMENT_TIMINGS[i].cycle_count, INSTRUMENT_TIMINGS[i].hits, INSTRUMENT_TIMINGS[i].cycle_count / INSTRUMENT_TIMINGS[i].hits, INSTRUMENT_TIMINGS[i].average_cycle_count);
                    #endif

                    INSTRUMENT_TIMINGS[i].cycle_count = 0;
                    INSTRUMENT_TIMINGS[i].hits = 0;
                    
                }
            }
        }
    #endif
    
#else
    #define BEGIN_TIMED_BLOCK(a) (void)0
    #define END_TIMED_BLOCK(a) (void)0
    #define SetupInstrumentation() (void)0
    #define DUMP_TIMINGS (void)0
#endif
