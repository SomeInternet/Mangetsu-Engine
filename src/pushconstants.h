#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct SceneData {
	VkDeviceAddress deviceSubMeshes;
	VkDeviceAddress materials;
	VkDeviceAddress textures;

	VkDeviceAddress lightTriangles;
	VkDeviceAddress lightAliasTable;
	VkDeviceAddress envAliasTable;

	uint32_t nLights{ 0 };
	float totalLightWeight{ 0.f };
	float totalEnvWeight{ 0.f };
};

struct PushConstantsPathtracer {
	glm::vec3 camPos;
	glm::vec3 camForward;
	glm::vec3 camRight;
	glm::vec3 camUp;

	VkDeviceAddress tlas;
	VkDeviceAddress sceneData;

	float fov;
	uint32_t frameNum;
	uint32_t maxBounces{ 10 };

	float lensRadius{ 0.f };
	float focusDist{ 1.f };
};

struct PushConstantsPost {
	uint32_t outputImageIdx;
};