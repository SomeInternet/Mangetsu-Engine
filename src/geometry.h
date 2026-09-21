#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <vulkan/vulkan.h>

//Nor and tan are octahedral-encoded, and everything is half-precision
struct Vertex {
	glm::uint32 nor;
	glm::uint32 tan;
	glm::uint32 uv;
};

struct Material {
	glm::vec4 color;
	glm::vec4 emission{ 0 }; //Also store emissive strength in here?
	glm::vec3 metallicRoughness{ 0 }; //Also store IoR in here?

	uint16_t colorTexIdx;
	uint16_t metallicRoughnessTexIdx;
	uint16_t norTexIdx;
};

struct SubMesh {
	uint32_t materialIdx;
};

//Adapted from Nvidia's Vulkan ray tracing tutorial
struct AccelerationStructure {
	VkAccelerationStructureKHR _as{};
	AllocatedBuffer _buffer;
};

struct Mesh {
	std::vector<SubMesh> subMeshes;

	AccelerationStructure blas;
};