@echo off
pushd %~dp0

pushd backend
pushd third_party

:: Unzip Freetype
pushd freetype
tar -xf ft2133.zip
popd

:: Unzip harfbuzz
pushd harfbuzz
tar -xf harfbuzz-11.2.1.zip
popd

:: Unzip vulkan
pushd vulkan
tar -xf Vulkan-Headers-1.4.317.zip
popd

popd
popd