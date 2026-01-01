
#if !LGUI_HEADER
#define LGUI_HEADER 1

#include <cstdio>

// OS Specific header imports
#if PLATFORM_WINDOWS
    #include <windows.h>
    #include <windowsx.h>

#elif PLATFORM_LINUX_X11 || PLATFORM_LINUX_WL
#define PLATFORM_LINUX 1
    #include "x86intrin.h"
    #include <unistd.h>
    #include <dlfcn.h>
    
    #if PLATFORM_LINUX_X11
        #include <xcb/xcb.h>
    #endif // PLATFORM_LINUX_X11
    #if PLATFORM_LINUX_WL
        #include <wayland-client-core.h>
        #include <xkbcommon/xkbcommon.h>
        #include <xkbcommon/xkbcommon-compose.h>
    #endif // PLATFORM_LINUX_WL
    
#elif PLATFORM_ANDROID
    #include "x86intrin.h"
    #include <unistd.h>
    #include <android/native_window_jni.h>
    #include <android/asset_manager_jni.h>
    #include <android/log.h>
    #undef printf()
    #define printf(...) __android_log_print(ANDROID_LOG_INFO, "RCM", __VA_ARGS__);
    
#elif PLATFORM_AGNOSTIC

#else
    #pragma message("No platform selected to compile for!")
    #pragma message("To select your platform, define one of the following constants as 1 in your compiler as a pre-processor flag/value")
    #pragma message("For Win10/Win11")
    #pragma message("PLATFORM_WINDOWS")
    #pragma message("For any linux, you should define PLATFORM_LINUX_WL (wayland) and/or PLATFORM_LINUX_X11 to select which")
    #pragma message("one to compile for or to compile with support for both.")
    #pragma message("For android")
    #pragma message("PLATFORM_ANDROID")
    #pragma message("For a barebones one (still requires thread_local support)")
    #pragma message("PLATFORM_AGNOSTIC")
    #error Could not determine platform to compile for!
#endif

#include "prim_types.h"
#include "arena.h"
#include "arena_queue.h"
#include "key_codes.h"

struct PlatformWindow;
struct GuiEvent;
struct GuiContext;

struct LGUI_VK_Window; // For storing VK related attributes
struct LGUI_SW_Window; // For storing software raster related attributes

enum class LGUI_RENDERER
{
    NONE,
    VULKAN,
    SOFTWARE
};

enum class LGUI_DISPLAY_MANAGER
{
    NONE,
    ANDROID,
    WINDOWS,
    WAYLAND,
    XORG,
};

typedef int(*ClientMain)();

void WindowsCaptureMainThread(ClientMain capture_callback);
b32 InitializeLGuiPlatform(u64 flags = 0);

// Returns the number of bytes that were consumed
u32 LGuiConsumeUTF16ToUTF32(const u16* utf16_buffer, u32* codepoint, u32 buffer_length);

#define InitializeVulkanRenderer(...) (false)
#define InitializeSoftwareRenderer(...) (false)

typedef u32 VirtualKeyCode;

enum class MouseButton
{
    NONE,
    LEFT,
    MIDDLE,
    RIGHT,
};

enum class MouseState 
{
    UP,
    DOWN,
    UP_THIS_FRAME, // The mouse has switched from being down last frame to now being up
    DOWN_THIS_FRAME, // The mouse has switched from being up last frame to now being down
};

enum class GuiEventType
{
    NONE,
    KEY_DOWN,
    KEY_REPEAT,
    KEY_UP,
    MOUSE_MOVE,
    MOUSE_SCROLL,
    MOUSE_DOWN,
    MOUSE_UP,
    VIRTUAL_KEYBOARD, // Mostly used for mobile
    WINDOW_RESIZED,
    WINDOW_CLOSE,
};

struct GuiEvent
{
    GuiEventType type;
    PlatformWindow* target_window;
    union
    {
        struct
        {
            u32 key_char;   // UTF 32 char this key represents (indicated by the os)
            VirtualKeyCode code; // The physical key on the keyboard
        } Key;
        struct
        {
            vec2 scroll;
        } MouseScroll;
        struct
        {
            vec2 old_pos;
            vec2 new_pos;
        } MouseMove;
        struct
        {
            MouseButton button;
            vec2 position;
        } Click;
        struct
        {
            bool is_shown;
        } VirtualKeyboard;
        struct
        {
            vec2 old_size;
            vec2 new_size;
        } WindowResize;
    };
};

struct LGuiSemaphore
{
    volatile u32 value;
};

// Note(Leo): Returns true if locking succeeded and false otherwise
volatile b32 LGuiLock(LGuiSemaphore* sem);

// Returns true if unlock was succesfull and false if the object was already unlocked
volatile void LGuiUnlock(LGuiSemaphore* sem);

//// Renderer Specific Structs //// 
#if LGUI_VK
#undef InitializeVulkanRenderer
b32 InitializeVulkanRenderer(u64 flags);

struct LGUI_VK_Window
{
};

#endif // LGUI_VK
#if LGUI_SOFTWARE
#undef InitializeSoftwareRenderer
b32 InitializeSoftwareRenderer(u64 flags);

struct LGUI_SW_Window
{
};

#endif // LGUI_SOFTWARE

//////////////////////////////////

