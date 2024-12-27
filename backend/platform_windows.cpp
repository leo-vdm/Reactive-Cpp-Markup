#include "platform.h"

#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#include <windows.h>
#include <cassert>

HMODULE win32_module_handle = {};
ATOM window_class_atom = {};

const char* win32_required_vk_extensions[] = {VK_E_KHR_SURFACE_NAME, VK_E_KHR_WIN32_SURFACE_NAME};

PlatformWindow* curr_processed_window;

// Note(Leo): should only handle size, close quit etc messages here and not input
LRESULT win32_main_callback(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
    LRESULT result = 0;
    if(!curr_processed_window)
    {
        result = DefWindowProcA(window_handle, message, w_param, l_param);
        return result;
    }
    switch(message)
    {
        case(WM_SIZING):
        {
            curr_processed_window->flags = curr_processed_window->flags | RESIZED_WINDOW;
            RECT* new_size = (RECT*)l_param;
            curr_processed_window->width = new_size->right - new_size->left;
            curr_processed_window->height = new_size->bottom - new_size->top;
            break;            
        }
        case(WM_SIZE):
        {
            curr_processed_window->flags = curr_processed_window->flags | RESIZED_WINDOW;
            curr_processed_window->width = (int)LOWORD(l_param);
            curr_processed_window->height = (int)HIWORD(l_param);
            break;
        }
        case(WM_DESTROY):
        {
            //curr_processed_window_events.destroy_window = true;
            result = DefWindowProcA(window_handle, message, w_param, l_param);
            break;
        }
        case(WM_CLOSE):
        {
            curr_processed_window->flags = curr_processed_window->flags | QUIT_WINDOW;
            break;
        }
        case(WM_ACTIVATEAPP):
            break;
        default:
        {
            result = DefWindowProcA(window_handle, message, w_param, l_param);
            break;
        }
    }
    
    return result;
}

PlatformWindow* win32_create_window(Arena* windows_arena, const char* window_name)
{
    PlatformWindow* created_window = (PlatformWindow*)Alloc(windows_arena, sizeof(PlatformWindow), zero());
    created_window->window_handle = CreateWindowExA(0, WINDOWS_WINDOW_CLASS_NAME, window_name, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, win32_module_handle, 0);
    
    RECT rect;
    if(!GetWindowRect(created_window->window_handle, &rect))
    {
        return NULL;
    }
    
    created_window->width = rect.right - rect.left;
    created_window->height = rect.bottom - rect.top;
    
    win32_vk_create_window_surface(created_window, win32_module_handle);
    
    if(!created_window->window_handle)
    {
        return NULL;
    }
    
    return created_window;
}

// Returns -1 if searched char is not found, otherwise returns the index of the last instance of searched_char
int find_last_of(const char* c_string, char searched_char)
{
    int last_index = -1;
    char* curr = (char*)c_string;
    int index = 0;
    while(*curr != '\0')
    {
        if(*curr == searched_char)
        {
            last_index = index;
        }
        index++;
        curr++;
    }
    
    return last_index;
}

char* win32_get_execution_dir()
{
    #define MAX_PATH_LEN 2000    
    char* executable_path = (char*)AllocScratch(sizeof(char)*MAX_PATH_LEN);
    GetModuleFileNameA(NULL, executable_path, MAX_PATH_LEN);
    
    int executable_dir_len = find_last_of(executable_path, '\\');
    executable_path[executable_dir_len] = '\0';
    return executable_path;
}

FILE* win32_open_relative_file_path(const char* relative_path, const char* open_params)
{
    char* working_dir = win32_get_execution_dir();
    int desired_len = snprintf(NULL, 0, "%s/%s", working_dir, relative_path);
    desired_len++; // +1 to make space for \0
    char* file_path = (char*)AllocScratch(desired_len*sizeof(char));
    sprintf(file_path, "%s/%s", working_dir, relative_path);
    
    FILE* opened = fopen(file_path, open_params);
    DeAllocScratch(file_path);
    DeAllocScratch(working_dir);
    
    return opened;
}

FileSearchResult* win32_find_markup_binaries(Arena* search_results_arena, Arena* search_result_values_arena)
{
    FileSearchResult* first = (FileSearchResult*)search_results_arena->next_adddress;
    char* working_dir = win32_get_execution_dir();
    SearchDir(search_results_arena, search_result_values_arena, working_dir, ".bin");
    
    DeAllocScratch(working_dir);
    return first;
}

FileSearchResult* win32_find_image_resources(Arena* search_results_arena, Arena* search_result_values_arena)
{
    FileSearchResult* first = (FileSearchResult*)search_results_arena->next_address;
    char* working_dir = win32_get_execution_dir();

    #define RESOURCE_DIR_NAME "resources"
    int desired_len = snprintf(NULL, 0, "%s/%s", working_dir, RESOURCE_DIR_NAME);
    desired_len++; // + 1 to fit \0
    char* resource_dir_path = (char*)AllocScratch(desired_len*sizeof(char));
    sprintf(resource_dir_path, "%s/%s", working_dir, RESOURCE_DIR_NAME);
    DeAllocScratch(working_dir);
    
    SearchDir(search_results_arena, search_result_values_arena, resource_dir_path, "");
    
    DeAllocScratch(resource_dir_path);
    return first;
}

