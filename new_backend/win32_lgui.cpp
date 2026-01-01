internal u16 win32_hash_window_handle(HWND handle)
{
    u64 handle_raw = (u64)handle;
    u16 hash = handle_raw ^ (handle_raw >> 16) ^ (handle_raw >> 32) ^ (handle_raw >> 48);
    return hash & WIN32_WINDOW_HASHMAP_MASK;
}

internal PlatformWindow* win32_find_window_from_handle(HWND handle)
{
    u16 handle_hash = win32_hash_window_handle(handle);
    PlatformWindow* curr = LGUI_BACKEND.win32.window_handle_hashmap[handle_hash];
    
    while(curr)
    {
        if(curr->win32.handle == handle)
        {
            return curr;
        }
        
        curr = curr->win32.next_with_same_hash;
    }
    
    return NULL;
}

internal LRESULT CALLBACK win32_service_window_proc(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
    LRESULT result = 0;

    switch(message)
    {
        case(LGUI_CREATE_WINDOW):
        {
            PlatformWindow* created = (PlatformWindow*)w_param; 
            
            debug_assert(created, "Window pointer should be present!\n");
            created->win32.handle = (HWND)CreateWindowExW(0, (LPCWSTR)CLIENT_WINDOW_CLASS_NAME, (LPCWSTR)L"Default",
                                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, (i32)created->w,
                                            (i32)created->h, 0, 0, LGUI_BACKEND.win32.module_handle, 0);
            result = (LRESULT)created->win32.handle;
            
            // Now that we have the handle we can put the window in the hashmap
            u16 handle_hash = win32_hash_window_handle(created->win32.handle);
            
            PlatformWindow* curr_occupant = LGUI_BACKEND.win32.window_handle_hashmap[handle_hash];
            created->win32.next_with_same_hash = curr_occupant;
            LGUI_BACKEND.win32.window_handle_hashmap[handle_hash] = created;
            break;
        }
        case(LGUI_DESTROY_WINDOW):
        {
            DestroyWindow((HWND)w_param);
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

internal LRESULT CALLBACK win32_client_window_proc(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
    LRESULT result = 0;
    
    PlatformWindow* processed_window = win32_find_window_from_handle(window_handle);

    GuiContext* event_context = NULL;
    
    if(processed_window)
    {
        event_context = processed_window->context;
    }

    if(event_context)
    {
        while(!LGuiLock(&event_context->event_queue_mutex)){;}
    }

    switch(message)
    {
        case(WM_SIZING):
        {
            if(!event_context)
            {
                break;
            }

            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::WINDOW_RESIZED;
            added->WindowResize.old_size = { processed_window->width, processed_window->height };

            processed_window->flags = processed_window->flags | LGUI_WINDOW_RESIZED;
            RECT* new_size = (RECT*)l_param;
            processed_window->width = new_size->right - new_size->left;
            processed_window->height = new_size->bottom - new_size->top;

            added->WindowResize.new_size = { processed_window->width, processed_window->height };

            break;            
        }
        case(WM_SIZE):
        {
            if(!event_context)
            {
                break;
            }

            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::WINDOW_RESIZED;
            added->WindowResize.old_size = { processed_window->width, processed_window->height };
        
            processed_window->flags = processed_window->flags | LGUI_WINDOW_RESIZED;
            processed_window->width = (f32)LOWORD(l_param);
            processed_window->height = (f32)HIWORD(l_param);
            
            added->WindowResize.new_size = { processed_window->width, processed_window->height };
            
            break;
        }
        case(WM_CLOSE):
        {
            if(!event_context)
            {
                break;
            }

            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::WINDOW_CLOSE;
        
            //processed_window->flags = processed_window->flags | LGUI_CLOSE_WINDOW;
            break;
        }
        case WM_MOUSEMOVE:
        {
            if(!event_context)
            {
                break;
            }

            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::MOUSE_MOVE;
        
            f32 x = (f32)GET_X_LPARAM(l_param);
            f32 y = (f32)GET_Y_LPARAM(l_param);

            added->MouseMove.new_pos =  {x, y};

            break;
        }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        {                        
            if(!event_context)
            {
                break;
            }
            
            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::MOUSE_SCROLL;

            if(message == WM_MOUSEWHEEL)
            {
                // Note(Leo): https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel
                //            120 is microsoft's magic number for how much to scroll lol
                added->MouseScroll.scroll = { 0.0f, ((f32)((SHORT) HIWORD(w_param)) / 120.0f) };
            }
            else
            {
                // Note(Leo): x-axis scroll is inverted compared to other window managers
                added->MouseScroll.scroll = { ((f32)HIWORD(w_param) / -120.0f), 0.0f };   
            }
            
            break;
        }
        // Note(Leo): These messages only come through if there is no char attatched
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            if((u32)w_param >= VIRTUAL_KEY_COUNT)
            {
                break;
            }
            
            if(!event_context)
            {
                break;
            }

            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            
            if(message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            {
                added->type = GuiEventType::KEY_DOWN;
            }
            else
            {
                added->type = GuiEventType::KEY_UP;
            }
            
            added->Key.code = (u32)w_param;
        
            break;
        }
        case LGUI_KEY_MESSAGE:
        {
            MSG* messages = (MSG*)w_param;
            
            if((u32)messages[0].wParam >= VIRTUAL_KEY_COUNT)
            {
                break;
            }
            
            if(!event_context)
            {
                break;
            }
            
            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            
            if(messages[0].message == WM_KEYDOWN || messages[0].message == WM_SYSKEYDOWN)
            {
                added->type = GuiEventType::KEY_DOWN;
            }
            else
            {
                added->type = GuiEventType::KEY_UP;
            }
            
            added->Key.code = (u32)messages[0].wParam;
            LGuiConsumeUTF16ToUTF32((u16*)&messages[1].wParam, &added->Key.key_char, 4);
            
            break;   
        }
        case WM_IME_CHAR:
        {
            if(!event_context)
            {
                break;
            }
        
            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::KEY_DOWN;
            added->Key.code = K_VIRTUAL;
            
            LGuiConsumeUTF16ToUTF32((u16*)&w_param, &added->Key.key_char, 4);
            
            break;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            if(!event_context)
            {
                break;
            }
            
            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::MOUSE_DOWN;
            added->Click.position = {(f32)GET_X_LPARAM(l_param), (f32)GET_Y_LPARAM(l_param)};
            
            if(message == WM_LBUTTONDOWN)
            {
                added->Click.button = MouseButton::LEFT;
            }
            else if(message == WM_RBUTTONDOWN)
            {
                added->Click.button = MouseButton::RIGHT;
            }
            else if(message == WM_MBUTTONDOWN)
            {
                added->Click.button = MouseButton::MIDDLE;
            }
        
            break;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            if(!event_context)
            {
                break;
            }
            
            GuiEvent* added = (GuiEvent*)Enqueue(&event_context->events);
            added->target_window = processed_window;
            added->type = GuiEventType::MOUSE_UP;
            added->Click.position = {(f32)GET_X_LPARAM(l_param), (f32)GET_Y_LPARAM(l_param)};
            
            if(message == WM_LBUTTONUP)
            {
                added->Click.button = MouseButton::LEFT;
            }
            else if (message == WM_RBUTTONUP)
            {
                added->Click.button = MouseButton::RIGHT;
            }
            else if (message == WM_MBUTTONUP)
            {
                added->Click.button = MouseButton::MIDDLE;
            }
        
            break;    
        }
        default:
        {
            // Note(Leo): There needs to be an unlock here because it seems that windows will randomly call something else
            //            from inside the default procedure which calls back to this same function recursively, at which point
            //            the event queue will be locked since the outer call still has it locked freezing the program.
            if(event_context)
            {
                LGuiUnlock(&event_context->event_queue_mutex);
                event_context = NULL;
            }
            result = DefWindowProcW(window_handle, message, w_param, l_param);
            break;
        }
    }
    
    if(event_context)
    {
        LGuiUnlock(&event_context->event_queue_mutex);
    }

    return result;
}

void win32_event_loop()
{
    while(true)
    {
        MSG messages[2] = {};
        GetMessageW(&messages[0], 0, 0, 0);
        
        // Note(Leo): To ensure that we always associate WM_KEY messages with WM_CHAR messages we have a special branch
        if(messages[0].message == WM_KEYDOWN || messages[0].message == WM_KEYUP ||
            messages[0].message == WM_SYSKEYDOWN || messages[0].message == WM_SYSKEYUP)
        {
            // Puts the associated WM_CHAR message into the queue.
            TranslateMessage(&messages[0]);
            
            // We need to combine the two messages into 1 to avoid cases where we switch to a new frame in beween messages
            // causing half the event to be lost.
            // Grab the char message (if there was one)
            if(PeekMessageW(&messages[1], messages[0].hwnd, WM_CHAR, WM_CHAR, PM_REMOVE))
            {
                //SendMessageW(messages[0].hwnd, LGUI_KEY_MESSAGE, (WPARAM)messages, NULL);
                MSG temp = {};
                temp.hwnd = messages[0].hwnd;
                temp.message = LGUI_KEY_MESSAGE;
                temp.wParam = (WPARAM)&messages[0];
                temp.time = messages[0].time;
                temp.pt = messages[0].pt;
                
                DispatchMessageW(&temp);
            }
            else if(PeekMessageW(&messages[1], messages[0].hwnd, WM_SYSCHAR, WM_SYSCHAR, PM_REMOVE))
            {
                MSG temp = {};
                temp.hwnd = messages[0].hwnd;
                temp.message = LGUI_KEY_MESSAGE;
                temp.wParam =(WPARAM)&messages[0];
                temp.time = messages[0].time;
                temp.pt = messages[0].pt;
                
                DispatchMessageW(&temp);
            }
            else // Key had no associated char
            {
                DispatchMessageW(&messages[0]);
            }
            
            continue;
        }
          
        TranslateMessage(&messages[0]);
        DispatchMessageW(&messages[0]);
    }
}

internal void win32_capture_main_thread(ClientMain capture_callback)
{
    LGUI_BACKEND.win32.using_event_thread = true;
        
    WNDCLASSW service_window_class = {};
    service_window_class.lpfnWndProc = win32_service_window_proc;
    service_window_class.hInstance = LGUI_BACKEND.win32.module_handle;
    service_window_class.lpszClassName = reinterpret_cast<LPCWSTR>(SERVICE_WINDOW_CLASS_NAME);
    
    LGUI_BACKEND.win32.service_window_class_atom = RegisterClassW(&service_window_class);

    LGUI_BACKEND.win32.service_window_handle = CreateWindowExW(0, reinterpret_cast<LPCWSTR>(SERVICE_WINDOW_CLASS_NAME),
                                                L"LGUI_SERVICE_WINDOW", 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                                CW_USEDEFAULT, 0, 0, LGUI_BACKEND.win32.module_handle, 0);

    // Creating the new thread we will return with
    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)capture_callback, 0, 0, &LGUI_BACKEND.win32.service_thread_id);

    win32_event_loop();
}

i32 win32_event_poll_thread(void* arg)
{
    WNDCLASSW service_window_class = {};
    service_window_class.lpfnWndProc = win32_service_window_proc;
    service_window_class.hInstance = LGUI_BACKEND.win32.module_handle;
    service_window_class.lpszClassName = reinterpret_cast<LPCWSTR>(SERVICE_WINDOW_CLASS_NAME);
    
    LGUI_BACKEND.win32.service_window_class_atom = RegisterClassW(&service_window_class);

    LGUI_BACKEND.win32.service_window_handle = CreateWindowExW(0, reinterpret_cast<LPCWSTR>(SERVICE_WINDOW_CLASS_NAME),
                                                L"LGUI_SERVICE_WINDOW", 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                                CW_USEDEFAULT, 0, 0, LGUI_BACKEND.win32.module_handle, 0);
    win32_event_loop();
        
    return 1;
}

internal b32 win32_init_lgui(u64 flags)
{
    SYSTEM_INFO sys_info = {};
    GetSystemInfo(&sys_info);

    WINDOWS_PAGE_SIZE = static_cast<uptr>(sys_info.dwPageSize);
    WINDOWS_PAGE_MASK = WINDOWS_PAGE_SIZE - 1;
    
    // Note(Leo): REQURIED otherwise our windows will be blurry
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    
    LGUI_BACKEND.win32.module_handle = GetModuleHandleA(0);
    
    // Note(Leo): Used for all windows created for the user.
    WNDCLASSW client_window_class = {};
    client_window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    client_window_class.lpfnWndProc = win32_client_window_proc;
    client_window_class.hInstance = LGUI_BACKEND.win32.module_handle;
    client_window_class.lpszClassName = reinterpret_cast<LPCWSTR>(CLIENT_WINDOW_CLASS_NAME);
    
    LGUI_BACKEND.win32.client_window_class_atom = RegisterClassW(&client_window_class);

    if(flags & CREATE_EVENT_THREAD)
    {
        LGUI_BACKEND.win32.using_event_thread = true;
    
        // Creating the new thread we will return with
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)win32_event_poll_thread, 0, 0, &LGUI_BACKEND.win32.service_thread_id);
        
        // Note(Leo): Not sure if this is neccesary but I believe it is since some input messages only come to the main thread
        AttachThreadInput(GetCurrentThreadId(), LGUI_BACKEND.win32.service_thread_id, true);
        
        // Note(Leo): Wait for the thread to startup before continuing
        while(!LGUI_BACKEND.win32.service_window_handle)
        {
            ;
        }
    }
    
    return 1;
}

internal void win32_create_window(PlatformWindow* created)
{
    if(LGUI_BACKEND.win32.using_event_thread)
    {
        SendMessageW(LGUI_BACKEND.win32.service_window_handle, LGUI_CREATE_WINDOW, (WPARAM)created, 0);
    }
}