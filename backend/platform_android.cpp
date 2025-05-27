#include <jni.h>
#include <string>
#define KEYCODE_TRANSLATION_IMPL 1
#include "platform.h"
#if PLATFORM_ANDROID
#include <android/log.h>
//#include <android_native_app_glue.h>
#include <pthread.h>
#include <android/native_window_jni.h>
#include <android/asset_manager_jni.h>

// Note(Leo): GCC Specific for now
struct android_semaphore
{
    int value;
};

void android_increment(android_semaphore* sem, int added)
{
    __sync_fetch_and_add(&sem->value, added);
}

inline void android_increment(android_semaphore* sem)
{
    android_increment(sem, 1);
}

void android_decrement(android_semaphore* sem, int subbed)
{
    __sync_fetch_and_sub(&sem->value, subbed);
}

inline void android_decrement(android_semaphore* sem)
{
    android_decrement(sem, 1);
}

bool android_compare(android_semaphore* sem, int value)
{
    return __sync_bool_compare_and_swap(&sem->value, value, value);
}

// True if lock was succesfull false otherwise
bool android_lock(android_semaphore* sem)
{
    return __sync_bool_compare_and_swap(&sem->value, 0, 1);
}

// True if unlock was succesfull false otherwise
bool android_unlock(android_semaphore* sem)
{
    return __sync_bool_compare_and_swap(&sem->value, 1, 0);
}

const char* android_required_vk_extensions[] = {VK_E_KHR_SURFACE_NAME, VK_E_KHR_ANDROID_SURFACE_NAME};

float SCROLL_MULTIPLIER; 

struct android_platform_state
{
    pthread_t app_thread;
    pthread_mutex_t app_thread_mutex;
    pthread_cond_t app_thread_pause;
    bool app_thread_paused;
    jobject activity;
    jclass activity_class;
    JavaVM* java_vm;
    
    AAssetManager* asset_manager;
    Arena master_arena;
    PlatformWindow window;
    bool window_ready;
    bool destroy_requested;
    
    Arena* pointer_arrays;
    Arena* search_results;
    Arena* search_result_values;
    Arena* binary_arena;
    Arena* platform_events[2]; // Two event buffers that we swap out each frame, buffer 0 is for reading and 1 is for writing
    android_semaphore events_mutex;

    Arena* runtime_master_arena;
    VirtualKeyboard keyboard_state;
    
    android_semaphore platform_mutex;
    android_semaphore events_semaphore;
};

android_platform_state platform;

struct android_args
{
    AAssetManager* asset_manager;
    jobject activity;
    jclass activity_class; 
    JavaVM* java_vm;
};

enum class AndroidEventType
{
    KEYBOARD,
    SOFT_KEYBOARD,
    MOUSE_BUTTON,
    MOUSE_SCROLL,
    MOUSE_MOVE,
    GESTURE,
    KEYBOARD_VISIBILITY,
};

enum class MouseButton
{
    LEFT,
    RIGHT,
    MIDDLE,  
};

struct android_event
{
    AndroidEventType type;
    union
    {
        struct
        {
            uint32_t key_code;
            uint32_t unicode_char;
            KeyState action;
        } KeyEvent;
        struct
        {
            uint32_t unicode_char;
            KeyState action;
        } SoftKeyEvent;
        struct
        {
            bool soft_keyboard_shown;
        } KeyboardVisibility;
        struct
        {
            MouseButton button;
            KeyState action;   
        } MouseButton;
        struct
        {
            float x_axis;
            float y_axis;
        } MouseScroll;
        struct
        {
            float x_pos;
            float y_pos;
        } MouseMove;
    };
};

void* android_main(void* args);
void initialize_window();
uint32_t android_consume_utf8_to_utf32(const char* utf8_buffer, uint32_t* codepoint, uint32_t buffer_length); // Returns the number of bytes consumed

