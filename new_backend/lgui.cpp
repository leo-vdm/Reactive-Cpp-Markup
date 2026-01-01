// Note(Leo): Unity build, #include this file into you main cpp file to build lgui into your project.
//            lgui.h can be included in other places if your project isnt using a unity build.

#include "lgui.h"

#if !LGUI_IMPLEMENTATION
#define LGUI_IMPLEMENTATION 1

/// Globals ///
lgui_backend LGUI_BACKEND = {};

thread_local GuiContext* LGUI_CURR_CONTEXT = {};
///////////////

/// Includes ///
#include "arena.cpp"
#include "arena_queue.cpp"

#if PLATFORM_WINDOWS
    #include "win32_lgui.cpp" 
#endif
////////////////

// Note(Leo): Shouldnt return until program exits.
void WindowsCaptureMainThread(ClientMain capture_callback)
{
    if(!capture_callback)
    {
        debug_assert(capture_callback, "Error: Capture callback must NOT be null!\n");
        return;
    }
    
    #if PLATFORM_WINDOWS    
    win32_capture_main_thread(capture_callback);
    #else
    // Note(Leo): on other platforms we dont have the same behaviour as on windows so we dont actually need to
    //            spin a new thread. Really this SHOULDNT be called on other platforms but this is here for lazy
    //            people who cant be bothered making platform-specific behaviours.
    capture_callback();
    #endif
}

b32 InitializeLGuiPlatform(u64 flags)
{   
    InitScratch(sizeof(char)*1000000);
    
    LGUI_BACKEND.display_manager = LGUI_DISPLAY_MANAGER::NONE;
    
    // Note(Leo): This part is a bit hairy with the #if's but its better than spreading it out to all the different platforms
    //            to try and detect which platform we are on.
    #if PLATFORM_WINDOWS
        LGUI_BACKEND.display_manager = LGUI_DISPLAY_MANAGER::WINDOWS;
        
        b32 success = win32_init_lgui(flags);
        if(!success)
        {
            return 0;
        }
        
    #endif // PLATFORM_WINDOWS
    #if PLATFORM_LINUX
        const char* session_type = getenv("XDG_SESSION_TYPE");
        if(!session_type)
        {
            return 0;
        }
        
        // Note(Leo): Prefer Wayland over X 
        #if PLATFORM_LINUX_WL
        
        if(getenv("WAYLAND_DISPLAY"))
        {
            LGUI_BACKEND.display_manager = LGUI_DISPLAY_MANAGER::WAYLAND;
        }
        
        #endif // PLATFORM_LINUX_WL
        #if PLATFORM_LINUX_X11
        
        if(getenv("DISPLAY") && LGUI_BACKEND.display_manager == LGUI_DISPLAY_MANAGER::NONE)
        {
            LGUI_BACKEND.display_manager = LGUI_DISPLAY_MANAGER::XORG;
        }
        
        #endif // PLATFORM_LINUX_X11
    #endif // PLATFORM_LINUX
    #if PLATFORM_ANDROID
        LGUI_BACKEND.display_manager = LGUI_DISPLAY_MANAGER::ANDROID;
    #endif // PLATFORM_ANDROID

    LGUI_BACKEND.contexts = CreateArena(sizeof(GuiContext)*100, sizeof(GuiContext));

    return 1;
}

GuiContext* CreateGuiContext(void* used_buffer, u32 used_buffer_size)
{
    GuiContext* created = NULL;
    
    if(!used_buffer)
    {
        created = (GuiContext*)Alloc(&LGUI_BACKEND.contexts, sizeof(GuiContext));
        for(i32 i = 0; i < 2; i++)
        {
            created->events = CreateQueue(sizeof(GuiEvent), 10000);
        }
        
        created->windows = CreateArena(sizeof(PlatformWindow)*1000, sizeof(PlatformWindow));

    }
    else
    {
        debug_assert(0, "Todo(Leo): Actually test this branch!\n");
        if(used_buffer_size < Kilobytes(100))
        {
            printf("Buffer for creating GUI context should be at least 100kb!\n");
            return NULL;
        }
        
        u32 per_arena_size = used_buffer_size / 3;
        uptr curr_buffer_start = (uptr)used_buffer;
        
        created = (GuiContext*)Alloc(&LGUI_BACKEND.contexts, sizeof(GuiContext));
        for(i32 i = 0; i < 2; i++)
        {
            curr_buffer_start = (uptr)AlignMem(curr_buffer_start, GuiEvent);
            u32 real_size = per_arena_size - per_arena_size % sizeof(GuiEvent); // Making space for Alignment
            created->events = CreateQueue(CreateArenaWith((void*)curr_buffer_start, real_size, sizeof(GuiEvent)));
            curr_buffer_start += real_size;
        }
        
        curr_buffer_start = (uptr)AlignMem(curr_buffer_start, PlatformWindow);
        u32 real_size = per_arena_size - per_arena_size % sizeof(PlatformWindow);
        created->windows = CreateArenaWith((void*)curr_buffer_start, real_size, sizeof(PlatformWindow));
        curr_buffer_start += real_size;
    }
    
    return created;
}

void SetContext(GuiContext* context)
{
    LGUI_CURR_CONTEXT = context;
}

