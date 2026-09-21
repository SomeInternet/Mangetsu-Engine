// The single translation unit that compiles the Vulkan Memory Allocator.
//
// VMA's default VMA_STATIC_VULKAN_FUNCTIONS resolves Vulkan entry points at link
// time, which is why the vma target links Vulkan::Vulkan (the loader) rather
// than just Vulkan::Headers.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
