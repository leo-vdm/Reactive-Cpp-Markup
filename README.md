# Reactive C++ Markup
A standalone UI framework aiming to create an experience similiar to many javascript frameworks except native and with C++.
## Who its for
RCM is meant for devs who want a familiar UI dev experience but would like to use C++ or become more familiar with it. RCM aims to implement most of the annoying platform/rendering boilerplate in order to provide a more complete out of the box experience (similiar to that of web technologies).

## Is it ready yet?
NO, RCM is certainly not ready and is still under heavy development. It is however in a state that is easy enough to play around with and make some simple apps.

## What platform are/will be supported
Currently RCM can compile for Windows/Linux/Android, in the future I aim to support a web version as well using WebAssembly and WebGPU.

## How does it work
RCM uses a compiler to create an intermediate representation of markup files and generate supporting C++ code. A runtime then reads these markup binaries and creates a tree representation which it uses to draw your UI each frame.