//// Platform Specific Structs ////
#if PLATFORM_WINDOWS
struct lgui_win32_window
{
    HWND handle;
    
    // For the HWND -> PlatformWindow hashmap
    PlatformWindow* next_with_same_hash; 
};
#elif PLATFORM_LINUX_X11
#elif PLATFORM_LINUX_WL
#elif PLATFORM_AGNOSTIC
#endif
///////////////////////////////////

struct PlatformWindow
{
    PlatformWindow* next;
    PlatformWindow* prev;

    // The context that this window belongs to
    GuiContext* context;
    
    union
    {
        f32 width;
        f32 w;
    };
    
    union
    {
        f32 height;
        f32 h;
    };

    u64 flags;
    
    // Window renderer specific members
    union
    {
    #if LGUI_VK
        LGUI_VK_Window vk;
    #endif
    #if LGUI_SOFTWARE
        LGUI_SW_Window sw;
    #endif
    };
    
    LGUI_RENDERER renderer_type;
    
    // Platform specific members
    union
    {
    #if PLATFORM_WINDOWS
    lgui_win32_window win32;
    #elif PLATFORM_LINUX_X11
    #elif PLATFORM_LINUX_WL
    #elif PLATFORM_AGNOSTIC
    #endif
    };
};

struct ElementGuid
{
    u64 hash;
    
    // Unique identifying items
    u64 pointer;
    u64 index;
    u64 other;
};

struct GuiInteraction
{
    ElementGuid element;
    
    MouseState l_click_progress;
    MouseState r_click_progress;
    MouseState m_click_progress;
};

struct GuiContext
{
    // Note(Leo): One queue for collecting events for next frame and one with completed events for this frame 
    ArenaQueue events;
    LGuiSemaphore event_queue_mutex;

    Arena windows;
    PlatformWindow* first_window;
    
    GuiInteraction active;
    GuiInteraction hovered;
};


PlatformWindow* CreatePlatformWindow(LGUI_RENDERER renderer_type, f32 width, f32 height);
GuiContext* CreateGuiContext(void* used_buffer = NULL, u32 used_buffer_size = 0);
void ContextNextFrame(GuiContext* context);
void SetContext(GuiContext* context);
void PollEvents();

// Returns true if there was an event to get or false if there was none
b32 GetPlatformEvent(GuiContext* context, GuiEvent* target);

//// Globals ////
#if PLATFORM_WINDOWS

// Note(Leo): MUST be pow2
#define WIN32_WINDOW_HASHMAP_SIZE 4096
#define WIN32_WINDOW_HASHMAP_MASK WIN32_WINDOW_HASHMAP_SIZE - 1

struct lgui_win32_globals
{
    HMODULE module_handle;
    ATOM client_window_class_atom;
    ATOM service_window_class_atom;
    
    b32 using_event_thread;
    HWND service_window_handle;
    DWORD service_thread_id;
    
    PlatformWindow* window_handle_hashmap[WIN32_WINDOW_HASHMAP_SIZE];
};

// Note(Leo): Needed for signalling to the window creation thread (if we are using that)
//            see https://github.com/cmuratori/dtc/ for more understanding.
#define LGUI_CREATE_WINDOW (WM_USER + 1)
#define LGUI_DESTROY_WINDOW (WM_USER + 2)
#define LGUI_KEY_MESSAGE (WM_USER + 3)

#define CLIENT_WINDOW_CLASS_NAME L"LGUI_CLIENT_WINDOW_CLASS"
#define SERVICE_WINDOW_CLASS_NAME L"LGUI_SERVICE_WINDOW_CLASS"

// Flags for the PlatformWindow struct
#define LGUI_WINDOW_RESIZED 1 << 0
#define LGUI_CLOSE_WINDOW 1 << 1

#endif // PLATFORM_WINDOWS

#define CAPTURE_PLATFORM_EVENT_LOOP 1 << 0
#define CREATE_EVENT_THREAD 1 << 1

#define VIRTUAL_KEY_COUNT 1028

enum class KeyState
{
    UP,
    DOWN,
};

struct VirtualKeyboard
{
    u8 keys[VIRTUAL_KEY_COUNT];
};

enum class CursorSource
{
    MOUSE,
    TOUCH
};

struct LGUI_PLATFORM_CONTROL_STATE
{
    // Note(Leo): Cursor pos coordinates are in the hovered window, which may be different from the focussed window.
    PlatformWindow* focused_window; // NULL if a window from another app has focus
    PlatformWindow* hovered_window; // NULL if none of our windows are hovered

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
    
    u8 cursor_count;
    
    // Note(Leo): Keyboard state is shared by all windows but only the active window gets                                 
    //            key press events
    VirtualKeyboard keyboard_state;
};

struct lgui_backend
{
    LGUI_PLATFORM_CONTROL_STATE control_state;
    LGUI_DISPLAY_MANAGER display_manager;
    
    union
    {
        #if PLATFORM_WINDOWS
        lgui_win32_globals win32;
        #elif PLATFORM_LINUX_X11
        #elif PLATFORM_LINUX_WL
        #elif PLATFORM_AGNOSTIC
        #endif
    };
    
    Arena contexts;
};

extern lgui_backend LGUI_BACKEND;

extern thread_local GuiContext* LGUI_CURR_CONTEXT;

/////////////////

#endif // !LGUI_HEADER
