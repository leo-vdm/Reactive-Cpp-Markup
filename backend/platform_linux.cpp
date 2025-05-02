#if defined(__linux__) && !defined(_WIN32)
#define INSTRUMENT_IMPLEMENTATION 1
#include "platform.h"
#include <stdio.h>
#include <iostream>
#include <cassert>
#include <X11/Xlib.h>
#include <unistd.h>
#include <libgen.h>


Display* x_display = {};
Visual* x_visual = {};
XDefaultValues x_defaults = {};
Atom x_wm_delete_message = {};

float SCROLL_MULTIPLIER; 

const char* linux_required_vk_extensions[] = {VK_E_KHR_SURFACE_NAME, VK_E_KHR_XLIB_SURFACE_NAME};

// Left/right scroll buttons for XButtonEvent
#define Button6 6
#define Button7 7

PlatformWindow* linux_create_window(Arena* windows_arena)
{
    #define WINDOW_WIDTH 800
    #define WINDOW_HEIGHT 400
    
    Window x_created_window = {};
    x_created_window = XCreateWindow(x_display, x_defaults.default_root_window, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, InputOutput, x_visual, 0, 0);
    
    XSelectInput(x_display, x_created_window, ExposureMask | KeyPressMask | KeyReleaseMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask  | StructureNotifyMask);
    
    // Note(Leo): Required here otherwise WM will just kill the app when the close window button is pressed without notifying us
    XSetWMProtocols(x_display, x_created_window, &x_wm_delete_message, 1);
    
    XMapWindow(x_display, x_created_window);
    XFlush(x_display);
    
    PlatformWindow* created_window = (PlatformWindow*)Alloc(windows_arena, sizeof(PlatformWindow), zero());
    created_window->window_handle = x_created_window;
    created_window->window_gc = XCreateGC(x_display, x_created_window, 0, 0);
    created_window->width = WINDOW_WIDTH;
    created_window->height = WINDOW_HEIGHT;
    
    
    linux_vk_create_window_surface(created_window, x_display);
    
    return created_window;
}

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

