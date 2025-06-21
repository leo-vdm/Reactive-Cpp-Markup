
cmake_minimum_required(VERSION 3.22.1)
set(${CMAKE_C_FLAGS}, "${CMAKE_C_FLAGS} -fPIC")


project("rcm")

#add_library(native_app_glue STATIC
#        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
#target_include_directories(native_app_glue PUBLIC
#        ${ANDROID_NDK}/sources/android/native_app_glue)

find_library(log-lib
        log)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
add_compile_definitions(FT2_BUILD_LIBRARY)

add_library(${CMAKE_PROJECT_NAME} SHARED
        backend/file_system.cpp backend/runtime.cpp backend/arena.cpp backend/arena_string.cpp backend/DOM.cpp
        backend/platform_android.cpp backend/platform_vulkan.cpp backend/harfbuzz_module.cpp backend/platform_font.cpp
        backend/shaping_platform.cpp backend/freetype_module.cpp
        generated/dom_attatchment.cpp)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC backend/third_party/freetype/freetype-2.13.3/include generated)

target_link_libraries(${CMAKE_PROJECT_NAME}
        # List libraries link to the target library
        android
        nativewindow
        #native_app_glue
        ${log-lib})
