#if defined(__linux__) && !defined(_WIN32)
#define INSTRUMENT_IMPLEMENTATION 1
#define KEYCODE_TRANSLATION_IMPL 1
#include "platform.h"
#include <stdio.h>
#include <iostream>
#include <cassert>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <libgen.h>
#include <climits>

Display* x_display = {};
Visual* x_visual = {};
XDefaultValues x_defaults = {};
XIM x_input_method = {};
Atom x_wm_delete_message = {};

Atom x_wm_clipboard = {};
Atom x_wm_clipboard_targets = {};
Atom x_wm_utf8 = {};
Atom x_wm_clipboard_target = {};

float SCROLL_MULTIPLIER; 

const char* linux_required_vk_extensions[] = {VK_E_KHR_SURFACE_NAME, VK_E_KHR_XLIB_SURFACE_NAME};

// Left/right scroll buttons for XButtonEvent
#define Button6 6
#define Button7 7

struct linux_platform_state
{
    Arena master_arena;  
    Arena* windows;
    Arena* pointer_arrays;
    Arena* search_results;
    Arena* search_result_values;

    Arena* runtime_master_arena;
    
    PlatformWindow* first_window;
    
    char* clipboard_data;
    uint32_t clipboard_len;
    
    VirtualKeyboard keyboard_state;
};

linux_platform_state platform;

