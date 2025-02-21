# Example usage:
#  mkdir build
#  cd build
#  cmake -g "Visual Studio 14 2015" ..

cmake_minimum_required(VERSION 2.8)
project(imgui_example_glfw_vulkan C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVK_PROTOTYPES -pthread")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_PROTOTYPES -finput-charset=utf-8 -DUNICODE -pthread -Og -DDEBUG")

# Libraries
#find_package(Vulkan REQUIRED)
#find_library(VULKAN_LIBRARY
  #NAMES vulkan vulkan-1)
#set(LIBRARIES "glfw;${VULKAN_LIBRARY}")
#set(LIBRARIES "glfw;Vulkan::Vulkan")

# Use vulkan headers from glfw:
#include_directories(${GLFW_DIR}/deps)

include_directories(/usr/local/include/freetype2) 

file(GLOB runtime_sources backend/runtime.cpp backend/arena.cpp backend/arena_string.cpp backend/DOM.cpp backend/platform_linux.cpp backend/platform_windows.cpp backend/graphics_types.cpp backend/platform_vulkan.cpp backend/harfbuzz_module.cpp backend/platform_font.cpp)
#add_executable(runtime ${runtime_sources})
add_library(runtime ${runtime_sources})

#add_executable(example_glfw_vulkan ${runtime_sources} ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_demo.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp)
#target_link_libraries(example_glfw_vulkan ${LIBRARIES})
#target_compile_definitions(example_glfw_vulkan PUBLIC -DImTextureID=ImU64)

file(GLOB compiler_sources backend/compiler.cpp backend/lexer.cpp backend/parser.cpp backend/arena.cpp backend/arena_string.cpp backend/prepass.cpp backend/codegen.cpp backend/file_system.cpp)

add_executable(compiler ${compiler_sources})
