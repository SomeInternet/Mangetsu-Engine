#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <vulkan/vulkan.h>

#include "allocator.h"

//Nor and tan are octahedral-encoded, and everything is half-precision
struct Vertex {
	glm::uint32 uv{ 0 };
	glm::uint32 nor{ 0 };
	glm::uint32 tan{ 0 };
	float w{ 1.f }; //Handedness of the tangent
};

struct Material {
	glm::vec4 color{ 1.f };
	glm::vec4 emission{ 0.f }; //Also store emissive strength in here?
	float metallic{ 1.f };
	float roughness{ 1.f };
	float transmissiveness{ 0.f };
	float ior{ 1.5f };

	int colorTexIdx{ -1 };
	int metallicRoughnessTexIdx{ -1 };
	int norTexIdx{ -1 };
	int emissionTexIdx{ -1 };
};

struct SubMesh {
	int materialIdx{ -1 };

	uint32_t vertexCount{ 0 };
	uint32_t indexCount{ 0 };
	
	AllocatedBuffer posBuffer;
	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
};

struct DeviceSubMesh {
	VkDeviceAddress pos;
	VkDeviceAddress vertex;
	VkDeviceAddress index;

	int materialIdx{ -1 };
	int padding;
};

//Adapted from Nvidia's Vulkan ray tracing tutorial
struct AccelerationStructure {
	VkAccelerationStructureKHR as{};
	AllocatedBuffer buffer;

	VkDeviceAddress address; //For instancing
};

struct Mesh {
	std::vector<SubMesh> subMeshes;

	AccelerationStructure blas{};
};

struct Texture {
	int imageIdx{ -1 };
	int samplerIdx{ -1 };
};

struct Scene {
	bool loaded{ false };

	AllocatedBuffer materials;

	std::vector<Mesh> meshes;

	std::vector<VkSamplerCreateInfo> samplerCreateInfos; //Descriptor heaps allocate the samplers from the create infos
	std::vector<VkImageViewCreateInfo> imageViewCreateInfos; //Descriptor heaps allocate image views from the create infos

	std::vector<AllocatedImage> images;

	//Buffers for the descriptor heaps for the samplers and images
	AllocatedBuffer samplerHeap;
	AllocatedBuffer imageHeap;

	std::vector<Texture> textures;
	AllocatedBuffer textureBuffer;

	AccelerationStructure tlas{};
};

struct Instance {
	glm::mat4 transform;
	uint32_t mesh;
};