PlatformWindow* linux_create_window(Arena* windows_arena)
{
    #define WINDOW_WIDTH 800
    #define WINDOW_HEIGHT 400
    
    Window x_created_window = {};
    x_created_window = XCreateWindow(x_display, x_defaults.default_root_window, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, InputOutput, x_visual, 0, 0);
    XMapRaised(x_display, x_created_window);
    XSync(x_display, false);
    
    assert(x_input_method);
    
    XIC xic = XCreateIC(x_input_method, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, x_created_window, XNFocusWindow, x_created_window, NULL);
    // Note(Leo): Asking window manager to give keyboard focus to this new window
    XSetICFocus(xic);
    
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
    created_window->controls.keyboard_state = &platform.keyboard_state;
    created_window->window_input_context = xic;
    
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

// Note(Leo): This method only exists seperately from process_window_events so we have something to direct unwanted
//            events to when querying the clipboard since that is a multi-event negotiation which we want to do in a
//            blocking manner since its more convenient to use.
int linux_process_window_event(PlatformWindow* target_window, XEvent* x_event)
{   
    int return_value = 0;
    // Note(Leo): IME's may want some keystrokes to not be sent which is what this filters for
    if(XFilterEvent(x_event, None) == true)
    {
        return 0;
    }
    
    switch(x_event->type)
    {
        case(DestroyNotify):
        {
            if(x_event->xdestroywindow.window == target_window->window_handle)
            {
                return_value = return_value | QUIT_WINDOW;
            }
            break;
        }
        case(ConfigureNotify):
        {
            target_window->width = x_event->xconfigure.width;
            target_window->height = x_event->xconfigure.height;
            return_value = return_value | RESIZED_WINDOW;
            //XSync(x_display, 0);
            //XFlush(x_display);
            break;
        }
        case(KeyPress):
        {
            // Note(Leo): remove the control modifier since it makes stuff return control codes
            x_event->xkey.state &= ~ControlMask;
        
            // Note(Leo): 100 is an arbitrary size
            #define TEXT_BUFF_SIZE 100
            char* key_text = (char*)AllocScratch(sizeof(char)*TEXT_BUFF_SIZE);
            Status status;
            KeySym keysym = NoSymbol;
            int text_len = Xutf8LookupString(target_window->window_input_context, &x_event->xkey, key_text, TEXT_BUFF_SIZE - 1, &keysym, &status);
            if(text_len < 0)
            {
                text_len = 0;
            }
            
            int key_code = x_event->xkey.keycode;
            VirtualKeyCode translated_keycode = 0; 
            if(key_code < sizeof(KEYCODE_TRANSLATIONS))
            {
                translated_keycode = KEYCODE_TRANSLATIONS[key_code];
            }
            
            // We didnt find a translation
            if(!translated_keycode)
            {
                translated_keycode = K_UNMAPPED;
            }
        
            uint32_t buffer_len = (uint32_t)text_len;
            char* curr_char = key_text;
            
            // Note(Leo): If a key only has 1 utf8 char in it then we send the char with they keystroke event.
            //            However for keys that are multi char we assume they are IME events and so we sepereate the
            //            text and key event. We send the keystroke event first with no codepoint then all the chars
            //            as individual K_VIRTUAL keystrokes with a utf8 char in each.
            //            This is mostly for consistency with the android layer which does something similiar.
            
            Event* added = PushEvent((DOM*)target_window->window_dom);
            added->type = EventType::KEY_DOWN;
            added->Key.code = translated_keycode;
            
            if(buffer_len)
            {
                uint32_t codepoint = 0;
                uint32_t consumed = PlatformConsumeUTF8ToUTF32(curr_char, &codepoint, buffer_len);
            
                buffer_len -= consumed;
                curr_char += consumed;
                
                if(!buffer_len)
                {
                    added->Key.key_char = codepoint;
                }
                else
                {
                    added = PushEvent((DOM*)target_window->window_dom);
                    added->type = EventType::KEY_DOWN;
                    added->Key.code = K_VIRTUAL;
                    added->Key.key_char = codepoint;
                }
            }
            
            while(buffer_len)
            {
                uint32_t codepoint = 0;
                uint32_t consumed = PlatformConsumeUTF8ToUTF32(curr_char, &codepoint, buffer_len);
            
                // Hit some type of invalid codepoint
                if(!consumed)
                {
                    buffer_len -= 1;
                    curr_char += 1;
                    return 0;
                }
                
                added = PushEvent((DOM*)target_window->window_dom);
                added->type = EventType::KEY_DOWN;
                added->Key.code = K_VIRTUAL;
                added->Key.key_char = codepoint;
                
                buffer_len -= consumed;
                curr_char += consumed;
            }
            
            target_window->controls.keyboard_state->keys[translated_keycode] = (uint8_t)KeyState::DOWN;
            DeAllocScratch(key_text);
            
            break;
        }
        case(KeyRelease):
        {
            int key_code = x_event->xkey.keycode;
            VirtualKeyCode translated_keycode = 0; 
            if(key_code < sizeof(KEYCODE_TRANSLATIONS))
            {
                translated_keycode = KEYCODE_TRANSLATIONS[key_code];
            }
            
            // We didnt find a translation
            if(!translated_keycode)
            {
                translated_keycode = K_UNMAPPED;
            }
        
            Event* added = PushEvent((DOM*)target_window->window_dom);
            added->type = EventType::KEY_UP;
            added->Key.code = translated_keycode;
            
            target_window->controls.keyboard_state->keys[translated_keycode] = (uint8_t)KeyState::UP;
            break;
        }
        case(ButtonPress):
        {
            if (x_event->xbutton.button == Button1) { target_window->controls.mouse_left_state = MouseState::DOWN_THIS_FRAME; }
            else if (x_event->xbutton.button == Button2) { target_window->controls.mouse_middle_state = MouseState::DOWN_THIS_FRAME; }
            else if (x_event->xbutton.button == Button3) { target_window->controls.mouse_right_state = MouseState::DOWN_THIS_FRAME; }
            else if (x_event->xbutton.button == Button4) { target_window->controls.scroll_dir = { 0.0f, SCROLL_MULTIPLIER }; }
            else if (x_event->xbutton.button == Button5) { target_window->controls.scroll_dir = { 0.0f, -1.0f * SCROLL_MULTIPLIER }; }
            else if (x_event->xbutton.button == Button6) { target_window->controls.scroll_dir = { SCROLL_MULTIPLIER, 0.0f }; }
            else if (x_event->xbutton.button == Button7) { target_window->controls.scroll_dir = { -1.0f * SCROLL_MULTIPLIER, 0.0f }; }
            break;
        }
        case(ButtonRelease):
        {
            if (x_event->xbutton.button == Button1) { target_window->controls.mouse_left_state = MouseState::UP_THIS_FRAME; }
            else if (x_event->xbutton.button == Button2) { target_window->controls.mouse_middle_state = MouseState::UP_THIS_FRAME; }
            else if (x_event->xbutton.button == Button3) { target_window->controls.mouse_right_state = MouseState::UP_THIS_FRAME; }
            break;
        }
        case MotionNotify:
        {
            float x = x_event->xmotion.x;
            float y = x_event->xmotion.y;
            
            target_window->controls.cursor_delta = { x - target_window->controls.cursor_pos.x, y - target_window->controls.cursor_pos.y };
            target_window->controls.cursor_pos = {x, y};
        }
        case(ClientMessage):
        {
            // Window manager has requested a close
            if(x_event->xclient.data.l[0] == x_wm_delete_message)
            {
                return_value = return_value | QUIT_WINDOW;
            }
            break;
        }
        case(SelectionRequest):
        {
            XSelectionRequestEvent request = x_event->xselectionrequest;
            if(XGetSelectionOwner(x_display, x_wm_clipboard) != platform.first_window->window_handle || request.selection != x_wm_clipboard)
            {
                break;   
            }
            
            // Request for the list of types we support
            if(request.target == x_wm_clipboard_targets && request.property != None)
            {
                XChangeProperty(request.display, request.requestor, request.property, XA_ATOM, 32, PropModeReplace, (unsigned char*)&x_wm_utf8, 1);
            }
            // Request for the value of our utf8 clipboard
            else if(request.target == x_wm_utf8 && request.property != None)
            {
                XChangeProperty(request.display, request.requestor, request.property, request.target, 8, PropModeReplace, (unsigned char*)platform.clipboard_data, platform.clipboard_len);
            }
            
            XSelectionEvent reply = {};
            reply.type = SelectionNotify;
            reply.serial = request.serial;
			reply.send_event = request.send_event;
			reply.display = request.display;
			reply.requestor = request.requestor;
			reply.selection = request.selection;
			reply.target = request.target;
			reply.property = request.property;
			reply.time = request.time;
			XSendEvent(x_display, request.requestor, 0, 0, (XEvent*)&reply);
			
            break;
        }
        default:
            break;
    }

    return return_value;
}

// Returns flags for the runtime to know the status of the window
void linux_process_window_events(PlatformWindow* target_window)
{
    update_control_state(target_window);
    XEvent x_event;
    while(XPending(x_display))
    {
        XNextEvent(x_display, &x_event);
        
        target_window->flags |= linux_process_window_event(target_window, &x_event);
    }
}

void PlatformSetTextClipboard(const char* utf8_buffer, uint32_t buffer_len)
{
    if(!utf8_buffer)
    {
        return;
    }

    // If we previously had a clipboard value clear it
    if(platform.clipboard_data)
    {
        free(platform.clipboard_data);
    }
    
    XSetSelectionOwner(x_display, x_wm_clipboard, platform.first_window->window_handle, CurrentTime);
    
    platform.clipboard_data = (char*)malloc(buffer_len*sizeof(char));
    platform.clipboard_len = buffer_len;
    
    memcpy(platform.clipboard_data, utf8_buffer, buffer_len*sizeof(char));
}

char* PlatformGetTextClipboard(uint32_t* buffer_len)
{
    // If we are lucky we will own the selection and dont need to do a back and forth with X11
    if(XGetSelectionOwner(x_display, x_wm_clipboard) == platform.first_window->window_handle)
    {
        *buffer_len = platform.clipboard_len;
        
        char* content = (char*)AllocScratch(*buffer_len*sizeof(char));
        memcpy(content, platform.clipboard_data, *buffer_len*sizeof(char));
        
        return content;
    }

    XConvertSelection(x_display, x_wm_clipboard, x_wm_clipboard_targets, x_wm_clipboard, platform.first_window->window_handle, CurrentTime);

    XEvent x_event;
    uint32_t attempts = 50; // Note(Leo): This is arbitrary
    while(attempts)
    {
        attempts--;
        usleep(30); // Note(Leo): Pause for 30 microseconds each time (also arbitrary)
        while(XPending(x_display))
        {   
            XNextEvent(x_display, &x_event);
            
            if(x_event.type == SelectionNotify)
            {
                attempts = 0;
                break;
            }
            
            platform.first_window->window_handle |= linux_process_window_event(platform.first_window, &x_event);
            
            // Failed to find the event in time
            if(!attempts)
            {
                return NULL;
            }
        }
        
    }
    
    XSelectionEvent selection = x_event.xselection;
    if(selection.property == None || selection.target != x_wm_clipboard_targets)
    {
        return NULL;
    }
    
    Atom actual_type;
    int actual_format;
    uint64_t bytes;
    unsigned char* data;
    uint64_t count;
    
    XGetWindowProperty(x_display, platform.first_window->window_handle, x_wm_clipboard, 0, LONG_MAX, False, AnyPropertyType, 
                        &actual_type, &actual_format, &count, &bytes, &data);

    Atom* list = (Atom*)data;
    for(uint64_t i = 0; i < count; i++)
    {
        if(list[i] == XA_STRING)
        {
            x_wm_clipboard_target = XA_STRING;
        }
        else if(list[i] == x_wm_utf8)
        {
            x_wm_clipboard_target = x_wm_utf8;
            break;
        }
    }
    
    if(data)
    {
        XFree(data);
    }
    
    if(x_wm_clipboard_target == None)
    {
        return NULL;
    }
    
    // Signal we want the data
    XConvertSelection(x_display, x_wm_clipboard, x_wm_clipboard_target, x_wm_clipboard, platform.first_window->window_handle, CurrentTime);
    
    attempts = 50; // Note(Leo): This is arbitrary
    while(attempts)
    {
        attempts--;
        usleep(30); // Note(Leo): Pause for 30 microseconds each time (also arbitrary)
        while(XPending(x_display))
        {   
            XNextEvent(x_display, &x_event);
            
            if(x_event.type == SelectionNotify)
            {
                attempts = 0;
                break;
            }
            
            platform.first_window->window_handle |= linux_process_window_event(platform.first_window, &x_event);
           
            // Failed to find the event in time
            if(!attempts)
            {
                return NULL;
            }
        }
        
    }
    
    selection = x_event.xselection;
    if(selection.target != x_wm_clipboard_target)
    {
        return NULL; 
    }
    
    XGetWindowProperty(x_display, platform.first_window->window_handle, x_wm_clipboard, 0, LONG_MAX, False, AnyPropertyType, 
                        &actual_type, &actual_format, &count, &bytes, &data);
    
    if(!data)
    {
        return NULL;
    }
    
    *buffer_len = static_cast<uint32_t>(count);
        
    char* content = (char*)AllocScratch(*buffer_len*sizeof(char));
    memcpy(content, data, *buffer_len*sizeof(char));
    XFree(data);
    
    
    return content;
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


PlatformFile PlatformOpenFile(const char* file_path, Arena* bin_arena)
{
    PlatformFile loaded = {};
    
    FILE* opened = linux_open_relative_file_path(file_path, "rb");
    
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

FileSearchResult* linux_find_markup_binaries(Arena* search_results_arena, Arena* search_result_values_arena)
{
    char* working_dir = linux_get_execution_dir();
    SearchDir(search_results_arena, search_result_values_arena, working_dir, ".bin");
    FileSearchResult* first = (FileSearchResult*)search_results_arena->mapped_address;
    
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


void linux_destroy_window(Arena* windows_arena, PlatformWindow* window)
{
    XDestroyWindow(x_display, window->window_handle);
    
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
    platform = {};
    InitScratch(sizeof(char)*1000000);
    platform.master_arena = CreateArena(1000*sizeof(Arena), sizeof(Arena));
    
    SCROLL_MULTIPLIER = 30;
    
    setlocale(LC_ALL, "");
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
    
    x_wm_clipboard = XInternAtom(x_display , "CLIPBOARD", False);
    x_wm_clipboard_targets = XInternAtom(x_display , "TARGETS", False);
    x_wm_utf8 = XInternAtom(x_display, "UTF8_STRING", False);
    x_wm_clipboard_target = None;
    
    // Note(Leo): Input system was adapted from this https://gist.github.com/baines/5a49f1334281b2685af5dcae81a6fa8a
    x_input_method = XOpenIM(x_display, 0, 0, 0);
    
    if(!x_input_method)
    {
        // fallback to internal input method
        XSetLocaleModifiers("@im=none");
        x_input_method = XOpenIM(x_display, 0, 0, 0);
    }
    
    InitializeFontPlatform(&(platform.master_arena), 0);
    PlatformInitKeycodeTranslations();
    
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
    
    // Note(Leo): We use 1 renderque for all the windows then when we loop back we swap them so that we write into the 
    //            other. This is done so we can safely access the sizing data of elements from the last frame.
    Arena* renderques[2] = {};  
    renderques[0] = (Arena*)Alloc(runtime.master_arena, sizeof(Arena), zero());
    renderques[1] = (Arena*)Alloc(runtime.master_arena, sizeof(Arena), zero());
    *renderques[0] = CreateArena(sizeof(Element) * 10000, sizeof(Element));
    *renderques[1] = CreateArena(sizeof(Element) * 10000, sizeof(Element));
    bool used_renderque = false;
    
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
        
        linux_process_window_events(curr_window);
                
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
                    linux_destroy_window(platform.windows, curr_window);                
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
        Arena* final_renderque = RuntimeTickAndBuildRenderque(renderques[used_renderque], (DOM*)curr_window->window_dom, &curr_window->controls, curr_window->width, curr_window->height);
        END_TIMED_BLOCK(TICK_AND_BUILD);
        
        BEGIN_TIMED_BLOCK(DRAW_WINDOW);
        RenderplatformDrawWindow(curr_window, final_renderque);
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
