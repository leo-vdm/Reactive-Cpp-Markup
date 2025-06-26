
#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#define INSTRUMENT_IMPLEMENTATION 1
#include "platform.h"
#include <windows.h>
#include <windowsx.h>
#include <cassert>

HMODULE win32_module_handle = {};
ATOM window_class_atom = {};

const char* win32_required_vk_extensions[] = {VK_E_KHR_SURFACE_NAME, VK_E_KHR_WIN32_SURFACE_NAME};

PlatformWindow* curr_processed_window;

float SCROLL_MULTIPLIER; 

struct win32_platform_state
{
    Arena master_arena;  
    Arena* windows;
    Arena* pointer_arrays;
    Arena* search_results;
    Arena* search_result_values;

    Arena* runtime_master_arena;
    
    PlatformWindow* first_window;
    
    VirtualKeyboard keyboard_state;
    HCURSOR cursors[20]; // You REALLY shouldnt need more than this
};
win32_platform_state platform;

void print_input_state(PlatformWindow* window)
{
    printf("Window input state: \n");
    printf("Scroll: (%f, %f)\n", window->controls.scroll_dir.x, window->controls.scroll_dir.y);
    printf("Buttons: l=%d, m=%d, r=%d\n", (int)window->controls.mouse_left_state, (int)window->controls.mouse_middle_state, (int)window->controls.mouse_right_state);
    printf("Cursor: (%f, %f)\n", window->controls.cursor_pos.x, window->controls.cursor_pos.y);
}

// Updates conrol state button states from THIS_FRAME to normal
// Updates scroll to 0
void update_control_state(PlatformWindow* target_window)
{
    if(target_window->controls.mouse_left_state == MouseState::DOWN_THIS_FRAME)
    {
        target_window->controls.mouse_left_state =  MouseState::DOWN;
    } 
    else if(target_window->controls.mouse_left_state == MouseState::UP_THIS_FRAME)
    {
        target_window->controls.mouse_left_state =  MouseState::UP;
    }
    
    if(target_window->controls.mouse_middle_state == MouseState::DOWN_THIS_FRAME)
    {
        target_window->controls.mouse_middle_state = MouseState::DOWN;
    }
    else if(target_window->controls.mouse_middle_state == MouseState::UP_THIS_FRAME)
    {
        target_window->controls.mouse_middle_state = MouseState::UP;
    }
    
    if(target_window->controls.mouse_right_state == MouseState::DOWN_THIS_FRAME)
    {
        target_window->controls.mouse_right_state = MouseState::DOWN;
    }
    else if(target_window->controls.mouse_right_state == MouseState::UP_THIS_FRAME)
    {
        target_window->controls.mouse_right_state = MouseState::UP;
    }
    
    target_window->controls.scroll_dir = { 0.0, 0.0 };
}

Event* last_event; 

