#include "lgui.cpp"

int main()
{
    // Need the UI to work for our program to be of any use!
    if(!InitializeLGuiPlatform(CREATE_EVENT_THREAD))
    {
        return 1;
    }

    GuiContext* main_context = CreateGuiContext();
    SetContext(main_context);

    PlatformWindow* first = CreatePlatformWindow(LGUI_RENDERER::VULKAN, 800.0f, 600.0f);
    PlatformWindow* second = CreatePlatformWindow(LGUI_RENDERER::VULKAN, 400.0f, 600.0f);
    
    while(true)
    {
        GuiEvent event = {};
        ContextNextFrame(main_context);
        while(GetPlatformEvent(main_context, &event))
        {
            switch(event.type)
            {
                case(GuiEventType::KEY_DOWN):
                {
                    printf("Pressed key: %c\n", (char)event.Key.key_char);
                    break;
                }
                case(GuiEventType::MOUSE_DOWN):
                {
                    printf("Mouse Click at x: %f y: %f\n", event.Click.position.x, event.Click.position.y);
                    break;
                }
                case(GuiEventType::WINDOW_RESIZED):
                {
                    printf("Window resized. Old size w: %f h: %f\nNew size w: %f h: %f\n",
                            event.WindowResize.old_size.x, event.WindowResize.old_size.y,
                            event.WindowResize.new_size.x, event.WindowResize.new_size.y
                            );
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        
        Sleep(2);
        
    }
    
    return 0;
}

/*
int main()
{
    //WindowsCaptureMainThread(real_main);
    return 1;
}
*/