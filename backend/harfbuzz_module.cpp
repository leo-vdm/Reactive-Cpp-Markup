// Note(Leo): Module to build harfbuzz in seperately since its slow
// Note(Leo): This gets around requiring a build system for harfbuzz since its a pain to setup
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow=compatible-local"

#include "third_party/harfbuzz/harfbuzz-11.2.1/src/hb.h"
#include "third_party/harfbuzz/harfbuzz-11.2.1/src/hb-ft.h"

#define HB_NO_MT 1
#include "third_party/harfbuzz/harfbuzz-11.2.1/src/harfbuzz.cc"

#define HAVE_FREETYPE 1
#include "third_party/harfbuzz/harfbuzz-11.2.1/src/hb-ft.cc"
#pragma GCC diagnostic pop