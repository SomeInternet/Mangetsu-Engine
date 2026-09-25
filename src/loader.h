#pragma once
#include "engine.h"

namespace loader {
	Scene loadScene(VkDevice &device, Allocator &allocator, const std::string &file, vkb::DispatchTable &dispatchTable);

	void destroyScene(VkDevice &device, Allocator &allocator, Scene &scene);
};