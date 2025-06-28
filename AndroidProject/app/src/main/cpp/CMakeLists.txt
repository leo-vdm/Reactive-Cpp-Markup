
cmake_minimum_required(VERSION 3.22.1)
set(${CMAKE_CXX_FLAGS}, "${CMAKE_CXX_FLAGS} -fPIC")

project("rcm")

find_library(log-lib
        log)

add_compile_definitions(FT2_BUILD_LIBRARY)

add_library(${CMAKE_PROJECT_NAME} SHARED
        backend/file_system.cpp backend/runtime.cpp backend/arena.cpp backend/arena_string.cpp backend/DOM.cpp
        backend/platform_android.cpp backend/platform_vulkan.cpp backend/harfbuzz_module.cpp backend/platform_font.cpp
        backend/shaping_platform.cpp backend/freetype_module.cpp
        generated/dom_attatchment.cpp)

if (${ANDROID_ABI} STREQUAL "armeabi-v7a")
    add_definitions("-DHAVE_ARM_7")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -mfpu=neon)
elseif (${ANDROID_ABI} STREQUAL "x86")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -g -msse -maes -fsanitize=undefined -fsanitize-trap=undefined)
elseif (${ANDROID_ABI} STREQUAL "x86_64")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -g -msse -maes -fsanitize=undefined -fsanitize-trap=undefined)
else ()
    add_definitions("-DHAVE_ARM_8")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -mfpu=neon)
endif ()

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC backend/third_party/freetype/freetype-2.13.3/include generated)

target_link_libraries(${CMAKE_PROJECT_NAME}
        # List libraries link to the target library
        android
        nativewindow
        #native_app_glue
        ${log-lib})
