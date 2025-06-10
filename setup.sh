#!/usr/bin/env bash
cd "$(dirname "$0")"

cd backend/third_party
cd freetype
unzip ft2133.zip
cd ../harfbuzz
unzip harfbuzz-11.2.1.zip
cd ../vulkan
unzip Vulkan-Headers-1.4.317.zip