extern "C"
{

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1bootstrap(JNIEnv* env, jclass clazz, jobject activity, jobject asset_manager)
{
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    
    android_args* args = (android_args*)malloc(sizeof(android_args));
    args->asset_manager = AAssetManager_fromJava(env, asset_manager);
    args->activity = env->NewGlobalRef(activity);
    args->activity_class = (jclass)env->NewGlobalRef(clazz);
    env->GetJavaVM(&args->java_vm);
    
    pthread_create(&platform.app_thread, &attributes, android_main, (void*)args);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1set_1surface(JNIEnv* env, jclass clazz, jobject surface)
{
    platform.window.window_handle = ANativeWindow_fromSurface(env, surface);
    platform.window_ready = true;
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1window_1changed(JNIEnv *env, jclass clazz, jint width, jint height)
{
    platform.window.flags |= RESIZED_WINDOW;
    platform.window.height = ANativeWindow_getHeight(platform.window.window_handle);
    platform.window.width = ANativeWindow_getWidth(platform.window.window_handle);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1pause(JNIEnv *env, jclass clazz)
{
    platform.window_ready = false;
    
    pthread_mutex_lock(&platform.app_thread_mutex);
    platform.app_thread_paused = true;
    pthread_mutex_unlock(&platform.app_thread_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1resume(JNIEnv *env, jclass clazz)
{
    if(platform.window_ready || !platform.window.window_dom)
    {
        return;
    }
    
    // Note(Leo): This blocking behaviour requires this function to be spun off to a new thread in java otherwise it
    //            blocks the actual function which sets the surface allowing this to stop blocking.
    while(!platform.window_ready)
    {
        ;
    }
    
    initialize_window();
    
    pthread_mutex_lock(&platform.app_thread_mutex);
    platform.app_thread_paused = false;
    pthread_cond_signal(&platform.app_thread_pause);
    pthread_mutex_unlock(&platform.app_thread_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1key(JNIEnv *env, jclass clazz, jint key_code, jint unicode, jint action)
{
    if(!platform.window_ready)
    {
        return;
    }

    while(!android_lock(&platform.events_mutex)){;}
    
    android_event* added = (android_event*)Alloc(platform.platform_events[1], sizeof(android_event));
    added->KeyEvent.key_code = KEYCODE_TRANSLATIONS[(uint32_t)key_code];
    added->KeyEvent.unicode_char = (uint32_t)unicode;
    added->KeyEvent.action = (KeyState)action;
    added->type = AndroidEventType::KEYBOARD;
    
    android_unlock(&platform.events_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1soft_1keyboard(JNIEnv *env, jclass clazz, jboolean is_shown)
{
    if(!platform.window_ready)
    {
        return;
    }

    while(!android_lock(&platform.events_mutex)){;}
    
    android_event* added = (android_event*)Alloc(platform.platform_events[1], sizeof(android_event));
    added->KeyboardVisibility.soft_keyboard_shown = is_shown;
    added->type = AndroidEventType::KEYBOARD_VISIBILITY;
    
    android_unlock(&platform.events_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1soft_1key(JNIEnv *env, jclass clazz, jstring entered)
{
    if(!platform.window_ready)
    {
        return;
    }

    while(!android_lock(&platform.events_mutex)){;}
    
    const char* entered_string = env->GetStringUTFChars(entered, NULL);
    char* curr_char = (char*)entered_string;
    uint32_t buffer_len = (uint32_t)strlen(entered_string);
    
    // Note(Leo): The soft keyboard doesnt send individual events, it sends collected whole pieces of text
    //            we simulate each char as a virtual key press for convieniece.
    while(buffer_len)
    {
        uint32_t codepoint = 0;
        uint32_t consumed = android_consume_utf8_to_utf32(curr_char, &codepoint, buffer_len);
        
        // Hit some type of invalid codepoint
        if(!consumed)
        {
            buffer_len -= 1;
            curr_char += 1;
            continue;
        }
        
        buffer_len -= consumed;
        curr_char += consumed;
        
        android_event* added = (android_event*)Alloc(platform.platform_events[1], sizeof(android_event));
        added->KeyEvent.key_code = K_VIRTUAL;
        added->KeyEvent.unicode_char = codepoint;
        
        // Note(Leo): The soft keyboard doesnt actually notify us about ANYTHING so this is the best we can realistically do
        added->KeyEvent.action = KeyState::DOWN;
        added->type = AndroidEventType::SOFT_KEYBOARD;
    }
    
    android_unlock(&platform.events_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1mouse_1button(JNIEnv *env, jclass clazz, jint button, jint action)
{
    if(!platform.window_ready)
    {
        return;
    }

    while(!android_lock(&platform.events_mutex)){;}
    
    android_event* added = (android_event*)Alloc(platform.platform_events[1], sizeof(android_event));
    added->MouseButton.button = (MouseButton)button;
    added->MouseButton.action = (KeyState)action;
    added->type = AndroidEventType::MOUSE_BUTTON;
    
    android_unlock(&platform.events_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1mouse_1scroll(JNIEnv *env, jclass clazz, jfloat x_axis, jfloat y_axis)
{
    if(!platform.window_ready)
    {
        return;
    }

    while(!android_lock(&platform.events_mutex)){;}
    
    android_event* added = (android_event*)Alloc(platform.platform_events[1], sizeof(android_event));
    added->MouseScroll.x_axis = (float)x_axis;
    added->MouseScroll.y_axis = (float)y_axis;
    added->type = AndroidEventType::MOUSE_SCROLL;
    
    android_unlock(&platform.events_mutex);
}

JNIEXPORT void JNICALL
Java_com_example_reactivecppmarkup_ExtendedNative_android_1on_1mouse_1move(JNIEnv *env, jclass clazz, jfloat x, jfloat y)
{
    if(!platform.window_ready)
    {
        return;
    }

    while(!android_lock(&platform.events_mutex)){;}
    
    android_event* added = (android_event*)Alloc(platform.platform_events[1], sizeof(android_event));
    added->MouseMove.x_pos = (float)x;
    added->MouseMove.y_pos = (float)y;
    added->type = AndroidEventType::MOUSE_MOVE;
    
    android_unlock(&platform.events_mutex);
}

}

void android_show_soft_keyboard()
{
    JNIEnv* java_env;
    if(platform.java_vm->GetEnv((void**)&java_env, JNI_VERSION_1_6) != JNI_EDETACHED)
    {
        return;
    }
    if(platform.java_vm->AttachCurrentThread(&java_env, NULL) != JNI_OK)
    {
        return;
    }
    
    jmethodID method = java_env->GetMethodID(platform.activity_class, "show_soft_keyboard", "()V");
    java_env->CallVoidMethod(platform.activity, method);
    
    platform.java_vm->DetachCurrentThread();
}

void android_hide_soft_keyboard()
{
    JNIEnv* java_env;
    if(platform.java_vm->GetEnv((void**)&java_env, JNI_VERSION_1_6) != JNI_EDETACHED)
    {
        return;
    }
    if(platform.java_vm->AttachCurrentThread(&java_env, NULL) != JNI_OK)
    {
        return;
    }

    jmethodID method = java_env->GetMethodID(platform.activity_class, "hide_soft_keyboard", "()V");
    java_env->CallVoidMethod(platform.activity, method);
    
    platform.java_vm->DetachCurrentThread();
}

void android_swap_event_buffers()
{
    while(!android_lock(&platform.events_mutex)){;}
    Arena* a = platform.platform_events[1];
    platform.platform_events[1] = platform.platform_events[0];
    platform.platform_events[0] = a;
    
    // Clearing the buffer thats going to get written to
    ResetArena(platform.platform_events[1]);
    android_unlock(&platform.events_mutex);
}

void initialize_window()
{
    platform.window.height = ANativeWindow_getHeight(platform.window.window_handle);
    platform.window.width = ANativeWindow_getWidth(platform.window.window_handle);
    android_vk_create_window_surface(&platform.window);
}

// Updates conrol state button states from THIS_FRAME to normal
// Updates scroll to 0
void android_update_control_state()
{
    if(platform.window.controls.mouse_left_state == MouseState::DOWN_THIS_FRAME)
    {
        platform.window.controls.mouse_left_state =  MouseState::DOWN;
    } 
    else if(platform.window.controls.mouse_left_state == MouseState::UP_THIS_FRAME)
    {
        platform.window.controls.mouse_left_state =  MouseState::UP;
    }
    
    if(platform.window.controls.mouse_middle_state == MouseState::DOWN_THIS_FRAME)
    {
        platform.window.controls.mouse_middle_state = MouseState::DOWN;
    }
    else if(platform.window.controls.mouse_middle_state == MouseState::UP_THIS_FRAME)
    {
        platform.window.controls.mouse_middle_state = MouseState::UP;
    }
    
    if(platform.window.controls.mouse_right_state == MouseState::DOWN_THIS_FRAME)
    {
        platform.window.controls.mouse_right_state = MouseState::DOWN;
    }
    else if(platform.window.controls.mouse_right_state == MouseState::UP_THIS_FRAME)
    {
        platform.window.controls.mouse_right_state = MouseState::UP;
    }
    
    platform.window.controls.scroll_dir = { 0.0, 0.0 };
}

void android_search_dir(Arena* results, Arena* result_values, const char* dir_name, const char* file_extension)
{
    // Note(Leo): The android assetmanager doesnt provide an interface for recursively searching a dir so we dont
    //            do that on android.
    AAssetDir* opened_dir = AAssetManager_openDir(platform.asset_manager, dir_name);
    
    int parent_len = strlen(dir_name);
    char* curr_file_name = (char*)AAssetDir_getNextFileName(opened_dir);
    while(curr_file_name)
    {
        int len = strlen(curr_file_name);
        if(strstr(curr_file_name, file_extension))
        {
            FileSearchResult* added = (FileSearchResult*)Alloc(results, sizeof(FileSearchResult));
            added->file_path = (char*)Alloc(result_values, (parent_len + len + 1) * sizeof(char)); // +1 to fit \0
            
            memcpy(added->file_path, dir_name, parent_len*sizeof(char));
            memcpy((added->file_path + parent_len), curr_file_name, len*sizeof(char));
            added->file_path[len + parent_len] = '\0';
            
            added->file_name = added->file_path + parent_len;
        }
        
        curr_file_name = (char*)AAssetDir_getNextFileName(opened_dir);
    }
    
}


void android_process_window_events()
{
    Arena* events = platform.platform_events[0];
    uint32_t event_count = (android_event*)events->next_address - (android_event*)events->mapped_address;
 
    android_update_control_state();
    
    for(uint32_t i = 0; i < event_count; i++)
    {
        android_event* curr_event = (android_event*)events->mapped_address + i;
        switch(curr_event->type)
        {
            case(AndroidEventType::KEYBOARD):
            {
                Event* added = PushEvent((DOM*)platform.window.window_dom);
                added->Key.code = curr_event->KeyEvent.key_code;
                added->Key.key_char = curr_event->KeyEvent.unicode_char;
                if(curr_event->KeyEvent.action == KeyState::DOWN)
                {
                    added->type = EventType::KEY_DOWN;
                }
                else
                {
                    added->type = EventType::KEY_UP;
                }
                
                platform.window.controls.keyboard_state->keys[(uint8_t)curr_event->KeyEvent.key_code] = (uint8_t)curr_event->KeyEvent.action;
                
                break;
            }
            case(AndroidEventType::SOFT_KEYBOARD):
            {
                Event* added = PushEvent((DOM*)platform.window.window_dom);
            
                added->Key.code = curr_event->KeyEvent.key_code;
                added->Key.key_char = curr_event->KeyEvent.unicode_char;
                added->type = EventType::KEY_DOWN;
                //__android_log_print(ANDROID_LOG_INFO, "Vulkan Tutorials", "%c", (char)added->Key.key_char);
                
                break;
            }
            case(AndroidEventType::MOUSE_MOVE):
            {
                float current_x = platform.window.controls.cursor_pos.x;
                float current_y = platform.window.controls.cursor_pos.y;
                platform.window.controls.cursor_delta = {curr_event->MouseMove.x_pos - current_x, curr_event->MouseMove.y_pos - current_y};
                
                platform.window.controls.cursor_pos = {curr_event->MouseMove.x_pos, curr_event->MouseMove.y_pos};
                
                break;
            }
            case(AndroidEventType::MOUSE_SCROLL):
            {
                platform.window.controls.scroll_dir = {curr_event->MouseScroll.x_axis, curr_event->MouseScroll.y_axis};
                
                break;
            }
            case(AndroidEventType::KEYBOARD_VISIBILITY):
            {
                if(curr_event->KeyboardVisibility.soft_keyboard_shown)
                {
                    
                }
                else
                {
                
                }
                
                break;
            }
            default:
            {
                break;
            }
        }
    }
}

FILE* android_open_relative_file_path(Arena* binary_arena, const char* relative_path, const char* open_params)
{
    AAsset* opened = AAssetManager_open(platform.asset_manager, relative_path, AASSET_MODE_BUFFER);
    uint32_t opened_size = AAsset_getLength(opened);
    void* allocated = Alloc(binary_arena, opened_size*sizeof(char));
    AAsset_read(opened, allocated, opened_size);
    AAsset_close(opened);
    FILE* created_file = fmemopen(allocated, opened_size, open_params);
    return created_file;
}

FileSearchResult* android_find_markup_binaries(Arena* binary_arena, Arena* search_results_arena, Arena* search_result_values_arena)
{
    android_search_dir(search_results_arena, search_result_values_arena, "", ".bin");
    FileSearchResult* first = (FileSearchResult*)search_results_arena->mapped_address;
       
    // Open all the files
    FileSearchResult* curr = first;
    while(curr->file_path)
    {
        curr->file = android_open_relative_file_path(binary_arena, curr->file_path, "rb");
        curr++;
    }
    
    return first;
}

FileSearchResult* android_find_image_resources(Arena* search_results_arena, Arena* search_result_values_arena)
{
    FileSearchResult* first = (FileSearchResult*)search_results_arena->next_address;

    #define RESOURCE_DIR_NAME "resources/images/"    
    android_search_dir(search_results_arena, search_result_values_arena, RESOURCE_DIR_NAME, "");
    
    return first;
}

// Returns the number of bytes that were consumed
uint32_t android_consume_utf8_to_utf32(const char* utf8_buffer, uint32_t* codepoint, uint32_t buffer_length)
{
    if(buffer_length < 1)
    {
        return 0;
    }
    
    if((utf8_buffer[0] & 0b10000000) == 0) 
    {
        *codepoint = (utf8_buffer[0] & 0b01111111);
        return 1;
    }
    else if((utf8_buffer[0] & 0b11100000) == 0b11000000)
    {
        if(buffer_length < 2)
        {
            return 0;
        }
        
        *codepoint = (utf8_buffer[0] & 0b00011111) << 6 | (utf8_buffer[1] & 0b00111111);
        return 2;
    }
    else if((utf8_buffer[0] & 0b11110000) == 0b11100000)
    {
        if(buffer_length < 3)
        {
            return 0;
        }
        
        *codepoint = (utf8_buffer[0] & 0b00001111) << 12 | (utf8_buffer[1] & 0b00111111) << 6 | (utf8_buffer[2] & 0b00111111);
        return 3;
    }
    else
    {
        if(buffer_length < 4)
        {
            return 0;
        }
        
        *codepoint = (utf8_buffer[0] & 0b00000111) << 18 | (utf8_buffer[1] & 0b00111111) << 12 | (utf8_buffer[2] & 0b00111111) << 6 | (utf8_buffer[3] & 0b00111111);
        return 4;
    }
    
    return 0;
}



void* android_main(void* arguments)
{
    platform = {};
    SCROLL_MULTIPLIER = 30.0f;
    android_args* args = (android_args*)arguments;
    
    platform.asset_manager = args->asset_manager;
    platform.activity = args->activity;
    platform.activity_class = args->activity_class;
    platform.java_vm = args->java_vm;
    
    platform.app_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
    platform.app_thread_paused = PTHREAD_COND_INITIALIZER;
    platform.app_thread_paused = false;
    
    InitScratch(sizeof(char)*1000000);
    platform.master_arena = CreateArena(1000*sizeof(Arena), sizeof(Arena));
    platform.binary_arena = (Arena*)Alloc(&platform.master_arena, sizeof(Arena));
    *(platform.binary_arena) = CreateArena(5000000*sizeof(char), sizeof(char));

    InitializeFontPlatform(&(platform.master_arena), 0);
    PlatformInitKeycodeTranslations();

    FILE* default_font = android_open_relative_file_path(platform.binary_arena, "resources/fonts/default.ttf", "rb");
    FontPlatformLoadFace("platform_default_font.ttf", default_font);
    fclose(default_font);
    ResetArena(platform.binary_arena);

    FILE* combined_shader = android_open_relative_file_path(platform.binary_arena, "compiled_shaders/combined_shader.spv", "rb");
        
    if(!combined_shader)
    {
        printf("Error: Shaders could not be loaded!\n");
        return NULL;
    }
    
    // Note(Leo): We need to wait to be signalled that the window has been created.
    while(!platform.window_ready)
    {
        ;
    }
    
    int required_extension_count = sizeof(android_required_vk_extensions) / sizeof(char**);
    InitializeVulkan(&(platform.master_arena), android_required_vk_extensions, required_extension_count, combined_shader);
    
    fclose(combined_shader);
    ResetArena(platform.binary_arena);

    platform.window.controls.keyboard_state = &platform.keyboard_state;
    initialize_window();
    
    platform.search_results = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.search_results) = CreateArena(sizeof(FileSearchResult)*1000, sizeof(FileSearchResult));
    
    platform.search_result_values = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.search_result_values) = CreateArena(sizeof(char)*100000, sizeof(char));
    
    FileSearchResult* first_binary = android_find_markup_binaries(platform.binary_arena, platform.search_results, platform.search_result_values);
    
    platform.runtime_master_arena = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.runtime_master_arena) = CreateArena(sizeof(Arena)*1000, sizeof(Arena));
    
    platform.platform_events[0] = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.platform_events[0]) = CreateArena(sizeof(android_event)*1000, sizeof(android_event));
    platform.platform_events[1] = (Arena*)Alloc(&(platform.master_arena), sizeof(Arena));
    *(platform.platform_events[1]) = CreateArena(sizeof(android_event)*1000, sizeof(android_event));
    
    InitializeRuntime(platform.runtime_master_arena, first_binary);
    ResetArena(platform.binary_arena);
    
    if(!RuntimeInstanceMainPage())
    {
        printf("ERROR: Failed to initialize main page! Is the binary missing?\n");
        return NULL;
    }
    
    FileSearchResult* first_image = android_find_image_resources(platform.search_results, platform.search_result_values);
    FileSearchResult* curr_image = first_image;
    
    while(curr_image->file_path)
    {
        FILE* opened = android_open_relative_file_path(platform.binary_arena, curr_image->file_path, "rb");
        RenderplatformLoadImage(opened, curr_image->file_name);
        
        fclose(opened);
        ResetArena(platform.binary_arena);
        curr_image++;
    }
    
    Arena* temp_renderque = (Arena*)Alloc(runtime.master_arena, sizeof(Arena), zero());
    *temp_renderque = CreateArena(sizeof(Element) * 10000, sizeof(Element));
    
    while(true)
    {
        pthread_mutex_lock(&platform.app_thread_mutex);
        while(platform.app_thread_paused)
        {
            pthread_cond_wait(&platform.app_thread_pause, &platform.app_thread_mutex);    
        }
        pthread_mutex_unlock(&platform.app_thread_mutex);
    
        android_swap_event_buffers();
        
        android_process_window_events();
    
        if(platform.window.flags)
        {
            if(platform.window.flags & RESIZED_WINDOW)
            {
                vk_window_resized(&platform.window);
                platform.window.flags = 0;
                
                continue;
            }
            if(platform.window.flags & DEAD_WINDOW)
            {
                if(RenderplatformSafeToDelete(&platform.window))
                {
                    break;
                }
            }
            continue;     
        }
        
        if(platform.destroy_requested)
        {
            platform.window.flags = DEAD_WINDOW;
        }
        Arena* final_renderque = RuntimeTickAndBuildRenderque(temp_renderque, (DOM*)platform.window.window_dom, &platform.window.controls, platform.window.width, platform.window.height);
        RenderplatformDrawWindow(&platform.window, final_renderque);
        ResetArena(temp_renderque);
        RuntimeClearTemporal((DOM*)platform.window.window_dom);

    }
    
    return NULL;
}


void PlatformRegisterDom(void* dom)
{
    platform.window.window_dom = dom;
}
#endif
