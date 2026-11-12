/// @file   mathematic.cpp
/// @brief  Anchors the static library for the header-only mathematic module.

// Every public type and function is defined inline in its header. This
// translation unit exists so the archive has a translation unit (and therefore
// a linker language); it deliberately defines nothing else. Including matrix.h
// here also keeps that widely used header in the module's own build.
#include <md/mathematic/matrix.h>