void win32_process_window_events(PlatformWindow* target_window)
{
    curr_processed_window = target_window;
    
    MSG message;
    int return_value = 0;
    while(PeekMessageA(&message, target_window->window_handle, 0, 0, PM_REMOVE))
    { 
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    
    curr_processed_window = NULL;
    
    target_window->flags = target_window->flags | return_value;
}

struct win32_platform_state
{
    Arena master_arena;  
    Arena* windows;
    Arena* pointer_arrays;
    Arena* search_results;
    Arena* search_result_values;

    Arena* runtime_master_arena;
    
    PlatformWindow* first_window;
};

void win32_destroy_window(Arena* windows_arena, PlatformWindow* window)
{
    DestroyWindow(window->window_handle);
    
    vk_destroy_window_surface(window);
    
    DeAlloc(windows_arena, window);
}

win32_platform_state platform;

int main()
{
    platform = {};
    InitScratch(sizeof(char)*100000);
    platform.master_arena = CreateArena(1000*sizeof(Arena), sizeof(Arena));
    
    win32_module_handle = GetModuleHandleA(0);
    
    WNDCLASSA window_class = {};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = win32_main_callback;
    window_class.hInstance = win32_module_handle;
    window_class.lpszClassName = WINDOWS_WINDOW_CLASS_NAME;
    
    window_class_atom = RegisterClassA(&window_class);
    
    FILE* main_vert_shader = win32_open_relative_file_path("compiled_shaders/vert.spv", "rb");
    
    FILE* main_frag_shader = win32_open_relative_file_path("compiled_shaders/frag.spv", "rb");
    
    if(!main_vert_shader || !main_frag_shader)
    {
        printf("Error: Shaders could not be loaded!\n");
        return 1;
    }
    
    int required_extension_count = sizeof(win32_required_vk_extensions) / sizeof(char**);
    InitializeVulkan(&(platform.master_arena), win32_required_vk_extensions, required_extension_count, main_vert_shader, main_frag_shader, 100000000);
    
    fclose(main_vert_shader);
    fclose(main_frag_shader);
    
    if(!window_class_atom)
    {
        printf("Error: Windows refused to create a window class.\n");
        return 1;
    }
    
    
    platform.search_results = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.search_results) = CreateArena(sizeof(FileSearchResult)*1000, sizeof(FileSearchResult));
    
    platform.search_result_values = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.search_result_values) = CreateArena(sizeof(char)*100000, sizeof(char));
    
    FileSearchResult* first_binary = win32_find_markup_binaries(platform.search_results, platform.search_result_values);
    
    platform.runtime_master_arena = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.runtime_master_arena) = CreateArena(sizeof(Arena)*1000, sizeof(Arena));
    
    InitializeRuntime(platform.runtime_master_arena, first_binary);
    
    platform.windows = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.windows) = CreateArena(sizeof(PlatformWindow)*100, sizeof(PlatformWindow));
    
    
    if(!RuntimeInstanceMainPage())
    {
        printf("ERROR: Failed to initialize main page! Is the binary missing?\n");
        return 1;
    }
    
    FileSearchResult* first_image = win32_find_image_resources(platform.search_results, platform.search_result_values);
    FileSearchResult* curr_image = first_image;
    while(curr_image->file_path)
    {
        FILE* opened = fopen(curr_image->file_path, "rb");
        RenderplatformLoadImage(opened, curr_image->file_name);
        fclose(opened);
        curr_image = first_image++;
    }
    
    PlatformWindow* curr_window = platform.first_window;
    while(true)
    {
        if(!curr_window)
        {
            curr_window = platform.first_window;
        }
        
        win32_process_window_events(curr_window);
        
        if(curr_window->flags)
        {
            
            if(curr_window->flags & RESIZED_WINDOW)
            {
                if(RenderplatformSafeToDelete(curr_window))
                {
                    vk_window_resized(curr_window);
                    curr_window->flags = 0;
                }
            }
            
            if(curr_window->flags & DEAD_WINDOW)
            {
                if(RenderplatformSafeToDelete(curr_window))
                {
                    win32_destroy_window(platform.windows, curr_window);
                    break;
                }
            }
            if(curr_window->flags & QUIT_WINDOW)
            {
                curr_window->flags = curr_window->flags | DEAD_WINDOW;
            }
            continue;
        }

        
        
        RenderplatformDrawWindow(curr_window);
        
        curr_window = curr_window->next_window;
    }
    
    return 0;
}


#define PLACEHOLDER_WINDOW_NAME "PlaceholderName"

void PlatformRegisterDom(void* dom)
{
    PlatformWindow* created_window = win32_create_window(platform.windows, PLACEHOLDER_WINDOW_NAME);
    created_window->window_dom = dom;
    
    created_window->next_window = platform.first_window;
    platform.first_window = created_window;
}

#endif
