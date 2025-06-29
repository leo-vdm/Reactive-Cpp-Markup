# Reactive C++ Markup
A standalone UI framework aiming to create an experience similiar to many javascript frameworks except native and with C++.
## Who its for
RCM is meant for devs who want a familiar UI dev experience but would like to use C++ or become more familiar with it. RCM aims to implement most of the annoying platform/rendering boilerplate in order to provide a more complete out of the box experience (similiar to that of web technologies).

## Is it ready yet?
NO, RCM is certainly not ready and is still under heavy development. It is however in a state that is easy enough to play around with and make some simple apps.

## What platform are/will be supported
Currently RCM can compile for Windows/Linux/Android, in the future I aim to support a web version using WebAssembly and WebGPU.

## How does it work
RCM uses a compiler to create an intermediate representation of markup files and generate supporting C++ code. A runtime then reads these markup binaries and creates a tree representation which it uses to draw your UI each frame.
To render each frame RCM walks the markup tree and generates a set of shaping primitives which it uses to calculate sizing. After calculating sizing a set of render primitives are generated and uploaded to the GPU where a Vulkan compute pipeline
finally renders them.

To this end RCM could be considered an immediate mode UI framework as rendering/sizing is calculated each frame rather than on an event/signal basis.

## Compilation
To ensure your build will work properly its best to emulate the structure of test_src and test_build in this repo.
1. Download a source version.
2. Run setup.bat (for windows) or setup.sh (for linux) to unpack the dependency archives.
3. Run build.bat (for windows) or build.sh (for linux).

Keep in mind that in order to use MSVC in the command line on windows you must first execute vcvarsall.bat (or one of those scripts that Microsoft ships) since Microsoft is apparently incapable of adding their compiler to the system path.

The build scripts shipped with this repo actually build 3 targets. First the compiler executable, then the runtime library and finally the executable for your application.
Obviously you dont need to rebuild the compiler or runtime each time you want to build your app so I recommend creating your own build script which only runs the compiler and compiles/links your app's executable.

The build scripts also try to compile the rendering compute shader but this will fail if you dont have the vulkan SDK. If you dont want to download the vulkan SDK you can simply use the pre-compiled combined_shader.spv file included in the release.

## Android Build
To build your application for android you must run the compiler on windows/linux to get the generated C++ and .bin files.
Ensure that you have installed the NDK in Android Studio.
Open the AndroidProject folder in Android Studio and do the following:
1. Copy the generated .cpp files to the src/main/cpp/generated folder (including overloads.cpp and dom_attatchment.cpp).
2. Copy your .bin files and other folders in your build directory into the src/main/assets directory.
2. Copy a version of the backend/ folder to the src/main/cpp/backend folder (make sure that the deps in third_party have been unpacked).
3. Copy the .h files in backend/ to src/main.cpp/generated
4. Build the project in Android Studio and pray that it works.

## Distribution
In order for your app executable to run you must create a folder structure like test_build. Markup .bin files go in the top level along with the executable and images/fonts go in their specified folders.
The compiled_shaders directory is also required and should contain combined_shader.spv
Android is an exception since they have more strict file acces rules. In android the files all go in the assets directory in the same structure they would usually be.

## Dependencies
All dependencies are included as static source archives in the backend/third_party folder.
The only thing not included is the Vulkan SDK which is required if you would like to compile the shader for yourself. The vulkan SDK is NOT required otherwise, the runtime will dynamically get the Vulkan driver from the OS.
