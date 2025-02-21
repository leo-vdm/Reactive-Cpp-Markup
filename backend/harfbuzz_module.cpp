// Note(Leo): Module to build harfbuzz in seperately since its slow
// Note(Leo): This gets around requiring a build system for harfbuzz since its a pain to setup

#include "harfbuzz/harfbuzz-10.1.0/src/hb.h"
#include "harfbuzz/harfbuzz-10.1.0/src/hb-ft.h"

#define HB_NO_MT 1
#include "harfbuzz/harfbuzz-10.1.0/src/harfbuzz.cc"

#define HAVE_FREETYPE 1
#include "harfbuzz/harfbuzz-10.1.0/src/hb-ft.cc"