#!/usr/bin/env bash
cd "$(dirname "$0")"

glslc backend/shaders/combined_shader.comp -o backend/shaders/combined_shaders.spv

cd build

dep_dir="../backend/third_party"
src_dir="../backend"
g++ -g -c -I$dep_dir/vulkan/Vulkan-Headers-1.4.317/include -I$dep_dir/freetype/freetype-2.13.3/include -DFT2_BUILD_LIBRARY $src_dir/*.cpp

## Link the library ##
ar rvs runtime.a freetype_module.o runtime.o arena.o arena_string.o DOM.o platform_linux.o platform_vulkan.o file_system.o platform_font.o harfbuzz_module.o shaping_platform.o

## Link the compiler executable ##
g++ -g -o compiler compiler.o lexer.o parser.o arena.o arena_string.o prepass.o codegen.o file_system.o

## Copy files over to the application ##
cd ..
cp backend/*.h test_build/
cp backend/file_system.cpp test_build/
cp backend/overloads.cpp test_build/
cp backend/shaders/*.spv test_build/compiled_shaders/
cp build/runtime.a test_build/

# Compiling the app's markup #
build/compiler test_src test_build
cd test_build

# Building the app #
g++ -g -c -I. -I$dep_dir/vulkan/Vulkan-Headers-1.4.317/include -I$dep_dir/freetype/freetype-2.13.3/include dom_attatchment.cpp

# Linking the app #
g++ -o test *.o *.a -g -ldl -lpthread -lX11