PlatformWindow* CreatePlatformWindow(LGUI_RENDERER renderer_type, f32 width, f32 height)
{
    PlatformWindow* created = (PlatformWindow*)Alloc(&LGUI_CURR_CONTEXT->windows, sizeof(PlatformWindow));
    created->w = width;
    created->h = height;
    created->renderer_type = renderer_type;
    created->context = LGUI_CURR_CONTEXT;
    
    #if PLATFORM_WINDOWS
    win32_create_window(created);
    #endif
    
    if(LGUI_CURR_CONTEXT->first_window)
    {
        LGUI_CURR_CONTEXT->first_window->prev = created;
    }
    
    created->next = LGUI_CURR_CONTEXT->first_window;
    LGUI_CURR_CONTEXT->first_window = created;
    
    return created;
}

b32 GetPlatformEvent(GuiContext* context, GuiEvent* target)
{
    debug_assert(target && context, "You must specify a target for events to be copied to and a context for events to be pulled from.\n");    
    if(!target || !context)
    {
        return 0;
    }
    
    // Wait to lock before grabbing
    while(!LGuiLock(&context->event_queue_mutex)){;}
    
    // Note(Leo): We pull events out of the non-active queue.
    ArenaQueue* events = &context->events;
    
    if(!events->count)
    {
        LGuiUnlock(&context->event_queue_mutex);
        return 0;
    }
    
    GuiEvent* src = (GuiEvent*)GetTail(events);
    memcpy(target, src, sizeof(GuiEvent));
    DeQueueTail(events);
    
    LGuiUnlock(&context->event_queue_mutex);
    
    // Updating platform state to reflect this event being dispatched
    
    switch(target->type)
    {
        case(GuiEventType::MOUSE_MOVE):
        {
            LGUI_BACKEND.control_state.cursor_delta = { target->MouseMove.new_pos.x - LGUI_BACKEND.control_state.cursor_pos.x,
                                                        target->MouseMove.new_pos.y - LGUI_BACKEND.control_state.cursor_pos.y
                                                      };
            target->MouseMove.old_pos = LGUI_BACKEND.control_state.cursor_pos;
            LGUI_BACKEND.control_state.cursor_pos = target->MouseMove.new_pos;
            
            break;
        }
        case(GuiEventType::MOUSE_SCROLL):
        {
            LGUI_BACKEND.control_state.scroll_dir = target->MouseScroll.scroll;
            break;
        }
        case(GuiEventType::KEY_DOWN):
        {
            LGUI_BACKEND.control_state.keyboard_state.keys[target->Key.code] = (u8)KeyState::DOWN;
            break;
        }
        case(GuiEventType::KEY_UP):
        {
            LGUI_BACKEND.control_state.keyboard_state.keys[target->Key.code] = (u8)KeyState::UP;
            break;
        }
        case(GuiEventType::MOUSE_DOWN):
        case(GuiEventType::MOUSE_UP):
        {
            MouseState state = {};
            
            if(target->type == GuiEventType::MOUSE_DOWN)
            {
                state = MouseState::DOWN_THIS_FRAME;
            }
            else
            {
                state = MouseState::UP_THIS_FRAME;
            }
        
            if(target->Click.button == MouseButton::LEFT)
            {
                LGUI_BACKEND.control_state.mouse_left_state = state;
            }
            else if(target->Click.button == MouseButton::RIGHT)
            {
                LGUI_BACKEND.control_state.mouse_right_state = state;
            }
            else if(target->Click.button == MouseButton::MIDDLE)
            {
                LGUI_BACKEND.control_state.mouse_middle_state = state;
            }
            break;
        }
        default:
        {
            
            break;
        }
    }
    

    return 1;
}

void ContextNextFrame(GuiContext* context)
{
    // Wait to lock before switching queues
    while(!LGuiLock(&context->event_queue_mutex)){;}
        
    LGuiUnlock(&context->event_queue_mutex);
}

volatile b32 LGuiLock(LGuiSemaphore* sem)
{
    debug_assert(sem, "You must provide a pointer to a semaphore!\n");

    #if PLATFORM_WINDOWS
    // Returns the initial value which should be 0, if it is not then someone else has already locked this object
    if(InterlockedCompareExchange(&sem->value, 1, 0) == 0)
    {
        return 1; 
    }
    return 0;
    
    #elif PLATFORM_LINUX || PLATFORM_ANDROID
    return __sync_bool_compare_and_swap(&sem->value, 0, 1);
    #endif
}

volatile void LGuiUnlock(LGuiSemaphore* sem)
{
    debug_assert(sem, "You must provide a pointer to a semaphore!\n");

    #if PLATFORM_WINDOWS
    InterlockedExchange(&sem->value, 0);
    #elif PLATFORM_LINUX || PLATFORM_ANDROID
    __sync_bool_compare_and_swap(&sem->value, 1, 0);
    #endif
}

u32 LGuiConsumeUTF16ToUTF32(const u16* utf16_buffer, u32* codepoint, u32 buffer_length)
{
    // Need 2 bytes to have a utf16 char
    if(buffer_length < 2)
    {
        return 0;
    }
    
    u16 high_surrogate = utf16_buffer[0];
    u16 low_surrogate = utf16_buffer[1];
    
    // Direct encoded codepoints
    if(high_surrogate < 0xD7FF || (high_surrogate > 0xE000 && high_surrogate < 0xFFFF))
    {
        *codepoint = high_surrogate;
    
        return 2;
    }
    // Double word encoded codepoints
    if(high_surrogate > 0xD800 && high_surrogate < 0xDBFF)
    {
        if(buffer_length < 4)
        {
            return 0;
        }
        *codepoint = ((0b0000001111111111 & high_surrogate) << 10) | (0b0000001111111111 & low_surrogate);
        
        return 4;
    }
    
    return 0;
}


#endif // !LGUI_IMPLEMENTATION
