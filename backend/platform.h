#include "arena.h"
#include <vulkan/vulkan.h>
#include "DOM.h"
#include <cassert>

#define MAX_WINDOW_COUNT 100
#define MAX_TEXTURE_COUNT 30

#define Kilobytes(num_bytes) (num_bytes * 1000 * sizeof(char)) 
#define Megabytes(num_bytes) (num_bytes * 1000000 * sizeof(char))
#define Gigabytes(num_bytes) (num_bytes * 1000000000 * sizeof(char))

#pragma once
#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#include <windows.h>


#define WINDOWS_WINDOW_CLASS_NAME "TemporaryMarkupWindowClass"

extern HMODULE windows_module_handle;

extern ATOM window_class_atom;

struct PlatformWindow
{
    PlatformWindow* next_window;
    void* window_dom;

    HWND window_handle;
    VkCommandBuffer vk_command_buffer;
    VkSurfaceKHR vk_window_surface;
    VkSwapchainKHR vk_window_swapchain;
    LinkedPointer* vk_first_image_view;
    LinkedPointer* vk_first_framebuffer;
    
    VkImage vk_window_depth_image;
    VkDeviceMemory vk_depth_image_memory;
    VkImageView vk_depth_image_view;

    VkImage vk_window_msaa_image;
    VkDeviceMemory vk_msaa_image_memory;
    VkImageView vk_msaa_image_view;
        
    VkSemaphore vk_image_available_semaphore;
    VkSemaphore vk_render_finished_semaphore;
    VkFence vk_in_flight_fence;
    
    void* vk_staging_mapped_address;

    int vk_vertex_staging_buffer_size;
    VkBuffer vk_window_staging_buffer;
    VkDeviceMemory vk_staging_memory;

    VkBuffer vk_window_vertex_buffer;
    VkDeviceMemory vk_vertex_memory;
    
    int vk_index_staging_buffer_size;
    VkBuffer vk_window_index_buffer;
    VkDeviceMemory vk_index_memory;
    
    VkDescriptorSet vk_uniform_descriptor;
    VkBuffer vk_window_uniform_buffer;
    VkDeviceMemory vk_uniform_memory;
    void* vk_uniform_mapped_address;
    
    int width;
    int height;
    int flags;
};

void win32_vk_create_window_surface(PlatformWindow* window, HMODULE windows_module_handle);

#elif defined(__linux__) && !defined(_WIN32)

#include <X11/Xlib.h>

extern Display* x_display;
extern Visual* x_visual;

struct XDefaultValues
{
    int default_screen;
    int default_depth;
    Window default_root_window;
};

extern XDefaultValues x_defaults;

struct PlatformWindow
{
    PlatformWindow* next_window;
    void* window_dom;

    Window window_handle;
    VkSurfaceKHR vk_window_surface;
    VkSwapchainKHR vk_window_swapchain;
    LinkedPointer* vk_first_image_view;
    LinkedPointer* vk_first_framebuffer;
    VkCommandBuffer vk_command_buffer;
    GC window_gc;
    
    VkImage vk_window_depth_image;
    VkDeviceMemory vk_depth_image_memory;
    VkImageView vk_depth_image_view;
    
    VkImage vk_window_msaa_image;
    VkDeviceMemory vk_msaa_image_memory;
    VkImageView vk_msaa_image_view;    
    
    VkSemaphore vk_image_available_semaphore;
    VkSemaphore vk_render_finished_semaphore;
    VkFence vk_in_flight_fence;
    
    void* vk_staging_mapped_address;

    int vk_vertex_staging_buffer_size;
    VkBuffer vk_window_staging_buffer;
    VkDeviceMemory vk_staging_memory;

    VkBuffer vk_window_vertex_buffer;
    VkDeviceMemory vk_vertex_memory;
    
    int vk_index_staging_buffer_size;
    VkBuffer vk_window_index_buffer;
    VkDeviceMemory vk_index_memory;
    
    VkDescriptorSet vk_uniform_descriptor;
    VkBuffer vk_window_uniform_buffer;
    VkDeviceMemory vk_uniform_memory;
    void* vk_uniform_mapped_address;
    
    int width;
    int height;
    int flags;
};

void linux_vk_create_window_surface(PlatformWindow* window, Display* x_display);

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

struct VulkanSupportedExtensions
{
    bool VK_E_KHR_SURFACE;
    bool VK_E_KHR_WIN32_SURFACE;
    bool VK_E_KHR_MACOS_SURFACE;
    bool VK_E_KHR_METAL_SURFACE;
    bool VK_E_KHR_XLIB_SURFACE;
    bool VK_E_KHR_WAYLAND_SURFACE;
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

void RenderplatformDrawWindow(PlatformWindow* window);

bool RenderplatformSafeToDelete(PlatformWindow* window);

void RenderplatformLoadImage(FILE* image_file, const char* name);

//void InitializePlatform(Arena* master_arena);

int InitializeRuntime(Arena* master_arena, FileSearchResult* first_binary);

bool RuntimeInstanceMainPage();

int InitializeVulkan(Arena* master_arena, const char** required_extension_names, int required_extension_count, FILE* opaque_vert_shader, FILE* opaque_frag_shader, FILE* transparent_vert_shader, FILE* transparent_frag_shader, int image_buffer_size);
void PlatformRegisterDom(void* dom);

struct FontPlatformShapedGlyph
{
    // Note(Leo): All dimensions here are scaled versions of those of fixed size glyphs to fit the requested font size
    // Note(Leo): Dimensions are in pixels and should be converted to relative screenspace coords in the vulkan layer
    int horizontal_offset;
    int vertical_offset;
    int width;
    int height;
    uint32_t glyph_code;
};

struct FontPlatformGlyph
{
    // Note(Leo): Here so that if a glyph is evicted but the reference in the cache map isnt it can be caught by comparing expected to actual code
    uint32_t glyph_code;
    int bearing_x;
    int bearing_y;
    int width;
    int height;
    
    // Note(Leo): Data goes after the end of rasterised glyph but is non-determined in size at compile time
};

#define FontHandle int

int InitializeFontPlatform(Arena* master_arena, int standard_glyph_size);

void FontPlatformLoadFace(const char* font_name, FILE* font_file);
void FontPlatformShape(Arena* glyph_arena, const char* utf8_buffer, FontHandle font_handle, int font_size, int area_width, int area_height);
FontHandle FontPlatformGetFont(const char* font_name);
FontPlatformGlyph* FontPlatformRasterizeGlyph(FontHandle font_handle, uint32_t glyph_index);