// The single translation unit that compiles the tinygltf implementation.
//
// TINYGLTF_NO_INCLUDE_STB_IMAGE{,_WRITE} (set PRIVATE on the tinygltf target)
// stops tiny_gltf.h from pulling in the stb headers vendored beside it; we
// include the stb target's copies instead, so the whole build agrees on one
// version of stb, defined exactly once in stb_impl.cpp.
#include <stb_image.h>
#include <stb_image_write.h>

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>