LRESULT win32_main_callback(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
    LRESULT result = 0;
    if(!curr_processed_window)
    {
        result = DefWindowProcW(window_handle, message, w_param, l_param);
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
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {

            if (message == WM_LBUTTONDOWN)
            {
                curr_processed_window->controls.mouse_left_state = MouseState::DOWN_THIS_FRAME;
            }
            else if(message == WM_LBUTTONUP)
            {
                curr_processed_window->controls.mouse_left_state = MouseState::UP_THIS_FRAME;
            }
            else if (message == WM_RBUTTONDOWN)
            {
                curr_processed_window->controls.mouse_right_state = MouseState::DOWN_THIS_FRAME;
            }
            else if (message == WM_RBUTTONUP)
            {
                curr_processed_window->controls.mouse_right_state = MouseState::UP_THIS_FRAME;
            }
            else if (message == WM_MBUTTONDOWN)
            {
                curr_processed_window->controls.mouse_middle_state = MouseState::DOWN_THIS_FRAME;
            }
            else if (message == WM_MBUTTONUP)
            {
                curr_processed_window->controls.mouse_middle_state = MouseState::UP_THIS_FRAME;
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            if((uint8_t)w_param >= VIRTUAL_KEY_COUNT)
            {
                break;
            }
        
            Event* added = PushEvent((DOM*)curr_processed_window->window_dom);
            if(message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            {
                added->type = EventType::KEY_DOWN;
                curr_processed_window->controls.keyboard_state->keys[(uint8_t)w_param] = (uint8_t)KeyState::DOWN;
            }
            else
            {
                added->type = EventType::KEY_UP;
                curr_processed_window->controls.keyboard_state->keys[(uint8_t)w_param] = (uint8_t)KeyState::UP;
            }
            added->Key.code = (uint8_t)w_param;
            last_event = added;
            
            break;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_IME_CHAR:
        {
            Event* added = last_event;
            if(!last_event)
            {
                // An IME of some kind has sent this char.
                added = PushEvent((DOM*)curr_processed_window->window_dom);
                added->type = EventType::KEY_DOWN;
                added->Key.code = K_VIRTUAL;
            }
            
            PlatformConsumeUTF16ToUTF32((uint16_t*)&w_param, &added->Key.key_char, 4);
            
            break;
        }
        case WM_MOUSEMOVE:
        {
            float x = GET_X_LPARAM(l_param);
            float y = GET_Y_LPARAM(l_param);

            curr_processed_window->controls.cursor_delta = {x - curr_processed_window->controls.cursor_pos.x, y - curr_processed_window->controls.cursor_pos.y};
            curr_processed_window->controls.cursor_pos = {x, y};

            break;
        }
        case WM_MOUSEWHEEL:
        {
            // Note(Leo): https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel
            //            120 is microsoft's magic number for how much to scroll lol
            curr_processed_window->controls.scroll_dir = {0.0f, ((float)((SHORT) HIWORD(w_param)) / 120.0f) * SCROLL_MULTIPLIER };
            break;
        }
        case WM_MOUSEHWHEEL:
        {
            // Note(Leo): x-axis scroll is inverted compared to other window managers
            curr_processed_window->controls.scroll_dir = {((float)HIWORD(w_param) / -120.0f) * SCROLL_MULTIPLIER, 0.0f };
            break;
        }
        case WM_SETCURSOR:
        {
            if(LOWORD(l_param) == HTCLIENT)
            {
                SetCursor(platform.cursors[0]);
                result = 1;
            }
            break;
        }
        default:
        {
            result = DefWindowProcW(window_handle, message, w_param, l_param);
            break;
        }
    }
    
    return result;
}

PlatformWindow* win32_create_window(Arena* windows_arena, const char* window_name)
{
    PlatformWindow* created_window = (PlatformWindow*)Alloc(windows_arena, sizeof(PlatformWindow), zero());
    created_window->window_handle = CreateWindowExW(0, (LPCWSTR)WINDOWS_WINDOW_CLASS_NAME, (LPCWSTR)window_name, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, win32_module_handle, 0);
    
    RECT rect;
    if(!GetWindowRect(created_window->window_handle, &rect))
    {
        return NULL;
    }
    
    created_window->width = rect.right - rect.left;
    created_window->height = rect.bottom - rect.top;
    created_window->controls.keyboard_state = &platform.keyboard_state;
    
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

PlatformFile PlatformOpenFile(const char* file_path, Arena* bin_arena)
{
    PlatformFile loaded = {};
    
    FILE* opened = win32_open_relative_file_path(file_path, "rb");
    
    if(!opened)
    {
        return loaded;
    }
    
    fseek(opened, 0, SEEK_END);
    uint64_t opened_size = ftell(opened);
    fseek(opened, 0, SEEK_SET);
    
    if(bin_arena)
    {
        loaded.data = Alloc(bin_arena, sizeof(char)*opened_size);
        loaded.data_arena = bin_arena;
    }
    else
    {
        loaded.data = malloc(opened_size);
    }
    
    loaded.len = opened_size;
    
    fread(loaded.data, opened_size, 1, opened);
    fclose(opened);
    
    return loaded;
}

void PlatformCloseFile(PlatformFile* file)
{
    if(file->data_arena)
    {
        DeAlloc(file->data_arena, file->data);
    }
    else
    {
        free(file->data);
    }
}

FileSearchResult* win32_find_markup_binaries(Arena* search_results_arena, Arena* search_result_values_arena)
{
    FileSearchResult* first = (FileSearchResult*)search_results_arena->next_address;
    char* working_dir = win32_get_execution_dir();
    SearchDir(search_results_arena, search_result_values_arena, working_dir, ".bin");
    
    DeAllocScratch(working_dir);
    
    // Open all the files
    FileSearchResult* curr = first;
    while(curr->file_path)
    {
        curr->file = fopen(curr->file_path, "rb");
        curr++;
    }
    
    return first;
}

FileSearchResult* win32_find_image_resources(Arena* search_results_arena, Arena* search_result_values_arena)
{
    FileSearchResult* first = (FileSearchResult*)search_results_arena->next_address;
    char* working_dir = win32_get_execution_dir();

    #define RESOURCE_DIR_NAME "resources/images"
    int desired_len = snprintf(NULL, 0, "%s/%s", working_dir, RESOURCE_DIR_NAME);
    desired_len++; // + 1 to fit \0
    char* resource_dir_path = (char*)AllocScratch(desired_len * sizeof(char));
    sprintf(resource_dir_path, "%s/%s", working_dir, RESOURCE_DIR_NAME);
    DeAllocScratch(working_dir);
    
    SearchDir(search_results_arena, search_result_values_arena, resource_dir_path, "");
    
    DeAllocScratch(resource_dir_path);
    return first;
}

void win32_process_window_events(PlatformWindow* target_window)
{
    last_event = NULL;
    update_control_state(target_window);
    curr_processed_window = target_window;
    
    MSG message;
    int return_value = 0;
    while(PeekMessageW(&message, target_window->window_handle, 0, 0, PM_REMOVE))
    { 
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    
    curr_processed_window = NULL;
    
    target_window->flags = target_window->flags | return_value;
}

void win32_destroy_window(Arena* windows_arena, PlatformWindow* window)
{
    DestroyWindow(window->window_handle);
    
    vk_destroy_window_surface(window);
    
    DeAlloc(windows_arena, window);
}

KeyState GetKeyState(uint8_t key_code)
{
    assert(key_code < VIRTUAL_KEY_COUNT);
    
    return (KeyState)platform.keyboard_state.keys[key_code]; 
}

int main()
{
    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);

    WINDOWS_PAGE_SIZE = static_cast<uintptr_t>(sys_info.dwPageSize);
    WINDOWS_PAGE_MASK = WINDOWS_PAGE_SIZE - 1;

    platform = {};
    InitScratch(sizeof(char)*1000000);
    platform.master_arena = CreateArena(1000*sizeof(Arena), sizeof(Arena));
    
    SCROLL_MULTIPLIER = 30; 
    
    win32_module_handle = GetModuleHandleA(0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    
    WNDCLASSW window_class = {};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = win32_main_callback;
    window_class.hInstance = win32_module_handle;
    window_class.lpszClassName = reinterpret_cast<LPCWSTR>(WINDOWS_WINDOW_CLASS_NAME);
    
    window_class_atom = RegisterClassW(&window_class);
    
    platform.cursors[0] = LoadCursor(NULL, IDC_ARROW);
    
    
    InitializeFontPlatform(&(platform.master_arena), 0);
    
    FILE* default_font = win32_open_relative_file_path("resources/fonts/default.ttf", "rb");
    FontPlatformLoadFace("platform_default_font.ttf", default_font);
    fclose(default_font);    
    
    FILE* combined_shader = win32_open_relative_file_path("compiled_shaders/combined_shader.spv", "rb");
    
    if(!combined_shader)
    {
        printf("Error: Shaders could not be loaded!\n");
        return 1;
    }
    
    int required_extension_count = sizeof(win32_required_vk_extensions) / sizeof(char**);
    InitializeVulkan(&(platform.master_arena), win32_required_vk_extensions, required_extension_count, combined_shader);
    
    fclose(combined_shader);
    
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
        curr_image++;
    }
    
    // Note(Leo): We use 1 renderque for all the windows then when we loop back we swap them so that we write into the 
    //            other. This is done so we can safely access the sizing data of elements from the last frame.
    Arena* renderques[2] = {};  
    renderques[0] = (Arena*)Alloc(runtime.master_arena, sizeof(Arena), zero());
    renderques[1] = (Arena*)Alloc(runtime.master_arena, sizeof(Arena), zero());
    *renderques[0] = CreateArena(sizeof(Element) * 1000000, sizeof(Element));
    *renderques[1] = CreateArena(sizeof(Element) * 1000000, sizeof(Element));
    bool used_renderque = false;
    
    PlatformWindow* curr_window = platform.first_window;
    while(true)
    {
        BEGIN_TIMED_BLOCK(PLATFORM_LOOP);
        if(!curr_window)
        {
            curr_window = platform.first_window;
            used_renderque = !used_renderque;
            // Reset the arena we will use
            ResetArena(renderques[used_renderque]);
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

        BEGIN_TIMED_BLOCK(TICK_AND_BUILD);
        Arena* final_renderque = RuntimeTickAndBuildRenderque(renderques[used_renderque], (DOM*)curr_window->window_dom, &curr_window->controls, curr_window->width, curr_window->height);
        END_TIMED_BLOCK(TICK_AND_BUILD);
        
        BEGIN_TIMED_BLOCK(DRAW_WINDOW);
        RenderplatformDrawWindow(curr_window, final_renderque);
        END_TIMED_BLOCK(DRAW_WINDOW);
        
        RuntimeClearTemporal((DOM*)curr_window->window_dom);
        
        curr_window = curr_window->next_window;
        END_TIMED_BLOCK(PLATFORM_LOOP);
        
        DUMP_TIMINGS();
    }
    
    return 0;
}


#define PLACEHOLDER_WINDOW_NAME L"PlaceholderName"

void PlatformRegisterDom(void* dom)
{
    PlatformWindow* created_window = win32_create_window(platform.windows, (char*)PLACEHOLDER_WINDOW_NAME);
    created_window->window_dom = dom;
    
    created_window->next_window = platform.first_window;
    platform.first_window = created_window;
}

#endif
