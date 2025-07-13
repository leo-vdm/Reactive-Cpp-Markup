@echo off
pushd %~dp0
:: Shaders
glslc.exe backend\shaders\combined_shader.comp -o backend\shaders\combined_shader.spv
IF %ERRORLEVEL% NEQ 0 (
	echo:
	echo Could not compile shaders...
	EXIT /B
)

pushd build
:: Compile all files
set dep_dir=..\backend\third_party
set src_dir=..\backend

:: Debug build
cl /MP12 /Zi /Od /I. /I%dep_dir%\vulkan\Vulkan-Headers-1.4.317\include /I%dep_dir%\freetype\freetype-2.13.3\include -DFT2_BUILD_LIBRARY /EHsc /c %src_dir%\freetype_module.cpp %src_dir%\compiler.cpp %src_dir%\lexer.cpp %src_dir%\parser.cpp %src_dir%\arena.cpp %src_dir%\arena_string.cpp %src_dir%\prepass.cpp %src_dir%\codegen.cpp %src_dir%\runtime.cpp %src_dir%\DOM.cpp %src_dir%\file_system.cpp %src_dir%\platform_windows.cpp %src_dir%\platform_vulkan.cpp %src_dir%\platform_font.cpp %src_dir%\harfbuzz_module.cpp %src_dir%\shaping_platform.cpp 

:: Release build
::cl /MP12 /O2t /GL /I%dep_dir%\vulkan\Vulkan-Headers-1.4.317\include /I%dep_dir%\freetype\freetype-2.13.3\include -DFT2_BUILD_LIBRARY /EHsc /c %src_dir%\freetype_module.cpp %src_dir%\compiler.cpp %src_dir%\lexer.cpp %src_dir%\parser.cpp %src_dir%\arena.cpp %src_dir%\arena_string.cpp %src_dir%\prepass.cpp %src_dir%\codegen.cpp %src_dir%\runtime.cpp %src_dir%\DOM.cpp %src_dir%\file_system.cpp %src_dir%\platform_windows.cpp %src_dir%\platform_vulkan.cpp %src_dir%\platform_font.cpp %src_dir%\harfbuzz_module.cpp %src_dir%\shaping_platform.cpp 

IF %ERRORLEVEL% NEQ 0 (
	echo:
	echo Compile error...
	EXIT /B
)

:: Link the .lib
lib /nologo /out:runtime.lib freetype_module.obj runtime.obj arena.obj arena_string.obj DOM.obj platform_windows.obj platform_vulkan.obj file_system.obj platform_font.obj harfbuzz_module.obj shaping_platform.obj
xcopy /y /s runtime.lib ..\test_build

:: Link the compiler .exe
link /debug:FULL /nologo /out:compiler.exe compiler.obj lexer.obj parser.obj arena.obj arena_string.obj prepass.obj codegen.obj file_system.obj

IF %ERRORLEVEL% NEQ 0 (
	echo:
	echo Linker error...
	EXIT /B
)

popd

xcopy /y backend\*.h test_build
xcopy /y backend\file_system.cpp test_build
xcopy /y backend\overloads.cpp test_build
xcopy /y backend\shaders\*.spv test_build\compiled_shaders

:: Running compiler
build\compiler.exe test_src test_build

IF %ERRORLEVEL% NEQ 0 (
	echo:
	echo Error while running compiler...
	EXIT /B
)

:: Building app
pushd test_build
:: Debug build
cl /MP12 /Zi /Od /EHsc /c dom_attatchment.cpp /I. /I%dep_dir%\vulkan\Vulkan-Headers-1.4.317\include /I%dep_dir%\freetype\freetype-2.13.3\include
:: Release build
::cl /MP12 /O2t /GL /EHsc /c dom_attatchment.cpp /I. /I%dep_dir%\vulkan\Vulkan-Headers-1.4.317\include /I%dep_dir%\freetype\freetype-2.13.3\include

IF %ERRORLEVEL% NEQ 0 (
	echo:
	echo Compiler error...
	EXIT /B
)

LINK /DEBUG:FULL *.obj *.lib user32.lib Gdi32.lib /OUT:test.exe

IF %ERRORLEVEL% NEQ 0 (
	echo:
	echo Linker error...
	EXIT /B
)

popd

popd