@echo off
pushd %~dp0
:: Shaders
D:\Vulkan\Bin\glslc.exe shaders\combined_shader.comp -o shaders\combined_shader.spv

pushd build
:: Compile all files
cl /ID:\Vulkan\Include /ID:\tmp\freetype\include /Zi /EHsc /c ../compiler.cpp ../lexer.cpp ../parser.cpp ../arena.cpp ../arena_string.cpp ../prepass.cpp ../codegen.cpp ../file_system.cpp ../runtime.cpp ../DOM.cpp ../file_system.cpp ../graphics_types.cpp ../platform_linux.cpp ../platform_windows.cpp ../platform_vulkan.cpp ../platform_font.cpp ../harfbuzz_module.cpp ../shaping_platform.cpp 

:: Link the .lib
lib /nologo /out:runtime.lib runtime.obj arena.obj arena_string.obj DOM.obj platform_linux.obj platform_windows.obj platform_vulkan.obj file_system.obj platform_font.obj graphics_types.obj harfbuzz_module.obj shaping_platform.obj
xcopy /y /s runtime.lib ..\test_build

:: Link the compiler .exe
link /debug:FULL /nologo /out:compiler.exe compiler.obj lexer.obj parser.obj arena.obj arena_string.obj prepass.obj codegen.obj file_system.obj

popd

xcopy /y *.h test_build
xcopy /y file_system.cpp test_build
xcopy /y overloads.cpp test_build
xcopy /y shaders\*.spv test_build\compiled_shaders

:: Running compiler
build\compiler.exe test_src test_build

:: Building app
pushd test_build
cl /Zi /EHsc /c dom_attatchment.cpp /I. /ID:\Vulkan\Include /ID:\tmp\freetype\include
LINK /DEBUG:FULL *.obj *.lib user32.lib Gdi32.lib D:\Vulkan\Lib\vulkan-1.lib D:\tmp\freetype\freetype.lib /OUT:test.exe
popd

popd