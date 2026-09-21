#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <tiny_gltf.h>
#include <vk_mem_alloc.h>
#include "engine.h"

int main(int argc, char *argv[]) {
	Engine engine;

	engine.init();

	engine.run();

	engine.cleanup();

	return 0;
}