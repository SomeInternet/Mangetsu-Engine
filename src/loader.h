#pragma once
#include "engine.h"

namespace loader {
	Scene loadScene(VkDevice &device, Allocator &allocator, const std::string &file, DeviceProperties &deviceProperties, vkb::DispatchTable &dispatchTable);

	void destroyScene(VkDevice &device, Allocator &allocator, Scene &scene, vkb::DispatchTable &dispatchTable);
};