@echo off
call "D:\Visual-Studio\Community\VC\Auxiliary\Build\vcvarsall.bat" x86_amd64
pushd %~dp0
cd ..
pushd new_build
set src_dir=..\new_backend
cl -arch:AVX2 /MP12 /Zi /Od /I. -DPLATFORM_WINDOWS#1 /EHsc /c %src_dir%\test_program.cpp

link /debug:FULL /nologo /out:test_program.exe test_program.obj user32.lib Gdi32.lib

popd
popd