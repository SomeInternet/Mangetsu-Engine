// The single translation unit that compiles the stb implementations.
//
// Built as C++ rather than C so that the definitions get the same linkage as
// every call site in the engine (and inside tinygltf, which is C++ too).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stb_image.h>
#include <stb_image_write.h>