// Returns flags for the runtime to know the status of the window
void linux_process_window_events(PlatformWindow* target_window)
{
    update_control_state(target_window);
    XEvent x_event;
    int return_value = 0;
    while(XPending(x_display))
    {
        XNextEvent(x_display, & x_event);
        switch(x_event.type)
        {
            case(DestroyNotify):
            {
                if(x_event.xdestroywindow.window == target_window->window_handle)
                {
                return_value = return_value | QUIT_WINDOW;
                }
                break;
            }
            case(ConfigureNotify):
            {
                target_window->width = x_event.xconfigure.width;
                target_window->height = x_event.xconfigure.height;
                return_value = return_value | RESIZED_WINDOW;
                //XSync(x_display, 0);
                //XFlush(x_display);
                break;
            }
            /*
            case(Expose):
            {
                int x = x_event.xexpose.x;
                int y = x_event.xexpose.y;
                int width = x_event.xexpose.width;
                int height = x_event.xexpose.height;
                XFillRectangle(x_display, target_window->window_handle, target_window->window_gc, x, y, width, height);
                break;
            }
            */
            case(KeyPress):
            {
                int key_code = x_event.xkey.keycode;
                KeySym x_key = XLookupKeysym(&(x_event.xkey), 0);
                char* key_char = XKeysymToString(x_key);
                printf("Keycode down: %d, Char: %c\n", key_code, *key_char);
                break;
            }
            case(KeyRelease):
            {
                int key_code = x_event.xkey.keycode;
                printf("Keycode up: %d", key_code);
                break;
            }
            case(ButtonPress):
            {
                if (x_event.xbutton.button == Button1) { target_window->controls.mouse_left_state = MouseState::DOWN_THIS_FRAME; }
                else if (x_event.xbutton.button == Button2) { target_window->controls.mouse_middle_state = MouseState::DOWN_THIS_FRAME; }
                else if (x_event.xbutton.button == Button3) { target_window->controls.mouse_right_state = MouseState::DOWN_THIS_FRAME; }
                else if (x_event.xbutton.button == Button4) { target_window->controls.scroll_dir = { 0.0f, SCROLL_MULTIPLIER }; }
                else if (x_event.xbutton.button == Button5) { target_window->controls.scroll_dir = { 0.0f, -1.0f * SCROLL_MULTIPLIER }; }
                else if (x_event.xbutton.button == Button6) { target_window->controls.scroll_dir = { SCROLL_MULTIPLIER, 0.0f }; }
                else if (x_event.xbutton.button == Button7) { target_window->controls.scroll_dir = { -1.0f * SCROLL_MULTIPLIER, 0.0f }; }
                break;
            }
            case(ButtonRelease):
            {
                if (x_event.xbutton.button == Button1) { target_window->controls.mouse_left_state = MouseState::UP_THIS_FRAME; }
                else if (x_event.xbutton.button == Button2) { target_window->controls.mouse_middle_state = MouseState::UP_THIS_FRAME; }
                else if (x_event.xbutton.button == Button3) { target_window->controls.mouse_right_state = MouseState::UP_THIS_FRAME; }
                break;
            }
            case MotionNotify:
            {
                float x = x_event.xmotion.x;
                float y = x_event.xmotion.y;
                
                target_window->controls.cursor_delta = { x - target_window->controls.cursor_pos.x, y - target_window->controls.cursor_pos.y };
                target_window->controls.cursor_pos = {x, y};
            }
            case(ClientMessage):
            {
                // Window manager has requested a close
                if(x_event.xclient.data.l[0] == x_wm_delete_message)
                {
                    return_value = return_value | QUIT_WINDOW;
                }
                break;
            }
            default:
                break;
        }
    }
    fflush(stdout);
    
    target_window->flags = target_window->flags | return_value;
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

char* linux_get_execution_dir()
{
    #define MAX_PATH_LEN 2000
    char* executable_path = (char*)AllocScratch(sizeof(char)*MAX_PATH_LEN, no_zero());
    if(readlink("/proc/self/exe", executable_path, MAX_PATH_LEN) == MAX_PATH_LEN)
    {
        assert(0);
        return NULL;
    }    
    int executable_dir_len = find_last_of(executable_path, '/');
    executable_path[executable_dir_len] = '\0';
    return executable_path;
}

FILE* linux_open_relative_file_path(const char* relative_path, const char* open_params)
{
    char* working_dir = linux_get_execution_dir();
    int desired_len = snprintf(NULL, 0, "%s/%s", working_dir, relative_path);
    desired_len++; // +1 to make space for \0
    char* file_path = (char*)AllocScratch(desired_len, no_zero());
    sprintf(file_path, "%s/%s", working_dir, relative_path);

    FILE* opened = fopen(file_path, open_params);
    DeAllocScratch(file_path);
    DeAllocScratch(working_dir);
    
    return opened;
}

FileSearchResult* linux_find_markup_binaries(Arena* search_results_arena, Arena* search_result_values_arena)
{
    char* working_dir = linux_get_execution_dir();
    SearchDir(search_results_arena, search_result_values_arena, working_dir, ".bin");
    FileSearchResult* first = (FileSearchResult*)search_results_arena->mapped_address;
    
    DeAllocScratch(working_dir);
    return first;
}

FileSearchResult* linux_find_image_resources(Arena* search_results_arena, Arena* search_result_values_arena)
{
    FileSearchResult* first = (FileSearchResult*)search_results_arena->next_address;
    char* working_dir = linux_get_execution_dir();

    #define RESOURCE_DIR_NAME "resources/images"
    int desired_len = snprintf(NULL, 0, "%s/%s", working_dir, RESOURCE_DIR_NAME);
    desired_len++; // + 1 to fit \0
    char* resource_dir_path = (char*)AllocScratch(desired_len * sizeof(char), no_zero());
    sprintf(resource_dir_path, "%s/%s", working_dir, RESOURCE_DIR_NAME);
    DeAllocScratch(working_dir);
    
    SearchDir(search_results_arena, search_result_values_arena, resource_dir_path, "");
    
    DeAllocScratch(resource_dir_path);
    return first;
}

struct linux_platform_state
{
    Arena master_arena;  
    Arena* windows;
    Arena* pointer_arrays;
    Arena* search_results;
    Arena* search_result_values;

    Arena* runtime_master_arena;
    
    PlatformWindow* first_window;
};

void linux_destroy_window(Arena* windows_arena, PlatformWindow* window)
{
    XDestroyWindow(x_display, window->window_handle);
    
    vk_destroy_window_surface(window);
    
    DeAlloc(windows_arena, window);
}

linux_platform_state platform;

int main()
{   
    platform = {};
    InitScratch(sizeof(char)*1000000);
    platform.master_arena = CreateArena(1000*sizeof(Arena), sizeof(Arena));
    
    SCROLL_MULTIPLIER = 30;
    
    x_display = XOpenDisplay(NULL);
    
    if(!x_display)
    {
        printf("ERROR: Failed to open x-display while initializing platform\n");
        return 1;
    }
    
    int default_screen = XDefaultScreen(x_display);
    
    x_visual = XDefaultVisual(x_display, default_screen);
    
    x_defaults = {};
    x_defaults.default_screen = default_screen;
    x_defaults.default_depth = XDefaultDepth(x_display, default_screen);
    x_defaults.default_root_window = XDefaultRootWindow(x_display);
    x_wm_delete_message = XInternAtom(x_display, "WM_DELETE_WINDOW", False);
    
    InitializeFontPlatform(&(platform.master_arena), 0);
    
    FILE* default_font = linux_open_relative_file_path("resources/fonts/default.ttf", "rb");
    FontPlatformLoadFace("platform_default_font.ttf", default_font);
    fclose(default_font);
    
    
    FILE* combined_shader = linux_open_relative_file_path("compiled_shaders/combined_shader.spv", "rb");
        
    if(!combined_shader)
    {
        printf("Error: Shaders could not be loaded!\n");
        return 1;
    }
        
    int required_extension_count = sizeof(linux_required_vk_extensions) / sizeof(char**);
    InitializeVulkan(&(platform.master_arena), linux_required_vk_extensions, required_extension_count, combined_shader);
    
    fclose(combined_shader);
    
    platform.search_results = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.search_results) = CreateArena(sizeof(FileSearchResult)*1000, sizeof(FileSearchResult));
    
    platform.search_result_values = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.search_result_values) = CreateArena(sizeof(char)*100000, sizeof(char));
    
    FileSearchResult* first_binary = linux_find_markup_binaries(platform.search_results, platform.search_result_values);
    
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
    
    FileSearchResult* first_image = linux_find_image_resources(platform.search_results, platform.search_result_values);
    FileSearchResult* curr_image = first_image;
    while(curr_image->file_path)
    {
        FILE* opened = fopen(curr_image->file_path, "rb");
        RenderplatformLoadImage(opened, curr_image->file_name);
        
        fclose(opened);
        curr_image++;
    }
    
    PlatformWindow* curr_window = platform.first_window;
    
    Arena* temp_renderque = (Arena*)Alloc(runtime.master_arena, sizeof(Arena), zero());
    *temp_renderque = CreateArena(sizeof(Element) * 10000, sizeof(Element));
    
    while(true)
    {
        BEGIN_TIMED_BLOCK(PLATFORM_LOOP);
        if(!curr_window)
        {
            curr_window = platform.first_window;
        }    
        linux_process_window_events(platform.first_window);
        
        //print_input_state(platform.first_window);    
        
        if(curr_window->flags)
        {
            if(curr_window->flags & RESIZED_WINDOW)
            {
                if(RenderplatformSafeToDelete(curr_window))
                {
                    vk_window_resized(curr_window);
                    curr_window->flags = 0;
                    continue;
                }
            }
            if(curr_window->flags & DEAD_WINDOW)
            {
                if(RenderplatformSafeToDelete(curr_window))
                {
                    linux_destroy_window(platform.windows, platform.first_window);                
                    break;
                }            
            }
            if(curr_window->flags & QUIT_WINDOW)
            {
                curr_window->flags = DEAD_WINDOW;
            }
            continue;
        }
    
        BEGIN_TIMED_BLOCK(TICK_AND_BUILD);
        Arena* final_renderque = RuntimeTickAndBuildRenderque(temp_renderque, (DOM*)curr_window->window_dom, &curr_window->controls, curr_window->width, curr_window->height);
        END_TIMED_BLOCK(TICK_AND_BUILD);
        
        BEGIN_TIMED_BLOCK(DRAW_WINDOW);
        RenderplatformDrawWindow(platform.first_window, final_renderque);
        ResetArena(temp_renderque);
        END_TIMED_BLOCK(DRAW_WINDOW);
        
        RuntimeClearTemporal((DOM*)curr_window->window_dom);
        
        curr_window = curr_window->next_window;
        
        END_TIMED_BLOCK(PLATFORM_LOOP);
        //DUMP_TIMINGS();
    }
    
    printf("Exiting\n");
    
    return 0;
}

void PlatformRegisterDom(void* dom)
{
    PlatformWindow* created_window = linux_create_window(platform.windows);
    created_window->window_dom = dom;
    
    created_window->next_window = platform.first_window;
    platform.first_window = created_window;
}


#endif
