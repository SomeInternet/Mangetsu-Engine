#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct PushConstantsPathtracer {
	glm::vec3 camPos;
	glm::vec3 camForward;
	glm::vec3 camRight;
	glm::vec3 camUp;

	VkDeviceAddress deviceSubMeshes;
	VkDeviceAddress materials;
	VkDeviceAddress textures;
	VkDeviceAddress tlas;

	float fov;
	uint32_t frameNum;
	uint32_t maxBounces{ 10 };

	float lensRadius{ 0.f };
	float focusDist{ 1.f };

	//TODO: Add things like depth of field, or environment color?
};

struct PushConstantsPost {
	uint32_t outputImageIdx;
};